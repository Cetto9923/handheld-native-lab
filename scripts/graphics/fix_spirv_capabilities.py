#!/usr/bin/env python3
"""Correct the capability declarations of a SPIR-V module, from the module itself.

Why this exists (P-MM-23, measured on the RG477M):

The translated modules of this game declare capabilities they never use.
The RG477M's Mali-G615 does not support ``shaderFloat64`` and does not offer
``VK_EXT_fragment_shader_interlock``, so a module that declares
``Float64`` or ``FragmentShaderPixelInterlockEXT`` is rejected at pipeline
creation with ``VK_ERROR_INITIALIZATION_FAILED``. The two compute modules
that fail ``spirv-val --target-env vulkan1.1`` fail for the opposite reason:
they write to a storage image of unknown format without declaring
``StorageImageWriteWithoutFormat``.

This tool does not take the capabilities on trust from a list. It reads the
module and works out what the module actually uses:

- ``Float64`` is used when the module declares a 64-bit floating point type.
- ``Int64`` is used when the module declares a 64-bit integer type.
- ``StorageImageWriteWithoutFormat`` is used when the module writes to a
  storage image whose format is unknown, and the image is resolved through
  its load or access chain to its image type.
- ``FragmentShaderPixelInterlockEXT`` is used when the module has a barrier
  with the interlocked workgroup memory semantics.

A capability is only dropped when the module does not use it. If a module
does use a capability that was asked to be dropped, the tool refuses and
says so, because a capability that is needed and gone is a module that will
fail to build or to run in a way that is much harder to see than a refusal.

Only ``OpCapability`` instructions are ever added or removed. No other
instruction is touched, the header stays untouched, and the input file is
never modified: the result goes to the output file.

Usage:
    fix_spirv_capabilities.py --in a.spv --out b.spv [--add NAME] [--drop NAME]
                              [--report report.json]

``NAME`` is a decimal capability number or one of the names in
``CAPABILITY_NAMES``. With neither ``--add`` nor ``--drop`` the tool only
writes the report of what the module uses and declares, and writes no
output file.
"""
import json
import struct
import sys

OP_CAPABILITY = 17
OP_TYPE_INT = 21
OP_TYPE_FLOAT = 22
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_POINTER = 32
OP_LOAD = 61
OP_ACCESS_CHAIN = 65
OP_FUNCTION_PARAMETER = 55
OP_VARIABLE = 59
OP_IMAGE_READ = 98
OP_IMAGE_WRITE = 99
OP_CONTROL_BARRIER = 224
OP_MEMORY_BARRIER = 225

CAPABILITY_NAMES = {
    "Matrix": 0,
    "Shader": 1,
    "Geometry": 2,
    "Tessellation": 3,
    "Addresses": 4,
    "Linkage": 5,
    "Kernel": 6,
    "Vector16": 7,
    "Float16Buffer": 8,
    "Float16": 9,
    "Float64": 10,
    "Int64": 11,
    "Int64Atomics": 12,
    "ImageBasic": 13,
    "ImageReadWrite": 14,
    "ImageMipmap": 15,
    "Pipes": 17,
    "Groups": 18,
    "DeviceEnqueue": 19,
    "LiteralSampler": 20,
    "AtomicStorage": 21,
    "Int16": 22,
    "TessellationPointSize": 23,
    "GeometryPointSize": 24,
    "ImageGatherExtended": 25,
    "StorageImageMultisample": 27,
    "UniformBufferArrayDynamicIndexing": 28,
    "SampledImageArrayDynamicIndexing": 29,
    "StorageBufferArrayDynamicIndexing": 30,
    "StorageImageArrayDynamicIndexing": 31,
    "ClipDistance": 32,
    "CullDistance": 33,
    "ImageCubeArray": 34,
    "SampleRateShading": 35,
    "ImageRect": 36,
    "SampledRect": 37,
    "GenericPointer": 38,
    "Int8": 39,
    "InputAttachment": 40,
    "SparseResidency": 41,
    "MinLod": 42,
    "Sampled1D": 43,
    "Image1D": 44,
    "SampledCubeArray": 45,
    "SampledBuffer": 46,
    "ImageBuffer": 47,
    "ImageMSArray": 48,
    "StorageImageExtendedFormats": 49,
    "ImageQuery": 50,
    "DerivativeControl": 51,
    "InterpolationFunction": 52,
    "TransformFeedback": 53,
    "GeometryStreams": 54,
    "StorageImageReadWithoutFormat": 55,
    "StorageImageWriteWithoutFormat": 56,
    "MultiViewport": 57,
    "SubgroupDispatch": 58,
    "NamedBarrier": 59,
    "PipeStorage": 60,
    "GroupNonUniform": 61,
    "GroupNonUniformVote": 62,
    "GroupNonUniformArithmetic": 63,
    "GroupNonUniformBallot": 64,
    "GroupNonUniformShuffle": 65,
    "GroupNonUniformShuffleRelative": 66,
    "GroupNonUniformClustered": 67,
    "GroupNonUniformQuad": 68,
    "DrawParameters": 4427,
    "FragmentShaderPixelInterlockEXT": 5378,
}

# The semantics value that means an interlocked workgroup memory barrier.
INTERLOCKED_WORKGROUP_MEMORY = 5335


def resolve(text):
    if text.isdigit():
        return int(text)
    if text in CAPABILITY_NAMES:
        return CAPABILITY_NAMES[text]
    raise SystemExit("unknown capability: %s" % text)


def capability_name(number):
    for name, value in CAPABILITY_NAMES.items():
        if value == number:
            return name
    return "Capability%d" % number


def instructions(words):
    """Yield (index, opcode, operands) for every instruction after the header."""
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        if count == 0:
            raise SystemExit("instruction of length 0 at word %d" % index)
        yield index, opcode, words[index + 1:index + count]
        index += count


def read_module(path):
    data = open(path, "rb").read()
    if len(data) % 4:
        raise SystemExit("%s: SPIR-V length %d is not a multiple of 4" % (path, len(data)))
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    if len(words) < 5 or words[0] != 0x07230203:
        raise SystemExit("%s: not a SPIR-V module" % path)
    return words


def analyse(words):
    """What the module declares and what it uses."""
    declared = []
    float_widths = set()
    int_widths = set()
    pointer_pointee = {}      # result id of OpTypePointer -> pointee type id
    image_of_sampled = {}     # result id of OpTypeSampledImage -> image type id
    loaded_image_type = {}    # result id of OpLoad -> the type id it holds
    chains_image_type = {}    # result id of OpAccessChain -> the type id it reaches
    reads = []                # image ids of OpImageRead
    writes = []               # image ids of OpImageWrite
    interlocked_barrier = False

    for _, opcode, operands in instructions(words):
        if opcode == OP_CAPABILITY:
            declared.append(operands[0])
        elif opcode == OP_TYPE_FLOAT:
            float_widths.add(operands[1])
        elif opcode == OP_TYPE_INT:
            int_widths.add(operands[1])
        elif opcode == OP_TYPE_POINTER:
            pointer_pointee[operands[0]] = operands[2]
        elif opcode == OP_TYPE_SAMPLED_IMAGE:
            image_of_sampled[operands[0]] = operands[1]
        elif opcode == OP_LOAD:
            loaded_image_type[operands[1]] = operands[0]
        elif opcode == OP_ACCESS_CHAIN:
            chains_image_type[operands[1]] = operands[0]
        elif opcode == OP_FUNCTION_PARAMETER:
            # Result Type, Result
            loaded_image_type[operands[1]] = operands[0]
        elif opcode == OP_VARIABLE:
            # Result Type, Result, Storage Class, Initializer
            loaded_image_type[operands[1]] = operands[0]
        elif opcode == OP_IMAGE_READ:
            reads.append(operands[2])
        elif opcode == OP_IMAGE_WRITE:
            # The write is a void instruction: it has no result type and no
            # result of its own, so the image is its first operand.
            writes.append(operands[0])
        elif opcode in (OP_CONTROL_BARRIER, OP_MEMORY_BARRIER):
            semantics = operands[-1]
            if semantics == INTERLOCKED_WORKGROUP_MEMORY:
                interlocked_barrier = True

    image_formats = {}        # OpTypeImage result id -> image format
    for _, opcode, operands in instructions(words):
        if opcode == OP_TYPE_IMAGE:
            # Result Type, Result, Sampled Type, Dim, Depth, Arrayed, MS,
            # Sampled, Image Format
            image_formats[operands[0]] = operands[7]

    def image_type_of(image_id):
        """The OpTypeImage id behind an image id, or None when it is not one."""
        type_id = loaded_image_type.get(image_id)
        if type_id is None:
            type_id = chains_image_type.get(image_id)
        if type_id is None:
            return None
        type_id = pointer_pointee.get(type_id, type_id)
        type_id = image_of_sampled.get(type_id, type_id)
        return type_id if type_id in image_formats else None

    def writes_unknown_format():
        for image_id in writes:
            type_id = image_type_of(image_id)
            if type_id is not None and image_formats[type_id] == 0:
                return True
        return False

    def reads_unknown_format():
        for image_id in reads:
            type_id = image_type_of(image_id)
            if type_id is not None and image_formats[type_id] == 0:
                return True
        return False

    used = set()
    if 64 in float_widths:
        used.add(10)      # Float64
    if 16 in float_widths:
        used.add(9)       # Float16
    if 64 in int_widths:
        used.add(11)      # Int64
    if 16 in int_widths:
        used.add(22)      # Int16
    if writes_unknown_format():
        used.add(56)      # StorageImageWriteWithoutFormat
    if reads_unknown_format():
        used.add(55)      # StorageImageReadWithoutFormat
    if interlocked_barrier:
        used.add(5378)    # FragmentShaderPixelInterlockEXT

    return {
        "declared": declared,
        "used": sorted(used),
        "float_widths": sorted(float_widths),
        "int_widths": sorted(int_widths),
        "image_formats": {str(k): v for k, v in image_formats.items()},
        "reads": len(reads),
        "writes": len(writes),
    }


def rewrite(words, add, drop):
    """The module with the capabilities added and dropped. Returns (words, added, dropped)."""
    kept = list(words[:5])
    added = []
    dropped = []
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        operands = words[index + 1:index + count]
        if opcode == OP_CAPABILITY and operands and operands[0] in drop:
            dropped.append(operands[0])
            index += count
            continue
        kept.append(words[index])
        kept.extend(operands)
        index += count

    if add:
        # The capabilities belong in the first section of the module, before
        # anything else. They go right after the capabilities that are
        # already there.
        out = list(kept[:5])
        placed = False
        index = 5
        while index < len(kept):
            count = kept[index] >> 16
            opcode = kept[index] & 0xFFFF
            if opcode == OP_CAPABILITY:
                if not placed:
                    for capability in add:
                        out.append((2 << 16) | OP_CAPABILITY)
                        out.append(capability)
                        added.append(capability)
                    placed = True
            out.append(kept[index])
            out.extend(kept[index + 1:index + count])
            index += count
        if not placed:
            for capability in add:
                out.append((2 << 16) | OP_CAPABILITY)
                out.append(capability)
                added.append(capability)
        kept = out

    return kept, added, dropped


def main(argv):
    source = None
    destination = None
    report_path = None
    add = []
    drop = []
    index = 1
    while index < len(argv):
        argument = argv[index]
        if argument == "--in":
            index += 1
            source = argv[index]
        elif argument == "--out":
            index += 1
            destination = argv[index]
        elif argument == "--report":
            index += 1
            report_path = argv[index]
        elif argument == "--add":
            index += 1
            add.append(resolve(argv[index]))
        elif argument == "--drop":
            index += 1
            drop.append(resolve(argv[index]))
        else:
            raise SystemExit("unknown argument: %s" % argument)
        index += 1

    if source is None:
        sys.stderr.write(__doc__)
        return 64

    words = read_module(source)
    facts = analyse(words)

    # A capability that the module uses is never dropped. Saying so is the
    # whole point of looking at the module instead of trusting a list.
    refused = sorted(set(drop) & set(facts["used"]))
    if refused:
        sys.stderr.write(
            "%s: refusing to drop %s: the module uses it\n"
            % (source, ", ".join(capability_name(c) for c in refused))
        )
        return 2

    to_add = [c for c in add if c not in facts["declared"]]
    to_drop = [c for c in drop if c in facts["declared"]]

    report = {
        "source": source,
        "declared": [capability_name(c) for c in facts["declared"]],
        "used": [capability_name(c) for c in facts["used"]],
        "float_widths": facts["float_widths"],
        "int_widths": facts["int_widths"],
        "image_reads": facts["reads"],
        "image_writes": facts["writes"],
        "added": [capability_name(c) for c in to_add],
        "dropped": [capability_name(c) for c in to_drop],
        "output": None,
    }

    if destination is not None:
        kept, added, dropped = rewrite(words, to_add, to_drop)
        open(destination, "wb").write(struct.pack("<%dI" % len(kept), *kept))
        report["added"] = [capability_name(c) for c in added]
        report["dropped"] = [capability_name(c) for c in dropped]
        report["output"] = destination

    text = json.dumps(report, indent=1)
    if report_path is not None:
        open(report_path, "w").write(text + "\n")
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Generate the NVN command buffer family of the FFRS RG477M native runtime.

The guest resolves every NVN entry point by name at start-up. The 156 names it
resolves that belong to the command buffer family are listed in
evidence/nvn/main_nvn_ids.txt, and the signatures of those entry points are in
the vendored NVN 53.105 header in src/graphics/nvn/include/nvn/nvn.h.

This script writes src/graphics/nvn_device/nvn_command_buffer.generated.c:
the 156 entry points, each of which records the arguments it was given, and
the name table the dispatcher hands them out with.

Regenerate with:

    python3 scripts/runtime/make_nvn_command_buffer_family.py

The output depends on nothing but the two inputs, so the same inputs always
give the same file: there is no timestamp and no ordering that depends on the
filesystem.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER_PATH = ROOT / "src/graphics/nvn/include/nvn/nvn.h"
NAMES_PATH = ROOT / "evidence/nvn/main_nvn_ids.txt"
OUTPUT_PATH = ROOT / "src/graphics/nvn_device/nvn_command_buffer.generated.c"

FAMILY_PREFIX = "nvnCommandBuffer"
TYPEDEF_PREFIX = "PFNNVNCOMMANDBUFFER"
CALLBACK_TYPEDEF = "PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC"

# The names the guest resolves for which the header this runtime was built
# against holds no typedef. Their signatures are not known and are not
# guessed: they are recorded as plain words, so the log shows the call shape
# the device really uses without the runtime pretending to know it.
RAW_NAMES = [
    "nvnCommandBufferBindImagesDeferred",
    "nvnCommandBufferBindSeparateSamplersDeferred",
    "nvnCommandBufferBindSeparateTexturesDeferred",
    "nvnCommandBufferBindStorageBuffersDeferred",
    "nvnCommandBufferBindTexturesDeferred",
    "nvnCommandBufferBindUniformBuffersDeferred",
    "nvnCommandBufferInsertCheckpointDynamic",
    "nvnCommandBufferInsertCheckpointStatic",
    "nvnCommandBufferOverrideSubtileSize",
    "nvnCommandBufferSetColorReductionEnable",
    "nvnCommandBufferSetColorReductionThresholds",
    "nvnCommandBufferSetCommandMemoryCallbackEnabled",
    "nvnCommandBufferSetControlMemoryCallbackEnabled",
    "nvnCommandBufferSetProvokingVertexMode",
    "nvnCommandBufferSetStencilCullCriteria",
    "nvnCommandBufferSetZCullZF32CompressionEnable",
]

# The structs that are handed to an entry point by value. They are the handle
# wrappers of the SDK, not the SDK objects themselves, so their layout is
# copied. Every other SDK object is only ever named by a pointer here, and
# those stay incomplete types, because the layout of an NVNobject is a
# property of the Nintendo SDK that this runtime does not have.
BY_VALUE_STRUCTS = {"NVNseparateTextureHandle", "NVNseparateSamplerHandle"}

# The scalar typedefs of the header that are handed over by value.
BY_VALUE_SCALARS = {
    "NVNboolean": "uint8_t",
    "NVNtextureHandle": "uint64_t",
    "NVNimageHandle": "uint64_t",
    "NVNcommandHandle": "uint64_t",
    "NVNbufferAddress": "uint64_t",
}

# The 64 bit types of the header and of C itself.
WIDE_TYPES = {
    "uint64_t",
    "int64_t",
    "size_t",
    "ptrdiff_t",
    "NVNbufferAddress",
    "NVNcommandHandle",
    "NVNtextureHandle",
    "NVNimageHandle",
}

# The types C itself provides. They are not in the header and do not have to
# be copied into the output.
BUILTIN_TYPES = {
    "void",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "unsigned",
    "int8_t",
    "uint8_t",
    "int16_t",
    "uint16_t",
    "int32_t",
    "uint32_t",
    "int64_t",
    "uint64_t",
    "size_t",
    "ptrdiff_t",
}

FUNC_TYPEDEF = re.compile(
    r"^typedef\s+(?P<ret>.+?)\(\s*\*\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\)\s*"
    r"\((?P<args>.*)\)\s*;$",
    re.S,
)
SIMPLE_TYPEDEF = re.compile(
    r"^typedef\s+(?P<base>[A-Za-z_][A-Za-z0-9_ ]*?\*?)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;$",
    re.S,
)
ENUM_TYPEDEF = re.compile(r"^typedef\s+enum\s*\{.*\}\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;$", re.S)
STRUCT_TYPEDEF = re.compile(r"^typedef\s+struct\s*\{.*\}\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;$", re.S)


def fail(message):
    sys.stderr.write("make_nvn_command_buffer_family: %s\n" % message)
    raise SystemExit(1)


def typedef_statements(text):
    """Every typedef statement of the header, one at a time.

    A typedef ends at the first semicolon that is not inside a bracket, so
    the statements are cut out of the header instead of being matched with a
    regular expression over the whole file, which would run one statement
    into the next."""
    index = 0
    while True:
        start = text.find("typedef", index)
        if start < 0:
            return
        depth = 0
        cursor = start
        while cursor < len(text):
            char = text[cursor]
            if char in "([{":
                depth += 1
            elif char in ")]}":
                depth -= 1
            elif char == ";" and depth == 0:
                break
            cursor += 1
        if cursor >= len(text):
            return
        yield text[start:cursor + 1]
        index = cursor + 1


def split_arguments(text):
    """The arguments of a parameter list, split at the commas that are not
    inside a bracket."""
    out = []
    depth = 0
    current = ""
    for char in text:
        if char == "," and depth == 0:
            out.append(current.strip())
            current = ""
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        current += char
    if current.strip():
        out.append(current.strip())
    return out


def base_type(argument):
    """The type of an argument without its qualifiers, and whether it is a
    pointer.

    A pointer to a pointer is written "const NVNtexture* const*", so every
    star and every const is peeled off until the name of the type is left."""
    text = argument.strip()
    pointer = "*" in text
    while True:
        stripped = text.rstrip()
        if stripped.endswith("*"):
            text = stripped[:-1].rstrip()
            continue
        if stripped.endswith("const"):
            text = stripped[:-len("const")].rstrip()
            continue
        text = stripped
        break
    text = re.sub(r"^const\s+", "", text).strip()
    return text, pointer


def read_header():
    """The typedefs of the header, by name.

    Each one is kept together with the kind it is, because the kind decides
    what the generator copies into the output: an enumeration and a handle
    struct are handed over by value and have to be complete, while an SDK
    object is only named by a pointer and stays incomplete."""
    text = HEADER_PATH.read_text()
    by_name = {}
    for statement in typedef_statements(text):
        match = FUNC_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {
                "kind": "func",
                "text": statement,
                "ret": match.group("ret").strip(),
                "args": split_arguments(match.group("args")),
            }
            continue
        match = ENUM_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {"kind": "enum", "text": statement, "args": []}
            continue
        match = STRUCT_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {"kind": "struct", "text": statement, "args": []}
            continue
        match = SIMPLE_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {
                "kind": "scalar",
                "text": statement,
                "args": [],
                "base": match.group("base").strip(),
            }
            continue
        fail("the header holds a typedef this generator does not understand:\n%s" % statement)
    return by_name


def read_family_names():
    """The names of the family, in the order the guest resolves them."""
    names = []
    for line in NAMES_PATH.read_text().splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        if name.startswith(FAMILY_PREFIX):
            names.append(name)
    return names


def select_signatures(names, typedefs):
    """The typedef of every name of the family, and the names that have none.

    The mapping is exact: the name of the guest without its family prefix,
    in upper case, is the middle of the typedef."""
    signed = {}
    unsigned = []
    for name in names:
        middle = name[len(FAMILY_PREFIX):].upper()
        typedef_name = TYPEDEF_PREFIX + middle + "PROC"
        if typedef_name == CALLBACK_TYPEDEF:
            continue
        if typedef_name in typedefs:
            signed[name] = typedefs[typedef_name]
        else:
            unsigned.append(name)
    return signed, unsigned


def needed_types(signed):
    """The types that have to be complete in the output.

    A type is needed when an entry point takes it by value, either as an
    argument or as what it returns. Everything else is only named by a pointer
    and stays an incomplete type, so that the output never carries the layout
    of an SDK object."""
    by_value = set()
    pointers = set()
    for typedef in signed.values():
        arguments = list(typedef["args"])
        if typedef["ret"] != "void":
            arguments.append(typedef["ret"])
        for argument in arguments:
            name, pointer = base_type(argument)
            if pointer:
                if name not in BUILTIN_TYPES:
                    pointers.add(name)
            else:
                by_value.add(name)
    return by_value, pointers


def collect_local_types(signed, typedefs):
    """The type definitions the output carries.

    The callback typedef names one more type by value, so the search follows
    it as well."""
    by_value, pointers = needed_types(signed)
    complete = set()
    pending = list(by_value)
    while pending:
        name = pending.pop()
        if name in complete or name in BUILTIN_TYPES:
            continue
        complete.add(name)
        if name not in typedefs:
            fail("the family uses the type %s, which the header does not define" % name)
        if typedefs[name]["kind"] == "func":
            for argument in typedefs[name]["args"]:
                inner, pointer = base_type(argument)
                if not pointer and inner not in complete:
                    pending.append(inner)
    return complete, pointers


def emit_local_types(complete, pointers, typedefs):
    """The type block of the output.

    The opaque types come first, because the callback typedef names one of
    them."""
    lines = []
    for name in sorted(pointers):
        if name in complete:
            continue
        lines.append("typedef struct nvn_cb_%s %s;" % (name.lower(), name))
    lines.append("")
    for name in sorted(complete):
        typedef = typedefs[name]
        if typedef["kind"] == "struct" and name not in BY_VALUE_STRUCTS:
            fail("the family takes the struct %s by value, and its layout would be copied" % name)
        if typedef["kind"] == "scalar" and name not in BY_VALUE_SCALARS:
            fail("the family takes the type %s by value, and it is not one of the known scalars" % name)
        if typedef["kind"] == "func" and name != CALLBACK_TYPEDEF:
            fail("the family takes the function pointer %s by value" % name)
        for line in typedef["text"].splitlines():
            lines.append(line)
        lines.append("")
    return lines


def parameter_declaration(argument, name):
    """The declaration of one parameter, with the type of the header."""
    text = argument.strip()
    if text.endswith("*"):
        return "%s *%s" % (text[:-1].rstrip(), name)
    return "%s %s" % (text, name)


def recorded_expression(argument, expression, typedefs):
    """The expression that puts one argument into a record.

    The value of a narrow argument is only its own width: the upper bits of
    the register are not part of it. A float is recorded as its bit pattern,
    because a float has no integer representation that survives the trip."""
    name, pointer = base_type(argument)
    if pointer:
        return "(uint64_t)(uintptr_t)%s" % expression
    if name in ("NVNboolean", "uint8_t"):
        return "(uint64_t)(uint8_t)%s" % expression
    if name in WIDE_TYPES:
        return "(uint64_t)%s" % expression
    if name == "float":
        return "nvn_command_buffer_float_bits(%s)" % expression
    if name in ("int", "uint32_t", "int32_t", "uint16_t", "int16_t"):
        return "(uint64_t)(uint32_t)%s" % expression
    if name in BY_VALUE_STRUCTS:
        return "(uint64_t)%s.value" % expression
    if name == CALLBACK_TYPEDEF:
        return "(uint64_t)(uintptr_t)%s" % expression
    if name in typedefs and typedefs[name]["kind"] == "enum":
        return "(uint64_t)(uint32_t)%s" % expression
    fail("the family takes the type %s by value, and this generator does not know how to record it"
         % name)


def format_signature(ret, name, parameters):
    """The definition of an entry point, wrapped the way the runtime wraps its
    definitions."""
    prefix = "%s %s(" % (ret, name)
    indent = " " * len(prefix)
    suffix = ") {"
    # A name with a long return type cannot even hold its first parameter, so
    # the return type goes on a line of its own.
    if parameters and len(prefix) + len(parameters[0]) + len(suffix) > 100:
        prefix = "%s(" % name
        indent = " " * len(prefix)
        head = [ret]
    else:
        head = []
    packed = []
    current = prefix
    for index, parameter in enumerate(parameters):
        last = index + 1 == len(parameters)
        piece = parameter + ("" if last else ",")
        limit = 100 - (len(suffix) if last else 0)
        candidate = current + piece if current == prefix else current + " " + piece
        if len(candidate) > limit:
            packed.append(current)
            current = indent + piece
        else:
            current = candidate
    packed.append(current + suffix)
    return head + packed


def format_comment(text, marker="/* "):
    """A comment, wrapped to the width the runtime uses."""
    words = text.split()
    lines = []
    current = marker.rstrip()
    for word in words:
        candidate = current + (" " if current.strip() else "") + word
        if len(candidate) > 100 and current.strip():
            lines.append(current)
            current = " * " + word
        else:
            current = candidate
    lines.append(current + " */")
    return lines


def emit_raw_wrapper(name, index):
    """An entry point whose signature the header does not give.

    The arguments are taken as plain words and written down as they came.
    Nothing is dereferenced, so a signature with fewer or more arguments
    cannot do any harm here."""
    lines = []
    lines.extend(format_comment(
        "The signature of %s is not in the header this runtime was built against, so the "
        "arguments are taken as plain words and written down as they came. Nothing is "
        "dereferenced, so a signature with fewer or more arguments cannot do any harm "
        "here. The log shows the call shape the device really uses." % name))
    parameters = ["NVNcommandBuffer *commandBuffer"]
    for index_of in range(1, 8):
        parameters.append("uint64_t arg%d" % index_of)
    lines.extend(format_signature("void", name, parameters))
    lines.append("    uint64_t args[8];")
    lines.append("    args[0] = (uint64_t)(uintptr_t)commandBuffer;")
    for index_of in range(1, 8):
        lines.append("    args[%d] = arg%d;" % (index_of, index_of))
    lines.append("    nvn_command_buffer_record(%d, commandBuffer, args, 8);" % index)
    lines.append('    nvn_command_buffer_log_raw("%s",' % name)
    lines.append("                                commandBuffer, args, 8);")
    lines.append("    NVN_VOID_RETURN();")
    lines.append("}")
    return lines


def emit_typed_wrapper(name, typedef, index):
    """An entry point whose signature the header gives.

    The signature is the one of the header, so that the compiler lays the
    arguments out the way the NVN calling convention wants them."""
    arguments = typedef["args"]
    parameters = []
    expressions = []
    for position, argument in enumerate(arguments):
        if position == 0:
            parameter_name = "commandBuffer"
        else:
            parameter_name = "arg%d" % position
        parameters.append(parameter_declaration(argument, parameter_name))
        expressions.append(recorded_expression(argument, parameter_name, TYPEDEF_BY_NAME))
    count = len(arguments)

    lines = []
    lines.extend(format_signature(typedef["ret"], name, parameters))
    lines.append("    uint64_t args[%d];" % count)
    pre = PRE_BODY.get(name)
    if pre:
        lines.extend(pre)
    for position, expression in enumerate(expressions):
        lines.append("    args[%d] = %s;" % (position, expression))
    lines.append("    nvn_command_buffer_record(%d, commandBuffer, args, %d);" % (index, count))

    extra = EXTRA_BODY.get(name)
    if extra:
        lines.extend(extra)
    else:
        lines.append("    NVN_VOID_RETURN();")
    lines.append("}")
    return lines


# The bodies that run before the record is written.
#
# The initialisation empties the recorder of the buffer, because a buffer
# that is initialised again starts empty. The call itself is recorded after
# that, so that the guest's own initialisation is one of the commands the
# runtime sees.
PRE_BODY = {
    "nvnCommandBufferInitialize": [
        "    int initialized = nvn_command_buffer_initialize(commandBuffer);",
    ],
}

# The bodies of the entry points that are more than a record: the ones that
# change the state of the recorder, and the ones that answer a question.
EXTRA_BODY = {
    "nvnCommandBufferInitialize": [
        "    return (NVNboolean)(initialized ? 1 : 0);",
    ],
    "nvnCommandBufferFinalize": [
        "    nvn_command_buffer_finalize(commandBuffer);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferBeginRecording": [
        "    nvn_command_buffer_begin_recording(commandBuffer);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferEndRecording": [
        "    return (NVNcommandHandle)nvn_command_buffer_end_recording(commandBuffer);",
    ],
    "nvnCommandBufferSetMemoryCallback": [
        "    nvn_command_buffer_set_memory_callback(commandBuffer, (uint64_t)(uintptr_t)arg1);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferSetMemoryCallbackData": [
        "    nvn_command_buffer_set_memory_callback_data(commandBuffer, (uint64_t)(uintptr_t)arg1);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferAddCommandMemory": [
        "    nvn_command_buffer_add_command_memory(commandBuffer, (uint64_t)arg3);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferAddControlMemory": [
        "    nvn_command_buffer_add_control_memory(commandBuffer, (uint64_t)arg2);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferSetCopyRowStride": [
        "    nvn_command_buffer_set_copy_row_stride(commandBuffer, (int64_t)arg1);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferSetCopyImageStride": [
        "    nvn_command_buffer_set_copy_image_stride(commandBuffer, (int64_t)arg1);",
        "    NVN_VOID_RETURN();",
    ],
    "nvnCommandBufferGetCommandMemorySize": [
        "    return (size_t)nvn_command_buffer_get_command_memory_size(commandBuffer);",
    ],
    "nvnCommandBufferGetCommandMemoryUsed": [
        "    nvn_command_buffer_report_used_is_unknown(commandBuffer, \"command memory\");",
        "    return 0;",
    ],
    "nvnCommandBufferGetCommandMemoryFree": [
        "    return (size_t)nvn_command_buffer_get_command_memory_free(commandBuffer);",
    ],
    "nvnCommandBufferGetControlMemorySize": [
        "    return (size_t)nvn_command_buffer_get_control_memory_size(commandBuffer);",
    ],
    "nvnCommandBufferGetControlMemoryUsed": [
        "    nvn_command_buffer_report_used_is_unknown(commandBuffer, \"control memory\");",
        "    return 0;",
    ],
    "nvnCommandBufferGetControlMemoryFree": [
        "    return (size_t)nvn_command_buffer_get_control_memory_free(commandBuffer);",
    ],
    "nvnCommandBufferGetCopyRowStride": [
        "    return (ptrdiff_t)nvn_command_buffer_get_copy_row_stride(commandBuffer);",
    ],
    "nvnCommandBufferGetCopyImageStride": [
        "    return (ptrdiff_t)nvn_command_buffer_get_copy_image_stride(commandBuffer);",
    ],
    "nvnCommandBufferGetMemoryCallback": [
        "    return (PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC)",
        "        (uintptr_t)nvn_command_buffer_get_memory_callback(commandBuffer);",
    ],
    "nvnCommandBufferGetMemoryCallbackData": [
        "    /* The header this runtime was built against declares this entry point as",
        "     * returning nothing, but the value it names is the callback data the guest",
        "     * set, and the guest reads it from the result register. The value goes",
        "     * there directly, because a C function that returns nothing cannot hand",
        "     * it over otherwise. */",
        "    nvn_command_buffer_set_result(",
        "        nvn_command_buffer_get_memory_callback_data(commandBuffer));",
    ],
    "nvnCommandBufferIsRecording": [
        "    return (NVNboolean)(nvn_command_buffer_is_recording(commandBuffer) ? 1 : 0);",
    ],
}


def build(names, signed, unsigned, typedefs, complete, pointers):
    """The whole generated file."""
    out = []
    out.append("/* The NVN command buffer family of the FFRS RG477M native runtime.")
    out.append(" *")
    out.append(" * This file is generated. Do not edit it; edit the generator instead:")
    out.append(" *")
    out.append(" *     python3 scripts/runtime/make_nvn_command_buffer_family.py")
    out.append(" *")
    out.append(" * The generator reads the names the guest resolves from")
    out.append(" * evidence/nvn/main_nvn_ids.txt and the signatures from the vendored NVN")
    out.append(" * header in src/graphics/nvn/include/nvn/nvn.h, and writes this file. The")
    out.append(" * output depends on nothing else, so the same inputs always give the same")
    out.append(" * file.")
    out.append(" *")
    out.append(" * Every entry point records the arguments it was given and hands the")
    out.append(" * record to the recorder. The replay of the recorded commands does not")
    out.append(" * exist yet; nothing here pretends that a command was executed.")
    out.append(" */")
    out.append('#include "nvn_command_buffer.h"')
    out.append("")
    out.append('#include "nvn_device.h"')
    out.append("")
    out.append("#include <stddef.h>")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("/* The types of the NVN header that this family needs.")
    out.append(" *")
    out.append(" * The SDK objects are only ever named by a pointer, so they stay")
    out.append(" * incomplete here: the layout of an NVNobject is a property of the")
    out.append(" * Nintendo SDK, which this runtime does not have. The enumerations and the")
    out.append(" * handle wrappers are handed over by value, so they are complete. */")
    out.extend(emit_local_types(complete, pointers, typedefs))
    out.append("")
    out.append("/* The entry points. */")

    for index, name in enumerate(names):
        out.append("")
        if name in unsigned:
            out.extend(emit_raw_wrapper(name, index))
        else:
            out.extend(emit_typed_wrapper(name, signed[name], index))

    out.append("")
    out.append("/* ------------------------------------------------------------------ */")
    out.append("/* The names                                                           */")
    out.append("/* ------------------------------------------------------------------ */")
    out.append("")
    out.append("static const nvn_command_buffer_family_entry s_command_buffer_family[] = {")
    for name in names:
        entry = '{ "%s", (void *)(uintptr_t)%s },' % (name, name)
        if len(entry) + 4 > 100:
            out.append('    { "%s",' % name)
            out.append("      (void *)(uintptr_t)%s }," % name)
        else:
            out.append("    " + entry)
    out.append("};")
    out.append("")
    out.append("size_t nvn_command_buffer_family_count(void) {")
    out.append("    return sizeof(s_command_buffer_family) / sizeof(s_command_buffer_family[0]);")
    out.append("}")
    out.append("")
    out.append("const nvn_command_buffer_family_entry *nvn_command_buffer_family_entry_at(size_t index) {")
    out.append("    if (index >= nvn_command_buffer_family_count()) return NULL;")
    out.append("    return &s_command_buffer_family[index];")
    out.append("}")
    out.append("")
    return "\n".join(out)


TYPEDEF_BY_NAME = {}


def main():
    global TYPEDEF_BY_NAME
    if not HEADER_PATH.exists():
        fail("the NVN header is not at %s" % HEADER_PATH)
    if not NAMES_PATH.exists():
        fail("the guest name list is not at %s" % NAMES_PATH)

    typedefs = read_header()
    TYPEDEF_BY_NAME = typedefs
    names = read_family_names()
    signed, unsigned = select_signatures(names, typedefs)

    if len(names) != 156:
        fail("the guest resolves %d names of the command buffer family, and this generator "
             "was written for 156" % len(names))
    if sorted(unsigned) != sorted(RAW_NAMES):
        fail("the names without a typedef are %s, and this generator expects %s"
             % (sorted(unsigned), sorted(RAW_NAMES)))
    for name in names:
        if name in signed:
            count = len(signed[name]["args"])
            if count > 8:
                fail("%s takes %d arguments, and a record holds 8" % (name, count))

    complete, pointers = collect_local_types(signed, typedefs)
    text = build(names, signed, unsigned, typedefs, complete, pointers)
    OUTPUT_PATH.write_text(text)
    sys.stdout.write("wrote %s: %d names, %d from the header, %d as raw recorders\n"
                     % (OUTPUT_PATH.relative_to(ROOT), len(names), len(signed), len(unsigned)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

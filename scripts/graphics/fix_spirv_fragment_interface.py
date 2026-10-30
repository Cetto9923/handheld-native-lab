#!/usr/bin/env python3
"""Make a fragment module's interface match the vertex stage it is paired with.

Why this exists (P-MM-29, measured on the RG477M's Mali-G615):

A graphics pipeline is only created when the two stages agree about the
interface between them.  The game ships one vertex module and three fragment
modules, and the fragment modules do not agree with the vertex module:

- the small fragment module reads ``Input`` location 0 with the
  ``NoPerspective`` interpolation decoration, while the vertex module writes
  location 0 with the default (smooth) interpolation;
- the two large fragment modules read ``Input`` locations 0 **and** 1, while
  the vertex module writes location 0 only, so location 1 has no producer.

A driver that checks the interface rejects such a pipeline.  On this device
that rejection is ``VK_ERROR_INITIALIZATION_FAILED`` (-3), not a crash.

This tool does two things, and only these two:

1. it removes the capability declarations the module does not use, by calling
   the coordinator's ``fix_spirv_capabilities.py`` (which decides from the
   module's own evidence and refuses to drop a capability the module uses);
2. it removes an extension declaration (``--drop-extension``).  This is the
   half the coordinator's capability tool cannot do: the driver reads the
   declaration, so a module that still declares
   ``SPV_EXT_fragment_shader_interlock`` is refused even after the
   capability is gone (measured on the RG477M, P-MM-29);
3. it fixes the interpolation decoration of the interface, either on the
   fragment side (``--drop-no-perspective``) or on the vertex side
   (``--add-no-perspective-to-vertex-output``).

It also has two switches that exist for bisection and change what the
module does: ``--strip-body`` leaves the entry point's function empty and
``--strip-descriptor-variables`` removes the variables that hold
descriptors.

It never touches the input file.  It never invents a producer for an input
location the vertex stage does not write: that is a gap in the game's shader
set, not something a module edit can repair.

Usage:
    fix_spirv_fragment_interface.py --in frag.spv --out fixed.spv \\
        [--vertex vert.spv] \\
        [--drop-capability NAME]... [--add-capability NAME]... \\
        [--drop-no-perspective] \\
        [--add-no-perspective-to-vertex-output LOCATION] \\
        [--report report.json]

Exit codes:
    0  the output was written
    2  a capability the module uses was asked to be dropped (the
       coordinator's tool refuses; its message is passed through)
    3  the arguments do not describe a possible edit
    4  an input file is missing or is not a SPIR-V module
"""
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile

OP_EXTENSION = 10
OP_ENTRY_POINT = 15
OP_EXECUTION_MODE = 16
OP_CAPABILITY = 17
OP_NAME = 5
OP_DECORATE = 71
OP_MEMBER_DECORATE = 72
OP_GROUP_DECORATE = 73
OP_VARIABLE = 59
OP_FUNCTION = 54
OP_FUNCTION_END = 56
OP_LABEL = 248
OP_RETURN = 253
OP_TYPE_POINTER = 32
OP_TYPE_VECTOR = 23

DEC_LOCATION = 30
DEC_FLAT = 14
DEC_CENTROID = 16
DEC_SAMPLE = 17
DEC_NO_PERSPECTIVE = 13

STORAGE_INPUT = 1
STORAGE_OUTPUT = 3
STORAGE_UNIFORM_CONSTANT = 0
STORAGE_UNIFORM = 2
STORAGE_STORAGE_BUFFER = 12
DESCRIPTOR_STORAGE_CLASSES = (STORAGE_UNIFORM_CONSTANT, STORAGE_UNIFORM,
                              STORAGE_STORAGE_BUFFER)

INTERPOLATION_NAMES = {
    DEC_FLAT: "Flat",
    DEC_CENTROID: "Centroid",
    DEC_SAMPLE: "Sample",
    DEC_NO_PERSPECTIVE: "NoPerspective",
}

# The coordinator owns this tool; it is used, never modified.
CAPABILITY_TOOL = "fix_spirv_capabilities.py"


def fail(message, code):
    sys.stderr.write(message + "\n")
    return code


def read_words(path):
    try:
        data = open(path, "rb").read()
    except OSError as error:
        raise ValueError("%s: %s" % (path, error))
    if len(data) % 4:
        raise ValueError("%s: SPIR-V length %d is not a multiple of 4" % (path, len(data)))
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError("%s: not a SPIR-V module" % path)
    return words


def write_words(path, words):
    open(path, "wb").write(struct.pack("<%dI" % len(words), *words))


def literal_string(words):
    """A SPIR-V literal string: the words are the characters, packed four to a word."""
    data = b"".join(struct.pack("<I", word) for word in words)
    return data.split(b"\0", 1)[0].decode("utf-8", "replace")


def instructions(words):
    """Yield (index, opcode, operands) for every instruction after the header."""
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        if count == 0:
            raise ValueError("instruction of length 0 at word %d" % index)
        yield index, opcode, words[index + 1:index + count]
        index += count


def decode(words):
    """The facts the interface table is built from."""
    facts = {
        "capabilities": [],
        "extensions": [],
        "entry_points": [],
        "decorations": {},      # id -> {decoration: [literals]}
        "variables": {},        # id -> (storage class, type id)
        "pointee": {},          # pointer type id -> pointee type id
        "vector_size": {},      # vector type id -> component count
    }
    for _, opcode, operands in instructions(words):
        if opcode == OP_CAPABILITY:
            facts["capabilities"].append(operands[0])
        elif opcode == OP_EXTENSION:
            facts["extensions"].append(literal_string(operands))
        elif opcode == OP_ENTRY_POINT:
            facts["entry_points"].append({
                "model": operands[0],
                "id": operands[1],
                "name": literal_string(operands[2:]),
                "interface": list(operands[3:]),
            })
        elif opcode == OP_DECORATE:
            target = operands[0]
            decoration = operands[1]
            facts["decorations"].setdefault(target, {}).setdefault(decoration, []).append(
                list(operands[2:]))
        elif opcode == OP_VARIABLE:
            facts["variables"][operands[1]] = (operands[2], operands[0])
        elif opcode == OP_TYPE_POINTER:
            facts["pointee"][operands[0]] = operands[2]
        elif opcode == OP_TYPE_VECTOR:
            facts["vector_size"][operands[0]] = operands[2]
    return facts


def interpolation_of(decorations):
    """The interpolation decorations of one variable, as a sorted list."""
    if not decorations:
        return []
    return sorted(INTERPOLATION_NAMES[decoration]
                  for decoration in decorations
                  if decoration in INTERPOLATION_NAMES)


def interface_table(facts):
    """The stage interface: location -> storage class, type, interpolation."""
    table = []
    for variable, (storage, type_id) in sorted(facts["variables"].items()):
        if storage not in (STORAGE_INPUT, STORAGE_OUTPUT):
            continue
        decorations = facts["decorations"].get(variable, {})
        location = None
        if DEC_LOCATION in decorations:
            literals = decorations[DEC_LOCATION][0]
            location = literals[0] if literals else None
        pointee = facts["pointee"].get(type_id)
        components = facts["vector_size"].get(pointee)
        table.append({
            "id": variable,
            "storage": "Input" if storage == STORAGE_INPUT else "Output",
            "location": location,
            "components": components,
            "interpolation": interpolation_of(decorations),
        })
    return table


def compare_interfaces(fragment, vertex):
    """The mismatches between a fragment stage and a vertex stage."""
    vertex_outputs = {}
    for row in vertex:
        if row["storage"] == "Output" and row["location"] is not None:
            vertex_outputs.setdefault(row["location"], []).append(row)
    fragment_inputs = {}
    for row in fragment:
        if row["storage"] == "Input" and row["location"] is not None:
            fragment_inputs.setdefault(row["location"], []).append(row)

    mismatches = []
    for location in sorted(fragment_inputs):
        producers = vertex_outputs.get(location)
        if not producers:
            mismatches.append({
                "kind": "no_producer",
                "location": location,
                "detail": "the fragment stage reads location %d and the vertex "
                          "stage writes no such location" % location,
                "fragment_inputs": fragment_inputs[location],
            })
            continue
        for consumer in fragment_inputs[location]:
            for producer in producers:
                if consumer["components"] != producer["components"]:
                    mismatches.append({
                        "kind": "type_mismatch",
                        "location": location,
                        "detail": "fragment input %d has %s components, vertex "
                                  "output %d has %s" % (
                                      consumer["id"], consumer["components"],
                                      producer["id"], producer["components"]),
                        "fragment_input": consumer,
                        "vertex_output": producer,
                    })
                elif consumer["interpolation"] != producer["interpolation"]:
                    mismatches.append({
                        "kind": "interpolation_mismatch",
                        "location": location,
                        "detail": "fragment input %d is %s, vertex output %d is %s" % (
                            consumer["id"], consumer["interpolation"] or ["smooth"],
                            producer["id"], producer["interpolation"] or ["smooth"]),
                        "fragment_input": consumer,
                        "vertex_output": producer,
                    })
    return mismatches


def drop_decorations(words, targets, decoration=None):
    """Remove the OpDecorate instructions whose target is in ``targets``.

    When ``decoration`` is given, only that decoration is removed and the
    other decorations of the same variable (its Location, for one) stay.
    """
    kept = list(words[:5])
    removed = []
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        operands = words[index + 1:index + count]
        if (opcode == OP_DECORATE and operands and operands[0] in targets
                and (decoration is None or operands[1] == decoration)):
            removed.append({"target": operands[0], "decoration": operands[1],
                            "literals": list(operands[2:])})
            index += count
            continue
        kept.append(words[index])
        kept.extend(operands)
        index += count
    return kept, removed


def drop_extensions(words, names):
    """Remove the OpExtension instructions whose name is in ``names``."""
    kept = list(words[:5])
    removed = []
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        operands = words[index + 1:index + count]
        if opcode == OP_EXTENSION and literal_string(operands) in names:
            removed.append(literal_string(operands))
            index += count
            continue
        kept.append(words[index])
        kept.extend(operands)
        index += count
    return kept, removed


def strip_function_body(words, entry_id):
    """Leave the entry point's function with an empty body."""
    out = list(words[:5])
    index = 5
    inside = False
    dropped = 0
    depth = 0
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        operands = words[index + 1:index + count]
        if opcode == OP_FUNCTION and operands and operands[1] == entry_id:
            inside = True
            depth = 0
            out.append(words[index])
            out.extend(operands)
            index += count
            continue
        if inside:
            if opcode == OP_FUNCTION:
                depth += 1
            elif opcode == OP_FUNCTION_END:
                if depth == 0:
                    # The function is over: keep the OpReturn and the end.
                    out.append(words[index])
                    out.extend(operands)
                    inside = False
                    index += count
                    continue
                depth -= 1
            if opcode == OP_LABEL and depth == 0:
                out.append(words[index])
                out.extend(operands)
                index += count
                continue
            if opcode == OP_RETURN and depth == 0:
                out.append(words[index])
                out.extend(operands)
                index += count
                continue
            if opcode != OP_FUNCTION_END or depth != 0:
                dropped += 1
            index += count
            continue
        out.append(words[index])
        out.extend(operands)
        index += count
    return out, dropped


def strip_descriptor_variables(words, facts):
    """Remove the variables that hold descriptors, and their decorations."""
    targets = set()
    for variable, (storage, _) in facts["variables"].items():
        if storage in DESCRIPTOR_STORAGE_CLASSES:
            targets.add(variable)
    if not targets:
        return words, []
    kept = list(words[:5])
    removed = []
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        operands = words[index + 1:index + count]
        if opcode == OP_VARIABLE and operands and operands[1] in targets:
            removed.append({"target": operands[1], "storage": operands[2]})
            index += count
            continue
        if opcode == OP_DECORATE and operands and operands[0] in targets:
            removed.append({"target": operands[0], "decoration": operands[1]})
            index += count
            continue
        if opcode == OP_NAME and operands and operands[0] in targets:
            # The name of a variable that no longer exists would be a forward
            # reference to an undefined id.
            removed.append({"target": operands[0], "name": True})
            index += count
            continue
        kept.append(words[index])
        kept.extend(operands)
        index += count
    return kept, removed


def add_decoration(words, target, decoration):
    """Add one OpDecorate instruction, in the annotation section."""
    out = list(words[:5])
    placed = False
    index = 5
    while index < len(words):
        count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        if opcode in (OP_DECORATE, OP_MEMBER_DECORATE, OP_GROUP_DECORATE):
            if not placed:
                out.append((3 << 16) | OP_DECORATE)
                out.append(target)
                out.append(decoration)
                placed = True
        out.append(words[index])
        out.extend(words[index + 1:index + count])
        index += count
    if not placed:
        # No annotation section yet: the decoration goes after the execution
        # modes, which is still inside the annotation section's position.
        out.append((3 << 16) | OP_DECORATE)
        out.append(target)
        out.append(decoration)
    return out


def run_capability_tool(arguments, source, destination, report_path):
    """The coordinator's capability tool decides what may be dropped."""
    here = os.path.dirname(os.path.abspath(__file__))
    tool = os.path.join(here, CAPABILITY_TOOL)
    if not os.path.isfile(tool):
        raise ValueError("the coordinator's capability tool is missing: %s" % tool)
    command = [sys.executable, tool, "--in", source, "--out", destination,
               "--report", report_path]
    for kind, name in arguments:
        command.extend(["--%s" % kind, name])
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.returncode != 0:
        raise CapabilityRefusal(completed.returncode, completed.stderr.strip())
    return json.load(open(report_path))


class CapabilityRefusal(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


def sha256_of(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main(argv):
    source = None
    destination = None
    report_path = None
    vertex_path = None
    drop_capabilities = []
    add_capabilities = []
    drop_no_perspective = False
    strip_body = False
    strip_descriptors = False
    drop_extensions_requested = []
    add_no_perspective_to_vertex = None

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
        elif argument == "--vertex":
            index += 1
            vertex_path = argv[index]
        elif argument == "--drop-capability":
            index += 1
            drop_capabilities.append(argv[index])
        elif argument == "--add-capability":
            index += 1
            add_capabilities.append(argv[index])
        elif argument == "--drop-no-perspective":
            drop_no_perspective = True
        elif argument == "--strip-body":
            strip_body = True
        elif argument == "--strip-descriptor-variables":
            strip_descriptors = True
        elif argument == "--drop-extension":
            index += 1
            drop_extensions_requested.append(argv[index])
        elif argument == "--add-no-perspective-to-vertex-output":
            index += 1
            add_no_perspective_to_vertex = int(argv[index])
        else:
            return fail("unknown argument: %s" % argument, 3)
        index += 1

    if source is None or destination is None:
        sys.stderr.write(__doc__)
        return 3
    if add_no_perspective_to_vertex is not None and vertex_path is None:
        return fail("--add-no-perspective-to-vertex-output needs --vertex", 3)

    try:
        source_words = read_words(source)
    except ValueError as error:
        return fail(str(error), 4)

    capability_report = None
    work = source
    workdir = None
    try:
        capability_arguments = [("drop", name) for name in drop_capabilities]
        capability_arguments += [("add", name) for name in add_capabilities]
        if capability_arguments:
            workdir = tempfile.mkdtemp(prefix="ffrs-spirv-interface-")
            stripped = os.path.join(workdir, "stripped.spv")
            capability_report = run_capability_tool(
                capability_arguments, source, stripped,
                os.path.join(workdir, "capabilities.json"))
            work = stripped
        words = read_words(work)

        facts = decode(words)
        fragment_interface = interface_table(facts)

        vertex_words = None
        vertex_facts = None
        vertex_interface = []
        if vertex_path is not None:
            try:
                vertex_words = read_words(vertex_path)
            except ValueError as error:
                return fail(str(error), 4)
            vertex_facts = decode(vertex_words)
            vertex_interface = interface_table(vertex_facts)

        removed_decorations = []
        added_decorations = []
        removed_body = 0
        removed_descriptors = []

        if drop_no_perspective:
            targets = set()
            for row in fragment_interface:
                if row["storage"] == "Input" and "NoPerspective" in row["interpolation"]:
                    targets.add(row["id"])
            words, removed_decorations = drop_decorations(words, targets,
                                                          DEC_NO_PERSPECTIVE)

        removed_extensions = []
        if drop_extensions_requested:
            words, removed_extensions = drop_extensions(words, drop_extensions_requested)

        if strip_body:
            entry_ids = [entry["id"] for entry in facts["entry_points"]]
            if not entry_ids:
                return fail("the module has no entry point", 3)
            words, dropped = strip_function_body(words, entry_ids[0])
            removed_body = dropped

        if strip_descriptors:
            facts_now = decode(words)
            words, removed_descriptors = strip_descriptor_variables(words, facts_now)

        if add_no_perspective_to_vertex is not None:
            targets = set()
            for row in vertex_interface:
                if (row["storage"] == "Output"
                        and row["location"] == add_no_perspective_to_vertex
                        and "NoPerspective" not in row["interpolation"]):
                    targets.add(row["id"])
            if not targets:
                return fail("the vertex module has no output at location %d"
                            % add_no_perspective_to_vertex, 3)
            for target in sorted(targets):
                vertex_words = add_decoration(vertex_words, target, DEC_NO_PERSPECTIVE)
                added_decorations.append({"target": target,
                                          "decoration": DEC_NO_PERSPECTIVE,
                                          "file": vertex_path})

        write_words(destination, words)
        if vertex_words is not None and vertex_path is not None and added_decorations:
            # The vertex module is rewritten into its own file next to the
            # fragment module, so the pair can be built and tested together.
            vertex_destination = os.path.join(
                os.path.dirname(os.path.abspath(destination)),
                os.path.basename(vertex_path))
            write_words(vertex_destination, vertex_words)

        after = decode(read_words(destination))
        report = {
            "source": source,
            "source_sha256": sha256_of(source),
            "output": destination,
            "output_sha256": sha256_of(destination),
            "vertex": vertex_path,
            "capability_tool": capability_report,
            "capabilities_requested_dropped": drop_capabilities,
            "capabilities_requested_added": add_capabilities,
            "removed_decorations": removed_decorations,
            "removed_body_instructions": removed_body,
            "removed_descriptor_variables": removed_descriptors,
            "removed_extensions": removed_extensions,
            "added_decorations": added_decorations,
            "fragment_interface_before": fragment_interface,
            "fragment_interface_after": interface_table(after),
            "vertex_interface": vertex_interface,
            "mismatches_before": compare_interfaces(fragment_interface, vertex_interface),
            "mismatches_after": compare_interfaces(interface_table(after), vertex_interface),
        }
        text = json.dumps(report, indent=1)
        if report_path is not None:
            open(report_path, "w").write(text + "\n")
        print(text)
        return 0
    except CapabilityRefusal as refusal:
        return fail("the capability tool refused: %s" % refusal.message, refusal.code)
    except ValueError as error:
        return fail(str(error), 4)
    finally:
        if workdir is not None:
            for name in os.listdir(workdir):
                os.unlink(os.path.join(workdir, name))
            os.rmdir(workdir)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

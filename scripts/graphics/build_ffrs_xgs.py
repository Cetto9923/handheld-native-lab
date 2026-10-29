#!/usr/bin/env python3
"""Build the XGS1 shader bundles (.xgs) for FINAL FANTASY RESONANCE.

The chain, in the order the bytes travel:

  1. **Extract** the shader out of the game's own IoStore container
     (``.local/romfs/FFRS/Content/Paks/FFRS-Switch.utoc``).  The container holds
     two shader libraries (Unreal Engine 5.6.1 ``SF_GLSL_SWITCH``); each holds
     groups of UE ``LSLG`` records; each record is a control section and a code
     area.  The group payloads are Oodle Mermaid compressed and are decoded
     with the local decoder (``.local/shader-verify/oodle_dec``, the open
     source ``ooz`` implementation, built for x86_64, so it needs Rosetta 2).
     The container reader is the reference host's own
     (``.local/refs/nxnative/dm/gfx/tools/uepak.py``), imported read only.

  2. **Translate** Maxwell machine code to SPIR-V with the vendored Ryujinx
     translator (``dmsh_xlate``, a .NET 10 application; the runtime is the
     local ``.local/shader-verify/dotnet``).  One job per shader; the job
     carries the code area without its 0x30-byte NVN header, the ``c1``
     constant segment (the indirect-jump table the translator reads through
     ``LDC c[1]``), the stage, and the answer-table row of the game's profile
     (``shader_facts.tsv``: the compute thread group and shared memory only).

  3. **Reflect** the module into the ``.vk.json`` schema the packer reads:
     the descriptors with their NVN source and slot, the vertex input
     locations, the capabilities, and the frozen ``c1`` bytes.

  4. **Pack** ``.vk.json`` + ``.spv`` into the XGS1 bundle the Vulkan engine
     loads at run time.  The format is the one the reference host defines
     (``xbg_shader_header`` / ``xbg_shader_binding`` in
     ``dm/gfx/vulkan/engine/xbg_game.h``): a 32-byte header, the binding
     table, the vertex input locations, the ``c1`` bytes, then the SPIR-V.

The device fixes, in the order they are applied to the module:

  a. **Binding renumbering** — the engine has one descriptor set per pipeline,
     so the stages' bindings would collide: the original binding number
     becomes ``original * 6 + stage`` (vertex 0, hull 1, domain 2, geometry 3,
     pixel 4, compute 5).  A word-level edit; nothing else in the module moves.
  b. **Capability trimming** — the translator declares capabilities the module
     does not use (``Float64``, the subgroup family, ``DrawParameters``), and
     the device has neither ``shaderFloat64`` nor ``VK_EXT_fragment_shader_interlock``.
     ``spirv-opt --trim-capabilities`` removes the unused ones and leaves the
     instruction stream otherwise byte for byte the translator's.  The
     ``OpExtension "SPV_EXT_fragment_shader_interlock"`` declaration goes with
     it; ``scripts/graphics/fix_spirv_capabilities.py`` is used for the rest
     and refuses to drop a capability the module actually uses.
  c. **Storage image format capabilities** — a write to a storage image of
     unknown format needs ``StorageImageWriteWithoutFormat`` (56), a read
     needs ``StorageImageReadWithoutFormat`` (55).  The same tool computes it
     from the module's own content.
  d. **Workgroup type isolation** — a shared array must not inherit a UBO's
     explicit ArrayStride through an interned type. Clone only its Workgroup
     type; preserve every UBO layout and the function instruction stream.
  e. **Validation** — ``spirv-val --target-env vulkan1.1``.

No arithmetic or control flow is changed by these postprocessing rules. A
private translator prepared with ``prepare_ffrs_xlate.py`` can separately lower
Vulkan-illegal dynamic texture offsets through the upstream IR fallback; select
that executable explicitly with ``--translator`` for the affected shaders.

The name of a bundle is the **control key**, ``sha256(control[:0x880])[:16]``:
that is what the engine computes from the control section the guest hands NVN
and what it looks up on disk (the reference host's ``xbg_shader_key.c``).
The **content key**, ``sha256(control[:nctl] || code[:ncode])[:16]``, is the
game's own name for the same shader and is what the reference host's
``shader_map.tsv`` carries; it is used here as the internal identity and
recorded in the manifest next to the control key.

Usage:

    build_ffrs_xgs.py --all
    build_ffrs_xgs.py --keys 174293a30c315aed dbc58f224deb3525
    build_ffrs_xgs.py --all --limit 200 --jobs 8

The output goes to ``.local/shaders/ffrs/`` by default; nothing is written
outside ``.local/``.  Every run prints what it built, what it failed and why,
and writes a ``manifest.json`` with the byte counts and digests.
"""

import argparse
import collections
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import fix_spirv_capabilities as caps_tool  # noqa: E402  (the project's own capability tool)

# --------------------------------------------------------------------------------------
# paths
# --------------------------------------------------------------------------------------

UTOC_REL = 'romfs/FFRS/Content/Paks/FFRS-Switch.utoc'
REF_REL = 'refs/nxnative/dm/gfx/tools'
PROFILE_REL = 'refs/nxnative/dm/games/0100606026F12000'
D2R_REL = 'refs/d2r-mobile/dm/gfx/tools'
DECODER_REL = 'shader-verify/oodle_dec'
DOTNET_REL = 'shader-verify/dotnet'


def find_data_root(explicit):
    """The directory that holds the game's data, the reference trees and the tools.

    The worktree this script runs in usually has no ``.local`` of its own: the
    game's bytes and the reference trees live in the main checkout.  The
    candidates are tried in order and the first one that has the container wins.
    """
    candidates = []
    if explicit:
        candidates.append(explicit)
    if os.environ.get('FFRS_DATA_ROOT'):
        candidates.append(os.environ['FFRS_DATA_ROOT'])
    candidates.append(os.path.join(ROOT, '.local'))
    # <repo>/.claude/worktrees/<name> -> <repo>
    candidates.append(os.path.abspath(os.path.join(ROOT, '..', '..', '..', '.local')))
    for candidate in candidates:
        if os.path.exists(os.path.join(candidate, UTOC_REL)):
            return os.path.abspath(candidate)
    raise SystemExit(
        'the game container %s is not under any of: %s\n'
        'pass --data-root or set FFRS_DATA_ROOT' % (UTOC_REL, ', '.join(candidates)))


# --------------------------------------------------------------------------------------
# the game's own constants
# --------------------------------------------------------------------------------------

CONTROL_SIZE = 0x880
CONTROL_LENGTH_OFFSET = 0x20
CONTROL_C1_OFFSET = 0x700
CONTROL_CODE_LENGTH_OFFSET = 0x704

GFX_MAGIC = bytes.fromhex('78563412')     # 0x12345678, a graphics code area
CS_MAGIC = bytes.fromhex('79563412')      # 0x12345679, a compute code area
CTL_MAGIC = bytes.fromhex('34127698')     # 0x98761234, a control section

STAGE_OF_TAG = {'SV': 'vs', 'SP': 'ps', 'SC': 'cs', 'SG': 'gs', 'SH': 'hs', 'SD': 'ds'}
SPH_STAGE = {1: 'vs', 2: 'hs', 3: 'ds', 4: 'gs', 5: 'ps'}
STAGE_NO = {'vs': 0, 'hs': 1, 'ds': 2, 'gs': 3, 'ps': 4, 'cs': 5}

# The memory semantics of an interlocked workgroup barrier, and the builtin
# numbers of the draw parameters (the capability's number is 4427, the builtin's
# is 4425).
INTERLOCKED_WORKGROUP_MEMORY = 5335
BUILTIN_DRAW_PARAMETERS = (4425, 4426)
# The pixel interlock's extension is declared under two names.
INTERLOCK_EXTENSIONS = ('SPV_EXT_fragment_shader_interlock', 'EXT_fragment_shader_interlock')

# The NVN driver's c2 handle tables, one layout in every game (the reference host's
# dm/gfx/tools/dmsh_build.py): combined textures at 0x20, images at 0x120 (8 slots),
# separate textures at 0x168 (NVN texture slot 128 + r), separate samplers at 0x568.
COMBINED_TEX_BASE = 0x20
IMG_BASE = 0x120
IMG_SLOTS = 8
SEPTEX_BASE = 0x168
SEPTEX_SLOT0 = 128
SAMP_BASE = 0x568
SAMP_SLOTS = 128

# The storage buffer address tables in c0, per stage (dmsh_build.py SSBO).
SSBO_BASE = {'vs': 0x110, 'cs': 0x310, 'ps': 0x510}


# --------------------------------------------------------------------------------------
# keys
# --------------------------------------------------------------------------------------

def control_length(control):
    """How many bytes of the control section belong to the game's own name.

    A control section that states its own length is used as it is; a longer
    one only by its first 0x880 bytes.  The engine's key always uses 0x880.
    """
    own = struct.unpack_from('<I', control, CONTROL_LENGTH_OFFSET)[0]
    return own if 0x760 <= own < CONTROL_SIZE else CONTROL_SIZE


def content_key(control, code):
    """The game's own name for a shader: the control section and the code area together."""
    nctl = control_length(control)
    ncode = struct.unpack_from('<I', control, CONTROL_CODE_LENGTH_OFFSET)[0]
    if ncode > len(code):
        ncode = len(code)
    return hashlib.sha256(control[:nctl] + code[:ncode]).hexdigest()[:16]


def control_key(control):
    """The name the Vulkan engine looks the bundle up under.

    ``sha256`` of the first 0x880 bytes of the control section, first sixteen
    hex digits.  The reference host's ``xbg_shader_key.c`` computes exactly
    this (a hand-written SHA-256 over the same 0x880 bytes with the standard
    padding), so the two were checked against each other on a real shader.
    """
    return hashlib.sha256(control[:CONTROL_SIZE]).hexdigest()[:16]


def stage_of(code, tag):
    """The stage of a code area: a compute header says compute, otherwise the SPH says."""
    if code[:4] == CS_MAGIC or tag == 'SC':
        return 'cs'
    if code[:4] != GFX_MAGIC or len(code) < 0x34:
        return None
    word = struct.unpack_from('<I', code, 0x30)[0]
    if (word >> 10) & 0xF == 0 and word & 0x1F == 1:
        return 'vs'
    return SPH_STAGE.get((word >> 10) & 0xF)


# --------------------------------------------------------------------------------------
# the container
# --------------------------------------------------------------------------------------

class Oodle:
    """The local Oodle Mermaid decoder (the open source ``ooz`` implementation).

    It is a separate program: the Oodle code that nxnative would use is the
    game's own, inside the guest, and this host has no guest.  The decoder is
    built for x86_64, so it runs under Rosetta 2.
    """

    def __init__(self, program, work):
        self.program = program
        self.work = work
        self.counter = 0
        self.lock = threading.Lock()

    def decode(self, blob, raw_size):
        with self.lock:
            index = self.counter
            self.counter += 1
        source = os.path.join(self.work, 'in-%d.bin' % index)
        target = os.path.join(self.work, 'out-%d.bin' % index)
        with open(source, 'wb') as handle:
            handle.write(blob)
        result = subprocess.run([self.program, source, str(raw_size), target],
                                capture_output=True)
        if result.returncode != 0:
            raise RuntimeError('the Oodle decoder failed: %s' %
                               result.stderr.decode('utf-8', 'replace').strip()[:200])
        with open(target, 'rb') as handle:
            data = handle.read()
        os.unlink(source)
        os.unlink(target)
        if len(data) != raw_size:
            raise RuntimeError('the Oodle decoder gave %d bytes, the table says %d'
                               % (len(data), raw_size))
        return data


class Container:
    """The IoStore container's two shader libraries and their groups."""

    def __init__(self, utoc, oodle, tools):
        sys.path.insert(0, tools)
        import uepak as _uepak
        uepak = _uepak
        self.uepak = uepak
        self.oodle = oodle
        self.toc = uepak.Toc(utoc)
        self.read_lock = threading.Lock()
        self.libraries = {}
        for index in self.toc.of_type(8):          # EIoChunkType 8 = ShaderCodeLibrary
            self.libraries[index] = self._read_library(index)

    def _read_library(self, index):
        reader = self.uepak.R(self.toc.read(index, None))
        reader.take('<I')
        reader.raw(20 * reader.take('<I'))
        count = reader.take('<I')
        reader.raw(20 * count)
        group_ids = [reader.raw(12) for _ in range(reader.take('<I'))]
        for _ in range(reader.take('<I')):
            reader.take('<II')
        total = reader.take('<I')
        entries = struct.unpack_from('<%dQ' % total, reader.b, reader.o)
        reader.o += 8 * total
        groups = [reader.take('<IIII') for _ in range(reader.take('<I'))]
        key_count = reader.take('<I')
        reader.o += 4 * key_count
        return {'gids': group_ids, 'groups': groups, 'entries': entries}

    def group_data(self, library_index, group):
        """One shader group, decompressed."""
        library = self.libraries[library_index]
        first, count, raw, stored = library['groups'][group]
        with self.read_lock:
            blob = self.toc.read(self.toc.index[library['gids'][group]], None)
        if len(blob) != stored:
            raise RuntimeError('group %d holds %d bytes, its table says %d'
                               % (group, len(blob), stored))
        if stored < raw:
            blob = self.oodle.decode(blob, raw)
        return blob

    def records(self, library_index, group):
        """Every shader record of one group: (tag, control section, code area)."""
        data = self.group_data(library_index, group)
        out = []
        offset = 0
        while offset < len(data):
            record = self.uepak.lslg(data[offset:])
            if record is None:
                break
            out.append(record)
            nxt = data.find(b'LSLG', offset + 4)
            if nxt < 0:
                break
            offset = nxt
        return out


# --------------------------------------------------------------------------------------
# the translator job
# --------------------------------------------------------------------------------------

def job_for(key, control, code, stage, facts, work):
    """The translator's job for one shader, and the c1 bytes it needs.

    The code area of a graphics shader starts with a 0x30-byte NVN header and
    the 0x50-byte SPH after it, which the translator reads itself, so the
    header is stripped.  A compute code area of this game has the 0x12345679
    header, zero padding, and its instructions from +0x100; stripping only the
    header would decode the control words at the wrong place and the jump
    tables of the indirect branches with it.
    """
    c1_offset = struct.unpack_from('<I', control, CONTROL_C1_OFFSET)[0]
    c1_end = struct.unpack_from('<I', control, CONTROL_CODE_LENGTH_OFFSET)[0]
    c1 = code[c1_offset:c1_end]
    if stage == 'cs':
        if code[:4] == CS_MAGIC:
            if any(code[0x30:0x100]):
                raise ValueError('the compute code area +0x30..+0x100 is not all zero')
            body = code[0x100:]
        else:
            body = code
    else:
        body = code[0x30:]
    path = os.path.join(work, key + '.code.bin')
    with open(path, 'wb') as handle:
        handle.write(body)
    job = {'key': key, 'stage': stage, 'code': path,
           'tex': {}, 'img': {}, 'attr': {}, 'fout': {},
           'c1': c1.hex(), 'debug': True, 'reduced': True}
    if facts:
        job.update(facts)
    else:
        job['no_facts'] = True
    return job, c1, body


# --------------------------------------------------------------------------------------
# SPIR-V
# --------------------------------------------------------------------------------------

def instructions(words):
    """(index, opcode, operands) for every instruction after the header."""
    index = 5
    while index < len(words):
        count = words[index] >> 16
        if not count:
            break
        yield index, words[index] & 0xFFFF, words[index + 1:index + count]
        index += count


def read_words(data):
    if len(data) % 4:
        raise ValueError('the SPIR-V length %d is not a multiple of 4' % len(data))
    words = list(struct.unpack('<%dI' % (len(data) // 4), data))
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError('not a SPIR-V module')
    return words


def write_words(words):
    return struct.pack('<%dI' % len(words), *words)


def renumber_bindings(words, stage):
    """The binding numbers: original * 6 + stage.

    The engine has one descriptor set per pipeline, and each stage's bindings
    would otherwise start at 0 and collide.  A word-level edit: only the
    Binding decoration's operand changes, the module keeps its shape.
    """
    base = STAGE_NO[stage]
    out = list(words)
    for index, opcode, operands in instructions(words):
        if opcode == 71 and len(operands) >= 3 and operands[1] == 33:
            out[index + 3] = operands[2] * 6 + base
    return out


def trim_capabilities(words, spirv_opt):
    """The capabilities the module does not use, removed.

    With ``spirv-opt --trim-capabilities`` the tool rewrites the module: the
    instruction stream stays byte for byte the translator's, only the capability
    section shrinks (and the extension declaration of a capability that is gone
    goes with it).  Without the tool the known spurious declarations are
    dropped here, one by one, and only when the module does not use them.
    """
    if spirv_opt:
        with tempfile.NamedTemporaryFile(suffix='.spv', delete=False) as handle:
            handle.write(write_words(words))
            path = handle.name
        try:
            result = subprocess.run([spirv_opt, '--trim-capabilities', path, '-o', path],
                                    capture_output=True, text=True)
            if result.returncode != 0:
                return words, ('spirv-opt --trim-capabilities failed (%s); the capability set '
                               'was computed here instead'
                               % (result.stderr.strip().splitlines() or ['?'])[0][:120])
            with open(path, 'rb') as handle:
                return read_words(handle.read()), 'spirv-opt --trim-capabilities'
        finally:
            os.unlink(path)
    facts = caps_tool.analyse(words)
    used = set(facts['used'])
    drop = [c for c in (10, 61, 62, 63, 64, 65, 66, 4427, 5378)
            if c in facts['declared'] and c not in used]
    kept, _, dropped, _ = caps_tool.rewrite(words, [], drop)
    return kept, 'native trim (%s)' % ','.join(str(d) for d in dropped)


def provably_unused(words):
    """The capabilities the module's own content shows it does not use.

    Only these are dropped.  Every other capability the translator declared is
    kept, even when the module looks like it does not need it: the engine's
    capability whitelist asks the device about them, and a capability that is
    needed and gone is a module that fails to build in a way that is much
    harder to see than a refusal.
    """
    facts = caps_tool.analyse(words)
    unused = set()
    if 64 not in facts['float_widths']:
        unused.add(10)                            # Float64
    if 16 not in facts['float_widths']:
        unused.add(9)                             # Float16
    if 64 not in facts['int_widths']:
        unused.add(11)                            # Int64
    if 16 not in facts['int_widths']:
        unused.add(22)                            # Int16
    opcodes = {opcode for _, opcode, _ in instructions(words)}
    if not any(333 <= opcode <= 366 for opcode in opcodes):
        unused |= {61, 62, 63, 64, 65, 66} - set(facts['used'])
    draw_builtin = False
    interlock = False
    for _, opcode, operands in instructions(words):
        if opcode == 71 and len(operands) >= 3 and operands[1] == 11 and operands[2] in BUILTIN_DRAW_PARAMETERS:
            draw_builtin = True                   # BuiltIn DrawParameters
        if opcode in (224, 225) and operands and operands[-1] == INTERLOCKED_WORKGROUP_MEMORY:
            interlock = True                      # the interlocked workgroup memory barrier
    if not draw_builtin:
        unused.add(4427)                          # DrawParameters
    if not interlock:
        unused.add(5378)                          # FragmentShaderPixelInterlockEXT
    return unused & set(facts['declared'])


def apply_device_fixes(words, original_declared, unused):
    """The device fixes of the RG477M's Mali-G615.

    The module must not declare ``Float64`` (the device has no
    ``shaderFloat64``) and must not declare the pixel interlock (the device
    offers no ``VK_EXT_fragment_shader_interlock``); the extension declaration
    has to go with the capability, the driver looks at the declaration and not
    at the use.  A write to a storage image of unknown format needs
    ``StorageImageWriteWithoutFormat`` and a read needs
    ``StorageImageReadWithoutFormat``.

    ``spirv-opt --trim-capabilities`` is conservative about the subgroup
    capabilities and, on some modules of this game, wrong: it removes the
    ``DrawParameters`` and ``StorageImageExtendedFormats`` capabilities that
    the module does use, and the module then does not validate.  So the
    capability set is set here to what the module's own content says it needs:
    everything the translator declared, minus the capabilities the module
    provably does not use, plus the storage image format capabilities the
    module needs and does not declare.  ``spirv-val`` is the backstop.

    ``unused`` is computed on the module as the translator wrote it, before the
    trim: the trim may have removed a capability already, and that must not
    make it look needed again.
    """
    facts = caps_tool.analyse(words)
    needed = set(original_declared) - set(unused)
    for capability in (55, 56, 61, 64, 69, 70):           # image formats and subgroup BuiltIns
        if capability in facts['used']:
            needed.add(capability)
    current = set(facts['declared'])
    add = sorted(needed - current)
    drop = sorted(current & set(unused))
    drop_extension = []
    if 5378 in unused:
        for name in INTERLOCK_EXTENSIONS:
            if name in facts['extensions']:
                drop_extension.append(name)
    if not add and not drop and not drop_extension:
        return words, {'added': [], 'dropped': [], 'extensions_dropped': []}
    kept, added, dropped, removed = caps_tool.rewrite(words, add, drop, drop_extension)
    return kept, {'added': added, 'dropped': dropped, 'extensions_dropped': removed}


def spirv_info(words):
    """Capabilities, extensions and the variables' decorations, for the reflection."""
    caps = []
    exts = []
    deco = collections.defaultdict(dict)
    types = {}
    pointers = {}
    variables = {}
    names = {}
    consts = {}
    for _, opcode, operands in instructions(words):
        if opcode == 17:
            caps.append(operands[0])
        elif opcode == 10:
            text = b''.join(struct.pack('<I', word) for word in operands)
            exts.append(text.split(b'\0')[0].decode('utf-8', 'replace'))
        elif opcode == 5:
            text = b''.join(struct.pack('<I', word) for word in operands[1:])
            names[operands[0]] = text.split(b'\0')[0].decode('utf-8', 'replace')
        elif opcode == 43 and len(operands) >= 3:
            consts[operands[1]] = operands[2]
        elif opcode == 71:
            deco[operands[0]][operands[1]] = operands[2] if len(operands) > 2 else 1
        elif opcode in (19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30):
            types[operands[0]] = (opcode, list(operands[1:]))
        elif opcode == 32:
            pointers[operands[0]] = (operands[1], operands[2])
        elif opcode == 59:
            variables[operands[1]] = (operands[0], operands[2])
    return {'caps': caps, 'exts': exts, 'deco': deco, 'types': types,
            'pointers': pointers, 'variables': variables, 'names': names, 'consts': consts}


def isolate_workgroup_array_layout(words):
    """Give plain Workgroup arrays their own undecorated type.

    The translator interns array types: its support UBO's uint[8] (stride 16)
    can also become a Workgroup uint[8]. Vulkan 1.1 forbids the UBO's explicit
    layout on ordinary Workgroup memory. Keep the UBO type and every decoration
    intact, clone only the direct scalar/vector array used by a Workgroup
    pointer, and redirect that pointer. Element access chains keep their types.
    Whole-array values and nested aggregates are deliberately not rewritten;
    they need a separate, proven type/value transformation.
    """
    parsed = list(instructions(words))
    types = {ops[0]: (op, list(ops)) for _, op, ops in parsed
             if 19 <= op <= 33 and ops}
    strides = {ops[0] for _, op, ops in parsed
               if op == 71 and len(ops) >= 3 and ops[1] == 6}
    targets = {}
    for _, op, ops in parsed:
        if op != 32 or ops[1] != 4 or ops[2] not in strides:
            continue
        original = ops[2]
        array_op, array = types[original]
        if array_op != 28 or types.get(array[1], (None,))[0] not in (21, 22, 23):
            raise ValueError('unsupported explicitly laid out Workgroup aggregate %d' % original)
        if any(op == 71 and operands[0] == original and operands[1] != 6
               for _, op, operands in parsed):
            raise ValueError('additional Workgroup array decorations require a separate proof')
        targets.setdefault(original, []).append(ops[0])
    if not targets:
        return words, {'cloned_arrays': []}
    for _, op, ops in parsed:
        # Loading or constructing a complete array would need its result type
        # changed too. All evidenced failures use scalar element operations.
        if (ops and ops[0] in targets and
                op not in (5, 6, 71, 72) and not 19 <= op <= 39):
            raise ValueError('whole Workgroup array value requires type rewrite (opcode %d)' % op)
        if op in (73, 74, 75):
            raise ValueError('decoration groups require a separate Workgroup layout proof')
    bound = words[3]
    clones = {original: bound + i for i, original in enumerate(sorted(targets))}
    pointer_targets = {pointer: original for original, pointers in targets.items()
                       for pointer in pointers}
    out = list(words[:5])
    out[3] = bound + len(clones)
    emitted = set()
    for _, op, operands in parsed:
        ops = list(operands)
        if op == 32 and ops[0] in pointer_targets:
            original = pointer_targets[ops[0]]
            if original not in emitted:
                array = list(types[original][1])
                array[0] = clones[original]
                out.extend([(len(array) + 1) << 16 | 28, *array])
                emitted.add(original)
            ops[2] = clones[original]
        out.extend([(len(ops) + 1) << 16 | op, *ops])
    return out, {'cloned_arrays': [
        {'original_type': original, 'workgroup_type': clones[original],
         'pointer_types': targets[original]}
        for original in sorted(targets)]}


def image_descriptor(info, type_id):
    """The image shape of a descriptor, as the bundle format carries it."""
    types = info['types']
    opcode, operands = types[type_id]
    while opcode in (27, 28, 29):
        type_id = operands[0]
        opcode, operands = types[type_id]
    sampled_type = operands[0]
    dim = operands[1]
    depth = operands[2]
    arrayed = operands[3]
    multisampled = operands[4]
    image_format = operands[6]
    scalar_opcode, scalar_operands = types[sampled_type]
    if scalar_opcode == 22:
        scalar = {'kind': 'float'}
    else:
        scalar = {'kind': 'int', 'signed': bool(scalar_operands[1])}
    return {'format': image_format, 'dim': dim, 'arrayed': arrayed,
            'multisampled': multisampled, 'depth': depth, 'scalar': scalar}


def decode_handle(slots, handle, image):
    """The NVN slot a texture or image handle names.

    The handle is the immediate the shader uses on its texture instruction: it
    carries a word index into the driver's c2 table (and a second one for a
    separate sampler) and the handle's type.  ``slots`` says which constant
    buffer the index is relative to; only the driver's c2 (index 2) holds the
    handle tables.

    Returns (slot, sampler slot, handle type, table name), or None when the
    handle does not name a slot.
    """
    if slots >= 0 and (slots & 0xFFFF) != 2:
        return None
    offset0 = handle & 0x3FFF
    offset1 = (handle >> 14) & 0x3FFF
    handle_type = handle >> 28
    if handle_type == 4:
        # The direct handle (0x40000000) is Ryujinx's marker for the bindless
        # fallback: a texture the shader picks at run time, which the
        # translator turns into an array of 32 textures and 32 samplers.  The
        # game profile's key tex_dyn_slots says this game has 2,077 such vertex
        # shaders.
        return None
    # Type 0 (a combined handle) carries one word, which holds the texture id
    # and the sampler id together; types 1 and 2 carry the sampler's word as
    # well; type 3 (a constant sampler) carries the sampler's id, not a word.
    words = [offset0] + ([offset1] if handle_type in (1, 2) else [])

    def table_of(word, base, count):
        at = word * 4
        if at < base or at >= base + 8 * count or (at - base) % 8:
            return None
        return (at - base) // 8

    if image:
        for word in words:
            slot = table_of(word, IMG_BASE, IMG_SLOTS)
            if slot is not None:
                return slot, None, handle_type, 'image'
        return None

    texture_slot = None
    table = None
    for word in words:
        slot = table_of(word, SEPTEX_BASE, (SAMP_BASE - SEPTEX_BASE) // 8)
        if slot is not None:
            texture_slot, table = slot, 'separate_texture'
            break
    if texture_slot is None:
        for word in words:
            slot = table_of(word, COMBINED_TEX_BASE, (IMG_BASE - COMBINED_TEX_BASE) // 8)
            if slot is not None:
                texture_slot, table = slot, 'combined_texture'
                break
    if texture_slot is None:
        # Texture instructions can read a driver image handle (two FFRS compute
        # shaders have cp_t_tcb_48 plus cp_i_cb2_48): same bound image, sampled
        # view. This is not combined texture slot32; c2+0x120 is image slot0.
        for word in words:
            slot = table_of(word, IMG_BASE, IMG_SLOTS)
            if slot is not None:
                return slot, None, handle_type, 'image_texture'
        return None
    sampler_slot = None
    if handle_type in (1, 2):
        for word in words:
            slot = table_of(word, SAMP_BASE, SAMP_SLOTS)
            if slot is not None:
                sampler_slot = slot
                break
    return texture_slot, sampler_slot, handle_type, table


# --------------------------------------------------------------------------------------
# the reflection: the module and the translator's answer table -> .vk.json
# --------------------------------------------------------------------------------------

CORE_LAYER_TARGET = {'instance_api_version': (1 << 22) | (2 << 12),
                     'physical_api_version': (1 << 22) | (2 << 12),
                     'shaderOutputLayer': True, 'shaderOutputViewportIndex': True}


def promote_layer_output_core(words):
    """One vertex Layer declaration route: SPIR-V1.3 EXT -> core SPIR-V1.5.

    No executable instruction, output decoration, type, value or ID changes.
    SPIR-V1.4+ requires ALL global variables in the entry-point interface;
    append them in declaration order, retaining existing interface order.
    Unknown extensions/stages/member Layer layouts refuse this narrow route.
    """
    facts = caps_tool.analyse(words)
    extension = 'SPV_EXT_shader_viewport_index_layer'
    if (words[1] > 0x00010300 or facts['entry_models'] != [0] or
            facts['extensions'] != [extension] or 5254 not in facts['declared'] or
            facts['output_builtins'] != [9]):
        raise ValueError('core Layer promotion requires the reviewed legacy vertex Layer-only declaration')
    globals_, entry_points, in_function = [], [], False
    layer_ids, output_ids = set(), set()
    for _, op, operands in instructions(words):
        if op == 54: in_function = True
        if op == 56: in_function = False
        if op == 59 and not in_function:
            globals_.append(operands[1])
            if operands[2] == 3: output_ids.add(operands[1])
        if op == 71 and len(operands) >= 3 and operands[1:3] == [11, 9]: layer_ids.add(operands[0])
        if op == 72 and len(operands) >= 4 and operands[2] == 11 and operands[3] in (9, 10):
            raise ValueError('member Layer/ViewportIndex promotion is not covered')
        if op == 15: entry_points.append(operands)
    if len(entry_points) != 1 or not layer_ids or not layer_ids <= output_ids:
        raise ValueError('core Layer promotion requires one entry and actual output Layer variables')
    out, interfaces = list(words[:5]), []
    out[1] = 0x00010500
    for _, op, operands in instructions(words):
        if op == 10: continue  # exact extension established above
        if op == 17 and operands == [5254]: operands = [69]
        if op == 15:
            end = 2
            while end < len(operands):
                word = operands[end]
                end += 1
                if any(((word >> shift) & 255) == 0 for shift in (0, 8, 16, 24)): break
            else: raise ValueError('unterminated entry-point name')
            old = operands[end:]
            if not layer_ids <= set(old): raise ValueError('Layer output missing from original entry-point interface')
            added = [identifier for identifier in globals_ if identifier not in old]
            operands = operands[:end] + old + added
            interfaces.append({'original': old, 'added_globals': added})
        if len(operands) + 1 > 65535: raise ValueError('entry-point interface exceeds SPIR-V word count')
        out.extend([(len(operands) + 1) << 16 | op, *operands])
    executable = lambda w: [(op, operands) for _, op, operands in instructions(w) if op not in (10, 15, 17)]
    if executable(out) != executable(words): raise AssertionError('Layer promotion changed executable/type/output instructions')
    return out, {'from_version': words[1], 'to_version': out[1], 'capability_replaced': [5254, 69],
                 'extension_removed': extension, 'global_variables': globals_, 'entry_interfaces': interfaces,
                 'all_other_instructions_identical': True}


def require_output_features(words, target_features=None):
    """Offline target contract only; runtime independently gates actual enabled facts."""
    declared = set(caps_tool.analyse(words)['declared'])
    if not declared & {69, 70}: return
    target = target_features or {}
    if (target.get('instance_api_version', 0) < CORE_LAYER_TARGET['instance_api_version'] or
            target.get('physical_api_version', 0) < CORE_LAYER_TARGET['physical_api_version'] or
            (69 in declared and target.get('shaderOutputLayer') is not True) or
            (70 in declared and target.get('shaderOutputViewportIndex') is not True)):
        raise ValueError('core shader output requires explicit Vulkan1.2 enabled-feature target contract')
    if words[1] != 0x00010500:
        raise ValueError('core shader output currently requires validated SPIR-V1.5')


def reflect(key, stage, c1, words, result, slot_base, target_features=None):
    """The .vk.json the packer reads: the descriptors with their NVN source and slot.

    The sources come out of the same decoding the reference host's
    ``dmsh_build.py`` uses: the translator's answer table says which NVN
    constant bank a uniform buffer is, and the texture and image handles are
    decoded against the driver's c2 tables.
    """
    require_output_features(words, target_features)
    if stage != 'vs' and set(caps_tool.analyse(words)['declared']) & {69, 70}:
        raise ValueError('core shader outputs currently require the reviewed vertex stage')
    info = spirv_info(words)
    if info['exts']:
        raise ValueError('the module still declares an extension: ' + ','.join(info['exts']))
    by_binding = {}
    for variable, (pointer_type, storage) in info['variables'].items():
        if 33 in info['deco'][variable] and storage in (0, 2, 12):
            by_binding[info['deco'][variable][33]] = (variable, info['pointers'][pointer_type][1], storage)

    base_no = STAGE_NO[stage]
    descriptors = []

    def binding_of(original):
        return original * 6 + base_no

    def block_bytes(type_id):
        opcode, operands = info['types'][type_id]
        if opcode == 30 and operands and info['types'][operands[0]][0] == 28:
            return 16 * info['consts'].get(info['types'][operands[0]][1][1], 0)
        return 0

    # The support buffer: the translator emits it in every module, at binding 0.
    support = by_binding.get(binding_of(0))
    if support and info['names'].get(support[0], '') == 'support_buffer':
        descriptors.append({'name': 'support_buffer', 'binding': binding_of(0),
                            'type': 'uniform_buffer', 'count': 1,
                            'source': {'category': 'support_buffer'}})

    for buffer in result['cbufs']:
        found = by_binding.get(binding_of(buffer['binding']))
        if not found:
            raise ValueError('the constant buffer %d is not in the module' % buffer['binding'])
        variable, type_id, _ = found
        slot = buffer['slot']
        if 3 <= slot <= 16:
            nvn = {'kind': 'uniform', 'slot': slot - 3}
        elif slot == 1:
            nvn = {'kind': 'c1', 'slot': 0}
        else:
            nvn = {'kind': 'driver', 'bank': slot, 'slot': 0}
        descriptors.append({'name': info['names'].get(variable, ''),
                            'binding': binding_of(buffer['binding']),
                            'type': 'uniform_buffer', 'count': 1,
                            'source': {'category': 'nvn', 'nvn': nvn},
                            'block': {'bytes': block_bytes(type_id)}})

    storage_buffers = sorted(result['sbufs'], key=lambda b: b['binding'])
    stage_base = SSBO_BASE.get(stage)
    for buffer in storage_buffers:
        found = by_binding.get(binding_of(buffer['binding']))
        if not found:
            raise ValueError('the storage buffer %d is not in the module' % buffer['binding'])
        variable, type_id, _ = found
        offset = buffer['cb_offset'] * 4 if buffer['cb_slot'] == 0 else None
        if offset is None:
            raise ValueError('the storage buffer %d is not in c0' % buffer['binding'])
        if stage == 'cs' and 0x210 <= offset < 0x310 and (offset - 0x210) % 16 == 0:
            nvn = {'kind': 'uniform_pointer', 'slot': (offset - 0x210) // 16}
        elif stage_base is not None and 0 <= offset - stage_base < 0x100 and (offset - stage_base) % 16 == 0:
            nvn = {'kind': 'storage_pointer', 'slot': (offset - stage_base) // 16}
        else:
            raise ValueError('the storage buffer slot of %d does not decode (%s, offset %#x)'
                             % (buffer['binding'], stage, offset))
        descriptors.append({'name': info['names'].get(variable, ''),
                            'binding': binding_of(buffer['binding']),
                            'type': 'storage_buffer', 'count': 1,
                            'source': {'category': 'nvn', 'nvn': nvn},
                            'block': {'bytes': block_bytes(type_id)}})

    for kind, entries in (('texture', result['textures']), ('image', result['images'])):
        for entry in entries:
            found = by_binding.get(binding_of(entry['binding']))
            if not found:
                raise ValueError('the %s %d is not in the module' % (kind, entry['binding']))
            variable, type_id, _ = found
            image = image_descriptor(info, type_id)
            decoded = decode_handle(entry['cbuf_slot'], entry['handle'], kind == 'image')
            if not decoded:
                if (entry['handle'] >> 28) == 4:
                    raise ValueError(
                        'dynamic-texture-array: a texture the shader picks at run time (the '
                        'translator\'s bindless fallback of 32 textures and 32 samplers) has '
                        'no representation in the XGS1 bundle format')
                raise ValueError('the %s handle %#x does not name a slot' % (kind, entry['handle']))
            slot, sampler_slot, handle_type, table = decoded
            if kind == 'texture':
                if slot is None:
                    raise ValueError('the texture handle %#x names only a sampler' % entry['handle'])
                descriptor_type = 'uniform_texel_buffer' if image['dim'] == 5 else 'combined_image_sampler'
                if table == 'separate_texture':
                    nvn_slot = slot + SEPTEX_SLOT0 if slot_base == 'nvn' else slot
                else:
                    nvn_slot = slot
                nvn = {'kind': 'image' if table == 'image_texture' else 'texture', 'slot': nvn_slot,
                       'sampler_slot': sampler_slot if handle_type else None}
            else:
                descriptor_type = 'storage_texel_buffer' if image['dim'] == 5 else 'storage_image'
                nvn = {'kind': 'image', 'slot': slot}
            descriptors.append({'name': info['names'].get(variable, ''),
                                'binding': binding_of(entry['binding']),
                                'type': descriptor_type, 'count': 1,
                                'source': {'category': 'nvn', 'nvn': nvn,
                                           'handle_decode': {'type': handle_type}},
                                'image': image,
                                'table': table})

    descriptors.sort(key=lambda d: d['binding'])
    inputs = []
    if stage == 'vs':
        for variable, (_, storage) in info['variables'].items():
            decorations = info['deco'][variable]
            if storage == 1 and 30 in decorations and 11 not in decorations:
                inputs.append({'storage': 'input', 'location': decorations[30]})

    return {'identity': {'key': key},
            'sha256': hashlib.sha256(write_words(words)).hexdigest(),
            'stage': stage,
            'capabilities': sorted(set(info['caps'])),
            'descriptors': descriptors,
            'interfaces': inputs,
            'frozen_job': {'c1': c1.hex()}}


# --------------------------------------------------------------------------------------
# the packer
# --------------------------------------------------------------------------------------

TYPES = {'sampler': 0, 'combined_image_sampler': 1, 'sampled_image': 2, 'storage_image': 3,
         'uniform_texel_buffer': 4, 'storage_texel_buffer': 5, 'uniform_buffer': 6,
         'storage_buffer': 7}
STAGES = {'vs': 0, 'hs': 1, 'ds': 2, 'gs': 3, 'ps': 4, 'cs': 5}


def pack(vk, spirv):
    """The XGS1 bundle: the header, the binding table, the vertex input locations,
    the c1 bytes, then the SPIR-V.

    This is the reference host's ``pack_game_shaders.py`` (the bundle format it
    defines), with the two tables the reference packer reads.
    """
    key = vk['identity']['key']
    if hashlib.sha256(spirv).hexdigest() != vk['sha256']:
        raise ValueError('the SPIR-V hash does not match the reflection')
    bindings = []
    for descriptor in vk['descriptors']:
        source_info = descriptor['source']
        nvn = source_info.get('nvn', {})
        kind = nvn.get('kind')
        slot = nvn.get('slot', 0)
        if source_info['category'] == 'support_buffer':
            source = 1
        elif kind in ('uniform', 'uniform_pointer'):
            source = 2
        elif kind == 'storage_pointer':
            source = 3
        elif kind == 'c1':
            source = 4
        elif kind == 'driver' and nvn['bank'] in (0, 2):
            source = 5 if nvn['bank'] == 0 else 6
        elif kind in ('texture', 'image') and slot is not None:
            source = 7 if kind == 'texture' else 8
        else:
            raise ValueError('the descriptor %s has no source: %s'
                             % (descriptor['name'], source_info))
        size = 1392 if source == 1 else descriptor.get('block', {}).get('bytes', 0)
        sampler = nvn.get('sampler_slot')
        sampler = 0xFFFFFFFF if sampler is None else sampler
        flags = 0
        if descriptor['type'] in ('storage_image', 'storage_texel_buffer') \
                and descriptor.get('image', {}).get('format') == 0:
            if 56 in vk['capabilities']:
                flags |= 1
            if 55 in vk['capabilities']:
                flags |= 2
        if 'image' in descriptor:
            image = descriptor['image']
            scalar = image['scalar']
            kind_code = 0 if scalar['kind'] == 'float' else 2 if scalar.get('signed') else 1
            flags |= ((1 << 31) | (image['dim'] << 8) | (image['arrayed'] << 12)
                      | (image['multisampled'] << 13) | ((image['depth'] == 1) << 14)
                      | (kind_code << 16))
        if source in (7, 8):
            handle_type = source_info['handle_decode']['type']
            if handle_type not in (0, 1):
                raise ValueError('the handle type %d is not supported' % handle_type)
        bindings.append(struct.pack('<8I', descriptor['binding'], TYPES[descriptor['type']],
                                    descriptor['count'], size, source, slot or 0, sampler, flags))
    locations = sorted({x['location'] for x in vk['interfaces']
                         if x['storage'] == 'input' and x['location'] is not None}) \
        if vk['stage'] == 'vs' else []
    c1 = bytes.fromhex(vk['frozen_job'].get('c1', ''))
    if len(c1) % 4:
        raise ValueError('the c1 bytes are not 4-byte aligned')
    header = struct.pack('<8I', 0x31534758, 1, STAGES[vk['stage']], len(bindings),
                          len(locations), len(c1), len(spirv), 0)
    return key, header + b''.join(bindings) + struct.pack('<%dI' % len(locations), *locations) + c1 + spirv


# --------------------------------------------------------------------------------------
# the build
# --------------------------------------------------------------------------------------

def load_facts(path):
    facts = {}
    if not os.path.exists(path):
        return facts
    with open(path, encoding='utf-8') as handle:
        for line in handle:
            if not line.strip() or line.startswith('#'):
                continue
            key, _, payload = line.rstrip('\n').partition('\t')
            facts[key] = json.loads(payload)
    return facts


def load_map(path):
    """The reference host's shader map for this game: key -> stage.

    It is the game's own name for each shader, generated by the reference host
    from the same bytes; it is used here to check what this script finds.
    """
    stages = {}
    if not os.path.exists(path):
        return stages
    with open(path, encoding='utf-8') as handle:
        for line in handle:
            if not line.strip() or line.startswith('#'):
                continue
            columns = line.rstrip('\n').split('\t')
            if len(columns) > 3:
                stages.setdefault(columns[3], columns[2])
    return stages


def extract(container, wanted, out_dir, facts, index_path, workers, report):
    """Every wanted shader out of the container, with its job written.

    Returns the list of (content key, control key, stage, job, c1).

    The whole container is walked once: 5,919 groups, each an Oodle
    decompression and a run of ``LSLG`` records.  The walk is the expensive
    part, so the result is written down as an index (content key, control key,
    library, group, record, stage) and a later run with ``--keys`` goes
    straight to the groups that hold the wanted shaders.
    """
    index = {}
    by_control = {}
    if wanted and index_path and os.path.exists(index_path):
        with open(index_path, encoding='utf-8') as handle:
            for line in handle:
                if not line.strip() or line.startswith('#'):
                    continue
                columns = line.rstrip('\n').split('\t')
                if len(columns) >= 6:
                    index[columns[0]] = (int(columns[2]), int(columns[3]), int(columns[4]),
                                         columns[5])
                    by_control[columns[1]] = columns[0]
        if index:
            print('index     : %s (%d shaders)' % (index_path, len(index)), flush=True)

    results = []
    seen = set()
    seen_lock = threading.Lock()
    index_rows = []
    groups = []
    for library_index in sorted(container.libraries):
        for group in range(len(container.libraries[library_index]['groups'])):
            groups.append((library_index, group))

    def take(library_index, group, record_index, records):
        """One shader out of one group: its key, its stage and its job."""
        if record_index >= len(records):
            raise RuntimeError('group %d holds %d records, not %d'
                               % (group, len(records), record_index + 1))
        tag, control, code = records[record_index]
        key = content_key(control, code)
        control_id = control_key(control)
        stage = stage_of(code, tag)
        if stage is None:
            report['stage_failures'].append({'key': key, 'reason': 'no stage in the code area'})
            return None
        if stage != STAGE_OF_TAG.get(tag, stage):
            report['stage_mismatches'].append({'key': key, 'tag': tag, 'stage': stage})
        try:
            job, c1, _ = job_for(key, control, code, stage, facts.get(key), out_dir)
        except Exception as error:
            report['job_failures'].append({'key': key, 'reason': str(error)[:200]})
            return None
        return (key, control_id, stage, job, c1)

    if wanted and index:
        # The index knows where each wanted shader is: only those groups are read.
        places = {}
        for key in wanted:
            content = key if key in index else by_control.get(key)
            if not content:
                continue
            place = index[content]
            places.setdefault((place[0], place[1]), []).append((content, place[2]))
        for (library_index, group), wanted_here in sorted(places.items()):
            try:
                records = container.records(library_index, group)
            except Exception as error:
                report['group_failures'].append(
                    {'library': library_index, 'group': group, 'reason': str(error)[:200]})
                continue
            for key, record_index in wanted_here:
                entry = take(library_index, group, record_index, records)
                if entry:
                    results.append(entry)
        return results

    def scan_group(library_index, group):
        """One group: every shader of it, with its job written."""
        out = []
        try:
            records = container.records(library_index, group)
        except Exception as error:
            report['group_failures'].append(
                {'library': library_index, 'group': group, 'reason': str(error)[:200]})
            return out
        for record_index, (tag, control, code) in enumerate(records):
            key = content_key(control, code)
            control_id = control_key(control)
            with seen_lock:
                if key in seen:
                    continue
                seen.add(key)
            index_rows.append((key, control_id, library_index, group, record_index,
                               stage_of(code, tag) or ''))
            if wanted and key not in wanted and control_id not in wanted:
                continue
            entry = take(library_index, group, record_index, records)
            if entry:
                out.append(entry)
        return out

    done = 0
    with cf.ThreadPoolExecutor(workers) as pool:
        for entries in pool.map(lambda item: scan_group(*item), groups):
            results.extend(entries)
            done += 1
            if done % 200 == 0:
                print('  scanned %d/%d groups, %d shaders' % (done, len(groups), len(seen)),
                      flush=True)

    if index_path:
        os.makedirs(os.path.dirname(index_path), exist_ok=True)
        with open(index_path, 'w', encoding='utf-8') as handle:
            handle.write('# content_key\tcontrol_key\tlibrary\tgroup\trecord\tstage\n')
            for row in sorted(index_rows):
                handle.write('\t'.join(str(x) for x in row) + '\n')
        print('index     : %s (%d shaders)' % (index_path, len(index_rows)), flush=True)
    return results


def translate(xlate, jobs, out_dir, dotnet_root, batch_size, workers):
    """Run the translator over the jobs, in batches.  Returns the failures.

    Each batch is one run of the translator, which is a separate process (a
    .NET application), so the batches run side by side.
    """
    failures = {}
    batches = [jobs[i:i + batch_size] for i in range(0, len(jobs), batch_size)]
    environment = dict(os.environ)
    if dotnet_root:
        environment['DOTNET_ROOT'] = dotnet_root

    def run(number, batch):
        job_path = os.path.join(out_dir, 'job-%05d.json' % number)
        with open(job_path, 'w') as handle:
            json.dump(batch, handle)
        result = subprocess.run([xlate, job_path, out_dir], capture_output=True, text=True,
                                env=environment)
        errors = {}
        for line in result.stderr.splitlines():
            match = re.match(r'\[RED\]\s*(\S+):\s*(.*)', line)
            if match:
                errors[match.group(1)] = match.group(2)[:200]
        if result.returncode not in (0, 1):
            errors['__batch__%d__' % number] = (
                'the translator exited with %d: %s'
                % (result.returncode, result.stderr.strip()[-200:]))
        return errors

    done = 0
    with cf.ThreadPoolExecutor(workers) as pool:
        for errors in pool.map(lambda item: run(*item), list(enumerate(batches))):
            failures.update(errors)
            done += 1
            if done % 50 == 0:
                print('  translated %d/%d batches' % (done, len(batches)), flush=True)
    return failures


def validate(spirv, spirv_val, target_env='vulkan1.1'):
    """What ``spirv-val`` says about a module for the Vulkan 1.1 target.

    Returns (ok, first error line, the capabilities the error names).
    """
    if not spirv_val:
        return True, '', []
    with tempfile.NamedTemporaryFile(suffix='.spv', delete=False) as handle:
        handle.write(spirv)
        path = handle.name
    try:
        result = subprocess.run([spirv_val, '--target-env', target_env, path],
                                capture_output=True, text=True)
    finally:
        os.unlink(path)
    if result.returncode == 0:
        return True, '', []
    lines = [line for line in result.stderr.strip().splitlines() if line.strip()]
    first = lines[0] if lines else '?'
    names = []
    for line in lines:
        match = re.search(r'requires one of these capabilities: (.+)$', line)
        if match:
            names.extend(name.strip() for name in match.group(1).split(','))
    return False, first, names


def reference_pack(vk, spirv, tools):
    """The reference host's own packer, on the same reflection and module.

    It is the definition of the bundle format, so a bundle that this script
    writes and a bundle the reference packer writes from the same inputs have
    to be byte for byte the same.
    """
    if not tools or not os.path.exists(os.path.join(tools, 'pack_game_shaders.py')):
        return None
    if tools not in sys.path:
        sys.path.insert(0, tools)
    import pack_game_shaders
    with tempfile.TemporaryDirectory() as directory:
        key = vk['identity']['key']
        with open(os.path.join(directory, key + '.spv'), 'wb') as handle:
            handle.write(spirv)
        with open(os.path.join(directory, key + '.vk.json'), 'w', encoding='utf-8') as handle:
            json.dump(vk, handle, ensure_ascii=False)
        _, data = pack_game_shaders.pack(__import__('pathlib').Path(directory, key + '.vk.json'))
    return data


def build_one(key, control_id, stage, job, c1, work, out_dir, spirv_opt, spirv_val, slot_base,
              packer_tools=None, target_features=None, core_layer_output=False):
    """One shader: translate, fix, reflect, pack.  Returns a report row."""
    row = {'key': key, 'control_key': control_id, 'stage': stage}
    try:
        with open(os.path.join(work, key + '.res.json'), encoding='utf-8') as handle:
            result = json.load(handle)
    except (OSError, ValueError) as error:
        row.update(status='failed', reason='no reflection from the translator: %s' % str(error)[:160])
        return row
    try:
        with open(os.path.join(work, key + '.spv'), 'rb') as handle:
            words = read_words(handle.read())
    except (OSError, ValueError) as error:
        row.update(status='failed', reason='no SPIR-V from the translator: %s' % str(error)[:160])
        return row
    try:
        if core_layer_output:
            words, row['layer_core_promotion'] = promote_layer_output_core(words)
        target_env = 'vulkan1.2' if words[1] == 0x00010500 else 'vulkan1.1'
        require_output_features(words, target_features)
        row['validation_target'] = target_env
        words = renumber_bindings(words, stage)
        words, layout_fixes = isolate_workgroup_array_layout(words)
        row['workgroup_layout_fixes'] = layout_fixes
        original_declared = set(caps_tool.analyse(words)['declared'])
        unused = provably_unused(words)
        words, trim_method = trim_capabilities(words, spirv_opt)
        words, fixes = apply_device_fixes(words, original_declared, unused)
        spirv = write_words(words)
        row['capability_trim'] = trim_method
        row['capabilities'] = sorted(set(caps_tool.analyse(words)['declared']))
        row['capability_fixes'] = fixes
        # Keep the exact module handed to the validator when --keep-work is
        # used, including failures; the translator's .spv remains untouched.
        device_path = os.path.join(work, key + '.device.spv')
        # The validator names the capability a module is missing; the translator
        # does not declare some of them (the storage image format capabilities
        # and, on some modules, the extended storage image formats).  The
        # capability is added and the module is validated again.
        for _ in range(6):
            with open(device_path, 'wb') as handle:
                handle.write(spirv)
            ok, first, names = validate(spirv, spirv_val, target_env)
            if ok:
                break
            numbers = [caps_tool.CAPABILITY_NAMES.get(name) for name in names]
            if not numbers or any(number is None for number in numbers):
                row.update(status='failed', reason='spirv-val: %s' % first[:160])
                return row
            words, _, _, _ = caps_tool.rewrite(words, numbers, [])
            spirv = write_words(words)
            row.setdefault('capability_fixes', {})['added'] = \
                list(row.get('capability_fixes', {}).get('added', [])) + names
        else:
            row.update(status='failed', reason='spirv-val: %s' % first[:160])
            return row
        row['capabilities'] = sorted(set(caps_tool.analyse(words)['declared']))
        vk = reflect(key, stage, c1, words, result, slot_base, target_features)
        with open(os.path.join(work, key + '.vk.json'), 'w') as handle:
            json.dump(vk, handle, indent=1, sort_keys=True)
    except Exception as error:                            # a shader that does not fit the schema
        row.update(status='failed', reason='%s: %s' % (type(error).__name__, str(error)[:150]))
        return row
    try:
        _, data = pack(vk, spirv)
    except Exception as error:
        row.update(status='failed', reason='pack: %s' % str(error)[:150])
        return row
    if packer_tools:
        try:
            other = reference_pack(vk, spirv, packer_tools)
        except Exception as error:
            row.update(status='failed',
                       reason='the reference packer failed: %s' % str(error)[:150])
            return row
        if other is not None and other != data:
            row.update(status='failed',
                       reason='the bundle differs from the reference packer\'s bundle')
            return row
        row['reference_packer'] = 'same bytes' if other is not None else 'not available'
    path = os.path.join(out_dir, control_id + '.xgs')
    with open(path, 'wb') as handle:
        handle.write(data)
    try:
        check_bundle(path, vk, spirv)
    except Exception as error:
        row.update(status='failed', reason='the bundle does not read back: %s' % str(error)[:150])
        return row
    row.update(status='ok', bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
               name=os.path.basename(path))
    return row


def check_bundle(path, vk, spirv):
    """The bundle, read back, against the reflection it was made from."""
    with open(path, 'rb') as handle:
        data = handle.read()
    magic, version, stage, binding_count, input_count, c1_bytes, spirv_bytes, reserved = \
        struct.unpack_from('<8I', data, 0)
    if magic != 0x31534758 or version != 1:
        raise ValueError('the header is not an XGS1 bundle')
    if stage != STAGES[vk['stage']]:
        raise ValueError('the header names stage %d, the reflection says %s'
                         % (stage, vk['stage']))
    if binding_count != len(vk['descriptors']):
        raise ValueError('the header names %d bindings, the reflection has %d'
                         % (binding_count, len(vk['descriptors'])))
    if spirv_bytes != len(spirv):
        raise ValueError('the header names %d SPIR-V bytes, the module is %d'
                         % (spirv_bytes, len(spirv)))
    offset = 32 + 32 * binding_count + 4 * input_count + c1_bytes
    if offset + spirv_bytes != len(data):
        raise ValueError('the bundle is %d bytes, its header describes %d'
                         % (len(data), offset + spirv_bytes))
    if data[offset:offset + spirv_bytes] != spirv:
        raise ValueError('the SPIR-V in the bundle is not the module')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--data-root', default=None,
                        help='the directory that holds the game data and the reference trees')
    parser.add_argument('--out', default=None, help='where the .xgs bundles go')
    parser.add_argument('--keys', nargs='*', default=None,
                        help='the shaders to build: content keys or control keys (default: all)')
    parser.add_argument('--all', action='store_true', help='build every shader of the game')
    parser.add_argument('--limit', type=int, default=0, help='stop after this many shaders')
    parser.add_argument('--jobs', type=int, default=int(os.environ.get('J', '8')),
                        help='how many shaders are translated at a time')
    parser.add_argument('--batch', type=int, default=32,
                        help='how many shaders one translator run carries')
    parser.add_argument('--slot-base', choices=('nvn', 'table'), default='nvn',
                        help="the texture slot in a bundle: 'nvn' is the NVN slot (128 + the "
                             "separate texture table index), 'table' is the table index itself")
    parser.add_argument('--manifest', default=None, help='where the manifest goes')
    parser.add_argument('--index', default=None,
                        help='where the shader index goes (written by a full walk, read by '
                             'a later run with --keys)')
    parser.add_argument('--keep-work', action='store_true', help='keep the intermediate files')
    parser.add_argument('--translator', help='explicit translator executable (for a prepared private fork)')
    parser.add_argument('--dotnet-root', help='runtime root for the selected translator')
    parser.add_argument('--core-layer-output', action='store_true',
                        help='single VS only: promote reviewed legacy Layer to SPIR-V1.5; target requires actually enabled Vulkan1.2 output features')
    parser.add_argument('--no-validate', action='store_true', help='skip spirv-val')
    args = parser.parse_args(argv)

    data_root = find_data_root(args.data_root)
    out_dir = args.out or os.path.join(ROOT, '.local', 'shaders', 'ffrs')
    os.makedirs(out_dir, exist_ok=True)

    xlate = args.translator or os.path.join(data_root, REF_REL, 'dmsh_xlate_bin', 'dmsh_xlate')
    decoder = os.path.join(data_root, DECODER_REL)
    dotnet_root = args.dotnet_root or (os.path.join(data_root, DOTNET_REL)
                                     if os.path.exists(os.path.join(data_root, DOTNET_REL)) else None)
    profile = os.path.join(data_root, PROFILE_REL)
    facts = load_facts(os.path.join(profile, 'shader_facts.tsv'))
    packer_tools = os.path.join(data_root, D2R_REL)
    map_stages = load_map(os.path.join(profile, 'shader_map.tsv'))

    spirv_opt = shutil.which(os.environ.get('SPIRV_OPT', 'spirv-opt'))
    spirv_val = None if args.no_validate else shutil.which(os.environ.get('SPIRV_VAL', 'spirv-val'))
    if args.no_validate:
        pass
    elif not spirv_val:
        print('[warn] spirv-val is not on the PATH: the modules are not validated')

    for path, what in ((xlate, 'the translator'), (decoder, 'the Oodle decoder')):
        if not os.path.exists(path):
            raise SystemExit('%s is not at %s' % (what, path))

    report = {'group_failures': [], 'stage_failures': [], 'stage_mismatches': [],
              'job_failures': []}
    work = tempfile.mkdtemp(prefix='ffrs_xgs_', dir=os.environ.get('WORK'))
    try:
        print('container : %s' % os.path.join(data_root, UTOC_REL))
        print('output    : %s' % out_dir)
        print('translator: %s' % xlate)
        print('tools     : spirv-opt=%s spirv-val=%s slot-base=%s'
              % (spirv_opt or 'none', spirv_val or 'none', args.slot_base))
        oodle = Oodle(decoder, work)
        container = Container(os.path.join(data_root, UTOC_REL), oodle,
                              os.path.join(data_root, REF_REL))
        print('libraries : %s' % ', '.join('%d (%d groups)'
                                           % (i, len(container.libraries[i]['groups']))
                                           for i in sorted(container.libraries)))
        wanted = set(args.keys or [])
        index_path = args.index or os.path.join(out_dir, 'index.tsv')
        found = extract(container, wanted, work, facts, index_path, args.jobs, report)
        if args.limit:
            found = found[:args.limit]
        print('shaders   : %d selected' % len(found), flush=True)
        if not found:
            print('nothing to build')
            return 1
        if args.core_layer_output and (len(found) != 1 or found[0][2] != 'vs' or args.no_validate or not spirv_val):
            raise SystemExit('--core-layer-output requires exactly one VS and real validation')
        jobs = [entry[3] for entry in found]
        failures = translate(xlate, jobs, work, dotnet_root, args.batch, args.jobs)
        print('translated: %d jobs, %d translator errors' % (len(jobs), len(failures)))

        rows = []
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            futures = [pool.submit(build_one, key, control_id, stage, job, c1, work, out_dir,
                                   spirv_opt, spirv_val, args.slot_base, packer_tools,
                                   CORE_LAYER_TARGET if args.core_layer_output else None, args.core_layer_output)
                       for key, control_id, stage, job, c1 in found]
            for future in cf.as_completed(futures):
                rows.append(future.result())
        rows.sort(key=lambda r: r['key'])

        # The translation errors that never reached the worker are failures of their own.
        for key, reason in failures.items():
            if key.startswith('__batch__'):
                rows.append({'key': key, 'status': 'failed', 'reason': reason})
            elif not any(r['key'] == key for r in rows):
                rows.append({'key': key, 'status': 'failed', 'reason': 'translation: ' + reason})

        manifest = {
            'container': os.path.join(data_root, UTOC_REL),
            'output': out_dir,
            'slot_base': args.slot_base,
            'translator': os.path.abspath(xlate),
            'translator_sha256': hashlib.sha256(open(xlate, 'rb').read()).hexdigest(),
            'translator_dll_sha256': (hashlib.sha256(open(os.path.splitext(xlate)[0] + '.dll', 'rb').read()).hexdigest()
                                      if os.path.exists(os.path.splitext(xlate)[0] + '.dll') else None),
            'spirv_opt': bool(spirv_opt),
            'spirv_val': bool(spirv_val),
            'name_rule': 'the bundle is named by the control key: sha256(control[:0x880])[:16]',
            'shaders': rows,
            'group_failures': report['group_failures'],
            'stage_failures': report['stage_failures'],
            'stage_mismatches': report['stage_mismatches'],
            'job_failures': report['job_failures'],
        }
        manifest_path = args.manifest or os.path.join(out_dir, 'manifest.json')
        with open(manifest_path, 'w') as handle:
            json.dump(manifest, handle, indent=1, sort_keys=True)

        ok = [r for r in rows if r['status'] == 'ok']
        bad = [r for r in rows if r['status'] != 'ok']
        by_stage = collections.Counter(r['stage'] for r in ok)
        total = sum(r['bytes'] for r in ok)
        print('built     : %d bundles (%s), %d bytes total'
              % (len(ok), ', '.join('%s %d' % (s, n) for s, n in sorted(by_stage.items())), total))
        print('failed    : %d' % len(bad))
        reasons = collections.Counter(r['reason'].split(':')[0] for r in bad)
        for reason, count in reasons.most_common(12):
            print('  %5d  %s' % (count, reason))
        for row in bad[:12]:
            print('  ! %s: %s' % (row['key'], row['reason'][:160]))
        if len(bad) > 12:
            print('  ... %d more in %s' % (len(bad) - 12, manifest_path))
        print('manifest  : %s' % manifest_path)
        return 0 if not bad else 1
    finally:
        if args.keep_work:
            print('work dir  : %s' % work)
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())

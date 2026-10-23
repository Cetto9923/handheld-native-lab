#!/usr/bin/env python3
"""Offline census of guest TPIDR_EL0 reads in an uncompressed NSO0.

Reads a caller supplied NSO0 and reports where the module reads the thread
pointer register, which TP relative offsets are visible around each read, and
whether the reviewed main TLS template bounds still hold. The script never
executes target code, never writes to the input, and never touches a device.

Offset recovery is deliberately conservative. A read is "strict" when the
instruction window before the MRS holds the MOVZ/MOVK pair that materialises
the offset in a register, and the offset is then the MOVK immediate. Every
other read is reported as "non strict" with no offset, because its offset
comes from a value the module computed elsewhere.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

MRS_TPIDR_EL0 = 0xD53BD040
MRS_TPIDRRO_EL0 = 0xD53BD060
MSR_TPIDR_EL0 = 0xD51BD040
MSR_TPIDRRO_EL0 = 0xD51BD060
CENSUS = {
    "mrs_tpidr_el0": MRS_TPIDR_EL0,
    "mrs_tpidrro_el0": MRS_TPIDRRO_EL0,
    "msr_tpidr_el0": MSR_TPIDR_EL0,
    "msr_tpidrro_el0": MSR_TPIDRRO_EL0,
}
# Reviewed main module facts. The template is the initialized part of the
# module TLS block; the linker total extent reaches further into ordinary
# module data and must never be copied into a mirror.
TEMPLATE_START = 0x7767108
TEMPLATE_INIT_SIZE = 0x220
TEMPLATE_TOTAL_SIZE = 0x1019
TEMPLATE_ALIGN = 8
TEMPLATE_HEADER = 0x10
TEMPLATE_SHA256 = "2236311146eecaa733a64cda6108fece94e6693159ba1d3add9bb3f0899834bf"


def parse_nso(path):
    data = path.read_bytes()
    if len(data) < 256 or data[:4] != b"NSO0":
        raise ValueError("expected an uncompressed NSO0")
    segments = [struct.unpack_from("<IIII", data, 16 + i * 16) for i in range(3)]
    return data, segments


def segment_of(segments, vaddr):
    """Return (file_offset, vaddr, size) of the segment that holds vaddr."""
    for file_offset, memory_offset, size, _align in segments:
        if memory_offset <= vaddr < memory_offset + size:
            return file_offset, memory_offset, size
    return None


def file_offset_of(segments, vaddr):
    found = segment_of(segments, vaddr)
    if found is None:
        return None
    file_offset, memory_offset, _size = found
    return file_offset + (vaddr - memory_offset)


def text_words(data, segments):
    """Words of the executable segment, with the module address of word 0."""
    file_offset, vaddr, size, _align = segments[0]
    raw = data[file_offset:file_offset + size]
    count = len(raw) // 4
    return list(struct.unpack_from(f"<{count}I", raw, 0)), vaddr


def decode_move_immediate(word):
    """Decode MOVZ and MOVK 64 bit forms.

    Returns (kind, register, value) or None. MOVZ masks the destination to
    its immediate; MOVK keeps the other bits, so only the immediate is
    reported here and the caller combines the pair.
    """
    if (word & 0xFF800000) != 0xD2800000 and (word & 0xFF800000) != 0xF2800000:
        return None
    kind = "movz" if (word & 0xFF800000) == 0xD2800000 else "movk"
    shift = ((word >> 21) & 3) * 16
    immediate = (word >> 5) & 0xFFFF
    register = word & 31
    return kind, register, immediate << shift


def memory_access(word):
    """Decode AArch64 load and store forms that carry a constant displacement.

    Returns (kind, offset, base, value_register) or None. Only the unsigned
    offset and the unscaled forms are decoded. A register offset form does
    not describe a TP relative constant and is rejected, so a caller never
    reads a register displacement as if it were a fixed address.

    The layouts were checked against assembled reference instructions:
    ldr x0,[x1,#0x10] is 0xf9400820, ldur x0,[x1,#0x10] is 0xf8410020,
    ldr x0,[x1,x2] is 0xf8626820, ldrb w0,[x1,#0x10] is 0x39404020,
    ldurb w0,[x1,#0x10] is 0x38410020 and ldrb w0,[x1,x2] is 0x38626820.
    Bit 24 selects the unsigned offset family from the unscaled family, bit
    22 selects load from store, and bit 21 marks the register offset forms.
    """
    if (word & 0x3C000000) != 0x38000000:
        return None
    scale = 1 << ((word >> 30) & 3)
    if (word >> 24) & 1:
        # Unsigned offset immediate, imm12 at bits 20:10. Bit 23 marks the
        # prefetch form, which is not a data access.
        if (word >> 23) & 1:
            return None
        offset = ((word >> 10) & 0xFFF) * scale
    elif (word >> 21) & 1:
        # Register offset, pre index or post index: not a constant.
        return None
    else:
        # Unscaled form, signed imm9 at bits 20:12.
        immediate = (word >> 12) & 0x1FF
        if immediate & 0x100:
            immediate -= 0x200
        offset = immediate
    kind = "load" if (word >> 22) & 1 else "store"
    return kind, offset, (word >> 5) & 31, word & 31


def strict_offset(words, index, window):
    """Recover the TP offset that the instruction before the MRS built.

    The reviewed module materialises the offset with MOVZ xd,#0,LSL#16
    followed by MOVK xd,#imm, so the pair yields the offset directly. The
    search walks backwards and accepts the pair at any distance inside the
    window, because the compiler may insert hints between the pair and the
    read.
    """
    for back in range(1, window + 1):
        pair = index - back - 1
        if pair < 0:
            break
        movk = decode_move_immediate(words[pair + 1])
        movz = decode_move_immediate(words[pair])
        if not movk or not movz:
            continue
        if movk[0] != "movk" or movz[0] != "movz":
            continue
        if movk[1] != movz[1]:
            continue
        if movz[2] != 0:
            continue
        return movk[2], movk[1], back + 1
    return None


def census(words):
    counts = {name: 0 for name in CENSUS}
    for word in words:
        for name, encoding in CENSUS.items():
            if (word & ~31) == encoding:
                counts[name] += 1
    return counts


def site_records(words, base_vaddr, window):
    records = []
    for index, word in enumerate(words):
        if (word & ~31) != MRS_TPIDR_EL0:
            continue
        register = word & 31
        strict = strict_offset(words, index, window)
        accesses = []
        for step in range(1, window + 1):
            if index + step >= len(words):
                break
            access = memory_access(words[index + step])
            if not access:
                continue
            kind, offset, base, value_register = access
            if base == register:
                accesses.append({"delta": step * 4, "kind": kind, "offset": offset,
                                 "value_register": value_register})
        records.append({
            "address": base_vaddr + index * 4,
            "register": register,
            "word": f"0x{word:08x}",
            "strict": strict is not None,
            "offset": strict[0] if strict else None,
            "offset_register": strict[1] if strict else None,
            "offset_distance": strict[2] if strict else None,
            "immediate_accesses": accesses,
        })
    return records


def offset_histogram(records):
    histogram = {}
    for record in records:
        if record["offset"] is None:
            continue
        key = record["offset"]
        histogram[key] = histogram.get(key, 0) + 1
    return histogram


def template_facts(data, segments):
    facts = {
        "template_start": TEMPLATE_START,
        "initialized_size": TEMPLATE_INIT_SIZE,
        "total_size": TEMPLATE_TOTAL_SIZE,
        "alignment": TEMPLATE_ALIGN,
        "reserved_header_size": TEMPLATE_HEADER,
    }
    init_end = TEMPLATE_START + TEMPLATE_INIT_SIZE
    total_end = TEMPLATE_START + TEMPLATE_TOTAL_SIZE
    facts["initialized_end"] = init_end
    facts["total_end"] = total_end
    facts["tail_size"] = TEMPLATE_TOTAL_SIZE - TEMPLATE_INIT_SIZE
    init_offset = file_offset_of(segments, TEMPLATE_START)
    tail_offset = file_offset_of(segments, init_end)
    if init_offset is None or tail_offset is None:
        facts["initialized_in_file"] = False
        facts["tail_in_file"] = False
        return facts
    facts["initialized_in_file"] = True
    facts["tail_in_file"] = True
    facts["initialized_file_offset"] = init_offset
    facts["tail_file_offset"] = tail_offset
    initialized = data[init_offset:init_offset + TEMPLATE_INIT_SIZE]
    tail = data[tail_offset:tail_offset + (total_end - init_end)]
    facts["initialized_nonzero_bytes"] = sum(1 for byte in initialized if byte)
    facts["initialized_sha256"] = hashlib.sha256(initialized).hexdigest()
    facts["initialized_sha256_matches_review"] = \
        facts["initialized_sha256"] == TEMPLATE_SHA256
    facts["tail_nonzero_bytes"] = sum(1 for byte in tail if byte)
    facts["tail_is_module_data_not_template"] = facts["tail_nonzero_bytes"] > 0
    return facts


def disassemble(data, segments, addresses, out, objdump):
    """Emit exact AArch64 disassembly evidence for selected module addresses."""
    slices = []
    for address in addresses:
        offset = file_offset_of(segments, address - 16)
        if offset is None:
            continue
        raw = data[offset:offset + 48]
        if len(raw) != 48:
            continue
        stem = out / f"slice-{address:x}"
        source = stem.with_suffix(".S")
        words = struct.unpack_from(f"<{len(raw) // 4}I", raw, 0)
        source.write_text(".text\n.globl _slice\n_slice:\n" +
                          "\n".join(f".inst 0x{word:08x}" for word in words) + "\n")
        commands = [["clang", "-target", "aarch64-linux-android", "-c", str(source),
                     "-o", str(stem.with_suffix(".o"))],
                    [str(objdump), "-d", "--no-show-raw-insn",
                     str(stem.with_suffix(".o"))]]
        logs = []
        for index, command in enumerate(commands):
            process = subprocess.run(command, capture_output=True, text=True)
            stem.with_suffix(f".{index}.stdout.log").write_text(process.stdout)
            stem.with_suffix(f".{index}.stderr.log").write_text(process.stderr)
            logs.append({"command": command, "returncode": process.returncode})
            if process.returncode:
                raise RuntimeError(f"static disassembly failed: {process.stderr}")
        slices.append({"address": address, "file_offset": offset,
                       "sha256": hashlib.sha256(raw).hexdigest(), "commands": logs})
    return slices


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--window", type=int, default=8,
                        help="instructions inspected before each TPIDR_EL0 read")
    parser.add_argument("--sample", type=int, default=64,
                        help="number of sites recorded in full")
    parser.add_argument("--disassemble", type=lambda v: int(v, 0), action="append",
                        default=[], help="module virtual address to disassemble as evidence")
    parser.add_argument("--llvm-objdump", type=Path, required=True,
                        help="llvm-objdump binary used for the optional evidence slices")
    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit("out must be a new directory")
    args.out.mkdir(parents=True)
    data, segments = parse_nso(args.input)
    words, base_vaddr = text_words(data, segments)
    counts = census(words)
    records = site_records(words, base_vaddr, args.window)
    strict = [record for record in records if record["strict"]]
    histogram = offset_histogram(records)
    report = {
        "input": str(args.input),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "segments": [{"file_offset": s[0], "vaddr": s[1], "size": s[2], "align": s[3]}
                     for s in segments],
        "text_vaddr": base_vaddr,
        "tls_instruction_counts": counts,
        "read_sites": len(records),
        "strict_sites": len(strict),
        "non_strict_sites": len(records) - len(strict),
        "distinct_offsets": len(histogram),
        "registers": {},
        "offset_window_instructions": args.window,
        "offset_histogram": {hex(offset): count for offset, count in sorted(histogram.items())},
        "template": template_facts(data, segments),
        "sampled_sites": records[:args.sample],
    }
    for record in records:
        key = f"x{record['register']}"
        report["registers"][key] = report["registers"].get(key, 0) + 1
    report["slices"] = disassemble(data, segments, args.disassemble, args.out,
                                   args.llvm_objdump)
    (args.out / "inspection.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"sha256": report["sha256"], "read_sites": report["read_sites"],
                      "strict_sites": report["strict_sites"],
                      "non_strict_sites": report["non_strict_sites"],
                      "distinct_offsets": report["distinct_offsets"],
                      "tls_instruction_counts": counts,
                      "template": report["template"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

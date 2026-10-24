#!/usr/bin/env python3
"""Report which NVN entry points the guest actually calls.

Read-only. The guest resolves every NVN entry point by name at start-up and
stores the resolved pointer in a global table. An entry point that the guest
never calls is a name the binary mentions but does not use, which matters: an
implementation for such a name cannot be validated by the guest, and its
signature cannot be confirmed from a call site.

This script rebuilds the table, then reports for every entry point whether its
slot is ever loaded and branched to.

Usage:
  python3 scripts/runtime/nvn_api_usage.py
  python3 scripts/runtime/nvn_api_usage.py --name nvnMemoryPoolBuilderGetMemory
"""

import argparse
import struct
from pathlib import Path

import capstone

NSO = Path(".local/uncompressed/main.bin")
TSV = Path("evidence/nvn/guest_nvn_code_references.tsv")


def load(path):
    data = path.read_bytes()
    seg = [struct.unpack_from("<IIII", data, 16 + i * 16) for i in range(3)]
    return data, seg


def file_offset(seg, addr, length=1):
    for f, m, n, _ in seg:
        if m <= addr and length <= n - (addr - m):
            return f + addr - m
    raise ValueError(f"0x{addr:x}+{length:x} outside file-backed segments")


def text_range(data, seg):
    f, m, n, _ = seg[0]
    return m, m + n, data[file_offset(seg, m, n): file_offset(seg, m, n) + n]


def window(data, seg, md, center, before, after):
    lo = max(0, center - before * 4)
    hi = center + after * 4
    raw = data[file_offset(seg, lo, hi - lo): file_offset(seg, lo, hi - lo) + (hi - lo)]
    return list(md.disasm(raw, lo))


def slot_of(data, seg, md, adrp_addr):
    """The table slot an entry point is stored in, read from the lookup code."""
    insns = window(data, seg, md, adrp_addr, 0, 16)
    if not insns or insns[0].address != adrp_addr:
        return None
    name_reg = insns[0].operands[0].reg
    add_index = None
    for i, ins in enumerate(insns):
        if (ins.mnemonic == "add" and len(ins.operands) == 3
                and ins.operands[0].reg == name_reg and ins.operands[1].reg == name_reg):
            add_index = i
            break
    if add_index is None:
        return None
    for i in range(add_index + 1, len(insns)):
        ins = insns[i]
        if ins.mnemonic != "str" or len(ins.operands) != 2:
            continue
        mem = ins.operands[1]
        if mem.type != capstone.arm64.ARM64_OP_MEM:
            continue
        base_reg, disp = mem.mem.base, mem.mem.disp
        page = None
        for near in list(insns[max(0, i - 5):i]) + list(insns[i + 1:i + 4]):
            if (near.mnemonic == "adrp" and len(near.operands) == 2
                    and near.operands[0].reg == base_reg):
                page = near.operands[1].imm
                break
        if page is None:
            return None
        return page, disp, base_reg
    return None


def slot_loads(data, seg, md, page, disp, base_reg, limit=4000):
    """Every place the slot is loaded, with the call that follows it."""
    sites = []
    for adrp_addr in adrp_sites(data, seg, page):
        insns = window(data, seg, md, adrp_addr, 2, 12)
        for i, ins in enumerate(insns):
            if ins.mnemonic != "ldr" or len(ins.operands) != 3:
                continue
            mem = ins.operands[1]
            if mem.type != capstone.arm64.ARM64_OP_MEM:
                continue
            if mem.mem.disp != disp or mem.mem.base != base_reg:
                continue
            sites.append((ins.address, insns))
        if len(sites) > limit:
            break
    return sites


def adrp_sites(data, seg, page):
    import numpy as np
    lo, hi, text = text_range(data, seg)
    w = np.frombuffer(text, dtype="<u4")
    is_adrp = (w & 0x9F000000) == 0x90000000
    out = []
    for i in np.nonzero(is_adrp)[0]:
        insn = int(w[i])
        immlo = (insn >> 29) & 3
        immhi = (insn >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & 0x100000:
            imm -= 0x200000
        if ((lo + int(i) * 4) & ~0xFFF) + (imm << 12) == page:
            out.append(lo + int(i) * 4)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", action="append", default=[])
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    data, seg = load(NSO)
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    census = {}
    for line in TSV.read_text().splitlines():
        if line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 4:
            census[parts[0]] = (int(parts[1], 16), int(parts[2]), int(parts[3]))

    # The table page is shared by every entry point, so it is computed once.
    page_cache = {}
    used = []
    unused = []
    no_slot = []
    for name, (adrp_addr, count, lookups) in sorted(census.items()):
        slot = slot_of(data, seg, md, adrp_addr)
        if not slot:
            no_slot.append(name)
            continue
        page, disp, base_reg = slot
        if page not in page_cache:
            page_cache[page] = adrp_sites(data, seg, page)
        loads = slot_loads(data, seg, md, page, disp, base_reg)
        entry = (name, page + disp, loads)
        if loads:
            used.append(entry)
        else:
            unused.append(entry)

    print(f"names in census: {len(census)}")
    print(f"slot found: {len(used) + len(unused)}   no slot: {len(no_slot)}")
    print(f"slot loaded somewhere: {len(used)}   never loaded: {len(unused)}")
    if args.name:
        wanted = set(args.name)
        for name, addr, loads in used + unused:
            if name in wanted:
                print(f"\n=== {name} slot=0x{addr:x} loads={len(loads)} ===")
                for at, insns in loads:
                    print(f"  loaded at 0x{at:x}")
                    if args.verbose:
                        for ins in insns:
                            print(f"    0x{ins.address:x}: {ins.mnemonic} {ins.op_str}")
    else:
        print("\nnever loaded:")
        for name, addr, _ in unused:
            print(f"  {name} slot=0x{addr:x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

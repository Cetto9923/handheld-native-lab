#!/usr/bin/env python3
"""Find the guest call sites of the NVN memory pool entry points.

Read-only. The guest resolves every NVN entry point by name at start-up and
stores the returned function pointer in a global table; this script rebuilds
that table from the guest's own lookup code, then finds the indirect calls that
go through each slot, so that the argument registers of a real call site can
be read instead of guessed.

The guest's lookup block looks like this:

    adrp x1, <name page>
    add  x1, x1, #<name offset>     ; the entry point name
    mov  x0, x20                    ; the device
    blr  x19                        ; nvnDeviceGetProcAddress
    adrp x8, <table page>
    str  x0, [x8, #<slot offset>]   ; the resolved function pointer

The compiler hoists the next name above the store, so a name belongs to the
call that follows it and to the store that follows that call.

The guest calls an entry point by loading its pointer out of that table, so
the call sites are the places where the table page is materialised and a slot
offset is loaded from it.

Usage:
  python3 scripts/runtime/nvn_pool_callers.py
  python3 scripts/runtime/nvn_pool_callers.py --name nvnMemoryPoolBuilderGetMemory --window 24
"""

import argparse
import struct
from pathlib import Path

import capstone

NSO = Path(".local/uncompressed/main.bin")
TSV = Path("evidence/nvn/guest_nvn_code_references.tsv")
POOL_NAMES = [
    "nvnMemoryPoolBuilderSetDevice",
    "nvnMemoryPoolBuilderSetDefaults",
    "nvnMemoryPoolBuilderSetStorage",
    "nvnMemoryPoolBuilderSetFlags",
    "nvnMemoryPoolBuilderGetDevice",
    "nvnMemoryPoolBuilderGetMemory",
    "nvnMemoryPoolBuilderGetSize",
    "nvnMemoryPoolBuilderGetFlags",
    "nvnMemoryPoolInitialize",
    "nvnMemoryPoolSetDebugLabel",
    "nvnMemoryPoolFinalize",
    "nvnMemoryPoolMap",
    "nvnMemoryPoolFlushMappedRange",
    "nvnMemoryPoolInvalidateMappedRange",
    "nvnMemoryPoolGetBufferAddress",
    "nvnMemoryPoolMapVirtual",
    "nvnMemoryPoolGetSize",
    "nvnMemoryPoolGetFlags",
]


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


def read_tsv():
    """name -> first name-materialisation address, from the reference census."""
    out = {}
    for line in TSV.read_text().splitlines():
        if line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        out[parts[0]] = int(parts[1], 16)
    return out


def disassemble_window(data, seg, md, center, before, after):
    lo = max(0, center - before * 4)
    hi = center + after * 4
    raw = data[file_offset(seg, lo, hi - lo): file_offset(seg, lo, hi - lo) + (hi - lo)]
    return list(md.disasm(raw, lo))


def find_lookup_slots(data, seg, md, census, names):
    """name -> (table page, slot offset), read from the guest's lookup code."""
    slots = {}
    for name in names:
        adrp_addr = census.get(name)
        if adrp_addr is None:
            continue
        insns = disassemble_window(data, seg, md, adrp_addr, 0, 16)
        if not insns or insns[0].address != adrp_addr:
            continue
        name_reg = insns[0].operands[0].reg

        # The add that completes the name address.
        add_index = None
        for i, ins in enumerate(insns):
            if (ins.mnemonic == "add" and len(ins.operands) == 3
                    and ins.operands[0].reg == name_reg
                    and ins.operands[1].reg == name_reg):
                add_index = i
                break
        if add_index is None:
            continue

        # The store that records the resolved pointer of the lookup.
        #
        # The compiler schedules the store *before* the call it belongs to:
        #
        #     adrp x1, <name page>
        #     add  x1, x1, #<name offset>
        #     str  x0, [x8, #<slot offset>]   <- stores the result of the blr
        #     mov  x0, x20
        #     blr  x19                        <- the lookup for this name
        #
        # so the store that follows the name is the slot of this entry point,
        # and the call that follows it is the lookup.
        disp = None
        base_reg = None
        table_page = None
        store_index = None
        for i in range(add_index + 1, len(insns)):
            ins = insns[i]
            if ins.mnemonic == "str" and len(ins.operands) == 2:
                mem = ins.operands[1]
                if mem.type == capstone.arm64.ARM64_OP_MEM:
                    disp = mem.mem.disp
                    base_reg = mem.mem.base
                    store_index = i
                    # The adrp that sets the base register can sit before or
                    # after the store, depending on the schedule.
                    for near in list(insns[max(0, i - 5):i]) + list(insns[i + 1:i + 4]):
                        if (near.mnemonic == "adrp" and len(near.operands) == 2
                                and near.operands[0].reg == base_reg):
                            table_page = near.operands[1].imm
                            break
                    break
        if disp is None or table_page is None or store_index is None:
            continue

        # The lookup call that uses this name.
        call_index = None
        for i in range(store_index + 1, len(insns)):
            if insns[i].mnemonic == "blr":
                call_index = i
                break
        if call_index is None:
            continue
        slots[name] = (table_page, disp, base_reg)
    return slots


def adrp_sites(text, lo, page):
    """Every address where an adrp materialises the given page."""
    import numpy as np
    w = np.frombuffer(text, dtype="<u4")
    is_adrp = (w & 0x9F000000) == 0x90000000
    idx = np.nonzero(is_adrp)[0]
    out = []
    for i in idx:
        insn = int(w[i])
        immlo = (insn >> 29) & 3
        immhi = (insn >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & 0x100000:
            imm -= 0x200000
        target = ((lo + int(i) * 4) & ~0xFFF) + (imm << 12)
        if target == page:
            out.append(lo + int(i) * 4)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", action="append", default=[])
    parser.add_argument("--window", type=int, default=14,
                        help="instructions of context to print around a call site")
    args = parser.parse_args()

    data, seg = load(NSO)
    lo, hi, text = text_range(data, seg)
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True
    census = read_tsv()
    wanted = args.name or POOL_NAMES
    slots = find_lookup_slots(data, seg, md, census, wanted)

    for name in wanted:
        slot = slots.get(name)
        if not slot:
            print(f"{name}: no slot found")
            continue
        page, disp, base_reg = slot
        print(f"\n=== {name}  slot=0x{page + disp:x} (page 0x{page:x} disp 0x{disp:x}) ===")
        sites = adrp_sites(text, lo, page)
        if not sites:
            print("  no adrp of the table page found")
            continue
        found = 0
        for site in sites:
            insns = disassemble_window(data, seg, md, site, 2, args.window + 8)
            for i, ins in enumerate(insns):
                if ins.mnemonic != "ldr" or len(ins.operands) != 3:
                    continue
                mem = ins.operands[1]
                if mem.type != capstone.arm64.ARM64_OP_MEM:
                    continue
                if mem.mem.disp != disp or mem.mem.base != base_reg:
                    continue
                loaded = ins.operands[0].reg
                call = None
                for j in range(i + 1, min(i + 12, len(insns))):
                    nxt = insns[j]
                    if (nxt.mnemonic == "blr" and len(nxt.operands) == 1
                            and nxt.operands[0].reg == loaded):
                        call = j
                        break
                if call is None:
                    continue
                found += 1
                print(f"  call site at module offset 0x{insns[call].address:x}")
                start = max(0, i - args.window)
                end = min(len(insns), call + 3)
                for ins2 in insns[start:end]:
                    mark = "   <<< CALL" if ins2.address == insns[call].address else ""
                    print(f"    0x{ins2.address:x}: {ins2.mnemonic} {ins2.op_str}{mark}")
        if not found:
            print(f"  slot is never read into a call ({len(sites)} table page uses)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

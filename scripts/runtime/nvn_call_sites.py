#!/usr/bin/env python3
"""Recover the guest call sites of an NVN entry point from the guest image.

Read-only. The guest resolves every NVN entry point by name at start-up and
stores the resolved pointer in a table. The lookup code has this shape:

    adrp x8, #<table page>
    adrp x1, #<name page>
    add  x1, x1, #<name offset>     ; the entry point name
    str  x0, [x8, #<slot offset>]   ; the resolved function pointer

and a call site has this shape:

    adrp x25, #<table page>
    ldr  x8, [x25, #<slot offset>]  ; the function pointer
    add  x0, x21, #<offset>         ; the first argument
    blr  x8                        ; the call

So the slot of a name is read out of the lookup code, and the calls are the
places that load that slot and branch to it. The instructions around the call
are printed, so the arguments the guest really passes and the value it really
reads back can be read instead of guessed.

Usage:
  python3 scripts/runtime/nvn_call_sites.py --name nvnBlendStateSetDefaults
  python3 scripts/runtime/nvn_call_sites.py --name nvnProgramSetShadersExt --window 16
"""

import argparse
import struct
from pathlib import Path

import capstone

NSO = Path(".local/uncompressed/main.bin")


def load(path):
    data = path.read_bytes()
    seg = [struct.unpack_from("<IIII", data, 16 + i * 16) for i in range(3)]
    return data, seg


def file_offset(seg, addr, length=1):
    for f, m, n, _ in seg:
        if m <= addr and length <= n - (addr - m):
            return f + addr - m
    raise ValueError(f"0x{addr:x}+{length:x} outside file-backed segments")


def read_cstring(data, seg, addr):
    off = file_offset(seg, addr)
    end = data.index(0, off, off + 96)
    return data[off:end].decode("utf-8", "replace")


def window(data, seg, md, center, before, after):
    lo = max(0, center - before * 4)
    hi = center + after * 4
    raw = data[file_offset(seg, lo, hi - lo): file_offset(seg, lo, hi - lo) + (hi - lo)]
    return list(md.disasm(raw, lo))


def build_name_table(data, seg, md):
    """Every NVN name the guest resolves, with the table slots it lands in.

    The lookup code repeats the same block once per name:

        adrp x8, #<table page>
        adrp x1, #<name page>
        add  x1, x1, #<name offset>     ; the entry point name
        str  x0, [x8, #<slot offset>]   ; the resolved function pointer

    The name is read back from the string the `add` completes, and the slot
    comes from the store. The same name is resolved more than once, so a name
    can have more than one slot; every slot is kept.
    """
    lo, hi = 0x142d000, 0x1432000
    raw = data[file_offset(seg, lo, hi - lo): file_offset(seg, lo, hi - lo) + (hi - lo)]
    # The text segment holds data islands; skipping them keeps the walk going
    # through the whole region instead of stopping at the first one.
    md.skipdata = True
    insns = list(md.disasm(raw, lo))
    md.skipdata = False

    # The name every `add` completes, and the store that follows it.
    name_of = {}
    for index, ins in enumerate(insns):
        if ins.mnemonic != "add" or len(ins.operands) != 3:
            continue
        if ins.operands[0].reg != ins.operands[1].reg:
            continue
        reg = ins.operands[0].reg
        page = None
        for back in range(index - 1, max(index - 6, -1), -1):
            p = insns[back]
            if p.mnemonic == "adrp" and len(p.operands) == 2 and p.operands[0].reg == reg:
                page = p.operands[1].imm
                break
        if page is None:
            continue
        try:
            name = read_cstring(data, seg, page + ins.operands[2].imm)
        except ValueError:
            continue
        if name.startswith("nvn"):
            name_of[index] = name

    names = {}
    for index, ins in enumerate(insns):
        if ins.mnemonic != "str" or len(ins.operands) != 2:
            continue
        mem = ins.operands[1]
        if mem.type != capstone.arm64.ARM64_OP_MEM:
            continue
        # The name is the last one materialised before this store.
        name = None
        for back in range(index - 1, max(index - 12, -1), -1):
            if back in name_of:
                name = name_of[back]
                break
        if name is None:
            continue
        base = mem.mem.base
        disp = mem.mem.disp
        # The table page is the page of the adrp that sets the base register
        # of the store. It can sit before or after the store.
        page = None
        for near in list(insns[max(0, index - 6):index]) + list(insns[index + 1:index + 4]):
            if (near.mnemonic == "adrp" and len(near.operands) == 2
                    and near.operands[0].reg == base):
                page = near.operands[1].imm
                break
        if page is None:
            continue
        names.setdefault(name, []).append((page, base, disp))
    return names


def adrp_sites(text, lo, page):
    import numpy as np
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
    parser.add_argument("--name", action="append", required=True)
    parser.add_argument("--window", type=int, default=8,
                        help="instructions of context to print around each call")
    parser.add_argument("--list-table", action="store_true",
                        help="print the whole name to slot table and exit")
    args = parser.parse_args()

    data, seg = load(NSO)
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    names = build_name_table(data, seg, md)
    if args.list_table:
        for name, entries in sorted(names.items()):
            for page, base, disp in entries:
                print(f"  0x{page + disp:x}  {name}")
        print(f"names recovered: {len(names)}")
        return 0

    lo, hi = seg[0][1], seg[0][1] + seg[0][2]
    text = data[file_offset(seg, lo, hi - lo): file_offset(seg, lo, hi - lo) + (hi - lo)]

    for name in args.name:
        entries = names.get(name)
        if not entries:
            print(f"{name}: no table slot found")
            continue
        print(f"\n=== {name} ===")
        found = 0
        for page, base, disp in entries:
          print(f"  slot=0x{page + disp:x} (page 0x{page:x} disp 0x{disp:x})")
          for site in adrp_sites(text, lo, page):
            insns = window(data, seg, md, site, 1, args.window + 14)
            for i, ins in enumerate(insns):
                if ins.mnemonic != "ldr" or len(ins.operands) != 3:
                    continue
                mem = ins.operands[1]
                if mem.type != capstone.arm64.ARM64_OP_MEM:
                    continue
                # The call site loads the slot through its own base register,
                # which is not the one the lookup code used, so only the
                # displacement identifies the slot here.
                if mem.mem.disp != disp:
                    continue
                loaded = ins.operands[0].reg
                call = None
                for j in range(i + 1, min(i + 14, len(insns))):
                    nxt = insns[j]
                    if (nxt.mnemonic == "blr" and len(nxt.operands) == 1
                            and nxt.operands[0].reg == loaded):
                        call = j
                        break
                if call is None:
                    continue
                found += 1
                print(f"\n  call site at module offset 0x{insns[call].address:x}")
                start = max(0, i - args.window)
                end = min(len(insns), call + 3)
                for ins2 in insns[start:end]:
                    mark = "   <<< CALL" if ins2.address == insns[call].address else ""
                    print(f"    0x{ins2.address:x}: {ins2.mnemonic} {ins2.op_str}{mark}")
        if not found:
            print("  no call site found")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

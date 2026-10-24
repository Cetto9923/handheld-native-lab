#!/usr/bin/env python3
"""List every import a module-offset range calls, in call order.

Offline static evidence: rebuilds the PLT-stub -> import mapping from the
NSO dynamic section, then scans the requested module-offset range for BL
instructions that target an import PLT stub. Reads only local bytes; it does
not execute guest code.

Usage: python3 scripts/runtime/scan_import_range.py <nso> <lo> <hi>
"""
import importlib.util
import pathlib
import struct
import sys

spec = importlib.util.spec_from_file_location("ric", pathlib.Path(__file__).with_name("resolve_import_callers.py"))
ric = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ric)


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    nso, lo, hi = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
    ctx = ric.parse(nso)
    stubs = ric.build_plt_map(ctx)
    seg = ctx["seg"][0]
    text = ctx["data"][ctx["file_offset"](seg[1], seg[2]): ctx["file_offset"](seg[1], seg[2]) + seg[2]]
    found = []
    for off in range(lo - seg[1], hi - seg[1], 4):
        if off < 0 or off + 4 > len(text):
            continue
        insn = struct.unpack_from("<I", text, off)[0]
        if (insn >> 26) != 0b100101:
            continue
        imm = insn & 0x03FFFFFF
        if imm & 0x02000000:
            imm -= 1 << 26
        tgt = seg[1] + off + (imm << 2)
        entry = stubs.get(tgt)
        if not entry:
            continue
        sym = entry[0]
        found.append((seg[1] + off, sym, ctx["sym_name"](sym)))
    print("# range 0x%x..0x%x : %d import calls" % (lo, hi, len(found)))
    seen = []
    for pc, sym, name in found:
        print("0x%x\timport %d\t%s" % (pc, sym, name))
        if sym not in [s for s, _ in seen]:
            seen.append((sym, name))
    print("# distinct imports: %d" % len(seen))
    for sym, name in seen:
        print("#   import %d %s" % (sym, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())

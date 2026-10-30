#!/usr/bin/env python3
"""Resolve AArch64 NSO import call sites without executing guest code.

Offline static evidence for the retail Nintendo NSOs already unpacked locally.
Given an NSO0 file it rebuilds the PLT-stub -> dynamic-symbol mapping from the
ELF-style dynamic section (DT_JMPREL / DT_RELA), then scans the text segment
for BL instructions that target a PLT stub, so a guest module offset can be
attributed to the exact import index that faulted or was bound by the HLE.

Usage:
  python3 scripts/runtime/resolve_import_callers.py <nso.bin> --name <mangled>
  python3 scripts/runtime/resolve_import_callers.py <nso.bin> --index 325
  python3 scripts/runtime/resolve_import_callers.py <nso.bin> --callers 0x1091274
  python3 scripts/runtime/resolve_import_callers.py <nso.bin> --list nn::socket

Reads only the supplied local bytes; it does not execute target code, fetch
packages, or modify the input.
"""

import argparse
import pathlib
import struct
import sys

R_AARCH64_ABS64 = 257
R_AARCH64_ABS32 = 258
R_AARCH64_GLOB_DAT = 1025
R_AARCH64_JUMP_SLOT = 1026
R_AARCH64_RELATIVE = 1027


def parse(path):
    data = pathlib.Path(path).read_bytes()
    if data[:4] != b"NSO0":
        raise ValueError("expected an uncompressed NSO0")
    seg = [struct.unpack_from("<IIII", data, 16 + i * 16) for i in range(3)]

    def file_offset(addr, length=1):
        for f, m, n, _ in seg:
            if m <= addr and length <= n - (addr - m) and f + addr - m + length <= len(data):
                return f + addr - m
        raise ValueError("virtual range 0x%x outside file-backed segments" % addr)

    mod = seg[0][1] + struct.unpack_from("<i", data, seg[0][0] + 4)[0]
    dynamic = mod + struct.unpack_from("<i", data, file_offset(mod) + 4)[0]
    tags = {}
    i = 0
    while True:
        tag, value = struct.unpack_from("<QQ", data, file_offset(dynamic + i * 16, 16))
        if tag == 0:
            break
        tags.setdefault(tag, []).append(value)
        i += 1

    def tag_value(t, default=0):
        return tags.get(t, [default])[0]

    strtab = tag_value(5)
    symtab = tag_value(6)

    def sym_name(index):
        off = struct.unpack_from("<I", data, file_offset(symtab + index * 24, 24))[0]
        start = file_offset(strtab + off)
        end = data.index(b"\0", start)
        return data[start:end].decode("utf8", "replace")

    def sym_info(index):
        return struct.unpack_from("<IBBHQQ", data, file_offset(symtab + index * 24, 24))

    # GOT slot -> dynamic symbol index, for both PLT and data relocations.
    got_to_sym = {}
    for dt in (23, 7):  # DT_JMPREL, DT_RELA
        base = tag_value(dt)
        size = tag_value(2 if dt == 23 else 8)
        for k in range(size // 24):
            tgt, info, _add = struct.unpack_from("<QQq", data, file_offset(base + k * 24, 24))
            sym = info >> 32
            rtype = info & 0xFFFFFFFF
            if sym and rtype in (R_AARCH64_JUMP_SLOT, R_AARCH64_GLOB_DAT):
                got_to_sym.setdefault(tgt, sym)

    return {
        "data": data,
        "seg": seg,
        "file_offset": file_offset,
        "sym_name": sym_name,
        "sym_info": sym_info,
        "got_to_sym": got_to_sym,
        "tags": tags,
    }


def rd32(ctx, addr):
    return struct.unpack_from("<I", ctx["data"], ctx["file_offset"](addr, 4))[0]


def adrp_page(ctx, pc):
    insn = rd32(ctx, pc)
    immlo = (insn >> 29) & 3
    immhi = (insn >> 5) & 0x7FFFF
    imm = (immhi << 2) | immlo
    if imm & (1 << 20):
        imm -= 1 << 21
    return (pc & ~0xFFF) + (imm << 12)


def ldr_offset(ctx, pc):
    """Return the byte offset of `ldr xN, [xM, #imm]` (64-bit, scaled by 8)."""
    insn = rd32(ctx, pc)
    if (insn >> 24) & 0x3F != 0b111001:
        return None
    opc = (insn >> 22) & 3
    if opc != 1:
        return None
    return ((insn >> 10) & 0xFFF) * 8


def build_plt_map(ctx):
    """stub module offset -> (import index, GOT slot)."""
    got_to_sym = ctx["got_to_sym"]
    stubs = {}
    text_lo, text_hi = ctx["seg"][0][1], ctx["seg"][0][1] + ctx["seg"][0][2]
    text_start = ctx["file_offset"](text_lo, text_hi - text_lo)
    text = ctx["data"][text_start: text_start + (text_hi - text_lo)]
    # PLT stubs are 16 bytes: adrp x16,page / ldr x17,[x16,#off] / add x16,x16,#off / br x17.
    # Anchor on the br x17 opcode so the scan runs at C speed instead of decoding
    # every instruction in the (very large) text segment.
    needle = b"\x20\x02\x1f\xd6"
    pos = text.find(needle)
    while pos != -1:
        base = pos - 12
        if base >= 0 and (base & 3) == 0:
            page = adrp_page(ctx, text_lo + base)
            off = ldr_offset(ctx, text_lo + base + 4)
            if off is not None:
                got = page + off
                sym = got_to_sym.get(got)
                if sym is not None and (rd32(ctx, text_lo + base + 8) & 0xFFC00000) == 0x91000000:
                    stubs[text_lo + base] = (sym, got)
        pos = text.find(needle, pos + 1)
    return stubs


def scan_callers(ctx, stub_addrs):
    """module offset of each BL -> target stub address."""
    callers = {}
    seg = ctx["seg"][0]
    text = ctx["data"][ctx["file_offset"](seg[1], seg[2]): ctx["file_offset"](seg[1], seg[2]) + seg[2]]
    for i in range(0, len(text) - 3, 4):
        insn = struct.unpack_from("<I", text, i)[0]
        if (insn >> 26) != 0b100101:
            continue
        off = insn & 0x03FFFFFF
        if off & 0x02000000:
            off -= 1 << 26
        tgt = seg[1] + i + (off << 2)
        if tgt in stub_addrs:
            callers.setdefault(tgt, []).append(seg[1] + i)
    return callers


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("nso")
    ap.add_argument("--name", help="exact mangled import name")
    ap.add_argument("--index", type=int, help="import index")
    ap.add_argument("--contains", help="substring match on the import name")
    ap.add_argument("--callers", help="module offset of a BL instruction; prints the import it calls")
    ap.add_argument("--list", action="store_true", help="list every import with its PLT stub")
    ap.add_argument("--max-callers", type=int, default=64)
    args = ap.parse_args()

    ctx = parse(args.nso)
    stubs = build_plt_map(ctx)
    sym_to_stub = {}
    for stub, (sym, _got) in stubs.items():
        sym_to_stub.setdefault(sym, []).append(stub)

    if args.callers:
        pc = int(args.callers, 0)
        insn = rd32(ctx, pc)
        if (insn >> 26) != 0b100101:
            print("module_offset 0x%x is not a BL (insn 0x%08x)" % (pc, insn))
            return 1
        off = insn & 0x03FFFFFF
        if off & 0x02000000:
            off -= 1 << 26
        tgt = pc + (off << 2)
        entry = stubs.get(tgt)
        if not entry:
            print("module_offset 0x%x -> target 0x%x is not a resolved PLT stub" % (pc, tgt))
            return 1
        sym = entry[0]
        print("module_offset 0x%x -> import index %d %s" % (pc, sym, ctx["sym_name"](sym)))
        return 0

    if args.list:
        for sym in sorted(sym_to_stub):
            print("%d\t%s\t%s" % (sym, ",".join("0x%x" % s for s in sym_to_stub[sym]), ctx["sym_name"](sym)))
        return 0

    wanted = []
    if args.index is not None:
        wanted = [args.index]
    elif args.name:
        wanted = [i for i in sym_to_stub if ctx["sym_name"](i) == args.name]
        if not wanted:
            print("no import named %s" % args.name)
            return 1
    elif args.contains:
        wanted = [i for i in sorted(sym_to_stub) if args.contains in ctx["sym_name"](i)]
    else:
        ap.error("one of --index/--name/--contains/--callers/--list is required")

    all_stubs = set()
    for sym in wanted:
        all_stubs.update(sym_to_stub.get(sym, []))
    callers = scan_callers(ctx, all_stubs) if all_stubs else {}

    for sym in wanted:
        name = ctx["sym_name"](sym)
        print("import %d %s" % (sym, name))
        for stub in sorted(sym_to_stub.get(sym, [])):
            sites = callers.get(stub, [])
            print("  plt 0x%x callers=%d %s" % (stub, len(sites), " ".join("0x%x" % s for s in sites[: args.max_callers])))
    return 0


if __name__ == "__main__":
    sys.exit(main())

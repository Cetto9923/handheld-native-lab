#!/usr/bin/env python3
"""Recover the guest call sites of the NVN entry points from a device transcript.

Read-only. The runtime trap reports the absolute return address of the call
that reached an unimplemented entry point, and the profile line reports the
module base. Subtracting one from the other gives the module offset of the
call site, which is a position inside the guest image. That position is
disassembled here, so the arguments the guest really passed and the value it
really reads back can be read instead of guessed.

This is the offline counterpart of the run: the transcript says which entry
point the guest reached, and the disassembly says what it wanted from it.

Usage:
  python3 scripts/runtime/nvn_trap_callsite.py \\
      --transcript .local/logs/p-mm-18-guest-transcript.log
  python3 scripts/runtime/nvn_trap_callsite.py \\
      --transcript .local/logs/p-mm-18-guest-transcript.log --window 12
"""

import argparse
import re
import struct
from pathlib import Path

import capstone

NSO = Path(".local/uncompressed/main.bin")

PROFILE = re.compile(r"module_base=0x([0-9a-f]+)")
TRAP = re.compile(
    r'unimplemented NVN entry "([^"]+)" was called \(trap slot (\d+), caller lr=0x([0-9a-f]+)\)'
)


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


def parse_transcript(path):
    """module base and the trapped calls, in the order the guest made them."""
    base = None
    traps = []
    for line in path.read_text(errors="replace").splitlines():
        m = PROFILE.search(line)
        if m:
            base = int(m.group(1), 16)
            continue
        m = TRAP.search(line)
        if m:
            traps.append((m.group(1), int(m.group(2)), int(m.group(3), 16)))
    return base, traps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--transcript", type=Path, required=True)
    parser.add_argument("--window", type=int, default=10,
                        help="instructions of context to print around the call site")
    args = parser.parse_args()

    base, traps = parse_transcript(args.transcript)
    if base is None:
        print("the transcript has no module_base line; the runtime has to report it")
        return 1
    if not traps:
        print("the transcript has no trapped call")
        return 1

    data, seg = load(NSO)
    lo, hi, text = text_range(data, seg)
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    for name, slot, lr in traps:
        offset = lr - base
        print(f"\n=== {name} (trap slot {slot}) ===")
        print(f"  caller lr=0x{lr:x}  module_base=0x{base:x}  module offset=0x{offset:x}")
        if offset < lo or offset >= hi:
            print("  the module offset is outside the text segment")
            continue
        start = max(lo, offset - args.window * 4)
        end = min(hi, offset + 16)
        raw = data[file_offset(seg, start, end - start): file_offset(seg, start, end - start) + (end - start)]
        for ins in md.disasm(raw, start):
            if ins.address < offset - args.window * 4 or ins.address > offset + 12:
                continue
            mark = "   <<< RETURN ADDRESS" if ins.address == offset else ""
            print(f"    0x{ins.address:x}: {ins.mnemonic} {ins.op_str}{mark}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

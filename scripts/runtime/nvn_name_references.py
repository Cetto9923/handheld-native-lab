#!/usr/bin/env python3
"""Static inventory of the NVN names the guest actually references from code.

Read-only. Scans the main NSO text segment for the ADRP/ADD pairs that
materialise an NVN entry point name, and reports which of the names present in
the binary are referenced by code, with the code offsets.
"""
import re, struct, sys
from pathlib import Path

NSO = Path('.local/nso/main.nso')
data = NSO.read_bytes()

seg = [struct.unpack_from("<IIII", data, 16 + i * 16) for i in range(3)]
TEXT_FILE, TEXT_VADDR, TEXT_SIZE, _ = seg[0]


def file_offset(addr, length=1):
    for f, m, n, _ in seg:
        if m <= addr and length <= n - (addr - m) and f + addr - m + length <= len(data):
            return f + addr - m
    raise ValueError(f"0x{addr:x}+{length:x} outside file-backed segments")


# --- every "nvn" name string present in the image -------------------------
names = {}
for m in re.finditer(rb'nvn[A-Za-z0-9_]{2,60}', data):
    end = data.find(b'\0', m.start(), m.start() + 80)
    if end < 0:
        continue
    name = data[m.start():end].decode()
    for f, base, n, _ in seg:
        if f <= m.start() < f + n:
            names.setdefault(base + m.start() - f, name)
            break
print(f"names in image: {len(names)}")

if not names:
    sys.exit(0)
lo, hi = min(names), max(names)
print(f"name address range: 0x{lo:x} .. 0x{hi:x}")

# --- find ADRP instructions whose target page can hold a name -------------
text = data[file_offset(TEXT_VADDR, TEXT_SIZE): file_offset(TEXT_VADDR, TEXT_SIZE) + TEXT_SIZE]
words = memoryview(text).cast('I')
try:
    import numpy as np
    w = np.frombuffer(text, dtype='<u4')
except ImportError:
    w = None

hits = []
if w is not None:
    is_adrp = (w & 0x9F000000) == 0x90000000
    idx = np.nonzero(is_adrp)[0]
    for i in idx:
        insn = int(w[i])
        immlo = (insn >> 29) & 3
        immhi = (insn >> 5) & 0x7ffff
        imm = (immhi << 2) | immlo
        if imm & 0x100000:
            imm -= 0x200000
        page = ((TEXT_VADDR + i * 4) & ~0xfff) + (imm << 12)
        if page < (lo & ~0xfff) or page > (hi & ~0xfff):
            continue
        rd = insn & 31
        # the matching ADD (immediate) usually follows within a few words
        for j in range(i + 1, min(i + 9, len(w))):
            nxt = int(w[j])
            if (nxt & 0xFF000000) == 0x91000000 and ((nxt >> 5) & 31) == rd and (nxt & 31) == rd:
                add_imm = (nxt >> 10) & 0xfff
                shift = (nxt >> 22) & 3
                if shift == 1:
                    add_imm <<= 12
                target = page + add_imm
                if target in names:
                    hits.append((TEXT_VADDR + i * 4, TEXT_VADDR + j * 4, names[target]))
                break
else:
    for i in range(len(words)):
        insn = int(words[i])
        if (insn & 0x9F000000) != 0x90000000:
            continue
        immlo = (insn >> 29) & 3
        immhi = (insn >> 5) & 0x7ffff
        imm = (immhi << 2) | immlo
        if imm & 0x100000:
            imm -= 0x200000
        page = ((TEXT_VADDR + i * 4) & ~0xfff) + (imm << 12)
        if page < (lo & ~0xfff) or page > (hi & ~0xfff):
            continue
        rd = insn & 31
        for j in range(i + 1, min(i + 9, len(words))):
            nxt = int(words[j])
            if (nxt & 0xFF000000) == 0x91000000 and ((nxt >> 5) & 31) == rd and (nxt & 31) == rd:
                add_imm = (nxt >> 10) & 0xfff
                if ((nxt >> 22) & 3) == 1:
                    add_imm <<= 12
                target = page + add_imm
                if target in names:
                    hits.append((TEXT_VADDR + i * 4, TEXT_VADDR + j * 4, names[target]))
                break

print(f"code references to NVN names: {len(hits)}")
used = {}
for a, b, name in hits:
    used.setdefault(name, []).append(a)
print(f"distinct names referenced from code: {len(used)}")
never = sorted(set(names.values()) - set(used))
print(f"names present in the image but never referenced from code: {len(never)}")
for n in never[:40]:
    print("  unreferenced:", n)

out = Path('evidence/nvn/guest_nvn_code_references.tsv')
out.parent.mkdir(parents=True, exist_ok=True)

# --- classify each reference: is the name handed to an indirect call? -----
# The guest looks an entry point up by calling a function pointer, so the
# string reference is a lookup when a BLR follows within a few instructions.
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
lookup = {}
for a, b, name in hits:
    window = data[file_offset(b, 4): file_offset(b, 4) + 40]
    called = False
    for ins in md.disasm(window, b):
        if ins.mnemonic == 'blr':
            called = True
            break
        if ins.mnemonic in ('ret', 'b') and ins.mnemonic == 'ret':
            break
    lookup.setdefault(name, {'first': a, 'count': 0, 'lookups': 0})
    lookup[name]['count'] += 1
    if called:
        lookup[name]['lookups'] += 1

with out.open('w') as f:
    f.write("# name\tfirst_code_offset\treference_count\tlookup_call_count\n")
    for name in sorted(lookup):
        e = lookup[name]
        f.write(f"{name}\t0x{e['first']:x}\t{e['count']}\t{e['lookups']}\n")
print("written:", out)
resolved_by_name = sum(1 for e in lookup.values() if e['lookups'] > 0)
print(f"names handed to an indirect call: {resolved_by_name}")
never = sorted(set(names.values()) - set(lookup))
print(f"names present in the image but never referenced from code: {len(never)}")
for n in never[:40]:
    print("  unreferenced:", n)

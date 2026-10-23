#!/usr/bin/env python3
"""nso_probe -- static structure probe for Nintendo Switch NSO0 / NPDM artifacts.

Scope: pure static byte observation.  Nothing from the target is executed.

What it reports
----------------
* NSO0 container header: flags, module name, the three segments
  (.text/.rodata/.data + .bss), build id, and the LZ4-compressed on-disk sizes.
* The dynamic tables, whose offsets are read from NSO header fields 0x90/0x94
  (dynstr) and 0x98/0x9C (dynsym), both relative to the start of .rodata.
* The ELF64-style relocation table (Rela) that follows the .rodata BuildInfo
  block, with a per-type histogram.
* The import surface (undefined dynamic symbols) and the export surface
  (defined dynamic symbols), with a namespace grouping.
* NPDM: ACI/ACID records, the kernel-service name pool, the syscall table
  size and the kernel-capability list.

Usage
-----
    nso_probe.py nso   <file.nso> [--json out.json] [--imports]
    nso_probe.py image <file.bin> [--json out.json] [--imports]
    nso_probe.py npdm  <file.npdm> [--json out.json]

`nso` accepts a compressed NSO0 container and decompresses its segments with a
built-in LZ4 block decoder.  `image` accepts an already-uncompressed NSO image
(the layout produced by `hactool --uncompressed`), which starts with the same
0x100-byte BuildInfo header.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from lz4_block import Lz4Error, decompress_block  # noqa: E402

# AArch64 relocation types that appear in Nintendo Switch NSOs.
R_AARCH64 = {
    0x100: "R_AARCH64_NONE",
    0x101: "R_AARCH64_ABS64",
    0x102: "R_AARCH64_ABS32",
    0x103: "R_AARCH64_ABS16",
    0x104: "R_AARCH64_PREL64",
    0x105: "R_AARCH64_PREL32",
    0x106: "R_AARCH64_PREL16",
    0x107: "R_AARCH64_MOVW_UABS_G0",
    0x108: "R_AARCH64_MOVW_UABS_G0_NC",
    0x109: "R_AARCH64_MOVW_UABS_G1",
    0x10A: "R_AARCH64_MOVW_UABS_G1_NC",
    0x10B: "R_AARCH64_MOVW_UABS_G2",
    0x10C: "R_AARCH64_MOVW_UABS_G2_NC",
    0x10D: "R_AARCH64_MOVW_UABS_G3",
    0x10E: "R_AARCH64_MOVW_UABS_G3_NC",
    0x10F: "R_AARCH64_MOVW_PREL_G0",
    0x110: "R_AARCH64_MOVW_PREL_G0_NC",
    0x200: "R_AARCH64_COPY",
    0x201: "R_AARCH64_GLOB_DAT",
    0x202: "R_AARCH64_JUMP_SLOT",
    0x203: "R_AARCH64_RELATIVE",
    0x204: "R_AARCH64_TLS_DTPMOD64",
    0x205: "R_AARCH64_TLS_DTPREL64",
    0x206: "R_AARCH64_TLS_TPREL64",
    0x207: "R_AARCH64_TLSDESC",
    0x208: "R_AARCH64_IRELATIVE",
    0x400: "R_AARCH64_COPY",
    0x401: "R_AARCH64_GLOB_DAT",
    0x402: "R_AARCH64_JUMP_SLOT",
    0x403: "R_AARCH64_RELATIVE",
    0x404: "R_AARCH64_TLS_DTPMOD64",
    0x405: "R_AARCH64_TLS_DTPREL64",
    0x406: "R_AARCH64_TLS_TPREL64",
    0x407: "R_AARCH64_TLSDESC",
    0x408: "R_AARCH64_IRELATIVE",
}
R_AARCH64_SET = set(R_AARCH64)

ELF64_SYM_SIZE = 24
ELF64_RELA_SIZE = 24

NSO_HEADER_SIZE = 0x100


class NsoHeader:
    """NSO0 container header (256 bytes, immediately after the file magic)."""

    def __init__(self, raw: bytes):
        (self.magic, self.version, self.reserved, self.flags) = struct.unpack_from(
            "<4I", raw, 0x00
        )
        self.text_file_offset, self.text_mem_offset, self.text_size = struct.unpack_from(
            "<3I", raw, 0x10
        )
        self.module_offset = struct.unpack_from("<I", raw, 0x1C)[0]
        (self.rodata_file_offset, self.rodata_mem_offset, self.rodata_size) = struct.unpack_from(
            "<3I", raw, 0x20
        )
        self.module_size = struct.unpack_from("<I", raw, 0x2C)[0]
        (self.data_file_offset, self.data_mem_offset, self.data_size) = struct.unpack_from(
            "<3I", raw, 0x30
        )
        self.bss_size = struct.unpack_from("<I", raw, 0x3C)[0]
        self.build_id = raw[0x40:0x60].hex()
        (
            self.compressed_text_size,
            self.compressed_rodata_size,
            self.compressed_data_size,
        ) = struct.unpack_from("<3I", raw, 0x60)
        # Dynamic table locations, relative to the start of .rodata.
        self.dynstr_offset = struct.unpack_from("<I", raw, 0x90)[0]
        self.dynstr_size = struct.unpack_from("<I", raw, 0x94)[0]
        self.dynsym_offset = struct.unpack_from("<I", raw, 0x98)[0]
        self.dynsym_size = struct.unpack_from("<I", raw, 0x9C)[0]

    def to_dict(self):
        return {
            "magic": self.magic.to_bytes(4, "little").decode("ascii", "replace"),
            "version": self.version,
            "flags": self.flags,
            "segments_compressed": {
                "text": bool(self.flags & 1),
                "rodata": bool(self.flags & 2),
                "data": bool(self.flags & 4),
            },
            "module_offset": self.module_offset,
            "module_size": self.module_size,
            "text": {
                "file_offset": self.text_file_offset,
                "mem_offset": self.text_mem_offset,
                "size": self.text_size,
                "compressed_size": self.compressed_text_size,
            },
            "rodata": {
                "file_offset": self.rodata_file_offset,
                "mem_offset": self.rodata_mem_offset,
                "size": self.rodata_size,
                "compressed_size": self.compressed_rodata_size,
            },
            "data": {
                "file_offset": self.data_file_offset,
                "mem_offset": self.data_mem_offset,
                "size": self.data_size,
                "compressed_size": self.compressed_data_size,
            },
            "bss_size": self.bss_size,
            "build_id": self.build_id,
            "dynstr": {"offset_rodata": self.dynstr_offset, "size": self.dynstr_size},
            "dynsym": {"offset_rodata": self.dynsym_offset, "size": self.dynsym_size},
        }


class NsoImage:
    """A materialised NSO memory image plus its file<->memory mapping."""

    def __init__(self, image: bytes, header: NsoHeader, base_delta: int,
                 module_name: str, source: str):
        self.image = image
        self.header = header
        self.base_delta = base_delta  # file_offset = mem_offset + base_delta
        self.module_name = module_name
        self.source = source

    @property
    def memory_end(self):
        h = self.header
        return max(
            h.text_mem_offset + h.text_size,
            h.rodata_mem_offset + h.rodata_size,
            h.data_mem_offset + h.data_size + h.bss_size,
        )

    def mem_to_file(self, mem: int):
        return mem + self.base_delta


def load_nso(path: str) -> NsoImage:
    """Load a compressed NSO0 container and materialise its memory image."""
    raw = open(path, "rb").read()
    header = NsoHeader(raw[:NSO_HEADER_SIZE])
    if raw[:4] != b"NSO0":
        raise SystemExit(f"{path}: not an NSO0 container")
    module_name = raw[
        header.module_offset:header.module_offset + header.module_size
    ].split(b"\x00")[0].decode("ascii", "replace")

    def section(file_offset, mem_offset, size, comp_size, compressed):
        if compressed:
            try:
                return decompress_block(
                    raw[file_offset:file_offset + comp_size], size
                )
            except Lz4Error as exc:
                raise SystemExit(f"{path}: LZ4 failure: {exc}")
        return raw[file_offset:file_offset + size]

    text = section(
        header.text_file_offset, header.text_mem_offset, header.text_size,
        header.compressed_text_size, header.flags & 1,
    )
    rodata = section(
        header.rodata_file_offset, header.rodata_mem_offset, header.rodata_size,
        header.compressed_rodata_size, header.flags & 2,
    )
    data = section(
        header.data_file_offset, header.data_mem_offset, header.data_size,
        header.compressed_data_size, header.flags & 4,
    )

    image = bytearray(header.data_mem_offset + header.data_size + header.bss_size)
    image[header.text_mem_offset:header.text_mem_offset + len(text)] = text
    image[header.rodata_mem_offset:header.rodata_mem_offset + len(rodata)] = rodata
    image[header.data_mem_offset:header.data_mem_offset + len(data)] = data
    # In the decompressed container the sections sit at their memory offsets with
    # no extra prefix, so file offset == memory offset.
    return NsoImage(bytes(image), header, 0, module_name, path)


def load_uncompressed(path: str) -> NsoImage:
    """Load an already-uncompressed NSO image (hactool --uncompressed layout)."""
    raw = open(path, "rb").read()
    header = NsoHeader(raw[:NSO_HEADER_SIZE])
    # The unpacked image stores section data at file offset = mem offset + 0x100.
    delta = 0x100
    image = bytearray(header.data_mem_offset + header.data_size + header.bss_size)
    for mem_off, size in (
        (header.text_mem_offset, header.text_size),
        (header.rodata_mem_offset, header.rodata_size),
        (header.data_mem_offset, header.data_size),
    ):
        start = mem_off + delta
        image[mem_off:mem_off + size] = raw[start:start + size]
    module_name = ""
    return NsoImage(bytes(image), header, delta, module_name, path)


def read_dynstr(image: NsoImage):
    """Return (base, {byte_offset: string}).

    Elf64 st_name is a *byte* offset into the string table, not an index, so
    the table is exposed as an offset->string map.
    """
    h = image.header
    base = h.rodata_mem_offset + h.dynstr_offset
    blob = image.image[base:base + h.dynstr_size]
    strings = {}
    start = 0
    for i, byte in enumerate(blob):
        if byte == 0:
            strings[start] = blob[start:i].decode("ascii", "replace")
            start = i + 1
    return base, strings


def read_dynsym(image: NsoImage):
    h = image.header
    base = h.rodata_mem_offset + h.dynsym_offset
    count = h.dynsym_size // ELF64_SYM_SIZE
    out = []
    for i in range(count):
        st_name, st_info, st_other, st_shndx = struct.unpack_from(
            "<IBBH", image.image, base + i * ELF64_SYM_SIZE
        )
        st_value, st_size = struct.unpack_from(
            "<QQ", image.image, base + i * ELF64_SYM_SIZE + 8
        )
        out.append(
            {
                "index": i,
                "st_name": st_name,
                "st_info": st_info,
                "bind": st_info & 0xF,
                "type": st_info >> 4,
                "st_other": st_other,
                "st_shndx": st_shndx,
                "st_value": st_value,
                "st_size": st_size,
            }
        )
    return base, out


def _valid_rela(image: NsoImage, off: int):
    """Return (r_offset, r_info, r_addend) if a valid AArch64 Rela lives at off."""
    if off + ELF64_RELA_SIZE > len(image.image):
        return None
    r_offset, r_info, r_addend = struct.unpack_from("<QQq", image.image, off)
    rtype = r_info & 0xFFFFFFFF
    sym = r_info >> 32
    if rtype not in R_AARCH64_SET or sym > (1 << 24):
        return None
    return r_offset, r_info, r_addend


def find_relocations(image: NsoImage):
    """Locate the contiguous run of Elf64_Rela records inside .rodata.

    The table starts right after the BuildInfo block that heads .rodata, at an
    offset that is not necessarily 24-byte aligned, so the search steps by 4
    bytes and grows the run forward from every plausible start.
    """
    h = image.header
    lo = h.rodata_mem_offset
    hi = h.rodata_mem_offset + h.rodata_size
    best = (lo, [], lo)
    off = lo
    while off < hi - ELF64_RELA_SIZE:
        if _valid_rela(image, off) is None:
            off += 4
            continue
        entries, end = scan_relocations(image, off)
        if len(entries) > len(best[1]):
            best = (off, entries, end)
        off = end
    return best


def scan_relocations(image: NsoImage, start: int, limit: int = 1 << 20):
    """Find the contiguous run of Elf64_Rela records starting at `start`."""
    buf = image.image
    entries = []
    off = start
    while off + ELF64_RELA_SIZE <= len(buf) and len(entries) < limit:
        rec = _valid_rela(image, off)
        if rec is None:
            break
        r_offset, r_info, r_addend = rec
        rtype = r_info & 0xFFFFFFFF
        sym = r_info >> 32
        entries.append(
            {
                "offset": r_offset,
                "type": rtype,
                "type_name": R_AARCH64.get(rtype, f"0x{rtype:x}"),
                "sym": sym,
                "addend": r_addend,
            }
        )
        off += ELF64_RELA_SIZE
    return entries, off


CRT_PREFIXES = ("__cxa", "__gxx", "_Unwind", "__register", "__deregister", "__aeabi",
                "_ITM_", "__stack_chk", "__gnu", "__clang", "___", "__assert",
                "__dynamic_cast", "__cxa_")
NN_CAPI_PREFIXES = ("nnos", "nns", "nnResult", "nnHttp", "nnRo", "nnFs", "nnDetail",
                     "nndetail", "_nnmusl", "__nnmusl")
NVN_PREFIXES = ("nvn", "nv::", "_ZN2nv")


def mangle_parts(name: str):
    """Extract the leading namespace components of an Itanium C++ mangled name.

    Handles the qualifier prefixes used by the various mangling forms
    (_ZNK..., _ZTVN..., _ZTHN..., _ZTIN..., _ZTv..._N...), which all encode
    the nested-name as <len><component>... after a leading 'N'.
    """
    if not name.startswith("_Z"):
        return None
    n = name.find("N", 2)
    if n < 0:
        return None
    i = n + 1
    parts = []
    while i < len(name):
        j = i
        while j < len(name) and name[j].isdigit():
            j += 1
        if j == i or j >= len(name):
            return None
        length = int(name[i:j])
        parts.append(name[j:j + length])
        i = j + length
        if i < len(name) and name[i].isdigit():
            continue
        break
    return parts or None


def classify(name: str) -> str:
    """Group an import symbol by the platform subsystem that provides it."""
    parts = mangle_parts(name)
    if parts:
        head = parts[0]
        if head == "nn":
            return "nn::" + (parts[1] if len(parts) > 1 else "?")
        if head == "nv":
            return "NVN (nv:: bootstrap)"
        if head == "movie":
            return "movie (subsdk0/multimedia)"
        if head == "std":
            return "libc++ (std::__1)"
        if head == "__cxxabiv1":
            return "CRT (cxxabi vtables/typeinfo)"
        return "C++ " + head
    if name.startswith(("_ZNSt", "_ZSt", "_ZTVSt", "_ZTISt")):
        return "libc++ (std::__1)"
    if name.startswith(NVN_PREFIXES):
        return "NVN (nvnBootstrapLoader)"
    if name.startswith(CRT_PREFIXES):
        return "CRT"
    if name.startswith(NN_CAPI_PREFIXES):
        return "nn C-API (nnos*/nns*/nnResult*)"
    return "libc (musl/nnmusl)"


def analyse_nso(path: str, dump_imports: bool):
    if path.endswith(".bin"):
        image = load_uncompressed(path)
    else:
        image = load_nso(path)
    h = image.header

    dynstr_base, dynstr = read_dynstr(image)
    dynsym_base, dynsym = read_dynsym(image)

    imports = []
    exports = []
    for sym in dynsym:
        name = dynstr.get(sym["st_name"], "")
        if sym["index"] == 0 and not name:
            continue  # the mandatory null symbol is not an import
        if sym["st_shndx"] == 0:
            imports.append({**sym, "name": name})
        else:
            exports.append({**sym, "name": name})

    # The relocation table follows the BuildInfo block at the start of .rodata.
    rela_start, rela, rela_end = find_relocations(image)
    type_hist = {}
    for r in rela:
        type_hist[r["type_name"]] = type_hist.get(r["type_name"], 0) + 1
    reloc_syms = sorted({r["sym"] for r in rela})

    groups = {}
    for imp in imports:
        groups.setdefault(classify(imp["name"]), []).append(imp["name"])

    result = {
        "source": path,
        "module_name": image.module_name,
        "container": "uncompressed-nso-image" if path.endswith(".bin") else "nso0",
        "header": h.to_dict(),
        "memory_image_size": image.memory_end,
        "file_to_memory_delta": image.base_delta,
        "dynstr": {
            "mem_offset": dynstr_base,
            "file_offset": image.mem_to_file(dynstr_base),
            "size": h.dynstr_size,
            "string_count": len(dynstr),
        },
        "dynsym": {
            "mem_offset": dynsym_base,
            "file_offset": image.mem_to_file(dynsym_base),
            "size": h.dynsym_size,
            "entry_count": len(dynsym),
            "undefined_count": len(imports),
            "defined_count": len(exports),
        },
        "relocations": {
            "mem_offset": rela_start,
            "file_offset": image.mem_to_file(rela_start),
            "end_mem_offset": rela_end,
            "entry_count": len(rela),
            "type_histogram": type_hist,
            "distinct_symbol_indices": len(reloc_syms),
            "max_symbol_index": max(reloc_syms) if reloc_syms else 0,
        },
        "imports": {
            "count": len(imports),
            "group_histogram": {k: len(v) for k, v in sorted(groups.items())},
            "groups": {k: sorted(v) for k, v in sorted(groups.items())},
        },
        "exports": {
            "count": len(exports),
            "symbols": [
                {
                    "name": e["name"],
                    "mem_offset": e["st_value"],
                    "file_offset": image.mem_to_file(e["st_value"]),
                    "size": e["st_size"],
                    "dynsym_index": e["index"],
                }
                for e in exports
            ],
        },
    }
    if dump_imports:
        result["imports"]["flat_list"] = sorted(i["name"] for i in imports)
    return result


def parse_npdm(path: str):
    raw = open(path, "rb").read()
    out = {"source": path, "size": len(raw)}

    # The NPDM section is wrapped in a 0x10-byte "META" header.
    meta_magic = raw[0:4].decode("ascii", "replace")
    out["meta_magic"] = meta_magic

    acid_off = raw.find(b"ACID")
    aci0_off = raw.find(b"ACI0")
    out["acid_offset"] = acid_off if acid_off >= 0 else None
    out["aci0_offset"] = aci0_off if aci0_off >= 0 else None

    # Kernel-service name pool: Pascal-style strings whose length byte counts
    # up to (but not including) the terminating NUL.
    names = []
    if acid_off >= 0:
        pos = acid_off + 0x70
        while pos < len(raw):
            length = raw[pos]
            if length == 0 or length > 0x40:
                pos += 1
                continue
            name = raw[pos + 1:pos + 2 + length].split(b"\x00")[0]
            if not name or not all(32 <= c < 127 for c in name):
                pos += 1
                continue
            names.append(name.decode("ascii"))
            pos += 2 + length
    out["kernel_service_names"] = names
    out["kernel_service_count"] = len(names)

    # Syscall table: the u32 array that sits between the header block and the
    # ACID section.  Its size is the distance to the ACID magic minus 16 bytes
    # of trailing padding.
    if acid_off >= 0:
        svc_table_end = acid_off
        syscalls = []
        for off in range(0x80, svc_table_end - 3, 4):
            syscalls.append(struct.unpack_from("<I", raw, off)[0])
        out["syscall_table_offset"] = 0x80
        out["syscall_count"] = len(syscalls)
        out["syscall_ids"] = syscalls
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_nso = sub.add_parser("nso", help="analyse an NSO0 container")
    p_nso.add_argument("path")
    p_nso.add_argument("--json")
    p_nso.add_argument("--imports", action="store_true")

    p_img = sub.add_parser("image", help="analyse an uncompressed NSO image")
    p_img.add_argument("path")
    p_img.add_argument("--json")
    p_img.add_argument("--imports", action="store_true")

    p_npdm = sub.add_parser("npdm", help="analyse an NPDM program descriptor")
    p_npdm.add_argument("path")
    p_npdm.add_argument("--json")

    args = ap.parse_args()
    if args.cmd == "npdm":
        result = parse_npdm(args.path)
    else:
        result = analyse_nso(args.path, args.imports)

    text = json.dumps(result, indent=2, ensure_ascii=False)
    if getattr(args, "json", None):
        with open(args.json, "w", encoding="utf-8") as fh:
            fh.write(text)
        print(f"wrote {args.json}")
    else:
        print(text)


if __name__ == "__main__":
    main()

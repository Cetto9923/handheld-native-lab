#!/usr/bin/env python3
"""Verify original-run D/E raw readbacks; never substitute this for replay agreement.

Usage: python3 scripts/graphics/audit_honor_full_capture.py CAPTURE_DIR --out REPORT.json
The trace and all binary files must come from one device capture directory.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def audit(directory):
    errors = []
    trace = directory / "capture.txt"
    rows = []
    for line in trace.read_text().splitlines():
        rows.append((line.split(" ", 1)[0], dict(re.findall(r"(\w+)=([^\s]+)", line))))

    def check(ok, message):
        if not ok:
            errors.append(message)

    def entries(kind):
        return [r for k, r in rows if k == kind]

    def number(row, key, default=-1):
        return int(row.get(key, default), 0) if isinstance(row.get(key, default), str) else default

    begin, end, final = entries("FULL_BEGIN"), entries("FULL_END"), entries("END")
    check(len(begin) == len(end) == len(final) == 1, "Missing/duplicate FULL_BEGIN/FULL_END/END")
    frame = number(begin[0], "frame") if begin else -1
    if final:
        check(number(final[0], "frame") == frame, "Final frame mismatch")
    if end:
        check(number(end[0], "nativeReadbackComplete") == 1, "Runtime reports incomplete native readbacks")
        check(number(end[0], "pairMask") == 3 and number(end[0], "issued") == 2, "Both target draws not issued")
        check(number(end[0], "missing") == number(end[0], "ioErrors") == 0, "Runtime missing/IO errors")
    check(not entries("FULL_MISSING") and not entries("SKIP") and not entries("IO_ERROR"), "Explicit missing/skip/IO records")
    check(not entries("PROBE_DIAG"), "Rendering diagnostic override present")
    draws = entries("FULL_DRAW")
    check(len(draws) == 2 and {r.get("pair") for r in draws} == {"1", "2"}, "Target pair absent/duplicated")
    check(all(number(r, "frame") == frame for r in draws), "Cross-frame target draws")
    check(len({r.get("globalDraw") for r in draws}) == 2, "Ambiguous global draw identity")

    files, hashes = {}, []
    for r in entries("FILE"):
        name = r["name"]
        check(Path(name).name == name and name not in files, "Unsafe/duplicate file: " + name)
        path = directory / name
        if not path.is_file():
            check(False, "Missing file: " + name)
            continue
        size = path.stat().st_size
        check(size == number(r, "bytes") == number(r, "requested") and number(r, "closed") == 0, "Short/unclosed file: " + name)
        files[name] = size
        with path.open("rb") as stream:
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        hashes.append({"name": name, "bytes": size, "sha256": digest.hexdigest()})
    check(set(files) == {p.name for p in directory.glob("*.bin")}, "Unindexed binary files")
    if final:
        check(sum(files.values()) == number(final[0], "bytes"), "Final byte counter mismatch")

    def filename(r):
        return f"f{frame}-e{r['event']}-{r['label']}.bin"

    copies = entries("FULL_BUFFER_COPY") + entries("FULL_IMAGE_COPY")
    for r in copies:
        size = number(r, "range") if "range" in r else number(r, "bytes")
        check(files.get(filename(r)) == size, "Copy not fully written: " + filename(r))
    if end:
        check(len(copies) == number(end[0], "queued") == number(end[0], "written"), "Copy counters mismatch")
    for r in entries("FULL_BUFFER_COPY"):
        offset, size, extent = (number(r, k) for k in ("copyOffset", "copyBytes", "actualExtent"))
        check(offset % 4 == size % 4 == 0 and offset >= 0 and size > 0 and offset + size <= extent, "Invalid copy extent: " + r["label"])
        check(number(r, "offset") == offset + number(r, "validOffset") and number(r, "range") + number(r, "validOffset") <= size, "Invalid buffer valid span: " + r["label"])
    for b in entries("FULL_BUFFER_BIND"):
        matches = [r for r in entries("FULL_BUFFER_COPY") if r["event"] == b["event"] and r["role"] == b["role"] and r["phase"] == "input"]
        check(len(matches) == 1 and all(matches[0].get(k) == b.get(k) for k in ("buffer", "offset", "range")), "Binding slice missing/mismatched: " + str(b))
        if number(b, "writable") == 1:
            check(any(r["event"] == b["event"] and r["role"] == b["role"] and r["phase"] == "after-storage" for r in entries("FULL_BUFFER_COPY")), "Writable slice post-draw missing: " + b["role"])

    image_rows = entries("FULL_IMAGE_COPY") + entries("FULL_IMAGE_REF")
    image_copies = {(r["event"], r["label"]): r for r in entries("FULL_IMAGE_COPY")}
    for b in entries("FULL_IMAGE_BIND"):
        expected = {(m, l, a) for m in range(number(b, "mip"), number(b, "mip") + number(b, "levels"))
                    for l in range(number(b, "layer"), number(b, "layer") + number(b, "layers"))
                    for a in (1, 2, 4) if number(b, "aspects") & a}
        for phase in (["input", "after-storage"] if number(b, "writable") else ["input"]):
            got = {(number(r, "mip"), number(r, "layer"), number(r, "aspect")) for r in image_rows
                   if r["event"] == b["event"] and r["role"] == b["role"] and r["phase"] == phase}
            check(got == expected, f"Image subresource coverage missing: event{b['event']} {phase} {b['role']}")
    for r in entries("FULL_IMAGE_REF"):
        check((r["event"], r["label"]) in image_copies, "Dangling image reference: " + str(r))
    for p in entries("FULL_PASS"):
        for phase in ("before", "after"):
            attached = {r["role"] for r in image_rows if r["event"] == p["event"] and r["phase"] == phase}
            check(len(attached) == number(p, "attachmentCount"), f"Attachment coverage mismatch event{p['event']} {phase}")
    for r in entries("FULL_IMAGE_COPY"):
        w, h, d = (number(r, k) for k in ("width", "height", "depth"))
        bw, bh, bb = map(int, r["block"].split("/"))
        row, rows_count = ((w + bw - 1) // bw) * bb, (h + bh - 1) // bh
        check(row == number(r, "rowBytes") and rows_count == number(r, "blockRows") and row * rows_count * d == number(r, "bytes"), "Invalid packed-block size: " + r["label"])
        if number(r, "format") == 139:
            check((bw, bh, bb) == (4, 4, 8), "BC4 interpreted as pixels")
    for g in entries("FULL_GEOMETRY"):
        vertex_bindings = {r["binding"] for r in entries("FULL_VERTEX_LAYOUT") if r["event"] == g["event"]}
        geometry = {r["role"] for r in entries("FULL_BUFFER_COPY") if r["event"] == g["event"] and r["phase"] == "input"}
        check(all("vertex" + b in geometry for b in vertex_bindings), "Vertex/instance input missing")
        check(number(g, "indexed") != 1 or "index" in geometry, "Index buffer missing")
        check(number(g, "indirect") != 1 or "indirect" in geometry, "Actual indirect arguments missing")
    for r in entries("FULL_SHADER"):
        path = directory / filename(r)
        check(files.get(path.name) == number(r, "words") * 4, "Module byte count mismatch")
        if path.is_file():
            raw = path.read_bytes()
            check(len(raw) >= 20 and struct.unpack_from("<I", raw)[0] == 0x07230203, "Invalid SPIR-V header")
    stage_bits = {"0": 1, "1": 2, "2": 4, "3": 8, "4": 16}
    for r in entries("FULL_PIPELINE_STAGE"):
        check(any(s["event"] == r["event"] and s["module"] == r["module"] and stage_bits[s["stage"]] == number(r, "stage") for s in entries("FULL_SHADER")), "Pipeline module lacks exact retained bytes")
    for draw in draws:
        event = draw["event"]
        for kind in ("FULL_GEOMETRY", "FULL_PASS", "FULL_DYNAMIC", "FULL_PIPELINE_KEY", "PROBE_GROUP", "PROBE_DRAW", "PROBE_RASTER"):
            field = "group" if kind.startswith("PROBE_") else "event"
            check(any(r.get(field) == event for r in entries(kind)), f"{kind} missing event{event}")
    return {"directory": str(directory), "native_readback_audit": "PASS" if not errors else "FAIL", "frame": frame,
            "errors": errors, "files": hashes, "draws": draws, "copied_subresources": len(entries("FULL_IMAGE_COPY")),
            "buffer_slices": len(entries("FULL_BUFFER_COPY")), "CAPTURE_COMPLETE": "NO",
            "remaining": "Actual original API/state creation match, unmodified replay vs original defined GPU inputs/attachments, per-family visual/audio gates are separate."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.directory.resolve())
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "files"}, indent=2))
    raise SystemExit(0 if result["native_readback_audit"] == "PASS" else 1)

#!/usr/bin/env python3
"""Targeted materialization of missing UE5 Pak/IoStore containers into the host romfs.

Streams two large containers out of the base NSP RomFS (AES-CTR decrypted on the
fly) and writes them to the host romfs tree, chunk by chunk, without ever
extracting the whole 8.5 GB RomFS to disk.

Only the containers listed in TARGETS are touched. Everything else in the NSP is
left alone.

This is a *materialization* tool: it produces byte-identical copies of the
original containers. It is NOT a device validation, NOT a title screen proof and
NOT a playable claim.
"""
import argparse
import hashlib
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from romfs_stream import RomFSStream  # noqa: E402

DEFAULT_ROOT = Path("/Users/yuyan9923/GitHub/ff-resonance-rg477m-native")
DEFAULT_NSP = Path(
    "/Users/yuyan9923/Downloads/SwitchGame/"
    "[最终幻想：共鸣][FINAL FANTASY RESONANCE]/"
    "[本体v1.0.0][0100B8A026F14000][22.1.0][中文][HK].nsp"
)

# RomFS path -> (host romfs relative path, documented expected size in bytes)
# Expected sizes come from docs/analysis/asset_inventory.md (T04 RomFS inventory).
TARGETS = {
    "FFRS/Content/Paks/FFRS-Switch.pak": (
        "FFRS/Content/Paks/FFRS-Switch.pak",
        2_958_593_334,
    ),
    "FFRS/Content/Paks/FFRS-Switch.ucas": (
        "FFRS/Content/Paks/FFRS-Switch.ucas",
        5_976_848_928,
    ),
}

EXPECTED = {p: exp for p, (_, exp) in TARGETS.items()}

CHUNK = 16 * 1024 * 1024


def human(n: int) -> str:
    return f"{n / (1024 ** 3):.3f} GiB" if n >= 1024 ** 3 else f"{n / (1024 ** 2):.2f} MiB"


def sha256_file(path: Path, chunk: int = 8 * 1024 * 1024) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            blk = f.read(chunk)
            if not blk:
                break
            h.update(blk)
    return h.hexdigest()


def materialize(stream: RomFSStream, entry: dict, dest: Path, chunk: int, log) -> dict:
    """Stream one RomFS file to `dest`. Returns a per-file result record."""
    size = entry["data_size"]
    data_ofs = entry["data_offset"]
    started = time.time()
    h = hashlib.sha256()
    written = 0
    last_report = started

    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")

    with open(tmp, "wb") as out:
        remaining = size
        while remaining > 0:
            n = min(chunk, remaining)
            # read_file_data() -> read_romfs_raw(): per-call AES-CTR counter is
            # derived from the absolute offset, so chunked reads stay coherent.
            buf = stream.read_file_data(data_ofs + written, n)
            if len(buf) != n:
                raise IOError(
                    f"short read at offset {data_ofs + written}: "
                    f"expected {n} B, got {len(buf)} B"
                )
            out.write(buf)
            h.update(buf)
            written += n
            remaining -= n

            now = time.time()
            if now - last_report >= 30.0:
                rate = written / max(now - started, 1e-6)
                log(
                    f"    progress {human(written)} / {human(size)} "
                    f"({100.0 * written / size:.1f}%) "
                    f"rate={rate / (1024 * 1024):.1f} MiB/s "
                    f"elapsed={now - started:.0f}s"
                )
                last_report = now

        out.flush()
        os.fsync(out.fileno())

    # Atomic publish: a .part file is never mistaken for a complete container.
    os.replace(tmp, dest)

    actual = dest.stat().st_size
    digest = h.hexdigest()
    elapsed = time.time() - started
    return {
        "romfs_path": entry["path"],
        "host_path": str(dest),
        "bytes_written": written,
        "bytes_on_disk": actual,
        "romfs_tree_size": size,
        "expected_size": EXPECTED.get(entry["path"]),
        "size_match": actual == size,
        "sha256": digest,
        "elapsed_sec": round(elapsed, 2),
        "throughput_mib_s": round((written / max(elapsed, 1e-6)) / (1024 * 1024), 1),
    }


def main():
    ap = argparse.ArgumentParser(
        description="Materialize missing Pak/IoStore containers into the host romfs"
    )
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    ap.add_argument("--nsp", type=Path, default=DEFAULT_NSP)
    ap.add_argument("--romfs", type=Path, default=None,
                    help="host romfs tree (default: <root>/.local/romfs)")
    ap.add_argument("--chunk", type=int, default=CHUNK)
    ap.add_argument("--force", action="store_true",
                    help="re-extract even if the destination already looks complete")
    ap.add_argument("--verify-only", action="store_true",
                    help="only re-hash existing destinations; do not extract")
    ap.add_argument("--only", action="append", default=None,
                    help="restrict to a RomFS path (repeatable)")
    ap.add_argument("--log", type=Path, default=None)
    args = ap.parse_args()

    root = args.root.resolve()
    romfs_root = (args.romfs or (root / ".local" / "romfs")).resolve()
    local = root / ".local"
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    log_path = args.log or (local / "logs" / f"materialize-pak-{stamp}.log")
    log_path.parent.mkdir(parents=True, exist_ok=True)

    log_fh = open(log_path, "w", encoding="utf-8")

    def log(msg: str = ""):
        print(msg, flush=True)
        log_fh.write(msg + "\n")
        log_fh.flush()

    wanted = set(args.only) if args.only else set(TARGETS)

    log("=" * 78)
    log("FFRS container materialization log")
    log(f"generated_at        : {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}")
    log(f"repo_root           : {root}")
    log(f"nsp                 : {args.nsp}")
    log(f"host_romfs          : {romfs_root}")
    log(f"chunk_size          : {args.chunk} B")
    log(f"mode                : {'verify-only' if args.verify_only else 'materialize'}"
        f"{' (+force)' if args.force else ''}")
    log(f"git_head            : {os.popen('git -C %s rev-parse HEAD' % root).read().strip()}")
    log(f"git_status          : {json.dumps(os.popen('git -C %s status --short' % root).read().splitlines())}")
    log("=" * 78)
    log()

    for target in sorted(wanted):
        if target not in TARGETS:
            log(f"[!] unknown target: {target}")
            return 2

    stream = RomFSStream(args.nsp, local / "keys.txt",
                          local / "nca" / "base.tik", local / "nca" / "program_hdr.bin")
    log(f"[+] titlekey          : {stream.titlekey.hex()}")
    dirs, files = stream.parse_file_tree()
    by_path = {f["path"]: f for f in files}
    log(f"[+] romfs files       : {len(files)}")
    log()

    results = []
    ok = True
    try:
        for target in sorted(wanted, key=lambda p: TARGETS[p][1]):
            rel, expected = TARGETS[target]
            entry = by_path.get(target)
            dest = romfs_root / rel
            log(f"[*] {target}")
            log(f"    romfs_tree_size  : {entry['data_size']:,} B")
            log(f"    expected_size    : {expected:,} B")
            log(f"    destination      : {dest}")

            if entry is None:
                log("    [!] NOT FOUND in RomFS tree")
                ok = False
                continue

            if args.verify_only:
                if not dest.exists():
                    log("    [!] destination missing")
                    ok = False
                    continue
                started = time.time()
                digest = sha256_file(dest)
                rec = {
                    "romfs_path": target,
                    "host_path": str(dest),
                    "bytes_on_disk": dest.stat().st_size,
                    "romfs_tree_size": entry["data_size"],
                    "expected_size": expected,
                    "size_match": dest.stat().st_size == entry["data_size"],
                    "sha256": digest,
                    "elapsed_sec": round(time.time() - started, 2),
                    "mode": "verify-only",
                }
                results.append(rec)
                log(f"    size_on_disk     : {rec['bytes_on_disk']:,} B")
                log(f"    sha256           : {digest}")
                continue

            if dest.exists() and not args.force and dest.stat().st_size == entry["data_size"]:
                log("    [=] destination already complete; skipping (use --force to redo)")
                rec = {
                    "romfs_path": target,
                    "host_path": str(dest),
                    "bytes_on_disk": dest.stat().st_size,
                    "romfs_tree_size": entry["data_size"],
                    "expected_size": expected,
                    "size_match": True,
                    "sha256": sha256_file(dest),
                    "mode": "skipped-existing",
                }
                results.append(rec)
                log(f"    sha256           : {rec['sha256']}")
                log()
                continue

            rec = materialize(stream, entry, dest, args.chunk, log)
            rec["mode"] = "materialized"
            results.append(rec)

            log(f"    bytes_written    : {rec['bytes_written']:,} B")
            log(f"    bytes_on_disk    : {rec['bytes_on_disk']:,} B")
            log(f"    sha256           : {rec['sha256']}")
            log(f"    elapsed          : {rec['elapsed_sec']} s "
                f"({rec['throughput_mib_s']} MiB/s)")
            log(f"    size_match       : {rec['size_match']}")
            if not rec["size_match"]:
                ok = False
                log("    [!] SIZE MISMATCH vs RomFS tree")
            log()
    finally:
        stream.close()

    log("-" * 78)
    log("summary")
    total = 0
    for r in results:
        total += r["bytes_on_disk"]
        log(f"  {r['romfs_path']:42s} {r['bytes_on_disk']:>14,} B  "
            f"sha256={r['sha256']}  mode={r['mode']}")
    log(f"  total materialized : {total:,} B ({human(total)})")
    log(f"  overall            : {'PASS' if ok else 'FAIL'}")
    log("-" * 78)
    log()
    log("NOTE: byte-identical copies of the original NSP containers only.")
    log("      NOT a device validation, NOT a title screen proof, NOT a playable claim.")

    evidence = {
        "tool": "scripts/extract/materialize_paks.py",
        "scope": "host-side materialization of missing UE5 Pak/IoStore containers; "
                 "no device execution, no title screen, no playable claim",
        "nsp": str(args.nsp),
        "host_romfs": str(romfs_root),
        "chunk_size": args.chunk,
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "results": results,
        "overall": "PASS" if ok else "FAIL",
    }
    ev_path = log_path.with_suffix(".json")
    ev_path.write_text(json.dumps(evidence, indent=2, ensure_ascii=False) + "\n")
    log(f"evidence_json        : {ev_path}")
    log(f"log                  : {log_path}")

    log_fh.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

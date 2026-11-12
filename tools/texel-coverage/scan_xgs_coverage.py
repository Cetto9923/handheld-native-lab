#!/usr/bin/env python3
"""Stream the FFRS shader archive and measure typed-storage rewrite coverage.

The archive lives on a network mount, so the scan never extracts it. Each
shader bundle is read one entry at a time from the zip and handed to the
host analyzer, which links the shipping typed-storage headers. The analyzer's
verdicts are authoritative; this driver only collects and aggregates them.

Usage:
  scan_xgs_coverage.py --zip <FFRS-data.zip> --analyzer <binary> \
      --out <output-dir> [--limit N] [--jobs N]
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import time
import zipfile

# Only real shader bundles: the macOS AppleDouble "._" entries are not shaders.
ENTRY_PREFIX = "FFRS/romfs/shaders/"
ENTRY_SUFFIX = ".xgs"


def list_shaders(path):
    """Return the sorted list of real shader bundle names in the archive."""
    with zipfile.ZipFile(path) as archive:
        names = [
            name for name in archive.namelist()
            if name.startswith(ENTRY_PREFIX)
            and name.endswith(ENTRY_SUFFIX)
            and not name[len(ENTRY_PREFIX):].startswith("._")
        ]
    return sorted(names)


def pack_record(key, payload):
    """Frame one bundle for the analyzer's length-prefixed stdin protocol."""
    key_bytes = key.encode("ascii")
    return struct.pack("<I", len(key_bytes)) + key_bytes + struct.pack("<I", len(payload)) + payload


def read_exactly(stream, count):
    """Read exactly count bytes, or return None at a clean end of stream."""
    chunks = []
    remaining = count
    while remaining:
        chunk = stream.read(remaining)
        if not chunk:
            return None
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def run_analyzer(analyzer, records, out_path):
    """Feed the framed bundles to the analyzer and collect its JSON lines."""
    proc = subprocess.Popen(
        [analyzer], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    results = []
    stderr_path = out_path + ".stderr"
    with open(stderr_path, "wb") as err_file:
        proc.stdin.write(records)
        proc.stdin.close()
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            results.append(json.loads(line))
        err_file.write(proc.stderr.read())
    code = proc.wait()
    return results, code, stderr_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip", required=True, help="FFRS-data.zip on the archive mount")
    parser.add_argument("--analyzer", required=True, help="typed_storage_analyzer binary")
    parser.add_argument("--out", required=True, help="output directory for the raw JSONL")
    parser.add_argument("--limit", type=int, default=0, help="stop after N shaders (0 = all)")
    parser.add_argument("--dump-dir", default="",
                        help="directory for the rewritten SPIR-V of every accepted rewrite")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    names = list_shaders(args.zip)
    if args.limit:
        names = names[:args.limit]
    print(f"shader bundles: {len(names)}", flush=True)

    started = time.time()
    raw_path = os.path.join(args.out, "modules.jsonl")
    # Stream the archive: read one entry, frame it, append to the analyzer.
    # The bundles are kept in memory in batches so the network mount is read
    # once, sequentially, without extracting the archive.
    records = bytearray()
    results = []
    analyzer_exit = 0
    stderr_path = os.path.join(args.out, "analyzer.stderr")
    with zipfile.ZipFile(args.zip) as archive, open(raw_path, "w") as raw_file:
        analyzer_argv = [args.analyzer]
        if args.dump_dir:
            analyzer_argv.append(args.dump_dir)
        proc = subprocess.Popen(
            analyzer_argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert proc.stdin is not None and proc.stdout is not None
        # Feed and drain concurrently so neither side blocks on a full pipe.
        import threading

        def feed():
            try:
                for name in names:
                    payload = archive.read(name)
                    key = name[len(ENTRY_PREFIX):-len(ENTRY_SUFFIX)]
                    records.extend(pack_record(key, payload))
                    if len(records) > (64 << 20):
                        proc.stdin.write(records)
                        records.clear()
                if records:
                    proc.stdin.write(records)
                    records.clear()
            finally:
                proc.stdin.close()

        writer = threading.Thread(target=feed, daemon=True)
        writer.start()
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            raw_file.write(line.decode("utf-8") + chr(10))
            results.append(json.loads(line))
        writer.join()
        with open(stderr_path, "wb") as err_file:
            err_file.write(proc.stderr.read())
        analyzer_exit = proc.wait()

    elapsed = time.time() - started
    print(f"analyzed: {len(results)} in {elapsed:.1f}s (analyzer exit {analyzer_exit})", flush=True)
    summary = {
        "zip": args.zip,
        "shader_count": len(names),
        "analyzed": len(results),
        "analyzer_exit": analyzer_exit,
        "seconds": round(elapsed, 2),
        "raw_jsonl": raw_path,
        "analyzer_stderr": stderr_path,
        "dump_dir": args.dump_dir,
    }
    with open(os.path.join(args.out, "scan_summary.json"), "w") as handle:
        json.dump(summary, handle, indent=2)
    print(json.dumps(summary, indent=2))
    return 0 if analyzer_exit == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

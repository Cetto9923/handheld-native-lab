#!/usr/bin/env python3
"""Reproduce readonly QueryMemoryInfo evidence from supplied uncompressed NSOs.

Run from the repository root. Existing clang/llvm-objdump disassemble bounded
slices; no supplied module code executes. Derived bytes stay in the chosen
private output directory, which must not exist before this invocation.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys


EXPECTED = {
    "sdk": "9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9",
    "main": "6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5",
}
SLICES = {
    "sdk": [
        "0x143710:0x4", "0x14711c:0xc0", "0x17570c:0x20",
        "0x143ed0:0xa0", "0x145480:0x138", "0x1455c0:0x1b0",
        "0x145260:0x220", "0x148fa4:0x68", "0x1493e0:0x268",
        "0x1421c4:0xc4", "0x141a68:0x214", "0x141f00:0xc8",
        "0x147f4c:0xcc", "0x141880:0xe8", "0x17557c:0x18",
        "0x144710:0x60", "0x1408a0:0x80", "0x1464f0:0x184",
        "0x146790:0x1a0", "0x146930:0x74", "0x17572c:0x10",
    ],
    "main": ["0x13954:0x94", "0x279f0:0x94"],
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = Path.cwd()
    out = args.out.resolve()
    private_root = (root / ".local").resolve()
    if not out.is_relative_to(private_root):
        parser.error("derived instruction evidence must stay under this checkout's .local")
    out.mkdir(parents=True, exist_ok=False)
    inputs = {key: root / ".local/uncompressed" / (key + ".bin") for key in EXPECTED}
    manifest = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "cwd": str(root),
        "kind": "static-only; no target execution",
        "input_before": {},
        "commands": [],
    }
    manifest_path = out / "manifest.json"

    def save():
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")

    for key, path in inputs.items():
        manifest["input_before"][key] = {
            "path": str(path), "size": path.stat().st_size, "sha256": sha256(path),
        }
        if manifest["input_before"][key]["sha256"] != EXPECTED[key]:
            raise ValueError("source version mismatch: " + key)
    save()
    for key, slices in SLICES.items():
        command = [sys.executable, "scripts/runtime/inspect_nso.py", str(inputs[key]),
                   "--out", str(out / key)]
        for value in slices:
            command.extend(["--slice", value])
        record = {"command": command, "cwd": str(root)}
        manifest["commands"].append(record)
        save()
        with (out / (key + ".stdout")).open("wb") as stdout, \
                (out / (key + ".stderr")).open("wb") as stderr:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr,
                                       start_new_session=True)
            record["pid"] = process.pid
            save()
            try:
                process.wait(timeout=45)
                record["timed_out"] = False
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                record["timed_out"] = True
        record["returncode"] = process.returncode
        save()
        if record["timed_out"] or process.returncode:
            raise RuntimeError("static disassembly failed: " + key)
    manifest["input_after"] = {key: sha256(path) for key, path in inputs.items()}
    if manifest["input_after"] != EXPECTED:
        raise ValueError("input changed during static inspection")
    manifest["source_unchanged"] = True
    manifest["artifacts"] = [
        {"path": str(path.relative_to(out)), "size": path.stat().st_size,
         "sha256": sha256(path)}
        for path in sorted(out.rglob("*")) if path.is_file() and path != manifest_path
    ]
    save()
    print(json.dumps({"out": str(out), "returncodes": [
        record["returncode"] for record in manifest["commands"]], "source_unchanged": True}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Run a bounded native experiment without losing its exit status to a pipe."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.timeout <= 0:
        parser.error("provide a command and a positive timeout")
    args.out.mkdir(parents=True, exist_ok=False)
    metadata = {"command": command, "cwd": os.getcwd(), "timeout_seconds": args.timeout,
                "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}
    metadata_path = args.out / "run.json"
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    started = time.monotonic()
    with (args.out / "stdout.log").open("wb") as stdout, (args.out / "stderr.log").open("wb") as stderr:
        try:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True)
        except OSError as exc:
            metadata.update(launch_error=str(exc), returncode=None, timed_out=False,
                            elapsed_seconds=time.monotonic() - started)
            metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
            print(json.dumps({"run": str(metadata_path), "launch_error": str(exc)}))
            return 127
        metadata["pid"] = process.pid
        metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
        try:
            process.wait(timeout=args.timeout)
            metadata["timed_out"] = False
        except subprocess.TimeoutExpired:
            metadata["timed_out"] = True
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
    metadata.update(returncode=process.returncode, elapsed_seconds=time.monotonic() - started)
    for name in ("stdout.log", "stderr.log"):
        data = (args.out / name).read_bytes()
        metadata[name] = {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps({"run": str(metadata_path), "returncode": process.returncode,
                      "timed_out": metadata["timed_out"]}))
    return 124 if metadata["timed_out"] else (128 - process.returncode if process.returncode < 0 else process.returncode)


if __name__ == "__main__":
    sys.exit(main())

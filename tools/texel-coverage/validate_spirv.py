#!/usr/bin/env python3
"""Validate rewritten SPIR-V modules with spirv-val.

The typed-storage rewrite only changes the Image Format operand of one
OpTypeImage. This script runs the real SPIR-V validator over every rewritten
module the analyzer produced and records the exit code of each run, so the
coverage report can state that the rewritten modules are valid SPIR-V for the
Vulkan 1.1 target environment instead of asserting it.

Usage:
  validate_spirv.py --dump-dir <dir> --out <report.json> [--target-env vulkan1.1]
"""
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys


def validate(args):
    validator, target_env, path = args
    proc = subprocess.run(
        [validator, "--target-env", target_env, path],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return path, proc.returncode, proc.stderr.decode("utf-8", "replace").strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump-dir", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--validator", default="spirv-val")
    parser.add_argument("--target-env", default="vulkan1.1")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    version = subprocess.run(
        [args.validator, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    ).stdout.decode("utf-8", "replace").strip()

    files = sorted(
        os.path.join(args.dump_dir, name)
        for name in os.listdir(args.dump_dir)
        if name.endswith(".spv")
    )
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for path, code, message in pool.map(
            validate, [(args.validator, args.target_env, path) for path in files]
        ):
            if code != 0:
                failures.append({"file": os.path.basename(path), "exit": code, "message": message})

    report = {
        "validator": version,
        "validator_path": args.validator,
        "target_env": args.target_env,
        "dump_dir": args.dump_dir,
        "modules_validated": len(files),
        "passed": len(files) - len(failures),
        "failed": len(failures),
        "failures": failures[:200],
    }
    with open(args.out, "w") as handle:
        json.dump(report, handle, indent=2)
    print(json.dumps({k: v for k, v in report.items() if k != "failures"}, indent=2))
    for failure in failures[:20]:
        print("FAIL", failure["file"], failure["exit"], failure["message"][:200], file=sys.stderr)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())

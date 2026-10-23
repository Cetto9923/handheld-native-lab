#!/usr/bin/env python3
"""Ensure local evidence validators reject missing/corrupt success evidence."""
import argparse
import copy
import json
from pathlib import Path
import runpy
import struct
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", required=True)
    parser.add_argument("--result", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    check_result = runpy.run_path(str(root / "scripts/graphics/validate_vulkan_result.py"))["validate"]
    check_elf = runpy.run_path(str(root / "scripts/graphics/validate_vulkan_artifact.py"))["verify_elf"]
    result = json.loads(Path(args.result).read_text())
    check_result(result, 0, result["source_sha256"])
    records = ["complete-json-accepted"]
    mutations = {
        "missing-stage": lambda r: r["stages"].pop(),
        "failed-vk-result": lambda r: r["stages"][0].update(vk_result=-1),
        "unsubmitted": lambda r: r["operations"].update(queue_submitted=False),
        "uncompared": lambda r: r["operations"].update(readback_compared=False),
        "buffer-mismatch": lambda r: r["operations"].update(buffer_mismatches=1),
        "image-short-read": lambda r: r["operations"].update(image_compared_bytes=255),
        "wrong-source": lambda r: r.update(source_sha256="incorrect"),
        "false-feature-claim": lambda r: r["features"].update(descriptor_indexing_used=True),
        "missing-cleanup": lambda r: r["cleanup"].update(destroyed_objects=0),
        "exit-mismatch": lambda r: r.update(exit_code=2),
    }
    for label, mutate in mutations.items():
        altered = copy.deepcopy(result)
        mutate(altered)
        try:
            check_result(altered, 0, result["source_sha256"])
        except ValueError:
            records.append(label + "-rejected")
        else:
            raise AssertionError(label + " was accepted")
    check_elf(args.elf)
    records.append("actual-android-elf-accepted")
    original = Path(args.elf).read_bytes()
    elf_header = struct.unpack_from("<16sHHIQQQIHHHHHH", original)
    table, size, count = elf_header[5], elf_header[9], elf_header[10]
    program_headers = [(table + i * size, struct.unpack_from("<IIQQQQQQ", original, table + i * size))
                       for i in range(count)]
    load = next(offset for offset, header in program_headers if header[0] == 1)
    stack = next(offset for offset, header in program_headers if header[0] == 0x6474E551)
    relro = next(offset for offset, header in program_headers if header[0] == 0x6474E552)
    mutations_elf = {"wrong-machine": (18, "H", 62), "small-page-alignment": (load + 48, "Q", 4096),
                     "writable-executable": (load + 4, "I", 7), "executable-stack": (stack + 4, "I", 7),
                     "missing-relro": (relro, "I", 0)}
    with tempfile.TemporaryDirectory(prefix="ffrs-vulkan-validator-") as temp:
        target = Path(temp) / "mutated-elf"
        for label, (offset, fmt, value) in mutations_elf.items():
            altered = bytearray(original)
            struct.pack_into("<" + fmt, altered, offset, value)
            target.write_bytes(altered)
            try:
                check_elf(target)
            except AssertionError:
                records.append(label + "-rejected")
            else:
                raise AssertionError(label + " was accepted")
    summary = {"status": "PASS", "scope": "evidence validator unit tests only", "cases": len(records), "tests": records}
    Path(args.output).write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({key: value for key, value in summary.items() if key != "tests"}))


if __name__ == "__main__":
    main()

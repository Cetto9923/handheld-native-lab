#!/usr/bin/env python3
"""Compile the actual C source with a host test double; no GPU/Android PASS claim."""
import argparse
import json
import os
from pathlib import Path
import runpy
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--toolchain", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--watchdog", action="store_true", help="also execute the 20-second watchdog failure test")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    include = Path(args.toolchain).resolve() / "sysroot/usr/include"
    binary = output / "host_vulkan_probe_test_double"
    source_sha, source_records = runpy.run_path(str(root / "scripts/graphics/validate_vulkan_artifact.py"))["source_bundle"](root)
    (output / "source-bundle.json").write_text(json.dumps({"sha256": source_sha, "sources": source_records}, indent=2) + "\n")
    command = ["clang", "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-isystem", str(include / "vulkan"),
               "-iquote", str(include), '-DFFRS_VK_SOURCE_SHA256="' + source_sha + '"',
               str(root / "src/graphics/vulkan_probe/vulkan_transfer_probe.c"),
               str(root / "src/graphics/vulkan/buffer_resource.c"),
               str(root / "tests/graphics/vulkan_mock_driver.c"), "-o", str(binary)]
    (output / "compile-command.json").write_text(json.dumps(command, indent=2) + "\n")
    compile_result = subprocess.run(command, text=True, capture_output=True)
    (output / "compile.stdout").write_text(compile_result.stdout)
    (output / "compile.stderr").write_text(compile_result.stderr)
    (output / "compile.exitcode").write_text(str(compile_result.returncode) + "\n")
    compile_result.check_returncode()
    records = []

    contract_binary = output / "host_buffer_resource_contract"
    contract_command = command[:command.index(str(root / "src/graphics/vulkan_probe/vulkan_transfer_probe.c"))]
    contract_command += ["-DFFRS_VK_BUFFER_RESOURCE_UNIT_TEST",
                         str(root / "src/graphics/vulkan/buffer_resource.c"),
                         str(root / "tests/graphics/vulkan_mock_driver.c"), "-o", str(contract_binary)]
    (output / "resource-contract-compile-command.json").write_text(json.dumps(contract_command, indent=2) + "\n")
    contract_compile = subprocess.run(contract_command, text=True, capture_output=True)
    (output / "resource-contract-compile.stdout").write_text(contract_compile.stdout)
    (output / "resource-contract-compile.stderr").write_text(contract_compile.stderr)
    (output / "resource-contract-compile.exitcode").write_text(str(contract_compile.returncode) + "\n")
    contract_compile.check_returncode()
    contract_records = []
    for label, environment in (("coherent", {}), ("noncoherent", {"MOCK_NONCOHERENT": "1"})):
        checked = subprocess.run([str(contract_binary)], env={**os.environ, **environment},
                                 text=True, capture_output=True, timeout=5)
        stem = output / ("resource-contract-" + label)
        stem.with_suffix(".stdout.json").write_text(checked.stdout)
        stem.with_suffix(".stderr").write_text(checked.stderr)
        stem.with_suffix(".exitcode").write_text(str(checked.returncode) + "\n")
        checked.check_returncode()
        contract_report, mock_report = json.loads(checked.stdout), json.loads(checked.stderr)
        assert contract_report["status"] == "PASS" and contract_report["gpu_execution"] == "NOT_RUN"
        assert all(contract_report[k] is True for k in
                   ("quiescence_refusal", "idempotent_release", "busy_owner_preserved", "local_errors_issue_no_vk_calls"))
        assert mock_report["live_objects"] == 0 and mock_report["mapped_memories"] == 0
        assert mock_report["flush_calls"] == 1 and mock_report["invalidate_calls"] == 1
        contract_records.append({"case": label, "exit_code": checked.returncode, "scope": "host contract only"})

    def run(label, environment, expected_exit):
        result = subprocess.run([str(binary)], env={**os.environ, **environment},
                                text=True, capture_output=True, timeout=25)
        (output / (label + ".stdout.json")).write_text(result.stdout)
        (output / (label + ".stderr")).write_text(result.stderr)
        (output / (label + ".exitcode")).write_text(str(result.returncode) + "\n")
        assert result.returncode == expected_exit, (label, result.returncode, result.stderr)
        report = json.loads(result.stdout)
        assert report["exit_code"] == expected_exit
        assert report["status"] == ("PASS" if expected_exit == 0 else "FAIL")
        assert "AddressSanitizer" not in result.stderr and "runtime error:" not in result.stderr
        if expected_exit != 124:
            mock = json.loads(result.stderr)
            pending = report["cleanup"]["strategy"] == "process_teardown_inflight_uncertain"
            assert (mock["live_objects"] > 0) if pending else (mock["live_objects"] == 0)
            assert (mock["mapped_memories"] > 0) if pending else (mock["mapped_memories"] == 0)
        records.append({"case": label, "environment": environment, "exit_code": result.returncode,
                        "status": "PASS", "coverage": "host test double only"})
        return report

    baseline = run("success", {}, 0)
    assert baseline["operations"]["buffer_compared_bytes"] == 65536
    assert baseline["operations"]["image_compared_bytes"] == 256
    assert baseline["operations"]["fence_completed"] is True
    assert baseline["features"]["descriptor_indexing_used"] is False
    assert baseline["cleanup"]["destroyed_objects"] == 12
    assert baseline["cleanup"]["unmapped_allocations"] == 3
    noncoherent = run("noncoherent-memory", {"MOCK_NONCOHERENT": "1"}, 0)
    assert not noncoherent["memory"]["source"]["memory_flags"] & 4
    for case in ("success", "noncoherent-memory"):
        mock = json.loads((output / (case + ".stderr")).read_text())
        assert mock["flush_calls"] == 3 and mock["invalidate_calls"] == 1
    for call in range(1, len(baseline["stages"]) + 1):
        failure = run(f"vkresult-failure-{call:02d}", {"MOCK_FAIL_CALL": str(call)}, 2)
        assert failure["vk_result_errors"] == 1
        assert failure["stages"][-1]["vk_result"] == -1
        assert failure["failure_stage"] == baseline["stages"][call - 1]["stage"]
    timeout = run("fence-timeout", {"MOCK_TIMEOUT": "1"}, 4)
    assert timeout["failure"] == "FENCE_TIMEOUT" and not timeout["operations"]["readback_compared"]
    assert timeout["stages"][-1]["vk_result"] == 2
    lost = run("device-lost-wait", {"MOCK_WAIT_DEVICE_LOST": "1"}, 2)
    assert lost["stages"][-1]["vk_result"] == -4
    assert lost["cleanup"]["strategy"] == "process_teardown_inflight_uncertain"
    assert not lost["operations"]["fence_completed"] and not lost["operations"]["readback_compared"]
    for target, offset in (("buffer", 123), ("image", 17)):
        mismatch = run(target + "-mismatch", {"MOCK_CORRUPT": target}, 3)
        assert mismatch["operations"][target + "_mismatches"] == 1
        assert mismatch["operations"][target + "_first_mismatch"] == offset
    for label, env in (("no-device", "MOCK_NO_DEVICE"), ("no-queue", "MOCK_NO_QUEUE"),
                       ("no-host-memory", "MOCK_NO_HOST_MEMORY")):
        run(label, {env: "1"}, 5)
    if args.watchdog:
        watchdog = run("watchdog-timeout", {"MOCK_HANG": "1"}, 124)
        assert watchdog["failure"] == "PROCESS_WATCHDOG_TIMEOUT"
    summary = {"schema": "ffrs.vulkan.host-tests.v1", "status": "PASS", "cases": len(records),
               "android_execution": "NOT_RUN", "gpu_execution": "NOT_RUN", "tests": records,
               "resource_contract_tests": contract_records}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({k: v for k, v in summary.items() if k != "tests"}))


if __name__ == "__main__":
    main()

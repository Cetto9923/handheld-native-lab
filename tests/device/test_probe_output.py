#!/usr/bin/env python3
"""
tests/device/test_probe_output.py
Validates the structure and determinism of probe_rg477m JSON output.
"""

import json
import os
import subprocess
import sys
import tempfile

def test_json_output(bin_path: str):
    print(f"Testing binary: {bin_path}")
    assert os.path.isfile(bin_path), f"Binary not found: {bin_path}"

    res = subprocess.run([bin_path, "--json"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res.returncode == 0, f"probe exited with code {res.returncode}, stderr: {res.stderr}"

    data = json.loads(res.stdout)
    required_sections = ["target_metadata", "device", "memory", "storage", "cpu", "display", "input", "audio", "vulkan"]
    for s in required_sections:
        assert s in data, f"Missing section: {s}"

    # Target metadata
    meta = data["target_metadata"]
    assert meta["target_device"] == "Anbernic RG477M"
    assert meta["target_soc"] == "MediaTek Dimensity 8300"
    assert meta["target_gpu"] == "Mali-G615 MC6"
    assert meta["target_abi"] == "arm64-v8a"

    # Memory
    mem = data["memory"]
    assert "page_size_bytes" in mem
    page_size = mem["page_size_bytes"]
    assert page_size in (4096, 16384, 65536), f"Unexpected page size: {page_size}"

    # CPU
    cpu = data["cpu"]
    assert "cores_online" in cpu
    assert cpu["cores_online"] > 0

    # This executable has no Activity/window snapshot. Unknown measurements
    # must remain unknown on Android as well as the host, never RG defaults.
    display = data["display"]
    assert display["status"] == "unavailable_no_activity_context"
    for field in ("width_px", "height_px", "density_dpi", "density", "refresh_rate_hz"):
        assert display[field] == 0, (field, display[field])
    assert display["modes"] == []
    assert display["refresh_rate_status"] == "unavailable"
    inputs = data["input"]
    assert inputs["status"] == "unavailable_no_android_api_snapshot"
    assert not inputs["enumeration_complete"] and inputs["devices"] == []
    assert inputs["gamepad_count_status"] == "unavailable"
    assert inputs["key_code_namespace"] == "android.view.KeyEvent.KEYCODE_*"
    audio = data["audio"]
    assert audio["status"] != "ok"
    for field in ("output_device_count", "sample_rate_hz", "output_frames_per_buffer",
                  "native_output_sample_rate_hz", "min_buffer_size_bytes"):
        assert audio[field] == 0, (field, audio[field])
    assert not audio["audiotrack_create_ok"] and audio["channel_config"] == ""

    # Vulkan
    vk = data["vulkan"]
    assert "status" in vk

    print("JSON schema and contents validated successfully.")

def test_file_output(bin_path: str):
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
        out_path = tf.name

    try:
        res = subprocess.run([bin_path, "-o", out_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert res.returncode == 0
        assert os.path.getsize(out_path) > 0
        with open(out_path, "r") as f:
            data = json.load(f)
            assert "target_metadata" in data
        print("File output option validated successfully.")
    finally:
        if os.path.exists(out_path):
            os.remove(out_path)

def test_summary_output(bin_path: str):
    res = subprocess.run([bin_path, "--summary"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    assert res.returncode == 0
    assert "RG477M Native Hardware & Capabilities Probe" in res.stdout
    assert "Memory:" in res.stdout
    assert "CPU:" in res.stdout
    assert "Vulkan:" in res.stdout
    assert "gamepad count unavailable" in res.stdout and "Detected 0 gamepad" not in res.stdout
    print("Summary output validated successfully.")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        binary = sys.argv[1]
    else:
        binary = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../build-host/probe_rg477m"))

    test_json_output(binary)
    test_file_output(binary)
    test_summary_output(binary)
    print("ALL TESTS PASSED.")

#!/usr/bin/env python3
"""Check a captured probe JSON against its independently captured real exit code."""
import argparse
import json
from pathlib import Path

REQUIRED_STAGES = (
    "create_instance", "enumerate_devices_count", "enumerate_devices", "create_device",
    "create_source_buffer", "allocate_source_memory", "bind_source_memory", "map_source_memory",
    "create_destination_buffer", "allocate_destination_memory", "bind_destination_memory", "map_destination_memory",
    "create_pixel_buffer", "allocate_pixel_memory", "bind_pixel_memory", "map_pixel_memory",
    "flush_source_memory", "flush_destination_memory", "flush_pixel_memory", "query_image_format",
    "create_image", "allocate_image_memory", "bind_image_memory", "create_command_pool",
    "allocate_command_buffer", "create_fence", "begin_command_buffer", "end_command_buffer",
    "queue_submit", "wait_for_fence", "invalidate_readback_memory",
)


def validate(report, actual_exit, source_sha256):
    if report.get("schema") != "ffrs.vulkan.transfer.v1":
        raise ValueError("unexpected JSON schema")
    if report.get("exit_code") != actual_exit:
        raise ValueError("JSON exit_code differs from independently captured process exit")
    if actual_exit != 0 or report.get("status") != "PASS":
        raise ValueError("probe did not report a successful transfer run")
    if report.get("source_sha256") != source_sha256:
        raise ValueError("probe source hash differs from frozen source")
    if report.get("scope") != "buffer_copy_and_image_clear_transfer_only" or report.get("game_rendering") is not False:
        raise ValueError("unexpected claimed scope")
    if report.get("failure") is not None or report.get("failure_stage") is not None or report.get("vk_result_errors") != 0:
        raise ValueError("successful probe contains an error")
    stages = report.get("stages", [])
    if tuple(s.get("stage") for s in stages) != REQUIRED_STAGES:
        raise ValueError("missing or reordered Vulkan operation stages")
    if any(s.get("vk_result") != 0 or s.get("vk_result_name") != "VK_SUCCESS" for s in stages):
        raise ValueError("a Vulkan result was not VK_SUCCESS")
    operations = report["operations"]
    if not all(operations.get(k) is True for k in
               ("commands_recorded", "queue_submitted", "fence_completed", "readback_compared")):
        raise ValueError("GPU submit, completion, or readback evidence missing")
    expected = {"buffer_copy_bytes": 65536, "buffer_compared_bytes": 65536, "buffer_mismatches": 0,
                "buffer_first_mismatch": None, "image_format": "VK_FORMAT_R8G8B8A8_UNORM",
                "image_extent": [8, 8], "image_expected_rgba": [255, 0, 255, 255],
                "image_compared_bytes": 256, "image_mismatches": 0, "image_first_mismatch": None,
                "fence_timeout_ns": 5000000000}
    for key, value in expected.items():
        if operations.get(key) != value:
            raise ValueError("unexpected transfer result: " + key)
    if not report["device"]["name"] or not report["device"]["queue_flags"] & 1:
        raise ValueError("graphics queue/device identity absent")
    features = report["features"]
    if features.get("optional_features_enabled") is not False or features.get("descriptor_indexing_used") is not False:
        raise ValueError("unexpected optional-feature claim")
    if features.get("device_extensions_enabled") != [] or features.get("timeline_semaphore_used") is not False:
        raise ValueError("unexpected extension or timeline semaphore claim")
    if report["cleanup"] != {"strategy": "explicit_reverse_release", "destroyed_objects": 12, "unmapped_allocations": 3}:
        raise ValueError("successful run did not explicitly release all owned objects")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("json_path")
    parser.add_argument("--exit-code", required=True, type=int)
    parser.add_argument("--source-sha256", required=True)
    args = parser.parse_args()
    try:
        report = json.loads(Path(args.json_path).read_text())
        validate(report, args.exit_code, args.source_sha256)
    except (ValueError, KeyError, TypeError) as error:
        parser.exit(1, "RESULT_REJECTED: " + str(error) + "\n")
    print(json.dumps({"validation": "PASS", "scope": report["scope"], "device": report["device"]["name"],
                      "buffer_compared_bytes": report["operations"]["buffer_compared_bytes"],
                      "image_compared_bytes": report["operations"]["image_compared_bytes"], "game_rendering": False}))


if __name__ == "__main__":
    main()

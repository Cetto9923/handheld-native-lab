# Vulkan transfer operation probe

This standalone C11 CLI exercises Vulkan 1.0 core operations on an Android ARM64
device. It does not use game code, shaders, surfaces, presentation, descriptor
indexing, or timeline semaphores. A successful run proves the buffer/image
transfer path tested, not a game rendering backend or any frame-rate target.

The CLI creates an instance, selects a physical device with a graphics queue,
creates a logical device and queue, then allocates three host-visible buffers
and one optimal-tiled `VK_FORMAT_R8G8B8A8_UNORM` image. One command buffer copies
a deterministic 65,536-byte pattern, transitions/clears an 8×8 image to
RGBA `[255,0,255,255]`, transitions it again, and copies its 256 bytes to a host
buffer. Explicit host/transfer barriers, whole-allocation flush/invalidate
ranges, queue submit, and fence completion precede exact byte comparison.

Every function returning `VkResult` is recorded in the JSON `stages` array.
Command-recording functions return void; their completion does not independently
prove GPU execution. `operations.queue_submitted`, `fence_completed`, and
`readback_compared` report those separate stages. Core features are queried but
no optional features or extensions are enabled. Physical-device API support and
the requested instance API version are separate fields.

Normal completion and failures before submission explicitly release owned
objects in reverse order. The fence wait requests a 5-second timeout; Vulkan
permits implementation-dependent timeout granularity. On timeout or another
wait error without confirmed completion, destroying in-flight resources would
violate Vulkan lifetime rules. The CLI therefore reports failure and uses process
teardown to reclaim those objects, without an unbounded `vkDeviceWaitIdle`.
A 20-second `SIGALRM` watchdog covers driver calls and cleanup. Its signal handler
only writes a minimal failure JSON and exits with code 124. An external runner
must still capture exit, timeout, stderr, device identity, and artifact hash.

Exit codes: 0 successful transfer/readback; 2 Vulkan/setup failure; 3 byte
mismatch; 4 fence timeout; 5 unsupported resource/queue/memory selection;
64 invalid arguments; 124 process watchdog. A failed operation has no fallback
path that can report success.

Build, using an explicitly configured partial official NDK cache:

```sh
FFRS_VK_TOOLCHAIN=/path/to/toolchain \
FFRS_VK_MEMBERS=/path/to/validated-members.json \
bash scripts/graphics/build_vulkan_transfer_android.sh
```

The build captures source snapshots, compiler/linker commands, real exits,
per-input hashes/CRC checks, an ELF protection check, and artifact SHA-256 in
`.local/vulkan-ops/`. It does not execute a device command. Source is frozen
before compilation; the build's Git HEAD and source hash are embedded in JSON.
The Android `libvulkan.so` used for linking is an NDK stub, not a software GPU.

Host failure-path tests compile the same C source with a clearly named test
double and ASan/UBSan. These results never count as Android/GPU validation:

```sh
python3 tests/graphics/test_vulkan_transfer_probe.py \
  --toolchain /path/to/toolchain --output .local/unique-host-test-run --watchdog
```

After the designated device operator captures the raw JSON and actual exit:

```sh
python3 scripts/graphics/validate_vulkan_result.py captured.json \
  --exit-code 0 --source-sha256 ACTUAL_FROZEN_SOURCE_SHA256
```

This validator checks the full expected operation sequence, exact byte counts,
errors, feature claims, cleanup, and source provenance. It does not establish
device identity or independence of the operator; retain the original execution
record alongside the validated JSON.

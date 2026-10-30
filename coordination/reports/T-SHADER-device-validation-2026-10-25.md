# T-SHADER device validation — RG477M (Mali-G615 MC6), 2026-10-25

Card: P-MM-23 device shader validation. Scope of this report: what the real
handheld answers about the offline-translated SPIR-V, and what it does not.

Nothing here claims that the port runs, that a frame exists, or that the
pipeline of the game is ready. Every tier below is reported separately, with
the driver's own result code, and a tier that was not reached says why.

## 1. What was run on the device

A standalone probe, `src/graphics/vulkan_probe/vulkan_shader_probe.c`, built by
`scripts/graphics/build_vulkan_shader_probe_android.sh` for aarch64-linux-android28.
It creates its own instance and device and never touches
`src/graphics/nvn_device/nvn_backend_vulkan.c`. The Vulkan loader is opened with
`dlopen` at run time and the entry points are resolved per scope, so the same
source starts on a host without a Vulkan loader and reports
`NO_VULKAN_IN_THIS_BUILD` instead of failing to link or inventing a device.

Device: `RG477M01445122` (RG477M, Android 14, board `mt6897`, `ro.hardware.vulkan=mali`).

## 2. Device Vulkan facts

Full file: `.local/shader-verify/device-vulkan-facts.json`.

| fact | value |
|---|---|
| instance API requested / used | 1.1.0 / 1.1.0 (loader supports 1.3.0) |
| loader | `libvulkan.so` (dlopen at run time) |
| physical device | `Mali-G615 MC6`, INTEGRATED_GPU, index 0 |
| physical device API version | 1.3.247 |
| driver version | 184553472 |
| vendor / device id | 5045 / 3097694256 |
| queue family | 0, flags 7 (GRAPHICS | COMPUTE | TRANSFER) |
| `VK_EXT_fragment_shader_interlock` | not offered (117 other extensions are) |

Support and enablement are two different facts and are kept apart:

| feature | device support | enabled by this probe | asked for by `nvn_backend_vulkan.c` |
|---|---|---|---|
| `shaderStorageImageWriteWithoutFormat` | **true** | **true** | true, conditionally on support (lines 178-186), so on this device it is on |
| `shaderStorageImageReadWithoutFormat` | true | true | not requested |
| `shaderStorageImageExtendedFormats` | true | true | not requested |
| `shaderInt64` | true | true | not requested |
| `shaderDrawParameters` | true | true | not requested |
| `shaderFloat64` | **false** | false (not supported, so not asked for) | not requested |

The device **does** support `shaderStorageImageWriteWithoutFormat`. That was
open until this probe asked the device; the runtime's conditional enablement in
`nvn_backend_vulkan.c` is therefore active on this handheld.

## 3. spirv-val before and after the capability word

Command for every module: `spirv-val --target-env vulkan1.1 <file>` (SPIRV-Tools
v2026.4). Full record with the exact commands, exit codes and output:
`.local/shader-verify/val/spirv-val-results.json`; raw output in
`.local/shader-verify/val/<key>.vulkan1.1.<before|after>.txt`.

| module | stage | before (originals) | after (`spv-fixed/`) |
|---|---|---|---|
| 174293a30c315aed | frag | exit 0 | exit 0 |
| 2ae58c692fb7fbb8 | frag | exit 0 | exit 0 |
| 3fefb8e379a30f64 | frag | exit 0 | exit 0 |
| 9cb65be4f7d0d498 | vert | exit 0 | exit 0 |
| 86affd63144799de | comp | **exit 1** — `error: line 1622: Capability StorageImageWriteWithoutFormat is required to write to storage image` | **exit 0** |
| 41b20b5f09269ad8 | comp | **exit 1** — `error: line 5381: Capability StorageImageWriteWithoutFormat is required to write to storage image` | **exit 0** |

The two failing modules become valid for the Vulkan target env after the
capability word `StorageImageWriteWithoutFormat` (56) is added. The originals
under `.local/shader-verify/spv/` were never modified; the fixed copies are in
`.local/shader-verify/spv-fixed/` and were produced by
`.local/shader-verify/fix_format_caps.py` (the same rule the sibling exporter
uses: walk `OpImageRead`/`OpImageWrite`, resolve the image through `OpLoad` to
its `OpTypeImage`, add the capability only when the image format is Unknown).

## 4. The four tiers on the device

### Tier 1 — device facts: PASS
Instance, physical device and the feature table above. `vkCreateInstance` and
`vkCreateDevice` both returned `VK_SUCCESS`.

### Tier 2 — shader module created: PASS, 24 of 24
`vkCreateShaderModule` returned `VK_SUCCESS` for every module that was handed
to it: the six originals, the six capability-fixed copies, and the two
capability-stripped experiment copies (24 module files in the compute run).
**The driver accepts the two modules that fail `spirv-val`, including the ones
without the capability word.** Module creation is therefore not where the
problem shows up on this device; the driver is more permissive than the
validator.

### Tier 3 — pipeline created: PARTIAL
Layouts are derived from the game's own reflection data
(`.local/shader-verify/spv/<key>.res.json`) and cross-checked against the
SPIR-V decorations: for all six modules every reflection binding is present in
the SPIR-V with the same class, there are no conflicts, and the only binding
the reflection does not list is binding 0, a uniform buffer that the translator
emits in every module. The layout is never empty; the probe refuses to create
a pipeline with an empty layout and reports the tier as not reached instead.

| pipeline | stages | layout | `vkCreate*Pipelines` | draw |
|---|---|---|---|---|
| 86affd63144799de original | comp | 4 bindings, real | `VK_ERROR_INITIALIZATION_FAILED` (-3) | not run |
| 41b20b5f09269ad8 original | comp | 12 bindings, real | `VK_ERROR_INITIALIZATION_FAILED` (-3) | not run |
| 86affd63144799de fixed (cap 56 added) | comp | 4 bindings, real | `VK_ERROR_INITIALIZATION_FAILED` (-3) | not run |
| 41b20b5f09269ad8 fixed (cap 56 added) | comp | 12 bindings, real | `VK_ERROR_INITIALIZATION_FAILED` (-3) | not run |
| 86affd63144799de **without Float64** | comp | 4 bindings, real | **`VK_SUCCESS`** | **consumed** |
| 41b20b5f09269ad8 **without Float64** | comp | 12 bindings, real | **`VK_SUCCESS`** | **consumed** |
| 86affd63144799de without Float64, without cap 56 | comp | 4 bindings, real | `VK_SUCCESS` | consumed |
| 41b20b5f09269ad8 without Float64, without cap 56 | comp | 12 bindings, real | `VK_SUCCESS` | consumed |
| the three frag modules and the vert module | graphics | 3-24 bindings, real | **NOT_RUN**, see below | not run |

Message recorded for the rejected pipelines:
`vkCreateComputePipelines returned VK_ERROR_INITIALIZATION_FAILED (-3)`.

### Tier 4 — draw consumed: PASS for the two compute modules, NOT_RUN for graphics
For the two compute pipelines that were created, the probe allocated a
descriptor set from a pool, bound real buffers, sampled images, samplers and
storage images for every binding of the layout, recorded a dispatch, submitted
it and waited on a fence. `vkQueueSubmit` returned `VK_SUCCESS` and
`vkWaitForFences` returned `VK_SUCCESS`: the GPU consumed the work.
No image content was compared and no frame was produced.

## 5. What actually blocks the two compute modules

The blocker is **not** the storage image format capability. It is the
capability `Float64`, which the translator declares in every module although
no module contains a single 64-bit type (`spirv-dis` finds no `OpTypeFloat 64`
in any of the six).

The device does not support `shaderFloat64` (`feature_support.shaderFloat64 =
false`). A module that declares a capability the device cannot provide is
rejected at pipeline creation with `VK_ERROR_INITIALIZATION_FAILED (-3)`, which
is exactly what the device returns. Removing only that unused declaration —
`.local/shader-verify/strip_caps.py`, capability `Float64` — makes both compute
pipelines succeed and their dispatches get consumed.

Order of the two findings, measured on the device:

1. `Float64` declared (unused) → pipeline creation fails with
   `VK_ERROR_INITIALIZATION_FAILED (-3)`.
2. `Float64` removed → pipeline creation succeeds and the draw is consumed,
   whether or not the `StorageImageWriteWithoutFormat` word is present.

The capability word is still the right thing to add: without it the module is
invalid for the Vulkan target env (`spirv-val` exit 1) even though this driver
happens to accept it. Both changes are needed for a module that is valid
*and* accepted: `spv-fixed/` plus the unused `Float64` declaration removed.

The `FragmentShaderPixelInterlockEXT` capability in the three fragment
modules is also unused (no invocation interlock instruction in any module) and
the device does not offer `VK_EXT_fragment_shader_interlock`; it is the second
thing to clean up in the translator.

## 6. The graphics tier was not reached — the driver crashes

`vkCreateGraphicsPipelines` **crashes the Mali driver** on this device:
`Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x40000000000`.
The probe dies inside the call, so the tier cannot be reported as a result.

This is not the game's shaders. A control run with a hand-written trivial
vertex stage and a trivial fragment stage — no game code at all — crashes at
the same address with the same signal
(`.local/shader-verify/run-trivial-control.exitcode`,
`.local/shader-verify/run-trivial-control.logcat.txt`). The crash also
reproduces with a non-null pipeline cache, with the framebuffer created after
the pipelines, and with the layout stage flags narrowed from
`VK_SHADER_STAGE_ALL` to the pipeline's own stages.

What is still open: the probe's graphics pipeline state has not been compared
against a second implementation, so "the driver crashes for any graphics
pipeline built this way" is what is measured, not "the driver cannot create
graphics pipelines". The compute path of the same probe works, so the instance,
the device and the queue are fine.

Consequence for the port: the first graphics pipeline the runtime creates on
this handheld is expected to take the process down with it. That has to be
settled before any frame can exist.

## 7. Files

New, in this task's write scope:

- `src/graphics/vulkan_probe/vulkan_shader_probe.c` — the standalone probe.
- `scripts/graphics/build_vulkan_shader_probe_android.sh` — the build script.

Evidence, under `.local/shader-verify/`:

- `device-vulkan-facts.json` — the device facts with support and enablement in
  separate fields.
- `device-run-compute.json` — the canonical device run (24 modules, 8 compute
  pipelines, the tier table).
- `device-run-graphics.json` — empty: the driver crashed before the report.
- `run-graphics.exitcode`, `run-graphics.logcat.txt` — the crash evidence.
- `run-trivial-control.exitcode`, `run-trivial-control.logcat.txt` — the
  control that shows the crash is not caused by the game's shaders.
- `run-compute/`, `run-graphics/`, `run-trivial-control/` — build directories
  with the source freeze, the manifest, the layout summary and the build
  evidence (ELF protections, re-checked NDK inputs).
- `val/spirv-val-results.json` and `val/*.vulkan1.1.<before|after>.txt` — the
  validator record.
- `fix_format_caps.py`, `strip_caps.py` — the two module tools.
- `spv/`, `spv-fixed/`, `spv-nof64/`, `spv-nof64orig/`, `spv-nof64fsil/`,
  `spv-trivial/`, `spv-trivialpair/`, `spv-vstest/` — the module variants. The
  originals in `spv/` are untouched.

## 8. What the coordinator's file needs

`src/graphics/nvn_device/nvn_backend_vulkan.c` already does the right thing for
the storage image feature: it asks the device first, turns
`shaderStorageImageWriteWithoutFormat` on only when the device supports it, and
logs support and enablement apart (lines 178-186, 192). The device supports
it, so that enablement is live on this handheld. No change is needed there for
this card.

Two things are needed in the **translator**, not in the runtime:

1. Do not declare `Float64` (and `FragmentShaderPixelInterlockEXT`) when the
   module uses neither. This is what the device rejects today.
2. Declare `StorageImageWriteWithoutFormat` when the module writes to a storage
   image of unknown format, and keep the runtime's conditional feature
   enablement. The format is a property of the bound `NVNtexture`
   (`hle_nvn.c:258-262`), not a shader constant, so it cannot be baked into the
   module; the game's answer table has no `img` format rows at all. Option (A)
   is the only one the game's data supports.

## 9. Not established

- No frame, no title screen, no game rendering is claimed or measured.
- The graphics pipeline tier is not reached; the driver crashes, see section 6.
- The content written by the consumed dispatches was not read back.
- The two compute pipelines that were created were dispatched with zeroed
  uniform buffers and dummy images; the game never ran them.
- The probe measured one driver build on one device; nothing here generalises
  to other Mali parts.

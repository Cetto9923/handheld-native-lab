# OSS-NOTES — pipeline warm-up and the learned pipeline list

Task: rescue-1.0.7-20261112 (Honor GT Pro, `ffresonance-honor-gtpro`).
Scope: the learned pipeline list (`vk-pipeline-list.bin`), the shipped seed entity
(`pipeline-warm-list.bin`) and the Vulkan pipeline cache around them.

Every entry states the source, an immutable revision, the licence, what the source
actually supports, and whether this project retrieved it. Nothing below was copied
as code; the project keeps its own implementation.

---

## 1. `VK_EXT_pipeline_creation_cache_control` — semantics, and what it does not give us

- **Source**: Khronos Vulkan-Docs, `appendices/VK_EXT_pipeline_creation_cache_control.adoc`,
  revision `f7de774cace1509af0f10b8d859c973b5354ff62` (2026-11-12).
  <https://github.com/KhronosGroup/Vulkan-Docs/blob/f7de774cace1509af0f10b8d859c973b5354ff62/appendices/VK_EXT_pipeline_creation_cache_control.adoc>
- **Licence**: CC-BY-4.0 (the file carries `SPDX-License-Identifier: CC-BY-4.0`).
- **Retrieved**: yes — the file was fetched from `raw.githubusercontent.com` at that revision.
- **Registry metadata** (from `xml/vk.xml` at the same revision, also retrieved):
  extension number **298**, type `device`, author AMD, `promotedto="VK_VERSION_1_3"`,
  `VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_SPEC_VERSION = 3`. The registry entry
  carries no `ratified` attribute, so the extension is *promoted to* Vulkan 1.3 but
  not ratified as a ratified extension.

**What the source says** (quoted from the adoc):

> "Pipeline creation is a costly operation, and the explicit nature of the Vulkan design
> means that cost is not hidden from the developer. Applications are also expected to
> schedule, prioritize, and load balance all calls for pipeline creation. It is strongly
> advised that applications create pipelines sufficiently ahead of their usage. Failure to
> do so will result in an unresponsive application, intermittent stuttering, or other poor
> user experiences. Proper usage of pipeline caches and/or derivative pipelines help
> mitigate this but is not assured to eliminate disruption in all cases."

> "Applications can prevent unexpected compilation by setting
> `VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT` on
> `Vk*PipelineCreateInfo::flags`. When set, an ICD must not attempt pipeline or shader
> compilation to create the pipeline object. In such a case, if the implementation fails to
> create a pipeline without compilation, the implementation must: return the result
> `VK_PIPELINE_COMPILE_REQUIRED_EXT` and return `VK_NULL_HANDLE` for the pipeline."

> "The most common case of locks inside the `vkCreate*Pipelines` is internal
> synchronization of the `VkPipelineCache` object.
> `VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT` can be set ... to state the
> cache is externally synchronized."

**Support scope on our target**: the extension is **not available** on the Honor GT Pro.
The device's own capability log lists 33 device extensions; it contains
`VK_EXT_pipeline_creation_feedback`, `VK_KHR_maintenance1..4` and
`VK_KHR_incremental_present`, but **not** `VK_EXT_pipeline_creation_cache_control`
(device log `NVN caps extensions[0] ...`, read on 2026-11-12).

**Consequence for this task**: the "fail instead of compiling" escape hatch is unusable
on the target, so the only way to keep a pipeline creation out of a battle frame is to
*create it ahead of the frame* — which is exactly what the warm-up list does. The
extension's background text is the reason the warm-up exists; its mechanism is not
available to us.

---

## 2. `vkGetPipelineCacheData` / `vkCreatePipelineCache` — the ordering and validity rules

- **Source**: Khronos Vulkan-Docs, `chapters/pipelines.adoc`,
  revision `f7de774cace1509af0f10b8d859c973b5354ff62`.
  <https://github.com/KhronosGroup/Vulkan-Docs/blob/f7de774cace1509af0f10b8d859c973b5354ff62/chapters/pipelines.adoc>
- **Licence**: CC-BY-4.0.
- **Retrieved**: yes — the chapter was fetched at that revision.

**What the source says** (quoted):

> "Any data written to `pData` is valid and can: be provided as the `pInitialData`
> member of the `VkPipelineCacheCreateInfo` structure passed to `vkCreatePipelineCache`."

> "Two calls to `vkGetPipelineCacheData` with the same parameters must: retrieve the same
> data unless a command that modifies the contents of the cache is called between them."

> "The initial bytes written to `pData` must: be a header as described in the
> Pipeline Cache Header section."

> "If `pInitialDataSize` is not `0`, it must: be equal to the size of
> `pInitialData`, as returned by `vkGetPipelineCacheData` when `pInitialData` was
> originally retrieved." (VUID-VkPipelineCacheCreateInfo-initialDataSize-00768)

> "If the pipeline cache data pointed to by `VkPipelineCacheCreateInfo::pInitialData`
> is not compatible with the device, pipeline cache creation will fail and
> `VK_ERROR_INVALID_PIPELINE_CACHE_DATA` will be returned."

**How this project follows them** (`src/graphics/nvn_vulkan/engine/xbg_game.cpp`):

- `load_pipeline_cache()` reads the file, checks the header against
  `properties.vendorID`, `properties.deviceID` and `properties.pipelineCacheUUID`,
  and hands the blob to `vkCreatePipelineCache` as `pInitialData`. A cache from
  another device is dropped rather than passed on (the UUID rule above makes a
  mismatched blob fail with `VK_ERROR_INVALID_PIPELINE_CACHE_DATA`).
- `write_pipeline_cache()` calls `vkGetPipelineCacheData` twice — once for the size,
  once for the bytes — then writes to `*.tmp` and renames, so a torn write cannot
  replace a good cache.
- The ordering is respected at shutdown: `save_pipeline_cache()` starts the saver
  thread, `cache_save_join()` joins it, and only afterwards does the engine call
  `vkDestroyPipelineCache`. The cache object is therefore still alive while its data
  is read, and the destroy happens after the read.

**What this source does not say**: it does not promise that a cache helps on any
particular driver, and it does not promise that a cache from one device works on
another. The project's own device check (UUID + vendor/device ID) is the honest
version of that promise.

---

## 3. The community pattern this project implements: record the pipelines, then warm them up

- **Source**: Khronos Vulkan-Samples, `samples/performance/pipeline_cache/README.adoc`,
  revision `56a72bd1c43268781a75d73a520e4458b6cac766` (main).
  <https://github.com/KhronosGroup/Vulkan-Samples/blob/56a72bd1c43268781a75d73a520e4458b6cac766/samples/performance/pipeline_cache/README.adoc>
- **Licence**: Apache-2.0 (repository `LICENSE`; the sample is "Copyright (c) 2019-2023,
  Arm Limited and Contributors", `SPDX-License-Identifier: Apache-2.0`).
- **Retrieved**: yes — the README was fetched from `raw.githubusercontent.com`.

**What the source says**:

> "Vulkan allows applications to get the binary data of a `VkPipelineCache` object and
> save it to a file before the application terminates. This can be done using two calls
> to the `vkGetPipelineCacheData` function, one to get the size and one to get the
> binary data of the `VkPipelineCache` object. On the next run of the application, the
> data from the last run can be used to initialize the `VkPipelineCache`. This will
> allow `vkCreateGraphicsPipelines` or `vkCreateComputePipelines` to re-use the baked
> state and avoid the expensive operation of shader compilation."

> "Resource cache warm-up: Because graphics pipelines need information from the render
> pass, render state, mesh data and shaders, it is harder for a game engine to prepare
> Vulkan pipelines ahead of time, because rendering is driven by game logic. ... This can
> also be implemented in a game engine by recording the pipelines created during the
> game run and then using that information to warm up the internal resource cache at
> start-up."

> "Best practice summary — DO: Create known pipelines early in the application's
> execution (using data from between application runs). Use pipeline caches to reduce
> pipeline creation cost. DON'T: Create pipelines on draw without a pipeline cache
> (this introduces performance hitches)."

The sample also reports a device measurement (Mali G76): with the pipeline cache,
re-creating the pipelines took 24.4 ms; without it, 50.4 ms.

**Relation to this project**: the project implements both halves of that pattern —
the driver cache (`vk-pipeline-cache.bin`) *and* a recipe list
(`vk-pipeline-list.bin`) that records the exact create-info of every pipeline the
device had to build, so background threads can rebuild those pipelines before a frame
needs them. The recipe list is the project's answer to the sample's "rendering is
driven by game logic" problem, and the reason a **seed** entity can be shipped with
the game data at all.

**Not taken from the source**: no code was copied. The sample is a reference for the
design and for the "create ahead of the frame" rule.

---

## 4. The "Maleoon driver does not grow the VkPipelineCache" comment — where it comes from and whether it applies here

The comment in `xbg_game.cpp` ("Pipeline warm-up (the "precompiled shader cache" of
console emulators). The Maleoon driver does not grow the VkPipelineCache, so every
launch recompiled each graphics pipeline on first use, inside the frame (~100+ ms each).")
is **upstream history**, not a property of this project's targets.

- **Source**: `d2r-mobile/d2r-mobile`, `dm/gfx/vulkan/engine/xbg_game.cpp`, revision
  `a8782caa28d7b49d1b341e34de488d3168acb86c` — the comment is at line 2021 of that file.
  <https://github.com/d2r-mobile/d2r-mobile/blob/a8782caa28d7b49d1b341e34de488d3168acb86c/dm/gfx/vulkan/engine/xbg_game.cpp>
- **Licence**: MIT, © 2026 jhaizhou (as recorded in this repository's `NOTICE.md`).
- **Retrieved**: yes — the raw file was fetched at that revision (467200 bytes) and the
  comment was located in it.
- **What "Maleoon" is, as far as the sources go**: the same upstream tree calls it "the
  Maleoon GPU" (`dm/gfx/vulkan/engine/xbg_texture.c`: "the Maleoon GPU has no BC
  (textureCompressionBC=0)") and "the Maleoon driver". It is the console GPU and driver
  the upstream project targets, i.e. the game's own console driver — **not** the Adreno
  of the Honor GT Pro and **not** the Mali of the RG477M. No external source was found
  that names the driver more precisely, so this project does not claim more than the
  upstream files do.
- **Does it apply to our targets? No — and the devices say so directly.**
  - Honor GT Pro: the engine wrote `vk-pipeline-cache.bin` and read it back at
    `NVN pipeline cache load bytes=30130629` (30 MB) on 2026-11-12. The cache **does**
    grow on this device.
  - RG477M (Mali-G615 MC6): not measured in this task. **Not verified here.**
- **Consequence**: the comment stays as upstream history; it is not a reason to skip the
  VkPipelineCache on our targets, and it is not evidence about Mali.

---

## 5. The seed entity and "seed hit = free"

There is no OSS source for a shipped seed entity; the design follows §3's "create
known pipelines early … using data from between application runs". What the project's
own code shows, and what the device confirmed:

- A **recipe** is the shader bundle keys plus the exact serialized create-info key
  (`xbg_pipeline_key.h`). `warm::record()` stores that key; `warm::take()` looks it up
  by the same bytes.
- A seed hit therefore means: *the recipe is known and the background thread has
  already built the pipeline*. The pipeline is still **created once** — by the warm-up
  thread, not by the draw. If the draw arrives before the background build finishes,
  `warm::take()` misses and the draw creates the pipeline itself. A seed hit is
  therefore **not free**: it moves the cost out of the frame, it does not remove it.
- The cost of that move is the warm-up's own driver work. Measured on the Honor device:
  `warm_built=1560 warm_ms=7010` — seven seconds of background pipeline creation.
  Whether that is a win depends entirely on whether the pipelines are needed in the
  frames that follow, and on how the warm-up competes with the submit thread. That is a
  measured question on this device, not a claim.

---

## 6. Sources not used, and why

- Blog posts and aggregator pages about "Android pipeline cache" surfaced in search
  (CSDN, DevPress and similar) were **not** used: they are not primary sources and
  could not be pinned to a revision. The Khronos sample (§3) and the spec (§1, §2)
  cover the same ground and are pinnable.
- No external code was copied into this project for this task.

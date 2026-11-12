# OSS-NOTES — the map transition: shader pre-compilation, pipeline caches, scene loading

Task: BIGMAP-LOAD-20261112 (the Honor GT Pro line, `ffresonance-honor-gtpro`, branch
`task/T-release-1.0.9-20261112`).
Scope: where the time of one map transition goes, and the community practice behind the
levers this line measured (shader pre-compilation, pipeline / PSO caches, scene loading).

Every entry states the source, an immutable revision, the licence, what the source actually
supports, and whether this project retrieved it. **Nothing below was copied as code**; the
project keeps its own implementation. This line changed no rendering, waiting or
warm/pipeline-persistence code — it added a marker-gated measurement only, so no upstream
code was imported at all.

---

## 1. Vulkan pipeline cache — the persistence the engine already does

- **Source**: Khronos Vulkan-Docs, `chapters/pipelines.adoc` (rendered as the
  `vkCreatePipelineCache` reference page), revision
  `f7de774cace1509af0f10b8d859c973b5354ff62` (2026-11-12T02:22:53Z, the `main` HEAD when
  this line fetched it).
  <https://www.khronos.org/vulkan/specs/latest/man/html/vkCreatePipelineCache.html>
  <https://github.com/KhronosGroup/Vulkan-Docs/tree/f7de774cace1509af0f10b8d859c973b5354ff62>
- **Licence**: CC-BY-4.0 for the adoc sources (the per-file `SPDX-License-Identifier:
  CC-BY-4.0` header, as already recorded in this project's root `OSS-NOTES.md` for the same
  repository and revision). This session's attempts at the repository-level `LICENSE` /
  `LICENSE.md` / `LICENSE.txt` / `COPYING.md` all returned **404**, so no repository-level
  licence file was read here; the per-file SPDX header is the cited fact.
- **Retrieved**: **yes** — the reference page was fetched (HTTP 200) and the quoted sentence
  is from the fetched body.

**What the source says** (quoted from the fetched page):

> "Once created, a pipeline cache can be passed to the `vkCreateGraphicsPipelines`,
> `vkCreateRayTracingPipelinesKHR`, `vkCreateRayTracingPipelinesNV`,
> `vkCreateDataGraphPipelinesARM`, and `vkCreateComputePipelines` commands. If the pipeline
> cache passed into these commands is not `VK_NULL_HANDLE`, the implementation will query it
> for possible reuse opportunities and update it with new content."

**What it actually supports for us**: the driver-side pipeline cache, with the data written
and read back by the host. The engine already does this (`vk-pipeline-cache-final.bin`,
30,130,629 B in the 1.0.7 rescue capture, see §7 of this file for the numbers that decide
whether it is worth more).

**What it does not cover**: the pipeline *recipes* the engine learns (the warm list). That is
this project's own extension and is documented in the root `OSS-NOTES.md`, not here.

## 2. `VK_KHR_pipeline_binary` — the community answer to "the first entry compiles everything"

- **Source**: Khronos Vulkan-Docs, `appendices/VK_KHR_pipeline_binary.adoc`, revision
  `f7de774cace1509af0f10b8d859c973b5354ff62` (same revision as above).
  <https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_pipeline_binary.html>
- **Licence**: CC-BY-4.0 (per-file SPDX header, as recorded in the root `OSS-NOTES.md` for
  this repository).
- **Retrieved**: **partly** — the rendered reference page was fetched (HTTP 200, title
  "VK_KHR_pipeline_binary(3) :: Vulkan Documentation Project"), but the body is rendered by
  Antora's JavaScript, so the static fetch returned only the title and the navigation. **No
  sentence of the extension text is quoted here on purpose.**
- **What it actually supports**: pipeline binaries kept on disk by the application, i.e. the
  device-independent form of "do not compile on the first entry". **Not verified on our
  devices** — whether the Mali and Adreno drivers we run on expose this extension is
  unmeasured, and this line touched no device.
- **Used by this line**: **no**. It is recorded as the direction to evaluate *after* the
  measurement below says the lever is worth pulling.
## 3. glslang — compiling shaders ahead of time instead of at run time

- **Source**: KhronosGroup/glslang, `README.md` at revision
  `ff6ad3c77a2cc2fdea329b4a1d5425c82bc56a1f` (the `main` HEAD when this line fetched it,
  2026-11-12).
  <https://github.com/KhronosGroup/glslang/blob/ff6ad3c77a2cc2fdea329b4a1d5425c82bc56a1f/README.md>
- **Licence**: the repository's `LICENSE.txt` was fetched (HTTP 200) and states that
  "glslang proper means core GLSL parsing, HLSL parsing, and SPIR-V code generation" and that
  "Glslang proper requires use of a number of licenses, one that covers preprocessing and
  others that covers non-pr[eprocessing]". The per-component SPDX identifiers are further down
  the file and were **not read line by line in this session**, so no per-component licence is
  asserted here.
- **Retrieved**: **yes** — the README at that revision was fetched (HTTP 200).

**What the source says** (quoted from the fetched README):

> "### AST -> SPIR-V Back End
> 
> Translates glslang's AST to the Khronos-specified SPIR-V intermediate language."

**What it actually supports for us**: the *practice* of compiling a shader to a binary ahead of
  time instead of at run time. The project already does exactly this with its own format and
  its own tool (`scripts/graphics/build_ffrs_xgs.py` turns the guest's shader bundles into
  the `.xgs` binaries the engine loads), so **no upstream code or tool is used** and the
  measured cost of the remaining run-time work is the 3446 ms per launch recorded in the
  result file.

## 4. Android Frame Pacing library — the 30 FPS target this port declares

- **Source**: Android Developers, "Frame Pacing library",
  <https://developer.android.com/games/optimize/frame-pacing>.
- **Immutable revision**: none published on the page; the retrieval is recorded by its date,
  **2026-11-12** (this session).
- **Licence**: not stated on the page in the fetched part; **not asserted** here.
- **Retrieved**: **partly** — the page was fetched (HTTP 200, title "Frame Pacing library |
  Android game development"), but the body is rendered client-side, so the static fetch
  returned the breadcrumb and navigation only. **No sentence is quoted on purpose.**
- **What it actually supports for us**: the platform-side frame pacing for the declared 30 FPS
  target on the Honor tablet. This line changed nothing of it.
- **Used by this line**: **no**.

## 5. Dawn — checked, nothing to cite

- **Source**: <https://api.github.com/repos/google/dawn/commits/main>, revision
  `85b7901759827252adf542adf7a1d2858515b12e` (2026-11-12T14:10:51Z).
- **Retrieved**: yes — the repository tree of `docs/dawn/features` was listed and contains
  **no** pipeline-cache document (the names matching "cache" are empty), so there was nothing
  to quote.
- **Used by this line**: **no**.

## 6. Scene loading — the practice we cannot use, and why

Community practice for a big map is to stream the scene's resources asynchronously and to
pre-build the pipelines of the next area before the player arrives. **Neither is available to
this port as a code change**: the guest game is our own unmodified binary and the asset
loader lives in it, so no upstream project can be "borrowed" for it. What the host side can
do is limited to the graphics path (persistent pipeline cache, persistent pipeline recipes,
run-time shader reuse), which is what §1–§3 cover. **No source is cited for the guest-side
practice, because this project does not implement it.**

## 7. What the measurement says about the value of these levers

From the 1.0.7 Honor run analysed in `BIGMAP-LOAD-20261112.md` (not from any upstream text):

- One session loaded **491 shaders, 491 distinct identities, `reused=0`, 3446 ms total**;
  137 of those loads were slow (≥ 5 ms) and cost 2704 ms.
- The graph path's own compile work (shaders + pipeline creations) is **0.9%–26.5%** of the
  excess wall time of a transition; in the two largest transitions it is **2.6%** and **1.9%**.
- The largest engine-side cost of a transition is the CPU time inside `vkQueueSubmit`
  (**32.7–37.7 ms/frame** against a steady-state **10.5 ms/frame**), while the GPU is far
  from saturated (**5.9–25.8 ms/frame** against a 33.3 ms budget).

**Consequence for this line**: a shader pre-compilation or PSO-cache line of work (upstream
  practice per §1–§3) can only ever address the 2%–26% slice; the remaining 74%–97% of a
  transition is the guest's own scene load plus the submit path. The persistent pipeline
  recipe work (the A class defect, already fixed in 1.0.9) addresses the `pipelines_created`
  part of that slice. Anything beyond that needs either the submit path or the guest, and this
  line deliberately stayed out of both (the submit path is the transition line's write scope;
  the guest is out of scope by rule).

---

**Summary of retrieval**: Vulkan pipeline cache — retrieved (quoted); `VK_KHR_pipeline_binary` —
page retrieved, body not quotable; glslang — retrieved (quoted, licence file read, per-component
SPDX not read); Android frame pacing — page retrieved, body not quotable; Dawn — retrieved,
nothing relevant. **No upstream code was copied into this project by this line.**

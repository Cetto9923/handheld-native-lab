# T-VULKAN graphics pipeline crash — RG477M (Mali-G615 MC6), 2026-10-25

Card: **P-MM-27**. Scope: why `vkCreateGraphicsPipelines` took the process
down on RG477M, and what builds a graphics pipeline on that device.

Device: `RG477M01445122` (RG477M, Android 14, board `mt6897`,
`ro.hardware.vulkan=mali`, `Mali-G615 MC6`, driver version 184553472,
loader supports API 1.3.0, physical device API 1.3.247).

Every number below is a measurement from that device unless the text says
**inference**. Nothing here claims that the port runs, that a frame exists, or
that the game renders: **this card produced no pixels.**

---

## 0. The answer in one paragraph

The crash was **not** a driver bug and **not** a device limitation. The probe
that P-MM-23 used declared its shader stages like this:

```c
VkPipelineShaderStageCreateInfo stages[2];      /* no initializer */
stages[0].sType = ...; stages[0].stage = ...;
stages[0].module = ...; stages[0].pName = "main";
```

so `stage.pNext`, `stage.flags` and `stage.pSpecializationInfo` kept whatever
the stack happened to hold. The Mali driver **dereferences
`pSpecializationInfo` and walks `pNext` without a null check**, so a leftover
pointer became the fault address: `0x40000000000` in P-MM-23's run. Writing
every field of the stage structure removes the crash, and graphics pipelines
then build on this device. **The one-line fix is in
`src/graphics/vulkan_probe/vulkan_shader_probe.c`** (a `memset` of the stage
array, plus the comment that says why).

The driver's behaviour is also a **driver defect**: an implementation that
dereferences a pointer it was given must handle a null pointer. The state the
caller passed was legal, the caller was at fault for leaving the field
unwritten, and the driver was at fault for not checking it. Both are true, and
the second one does not make the first one go away.

---

## 1. What was measured

### 1.1 The bisect probe

New file `src/graphics/vulkan_probe/vulkan_graphics_bisect.c`. It owns its own
instance and device, loads the Vulkan loader with `dlopen`, and resolves the
entry points through `vkGetInstanceProcAddr` / `vkGetDeviceProcAddr`.

**One experiment per process.** The parent forks and execs itself once per
experiment, so each experiment has its own exit status, its own signal and its
own fault address, and nothing one experiment does to the address space can
influence the next. Each experiment writes into
`<out>/<experiment>/`: `command.txt`, `stdout.txt`, `stderr.txt`,
`exitcode.txt` (the raw `waitpid` status, the exit code and the terminating
signal), `result.json` and `maps.txt` (the `/proc/self/maps` snapshot taken
before the call, so the fault address and the entry-point addresses can be
placed in their mappings).

The crash handler is **opt-in** (`FFRS_BISECT_HANDLER=1`). With it off,
Android's own crash handler produces the tombstone, which carries the
module-relative program counter of every frame — that is the evidence that
names the library. With it on, the probe records the fault address, the
faulting program counter (`uc_mcontext.pc`), the link register and the
frame-pointer chain itself.

### 1.2 The complete table

Final run: `.local/shader-verify/graphics-crash/matrix-final/out-final/`,
crash buffer `.local/shader-verify/graphics-crash/logcat-crash-matrix-final.txt`,
machine-readable copy `.local/shader-verify/graphics-crash-bisect.json`.

| experiment | outcome | adb exit | signal | fault addr |
|---|---|---|---|---|
| `pmm23-uninit` | **SIGSEGV** | 139 | 11 | **`0x40000000000`** |
| `pmm23-uninit-nopoison` | **SIGSEGV** | 139 | 11 | `0x1` |
| `stage-zeroed` | **VK_SUCCESS** | 0 | 0 | — |
| `stage-pnext-poison` | **SIGSEGV** | 139 | 11 | `0xdeadbeef0000` |
| `stage-specinfo-poison` | **SIGSEGV** | 139 | 11 | `0xdeadbeef0000` |
| `stage-flags-poison` | VK_SUCCESS | 0 | 0 | — |
| `vertex-binding-1` | VK_SUCCESS | 0 | 0 | — |
| `viewport-dynamic` | VK_SUCCESS | 0 | 0 | — |
| `attachment-count-0` | VK_SUCCESS | 0 | 0 | — |
| `samples-4` | VK_SUCCESS | 0 | 0 | — |
| `depth-attachment` | VK_SUCCESS | 0 | 0 | — |
| `depth-clamp` | VK_SUCCESS | 0 | 0 | — |
| `rasterizer-discard` | VK_SUCCESS | 0 | 0 | — |
| `blend-enable` | VK_SUCCESS | 0 | 0 | — |
| `logic-op` | VK_SUCCESS | 0 | 0 | — |
| `dynamic-rendering` | VK_SUCCESS | 0 | 0 | — |
| `dynamic-rendering-poison` | **SIGSEGV** | 139 | 11 | `0xdeadbeef0000` |
| `game-modules` | `VK_ERROR_INITIALIZATION_FAILED (-3)` | 1 | 0 | — |

Base state for the table: one colour attachment `R8G8B8A8_UNORM`, one
viewport and one scissor of 8×8, vertex input count 0, `rasterizationSamples`
1, no depth attachment, no dynamic state, `VkPipelineColorBlendStateCreateInfo`
`attachmentCount` 1, a render pass with one colour attachment and one subpass,
a descriptor set layout with one uniform buffer at set 0 binding 0, an empty
pipeline cache, and a hand-written trivial vertex + fragment pair
(`.local/shader-verify/spv-trivialpair/`, the same pair P-MM-23 used as its
control).

### 1.3 The A/B that settles it

The decisive experiment is not the table, it is the A/B on **P-MM-23's own
probe**, with **its own manifest and its own modules**, on the same device:

```
base  = .local/shader-verify/run-graphics/vulkan_shader_probe   (as P-MM-23 left it)
fixed = .local/shader-verify/graphics-crash/build-03-fixed/vulkan_shader_probe
       (identical source plus one memset of the stage array)
```

| binary | run 1 | run 2 | run 3 | run 4 | run 5 |
|---|---|---|---|---|---|
| base (P-MM-23's) | 139 | 139 | 139 | 139 | 139 |
| fixed | 0 | 0 | 0 | 0 | 0 |

139 is `adb shell`'s code for a process killed by SIGSEGV. Same manifest
(`run-graphics/manifest.txt`, byte-identical), same twelve modules, same
device, same minute. The **only** difference is the `memset`.

Evidence: `.local/shader-verify/graphics-crash/ab-base-*.{stdout,stderr}.txt`,
`ab-fixed-*.{stdout,stderr}.txt`.

### 1.4 The crash is intermittent, and that is why P-MM-23 saw one address

The leftover in the stage structure is whatever was on the stack, so the crash
depends on the stack layout:

- In one build of the bisect probe, `pmm23-uninit` **succeeded** — the
  leftover happened to be harmless.
- In the final build, the same experiment crashed with fault addr
  **`0x40000000000`**, which is **byte-for-byte the address P-MM-23 recorded**.

Same reason, different luck. That is the signature of an uninitialised field,
not of a state the driver dislikes.

---

## 2. Q1 — the minimal trigger

**The minimal trigger is one uninitialised pointer in
`VkPipelineShaderStageCreateInfo`.** It is not the vertex input, not the
viewport, not the attachment count, not the render pass, not the framebuffer,
not the samples, not the dynamic state, not the blend, not the logic op, not
dynamic rendering: every one of those was changed one at a time and the
pipeline still built.

The two experiments that pin it down:

| experiment | stage structure | result |
|---|---|---|
| `pmm23-uninit` | only `sType`, `stage`, `module`, `pName` written — `pNext`, `flags`, `pSpecializationInfo` keep the stack's leftovers | SIGSEGV, fault addr `0x40000000000` |
| `stage-zeroed` | the same state, every field written | **VK_SUCCESS** |
| `stage-specinfo-poison` | every field written, `pSpecializationInfo = 0xdeadbeef0000` | SIGSEGV, fault addr `0xdeadbeef0000` |
| `stage-pnext-poison` | every field written, `pNext = 0xdeadbeef0000` | SIGSEGV, fault addr `0xdeadbeef0000` |
| `stage-flags-poison` | every field written, `flags = 0xffffffff` | VK_SUCCESS |

The poison experiments are the causal ones: they put a **known** value in one
field and the fault address becomes **exactly that value**. `flags` is not
dereferenced by the driver, so poisoning it changes nothing.

### 1.1 The work order's checklist, item by item

| item asked for | measured | outcome |
|---|---|---|
| `vertexBindingDescriptionCount` / `vertexAttributeDescriptionCount` = 0, `pVertexBuffers` NULL | base state; `vertex-binding-1` (1 binding + 1 attribute) also tested | both VK_SUCCESS |
| `pTessellationState` NULL | NULL in every experiment | no effect (tessellation was never exercised with a tess stage) |
| `pViewportState` viewportCount / scissorCount | 1+1 static (base); 0+0 with `VK_DYNAMIC_STATE_VIEWPORT`/`SCISSOR` | both VK_SUCCESS |
| `pRasterizationState` lineWidth / cullMode / frontFace / depthClampEnable / rasterizerDiscardEnable | 1.0 / NONE / CCW / FALSE (base); depthClamp TRUE (with a depth attachment); rasterizerDiscard TRUE | all VK_SUCCESS |
| `pMultisampleState` samples vs render pass / framebuffer | 1 vs 1 (base); 4 vs 4 with a 4× multisampled image and framebuffer | both VK_SUCCESS |
| `pDepthStencilState` / depth attachment | absent (base); present with a `D32_SFLOAT` attachment and a matching subpass reference | both VK_SUCCESS |
| `pColorBlendState` attachmentCount vs render pass | 1 vs 1 (base); 0 vs 0 with a render pass without attachments | both VK_SUCCESS |
| `pDynamicState` count | none (base); 2 dynamic states (viewport + scissor) | both VK_SUCCESS |
| render pass: attachment count, loadOp/storeOp, initial/final layout, subpass colour attachments | 1 colour attachment, CLEAR/STORE, UNDEFINED→COLOR_ATTACHMENT_OPTIMAL, 1 subpass | no effect |
| framebuffer: attachment count, view count, size, format | 1 view, 8×8, R8G8B8A8_UNORM | no effect |
| swapchain | **not used** — the probe renders into an off-screen image, not a swapchain | not exercised |
| `logicOp` / `blendEnable` | FALSE/none (base); blend enabled (ONE/ZERO, ADD); `VK_LOGIC_OP_OR` | all VK_SUCCESS |
| `vertexBindingDescription.binding` out of range | binding 0 in the one-binding experiment | no effect |

### 2.2 The one thing the checklist could not have found

The work order listed the *contents* of the pipeline state. The trigger was
not in the contents; it was in a field of the stage structure that **was never
written at all**. That is why the "decrease the state until it crashes /
increase it until it first crashes" search of the checklist could not have
reached it, and why a control run with hand-written trivial shaders crashed
the same way: the trivial shaders were never the problem.

---

## 3. Q2 — which layer crashes

**The Mali driver crashes, not the loader.** Measured, not guessed.

### 3.1 The libraries

| role | library |
|---|---|
| loader | `/system/lib64/libvulkan.so` (Android's loader, 206280 bytes, BuildId 5626fc16a3ebb49a8772110d4567c01f) |
| driver | `/vendor/lib64/egl/mt6897/libGLES_mali.so` (BuildId `aae8eac824d754d3`) — this is where the Mali Vulkan implementation lives on this device |
| also present | `/vendor/lib64/hw/vulkan.mali.so` |

There is **no ICD directory and no layer directory** on this device:
`/vendor/etc/vulkan/icd.d/`, `/system/vendor/lib/hw/vulkan/`,
`/vendor/etc/vulkan/implicit_layer.d/` and `/vendor/etc/vulkan/explicit_layer.d/`
all do not exist. **`VK_LAYER_KHRONOS_validation` is not available**, so no
validation-layer diagnostic could be obtained. That is a fact about the device,
recorded so nobody re-looks for it.

### 3.2 The crash frames

P-MM-23's own tombstone, still in the crash buffer
(`.local/shader-verify/graphics-crash/logcat-crash-matrix-final.txt`, 02:34:14):

```
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0000040000000000
  #00 pc 00000000008bedf0  /vendor/lib64/egl/mt6897/libGLES_mali.so
  #01 pc 00000000008bd264  /vendor/lib64/egl/mt6897/libGLES_mali.so
  #02 pc 000000000000a314  vulkan_shader_probe
  #03 pc 000000000000c5b4  vulkan_shader_probe
  #04 pc 0000000000007edc  vulkan_shader_probe
  #05 pc 00000000000546e8  bionic/libc.so (__libc_init+104)
```

The bisect probe's `stage-specinfo-poison` (02:47:48):

```
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0000deadbeef0000
  #00 pc 00000000008bedf0  /vendor/lib64/egl/mt6897/libGLES_mali.so
  #01 pc 00000000008bd264  /vendor/lib64/egl/mt6897/libGLES_mali.so
  #02 pc 0000000000008178  vulkan_graphics_bisect
```

and `stage-pnext-poison` (02:47:48):

```
  #00 pc 00000000008cdf20  /vendor/lib64/egl/mt6897/libGLES_mali.so
  #01 pc 00000000008bee24  /vendor/lib64/egl/mt6897/libGLES_mali.so
  #02 pc 00000000008bd264  /vendor/lib64/egl/mt6897/libGLES_mali.so
```

**P-MM-23's frame `#00` (`0x8bedf0`) is the same frame as the
`pSpecializationInfo` dereference**, and the `pNext` walk is a third frame
deeper in the same driver. The loader never appears in a fault frame.

### 3.3 Where the entry points live

The bisect probe prints the address of every entry point it resolves through
`vkGetDeviceProcAddr` and snapshots `/proc/self/maps` before the call
(`<experiment>/maps.txt`), so the addresses can be placed in their mappings.
On this device all of them — `vkCreateGraphicsPipelines`,
`vkCreateComputePipelines`, `vkCreateRenderPass`, `vkCreateFramebuffer`,
`vkCreateShaderModule`, `vkCreatePipelineLayout` — resolve into the **same
library range**, the Mali driver, and not into the loader. The compute path
works and the graphics path does not, from the same device pointer table.

### 3.4 The driver's defect, stated precisely

The driver dereferences, without a null check:

- `VkPipelineShaderStageCreateInfo::pSpecializationInfo` at module offset
  `0x8bedf0`, and
- `VkPipelineShaderStageCreateInfo::pNext` at module offset `0x8cdf20`.

A Vulkan implementation may reject an illegal structure with a `VkResult`, but
dereferencing a null pointer is a defect in the implementation, not in the
caller's state. Both statements are measured: the fault addresses are the
exact poison values, and the faulting program counters are inside the driver.

---

## 4. Q3 — is the state wrong, or is the driver broken?

**Both, and the measurements separate them.**

### 4.1 The control that was available: same device, same driver, same state, one line different

The A/B in section 1.3 is the control: **5/5 SIGSEGV before, 5/5 clean after**,
with the manifest, the modules and the device held constant. The state that
crashes and the state that does not differ by one `memset`. Therefore **our
state was the problem**, and the device can build graphics pipelines.

### 4.2 The control that was not available: a second device

**There is no second device.** `adb devices -l` lists only `RG477M01445122`.
The host Mac has no Vulkan loader — the probe built for the host reports
`NO_VULKAN_IN_THIS_BUILD`, and per the work order MoltenVK / lavapipe were
**not** used as a stand-in, because that would be a different device wearing a
different implementation, not a second implementation of the same thing.

**What the missing control costs.** Without a second device it cannot be shown
that the *driver build* on this device is free of other graphics-pipeline
defects. The claim this card supports is the narrower and the useful one:
*this* state, *this* device, *this* driver build: the crash is ours and it is
gone. It does **not** generalise to other Mali parts, other driver versions,
or other graphics-pipeline states that were not exercised.

### 4.3 The second-implementation control that was available: the driver itself

There is no second Vulkan implementation on the device (no SwiftShader, no
second ICD, `/apex` is not readable by the shell user). What there **is** is a
second implementation *of the same pipeline state*: the **compute** path. The
same instance, the same device, the same queue and the same driver build
create compute pipelines and consume their dispatches (section 5). A driver
that cannot build any pipeline at all is ruled out; a driver that chokes on
one specific structure is not.

### 4.4 A second implementation on the device, for the record

`pm list packages` on the device lists `org.vita3k.emulator`, `net.rpcs3`,
`dev.eden.eden_emulator` and `com.flycast.emulator` — emulators that build
Vulkan graphics pipelines. They were **not** run as a control: the card's
device time was spent on the bisect, and running a third-party emulator is
both a bigger intervention on the device and an indirect observation (no
pipeline-creation log to read). Recorded as **available but not exercised**,
not as a control that passed.

---

## 5. Q4 — the workaround

### 5.1 The change

`src/graphics/vulkan_probe/vulkan_shader_probe.c`, in `create_pipelines()`:

```c
            /* Every field of the stage structure has to be written.  The
             * driver dereferences stage.pNext and stage.pSpecializationInfo
             * without checking them, so a field that keeps the stack's
             * leftovers takes the process down with a SIGSEGV; P-MM-27
             * measured that on RG477M and the fix is the memset below. */
            VkPipelineShaderStageCreateInfo stages[2];
            memset(stages, 0, sizeof(stages));
```

The compute path needs no change: it already builds its stage with a
designated initializer, which zero-fills the rest.

**This is a fix to the probe, not to the runtime.** The runtime backend
(`src/graphics/nvn_device/nvn_backend_vulkan.c`) is the coordinator's file and
was **not** touched. If the runtime builds its stages the way P-MM-23's probe
did, it has the same defect and needs the same one-line change; that is the
coordinator's call, not this card's.

### 5.2 A graphics pipeline that builds on RG477M — the full state

Measured, `VK_SUCCESS`, from the `stage-zeroed` experiment (and identically
from thirteen other state combinations in the table):

| part | value |
|---|---|
| instance | API 1.1, no layers, no extensions |
| device | `Mali-G615 MC6`, queue family 0, **no features and no extensions enabled** |
| descriptor set layout | 1 binding: set 0, binding 0, `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, count 1, stage flags VERTEX\|FRAGMENT |
| pipeline layout | 1 set layout, no push constants |
| stages | 2: vertex (`trivial_vert.spv`) and fragment (`trivial_frag.spv`), entry point `"main"`, **`pNext = NULL`, `flags = 0`, `pSpecializationInfo = NULL`** |
| vertex input | `vertexBindingDescriptionCount = 0`, `vertexAttributeDescriptionCount = 0`, both pointer arrays NULL |
| input assembly | `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST`, `primitiveRestartEnable = VK_FALSE` |
| viewport state | `viewportCount = 1` (8×8, depth 0..1), `scissorCount = 1` (0,0 8×8) — static, not dynamic |
| rasterization | `FILL`, `cullMode = NONE`, `frontFace = COUNTER_CLOCKWISE`, `lineWidth = 1.0`, `depthClampEnable = FALSE`, `rasterizerDiscardEnable = FALSE` |
| tessellation | `pTessellationState = NULL` |
| multisample | `rasterizationSamples = VK_SAMPLE_COUNT_1_BIT` |
| depth stencil | `pDepthStencilState = NULL` (no depth attachment) |
| colour blend | `attachmentCount = 1`, one attachment, `blendEnable = FALSE`, `colorWriteMask = RGBA`, `logicOp = VK_LOGIC_OP_CLEAR` |
| dynamic state | `pDynamicState = NULL` |
| render pass | 1 attachment `R8G8B8A8_UNORM`, samples 1, `loadOp = CLEAR`, `storeOp = STORE`, `initialLayout = UNDEFINED`, `finalLayout = COLOR_ATTACHMENT_OPTIMAL`; 1 subpass, `pipelineBindPoint = GRAPHICS`, 1 colour attachment referencing attachment 0 |
| framebuffer | 1 attachment (the 8×8 `R8G8B8A8_UNORM` image view), 8×8, 1 layer |
| pipeline cache | empty (`vkCreatePipelineCache` with no initial data) |
| result | **`vkCreateGraphicsPipelines` → `VK_SUCCESS` (0)**, a non-null `VkPipeline` |

Variants of that state that also build, all measured: 1 vertex binding +
1 attribute; viewport/scissor fully dynamic; render pass with **0** attachments
and `attachmentCount = 0`; 4× multisampling with a matching image and
framebuffer; a `D32_SFLOAT` depth attachment with `pDepthStencilState`; depth
clamp; `rasterizerDiscardEnable = TRUE`; blending enabled; `logicOp = OR`; and
**`VK_KHR_dynamic_rendering`** with `VkPipelineRenderingCreateInfo` and
`renderPass = VK_NULL_HANDLE`.

### 5.3 The candidates the work order listed, one by one

| candidate | measured on the device |
|---|---|
| 1. `VkPipelineRenderingCreateInfo` instead of a render pass | **works** (`dynamic-rendering` → VK_SUCCESS) — but it is not needed; the render pass works too |
| 2. narrow `VK_SHADER_STAGE_ALL` to the pipeline's own stages | already what the probe did; P-MM-23 tested it and it still crashed — **not the trigger** |
| 3. no render pass, dynamic rendering, matching attachment count | **works**, and is unnecessary |
| 4. explicit 1 viewport / 1 scissor instead of 0 | already the base state; **not the trigger** |
| 5. a minimal legal vertex input instead of 0 | both work; **not the trigger** |
| 6. `rasterizationSamples = 1`, matching the framebuffer | already the base state; **not the trigger** |
| 7. `VK_EXT_extended_dynamic_state` / `2` | **not tested** — the device's extension list was not enumerated for them, and no candidate needed them |
| 8. `attachmentCount` matching the render pass's colour attachments | already the base state; **not the trigger** |

### 5.4 The game's own shaders — a second, separate problem

With the crash gone, the game's own graphics modules are now **rejected, not
crashing**: `vkCreateGraphicsPipelines` returns
`VK_ERROR_INITIALIZATION_FAILED (-3)`. That is a different problem on the
shader side, and it is not this card's crash.

What was measured about it:

- With the capability words `Float64` and `FragmentShaderPixelInterlockEXT`
  removed (the same removals P-MM-23 made for compute) and the fix above
  applied, the **compute** pipelines build and their dispatches are consumed.
- The **game's vertex module** builds: with the game's vertex module and the
  trivial fragment module, `vkCreateGraphicsPipelines` returns **`VK_SUCCESS`**.
- The **game's fragment module** is what is rejected: with the trivial vertex
  module and the game's fragment module `2ae58c692fb7fbb8`, the result is
  `-3`. The pipeline state was identical in both directions, so the
  fragment module is the variable.
- Two shader-side defects were found by reading the modules
  (`spirv-dis`):
  1. the fragment module declares its interface variables `%23` (input) and
     `%26` (output) as `v4float` and then addresses their components with
     **`OpAccessChain`**, which is only defined for arrays and structures;
  2. the fragment module reads `Input` locations 0 and 1 while the vertex
     module writes only location 0, and the fragment's input at location 0
     carries `NoPerspective` while the vertex output carries no such
     qualifier.
- `spirv-val --target-env vulkan1.1` exits **0** on all six modules, so the
  validator does not catch either defect.

**Inference, not measurement:** that the malformed `OpAccessChain` is what the
driver rejects. The substitution experiment isolates the fragment module; it
does not isolate the instruction inside it. Fixing the translator is the
coordinator's work, not this card's.

---

## 6. Compute path — no regression

Re-run of P-MM-23's original compute experiment, same manifest, same 24
modules, same device, with the **fixed** probe
(`.local/shader-verify/graphics-crash/compute-regression.stdout.txt`):

| pipeline | created | `vkCreateComputePipelines` | dispatch consumed |
|---|---|---|---|
| `41b20b5f09269ad8_original` | no | `VK_ERROR_INITIALIZATION_FAILED (-3)` | no |
| `86affd63144799de_original` | no | `VK_ERROR_INITIALIZATION_FAILED (-3)` | no |
| `41b20b5f09269ad8_fixed` | no | `VK_ERROR_INITIALIZATION_FAILED (-3)` | no |
| `86affd63144799de_fixed` | no | `VK_ERROR_INITIALIZATION_FAILED (-3)` | no |
| `41b20b5f09269ad8_nof64` | **yes** | **`VK_SUCCESS`** | **yes** |
| `86affd63144799de_nof64` | **yes** | **`VK_SUCCESS`** | **yes** |
| `41b20b5f09269ad8_nof64orig` | **yes** | **`VK_SUCCESS`** | **yes** |
| `86affd63144799de_nof64orig` | **yes** | **`VK_SUCCESS`** | **yes** |

Tier 4 draw: `PASS`, attempted 4, consumed 4. This is **identical** to
P-MM-23's table: the `Float64` word is still what the device objects to in
compute, and the fix changed nothing on that path.

---

## 7. Files

Written, in this task's scope:

- `src/graphics/vulkan_probe/vulkan_shader_probe.c` — the one-line fix and the
  comment that says why.
- `src/graphics/vulkan_probe/vulkan_graphics_bisect.c` — **new**, the bisect
  probe.
- `scripts/graphics/build_vulkan_shader_probe_android.sh` — builds the bisect
  probe alongside the shader probe, stages the trivial module pair, and
  records the bisect artifact in the build evidence.
- `coordination/reports/T-VULKAN-graphics-pipeline-crash-2026-10-25.md` — this
  report.

**Not touched:** `src/graphics/nvn_device/**` (including
`nvn_backend_vulkan.c`), `src/android/native/CMakeLists.txt`,
`scripts/runtime/build_guest_process_contract.sh`, `src/runtime/**`,
`tests/native/guest_process_contract.c`.

Evidence, under `.local/shader-verify/` (not committed):

```
graphics-crash/
  build-01/                      first bisect build (invalid: the probe itself crashed early)
  build-02/                      bisect with tracing
  build-03-fixed/                P-MM-23's probe plus the memset  (the A/B's "fixed" binary)
  build-04-graphics-ok/          the shader probe on the capability-stripped modules
  build-05-game/ .. build-07-game/  the bisect with the game-module mode
  matrix-01/                     the first matrix (invalid, see 8.3)
  matrix-02/                     the second matrix
  matrix-final/out-final/        the final matrix, one directory per experiment
  spv-graphics-ok/               the game modules without Float64 and without FSIL
  spv-noflat/                    the same, without the NoPerspective decoration
  ab-*.{stdout,stderr}.txt       the A/B runs
  baseline-pmm23-probe*.txt      the baseline reproductions
  fixed-probe.stdout.txt         the fixed probe's report
  graphics-ok.stdout.txt         the capability-stripped run
  compute-regression.stdout.txt  the compute re-run
  logcat-*.txt                   the crash buffers
  matrix-run*.log                the matrix runners
graphics-crash-bisect.json       the table, machine-readable
```

---

## 8. Not established

- **No frame, no title screen, no game rendering.** This card produced no
  pixels and claims none.
- **The game's own graphics pipelines still do not build** — they are rejected
  with `-3` for the shader-side reasons in section 5.4. The crash is gone; the
  next blocker is elsewhere.
- **No second device.** The driver build on this device has not been shown to
  be free of other graphics-pipeline defects; only the states in the table
  were exercised.
- **The swapchain path was not exercised.** The probe renders into an
  off-screen image; `vkAcquireNextImageKHR` / `vkQueuePresentKHR` and their
  image-layout agreement were not tested.
- **Tessellation was not exercised.** `pTessellationState` was always NULL
  and no tessellation stage was used.
- **`VK_EXT_extended_dynamic_state` / `2` were not tested**; no candidate
  needed them.
- **The validation layer is not on the device**, so no
  `VK_LAYER_KHRONOS_validation` diagnostic exists for any of this.
- **The emulators on the device were not used as a control** (section 4.4).
- **The driver's defect is characterised, not fixed.** The driver still
  dereferences a null `pSpecializationInfo` and `pNext`; the fix is on the
  caller's side.
- **The measurement covers one driver build on one device.** Nothing here
  generalises to other Mali parts.
- The first matrix (`matrix-01/`) is **invalid**: that build of the bisect
  probe resolved `vkGetPhysicalDeviceMemoryProperties` at device scope, which
  is an instance-level command; the driver returned a pointer that faulted
  before any pipeline was created. It is kept as the record of the mistake,
  not as evidence. Every crash attributed in this report comes from
  `matrix-final/` or from the A/B runs, all of which reach
  `vkCreateGraphicsPipelines`.

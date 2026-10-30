# 工单 P-MM-27｜MiniMax Code｜真机 `vkCreateGraphicsPipelines` 崩溃根因（S2 首帧的硬卡点）

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `c527ca6`（含 P-MM-25 sampler 对象族）
> 设备证据：`coordination/reports/T-SHADER-device-validation-2026-10-25.md`（P-MM-23）、`.local/shader-verify/**`
> 影响：**没有图形管线就没有第一帧**。S2（真实 guest 命令 → 窗口第一帧）在这个问题解决前无法开始。

---

## 0. 目标（一句话）

把 `vkCreateGraphicsPipelines` 在 RG477M 上「一调用就 SIGSEGV」这件事，从「不知道原因」变成「知道是**谁的**问题、**最小触发条件**是什么、**怎么绕过去**」。

---

## 1. 已测到的事实（**别重做，照着往下走**）

P-MM-23 在设备 `RG477M01445122`（Mali-G615 MC6，`ro.hardware.vulkan=mali`，驱动版本 184553472）上测到：

| 事实 | 值 |
|---|---|
| `vkCreateInstance` / `vkCreateDevice` | `VK_SUCCESS` |
| `vkCreateShaderModule` × 24 | 全部 `VK_SUCCESS`（驱动比 spirv-val 宽松） |
| `vkCreateComputePipelines` × 8 | 2 个成功（去掉未用的 `Float64` 能力后），其余 `VK_ERROR_INITIALIZATION_FAILED (-3)` |
| compute 的 dispatch | `vkQueueSubmit` + `vkWaitForFences` 都 `VK_SUCCESS`（GPU 消费了） |
| **`vkCreateGraphicsPipelines`** | **崩溃：`Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x40000000000`** |

崩溃复现条件（都已测）：
- 用**手写的 trivial vertex stage + trivial fragment stage**（不含任何游戏代码）→ **同样崩、同一个 fault addr、同一个 signal**
- 非空 pipeline cache → 仍崩
- framebuffer 在管线**之后**创建 → 仍崩
- `VK_SHADER_STAGE_ALL` 收窄到管线自己的 stage → 仍崩

**所以：崩的不是游戏的 shader，是图形管线状态本身。**

**尚未排除的**：probe 的图形管线状态没有和**第二个实现**对比过，所以现在只能说「按这个方式建的图形管线会崩」，不能说「这台设备建不了图形管线」。这句话的区别就是本工单要做的。

证据文件：
- `.local/shader-verify/run-graphics.exitcode`、`run-graphics.logcat.txt`
- `.local/shader-verify/run-trivial-control.exitcode`、`run-trivial-control.logcat.txt`
- `.local/shader-verify/device-run-graphics.json`（**空**：崩溃发生在写报告之前）
- 探针源码 `src/graphics/vulkan_probe/vulkan_shader_probe.c`、构建脚本 `scripts/graphics/build_vulkan_shader_probe_android.sh`

---

## 2. 仓库与设备（必须）

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- **自己开独立 worktree**：`.worktrees/p-mm-27`，分支 `task/P-MM-27-graphics-pipeline-crash`
- 设备 `RG477M01445122`：**本工单期间设备归你独占**
  - 跑真机前：`touch .local/agent-pm/BOOT_LOCK_MM_A`；**结束必须删掉**
  - 跑真机前先确认锁不在（`ls .local/agent-pm/`），有别人的锁就停下报告，不要抢
  - 不 root / 不刷机 / 不清数据
- 成功可 `git commit`；**不 push**
- `.local/romfs/**` 只读

---

## 3. 你要回答的问题（**按顺序**，每个都要有实测证据）

### Q1 最小触发条件是什么？

把 `VkGraphicsPipelineCreateInfo` 的状态**逐项减**，直到找到一个**最小可复现**（或反过来，逐项加，直到第一次崩）：

- 顶点输入：`vertexBindingDescriptionCount` / `vertexAttributeDescriptionCount` 是否为 0？`pVertexBuffers` 是否为 NULL？
- `pTessellationState` 是否为 NULL（tessellation 关掉）
- `pViewportState`：`viewportCount` / `scissorCount` 是否为 0？`pViewports` / `pScissors` 是否为 NULL？
- `pRasterizationState`：lineWidth、cullMode、frontFace、depthClampEnable、rasterizerDiscardEnable
- `pMultisampleState`：`rasterizationSamples` 是否和 render pass / framebuffer 一致（**1 vs 1？**）
- `pDepthStencilState`：是否有 depth attachment？`pColorBlendState`：`attachmentCount` 是否为 0？逻辑不一致会不会走这个路径？
- `pDynamicState` 的 dynamic state 数量
- **render pass**：`VkRenderPassCreateInfo` 的 attachment 数量、`loadOp`/`storeOp`、`initialLayout`/`finalLayout`、subpass 的 `pColorAttachments`
- **framebuffer**：`VkFramebufferCreateInfo` 的 attachment 数量、view 数量、宽高、format
- **swapchain**：本卡有没有用？如果用了，`vkAcquireNextImageKHR` / `vkQueuePresentKHR` 与管线的 image layout 是否匹配
- `VkPipelineColorBlendStateCreateInfo` 的 `logicOp` / `blendEnable`
- `VkPipelineVertexInputStateCreateInfo` 的 `vertexBindingDescription.binding` 是否越界

**每减一项，重跑一次，把结果记成表**：`状态组合 → 崩 / 不崩 → 崩的话 fault addr + signal`。

### Q2 崩在驱动哪一层？

- 抓 **logcat**：`adb logcat -b crash`、`adb logcat -d | grep -i -E "vulkan|mali|gralloc|libvulkan"`（**崩溃前后的完整上下文都要留档**）
- 看是不是 **loader**（`libvulkan.so`）崩而不是 **driver**（`libGLES_mali_vulkan.so` 之类）崩：用 `/proc/<pid>/maps` 看崩溃进程的库映射；能拿到就记下来
- 用 **`vkGetDeviceProcAddr`** 拿到 `vkCreateGraphicsPipelines` 的真实函数指针地址，和其它 `vkCreate*` 的地址对比（同一区间 = 同一驱动模块）
- 试一次 **`VK_INSTANCE_LAYERS`**：开 `VK_LAYER_KHRONOS_validation`（设备上有没有这个 layer？`ls /vendor/etc/vulkan/icd.d/`、`ls /system/vendor/lib/hw/vulkan/`），有就开，validation 的报错能直接指到 API 用错
- 试 **`VK_EXT_debug_utils`**：如果有 messenger，把 `VK_EXT_debug_utils` / `VK_EXT_debug_report` 挂上，让驱动在崩之前说话

### Q3 是「我们的状态错了」还是「驱动有 bug」？

需要一个**第二个实现**做对照，至少做到其中一条：

- **同款 probe 在另一台 Vulkan 设备 / 另一台机器上跑同一段状态**，不崩 ⇒ 我们的状态错了（虽然 A 组状态在别处能跑）
  - 宿主 Mac 没有 Vulkan（**不能**用 MoltenVK/lavapipe 顶替，这属于换设备不是换实现，要说清楚）
  - 可选：CI 上有没有别的 Android 设备？没有就在报告里写「没有第二台设备」
- **同设备上换一个已知能跑的图形管线实现**：系统自带任何用到 Vulkan 图形管线的程序（launcher / 系统渲染）能跑 ⇒ 驱动能建图形管线 ⇒ 我们的状态有问题
- **反过来**：如果**任何**图形管线状态在这台设备上都崩，且第二实现也一样崩 ⇒ 驱动 bug，要给出**最小触发条件**和**规避办法**（例如：某个 `VkPipelineXXXCreateInfo` 结构必须带某个 pNext、某个 attachmentCount 不能为 0、必须用 `VkPipelineRenderingCreateInfo` 而不是 render pass）

### Q4 规避办法是什么？（**这是本工单真正的交付**）

即使根因是驱动 bug，**也必须给出在 RG477M 上能建成图形管线的办法**。候选（自己测，不要假设）：

1. 用 **`VkPipelineRenderingCreateInfo`**（`VK_KHR_dynamic_rendering`）代替 render pass
2. 关掉 `VK_SHADER_STAGE_ALL`，只声明真正用到的 stage
3. **不创建 render pass**，用 dynamic rendering + `VkPipelineColorBlendStateCreateInfo` 的 attachment 数与实际一致
4. `pViewportState` 显式给 1 个 viewport / scissor（不是 0）
5. 顶点输入 / `vertexBindingDescriptionCount` 给一个最小合法值（不是 0）
6. `rasterizationSamples` = 1，且和 framebuffer 一致
7. `VK_EXT_extended_dynamic_state` / `VK_EXT_extended_dynamic_state2` 特性启用（设备支持与否要查）
8. `VkPipelineColorBlendStateCreateInfo::attachmentCount` 与 render pass 的颜色附件数一致

**每条都要在设备上实测**，通过的写进报告，没通过的也写（省下别人重试）。

---

## 4. 交付物

### 4.1 你写的文件

```
src/graphics/vulkan_probe/vulkan_shader_probe.c        （可改）
scripts/graphics/build_vulkan_shader_probe_android.sh  （可改）
src/graphics/vulkan_probe/vulkan_graphics_bisect.c     （新增：最小化二分探针）
coordination/reports/T-VULKAN-graphics-pipeline-crash-2026-10-25.md
```

证据（`.local/`，**不提交**）：
```
.local/shader-verify/graphics-crash/<实验名>/   （每次实验独立目录：命令、退出码、logcat、run.json）
.local/shader-verify/graphics-crash-bisect.json （状态组合 → 崩/不崩 的完整表）
```

### 4.2 你**不许**改的文件（总控保留写权限）

`src/graphics/nvn_device/nvn_backend_vulkan.c`、`src/graphics/nvn_device/**` 其余全部、
`src/android/native/CMakeLists.txt`、`scripts/runtime/build_guest_process_contract.sh`、
`src/runtime/**`、`tests/native/guest_process_contract.c`

⚠️ **不要去改 `nvn_backend_vulkan.c` 去"绕过"崩溃**。你只负责把根因和规避办法**测出来并写清楚**，接线由总控做。

### 4.3 硬性要求

- **一次实验一个目录**，日志目录不覆盖
- 每个结论都附**命令 + 退出码 + 原始日志路径**
- 崩溃的**进程级**退出码（SIGSEGV → 退出码 139 / -11）和 fault addr 都要记
- **不要**把「崩」写成「失败」或「返回错误码」——这是 SIGSEGV，两者不同
- 报告里要写明：哪些是**实测**，哪些是**推断**

---

## 5. 验收

1. **最小复现**：给出一个**尽可能小的**图形管线状态集，并说明它崩 / 不崩。给出状态清单 + 每次的退出码和 fault addr。
2. **归因**：说明崩在 loader 还是 driver，依据是**实测证据**（库映射 / proc 地址 / logcat），不是猜测。
3. **对照**：要么给出第二实现/第二设备的对照结果，要么明确写「没有第二台设备，无法对照」并说明这对结论的影响。
4. **规避办法**：至少给出**一个在设备上实测通过**的建管线办法（能建成 `VK_SUCCESS` 的图形管线），并把它的完整状态写出来。
5. **不回归**：compute 路径仍然通（复跑一遍原来的 compute 实验，贴结果）。
6. 报告 `coordination/reports/T-VULKAN-graphics-pipeline-crash-2026-10-25.md`。

---

## 6. 禁止事项

- ❌ 不 push；不 reset --hard / clean / stash
- ❌ 不改 4.2 里的文件
- ❌ 不声称「已经有帧 / 标题画面 / 可玩」——**这一轮一个像素都不会有**
- ❌ 不把「驱动崩了」当成「设备不支持」，也不反过来
- ❌ 不把对照缺失说成对照通过
- ❌ 不为让实验跑通而伪造状态
- ❌ 跑完不删 boot lock

---

## 7. 完成后的动作

1. `git commit`（分支 `task/P-MM-27-graphics-pipeline-crash`，**不 push**），只放 4.1 里的文件
2. **删掉** `.local/agent-pm/BOOT_LOCK_MM_A`
3. 把证据目录拷到主检出的 `.local/shader-verify/graphics-crash/`
4. 写完报告就**停下来**，最终回复给我：分支、commit、Q1–Q4 的实测答案（含退出码和 fault addr）、证据路径、以及**没有**证明的东西

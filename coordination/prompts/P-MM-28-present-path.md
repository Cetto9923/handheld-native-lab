# 工单 P-MM-28｜MiniMax Code｜呈现路径：acquire → render pass → clear → submit → present

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `cce6222`（含 P-MM-24 命令内存回调、P-MM-25 sampler 族、描述符池编号基址接线）
> 影响：S1-c（replay → Vulkan → 窗口）缺的就是这一段。**没有它，replay 读到的命令包到不了屏幕。**

---

## 0. 目标（一句话）

把 `nvn_backend_vulkan.c` 从「能创建设备、队列、surface、swapchain」补成「**能取到 swapchain 图像、开 render pass、用 guest 自己的清屏颜色清屏、提交、present**」。

**这一轮不会出游戏帧。** 出来的最多少量是「**guest 的清屏命令第一次到了屏幕**」。这句话必须这么写，不得写成「出帧」「标题画面」「可玩」。

---

## 1. 现状（**别重做**）

`src/graphics/nvn_device/nvn_backend_vulkan.c` 已有：

| 已有 | 位置 |
|---|---|
| `nvn_vulkan_create`（VkInstance + VkDevice，含 `shaderStorageImageWriteWithoutFormat` 的条件启用） | :51 |
| `nvn_vulkan_destroy` | :227 |
| `nvn_vulkan_describe` | :236 |
| `nvn_vulkan_get_queue` | :250 |
| `nvn_vulkan_create_window_surface`（VkSurfaceKHR + VkSwapchainKHR，格式/尺寸/present mode 都查过设备） | :259 |
| `nvn_vulkan_destroy_window_surface` | :394 |

**缺的**：`vkAcquireNextImageKHR`、`vkCreateRenderPass` / `vkCreateFramebuffer`（对着 swapchain 图像）、`vkBeginRenderPass` / `vkEndRenderPass`、`vkQueueSubmit`、`vkQueuePresentKHR`。

后端是**虚表**（`src/graphics/nvn_device/nvn_device.h` 的 `typedef struct nvn_backend`，:69），当前有 `create` / `destroy` / `describe` / `get_queue` / `create_window_surface` / `destroy_window_surface`。**你要往这个虚表上加方法**，`nvn_backend_none.c` 必须同样加上并**诚实拒绝**（不能假装成功）。

`src/runtime/nso/native_window.c` / `native_window.h` 那边有 Android 的 `ANativeWindow` 桥。

---

## 2. 仓库与设备

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- **自己开 worktree**：`.worktrees/p-mm-28`，分支 `task/P-MM-28-present-path`
- 设备 `RG477M01445122`：**P-MM-27 现在占着设备**（它在查 `vkCreateGraphicsPipelines` 崩溃）。
  - 你**先写代码和宿主契约**；要跑真机时先 `ls .local/agent-pm/`，**有别人的锁就停下报告**，不要抢。
  - 真机 boot / 装包前 `touch .local/agent-pm/BOOT_LOCK_MM_A`，**结束必须删掉**。
- 成功可 `git commit`；**不 push**；不 reset --hard / clean / stash

---

## 3. 你要交付的东西

### 3.1 后端的新方法（虚表）

| 方法 | 做什么 | 失败时 |
|---|---|---|
| `acquire_image` | `vkAcquireNextImageKHR`，把 `VkImage` 和 index 交回 | 返回 0 并说明原因（超时、surface 没了、swapchain 重建中） |
| `begin_render_pass` | 用**调用方给的颜色**建/复用 render pass（`loadOp = CLEAR`，`clearValue` = 该颜色），framebuffer 对着 swapchain 图像，开 `vkBeginRenderPass` | 返回 0 并说明原因 |
| `submit_and_present` | `vkEndRenderPass` → `vkQueueSubmit` → `vkQueuePresentKHR`，**等 fence**（用 `vkWaitForFences` 或 `vkQueueWaitIdle`，并把等待时间记进日志） | 返回 0 并说明原因 |

**render pass 与 framebuffer 必须缓存复用**（每个 swapchain 图像一个 framebuffer），**不要每帧重建**。

**颜色格式**：swapchain 的 `VkSurfaceFormatKHR` 是什么就用什么，**不要假设是 `VK_FORMAT_B8G8R8A8_UNORM`**；拿到格式先记进日志。

### 3.2 新模块 `src/graphics/nvn_device/nvn_present.{h,c}`

对外一个薄接口，把「后端」和「replay」隔开：

```c
/* 拿下一张 swapchain 图像，用 rgba 清屏，提交并 present。
 * 返回 1 表示这一帧真的提交并显示了；返回 0 表示没有，
 * 并把原因写进日志。 */
int nvn_present_frame(nvn_device_state *device, const float rgba[4]);
```

- `rgba` 是 **guest 自己命令流里的清屏颜色**（`OP_CLEAR_COLOR` 记录里的四个浮点数）。
- **不许**在这里编一个颜色；颜色必须由调用方给。
- 这一轮**颜色从哪来由协调者接**（replay 侧的状态表还没有清屏颜色字段），**你只负责接口和后端**，并在报告里写清协调者要接哪一行。

### 3.3 宿主契约 `tests/native/nvn_present_contract.c`

- 文件名 ⇒ 开关 `--nvn-present`（构建脚本从文件名推导，别自己改构建脚本）
- 自带 `main`、自己的 `CHECK` / `expect_value` / `expect_condition`、末行 stdout 一行 JSON，里面有 `nvn_present_errors`
- **宿主没有 Vulkan**（宿主 macOS 没有 Vulkan 驱动）⇒ 契约在宿主上要**诚实**：
  - 用 `nvn_backend_none` 那一侧，断言「没有后端时**不**报成功」「**不**present」「错误被计数」；
  - **不许**在契约里造一个假的 VkImage 来假装 present 成功。
- 契约要能证明：**接口存在、参数被检查、失败被点名、失败不产生「已显示」的假象**。

### 3.4 自证脚本（三变体）

`scripts/runtime/selfproof_nvn_present.sh` + `scripts/runtime/make_nvn_present_variants.py`，照 `scripts/runtime/selfproof_nvn_sampler.sh` 的形状：

| 变体 | 期望 |
|---|---|
| baseline | exit 0，stdout `errors:0` |
| **A** 改坏一个期望值 | **exit 1**，stderr 有真 `[check-fail]` |
| **B** 同处改坏 + 去 CHECK + 错误数硬编码 0 | **exit 0** 而 stdout 仍谎报 `errors:0`（**假绿要被量出来**） |
| **C** 契约不动、只改桩 | **exit 1** |

脚本自己判四个退出码。日志写 `.local/logs/p-mm-28-present-selfproof.log`。

### 3.5 真机（**设备空出来之后**）

如果 P-MM-27 交了回设备：

1. 用**宿主同一份代码**编出 APK 装到 RG477M；
2. 跑一次**受控清屏**（`nvn_present_frame` 用一个固定颜色），**≥300 秒窗口**、**独立日志目录**；
3. 报告要**分开写**：
   - **「宿主探针成功」** = swapchain 图像取到了、render pass 开了、提交返回 `VK_SUCCESS`、present 返回 `VK_SUCCESS`、fence 等到了；
   - **「guest 的命令到了屏幕」** = 这一轮**做不到**（replay 的清屏颜色还没接），**不要混写**；
   - 截图（`adb exec-out screencap -p`）**只能证明屏幕上有东西**，不能证明那是游戏画面。

---

## 4. 写范围（**只有这些**）

```
src/graphics/nvn_device/nvn_backend_vulkan.c
src/graphics/nvn_device/nvn_backend_none.c
src/graphics/nvn_device/nvn_device.h          （只加虚表条目，别改别的）
src/graphics/nvn_device/nvn_present.h
src/graphics/nvn_device/nvn_present.c
tests/native/nvn_present_contract.c
scripts/runtime/selfproof_nvn_present.sh
scripts/runtime/make_nvn_present_variants.py
coordination/reports/T-PRESENT-path-2026-10-25.md
```

## 5. 不许改（总控保留写权限）

`src/graphics/nvn_device/nvn_command_replay.{h,c}`、`nvn_command_buffer.*`、`nvn_device.c`、
`src/graphics/nvn_device/nvn_texture*.{h,c}`、`nvn_sampler*.{h,c}`、`nvn_pool*`、
`src/android/native/CMakeLists.txt`、`scripts/runtime/build_guest_process_contract.sh`、
`src/runtime/**`、`tests/native/guest_process_contract.c`

---

## 6. 验收

1. **接口完整**：虚表上有 `acquire_image` / `begin_render_pass` / `submit_and_present`；`nvn_backend_none.c` 同样有，且**诚实拒绝**。
2. **失败被点名**：每个失败路径都 `fprintf` 出**具体原因**（不是 "failed"），并计数。
3. **render pass / framebuffer 复用**：第二次 `begin_render_pass` **不重建**（日志或计数能看出来）。
4. **颜色来自调用方**：`nvn_present_frame` 不含任何写死的颜色。
5. **契约 + 三变体自证**全绿（A红 / B假绿 / C红），契约**不造假的成功**。
6. **NDK 交叉编译**：`--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` 过。
7. **真机**（设备空出来才做）：把「宿主探针成功」和「guest 的命令到了屏幕」**分开报**，后者这轮**做不到就写做不到**。
8. **报告** `coordination/reports/T-PRESENT-path-2026-10-25.md`。

---

## 7. 禁止事项

- ❌ push；reset --hard / clean / stash
- ❌ 改第 5 节里的文件
- ❌ 声称「出帧」「标题画面」「可玩」「游戏画面」
- ❌ 把**宿主探针的成功**写成**guest 的画面**
- ❌ 在契约里造假的 VkImage / 假的 present 成功
- ❌ 恒真断言 / 硬编码错误数 0 / 删测试改绿
- ❌ 每帧重建 render pass / framebuffer（要复用）
- ❌ 假设 swapchain 格式（先查再写）
- ❌ 跑完不删 boot lock

---

## 8. 完成后的动作

1. `git commit`（分支 `task/P-MM-28-present-path`，**不 push**），只放第 4 节的文件
2. 报告写完就**停下来**，最终回复给我：分支、commit、每个验收项的实测结果（含四个退出码）、证据路径、**没有**证明的东西

---

## 10. 第二次派工：设计已经定了，**先写代码**

> 上一轮的 worker 把预算全花在设计推演上，**一个文件、一个提交都没留下**（worktree 干净、分支无提交）。
> 这一轮**不许再从零做设计**。下面的设计**已经定好，照着写**。

### 10.1 纪律（**违反这条等于没干活**）

1. **第一条工具调用就是写文件**，不是搜索、不是读文档、不是思考。
2. **先写最小可编译版本，立刻 `git commit`**，再一项一项补。**宁可有五个能编译的提交，也不要一个想清楚了但没写出来的提交。**
3. 中途被压缩上下文时，**提交里要能看出进度**（检查点）。
4. 预算过半还没提交 ⇒ **立刻提交**当前的能编译状态。

### 10.2 虚表（`nvn_device.h`，只加这三个条目）

```c
int (*acquire_image)(nvn_device_state *device, uint32_t *out_index, void **out_image,
                     char *reason, size_t reason_size);
int (*begin_render_pass)(nvn_device_state *device, uint32_t index, void *image,
                          const float rgba[4], char *reason, size_t reason_size);
int (*submit_and_present)(nvn_device_state *device, uint32_t index, void *image,
                           char *reason, size_t reason_size);
```

`reason` 是**失败被点名**的通道：失败时把**具体原因**写进去（不是 "failed"），由 `nvn_present.c` 记下来。`nvn_backend_none.c` 三个都要有，且**诚实拒绝**。

### 10.3 Vulkan 后端自己持有呈现路径

`nvn_vulkan_state` 增加：surface、swapchain、swapchain 格式、宽高、image_count、`VkImage*` 图像、`VkImageView*` 视图、`VkFramebuffer*` framebuffer、render_pass、command_pool、每图像一个 `VkCommandBuffer`、fence，以及 builds/reuses 计数。

**单条呈现路径**（后端自己创建的那一条），多窗口不支持 —— **在报告里点명**，不要假装支持。

### 10.4 ⚠️ 必须修的一个隐藏缺陷

`nvn_backend_vulkan.c` **只启用了实例扩展**（`VK_KHR_surface` / `VK_KHR_android_surface`），**没有启用设备扩展 `VK_KHR_swapchain`**，却**直接调用** `vkCreateSwapchainKHR`。
⇒ 在设备上这会 **失败**（`VK_ERROR_EXTENSION_NOT_PRESENT`）。
**在 `nvn_vulkan_create` 里把 `VK_KHR_SWAPCHAIN` 加进设备扩展列表**（先问设备支不支持，不支持就**如实报告**）。

### 10.5 render pass / framebuffer 的做法

- **render pass 建一次就复用**：1 个颜色附件，格式 = swapchain 格式，`loadOp = CLEAR`，`storeOp = STORE`，`initialLayout = UNDEFINED`，`finalLayout = PRESENT_SRC_KHR`。
  **颜色不在 render pass 里**，在 `VkRenderPassBeginInfo.pClearValues` 里 ⇒ **换颜色不需要重建 render pass**。
- **每个 swapchain 图像一个 image view + 一个 framebuffer**，建好就缓存；**计数 builds 与 reuses**，第二次必须是 reuse。
- **每个 swapchain 图像一个 command buffer**（从 command pool 分配），录制前 `vkResetCommandBuffer`。

### 10.6 三个方法的分工

| 方法 | 做什么 | 顺序要点 |
|---|---|---|
| `acquire_image` | `vkAcquireNextImageKHR`，交回 index 和 `VkImage` | 超时 / surface 没了 / swapchain 重建中 → **分别点名** |
| `begin_render_pass` | 确保 render pass / view / framebuffer 存在（复用优先）→ 重置并开始记录该图像的 command buffer → `vkCmdBeginRenderPass`，`pClearValues[0].color.float32 = 调用方给的 rgba` | **颜色只在这里出现，且来自参数** |
| `submit_and_present` | `vkCmdEndRenderPass` → `vkEndCommandBuffer` → `vkQueueSubmit`（带 fence）→ `vkQueuePresentKHR` → `vkWaitForFences` | **等待时间用 `clock_gettime(CLOCK_MONOTONIC)` 量出来记进日志**；等待超时**按失败点名**，不要无限等 |

### 10.7 `nvn_present.c` 的形状

```c
int nvn_present_frame(nvn_device_state *device, const float rgba[4]);
```

- 参数为 NULL ⇒ 拒绝 + 点名 + 计数。
- 后端没有这三个方法 ⇒ 拒绝 + 点名 + 计数。
- 任一步失败 ⇒ 记下**那一步的 reason**、计数、返回 0。
- 全程成功 ⇒ 计数 `frames_presented`、返回 1。
- **这个文件里不许出现任何写死的颜色**（不许有 `0.0f` / `1.0f` 这类字面量）。
- 诊断计数按**项目的房规**暴露一个 `nvn_present_get_stats()`（照 `nvn_sampler_get_stats` / `nvn_command_replay_get_stats` 的样子）。**对调用方的接口仍然只有 `nvn_present_frame` 一个**。

### 10.8 契约（宿主没有 Vulkan ⇒ 用 `nvn_backend_none`）

要证明的四件事，**各自独立**：

1. **接口存在**：`nvn_backend_none` 的三个方法都在，且**直接调用时都返回 0 并给出 reason**。
2. **参数被检查**：device / rgba / out 参数为 NULL 时**拒绝**，不是崩。
3. **失败被点名**：`nvn_present_frame` 返回 0，**记录的 reason 里含具体步骤名**（如 "acquire"），且**stderr 里有同样的字样**。
4. **失败不产生「已显示」的假象**：`frames_presented == 0`。

再加一条**颜色确实来自调用方**的证明：在契约里放一个**只记录、不 present** 的假后端（`acquire_image` 收下颜色、`submit_and_present` **返回 0 拒绝 present**），断言它收到的颜色与契约传入的**逐位相同**，且**这一帧仍然没有被报告成已显示**。
⚠️ 这个假后端**不许**假装 present 成功。

### 10.9 三变体

- **A**：改坏一个期望值 → **exit 1**，stderr 有真 `[check-fail]`。
- **B**：同处改坏 **+ 去掉最终 CHECK **+ 把上报的错误数**硬编码 0** → **exit 0**，而 stdout 仍谎报 `errors:0`、stderr 留真 `[check-fail]`（**假绿要量出来**）。
- **C**：契约一个字不改，**把桩改坏** —— 让假后端的三个方法**都返回 1**（谎称 present 成功）⇒ 契约必须抓到「报告了已显示但什么都没显示」⇒ **exit 1**。

### 10.10 真机

设备归 P-MM-27 的那轮已经结束，锁已释放，但**协调者现在占着设备**做真机重跑。
你要真机时先 `ls .local/agent-pm/`：**有锁就停下报告，不要抢**。没有锁才 `touch .local/agent-pm/BOOT_LOCK_MM_A`，**结束必须删掉**。
真机那部分只证明**「宿主探针成功」**（图像取到 / render pass 开了 / 提交 `VK_SUCCESS` / present `VK_SUCCESS` / fence 等到），**不证明** guest 的画面 —— 那一轮**做不到**。

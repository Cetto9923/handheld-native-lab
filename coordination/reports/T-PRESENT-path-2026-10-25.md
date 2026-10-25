# T-PRESENT-path-2026-10-25 ｜ P-MM-28（第二次派工）：呈现路径

> 分支：`task/P-MM-28-present-path-v2`　worktree：`.worktrees/p-mm-28b`
> 分支点：`work/rg477m-native` @ `971df74`
> 状态：**代码 + 宿主契约 + 三变体自证已完成并全绿**；**真机未做**（设备锁在协调者手上）

---

## 0. 一句话

`nvn_backend_vulkan.c` 现在能**取 swapchain 图像 → 开 render pass → 用调用方给的颜色清屏 → 提交 → present → 等 fence**；
宿主契约用 `nvn_backend_none` 和一个**只记录不 present** 的假后端证明了四件事，三变体自证 A 红 / B 假绿 / C 红。

**这一轮没有出游戏帧，也没有证明 guest 的命令到了屏幕。** 真机那一步因为设备锁没有做（见第 6 节）。

---

## 1. 分支与提交

| commit | 内容 |
|---|---|
| `ccf76fd` | feat(graphics): declare the present path interface — `nvn_present.h` |
| `a8678e4` | feat(graphics): add the three present path steps to the backend vtable — `nvn_device.h` 虚表 + `nvn_backend_none.c` |
| `6a79de3` | feat(graphics): the Vulkan backend owns the present path — `nvn_backend_vulkan.c` |
| `90319d2` | test(graphics): the contract of the present path and its three variants — 契约 + 两个脚本 |

未 push。未做 `reset --hard` / `clean` / `stash`。

**写入范围核对**（`git diff --name-only 971df74..HEAD`）：

```
scripts/runtime/make_nvn_present_variants.py
scripts/runtime/selfproof_nvn_present.sh
src/graphics/nvn_device/nvn_backend_none.c
src/graphics/nvn_device/nvn_backend_vulkan.c
src/graphics/nvn_device/nvn_device.h          （只加虚表条目）
src/graphics/nvn_device/nvn_present.c
src/graphics/nvn_device/nvn_present.h
tests/native/nvn_present_contract.c
coordination/reports/T-PRESENT-path-2026-10-25.md   （本文件）
```

工单第 5 节的禁改文件**一个都没碰**（脚本按名字过滤核对过）。
`nvn_device.h` 只加了三个虚表条目，没有改别的。

**基线已经前进**：集成时 `work/rg477m-native` 已到 `6d6cbb1`。核对过：`971df74..6d6cbb1` 之间基线**没有碰**我改的任何文件，
只有 `tests/native/nvn_texture_contract.c` 变了（不是我的文件），所以合并不冲突。

---

## 2. 虚表（`nvn_device.h`，只加这三个）

```c
int (*acquire_image)(nvn_device_state *device, uint32_t *out_index, void **out_image,
                     char *reason, size_t reason_size);
int (*begin_render_pass)(nvn_device_state *device, uint32_t index, void *image,
                          const float rgba[4], char *reason, size_t reason_size);
int (*submit_and_present)(nvn_device_state *device, uint32_t index, void *image,
                           char *reason, size_t reason_size);
```

`reason` 就是**失败被点名**的通道：失败时把**具体原因**写进去（不是 "failed"），`nvn_present.c` 把它连同**步骤名**一起记下来并打到日志。

`nvn_backend_none.c` 三个都有，且**诚实拒绝**（返回 0 + 具体原因），不假装成功。

---

## 3. Vulkan 后端自己持有呈现路径

`nvn_vulkan_state` 新增：surface、swapchain、swapchain 格式与颜色空间、宽高、image_count、
`VkImage*` 图像、`VkImageView*` 视图、`VkFramebuffer*` framebuffer、render pass、command pool、
每图像一个 `VkCommandBuffer`、每图像一个 `VkFence`，以及 builds/reuses 计数。

⚠️ **单条呈现路径**：一个 backend 一条 surface/swapchain。**多窗口不支持**（已在代码与本报告里点名，没有假装支持）。

### 3.1 必须修的隐藏缺陷：设备扩展 `VK_KHR_swapchain`

原来的代码只启用了**实例扩展**（`VK_KHR_surface` / `VK_KHR_android_surface`），
却直接调 `vkCreateSwapchainKHR` ⇒ 设备上会以 `VK_ERROR_EXTENSION_NOT_PRESENT` 失败。

**修法**（`nvn_vulkan_create`）：先用 `vkEnumerateDeviceExtensionProperties` **问设备支不支持**，
支持才把 `VK_KHR_SWAPCHAIN` 加进设备扩展列表；不支持就**如实打日志**
（`the device does not have VK_KHR_swapchain, so this runtime cannot present`），不假装有。

### 3.2 render pass / framebuffer 复用

- render pass **建一次就复用**：1 个颜色附件，格式 = **swapchain 实际给的格式**（不假设 `B8G8R8A8`），
  `loadOp = CLEAR`、`storeOp = STORE`、`initialLayout = UNDEFINED`、`finalLayout = PRESENT_SRC_KHR`。
  **颜色不在 render pass 里**，在 `VkRenderPassBeginInfo.pClearValues` 里 ⇒ **换颜色不重建 render pass**。
- 每个 swapchain 图像一个 view + 一个 framebuffer，建好就缓存。
- 计数：`render_passes_built / render_passes_reused`、`framebuffers_built / framebuffers_reused`。
- 每个 swapchain 图像一个 command buffer（`VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT`），
  录制前 `vkResetCommandBuffer`。

### 3.3 三个方法的分工与顺序

| 方法 | 做什么 | 失败时分别点名 |
|---|---|---|
| `acquire_image` | `vkAcquireNextImageKHR`（**超时上限 1 s**），交回 index 与 `VkImage` | 超时 / swapchain 过期 / surface 丢失 / device 丢失 / 其它码，各自一条文字 |
| `begin_render_pass` | 确保 render pass 与 framebuffer 存在（**复用优先**）→ 重置并开始记录 → `vkCmdBeginRenderPass`，`pClearValues[0].color.float32 = 调用方给的 rgba`（**逐位拷贝**） | 索引越界 / 图像对不上 / `vkCreateRenderPass` / `vkCreateImageView` / `vkCreateFramebuffer` / `vkBeginCommandBuffer` 的返回码 |
| `submit_and_present` | `vkCmdEndRenderPass` → `vkEndCommandBuffer` → `vkQueueSubmit`(fence) → `vkQueuePresentKHR` → `vkWaitForFences` | `vkEndCommandBuffer` / `vkQueueSubmit` / `vkQueuePresentKHR` / fence 超时，各自一条文字 |

**等待时间用 `clock_gettime(CLOCK_MONOTONIC)` 量出来**：
`last_fence_wait_ms` / `max_fence_wait_ms` / `total_fence_wait_ms`，
日志按「第一帧 + 每 100 帧 + 每次失败」输出（每帧都打会刷屏）。
**fence 等待有上限（2 s），超时按失败点名，不无限等**；acquire 同样有上限（1 s）。

---

## 4. `nvn_present.{h,c}`

对外接口只有一个：

```c
int nvn_present_frame(nvn_device_state *device, const float rgba[4]);
```

- **成功返回 1，失败返回 0**（工单第 3.2 / 10.7 节）。
- 参数为 NULL ⇒ 拒绝 + 点名 + 计数；后端没有这三个方法 ⇒ 拒绝 + 点名 + 计数。
- 任一步失败 ⇒ 记下**那一步的 reason**（reason 以步骤名开头，例如 `acquire: ...`）、计数、返回 0。
- 全程成功 ⇒ `frames_presented++`、返回 1。
- **失败绝不产生「已显示」的假象**：`frames_presented` 只在第三步成功后才动。
- **这个文件里没有任何写死的颜色**：`grep -nE "[0-9]+\.[0-9]*f?|[0-9]*\.[0-9]+f" src/graphics/nvn_device/nvn_present.c` → **一个浮点字面量都没有**。
- 诊断计数按房规暴露 `nvn_present_get_stats(nvn_present_stats *out)`（另有 `nvn_present_reset_stats()`）。
  计数项：`frames_requested / frames_acquired / frames_rendered / frames_submitted / frames_presented /
  failures / acquire_failures / render_pass_failures / submit_failures / last_reason`。
  `last_reason` 是**记下来的那一步的原因**，与打到 stderr 的文字**逐字相同**。

---

## 5. 契约 + 三变体（实测）

### 5.1 契约 `tests/native/nvn_present_contract.c`

文件名 ⇒ 开关 `--nvn-present`（**没有改构建脚本**；开关由 `build_guest_process_contract.sh` 从文件名推导）。
契约自带 `main`、自己的 `CHECK` / `expect_value` / `expect_condition`，末行 stdout 一行 JSON，含 `nvn_present_errors`。

宿主没有 Vulkan ⇒ 契约用 `nvn_backend_none` 那一侧，外加一个**只记录、不 present** 的假后端。
**契约里没有造假的 VkImage，也没有假装 present 成功**。

证明的四件事，各自独立：

1. **接口存在**：`nvn_backend_none` 的三个方法都在，**直接调用时都返回 0 并给出 reason**。
2. **参数被检查**：device / rgba / 输出指针为 NULL 时**拒绝**，不崩；后端没有这三个方法时也拒绝。
3. **失败被点名**：`nvn_present_frame` 返回 0，`last_reason` **以 `acquire:` 开头**（步骤名），
   且 **stderr 里有逐字相同的文字**（契约用 `tmpfile()` + `dup2` 抓 stderr 核对，不是假设）。
4. **失败不产生「已显示」的假象**：`frames_presented == 0`、`frames_submitted == 0`。

再加一条**颜色确实来自调用方**：假后端把 `rgba` 逐位记下来、且**拒绝 present**；
契约断言它收到的四个 float 与传入的**逐位相同**（`memcmp`），且这一帧**仍然没有被报告成已显示**。

> 契约用 `#include "../../src/graphics/nvn_device/nvn_present.c"` 把模块带进来：
> 宿主构建脚本的源文件清单是写死的，而 `scripts/runtime/build_guest_process_contract.sh` 的写权限在总控，
> 所以这里用相对路径把**树里的那份文件**带进契约（不是拷贝）。
> 总控若愿意把 `nvn_present.c` 加进 `build_guest_process_contract.sh` 的 `core_sources`，这一行可以删掉。

### 5.2 四个退出码（实测）

`bash scripts/runtime/selfproof_nvn_present.sh` → **退出码 0**（脚本自己判四个退出码，不靠人看）。

| 变体 | 退出码 | stdout / stderr 实测 |
|---|---|---|
| **baseline** | **0** | stdout `{"nvn_present_errors":0,...}` |
| **A** 改坏一个期望值 | **1** | stderr `[check-fail] nvn-present-shows-nothing-when-the-step-failed actual=0x0 expected=0x1`；stdout 空（终闸拦下） |
| **B** 同处改坏 + 去 CHECK + 错误数硬编码 0 | **0** | stdout **仍谎报** `{"nvn_present_errors":0,...}`，stderr **留真** `[check-fail] nvn-present-shows-nothing-when-the-step-failed actual=0x0 expected=0x1` ⇒ **假绿被量出来** |
| **C** 只改桩：假后端谎称 present 成功 | **1** | stderr 4 条真 `[check-fail]`，其中 `nvn-present-the-fake-backend-refuses-to-present actual=0x1 expected=0x0`、`nvn-present-the-fake-frame-was-not-shown actual=0x1 expected=0x0` |

脚本另外断言：baseline 必须报 `"nvn_present_errors":0`；A/C 的 stderr 必须有真 `[check-fail]`；
**B 必须「exit 0 且 stdout 谎报 errors:0 且 stderr 仍有真 check-fail」**——这就是假绿的度量。

### 5.3 真实构建脚本已经收编这个契约（实测）

`scripts/runtime/build_guest_process_contract.sh` 按 `tests/native/nvn_*_contract.c` 的 glob 收编家族契约，
并**从文件名推导开关**（`nvn_present_contract.c` ⇒ `--nvn-present`）。**没有改这个脚本。**

```sh
sh scripts/runtime/build_guest_process_contract.sh \
  --out .local/owned-startup-p-mm-28 \
  --toolchain <主仓>/.local/minimal-ndk/toolchain \
  --resource-quota-bytes 1073741824
```

实测结果（`.local/owned-startup-p-mm-28/manifest.json`）：

- 脚本**退出码 0**，252 个 case 全部通过；
- `family_contracts` 里有 `nvn_present_contract -> {"nvn_present_errors":0,"frames_requested":1,"frames_presented":0,"failures":1,...}`；
- 该 case 的 `returncode = 0`、`timed_out = false`、命令行就是 `nvn_present_contract.host --nvn-present`；
- `android_executed: false`（**只跑宿主，没碰设备**）；本次没有传 `--main`，**没有跑 guest**。

---

## 6. NDK 交叉编译（实测）

NDK `~/Library/Android/sdk/ndk/26.1.10909125`，`--target=aarch64-linux-android34`，`-std=c11 -O2 -Wall -Wextra -Werror`：

| 文件 | 结果 |
|---|---|
| `src/graphics/nvn_device/nvn_backend_vulkan.c` | **通过** |
| `src/graphics/nvn_device/nvn_present.c` | **通过** |
| `src/graphics/nvn_device/nvn_device.c` | **通过** |

（修了一处：`vkAcquireNextImageKHR` 的 KHR 签名是 6 个参数，多一个 `VkFence`，已按 6 参调用。）

---

## 7. 真机：**没做**（设备锁在协调者手上）

`ls .local/agent-pm/` → `BOOT_LOCK_MM_A` 存在（Oct 25 02:59 创建）。
按工单 10.10 的纪律：**有锁就停下报告，不抢**。因此：

- **没有** boot 设备、**没有**装包、**没有**跑受控清屏、**没有**抓截图；
- **没有** touch 任何 boot lock（也就不存在「跑完不删锁」的问题）；
- 因此下面这些**一条都没有被证明**：swapchain 图像取到、render pass 开了、
  `vkQueueSubmit` 返回 `VK_SUCCESS`、`vkQueuePresentKHR` 返回 `VK_SUCCESS`、fence 等到了、
  `VK_KHR_swapchain` 在真机上被启用、`render_passes_built/reused` 与 `framebuffers_built/reused` 的**真机数值**。

协调者把设备交回来后，需要补的是**宿主探针**（用宿主这份代码编 APK、跑一次固定颜色的受控清屏、
读回 `VK_SUCCESS` 与 fence 等待时间），**不是** guest 的画面。

---

## 8. **没有**证明的东西（明确写清）

- **没有出帧、没有标题画面、没有可玩、没有游戏画面。** 这一轮最多是「guest 的清屏命令第一次到了屏幕」，**而这一轮连那一步也没做到**（真机未做 + replay 侧还没接颜色）。
- **没有**证明 guest 的命令到了屏幕：replay 侧的状态表还没有清屏颜色字段，颜色接口做好了但**还没有调用方**。
- **没有**在真机上跑过 Vulkan 呈现路径：第 3 节里 render pass / framebuffer 的复用与 fence 计时**是代码级的**，宿主没有 Vulkan，契约无法执行这段。
- 宿主契约证明的是**契约层的行为**（接口存在 / 参数被检查 / 失败被点名 / 失败不产生已显示假象 / 颜色逐位来自调用方），
  **不是**「画面真的上屏」。
- 截图（如果协调者后面抓了）**只能证明屏幕上有东西**，不能证明那是游戏画面。

---

## 9. 协调者要接的三件事

1. **把 `nvn_present.c` 加进 Android 构建**：`src/android/native/CMakeLists.txt` 的 `ffrs_guest_runtime`
   源文件是**显式列表**（不是 glob），当前**不含** `nvn_present.c` ⇒ `nvn_present_frame` 还不进 APK。
   该文件写权限在总控，本工单没碰。加一行即可：
   `"${FFRS_GUEST_NVN_DIR}/nvn_present.c"`
2. **接清屏颜色**：replay 侧从 guest 命令流里取 `OP_CLEAR_COLOR` 的四个 float，
   调 `nvn_present_frame(device, rgba)`。**唯一入口就是这一个函数**，颜色必须由调用方给。
3. **让 surface 真的建出来**：`nvn_vulkan_create_window_surface` 需要一个 `ANativeWindow`；
   NN 的 window 家族 / `nn::vi` 桥要把它交过来，之后呈现路径才有 swapchain 可用。

另：本分支基于 `971df74`，基线已到 `6d6cbb1`，合并前 rebase 一下即可（已核对无冲突）。

---

## 10. 证据路径

| 证据 | 路径（worktree `.worktrees/p-mm-28b/`） |
|---|---|
| 三变体自证日志 | `.local/logs/p-mm-28-present-selfproof.log` |
| 四个退出码的 stdout / stderr | `.local/selfproof/nvn_present_{baseline,broken_fixed,broken_prefix,stub_lies}.{stdout,stderr}` |
| 生成的三个变体源 | `.local/selfproof/nvn_present_{broken_fixed,broken_prefix,stub_lies}.c` |
| 宿主契约二进制 | `.local/build-p-mm-28b/nvn_present_contract.host` |
| NDK 交叉编译产物 | `.local/build-p-mm-28b/nvn_backend_vulkan.android.o`、`nvn_present.android.o`、`nvn_device.android.o` |
| 真实构建脚本（宿主部分） | `.local/logs/p-mm-28-real-build.log`、`.local/owned-startup-p-mm-28/manifest.json`（`family_contracts.nvn_present_contract`） |
| 设备锁（未动） | 主仓 `.local/agent-pm/BOOT_LOCK_MM_A`（协调者的锁，本工单没碰） |

复现命令：

```sh
cd .worktrees/p-mm-28b
bash scripts/runtime/selfproof_nvn_present.sh        # 四个退出码
NDK=~/Library/Android/sdk/ndk/26.1.10909125
$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang --target=aarch64-linux-android34 \
  -std=c11 -O2 -Wall -Wextra -Werror \
  -c src/graphics/nvn_device/nvn_backend_vulkan.c -o /tmp/v.o
```

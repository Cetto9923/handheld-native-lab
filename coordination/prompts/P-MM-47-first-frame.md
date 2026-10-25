# 工单 P-MM-47｜MiniMax Code｜S1-c：replay 把 guest 的命令流变成真正的 Vulkan 命令（第一帧）

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `3517404`
> 影响：**这是「帧数从 0 到 1」的那一步**。replay 目前**只解码**命令包、**一条 Vulkan 命令都不发**；呈现路径（P-MM-28b）已经有 acquire / render pass / submit / present，但**没有调用方**。

---

## 0. 目标（一句话）

让 replay 把 **guest 自己录下来的命令**变成**真正的 Vulkan 命令**，并在**同一个 VkDevice / queue** 上提交、**把帧送上屏幕**。

**第一帧的颜色必须是 guest 自己的清屏命令解释出来的颜色，不是宿主探针的颜色。** 这是硬红线。

---

## 1. 现状（自己先读一遍确认）

- `src/graphics/nvn_device/nvn_command_replay.c`：**能解码**命令包（49 个 opcode），**不发一条 Vulkan 命令**。它已经能按池基址读纹理/采样器句柄（`last_texture` / `last_sampler`）。
- `src/graphics/nvn_device/nvn_command_buffer.h`：**`nvn_command_buffer_get_package(handle)`** 给出那个不可变的命令包。
- `src/graphics/nvn_device/nvn_present.c`（P-MM-28b）：**`nvn_present_frame(device, rgba)`** —— acquire → render pass → 用给定颜色清 → submit → present。**颜色按位使用，不夹取、不改写。**
- `src/graphics/nvn_device/nvn_backend_vulkan.c`：device / queue / surface / swapchain 已有；**没有** acquire / submit / present 的调用方。
- `src/graphics/nvn_device/nvn_texture.c` / `nvn_texture_view.c` / `nvn_sampler.c`：**宿主侧**的纹理/采样器状态（guest 对象的描述）。
- `src/graphics/nvn_device/nvn_texture_pool.c`：**`nvn_texture_pool_texture_at(pool, index)`** —— 把 replay 读到的**号码**变成宿主纹理状态。**这正是 S1-c 要用的那一步。**

---

## 2. 分级验收（**逐级报，不许跳级**）

每一级都要**分别**回答「做到了没有」，**不许把「结构检查通过 / 捕获成功 / 缓存命中 / module 创建 / pipeline 创建 / draw 消费」混成一句「有帧了」**。

| 级 | 内容 | 判定 |
|---|---|---|
| **L1** | replay 读到 guest 的 `OP_CLEAR_COLOR`，把它**按位**交给呈现路径，`nvn_present_frame` 在**同一个 VkDevice / queue** 上真正 submit + present | 真机：**窗口里出现 guest 命令里的那个颜色**（截图取像素比对） |
| **L2** | replay 读到 guest 的**纹理绑定**与**采样器绑定**，用 `nvn_texture_pool_texture_at()` 把号码变成宿主纹理，建 **descriptor set**，绑定到 pipeline | 真机：**descriptor set 真的绑上了**（不报 `VK_ERROR_DESCRIPTOR_SET_NOT_ALLOCATED`） |
| **L3** | replay 读到 guest 的 **draw**，用**这个游戏自己的 shader**（经能力+扩展修正后）建 pipeline 并 `vkCmdDraw` | 真机：**draw 被 GPU 消费**（submit/wait 都 `VK_SUCCESS`） |

**L1 没做到就不许声称 L2/L3 做到。**

---

## 3. 关键事实（**别重新推导，去读**）

1. **驱动的拒收真因已查明（P-MM-29，实测）**：
   - 游戏的 **fragment 模块**声明了 `FragmentShaderPixelInterlockEXT` **能力**和 `EXT_fragment_shader_interlock` **扩展**，设备两者都没有 ⇒ **两个都去掉**才建成（**只去能力仍 -3**）。
   - 游戏的 **vertex 模块**声明了 `Float64`，设备 `shaderFloat64=false` ⇒ **去掉这一个词**才建成。
   - 修正工具：`scripts/graphics/fix_spirv_capabilities.py`，现在**支持 `--drop-extension`**；用下面这条命令得到的模块，与 P-MM-29 在设备上验证过的交付件**逐字节一致**（四个模块实测 sha256 相同）：
     ```bash
     python3 scripts/graphics/fix_spirv_capabilities.py \
       --in <原模块>.spv --out <修正>.spv \
       --drop Float64 --drop FragmentShaderPixelInterlockEXT \
       --drop-extension EXT_fragment_shader_interlock
     ```
     **产物链必须走这个工具**，不许再用手工改过的 `spv-fixed/`。
2. **参考树** `.local/refs/nxnative/dm/gfx/nvn/`（同款游戏，MIT）：
   - `nvn_obj.h`：`op_hdr`、49 个 opcode、33 个 `p_*` 载荷；`p_bind_tex1{stage,slot,h,tbase,sbase}`。
   - `nvn_mtl.m`：**逐 opcode 的语义**（每个 opcode 该干什么）。**这是你翻译命令流时的对照表。**
   - `hle_nvn.c`：`nvnQueueSubmitCommands`（L1132）、`nvnQueuePresentTexture`（L1630）、`dpool_add`/`dpool_base`（L235-246）。
3. **已知缺陷要先查**：`nvn_backend_vulkan.c` 之前**未启用设备扩展 `VK_KHR_swapchain`** 却直接调 `vkCreateSwapchainKHR`（P-MM-28b 报告里点名过）。**先查现状再决定要不要改。**

---

## 4. 写范围（**严格**）

- **你独占写**：`src/graphics/nvn_device/nvn_command_replay.c`、`src/graphics/nvn_device/nvn_command_replay.h`、
  `src/graphics/nvn_device/nvn_backend_vulkan.c`、`src/graphics/nvn_device/nvn_backend_vulkan.h`（如无则新建）、
  `tests/native/nvn_command_replay_contract.c`（**只改你自己的那段**，不碰别的族）、
  `scripts/graphics/`（**新增**你的脚本，**不许改** `fix_spirv_capabilities.py`——那是协调者的文件）、
  `coordination/reports/T-S1C-first-frame-2026-10-25.md`。
- **你只读、**不许改**：`nvn_device.c`、`nvn_present.c`、`nvn_texture*.c`、`nvn_sampler*.c`、`nvn_queue.c`、`scripts/runtime/build_guest_process_contract.sh`、`src/android/native/CMakeLists.txt`、`tests/native/guest_process_contract.c`。
- 分支：`task/P-MM-47-first-frame`，worktree `.worktrees/p-mm-47`（自建）。**不许 push。**

---

## 5. 设备（**你会需要它**）

- 设备：`RG477M01445122`（Android 14 / Mali-G615 MC6）。**不要 root、不要刷机、不要清数据。**
- **协调者现在持有 `.local/agent-pm/BOOT_LOCK_MM_A`**。**你必须先向协调者要锁**（用 `session send` 告诉协调者你要用设备），**拿到确认后再动**；**跑完必须把锁还回去**（并写明跑了什么）。
- 真机跑：**≥300 秒**、**独立日志目录**（`.local/logs/<你的名字>-android/`）、APK 由协调者的构建脚本出（`scripts/android/build_game_window_apk.sh`）。**如果构建脚本要改（接线），把改动写在报告里，由协调者接线**，不要自己改协调者的文件。
- 截图取像素要比对**颜色**；**一次 present 或一张截图不等于可玩**。

---

## 6. 纪律

- **第一条工具调用就写文件**；先写最小可编译版本**立刻 commit**；预算过半还没 commit 就立刻 commit。**不许只交设计。**
- 契约里**不许恒真断言**，**不许硬编码 0 错误数**；期望值写死在契约里。
- **不许**用万能成功桩、**不许**绕过 guest 的 abort、**不许**把没实现的东西报成成功。
- **不许**声称标题画面 / 可玩 / 有帧 / 帧率，除非有**像素级证据**。
- 每一级都要**先在宿主上验**，再上真机。

---

## 7. 恢复点

- 基线：`work/rg477m-native` @ `3517404`。
- 交回时给出：分支、commit 列表、工作树状态、**L1/L2/L3 分别做到没有**、证据路径（真机日志 + 截图 + 像素值）、**没有证明的东西**。

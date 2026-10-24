# 工单 P-MM-23｜worker｜真机 shader 分级验证 + Vulkan 目标环境修复

> **状态**：正式派工（总控：Codex 只读复核后的追加纠偏第 1 项收尾）
> 基线：`work/rg477m-native` @ **`aa6ffbb`**（工作区干净；两个 worker 正在改 `nvn_command_buffer.*` 与 `nvn_{sampler,texture}_builder.*`，**不要碰那三处**）
> 纠偏原文：`/Users/yuyan9923/Documents/Codex/2026-10-25/kiro-http-localhost-5476-chat-diagnosing/outputs/MiniMAX执行中复核-追加纠偏.md` §1
> 已有报告：`coordination/reports/T-SHADER-identity-2026-10-25.md`（末节「What the SPIR-V is worth, and what it is not」）

---

## 0. 仓库与环境（必须）

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- 设备 `adb -s RG477M01445122`；不 root / 不刷机 / 不清数据；**不 push**
- 先读：`AGENTS.md` → `coordination/HANDOFF.md` 顶部 → **本工单** → `coordination/reports/T-SHADER-identity-2026-10-25.md` → `src/graphics/vulkan_probe/vulkan_transfer_probe.c`（**这台设备上已经有的 Vulkan 探针，照它的形态写**）→ `scripts/graphics/build_vulkan_transfer_android.sh`（**照它的构建脚本写**）
- `.local/romfs/**` 只读；**不要调用 agy**；不 `git add .`；**不要 commit**（总控统一提交）
- **真机 boot / 装包前 `touch .local/agent-pm/BOOT_LOCK_MM_A`，结束后删掉**（先确认没有别的 owner 在用设备）

---

## 1. 事实（本单的事实基础，不要再假设）

1. **默认 SPIR-V 目标环境：6/6 通过。Vulkan 目标环境（`--target-env vulkan1.1` 与 `vulkan1.3`）4/6。**
   两个失败的模块是**计算着色器**，报同一个错：
   ```
   error: line 5381: Capability StorageImageWriteWithoutFormat is required to write to storage image
     OpImageWrite %5167 %5168 %5173 None
   ```
   文件：`.local/shader-verify/spv/41b20b5f09269ad8.spv`、`.local/shader-verify/spv/86affd63144799de.spv`。
   命令、退出码与完整输出：`.local/shader-verify/val/`。
2. **六个样本的来源**：`.local/shader-verify/spv/*.spv` 与 `.local/shader-verify/out/`，由
   `.local/shader-verify/run_verify.sh` 从**游戏自己的 IoStore 容器**产出；翻译器是
   `.local/refs/nxnative/dm/gfx/tools/dmsh_xlate_bin/dmsh_xlate`（vendored Ryujinx 翻译器，MIT）。
3. **本机没有 Vulkan**（宿主是 macOS），**Vulkan 只能在设备上执行**。
4. **设备上已有的 Vulkan 底座**：`src/graphics/nvn_device/nvn_backend_vulkan.c`（真 device/queue/surface/swapchain）与 `src/graphics/vulkan_probe/vulkan_transfer_probe.c`（独立探针，已在设备上跑过）。
5. **不要机械补 capability**。补声明之前必须先确定三件事：
   - 本产品**实际使用的 Vulkan / SPIR-V 目标环境**（设备 `ro.hardware.vulkan=mali`，API 版本要**实测**）；
   - 设备的 `shaderStorageImageWriteWithoutFormat` **支持**与**启用**状态（支持 ≠ 启用）；
   - 这两个计算着色器的 storage image **格式**是从**游戏的 control 段**来的，不是猜的。

---

## 2. 本单要做的事

### 2.1 先查设备的 Vulkan 事实（不是猜的）

在 RG477M 上实测并落盘：

- `vkEnumerateInstanceVersion` / `vkEnumeratePhysicalDevices` 得到的 **API 版本**与**驱动版本**；
- `VkPhysicalDeviceFeatures` / `VkPhysicalDeviceVulkan11Features` / `VkPhysicalDeviceVulkan12Features` 里
  `shaderStorageImageWriteWithoutFormat` / `storageImageWriteWithoutFormat` 的**支持位**；
- **本运行时实际创建 device 时启用了哪些 feature**（`nvn_backend_vulkan.c` 现在的 `device_info`）——「查询支持」≠「创建时启用」，这两个要**分别记录**；
- 设备能接受的 **SPIR-V 版本**与 **shader 扩展**（`VK_KHR_shader_*` 之类）实际可用哪些。

落盘成 `.local/shader-verify/device-vulkan-facts.json`（设备派生数据，不进 git），并在报告里写清楚。

### 2.2 修 Vulkan 目标环境（按 1 的结论决定怎么修）

两个候选，**先查清再选**：

- **(A) 补 capability**：模块里声明 `StorageImageWriteWithoutFormat`，同时设备**真实启用** `shaderStorageImageWriteWithoutFormat`。
- **(B) 把格式烘进模块**：从**游戏的 control 段**取出该 storage image 的格式，让翻译器按格式输出，模块就不需要那个 capability。

**判断依据是游戏的数据**。去 `.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c` 与 `nvn_obj.h` 看**这款游戏**怎么处理 storage image 的格式（`p_bind_img` / `NVNimageHandle` 那一路），以及 `.local/refs/nxnative/dm/gfx/tools/dmsh_vk.py` / `dmsh_build.py` 怎么导出 image 的格式。**能证明用哪个就用哪个**，并在报告里说明为什么。

**不许**只加一句 capability 声明就宣布「Mali 支持」。

### 2.3 真机分级验证（本单的主交付）

在设备上，按**分级状态**给结论，**不许用一条 PASS 盖过去**：

| 级别 | 状态 | 怎么证 |
|---|---|---|
| 捕获成功 | 离线拿到真实 shader 的字节 | 已有（`K_content=174293a30c315aed` 等） |
| 缓存命中 | 运行时按 content 键找到 SPIR-V | 设备上按 key 查表，记录命中/未命中 |
| **module 创建** | **真机 `vkCreateShaderModule` 成功** | 驱动返回码 + 日志 |
| **pipeline 创建** | **真机 `vkCreateGraphicsPipelines` / `vkCreateComputePipelines` 成功** | 需要**真实的 pipeline layout 与 descriptor set layout**（从 reflection 来），不是空 layout |
| draw 消费 | 真的画了一次并被 GPU 消费 | 需要渲染目标与提交链，**本单做不到就写 NOT_RUN** |

**要求**：
- 至少对**四个已通过 Vulkan 目标环境**的模块做**真机 module 创建**。
- 对**两个计算着色器**，在 2.2 修好之后重跑 `spirv-val --target-env vulkan1.1`，把命令与退出码落盘，再做**真机 module 创建**。
- **pipeline 创建**这一级：如果 descriptor layout 还没从 reflection 推出来，**就写 NOT_RUN 并说明差什么**，不要用空 layout 骗一个「pipeline 创建成功」。
- 设备上跑：**新建一个独立的探针**（照 `vulkan_transfer_probe.c` 的形态），例如 `src/graphics/vulkan_probe/vulkan_shader_probe.c` + `scripts/graphics/build_vulkan_shader_probe_android.sh`。**不要**改 `tests/native/guest_process_contract.c`、不要改 `scripts/runtime/build_guest_process_contract.sh`、不要改 `src/graphics/nvn_device/nvn_backend_vulkan.c`（那三个是总控的）。
- 探针在**没有 Vulkan 的宿主**上必须**如实报「本构建没有 Vulkan」**，不能假装成功。
- 真机跑完，**释放 `BOOT_LOCK_MM_A`**。

---

## 3. 写入范围

| 文件 | 动作 |
|---|---|
| `src/graphics/vulkan_probe/vulkan_shader_probe.c` | 新建 |
| `scripts/graphics/build_vulkan_shader_probe_android.sh` | 新建 |
| `.local/shader-verify/**` | 随意（游戏派生数据，不进 git） |

**禁止修改**：`src/graphics/nvn_device/**`（两个 worker 正在改其中三个文件，总控保留其余）、`src/android/native/CMakeLists.txt`、`tests/native/guest_process_contract.c`、`scripts/runtime/**`、`coordination/**`、`scripts/graphics/**` 里既有文件（只新建，不改）。
如需改 `nvn_backend_vulkan.c`（例如为了拿 VkDevice），**先在报告里写明要改什么，等总控确认**；优先用探针自建 instance/device。

---

## 4. 验收

- 探针编译无警告，宿主上**如实**报「没有 Vulkan」，设备上给出分级状态。
- 设备事实 JSON 落盘，报告里有 **API 版本 / 支持位 / 实际启用位** 三栏，且**分得开**。
- 六个模块的 `spirv-val --target-env vulkan1.1` 命令与退出码落盘（修之前与修之后各一份）。
- 报告写清楚：选了 (A) 还是 (B)、**依据是什么**、真机 module 创建了几个、pipeline 创建是 PASS 还是 **NOT_RUN（差什么）**。
- **不许**声称「Mali 支持」「管线就绪」「已出帧」。

---

## 5. 收尾

- 文件留在工作区，**不要 commit**。
- 报告写在最终回答里（纯文本，≤ 60 行）。
- 设备锁用完即删。

---

## 6. 禁止

- 只补 capability 就宣布设备支持
- 用空 descriptor layout 骗一个 pipeline 成功
- 把「spirv-val 通过」说成「设备接受」
- 万能成功桩、绕过 abort
- 声称标题/可玩/游戏帧
- push / force / 动 main / 调 agy / 删 romfs / 改 `nvn_device/**` / 改契约 / 改 `scripts/runtime/**` / `git add .` / `git commit` / `git reset` / `git clean` / `git stash`

做完停下汇报。

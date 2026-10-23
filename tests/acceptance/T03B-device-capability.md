# T03B — RG477M 真机能力与宿主验证（验收用例集）

任务来源：`TASKS.md#T03B`、`docs/PROJECT.md` §目标设备 / §原生定义 / §分辨率规格。

**当前整体状态：BLOCKED_DEVICE**（本文件内所有用例均为 `BLOCKED_DEVICE`，一条都没有执行过）。
RG477M 未连接；`adb devices` 在 2026-10-23 12:47 实测为空列表。

> 重要：本文件的所有用例都**只能**在真实 RG477M 上执行。
> 模拟器、Mac、其他手机、`adb` 连接到桌面模拟器的结果，**一律不得**作为本文件任何用例的证据。
> 这与 `AGENTS.md`「真机性能 PASS 必须来自 RG477M 的真实场景」一致。

---

## 共享前置条件（DEV 组）

| 编号 | 条件 | 校验方式 |
|---|---|---|
| PRE-1 | 探针 APK 已由 T03A 交叉构建产出，ABI = `arm64-v8a`，安装成功 | `adb install` 返回码 0；`aapt dump badging` 显示 `native-code: 'arm64-v8a'` |
| PRE-2 | APK 的 SHA-256 已记录 | `shasum -a 256 <apk>` → 写入 `build.apk_sha256` |
| PRE-3 | 设备型号确认为 Anbernic RG477M，而非同名仿品 | `getprop ro.product.manufacturer` / `ro.product.model` / `ro.build.version.release` 三项均记录；营销名取自机身/系统信息 |
| PRE-4 | 设备已连电（AC 或稳定 USB），非电池供电 | `dumpsys battery` 的 `AC powered: true` |
| PRE-5 | 屏幕刷新率已固定为测试用模式 | `dumpsys display` / `dumpsys SurfaceFlinger` 读取当前 mode，写入 `display.current_mode` |
| PRE-6 | 无第二台设备/录屏进程抢占 CPU | `top` 快照留档 |

---

## DEV-001 设备身份与 ABI

- **前置条件**：PRE-1…PRE-6
- **步骤**：
  1. 安装并启动探针。
  2. 读取 `ro.product.manufacturer`、`ro.product.model`、`ro.product.device`、`ro.product.board`、`ro.hardware`、`ro.product.cpu.abi`、`ro.product.cpu.abilist`、`ro.build.version.release`、`ro.build.version.sdk`、`ro.build.fingerprint`。
  3. 读取 `ro.build.version.release` 与 CPU 实现者（`/proc/cpuinfo`）。
- **期望**：主 ABI = `arm64-v8a`；系统为 Android 14（sdk 34）；SoC 标识为 Dimensity 8300（若设备不暴露该串则记 `null`，不得推测）。
- **判定口径**：`device.abi == "arm64-v8a"` **且** `device.sdk_int == 34` **且** `device.android_version` 以 `"14"` 开头 ⇒ PASS。任一不满足 ⇒ FAIL。`soс_model` 为 `null` **不**构成 FAIL，但必须在 `limitations` 写明"SoC 型号未由系统直接暴露，未独立核实"。
- **证据产物**：`evidence/device/<capture_id>/device-capability.json`（按 `schema/device-capability.schema.json`）、`evidence/device/<capture_id>/getprop.txt`
- **状态**：BLOCKED_DEVICE

## DEV-002 内存与可用 RAM

- **前置条件**：DEV-001 PASS
- **步骤**：
  1. 读取 `ActivityManager.MemoryInfo`：`totalMem`、`availMem`、`threshold`、`lowMemory`。
  2. 读取 `/proc/meminfo` 的 `MemTotal`、`MemAvailable`、`SwapTotal`。
  3. 读取 `Runtime.maxMemory()`（Java 堆上限）与 native 堆上限（可测时）。
- **期望**：记录 12GB 级 LPDDR5 的实际值；`availMem` 为空载可用值。
- **判定口径**：**本用例只做记录，不设阈值**（硬件规格不是被测对象）。判定为 PASS 的条件是「三处内存读数互相自洽（`totalMem` 与 `MemTotal` 同量级，差 < 5%）」且字段齐全；不自洽 ⇒ FAIL（说明采集有问题）。
- **证据产物**：同上 JSON 的 `memory` 段 + `meminfo.txt`
- **状态**：BLOCKED_DEVICE

## DEV-003 存储空间

- **前置条件**：DEV-001 PASS
- **步骤**：
  1. 读取 app 数据目录（`/data/data/<pkg>` 所在卷）的总字节与可用字节。
  2. 读取安装分区（只读区）总量。
  3. 记录文件系统类型（`statfs` 的 `f_type`）。
- **期望**：可用空间足以安装最终交付（游戏资源 + APK）。
- **判定口径**：记录项。PASS 条件 = 读数成功且 `data_dir_available_bytes` 与 `stat` 实测一致（差 0）。
- **证据产物**：JSON 的 `storage` 段 + `df.txt`
- **状态**：BLOCKED_DEVICE

## DEV-004 Vulkan 基础信息与 feature/limit/formats/extension 全量清单

- **前置条件**：DEV-001 PASS；探针内含 Vulkan 初始化 + `vkGetPhysicalDeviceFeatures2` 全结构链查询
- **步骤**：
  1. 创建 instance，枚举物理设备，记录 `apiVersion`、`driverVersion`、`deviceName`、`vendorID`、`deviceID`、`deviceType`。
  2. 用 `vkGetPhysicalDeviceFeatures2` 依次查询 `VkPhysicalDeviceFeatures`、`Vulkan11Features`、`Vulkan12Features`、`Vulkan13Features`、`Vulkan14Features`（存在才查）。
  3. 用 `vkGetPhysicalDeviceProperties2` 查询 `VkPhysicalDeviceLimits` 与 `VkPhysicalDeviceMaintenanceProperties3`。
  4. 用 `vkEnumerateDeviceExtensionProperties` 列出全部设备扩展（排序）。
  5. 对候选格式集逐个查 `vkGetPhysicalDeviceFormatProperties`。
- **期望**：全部字段有值或显式 `null`；`features_probed` 列出实际查询过的结构。
- **判定口径**：
  - PASS 条件：`vulkan.available == true` **且** `features_probed` 非空 **且** `extensions` 与 `formats` 数组非空 **且** `limitations` 明确列出"未查询的结构"。
  - **不得**因为某 feature 为 `false` 而判 FAIL——feature 清单是事实记录，不是及格线。
  - **未查询的 feature 记为 `unknown`，不得写成 `false`。**
- **证据产物**：JSON 的 `vulkan` 段（完整）、`vulkan-limits.txt`、`vulkan-extensions.txt`
- **状态**：BLOCKED_DEVICE

## DEV-005 GPU 必需特性可用性核对（相对实现的实际需求）

- **前置条件**：DEV-004 PASS **且** T02/T04 已确定实际使用的图形接口
- **步骤**：以 T04 实现的**实际** Vulkan 用法为准，逐条核对：所需 feature 位、所需 extension、所需 format 组合、所需 queue family、所需 image usage。
- **期望**：实现所需的每一项在设备上 `true`/可用。
- **判定口径**：PASS 条件 = 实现用到的 feature/extension/format 集合 ⊆ 设备支持集合，且差集为空。差集非空 ⇒ FAIL，并列出缺失项。
- **证据产物**：`evidence/device/<capture_id>/vulkan-requirement-gap.json`、`docs/analysis/vulkan-requirements.md`
- **状态**：BLOCKED_DEVICE（且依赖 T04）

## DEV-006 显示模式、分辨率、刷新率

- **前置条件**：DEV-001 PASS
- **步骤**：
  1. `dumpsys display` / `dumpsys SurfaceFlinger --display-id` 读取物理分辨率与全部 mode。
  2. 读取当前 mode 的宽、高、刷新率。
  3. 读取 `wm density`。
  4. 用 `Surface.FRAME_RATE` / `preferredDisplayModeId` 读取系统当前选择的刷新率。
- **期望**：物理面板 1280×960、4:3；系统刷新率 60 或 120。
- **判定口径**：PASS 条件 = `physical_resolution == {1280, 960}` **且** `current_mode.refresh_rate_hz ∈ {60, 120}`（真机实际值，以实测为准记录；若为其它值，如 90，必须记录并由用户确认是否可接受，不得静默改口径）。
- **注意**：`panel_refresh_rate_hz` 若为 120，后续 T06 帧统计必须启用"重复呈现去重"规则（见 `schema/frametime-format.md` §3）。
- **证据产物**：JSON 的 `display` 段 + `dumpsys-display.txt`
- **状态**：BLOCKED_DEVICE

## DEV-007 手柄按键与轴向

- **前置条件**：DEV-001 PASS；实体手柄已连接
- **步骤**：
  1. 枚举 `InputDevice`，记录每个 gamepad 的 name / vendorId / productId / source。
  2. 逐轴读取 min/max/value/deadzone/flat/fuzz。
  3. 逐键读取按下状态；测试触发确认/取消/菜单/返回/摇杆/十字键。
  4. 记录轴的**静止值**（手柄放平）和**极限值**（推到底）。
- **期望**：至少 1 个 gamepad；两轴各能覆盖 [-1, 1]（或等价整数范围）；确认/取消/菜单/返回键存在且可区分。
- **判定口径**：
  - PASS 条件 = 设备数 ≥ 1 **且** 每个必需按键都能被独立触发并读出不同值 **且** 摇杆静止值 ≈ 0（|v| ≤ deadzone）、极限可达 ±1（≥ 0.95）。
  - 若静止值显著偏置（|v| > 0.1 且 deadzone 覆盖不到）⇒ 记录为**缺陷**，但**不**判 DEV-007 FAIL，改为在 T05 的输入映射用例中判 FAIL。
- **证据产物**：JSON 的 `input` 段 + `gamepad-axes.csv`
- **状态**：BLOCKED_DEVICE

## DEV-008 音频输出与延迟

- **前置条件**：DEV-001 PASS
- **步骤**：
  1. 枚举 `AudioManager` 输出设备，记录采样率、声道数。
  2. 读取 `PROPERTY_OUTPUT_SAMPLE_RATE` / `PROPERTY_OUTPUT_FRAMES_PER_BUFFER`。
  3. 用 `AAudio` 打开低延迟流，记录实际打开的采样率、帧数、是否低延迟路径。
- **期望**：输出采样率、缓冲大小、延迟有据可查。
- **判定口径**：记录项。PASS 条件 = 采集成功且 `native_sample_rate_hz` 与设备声明一致。
- **证据产物**：JSON 的 `audio` 段 + `audio-probe.txt`
- **状态**：BLOCKED_DEVICE

## DEV-009 生命周期：后台/前台、休眠/唤醒

- **前置条件**：DEV-001 PASS；探针已声明并处理 `onPause` / `onStop` / `onResume` / `onSaveInstanceState`
- **步骤**：
  1. 应用在前台运行 → `adb shell input keyevent KEYCODE_HOME` → 等待 10 s → 观察 `onPause`/`onStop` 是否触发、EGL/Vulkan surface 是否销毁。
  2. 回到前台 → 记录 `onResume` 到首帧呈现的延迟（ms）；确认 surface 是否重建、上下文是否丢失。
  3. 设备休眠：`adb shell input keyevent KEYCODE_SLEEP` → 60 s → `KEYCODE_WAKEUP` → 解锁 → 回到前台 → 记录恢复延迟与状态一致性。
- **期望**：后台/前台切换不崩溃；恢复后画面与状态正常。
- **判定口径**：
  - PASS 条件 = 两次切换均无崩溃/ANR（`logcat` 无 FATAL EXCEPTION / ANR），且恢复后 `on_resume_after_background_ms`、`on_resume_after_sleep_ms` 均有实测值。
  - 若恢复后黑屏/闪退 ⇒ FAIL，并附 `logcat` 片段。
- **证据产物**：JSON 的 `lifecycle` 段 + `lifecycle-logcat.txt`
- **状态**：BLOCKED_DEVICE

## DEV-010 探针本身不是游戏（防污染）

- **前置条件**：DEV-004 PASS
- **步骤**：核对探针 APK 的 `AndroidManifest`、native 库清单与启动日志。
- **期望**：探针只有空窗口/测试场景，不含任何来自游戏本体的资源或代码。
- **判定口径**：PASS 条件 = native 库清单仅含探针自身库（无游戏派生产物），且 manifest 不声明游戏资源。**此用例是 T04/T06 的前置**，用来防止用探针空窗口冒充游戏运行。
- **证据产物**：`evidence/device/<capture_id>/probe-manifest.txt`、APK 内容清单
- **状态**：BLOCKED_DEVICE

---

## 组级出口条件

DEV-001…DEV-010 全部 PASS **且** `device-capability.json` 通过 `schema/device-capability.schema.json` 校验，
T03B 才可标记为通过（`TASKS.md`：真机宿主与所需能力验证通过）。

**明确不做的事**：本组不产生任何帧率结论、不产生任何画面结论。空场景、菜单、
重复呈现帧、系统刷新率都**不得**用于满足 PROJECT.md 的性能指标。

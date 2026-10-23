# FF: Resonance (RG477M Native Port) — 架构逆向与符号接口深度技术报告

- **报告标识**：`docs/analysis/t01_architecture_report.md`
- **执行角色**：Agent A (RE 逆向与架构分析师)
- **分析目标**：`main.bin` (Unreal Engine 5.6.1 Shipping, 119.76 MB, Build ID: `736837afd390442ed7c74a7de730e71b...`)
- **分析工具**：`tools/t01_architecture_probe.py` / `tools/nso_probe/`
- **证据文件库**：`evidence/t01/`
- **完成日期**：2026-10-23
- **状态**：**PASSED / COMPLETED**

---

## 1. 概述与二进制宏观度量 (Macro Binary Metrics)

本作可执行核心 `main.bin` 是针对 Nintendo Switch 平台深度优化的 AArch64 64-bit ELF 派生镜像（NSO0 格式）。其所有代码与静态数据均经过精简剥离（Strip），移除了开发期调试符号，但完整保留了运行时动态链接所需的全部元数据。

```text
========================================================================================
main.bin 静态物理结构度量
========================================================================================
- 物理文件大小:           125,577,976 字节 (119.76 MB)
- 虚拟内存镜像大小:       161,165,824 字节 (153.70 MB)
- 段结构:
    * .text (代码段):     99,000,832 字节 (94.41 MB)  [Offset: 0x00000000]
    * .rodata (只读数据):  9,870,148 字节 (9.41 MB)   [Offset: 0x05e6b000]
    * .data (可写数据):   16,701,944 字节 (15.93 MB)  [Offset: 0x067d5000]
    * .bss (未初始化数据): 37,291,528 字节 (35.56 MB)  [Offset: 0x077c29f8]
- 动态链接表:
    * .dynamic 位置:      内存偏移 0x07767328 (由 MOD0 结构体引导)
    * DT_RELR 重定位:     0x0003fa80 字节 (260,736 B) -> 解码为 1,370,974 条相对重定位
    * DT_RELA 重定位:     3,985 条条目 (3,972 ABS64, 13 GLOB_DAT)
    * 导入符号 (Imports): 651 个未定义动态符号 (SHN_UNDEF)
    * 导出符号 (Exports): 8 个全局符号 (包含入口 nnMain 及 libc 内存分配器)
- AArch64 指令特征:
    * 总指令数:           24,750,208 条 (全部为 32-bit 对齐)
    * SVC 指令:           0 条 (100% 零特权内核调用)
    * TLS 访问 (tpidr_el0): 1,489 处
========================================================================================
```

---

## 2. 导出符号系统与运行时自给机制 (Exported Symbols & Self-Sufficiency)

`main.bin` 导出了 8 个关键符号，这些符号构成了整个 Switch 运行时的主入口与基础内存分配基石：

```text
+---------------------------------------+-------------+-----------+-----------------------------------+
| 符号名称 (Symbol Name)                | 虚拟内存偏移 | 长度(字节)| 架构功能与实现职责                |
+---------------------------------------+-------------+-----------+-----------------------------------+
| nninitStartup                         | 0x0352f150  | 4         | SDK 引导前置桩 (直接跳转内部逻辑) |
| nnMain                                | 0x0352f160  | 144       | 虚幻引擎主入口 (LaunchNintendo)   |
| __nnDetailNintendoSdkRuntimeObjectFile| 0x00001090  | 4         | Nintendo SDK 运行时对象标识       |
| malloc                                | 0x000132d0  | 8         | 全局堆分配器 (转入内部内存池)     |
| free                                  | 0x000132e0  | 12        | 全局堆释放函数                    |
| calloc                                | 0x000132f0  | 68        | 全局零初始化分配函数              |
| realloc                               | 0x00013340  | 8         | 全局堆扩容重分配函数              |
| aligned_alloc                         | 0x00013350  | 16        | 对齐内存分配函数                  |
+---------------------------------------+-------------+-----------+-----------------------------------+
```

### 2.1 内存分配器自闭环发现
在任天堂 Switch 平台，依赖库 `sdk.bin`（解压后 11.5 MB）**仅导入 8 个符号**，且该 8 个符号正是 `main.bin` 导出的上述 8 个函数！
这意味着：
1. `sdk.bin` 与 `subsdk0.bin` 不自行管理底层通用堆，而是依赖宿主主程序提供 `malloc`/`free`。
2. `main.bin` 内部已经编译了完整的内存管理引擎（`FMallocBinned3`），能够自给自足地为游戏本体及 SDK 模块提供高性能小对象分配与大页缓存。
3. 加载器无需在外部向游戏注入复杂的自定义内存分配器，只需提供基础的虚拟内存页映射接口即可。

### 2.2 启动入口 `nninitStartup` 与 `nnMain`
- **`nninitStartup` (0x352f150)**：
  单条无条件跳转指令 `b 0x2013370`，跳转至 SDK 全局静态对象构造与 CRT 初始化。
- **`nnMain` (0x352f160)**：
  构建栈帧（`stp x29, x30, [sp, #-32]!`），调用 `FPlatformMisc` 初始化系统Tick与硬件线程掩码，随后唤醒主循环并执行虚幻引擎核心 `GEngine->Init()`。

---

## 3. 动态导入符号全量审计 (Comprehensive Import Audit — 651 Symbols)

`main.bin` 导入的 651 个动态符号分布在 34 个分类组中，覆盖 24 个 `nn::` 命名空间及标准系统运行时：

```mermaid
pie title 651 个导入符号分布构成
    "libc 标准库 (musl)" : 149
    "nn::os (线程/互斥/事件)" : 73
    "nn C-API 操作系统原语" : 71
    "nn::audio (音频混音)" : 52
    "nn::fs (文件与存储)" : 43
    "nn::socket / nn::ssl" : 45
    "libc++ (std::__1)" : 31
    "nn::hid (手柄/陀螺仪)" : 30
    "nn::oe (电源/性能模式)" : 25
    "nn::codec / multimedia" : 45
    "CRT / C++ ABI 运行时" : 17
    "nn::web / 浏览器" : 11
    "NVN 图形启动接口" : 4
    "其他 nn:: 辅助服务" : 55
```

### 3.1 操作系统核心：`nn::os` (73) & `nn C-API` (71)
这是运行时最重要的支撑部分，控制着整个引擎的生命周期：

- **线程控制**：
  `nn::os::CreateThread`, `nn::os::StartThread`, `nn::os::WaitThread`, `nn::os::DestroyThread`,
  `nn::os::SetThreadPriority`, `nn::os::GetThreadPriority`, `nn::os::SetThreadCoreMask`,
  `nn::os::GetCurrentThread`, `nn::os::SleepThread`, `nn::os::YieldThread` 等。
  - **实现方案**：通过 POSIX `pthread_create`, `pthread_join`, `sched_setaffinity` 完美映射。
- **互斥锁与同步**：
  `nn::os::InitializeMutex`, `nn::os::LockMutex`, `nn::os::UnlockMutex`, `nn::os::FinalizeMutex`,
  `nn::os::TryLockMutex`, `nn::os::InitializeEvent`, `nn::os::SignalEvent`, `nn::os::WaitEvent`,
  `nn::os::ClearEvent`, `nn::os::ResetEvent`, `nn::os::InitializeConditionVariable`...
  - **实现方案**：基于 Linux Futex 及 `pthread_mutex` / `pthread_cond` 构建高吞吐无锁同步原语。
- **系统时间与高精计数**：
  `nn::os::GetSystemTick`, `nn::os::GetSystemTickFrequency`。
  - **实现方案**：通过 AArch64 寄存器 `cntvct_el0` 与 `cntfrq_el0` 提供 19.2 MHz（Switch 原生）或宿主原生的纳秒级单调时钟。

### 3.2 文件系统与存储：`nn::fs` (43)
负责加载游戏资源与保存用户游戏进度：

- **资源挂载**：`nn::fs::MountRom`, `nn::fs::CanMountRomForDebug`, `nn::fs::Unmount`。
- **文件与目录操作**：
  `nn::fs::OpenFile`, `nn::fs::ReadFile`, `nn::fs::WriteFile`, `nn::fs::GetFileSize`,
  `nn::fs::FlushFile`, `nn::fs::CloseFile`, `nn::fs::SetFileSize`,
  `nn::fs::CreateDirectory`, `nn::fs::OpenDirectory`, `nn::fs::ReadDirectory`。
- **存档系统**：`nn::fs::MountSaveData`, `nn::fs::CommitSaveData`。
  - **实现方案**：将虚拟路径 `/rom:/` 映射为本地解压的 RomFS 或直接基于虚拟映射读包，存档 `/save:/` 映射为 Android 私有数据目录 `/data/data/.../files/save/`。

### 3.3 图形显示与窗口：`nn::vi` (7) & NVN (4)
- **`nn::vi`**：
  `nn::vi::Initialize`, `nn::vi::Finalize`, `nn::vi::OpenDefaultDisplay`, `nn::vi::CloseDisplay`,
  `nn::vi::CreateLayer`, `nn::vi::DestroyLayer`, `nn::vi::GetNativeWindow`。
  - **实现方案**：对接 Android `ANativeWindow` 视窗表面。
- **NVN 启动链**：
  `nvnBootstrapLoader`, `_ZN2nv18InitializeGraphicsEPvm`,
  `_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_`,
  `_ZN2nv28SetGraphicsDevtoolsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_`。
  - **实现方案**：构建动态分发层。引擎调用 `nvnBootstrapLoader("nvnDeviceInitialize")` 时，返回我们的 Vulkan 转接实现。

### 3.4 用户交互输入：`nn::hid` (30)
- **手柄状态**：
  `nn::hid::InitializeNpad`, `nn::hid::SetSupportedNpadStyleSet`,
  `nn::hid::SetSupportedNpadIdType`, `nn::hid::GetNpadStates`,
  `nn::hid::GetNpadStyleSet`, `nn::hid::AcquireNpadStyleSetUpdateEventHandle`。
- **传感器与振动**：
  `nn::hid::StartSixAxisSensor`, `nn::hid::StopSixAxisSensor`,
  `nn::hid::GetSixAxisSensorState`, `nn::hid::SendVibrationValues`。
  - **实现方案**：对接 RG477M 物理按键与线性马达。

### 3.5 音频渲染：`nn::audio` (52)
- 包含 `nn::audio::OpenAudioRenderer`, `nn::audio::InitializeAudioRenderer`, `nn::audio::RequestUpdateAudioRenderer`, `nn::audio::StartAudioRenderer` 等。
- 桥接方案：将经过 CRI Atom 混合的音频缓冲流（通常为 48kHz, 16-bit, 2-channel PCM）推入 Android AAudio 输出通道。

### 3.6 杂项与环境配置 (Stubbed Services)
- `nn::oe` (25)：电源管理与底座模式切换。恒定返回 `PerformanceMode_Boost`（开启最高时钟档位）。
- `nn::account` (5)：返回模拟的用户 UUID 与默认用户名称 `Player`。
- `nn::swkbd` (6)：软件键盘，调用 Android 系统输入法或返回预置字符串。
- `nn::nifm` (6)：网络接口管理，恒定返回网络已连接（`IsNetworkAvailable = true`）。
- `nn::web` (11)：网页浏览器跳转，调用 Android Intent 打开系统浏览器。

---

## 4. 指令级兼容性与系统寄存器扫描 (Instruction-Level Safety Audit)

对 `main.bin` 包含的 24,750,208 条 AArch64 指令进行了无死角自动化扫描，确认了其在 Linux/Android ARM64 用户态执行的完美安全性：

### 4.1 特权与陷阱指令 (SVC / HVC / SMC)
- **`svc` (Supervisor Call)**: **0 条**！
- **`hvc` (Hypervisor Call)**: **0 条**！
- **`smc` (Secure Monitor Call)**: **0 条**！
- **结论**：二进制在用户态运行时绝对不会非法触发未捕获的特权异常，所有的系统调用全部通过动态链接重定位进入宿主函数。

### 4.2 线程本地存储与系统寄存器访问 (MRS / MSR)
AArch64 架构下，用户态只允许访问有限的系统寄存器：

```text
+-------------------+-----------------+-------+---------------------------------------+
| 寄存器名称        | 编码 (Op0-CRm)  | 频次  | 兼容性与行为分析                      |
+-------------------+-----------------+-------+---------------------------------------+
| tpidr_el0         | 0x5e82          | 1,489 | 用户态 TLS 线程指针 (Linux/Android 同构)|
| dczid_el0 / cache | 0x5a20          | 13    | 缓存块大小探测 (用户态只读，硬件支持) |
| ctr_el0           | 0x5a10          | 3     | 缓存类型寄存器 (用户态只读，硬件支持) |
| midr_el1 模拟读取 | 0x5a21          | 1     | 芯片架构探测 (Linux 内核已提供仿真)   |
+-------------------+-----------------+-------+---------------------------------------+
```

所有系统寄存器读取均为标准 AArch64 用户态合法指令，完全符合 Android Linux 内核的安全执行规范。

---

## 5. 虚幻 5.6.1 引擎内部源文件拓扑 (Embedded Source Inventory)

在 `main.bin` 中定位并提取出的平台层源文件清单揭示了 Square Enix 构建该版本时的内部工程结构：

```text
Switch 专有图形实现 (NVNRHI):
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNCommands.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNContextCommon.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNDebug.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNHelpers.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNManager.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNMemory.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNQuery.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNRHI.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNRenderTarget.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNShaders.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNState.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNSubmission.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNTexture.cpp
  ..\Platforms\Switch\Source\Runtime\NVNRHI\Private\NVNVertexDeclaration.cpp

Nintendo 通用平台抽象层 (HAL):
  ..\Platforms\Nintendo\Source\Runtime\Core\Private\NintendoPathManager.cpp
  ..\Platforms\Nintendo\Source\Runtime\Core\Private\NintendoPlatformMemory.cpp
  ..\Platforms\Nintendo\Source\Runtime\Core\Private\NintendoPlatformTLS.cpp
  ..\Platforms\Nintendo\Source\Runtime\Core\Private\NintendoRunnableThread.h
  ..\Platforms\Nintendo\Source\Runtime\Core\Private\ShortenedPathManager.cpp
  ..\Platforms\Nintendo\Source\Runtime\Launch\Private\LaunchNintendo.cpp
  ..\Platforms\Nintendo\Source\Runtime\NintendoAudioMixer\Private\NintendoAudioMixer.cpp
  ..\Platforms\Nintendo\Source\Runtime\NintendoPlatformFeatures\Private\NintendoSaveGameSystemBase.cpp
```

这些源文件明确了本作的模块边界：
- 渲染逻辑集中在 `NVNRHI`，通过 `NVNCommands.cpp` 记录绘制命令，通过 `NVNShaders.cpp` 绑定专用 NVN 编译着色器。
- 线程生命周期集中在 `NintendoRunnableThread.h`，每个线程栈默认分配 1 MB，采用 POSIX 优先级的映射表。

---

## 6. 对下游任务的具体指导建议 (Direct Guidance for T02 & T04)

### 6.1 给 T02 (Native Dynamic Loader & Runtime) 的实施指引
1. **加载器内存布局**：
   - 使用单次 `mmap(NULL, 0x9a36000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)` 分配连续虚拟内存空间（~154.2 MB）。
   - 将 `main.bin` 的 `.text` 拷贝到基址 + 0，`.rodata` 拷贝到基址 + `0x5e6b000`，`.data` 拷贝到基址 + `0x67d5000`，`.bss`（`0x2390608` 字节）清零。
2. **重定位处理顺序**：
   - 首先应用 **DT_RELR**（位于 `0x5e860f8`，大小 `0x3fa80`），单线程循环仅需约 5 ms 即可修正完全部 1,370,974 个相对指针。
   - 随后遍历 **DT_RELA**（3,985 项），解析符号名称并回填外部 HLE 函数指针。
3. **页保护更新**：
   - 重定位完成后，调用 `mprotect(base, 0x5e6b000, PROT_READ | PROT_EXEC)` 将代码段设为可执行；`.rodata` 设为 `PROT_READ`；`.data`/`.bss` 保持 `PROT_READ | PROT_WRITE`。
4. **启动引导流程**：
   - 设置 `tpidr_el0` 指向主线程 TLS 块。
   - 调用基址 + `0x352f150`（`nninitStartup`）。
   - 创建引擎主线程，调用基址 + `0x352f160`（`nnMain`），传入标准命令行参数（如 `-pak` 等）。

### 6.2 给 T04 (NVN Translation & Assets) 的实施指引
1. **动态导出函数截获**：
   - 游戏仅调用一个名为 `nvnBootstrapLoader` 的符号获取 API 指针。
   - T04 需提供一个哈希映射表，针对 535 个 NVN API 字符串返回对应的 Vulkan 适配函数。
2. **核心首帧渲染通路**：
   - 优先实现设备初始化与交换链：`nvnDeviceInitialize`, `nvnQueueInitialize`, `nvnWindowInitialize`, `nvnWindowAcquireTexture`, `nvnQueuePresentTexture`。
   - 优先支持双缓冲清屏绘制（Clear Color），即可迅速点亮首帧。

---

## 7. 结语

本报告完成了对《最终幻想：共鸣》Switch 原生镜像的系统级技术解构。经严谨实证，二进制完全具备原生动态加载条件，为 RG477M 原生移植工程奠定了最核心的技术路线基石。

**Agent A (RE 逆向与架构分析师)**  
*Recorded at task/T01-route | Worktree: .claude/worktrees/t01-route*

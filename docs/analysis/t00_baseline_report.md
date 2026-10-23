# T00 输入与工具基线报告 (Baseline & Environment Verification)

- 执行角色：RE (逆向与环境分析)
- 验证时间：2026-10-23
- 状态：PASSED
- 任务目标：只读核实游戏包身份、完整 SHA-256、版本与元数据、提取 ExeFS 关键二进制、识别真实引擎与架构特征、核验主机分析及交叉编译环境。

---

## 1. 游戏包完整性与外层 vs 内层身份实证

### 1.1 文件摘要与哈希对照 (Whole-package SHA-256)

| 目标文件 | 大小 (Bytes) | 完整 SHA-256 |
|---|---|---|
| `[本体v1.0.0][0100B8A026F14000][22.1.0][中文][HK].nsp` | 9,061,339,984 | `02b61f9fe88cb8b04e948dffbfa15eaadbd7f8a6bd45b53960a66812cf8a325e` |
| `[升级档v1.0.1][0100B8A026F14800][22.5.0][中文][HK].nsp` | 143,005,520 | `a31ebd0a1f416321da0b32ec0e519b337f0b322d9f00209d6bc1695a14cde06b` |
| `[DLC][0100B8A026F15002][v0][HK][見習騎士起步衝刺包].nsp` | 121,040 | `17631319c1fad16a93ef5cdda58713a5e9b61b1601d7de6cf52b8e26b2fb4a49` |

### 1.2 内部元数据（NACP / NPDM / CNMT 实测）

通过 `hactool` 与自建 Switch NCA 流式解密验证：
- **Base Title ID**：`0100B8A026F14000`
- **Application Display Version**：`1.0.0`
- **NACP 登记标题**：
  - `Korean`：`FINAL FANTASY RESONANCE` | `SQUARE ENIX`
  - `TraditionalChinese`：`FINAL FANTASY RESONANCE` | `SQUARE ENIX`
  - `SimplifiedChinese`：`FINAL FANTASY RESONANCE` | `SQUARE ENIX`
- **NPDM 内部代号**：`FFRS` (MMU Flags 37, Default CPU ID 0, Stack Size 0x10000, 64-bit ARM64)
- **CNMT 内容结构**：
  - Program NCA: `786609ae20a88b04b4901b09a4e400f9.nca` (8,641.03 MB)
  - Control NCA: `6d439989edb47d981ef3f908e0955c5f.nca` (0.38 MB)
  - LegalInfo NCA: `f85eb584c08c55c4d5ace94a6ee85279.nca` (0.15 MB)

---

## 2. 提取与模块结构 (ExeFS & NSO Binaries)

利用 Ticket (`base.tik`) 中的 Rights ID (`0100b8a026f140000000000000000016`) 与 `titlekek_15` 解密出 Program NCA 的 Section 0，成功定向提取 ExeFS 分区（总计 71.45 MB），无需解包 8.5 GB RomFS：

| 模块 | 类型 | 大小 (Bytes) | SHA-256 | BuildId | 内存映射 (.text/.rodata/.rwdata/.bss) |
|---|---|---|---|---|---|
| `main` | NSO0 | 65,285,909 | `5fd6cfaf554672d1364f519d0a93b33cffdd81a3e3f0cf96b8ddc599f579b570` | `736837AFD390442ED7C74A7DE730E71B...` | .text 94.4MB / .rodata 9.4MB / .rwdata 15.9MB / .bss 35.5MB |
| `main.npdm` | NPDM | 1,620 | `a39b66ed07bcb7b2730a9bbd37ec9afe968dd28d6e3d5f4d7794c3528c1b3f95` | - | ACI0: 0100b8a026f14000, SVCs, SACs |
| `rtld` | NSO0 | 10,109 | `4a520d568bbc0e584bc2da5f3250f81f28d09562f7b4b5f57365b970907b709c` | `387E9A52A10AC682BD84858284D2EFF7...` | .text 12KB / .rodata 4KB / .rwdata 4KB / .bss 4KB |
| `sdk` | NSO0 | 6,200,384 | `56af5073788dc1975b5186afafcdcbc0ab0f2f9b40b9d483e8747fd649bd8f8e` | `0023A1B06130CD795F7BC4613772AF0A...` | .text 6.4MB / .rodata 4.8MB / .rwdata 341KB / .bss 830KB |
| `subsdk0` | NSO0 | 3,367,524 | `017fa4eea05892b997ef749077a9c38ebede7bafa648c7a4a794d297d5e58be3` | `AB62FE8EDD07A6A293F885D54359E559...` | .text 3.3MB / .rodata 1.8MB / .rwdata 194KB / .bss 1.1MB (multimedia) |

解压后的完整反编译原语保存在 `.local/uncompressed/main.bin`（119.76 MB）。

---

## 3. 游戏引擎与架构特征确认 (Critical Finding)

通过反编译符号与二进制只读段搜索，发现本作具有极度明确的引擎身份：

1. **引擎类型**：**Epic Games Unreal Engine 5 (UE 5.4)**
   - 内部项目代码代号：**`mucha`**
   - 目标构建配置：`E:/workspace/mucha/mucha/Binaries/Switch/FFRS-Switch-Shipping.nss`
   - 核心源文件路径痕迹：
     - `E:/workspace/mucha/mucha/Source/mucha/GameInstance/CPP_MuchaGameInstance.cpp`
     - `E:/workspace/mucha/mucha/Source/mucha/Unit/CPP_Unit.h`
     - `E:/workspace/mucha/mucha/Source/mucha/Task/CPP_Task.h`
     - `E:/workspace/mucha/mucha/Source/mucha/Encount/Slate/CPP_SW_Encount_CellList.cpp`
     - `E:/workspace/mucha/mucha/Source/mucha/UI/...`
   - UE5.4 特征符号验证：
     - `EChaosVDAABBTreeNodeFlags::IsDirty` (Chaos Visual Debugger, UE 5.4 引入)
     - `UniversalObjectLocator`, `IrisCore`, `DataflowEngine`, `HairStrandsSolver`, `NNE` (Neural Network Engine)
     - `EnhancedInput` / `UEnhancedInputUserSettings` (支持实体键位、输入重映射)
     - `CommonUI` / `CommonLoadingScreen`
     - `SpriteStudio6` UMG 插件
2. **中间件**：
   - 音频与视频：**CRIWARE CriAtom / CriMana** (`CriWareRuntime`)
   - 压缩算法：**Oodle LZ / Oodle Network**
3. **图形接口与后端**：
   - 原作在 Switch 上使用 **Nintendo NVN** 原生底层图形 API (`nvnDeviceInitialize`, `nvnQueueInitialize`, `nvnCommandBufferInitialize`)。
   - 在 Ryujinx 历史报告中表现为严重依赖 `bindless / textureBuffer` 访问（格式 `0x25A5A`）。

---

## 4. 工具链与环境准备情况

- **hactool**：1.4.0（本地 ARM64，已验证 PFS0/NCA/NSO 解析能力）。
- **Ghidra**：12.1.4 + Switch Loader（已实测加载 NSO0，自动识别 `AARCH64:LE:64:v8A:default`，成功分析段表与重定位）。
- **REA (Reverse-Engineer-Anything)**：6.5.0（Antigravity MCP 207 工具已加载就绪）。
- **Android SDK**：已就绪于 `/Users/yuyan9923/Library/Android/sdk`，含 `cmdline-tools/latest/bin/sdkmanager` 与 `adb`。
- **Android NDK / CMake**：SDK 中暂未安装 NDK；T03A 阶段将通过 `sdkmanager` 安装目标 NDK 版本（如 `ndk;26.1.10909125`）。
- **RG477M 真机**：暂未连接。状态标记为 `BLOCKED_DEVICE`。

---

## 5. 结论与出口判定

- [x] **PFS0 外层清单与内部真实身份已明确区分**：真实项目为基于 UE 5.4 开发的 `mucha` (`FFRS-Switch-Shipping.nss`)。
- [x] **所有二进制文件哈希完整记录，产物位于 `.local/`，不污染 Git**。
- [x] **T00 成功通过 (PASSED)**，下一阶段 T01 (技术路线评估) 与 T03 (Android 宿主骨架) 具备开工条件。

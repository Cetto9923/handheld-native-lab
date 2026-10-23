# T04-assets 资源与解包分析报告 (Asset Inventory & RomFS Structure)

- **执行角色**：Agent D (资源与解包工程师)
- **任务编号**：T04-assets
- **分析日期**：2026-10-23
- **目标游戏**：《最终幻想：共鸣 / FINAL FANTASY RESONANCE》
- **基础 NSP**：`[本体v1.0.0][0100B8A026F14000][22.1.0][中文][HK].nsp` (9,061,339,984 字节, SHA-256: `02b61f9fe88cb8b04e948dffbfa15eaadbd7f8a6bd45b53960a66812cf8a325e`)
- **工作分支**：`task/T04-assets`
- **成果工件**：
  - 核心解析工具：`scripts/extract/titlekey_derivation.py`, `scripts/extract/romfs_stream.py`
  - 证据资产清单：`evidence/assets/romfs_inventory.json`, `evidence/assets/manifest_ufs_summary.json`, `evidence/assets/shader_inventory.json`

---

## 1. 任务背景与核心约束

本作基础包体积高达 8.44 GiB，解包后 RomFS 单体约 8.35 GiB，解压资产树将超过 10 GiB。当前宿主可用磁盘空间约 28 GiB，且真机 RG477M 存储亦需节约。
严格遵照项目工程规范：
1. **严禁全量解包到磁盘**：不得将 8.5 GB RomFS 完整提取至文件系统，避免产生大量临时垃圾文件或导致磁盘耗尽。
2. **零磁盘损耗定向流式解包**：在 `scripts/extract/` 下构建基于 Python 的 AES-128-CTR 随机流式读取器，直接在 NSP 内部寻址并解密元数据、目录树及目标资产。
3. **敏感凭据与资产隔离**：所有密钥保留在 `.local/`，轻量级脱敏资产清单入库至 `evidence/assets/`。

---

## 2. Rights-ID Title Key 解密与推导机制实证

### 2.1 Ticket 结构与密文提取

本作作为 eShop 数字发行版，在 NSP 内层 PFS0 分区携带 Ticket (`0100b8a026f140000000000000000016.tik`，大小 704 字节)：
- **签名算法**：RSA-2048 with SHA-256 (`0x10004`)
- **证书颁发者 (Issuer)**：`Root-CA00000003-XS00000020`
- **Rights ID**：`0100b8a026f140000000000000000016` (Ticket 偏移 `0x2a0:0x2b0`)
- **加密 Title Key 块 (Encrypted Title Key Block)**：Ticket 偏移 `0x180:0x190` (16 字节)：
  `534dc8759e72368240646426ab13193c`
- **Master Key 世代 (Key Generation)**：
  - Ticket 偏移 `0x285` 为 `0x16` (22)
  - Program NCA 头偏移 `0x220` (Crypto Type 2) 标明为 `22`，对应 Nintendo Switch Master Key 世代 `15` (`titlekek_15`)

### 2.2 Title Key 推导数学过程

Switch Title Key 采用 AES-128-ECB 解密：
$$\text{TitleKey} = \text{AES-128-ECB-Decrypt}(\text{EncryptedTitleKey}, \text{TitleKEK}_{15})$$

输入参数：
- $\text{EncryptedTitleKey}$: `534dc8759e72368240646426ab13193c`
- $\text{TitleKEK}_{15}$: `6d392d6b35589479275715900e6bad2b` (来自 `.local/keys.txt` 第 15 代 TitleKEK)
- **解密得出唯一真实 Title Key**：
  $$\mathbf{14d985e5730ed0e8b847102332f5ad99}$$

### 2.3 历史记录 `.local/test_keys.txt` 差异排查

在项目早期探索中，`.local/test_keys.txt` 记录了一条记录：
`0100b8a026f140000000000000000016 = 0b82ed357de7134737e16699c7b6f320`

通过对 `keys.txt` 中全部 22 组密钥矩阵进行全排列碰撞测试，证实：
- `0b82ed357de7134737e16699c7b6f320` 是使用未派生的母密钥 `titlekek_source` (`1edc7b3b60e6b4d878b81715985e629b`) 直接解密的结果。
- 实测使用 `0b82ed357de7134737e16699c7b6f320` 解密 NCA Section 0，输出为随机乱码 (`69e58936...`)，无法匹配 `PFS0` 标识。
- 使用 `titlekek_15` 解出的 `14d985e5730ed0e8b847102332f5ad99` 可**完美同时解出 ExeFS (PFS0) 与 RomFS (IVFC/RomFS Header)**，证实 `14d985e5...` 才是正确的游戏 Title Key。

### 2.4 双重密码学真实性校验

| 目标分区 | 校验偏移 | 算法 | 期望标识 / 结构 | 实测解密结果 | 状态 |
|---|---|---|---|---|---|
| **Section 0 (ExeFS)** | NCA 偏移 `0x217994000` + `0xc000` | AES-128-CTR (`sec_ctr = 1`) | Magic `PFS0` (`50 46 53 30`) | `5046533005000000...` (`PFS0`) | **PASSED** |
| **Section 1 (RomFS)** | NCA 偏移 `0x1c000` + `0x10d0000` | AES-128-CTR (`sec_ctr = 2`) | Header Size = `0x50` (80 字节) | `5000000000000000...` (HeaderSize 80) | **PASSED** |

推导与校验过程已封装为独立自动化测试工具：`scripts/extract/titlekey_derivation.py`。

---

## 3. 定向流式解包架构 (`scripts/extract/romfs_stream.py`)

为了实现零磁盘占用的全量资产审计，开发了流式访问器 `RomFSStream`：

```
[base.nsp] (9,061,339,984 B)
   │
   ├── PFS0 Header & Ticket/Cert/CNMT
   │
   └── Program NCA (Offset 6480, Size 9,060,777,984 B)
         │
         ├── NCA Header (16 KiB, AES-128-XTS Decrypted with header_key)
         │
         ├── Section 0: ExeFS (71.45 MB, AES-CTR)
         │
         └── Section 1: RomFS (8,569.47 MB, AES-CTR with TitleKey)
               │
               ├── Level 0..4: IVFC Hash Tables (16.81 MB)
               │
               └── Level 5: RomFS Data Partition (8,552.66 MB)
                     ├── RomFS Header (80 B)
                     ├── Data Blocks (8,552.66 MB)
                     │     ├── StagedBuild_FFRS.ini
                     │     ├── TessellationTable.bin
                     │     ├── Fonts (LastResort.ttf)
                     │     ├── FFRS-Switch.pak (2.76 GiB)
                     │     ├── FFRS-Switch.ucas (5.57 GiB)
                     │     ├── FFRS-Switch.utoc (11.81 MB)
                     │     ├── global.ucas / global.utoc (3.20 MB)
                     │     └── Manifest_UFSFiles_Switch.txt (10.74 MB)
                     └── RomFS Metadata (1,080 B)
                           ├── Dir Hash Table (44 B)
                           ├── Dir Meta Table (308 B)
                           ├── File Hash Table (60 B)
                           └── File Meta Table (716 B)
```

通过定位 Level 5 末尾的元数据表（总计仅 1 KB），无需扫描 8.5 GB 数据主体即可在 **0.5 秒内**提取 RomFS 全部分区与文件节点。

---

## 4. RomFS 完整文件树与分区清单

RomFS 内部共包含 **10 个目录节点** 与 **14 个文件实体**：

| 文件路径 | RomFS 数据偏移 | 大小 (字节) | 大小格式化 | SHA-256 (元数据与小文件) | 说明 |
|---|---|---|---|---|---|
| `Engine/Config/StagedBuild_FFRS.ini` | `0x0` | 3 | 3 B | `f1945cd6c19e56b3...` | UE 打包环境标识 (空/BOM) |
| `Engine/Content/Renderer/TessellationTable.bin` | `0x10` | 259,568 | 253.5 KB | `edc4f3b628a139c5...` | 曲面细分预计算查找表 |
| `Engine/Content/SlateDebug/Fonts/LastResort.tps` | `0x3f600` | 930 | 930 B | `0f775fa99d05e21d...` | 备用字体许可描述 |
| `Engine/Content/SlateDebug/Fonts/LastResort.ttf` | `0x3f9b0` | 5,395,052 | 5.15 MB | - | Slate 备用后备字体 |
| `FFRS/Content/Paks/FFRS-Switch.pak` | `0x564c20` | 2,958,593,334 | **2.76 GiB** | - | 主 Pak 容器 (Pak v11) |
| `FFRS/Content/Paks/FFRS-Switch.ucas` | `0xb0aed960` | 5,976,848,928 | **5.57 GiB** | - | IoStore 主资产流式容器 (Zen Container) |
| `FFRS/Content/Paks/FFRS-Switch.utoc` | `0x214ee5380` | 12,382,627 | **11.81 MB** | - | IoStore 目录与块描述表 (TOC v8) |
| `FFRS/Content/Paks/global.ucas` | `0x215ab4530` | 3,353,200 | **3.20 MB** | - | 全局基础类与元数据容器 |
| `FFRS/Content/Paks/global.utoc` | `0x215de6fa0` | 818 | 818 B | `7ca83c64f8205783...` | 全局基础类 TOC |
| `Manifest_DebugFiles_Switch.txt` | `0x215de72e0` | 395 | 395 B | `0ff328bd5c63a4c0...` | 调试符号清单 (记录 .nss 构建时间) |
| `Manifest_NonUFSFiles_Switch.txt` | `0x215de7470` | 3,448 | 3.37 KB | `ce029d95f3ab29ff...` | 非 UFS 阶段文件清单 (NSPD 目录等) |
| `Manifest_UFSFiles_Switch.txt` | `0x215de81f0` | 11,266,505 | **10.74 MB** | `62eb9756ea3f64d8...` | **全量虚幻引擎资产清单 (106,968 项)** |
| `UECommandLine.txt` | `0x2168a6bc0` | 38 | 38 B | `f842c65b9968c0c4...` | `-project="../../../FFRS/FFRS.uproject"` |
| `__rom` | `0x2168a6bf0` | 4 | 4 B | `c2368fda4366b6f2...` | Switch RomFS 占位标志 |

---

## 5. 虚幻引擎 5.6.1 IoStore 与 Pak 架构深度剖析

本作在资源组织上采用了标准的 **Unreal Engine 5 IoStore (Zen Loader)** 体系：

### 5.1 容器分工与层级

1. **`FFRS-Switch.ucas` (5.57 GiB) / `FFRS-Switch.utoc` (11.81 MB)**：
   - 承载游戏绝大多数 UAsset 逻辑对象、材质属性、骨骼动画与静态网格数据。
   - TOC 头部魔数为 `b"-==--==--==--=-"`，版本号为 `8`（UE 5.4+ 新结构）。
   - **TOC Chunk 条目数**：**63,910 项**。
   - **压缩块数量**：**518,224 块**。
   - **压缩块尺寸**：**65,536 字节 (64 KiB)**。
   - **目录索引 (DirectoryIndex)** 尺寸：**3,096,083 字节 (3.09 MB)**。
   - 压缩算法：采用 Oodle 字典块压缩（NSO 中对应的 `OodleNetworkHandlerComponent`）。

2. **`FFRS-Switch.pak` (2.76 GiB)**：
   - Pak 文件版本为 **11** (`FPakInfo::PakFile_Version_Latest`)。
   - 索引位置：偏移 `0xb052a3b8`，索引大小 `236,702 字节`。
   - 挂载点：`../../../`（对应打包工程工作路径）。
   - **条目总数**：**2,363 个文件**。
   - 索引格式：采用 UE 5 的 `PathHashIndex`（64位路径哈希散列表），主要容纳未合入 IoStore 的大尺寸流式媒体（CRIWARE 视频 `.usm`、长音频 `.awb` 以及特定松散资源）。

3. **`global.ucas` (3.20 MB) / `global.utoc` (818 B)**：
   - 仅包含 1 个 Chunk、52 个压缩块，用于启动时快速预加载核心基础类型定义。

---

## 6. UFS 全量资产清单统计 (106,968 条目)

通过在内存中流式解析 10.74 MB 的 `Manifest_UFSFiles_Switch.txt`，完整掌握了本作资产总量与分布：

### 6.1 文件扩展名分布 (Top 20)

| 扩展名 | 条目数量 | 类型说明 |
|---|---|---|
| `.uexp` | 46,601 | 虚幻资产导出数据 (Export Data) |
| `.uasset` | 45,055 | 虚幻资产头描述 (Asset Header) |
| `.ubulk` | 11,394 | 纹理/网格海量二进制块 (Bulk Data) |
| `.umap` | 1,546 | 关卡与世界分区地图 (Level/World Partition) |
| `.res` | 1,079 | 引擎二进制资源 |
| `.png` | 424 | Slate UI 与贴图原始素材 |
| `.svg` | 313 | 矢量 UI 图标 |
| `.uplugin` | 168 | 插件模块描述符 |
| `.usm` | **154** | **CRIWARE VP9 视频动画** |
| `.ini` | 96 | 引擎与项目配置 |
| `.ufont` | 46 | 字体定义资产 |
| `.locres` | 37 | 本地化翻译字符串表 |
| `.ttf` | 15 | TrueType 矢量字体 |
| `.brk` | 12 | ICU 断词规则库 |
| `.locmeta` | 5 | 本地化元数据 |
| `.icu` | 4 | ICU 国际化数据包 |
| `.ushaderbytecode` | **2** | **着色器归档字节码库** |
| `.stinfo` | **2** | **着色器类型元信息** |
| `.upipelinecache` | **1** | **运行时管线状态缓存** |
| `.awb` | **1** | **CRIWARE 流式音频波形库** (`BGM_PP.awb`) |

### 6.2 模块与核心插件构成

本作基于 UE 5.6.1 开发，包含大量官方及中间件插件：
- **`FFRS/Content`**：103,072 个文件（游戏本体主要逻辑与场景美术资产）。
- **`Engine/Content`**：2,621 个基础引擎资产。
- **关键插件模块**：
  - `CommonLoadingScreen` / `CommonUI`：统一加载屏与跨平台手柄/触屏输入 UI 栈。
  - `EnhancedUI` / `EnhancedInput`：虚幻高级输入重映射系统（支持实体键位绑定）。
  - `CriWare` (`CriWareCinematics`, `CriWareNiagara`, `CriWareSwitch`)：音频与视频中间件。
  - `SpriteStudio6`：2D 骨骼动画与立绘特效插件。
  - `DebugMenuWidget`：开发者调试菜单（可作为移植阶段功能验证入口）。
  - `Krystallos_SaveDataTransfer` / `DemoSaveTransfer`：项目内部代号 `Krystallos` 与存档转移支持。
  - `NodeGraphAssistant`, `ChaosVD`, `MetaHuman` 等扩展。

---

## 7. 着色器与图形资源详查 (Critical for GPU Porting)

本作在 RomFS 中明确预编译了 5 个专用着色器工件与 1 个管线缓存文件：

```
Engine/GlobalShaderCache-SP_GLSL_SWITCH.bin
FFRS/Content/ShaderArchive-FFRS-SF_GLSL_SWITCH-SP_GLSL_SWITCH.ushaderbytecode
FFRS/Content/ShaderArchive-Global-SF_GLSL_SWITCH-SP_GLSL_SWITCH.ushaderbytecode
FFRS/Content/ShaderTypeInfo-FFRS-SF_GLSL_SWITCH-SP_GLSL_SWITCH.stinfo
FFRS/Content/ShaderTypeInfo-Global-SF_GLSL_SWITCH-SP_GLSL_SWITCH.stinfo
FFRS/Content/PipelineCaches/Switch/FFRS_SF_GLSL_SWITCH.stable.upipelinecache
```

### 7.1 着色器格式与平台归属

- **Shader Platform**：**`SP_GLSL_SWITCH`**
- **Shader Format**：**`SF_GLSL_SWITCH`**
- **本质分析**：
  这是虚幻引擎面向 Nintendo NVN 图形栈编译的专有字节码，通过任天堂 Switch SDK 的 GLSL 编译器生成。其二进制包含针对 NVIDIA Tegra X1 (Maxwell 架构) 的专有 ISA 指令与 Bindless Uniform/Texture 资源绑定布局。
- **对 RG477M (Mali-G615 MC6) 的移植约束**：
  - RG477M 使用 ARM Mali-G615 GPU，原生图形 API 为 **Vulkan 1.3** 与 **OpenGL ES 3.2**，不支持 NVN 专有着色器字节码。
  - 原生移植路线（T05/T06）中，必须通过以下方案之一解决：
    1. **NVN-to-Vulkan 动态转译器**：在运行期将 NVN GLSL / SPIR-V 转译为标准 Vulkan SPIR-V。
    2. **ShaderCodeLibrary 重构**：借助 UE 5.6.1 引擎的 `ShaderTypeInfo` (`.stinfo`) 反推着色器变体签名，利用移动端（Vulkan Mobile / SM5）管线生成兼容 Mali 的着色器库。

---

## 8. 多媒体资产架构 (CriWare Video & Audio)

### 8.1 视频资产 (`.usm`)

本作包含 **154 个 `.usm` 视频文件**，集中存放在 `FFRS/Content/Streams/Movies/Event/`：
- 分布涵盖 **C01 至 C08 八个主线章节**的过场动画，以及开场动画 (`MV_Opening_vp9.usm`)、标题背景 (`MV_Title_vp9.usm`)。
- **编码格式**：文件名后缀统一标明为 **`vp9`**，即使用 Google VP9 视频编码封装在 CRI Sofdec2 USM 容器中。
- **解码可行性**：RG477M 搭载的天玑 8300 (Dimensity 8300) 硬件多媒体引擎原生支持 VP9 4K@60fps 硬解，Android 平台上的 `CriMana` 或 Android MediaCodec 可直接低功耗硬解。

### 8.2 音频资产 (`.awb` / `.acb`)

- `FFRS/Content/Streams/Sound/BGM_PP.awb` 负责全游戏长音频背景音乐流式传输。
- 战斗音效、UI 音效与配音切片打包于 IoStore 的 Sound UAsset 中，由 `CriWareRuntime` (`nn::audio` / `CriAtom`) 统一混音回放。

---

## 9. Android RG477M 原生资产交付与挂载方案

在 Android 端移植中，必须避免在手机上全量解压 8.5 GB RomFS：

```
[Android APK / Storage]
   │
   ├── Assets Directory / SDCard OBB:
   │     ├── FFRS-Switch.pak  (2.76 GiB, 存放流式视频与大文件)
   │     ├── FFRS-Switch.ucas (5.57 GiB, IoStore 数据)
   │     ├── FFRS-Switch.utoc (11.81 MB, IoStore TOC)
   │     ├── global.ucas / global.utoc
   │     └── UECommandLine.txt
   │
   └── Engine Runtime (FPakPlatformFile / FIoDispatcher):
         Direct mmap() / 64-bit File Descriptor Read
```

- **挂载方案**：
  直接将 RomFS 中提取出的 4 个容器工件 (`.pak`, `.ucas`, `.utoc`, `global.*`) 放入 Android 内部存储（如 `/sdcard/Android/data/com.square_enix.ffrs/files/Content/Paks/`）。
  利用虚幻引擎原生的 `FPakPlatformFile` 与 `FIoStoreReader`，以 `mmap` 或 64 位文件描述符直接零拷贝随机读取，**无需解包 10 万个散文件，完全符合 60 FPS 瞬时加载要求**。

---

## 10. 交付物与自动化验证索引

所有成果与工具已就绪并经过实测校验：

1. **自动化工具链**：
   - [titlekey_derivation.py](file:///Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.claude/worktrees/t04-assets/scripts/extract/titlekey_derivation.py)：Ticket 密文提取、Title Key ECB 推导、双重 NCA 分区密码学校验。
   - [romfs_stream.py](file:///Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.claude/worktrees/t04-assets/scripts/extract/romfs_stream.py)：零磁盘损耗 RomFS 内存流式解析器、UFS 清单分析器、TOC 结构提取器。
2. **轻量级证据索引**：
   - [romfs_inventory.json](file:///Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.claude/worktrees/t04-assets/evidence/assets/romfs_inventory.json)：RomFS 根目录与 14 个分区的完整偏移、尺寸与哈希表。
   - [manifest_ufs_summary.json](file:///Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.claude/worktrees/t04-assets/evidence/assets/manifest_ufs_summary.json)：106,968 项虚幻引擎资产统计、插件清单、着色器与视频清单。
   - [shader_inventory.json](file:///Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.claude/worktrees/t04-assets/evidence/assets/shader_inventory.json)：着色器格式 (`SP_GLSL_SWITCH`) 与针对 Mali-G615 的转译评估。
3. **结论与后续任务输入**：
   - Title Key 正确解密为 `14d985e5730ed0e8b847102332f5ad99`。
   - 虚幻引擎资产树与容器边界完全闭合，为 T04 标题画面引导与 T05 着色器转译提供了精准的输入数据。

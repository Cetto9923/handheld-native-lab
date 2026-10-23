# 当前接力入口 (Handover to Claude Code)

## 1. 任务完成与状态恢复

- **总控交接状态**：Antigravity 已完成 T00 基线验收及交接检查点收口；已核实无后台活动写者及长进程；已停止本轮所有写入操作，等待 Claude Code 接管。
- **状态模型漂移修复**：已在 `coordination/state.json` 中完整恢复 `T03A`（宿主交叉构建）与 `T03B`（真机验证）的拆分结构。
  - `T03A` 依赖 `T00`，状态 `ready`；
  - `T03B` 依赖 `T03A`，验收受 `BLOCKED_DEVICE` 门禁约束；
  - `T04` 启动依赖 `T02` 与 `T03A`，验收门禁为 `T03B`（缺设备可推进主机实现与交叉构建，真机标题验收仍须 T03B 通过）。

---

## 2. Git 状态与检查点

- **当前分支**：`work/rg477m-native`
- **当前 HEAD**：`ff881e99565f79b36ecd42ee952e4c9c41e75485`（待完成本轮提交）
- **Dirty / Untracked 清单**：
  - Tracked 修改：`coordination/state.json`、`coordination/HANDOFF.md`、`coordination/STATUS.md`
  - 新增审查文件：`scripts/t00_verify.py`、`evidence/t00_verification.json`、`docs/analysis/t00_baseline_report.md`
  - 未跟踪/私有生成内容：严格隔离在 `.local/`，已在 `.gitignore` 忽略，不含任何未追踪私有脏数据泄露到 Git。

---

## 3. T00 实际证据与覆盖范围

### 3.1 游戏包全量哈希 (Whole-package SHA-256)
- 本体 `[本体v1.0.0][0100B8A026F14000][22.1.0][中文][HK].nsp` (9,061,339,984 B)：`02b61f9fe88cb8b04e948dffbfa15eaadbd7f8a6bd45b53960a66812cf8a325e`
- 升级档 `[升级档v1.0.1][0100B8A026F14800][22.5.0][中文][HK].nsp` (143,005,520 B)：`a31ebd0a1f416321da0b32ec0e519b337f0b322d9f00209d6bc1695a14cde06b`
- DLC `[DLC][0100B8A026F15002][v0][HK][見習騎士起步衝刺包].nsp` (121,040 B)：`17631319c1fad16a93ef5cdda58713a5e9b61b1601d7de6cf52b8e26b2fb4a49`

### 3.2 已提取工件与本地摘要 (置于 `.local/exefs/`)
利用 Ticket 中的 Rights ID (`0100b8a026f140000000000000000016`) 与 `titlekek_15` 解密 Program NCA Section 0，定向提取 ExeFS 分区（总计 71.45 MB），无需解包 8.5 GB RomFS：
- `main` (65,285,909 B, SHA-256: `5fd6cfaf554672d1364f519d0a93b33cffdd81a3e3f0cf96b8ddc599f579b570`, BuildId: `736837AFD390442ED7C74A7DE730E71B00000000000000000000000000000000`)
- `main.npdm` (1,620 B, SHA-256: `a39b66ed07bcb7b2730a9bbd37ec9afe968dd28d6e3d5f4d7794c3528c1b3f95`)
- `rtld` (10,109 B, SHA-256: `4a520d568bbc0e584bc2da5f3250f81f28d09562f7b4b5f57365b970907b709c`)
- `sdk` (6,200,384 B, SHA-256: `56af5073788dc1975b5186afafcdcbc0ab0f2f9b40b9d483e8747fd649bd8f8e`)
- `subsdk0` (3,367,524 B, SHA-256: `017fa4eea05892b997ef749077a9c38ebede7bafa648c7a4a794d297d5e58be3`，multimedia 模块)
- 解压后二进制镜像：`.local/uncompressed/main.bin` (125,577,976 B, 119.76 MB)

---

## 4. 引擎身份：直接证据与推断区分

- **直接确凿证据 (Direct Evidence)**：
  1. 内部构建项目路径与目标工件直接写入二进制：`E:/workspace/mucha/mucha/Binaries/Switch/FFRS-Switch-Shipping.nss`，项目代号为 `mucha`。
  2. 源码树痕迹直读：`Source/mucha/GameInstance/CPP_MuchaGameInstance.cpp`、`Source/mucha/Unit/CPP_Unit.h`、`Source/mucha/Task/CPP_Task.h`、`Source/mucha/UI/...`。
  3. Unreal Engine UObject 导出表与命名空间：`/Script/mucha`、`/Script/Engine`、`/Script/Slate`、`/Script/EnhancedInput`、`/Script/CommonUI`、`/Script/IrisCore`。
  4. 底层图形与音频直接符号：`nvnDeviceInitialize`、`nvnQueueInitialize` (Nintendo NVN)；`CriAtom`、`CriMana` (CRIWARE)；`OodleLZ`、`OodleNetworkHandlerComponent`。
  5. 明确特征枚举值：`EChaosVDAABBTreeNodeFlags::IsDirty`（Chaos Visual Debugger，在 UE 5.4 正式引入并提供运行时支持）。
- **推断部分 (Inferred)**：
  - 核心引擎版本推断为 **Unreal Engine 5.4.x**（基于 `ChaosVDRuntime` + `IrisCore` + `DataflowEngine` + `UniversalObjectLocator` 的同时存在，排除 5.2 及更早版本；具体补丁小版本如 5.4.1/5.4.2 需待源码符号深入核对）。

---

## 5. 失败尝试、避坑记录与存活进程

- **失败尝试 1**：早期测试时使用 `hactool` 检查 header 报 `Failed to read section`。
  - *原因定位*：单独切片 16KB header 时，hactool 默认会 seek 至 section 内部校验 hash table，遇到 EOF 导致报错。
  - *处置*：编写直接解析 NCA FS Header 的流式解密算法，准确定位 `pfs0_offset = 0xc000` 并通过 `titlekek_15` 完成解密。
- **失败尝试 2**：Ghidra Headless 项目路径带 `.`（如 `.local`）时直接异常中止。
  - *原因定位*：Ghidra 内部 `NamingUtilities.checkName` 严禁项目路径元素以点开头。
  - *处置*：Ghidra 分析工程目录必须指向无点路径（如系统临时目录或非隐藏目录）。
- **存活进程核查**：经 `manage_task` 与系统进程查询，**无任何存活后台长进程、无挂起任务**。

---

## 6. 自测与独立 QA 分开记录

- **实现者自测 (Antigravity Self-Test)**：
  - `python3 scripts/t00_verify.py`：通过，SHA-256 全量核验及元数据提取 100% 成功。
  - `hactool` / `ghidra-headless` 导入自测：通过，成功识别 AARCH64 NSO 并建立段表。
- **独立 QA 验收**：
  - **NOT_RUN**：T00 仅为总控与环境自检，QA 独立验收按计划在 T07 或各阶段 Gate 执行，不伪造独立 QA PASS。

---

## 7. 当前阻塞与下一步前三条精确命令

- **当前阻塞**：RG477M 真机未连接，真机能力项保持 `BLOCKED_DEVICE`；不阻塞 T01/T03A。
- **下一位 (Claude Code) 启动的前三条精确命令**：
  1. 核实状态与工作区（确认 clean / commit 状态）：
     ```sh
     git status && python3 scripts/project_status.py
     ```
  2. 接管总控角色并派发 T01（逆向路线评估）：
     ```sh
     git checkout -b task/T01-re-route work/rg477m-native
     ```
  3. 读取反编译分析环境与 ExeFS 目标并开始路线评估：
     ```sh
     cat docs/analysis/t00_baseline_report.md
     ls -la .local/exefs/
     ```

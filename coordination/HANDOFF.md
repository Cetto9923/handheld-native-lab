# 当前接力入口 (Round 1 — Claude Code 总控)

> 本文件是**总控独占写**的交接入口。`coordination/state.json` 与本文件由 Claude Code (Fable 5) 独占维护；
> 其他 Agent 只在自己的分支/工作区写自己被授权的路径。`coordination/STATUS.md` 由 `scripts/project_status.py --write` 生成，不手工编辑。

---

## 1. 总控登记与接管核验（2026-10-23）

- **总控**：Claude Code (Fable 5)，唯一总控与集成人。接管自 Antigravity 的交接检查点。
- **接管方式**：Antigravity 已停止写入并释放占用；Claude Code 在 `b26bf15` 基础上核验现场、登记总控、创建第一轮工作区并派工。
- **集成分支**：`work/rg477m-native`（根交付分支）。本总控在 `claude/laughing-cray-3464b0` 工作区编辑，**检查点式合并**进 `work/rg477m-native`。

### 1.1 现场实测（与交接记录比对）

| 项 | 交接记录 | 实测 | 判定 |
|---|---|---|---|
| HEAD | `ff881e9`（HANDOFF 写的是"待完成本轮提交"） | `b26bf15` | 交接文档的 HEAD 引用**过期**；实际提交已包含 T00 全部产物 |
| 分支 | `work/rg477m-native` | `work/rg477m-native` + 本总控分支 + 3 个任务分支 | 一致 |
| 工作区 | clean | 主检出 clean；本工作区 clean（接管前） | 一致 |
| `active_writers` | 空 | 空 | 一致，无遗留写者 |
| 后台长进程 | 无 | 未发现遗留 Ghidra/hactool/build 进程 | 一致 |

**结论：交接基线成立，无新增未提交工作需要保护。未执行 reset / clean / stash。**

### 1.2 轻量证据复核（复用 T00，未重做昂贵步骤）

- **ExeFS 五个工件 SHA-256 现场重算**，与 `evidence/t00_verification.json` **逐项一致**：
  `main`=`5fd6cfaf…f579b570`、`main.npdm`=`a39b66ed…1b3f95`、`rtld`=`4a520d56…7b709c`、`sdk`=`56af5073…bd8f8e`、`subsdk0`=`017fa4ee…3e58be3`。
- **未重算 9 GB NSP 哈希**（无必要且昂贵）；NSP 仅核对文件存在与大小。
- **发现（已交 Agent C 独立复核）**：`scripts/t00_verify.py` 的 `engine` 与 `toolchain` 两段是**硬编码字面量**，不是从二进制派生的证据；且该脚本会**无条件覆盖** `evidence/t00_verification.json`。T00 的引擎结论目前属"报告结论 + 直证字符串"，**自动化验证尚未建立**。

### 1.3 宿主环境实测

- `adb devices` → **无设备**。真机项一律 `NOT_RUN` / `BLOCKED_DEVICE`。
- Android SDK 根 `/Users/yuyan9923/Library/Android/sdk`：有 `cmdline-tools`/`platform-tools`/`emulator`/`licenses`/`system-images`；
  **缺 `ndk/`、`build-tools/`、`platforms/`、`cmake/`**；系统 `cmake` 不在 PATH。
- 工具就位：`hactool`、`ghidra-headless`(Ghidra 12.1.4 + Switch Loader)、`rea` 6.5.0、`adb`、`ninja`。
- **磁盘仅余约 18 GiB（98% 已用）** → 重型占用（NDK 安装、Ghidra 工程）必须**串行**并及时回收。

---

## 2. 第一轮工作区、写入占用与分支

| Agent | 任务 | 工作区 | 分支 | 授权写入范围 |
|---|---|---|---|---|
| A (RE) | T01 | `.claude/worktrees/t01-route` | `task/T01-route` | `docs/analysis/route.md`、`docs/analysis/t01_*.md`、`tools/`、`evidence/t01/` |
| B (ANDROID) | T03A | `.claude/worktrees/t03a-android` | `task/T03A-android` | `src/android/`、`scripts/device/`、`tests/device/`、`docs/analysis/t03a_build_report.md` |
| C (QA) | T00R | `.claude/worktrees/t00-qa-review` | `task/T00-qa-review` | `coordination/reports/T00R-evidence-review.md`、`tests/acceptance/`、`docs/analysis/acceptance_matrix.md` |

- 三个工作区均以 `work/rg477m-native@b26bf15` 为基线创建，各自独立分支，**互不共享工作区**。
- 每个工作区的 `.local` 软链到主仓库 `.local/`（已 gitignore），私有日志按 `.local/agent-a|b|c/` 分隔，避免并发覆盖。
- **Ghidra 工程目录禁用点开头路径元素**（T00 已踩 `NamingUtilities.checkName`）：统一用 `/Users/yuyan9923/ffrs-scratch/ghidra/`。
- 三个 Agent 均被告知：**禁止 `git add .`**、禁止推送远端、禁止改 `coordination/` 与 `TASKS.md`、禁止打印密钥。

---

## 3. 总控裁决（对后续 T02/T04 的口径约束）

1. **"原生执行"的操作化定义**（写给实现者，避免自我降格）：
   - 合格：游戏派生代码由 NDK 编译为 arm64-v8a 原生代码并真实执行；或原作 ARM64 代码在**不经解释器/JIT**的前提下由自研原生加载器执行，且平台 API 由自研兼容层承接。
   - 不合格：运行时通用 CPU 解释器或 JIT；把 Ryujinx 等模拟器套壳；串流/视频/硬编码菜单/空窗口。
   - **验收不能只看进程名或"没有 JIT"**：需同时给出「源模块/地址 ↔ 实现对应」「构建链与 ABI」「原生库符号与执行路径」「已加载模块/进程检查」「真实行为对照」。
2. **行为对照必须有参照物**：任何"重建代码行为正确"的结论，必须附**同输入下的原始行为对照**，否则只算"看起来对"。
3. **主机执行 ≠ 真机执行**：Mac 上的样例通过只证明该样例；Android 侧只证明交叉构建产物存在。真机项在 T03B 通过前保持 `NOT_RUN`。
4. **证据不可覆盖**：原始证据只追加不覆盖；重跑会覆写证据文件的脚本必须改输出路径后执行（见 1.2）。
5. **资源调度**：Ghidra 大工程与 NDK 安装**串行**；每轮只允许一个重型分析工程。
6. **历史线索不继承结论**：用户历史 "Mac移植交付" 是 **Ryujinx 模拟器 + Cecil IL 补丁**，明确"非原生重写"。按 `AGENTS.md`，其 PASS/帧率/引擎归属一律**不继承**；其中可用的只有**关于本作自身的事实性线索**：
   - 本作**重度依赖 bindless / textureBuffer 绑定**（日志：`Failed to find handle source for bindless access of type "textureBuffer"`）；
   - 本作使用 **NVN 专有纹理格式 `0x25A5A`**（Ryujinx 格式表缺失它，表现为特效 CG 色偏/噪点，非崩溃）；
   - 原生 Switch 上本作**限帧 30**（Mac 上观测上限 31），与 PROJECT.md 的 30 FPS 口径吻合；
   - 历史瓶颈判定为 **GPU 侧 FIFO 积压而非像素填充率**（渲染倍率降至 0.5x 仅提升约 40%）。
   - **推论（待验证）**：Mali-G615 的 descriptor indexing / bindless 支持度是本项目 30 FPS 目标的**最高风险项**，只能由 T03B 真机探针实测判定。
7. **已识别的可复现性缺口**：T00 的 ExeFS 提取依赖一段**未入库**的 NCA 流式解密逻辑（仓库内只有产物 `.local/exefs/`，没有脚本）。RomFS（8.5 GB）同样未提取，且提取会加剧磁盘压力。这是 T04 启动链的硬前置，需在 T02/T04 前补齐（脚本进 `scripts/` 或 `tools/`，产物进 `.local/`）。

### 3.1 本轮新增的路由风险（已随提示转给 Agent A / Agent B）

- **T01**：NVN 兼容层必须支持 bindless texture buffer；须给出本作 NVN 导入面里 texture buffer / descriptor array 相关符号的**符号级证据与数量**，并单列 **NVN 纹理格式表 → Vulkan format 映射**（含 `0x25A5A`）为兼容层必做项。
- **T03A**：Vulkan 探针必须采集 `VkPhysicalDeviceDescriptorIndexingFeatures` 全字段、`Vulkan11/12/13Features` 相关位、`Maintenance3Features`、以及 `maxPerStageDescriptorUpdateAfterBind*` / `maxDescriptorSetUpdateAfterBind*` 等 limits 与关键 format properties；host 侧同 schema 占位，便于 T03B 逐字段对比。

---

## 4. 当前阶段与门禁

- T00 `passed`（已复核关键工件哈希）；T00R / T01 / T03A `in_progress`。
- T02 依赖 T01；T04 启动依赖 T02+T03A，**验收门禁 T03B**（无设备时不得标记真机标题画面 PASS）。
- 阻塞项：`BLOCKED_DEVICE`（RG477M 未连接）+ 磁盘余量偏紧。

---

## 5. 下一步前三条精确命令

```sh
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.claude/worktrees/laughing-cray-3464b0 && python3 scripts/project_status.py
```

```sh
git -C /Users/yuyan9923/GitHub/ff-resonance-rg477m-native worktree list && git -C /Users/yuyan9923/GitHub/ff-resonance-rg477m-native branch -v
```

```sh
git -C /Users/yuyan9923/GitHub/ff-resonance-rg477m-native log --oneline -5 work/rg477m-native
```

---

## 6. 收口检查项（本轮交接前必须逐条确认）

- [ ] 各子 Agent 是否已结束（或已明确转移写入占用）
- [ ] 每个工作区是否已提交或有明确的未提交 WIP 记录
- [ ] 长进程（构建/Ghidra/安装）是否已结束并落盘日志
- [ ] `state.json` 的 `active_writers` 与实际占用一致
- [ ] `HANDOFF.md` / `state.json` / `STATUS.md` 已更新且 `python3 scripts/project_status.py --write` 已跑通
- [ ] 明确写出"当前是否只有一个总控"

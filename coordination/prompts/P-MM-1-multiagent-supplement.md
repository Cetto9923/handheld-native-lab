# 补充提词｜MiniMax 多 Agent 团队并行（贴在主工单 P-MM-1 后面）

你现在启用 **多 Agent / 团队模式**。目标：在**不互相覆盖同一文件**的前提下并行推进 FFRS→RG477M 出画路径。主工单正文仍以 `coordination/prompts/P-MM-snapshot-thread-policy.md` 为准；本补充只规定分工与并行纪律。

## 硬约束（所有子 Agent 共用）
- 仓库：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- 禁止：`main`、force-push、reset --hard、clean -fd、stash pop 覆盖别人、push 远端
- 禁止声称标题/可玩/已出画面
- 真机：`adb -s RG477M01445122`；不 root/刷机/清数据
- **同一时刻只允许一个 Agent 持有「真机 boot」锁**（见下）
- **不要调用 agy**；不要和本机其它 Claude/Codex 会话抢改同一文件——若发现 `guest_process.c` 被外部进程同时改，立即停下并在回报里写「双写冲突」
- 大文件 `.local/romfs/**` 只读；NVN 既有 worktree 规则见各 Agent 分工

## 推荐 3 个并行角色（按文件所有权切开）

### Agent A｜主路径写手（唯一改 HLE 核心）——**必须先完成**
**任务**：执行 P-MM-1 全文（快照多线程策略）。
**独占文件（其它 Agent 只读）**：
- `src/runtime/hle/guest_process.c`
- `src/runtime/hle/guest_os_tls.c` / `guest_os_tls.h`（若需改）
- `src/runtime/hle/guest_process.h`
- `tests/native/guest_process_contract.c`
- `scripts/runtime/build_guest_process_contract.sh`（仅本单相关段）
- `coordination/HANDOFF.md` / STATUS / state（闭环时由 A 写，或 A 完成后由协调者合并）

**独占资源**：`.local/build-p-mm-1/`、真机全量 `--boot` → `.local/logs/rg477m-boot-v52-snapshot-thread.log`
**完成定义**：越过 `primary-thread` snapshot abort；提交检查点；HANDOFF 写 P-MM-1 闭环 + 新阻塞符号。

### Agent B｜下一阻塞侦察（只读 + 只写分析/提词草稿）
**任务**：在 A 跑契约/boot 的同时，**不改** `src/runtime/**`，只做：
1. 精读 v51 日志尾部与 A 产出的 v52（一旦出现）
2. 对照 `evidence/coordinator/nso_imports_main.tsv`、现有 HLE 白名单
3. 起草下一张工单草稿到：`coordination/prompts/P-MM-2-DRAFT-<符号简名>.md`（文件名自拟）
**可写**：仅 `coordination/prompts/P-MM-2-DRAFT-*.md`、`docs/analysis/` 下新建笔记（可选）
**禁止**：改任何 `.c/.h`、跑会改设备状态的实验性 push、杀 A 的进程
**完成定义**：草稿提词含：符号/证据行、候选改动文件表、验收命令、禁止项。

### Agent C｜出画旁路预研（独立 worktree，禁止碰主树 HLE）
**任务**：加速「第一帧」旁路，不阻塞 A。
**工作树**（没有就建，有就用，勿污染主树）：
```bash
# 示例：若尚无则创建（目录名固定，便于回收）
git worktree add ../ffrs-mm-nvn-prep work/rg477m-native
# 或沿用已有：task/T04-nvn-firstframe / Documents/Codex/2026-10-24/task/ffrs-nvn-firstframe
```
**可写**：仅该 worktree 内文件；优先读已有 `coordination/prompts/P-CC-nvn-firstframe.md` 并续做设计/桩代码/文档
**禁止**：
- 改主工作区 `src/runtime/hle/guest_process.c` 等
- 占用 `RG477M01445122` 做与 A 冲突的全量 boot（C 若需真机，必须等 A 释放 boot 锁，并改用短契约测或书面说明「等待 A」）
**完成定义**：worktree 内留下「NVN→Vulkan 首帧」缺口清单 + 最小补丁草案；主树 HANDOFF 只追加一行「P-MM-NVN 旁路进度」（不要抢写 A 的闭环段）。

## 协调者（你 / 父 Agent）节奏
1. **先拉起 A**；B、C 可立即并行，但 C 默认不抢设备。
2. A 提交并写完 HANDOFF 闭环后：
   - 协调者阅读 v52 新阻塞；
   - 把 B 的 DRAFT 提词升格为正式 `P-MM-2-….md`（或否决重写）；
   - **再**派下一主路径写手（仍建议单写手改 HLE）。
3. 任何时候发现双写：停次要 Agent，保留 A（或用户指定的唯一写手）。

## 真机 boot 锁（强制）
- 持有者：默认 **Agent A**
- 标志约定：开始 boot 前 `touch .local/agent-pm/BOOT_LOCK_MM_A`；结束后删掉
- 其它 Agent 看到该文件存在 → 禁止对 RG477M 发全量 `--boot`

## 你应立刻对用户/父进程说的话（启动时一句）
「已按三角色并行：A 改 snapshot 策略并独占真机；B 只写下一单草稿；C 在独立 worktree 做 NVN 预研不碰主树 HLE。」

然后开始执行：先读 `P-MM-snapshot-thread-policy.md`，再拆子任务。

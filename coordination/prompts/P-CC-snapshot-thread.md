# 工单 P-CC-8｜Claude Code｜主路径：内存/资源快照线程策略（越过 snapshot-refused）

你是实现 Agent。上一单 P-CC-7 已闭环：HEAD `3a9092e`（分支 `work/rg477m-native`，工作区 clean）。真机已越过 `nn::socket` 事件族（EventFd/Write/Read/Close/GetLastError/Poll），**全量 boot 日志 unresolved-import 计数 = 0**。新阻塞不再是导入，而是工作线程取内存快照被拒。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md` 顶部 P-CC-7 闭环段、`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`（及 `.exitcode`=134）
- 关键代码：`src/runtime/hle/guest_process.c` 的 `snapshot_locked`（约 423 行起）；调用点 `nn::os::GetMemoryInfo` / `GetVirtualAddressMemoryResourceUsage`
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git；勿删 `.local/romfs` 大文件
- 验收不降级；勿伪称标题/可玩；做完停下汇报，不要 ask_user
- 若存在 worktree `task/T04-nvn-firstframe` / `.claude/worktrees/t04-nvn`，勿改其内容
- **不要调用 agy**（Antigravity 仍 429 额度窗口）

## 当前阻塞（证据）
- 基线：`.local/logs/rg477m-boot-v51-socket-eventfd-final.log` exit **134**
- 已越过：EventFd 族；guest 已起 `IOThreadPool #0/#1`、`HttpManagerThread` 等；`unresolved-import`=0
- 致命链：
  ```
  [snapshot-refused] reason=primary-thread; memory/resource answer withheld
  [va-resource-invalid] status=1 native=22; no valid resource return
  Aborted → exit 134
  ```
- 归因：guest **工作线程**调用 `nn::os::GetVirtualAddressMemoryResourceUsage`，而 `snapshot_locked` 要求 `p->thread.state==THREAD_RUNNING && pthread_equal(p->thread.native, pthread_self())`（仅主线程），拒答后上层 abort。
- 证据链（P-CC-7）：v47/v48/v49 中该查询最后一次成功在子线程注册之前；v50 起在多次 child TLS 绑定后失败。
- 阶段建议：`T02_ZERO_UNRESOLVED_SNAPSHOT_PRIMARY_THREAD_ON_RG477M`

## 目标（按序）
1. **策略决策（优先推荐 A）**：放宽 `snapshot_locked` 的线程限制，使任意已绑定的 guest 线程（含 IOThreadPool）可取内存/资源快照。底层 pages/tls/context 读取路径本就应可在锁下并发只读；保留其余拒绝条件的可归因 `[snapshot-refused] reason=…` 日志。
   - 备选 B：对非主线程返回可归因非 abort 失败（勿空返回后立刻不可诊断崩溃）。仅当 A 会破坏已有契约/主线程语义时才用 B，并在 HANDOFF 写明理由。
2. **注意二次不变量**：`snapshot_locked` 后续还有 `tls.registered_threads!=1 → reason=os-tls-registered-threads`。工作线程起来后 registered_threads>1，仅去掉 primary-thread 仍可能立刻撞上该条件。须一并评估：多线程场景下该计数检查是否仍成立；若不成立，改为与多线程现实一致的检查（例如允许 ≥1，或按「调用线程已注册」判断），并保留可归因日志。**不要静默吞掉。**
3. 宿主契约：为「非主线程可取快照」或选定策略补最小契约（可新建 `--snapshot-thread` 或扩展既有 guest_process 契约）；宿主 PASS；有条件时 RG477M 同测。
4. 按 v51 **同一套** diagnostic 参数跑全量 `--boot`（`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216`）。新日志建议：`.local/logs/rg477m-boot-v52-snapshot-thread.log` + `.exitcode`。产物可放 `.local/build-p-cc-8/`。
5. 验收：日志不再以 `[snapshot-refused] reason=primary-thread`（及因本单未处理而立刻复现的 `os-tls-registered-threads`）致命 abort；须出现工作线程成功取快照或明确推进到**新的**可归因阻塞；记录新阻塞符号/阶段与 exitcode。
6. 更新 `coordination/HANDOFF.md` 写 P-CC-8 闭环段；`python3 scripts/project_status.py --write`（若可用）。
7. **提交检查点**（仅本工单相关文件）；回报：HEAD / 是否越过 snapshot-refused / 新阻塞 / 日志路径 / exitcode。
8. 做完停下。

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push 远端；不伪造完整网络在线/付费账号；不因额度改去调用 agy；不回退 EventFd/stdio/nnssl 已落地行为。

做完停下汇报，等项目经理下一派工。

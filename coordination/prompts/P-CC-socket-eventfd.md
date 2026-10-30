# 工单 P-CC-7｜Claude Code｜主路径：nn::socket::EventFd 族推进

你是实现 Agent。上一单 P-CC-6 已闭环：HEAD `2120088`（分支 `work/rg477m-native`，工作区 clean）。真机已越过 stdio 数据导入（stdout/stdin/stderr）与 stdio 函数族；新阻塞在 **代码导入** `nn::socket::EventFd`（import 325）。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md` 顶部 P-CC-6 闭环段、`.local/logs/rg477m-boot-v47-stdio-stdout.log`（及 `.exitcode`=133）
- 对照已落地：`nn::socket::Initialize`（早期 P-AG-10）、`nnssl*`、stdio 变参网关、typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git；勿删 `.local/romfs` 大文件
- 验收不降级；勿伪称标题/可玩；做完停下汇报，不要 ask_user
- 若存在 worktree `task/T04-nvn-firstframe` / `.claude/worktrees/t04-nvn`，勿改其内容
- **不要调用 agy**（Antigravity 仍 429 额度窗口）

## 当前阻塞（证据）
- 基线：`.local/logs/rg477m-boot-v47-stdio-stdout.log` exit **133**
- 已越过：`nnsslInitialize`、stdio 对象绑定、`HttpManagerThread` 已创建并进入 guest_entry
- `[fault] signal=5 pc=… fa=…`
- `[unresolved-import] index=325 name=_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE caller_offset=0x1091278`
- 即 `nn::socket::EventFd(unsigned long, nn::socket::EventFdFlags)`
- 阶段建议标记：`T02_STDIO_UNBLOCKED_SOCKET_EVENTFD_ON_RG477M`

## 同族未解析（`evidence/coordinator/nso_imports_main.tsv`，优先按真机下一阻塞推）
- 325 `nn::socket::EventFd(unsigned long, EventFdFlags)` ← **当前前沿**
- 326 `nn::socket::EventFdWrite(int, unsigned long)`
- 327 `nn::socket::EventFdRead(int, unsigned long*)`
- 可能紧随：`Poll`/`Select`/`Fcntl` 等 socket 族（已有 Initialize 基线，勿重复造 Initialize）

## 目标（按序）
1. 落地最小可用 `nn::socket::EventFd` HLE：按 ABI 接受 initval + flags，返回可用 fd（可用宿主 `eventfd`/`pipe`/`socketpair` 等价物）；打清晰 `[hle]` 归因日志。失败须可归因，勿空返回后立刻不可诊断崩溃。
2. 按真机下一阻塞优先补紧随 `EventFdWrite` / `EventFdRead`（及确需的 Poll/Select/Fcntl）；先以越过 import 325 并留下可归因下一阻塞为准，能推则继续推。
3. `guest_process.c` typed 白名单 + `import_signature`；`nso_host_resolve` 绑定；宿主契约覆盖 EventFd 往返（创建→写→读或等价）；RG477M 契约同测。
4. 按 v47 **同一套** diagnostic 参数跑全量 `--boot`（`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216`）。新日志建议：`.local/logs/rg477m-boot-v48-socket-eventfd.log` + `.exitcode`。产物可放 `.local/build-p-cc-7/`。
5. 验收：日志不再以 `unresolved-import … name=_ZN2nn6socket7EventFd…` 致命；须出现 EventFd 绑定/调用证据；记录新阻塞符号/阶段与 exitcode。
6. 更新 `coordination/HANDOFF.md` 写 P-CC-7 闭环段；`python3 scripts/project_status.py --write`（若可用）。
7. **提交检查点**（仅本工单相关文件）；回报：HEAD / 是否越过 EventFd / 新阻塞 / 日志路径 / exitcode。
8. 做完停下。

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push 远端；不伪造完整网络在线/付费账号；不因额度改去调用 agy。

做完停下汇报，等项目经理下一派工。

# 工单 P-AG-21｜Antigravity｜主路径：nn::os::GetCurrentProcessHandle（import 105）推进

你是实现 Agent。上一单 P-AG-20 已闭环：HEAD `6e8cbc1`（分支 `work/rg477m-native`）。真机已越过 `gettimeofday` / `getpid`（import 211 等）；pak 物理读、虚存 map、多线程 `SignalEvent`/`WaitEvent`、atexit≈13594、构建描述符探测仍在推进。新阻塞在未解析 `nn::os::GetCurrentProcessHandle`。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`（P-AG-20 闭环段）、`.local/logs/rg477m-boot-v42-gettimeofday.log`（及 `.exitcode`=133）
- 对照已落地 HLE 模式：`nn::oe::Initialize` / `GetDesiredLanguage`、`gettimeofday`/`getpid`、`nn::os` 事件/TLS —— typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已有 FFRS-Switch.pak/ucas（约 8.4GiB）——可读，勿删大文件；真机容器已推过，勿无故重推除非校验缺失
- 验收不降级；勿动 ProtectRelro 桩除非硬需要
- **并行**：P-CC-4 Claude 在 worktree `task/T04-nvn-firstframe` 做 NVN 首帧预研——**勿改其 worktree、勿抢设备整局长跑、勿杀其进程**

## 当前阻塞（证据）
- `[fault] signal=5 pc=0x6ff7e2a348 fa=0x6ff7e2a348`
- `[unresolved-import] index=105 name=_ZN2nn2os23GetCurrentProcessHandleEv caller_offset=0x7f1f4`
- exit **133**；基线日志：`.local/logs/rg477m-boot-v42-gettimeofday.log`
- 阶段：`T02_GETTIMEOFDAY_UNBLOCKED_NN_OS_PROCESS_HANDLE_ON_RG477M`
- 上下文：`gettimeofday` 已两次成功返回；紧随 VM map + pak ReadFile + 事件同步；探测 EpicInternal/Perforce/SourceDistribution 描述符后撞上本符号

## 目标（按序）
1. 落地最小可用 `nn::os::GetCurrentProcessHandle` HLE（Nintendo SDK：无参，返回当前进程 `nn::os::NativeHandle` / 进程句柄；可用稳定伪句柄或宿主进程句柄映射，保证后续若有 GetProcessId 等可衔接；打清晰 `[hle] nn::os::GetCurrentProcessHandle ...` 日志）
2. 按真机下一阻塞优先补紧随 `nn::os` 进程/句柄族符号（若紧邻仍是 Process/Handle/Id 可一并推）；先以越过 index 105 并留下可归因下一阻塞为准，能推则继续推
3. native_boundary / guest_process / nso_host 绑定；宿主契约覆盖 GetCurrentProcessHandle；RG477M `--boot` 沿用既有配额参数复测
4. 新日志建议 `rg477m-boot-v43-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
5. 提交检查点；回报 HEAD / 是否越过 GetCurrentProcessHandle / 新阻塞符号 / 日志路径 / exitcode；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push；不伪造付费/在线/Applet 完整态；不干扰 P-CC-4 worktree

做完停下汇报，等项目经理下一派工。

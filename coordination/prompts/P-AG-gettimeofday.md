# 工单 P-AG-20｜Antigravity｜主路径：gettimeofday（import 211）推进

你是实现 Agent。上一单 P-AG-19 已闭环：HEAD `902ef4a`（分支 `work/rg477m-native`）。真机已越过 `nn::oe::Initialize` / `GetDesiredLanguage`（import 80/81）；ICU 本地化与 pak 物理读、多线程 `SignalEvent`/`WaitEvent` 仍在推进，atexit 约 13,593。新阻塞在未解析 C 运行时 `gettimeofday`。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`（P-AG-19 闭环段）、`.local/logs/rg477m-boot-v41-oe-init.log`（及 `.exitcode`=133）
- 对照已落地 HLE 模式：`setlocale` / `getenv` / `nl_langinfo` / libc++ clock（`steady_clock::now` / `pthread_cond_clockwait`）——typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已有 FFRS-Switch.pak/ucas（约 8.4GiB）——可读，勿删大文件；真机容器已推过，勿无故重推除非校验缺失
- 验收不降级；勿动 ProtectRelro 桩除非硬需要

## 当前阻塞（证据）
- `[fault] signal=5 pc=0x7668e4c698 fa=0x7668e4c698`
- `[unresolved-import] index=211 name=gettimeofday caller_offset=0x5920c94`
- exit **133**；基线日志：`.local/logs/rg477m-boot-v41-oe-init.log`
- 阶段：`T02_OE_INIT_UNBLOCKED_GETTIMEOFDAY_ON_RG477M`
- 上下文：`nn::oe::Initialize` / `GetDesiredLanguage` 已两次成功返回；紧随大量 pak ReadFile 与 `nn::os::SignalEvent`/`WaitEvent`；非 oe、非 TaskGraph SIGSEGV

## 目标（按序）
1. 落地最小可用 `gettimeofday` HLE（POSIX：填充 `struct timeval`，返回 0；timezone 指针可为 NULL 时忽略；用宿主 `gettimeofday`/`clock_gettime(CLOCK_REALTIME)` 即可）；打清晰 `[hle] gettimeofday ...` 日志
2. 按真机下一阻塞优先补紧随时间/C 运行时符号（若紧邻仍是 clock/time 族可一并推）；先以越过 index 211 并留下可归因下一阻塞为准，能推则继续推
3. native_boundary / guest_process / nso_host 绑定；宿主契约覆盖 gettimeofday；RG477M `--boot` 沿用既有配额参数复测
4. 新日志建议 `rg477m-boot-v42-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
5. 提交检查点；回报 HEAD / 是否越过 gettimeofday / 新阻塞符号 / 日志路径 / exitcode；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push；不伪造付费/在线/Applet 完整态

做完停下汇报，等项目经理下一派工。

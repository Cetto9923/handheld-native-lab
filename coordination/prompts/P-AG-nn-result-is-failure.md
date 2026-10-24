# 工单 P-AG-22｜Antigravity｜主路径：nnResultIsFailure（import 607）推进

你是实现 Agent。上一单 P-AG-21 已闭环：HEAD `c61fbac`（分支 `work/rg477m-native`）。真机已越过 `nn::os::GetCurrentProcessHandle`（import 105，handle=0x6635）；引擎继续 atexit≈13616、插件 Pak 目录扫描、虚存 map、`nn::util::ReferSymbol`、并成功 `nn::os::CreateThread` 调度子线程（tid=26311）。新阻塞在子线程内未解析 `nnResultIsFailure`。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`（P-AG-21 闭环段）、`.local/logs/rg477m-boot-v43-os-process-handle.log`（及 `.exitcode`=133）
- 对照已落地 HLE 模式：`GetCurrentProcessHandle` / `gettimeofday`/`getpid` / `nn::oe::*` / `nn::os` 事件/TLS —— typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已有 FFRS-Switch.pak/ucas（约 8.4GiB）——可读，勿删大文件；真机容器已推过，勿无故重推除非校验缺失
- 验收不降级；勿动 ProtectRelro 桩除非硬需要
- P-CC-4 Claude NVN 预研进程已退出；若仍存在 worktree `task/T04-nvn-firstframe` / `Documents/Codex/2026-10-24/task/ffrs-nvn-firstframe`，勿改其内容

## 当前阻塞（证据）
- `[hle] nn::os::CreateThread: thread=0x6b2aa23200 entry=0x785096f450 arg=0x6b2aa23200 stack=0x6eeaa1f000 size=0x4000 prio=15 tid=26311`
- `[fault] signal=5 pc=0x7af08462f8 fa=0x7af08462f8`
- `[unresolved-import] index=607 name=nnResultIsFailure caller_offset=0x5e663c0`
- 故障时寄存器摘录：`x0=0x0 x1=0x728ae7f7e0 x2=0x85 ...`（子线程栈上调用）
- exit **133**；基线日志：`.local/logs/rg477m-boot-v43-os-process-handle.log`
- 阶段：`T02_OS_PROCESS_HANDLE_UNBLOCKED_NN_RESULT_ON_RG477M`

## 目标（按序）
1. 落地最小可用 `nnResultIsFailure` HLE（Nintendo SDK C 导出：通常判断 `nn::Result` 是否失败并返回 bool；按 dynsym/调用约定核对参与返回值 ABI，打清晰 `[hle] nnResultIsFailure ...` 日志）
2. 按真机下一阻塞优先补紧随 Result/错误族符号（若紧邻仍是 `nnResult*` / `Result*` / `nn::Result` 助手可一并推）；先以越过 index 607 并留下可归因下一阻塞为准，能推则继续推
3. native_boundary / guest_process / nso_host 绑定；宿主契约覆盖 nnResultIsFailure；RG477M `--boot` 沿用既有配额参数复测
4. 新日志建议 `rg477m-boot-v44-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
5. 提交检查点；回报 HEAD / 是否越过 nnResultIsFailure / 新阻塞符号 / 日志路径 / exitcode；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push；不伪造付费/在线/Applet 完整态

做完停下汇报，等项目经理下一派工。

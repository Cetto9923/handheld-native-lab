# 工单 P-AG-14｜Antigravity｜主路径：posix_memalign 及对齐分配推进

你是实现 Agent。上一单 P-AG-13 已闭环：HEAD `b6cf8c6`（分支 `work/rg477m-native`）。真机全量 1890/1890 ctor、nnMain、CreateThread/GameThread、guest TP + native_boundary、FS 链（SetAllocator/MountRom/OpenFile/OpenDirectory）、AOC/nifm/socket/account、`nn::os::InitializeEvent` 及 Finalize/Signal/Clear/Wait/TryWait/TimedWait 均已越过。新阻塞在对齐内存分配。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v29-initializeevent.log`
- 对照已落地 HLE：malloc/free/calloc 等 liballoc（typed 白名单 + `import_signature` + `[hle]` 日志 + 宿主契约）
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 覆盖；本仓可在验证成功后提交检查点
- 密钥/大二进制只留 `.local/`；验收不降级；勿抢并行 worktree `task/T04-pak-nvn-prep`；勿动 ProtectRelro 桩（除非硬需要）
- 并行参考（只读）：`/Users/yuyan9923/Documents/Codex/2026-10-24/task/ffrs-pak-nvn-prep/docs/analysis/codex_pak_iostore_prep.md` —— Pak 读不需新 fs HLE；勿为 Pak 改 fs

## 当前阻塞（证据）
- 符号：`posix_memalign`（symbol index **190**，JMPREL reloc index 186，GOT≈`0x77725d0`，caller≈`0xadbc8` / lr≈`0xadbcc`）
- exit **133**；日志：`.local/logs/rg477m-boot-v29-initializeevent.log` + `.exitcode`
- 阶段：`T02_OS_EVENT_PASSED_POSIX_MEMALIGN_REACHED_ON_RG477M`
- 基线：按 POSIX/musl ABI 对齐分配并写出指针；失败可归因；打 `[hle] posix_memalign: ...`；紧随必现的 aligned_alloc/valloc 等可一并最小落地，以越过 190 并留下下一阻塞为准

## 目标（按序）
1. 落地最小可用 `posix_memalign` HLE（必要时补紧随对齐分配 import）
2. 继续走 native_boundary / guest_process 绑定
3. 宿主契约覆盖；RG477M `--boot`（quota/stack/resource 同前）；日志证明越过；能推则续推
4. 新日志建议 `rg477m-boot-v30-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
5. 本地提交检查点；回报 HEAD、是否越过、新阻塞、日志、exitcode
6. 做完停下汇报；不要 ask_user

## 禁止
- 不 reset/clean/stash；不 push；不清数据；不 root/刷机
- 不声称标题/可玩；不空桩乱过；不下载未授权本体/密钥

做完停下汇报，等项目经理下一派工。

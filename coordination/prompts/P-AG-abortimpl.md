# 工单 P-AG-16｜Antigravity｜主路径：nn::diag::detail::AbortImpl 推进

你是实现 Agent。上一单 P-AG-15 已闭环：HEAD `2d78653`（分支 `work/rg477m-native`）。真机已越过 `__cxa_thread_atexit` / `__cxa_thread_atexit_impl`；GameThread 已枚举 `rom:/FFRS/Content/Paks/`、派生 `FHeartBeatThread` 并完成跨线程 Event 唤醒。新阻塞在诊断中止入口。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`.local/logs/rg477m-boot-v31-cxa-thread-atexit.log`（及 `.exitcode`=133）
- 对照已落地：diag / Result / abort / assert 相关 HLE；勿空桩乱过——先归因调用点与 Result
- 真机：`adb -s RG477M01445122`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已物化 FFRS-Switch.pak/ucas（约 8.32GiB，P-CC-3 PASS）——可读，勿删大文件、勿抢写素材抽取
- 验收不降级；勿动 ProtectRelro 桩除非硬需要

## 当前阻塞
- 符号：`_ZN2nn4diag6detail9AbortImplEv`（`nn::diag::detail::AbortImpl()`）
- symbol index **12**，JMPREL reloc index **3996**，GOT `0x7772058`
- caller offset `0x11b8`，lr offset `0x11bc`（lr=`0x6db9f3d1bc`），基址约 `0x6db9f3c000`
- 故障现场：`signal=5` pc 落未链接 import；寄存器快照见 v31 日志（含 `x0=0x6259` 等）
- exit **133**；日志 `.local/logs/rg477m-boot-v31-cxa-thread-atexit.log`
- 阶段：`T02_CXA_THREAD_ATEXIT_PASSED_ABORT_IMPL_REACHED_ON_RG477M`

## 目标
1. 逆向 AbortImpl 调用点与触发原因（Result/断言/缺失资源等）；优先修根因，必要时落地最小可归因 AbortImpl HLE（打 `[hle]` 日志，保留现场），使 GameThread 越过 import 12
2. native_boundary / guest_process 绑定；宿主契约；RG477M `--boot` 同前参数复测
3. 新日志建议 `rg477m-boot-v32-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
4. 提交检查点；回报 HEAD / 新阻塞 / 日志路径；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空返回掩盖未归因 abort；不下载未授权本体；不删 `.local/romfs` 大文件；不 push

做完停下汇报，等项目经理下一派工。

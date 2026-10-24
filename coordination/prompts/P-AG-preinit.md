# 工单 P-AG-17｜Antigravity｜主路径：越过 GEngineLoop.PreInit / PreInitPreStartupScreen 失败

你是实现 Agent。上一单 P-AG-16 已闭环：HEAD `95d18ef`（分支 `work/rg477m-native`）。真机 v32 已越过 `nn::diag::detail::AbortImpl` / `GetBacktrace`（import 12 消除，不再 signal 5）；成功捕获 UTF-16 致命错误并归因到虚幻引擎 PreInit 失败。新阻塞是引擎初始化本身，不是未绑定 import。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`.local/logs/rg477m-boot-v32-abortimpl.log`（exit **132** / signal 4 `udf #0`）
- 对照已落地：AbortImpl 可归因 HLE、FS/Event/memalign/cxa_thread_atexit、pak/ucas 已物化
- 真机：`adb -s RG477M01445122`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已有 FFRS-Switch.pak/ucas/utoc + global.*（约 8.32GiB）——可读，勿删大文件
- 验收不降级；勿动 ProtectRelro 桩除非硬需要

## 当前阻塞（已归因）
- `LaunchNintendo.cpp:241`：`LowLevelFatalError: PreInit() failed! Aborting`
- 调用链：`GEngineLoop.PreInit`（约 `0x3530700`）→ `FEngineLoop::PreInitPreStartupScreen`（约 `0x3530920`）返回非零 → `cbnz` 跳致命分支 → AbortImpl（已 HLE）→ `0x11f8: udf #0`
- 日志线索（须验证是否根因）：`[hle] nn::fs::OpenFile: path='rom:/FFRS/FFRS.uproject' ... -> open failed errno=2`
- romfs 现状：有 `Engine/Config/StagedBuild_FFRS.ini`；**未见** `FFRS/FFRS.uproject`；Paks 目录四件套齐
- 阶段：`T02_ABORT_IMPL_PASSED_PREINIT_ATTRIBUTED_ON_RG477M`

## 目标
1. 逆向 `PreInitPreStartupScreen` 失败点：缺配置/缺 uproject、Pak/IoStore 挂载与解密、文件系统路径映射、渲染/音频预初始化等；优先修根因（补最小宿主文件、修 Mount/Open 映射、或补必要 HLE），使 PreInit 返回成功或推进到下一明确阻塞
2. 勿空返回掩盖 PreInit 失败；若必须 stub，打清晰 `[hle]`/`[preinit]` 日志并保留可归因现场
3. native_boundary / guest_process 如需新符号则绑定；宿主契约；RG477M `--boot` 同前参数复测
4. 新日志建议 `rg477m-boot-v33-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
5. 提交检查点；回报 HEAD / 新阻塞 / 日志路径；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空过 PreInit；不下载未授权本体；不删 `.local/romfs` 大文件；不 push

做完停下汇报，等项目经理下一派工。

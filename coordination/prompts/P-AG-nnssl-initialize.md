# 工单 P-AG-23｜Antigravity｜主路径：nnsslInitialize（import 606）推进

你是实现 Agent。上一单 P-AG-22 已闭环：HEAD `4393ed9`（分支 `work/rg477m-native`）。真机已越过 `nnResultIsFailure` 及 `nnResult*` 族（import 607）；LibcurlGc 子线程已 CreateThread / SetThreadNamePointer / InitializeEvent / StartThread / SignalEvent / WaitEvent / FinalizeEvent。新阻塞在子线程路径未解析 `nnsslInitialize`。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`（P-AG-22 闭环段）、`.local/logs/rg477m-boot-v44-nn-result-is-failure.log`（及 `.exitcode`=133）
- 对照已落地 HLE 模式：`nnResult*` / `nn::socket::Initialize` / `nn::nifm::Initialize` / `nn::os::*` —— typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已有 FFRS-Switch.pak/ucas（约 8.4GiB）——可读，勿删大文件；真机容器已推过，勿无故重推除非校验缺失
- 验收不降级；勿动 ProtectRelro 桩除非硬需要
- 若仍存在 worktree `task/T04-nvn-firstframe` / `Documents/Codex/2026-10-24/task/ffrs-nvn-firstframe`，勿改其内容

## 当前阻塞（证据）
- `[hle] nn::os::CreateThread: ... name 随后 SetThreadNamePointer(name='LibcurlGc')`
- `[hle] nnResultIsFailure: res=0x0 -> 0`（两次）
- `[hle] nn::os::StartThread` → child guest TLS 绑定 → SignalEvent / WaitEvent / FinalizeEvent
- `[fault] signal=5 pc=0x76f4b2b2f0 fa=0x76f4b2b2f0`
- `[unresolved-import] index=606 name=nnsslInitialize caller_offset=0x5e647e8`
- 故障时寄存器摘录：`x0=0x7454b357d8 x1=0x6e8f07f8f0 x2=0x30 ... lr=0x7454b357e8`（子线程栈上调用；按 dynsym/ABI 核对参与返回值，勿盲信 x0 为第一参数）
- exit **133**；基线日志：`.local/logs/rg477m-boot-v44-nn-result-is-failure.log`
- 阶段：`T02_NN_RESULT_UNBLOCKED_NNSSL_INIT_ON_RG477M`
- 同族导入参考（`evidence/coordinator/nso_imports_main.tsv`）：606 `nnsslInitialize`、608 `nnsslFinalize`，以及后续 `nnsslConnection*` / `nn::ssl::Context::*`——先越过 Initialize；紧邻可推则推，勿一次造完整 TLS 栈

## 目标（按序）
1. 落地最小可用 `nnsslInitialize` HLE（Nintendo SDK C 导出：通常无参或按 ABI 核对，返回 `nn::Result` 成功 0；打清晰 `[hle] nnsslInitialize ...` 日志）。可一并补紧随 `nnsslFinalize`（若真机下一阻塞紧邻）。**勿伪造真实 TLS 握手/证书校验/在线账号**；Initialize/Finalize 成功桩即可先推过 LibcurlGc SSL 子系统初始化。
2. 按真机下一阻塞优先补紧随 ssl 族符号（`nnssl*` / `nn::ssl::*`）；先以越过 index 606 并留下可归因下一阻塞为准，能推则继续推
3. native_boundary / guest_process / nso_host 绑定；宿主契约覆盖 nnsslInitialize（及你一并补的 Finalize）；RG477M `--boot` 沿用既有配额参数复测
4. 新日志建议 `rg477m-boot-v45-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
5. 提交检查点；回报 HEAD / 是否越过 nnsslInitialize / 新阻塞符号 / 日志路径 / exitcode；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push；不伪造付费/在线/完整 TLS 会话态

做完停下汇报，等项目经理下一派工。

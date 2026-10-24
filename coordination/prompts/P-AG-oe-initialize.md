# 工单 P-AG-19｜Antigravity｜主路径：nn::oe::Initialize（及紧随 oe 语言 API）推进

你是实现 Agent。上一单 P-AG-18 已闭环：HEAD `15636ca`（分支 `work/rg477m-native`）。真机已越过 TaskGraph worker SIGSEGV（子线程 guest OS TLS 隔离）、libc++ mutex/cv/clock、`setlocale`/`getenv`/`nl_langinfo`；ICU 本地化与 pak 物理读已发生，atexit 登记约 13,591。新阻塞在 `nn::oe`（Operation Environment）初始化。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`（P-AG-18 闭环段）、`.local/logs/rg477m-boot-v40-setlocale.log`（及 `.exitcode`=133）
- 对照已落地 HLE 模式：`nn::account::Initialize` / `nn::nifm::Initialize` / `nn::socket::Initialize` / AOC SystemEvent（typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约）
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已有 FFRS-Switch.pak/ucas/utoc + global.*（约 8.32GiB）——可读，勿删大文件；真机容器已推过，勿无故重推除非校验缺失
- 验收不降级；勿动 ProtectRelro 桩除非硬需要

## 当前阻塞（证据）
- `[fault] signal=5 pc=0x7a20cc7280 fa=0x7a20cc7280`
- `[unresolved-import] index=80 name=_ZN2nn2oe10InitializeEv`（`nn::oe::Initialize()`）caller_offset=`0x15440`
- 紧邻下一导入：`_ZN2nn2oe18GetDesiredLanguageEv`（index **81**）
- exit **133**；基线日志：`.local/logs/rg477m-boot-v40-setlocale.log`
- 阶段：`T02_TASKGRAPH_SIGSEGV_UNBLOCKED_NN_OE_ON_RG477M`
- 上下文：ICU 已尝试 OpenFile `rom:/Engine/Content/Internationalization/...`（部分 errno=2 可预期）；连续 ReadFile 自 pak；非 TaskGraph SIGSEGV、非 setlocale

## 目标（按序）
1. 落地最小可用 `nn::oe::Initialize` HLE（无参，基线成功返回 0 / ResultSuccess 可接受）；打清晰 `[hle] nn::oe::Initialize ...` 日志；勿伪造完整主机/Applet 生命周期
2. 按真机下一阻塞优先补紧随 oe 符号（至少预期 `GetDesiredLanguage`；语言返回合理默认如 `AmericanEnglish`/`en-US` 对应 SDK 枚举，须有依据或与现有 i18n/`setlocale("C")` 一致）；先以越过 index 80 并留下可归因下一阻塞为准，能推则继续推
3. native_boundary / guest_process 绑定；宿主契约覆盖 Initialize（及一并补的 oe API）；RG477M `--boot` 沿用既有配额参数复测
4. 新日志建议 `rg477m-boot-v41-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
5. 提交检查点；回报 HEAD / 是否越过 oe::Initialize / 新阻塞符号 / 日志路径 / exitcode；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push；不伪造付费/在线/Applet 完整态

做完停下汇报，等项目经理下一派工。

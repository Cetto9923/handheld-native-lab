# 工单 P-AG-11｜Antigravity｜主路径：nn::account::Initialize 及账号子系统推进

你是实现 Agent。上一单 P-AG-10 已闭环：HEAD `724957c`（分支 `work/rg477m-native`）。真机全量 1890/1890 ctor、nnMain、CreateThread/StartThread/SetThreadName(GameThread)、子线程 guest TP + native_boundary、GetHostArgc、`nn::fs::SetAllocator`、`nn::fs::QueryMountRomCacheSize`（64KB）、`nn::fs::MountRom`（name=`rom`，host_path=`romfs`）、`nn::fs::OpenFile`/`GetFileSize`/`ReadFile`/`CloseFile`（`rom:/UECommandLine.txt` 38B）、`nn::aoc::CountAddOnContent`（返回 0）、`nn::aoc::ListAddOnContent`（count=256→0）、`nn::nifm::Initialize`（返回 0）、`nn::socket::Initialize`（buffer=6MB alloc=128KB concurrency=14 → 0）均已越过。新阻塞在账号子系统初始化。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v26-socketinit.log`
- 对照已落地 HLE 模式：`nn::socket::Initialize` / `nn::nifm::Initialize`（typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约）
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓可正常提交检查点（仅在验证成功的 HLE 后）
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题
- 勿抢写已声明保护的并行路径；本单在主树 `work/rg477m-native` 推进
- 勿动 `ProtectRelro` 桩，除非越过本阻塞硬性需要；默认保持现状

## 当前阻塞（证据）
- GameThread 已完整：MountRom → OpenFile → … → CountAddOnContent(0) → ListAddOnContent(0) → nifm::Initialize(0) → socket::Initialize(0) → fault
- `[fault] signal=5`（未绑定 import 的 sentinel brk）；例 pc `0x717b66fb60`；lr `0x6ed8d1a53c`
- 符号：`_ZN2nn7account10InitializeEv`（`nn::account::Initialize()`）
- symbol index=364；reloc index≈349；JMPREL/GOT 槽≈`0x7772ae8`；caller_offset≈`0x352f538` / lr_offset≈`0x352f53c`
- exit **133**；日志：`.local/logs/rg477m-boot-v26-socketinit.log` + `.exitcode`
- 阶段：`T02_SOCKET_INIT_PASSED_ACCOUNT_INIT_REACHED_ON_RG477M`
- 基线合理行为：Initialize 成功返回 0（ResultSuccess）；记录可归因 `[hle] nn::account::Initialize ... -> returning 0`；勿伪造登录态/付费账号/在线商店；失败须可归因；可按真机下一阻塞按需补 account 集群相关符号，但先以越过本符号并留下可归因下一阻塞为准

## 目标（按序）
1. 落地最小可用 `nn::account::Initialize` HLE：按 ABI 初始化账号子系统（无参，基线成功返回 0 可接受）；必要时按真机下一阻塞补紧随 account 符号，但先以越过 import 364 并留下可归因下一阻塞为准
2. 子线程（GameThread）路径上的导入分派须继续走已有 `native_boundary` 槽表 / `guest_process` 绑定
3. 宿主契约能覆盖最小 account Initialize（及你一并补的 account）用例；RG477M 复跑 `--boot`（沿用 `--guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216`），日志证明越过 `nn::account::Initialize`；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 捕获新日志到 `.local/logs/`（建议 `rg477m-boot-v27-*.log`）+ `.exitcode`；更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点（清晰 message）；回报：HEAD、是否越过 account::Initialize、新阻塞符号、日志路径、exitcode
6. 做完停下汇报，等项目经理下一派工；不要向用户 ask_user

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、大包 Pak/IoStore 读闭环未完成

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据；不 root/刷机
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志
- 不要伪造 Nintendo Account 登录成功、付费权益或在线商店状态

做完停下汇报，等项目经理下一派工。

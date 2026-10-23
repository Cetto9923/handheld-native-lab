# 工单 P-AG-10｜Antigravity｜主路径：nn::socket::Initialize 及 Socket 子系统推进

你是实现 Agent。上一单 P-AG-9 已闭环：HEAD `28cde16`（分支 `work/rg477m-native`）。真机全量 1890/1890 ctor、nnMain、CreateThread/StartThread/SetThreadName(GameThread)、子线程 guest TP + native_boundary、GetHostArgc、`nn::fs::SetAllocator`、`nn::fs::QueryMountRomCacheSize`（64KB）、`nn::fs::MountRom`（name=`rom`，host_path=`romfs`）、`nn::fs::OpenFile`/`GetFileSize`/`ReadFile`/`CloseFile`（`rom:/UECommandLine.txt` 38B）、`nn::aoc::CountAddOnContent`（返回 0）、`nn::aoc::ListAddOnContent`（count=256→0）、`nn::nifm::Initialize`（返回 0）均已越过。新阻塞在 Socket 子系统初始化。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v25-nifminit.log`
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓可正常提交检查点
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题
- 勿抢写已声明保护的并行路径；本单在主树 `work/rg477m-native` 推进
- 勿动 `ProtectRelro` 桩，除非越过本阻塞硬性需要；默认保持现状

## 当前阻塞（证据）
- GameThread 已完整：MountRom → OpenFile → … → CountAddOnContent(0) → ListAddOnContent(0) → nifm::Initialize(0) → fault
- `[fault] signal=5`（未绑定 import 的 sentinel brk）；例 pc `0x7c6ef23888`；lr `0x79ce0214f8`
- 符号：`_ZN2nn6socket10InitializeEPvmmi`（`nn::socket::Initialize(void *work_buffer, size_t work_buffer_size, size_t allocator_size, int concurrency)`）
- symbol index=273；reloc index≈267；JMPREL/GOT 槽≈`0x7772858`；caller_offset / lr_offset≈`0x352f4f8`
- 真机现场实参（v25）：`x0=0x79d27b3000`（work_buffer）、`x1=0x600000`（6MB）、`x2=0x20000`（128KB）、`x3=0xe`（concurrency=14）
- exit **133**；日志：`.local/logs/rg477m-boot-v25-nifminit.log` + `.exitcode`
- 阶段：`T02_NIFM_INIT_PASSED_SOCKET_INIT_REACHED_ON_RG477M`
- 基线合理行为：Initialize 成功返回 0（ResultSuccess）；记录 work_buffer/size/allocator/concurrency 到可归因 `[hle]` 日志；勿伪造在线账号/付费网络；失败须可归因；可按真机下一阻塞按需补 socket 集群相关符号，但先以越过本符号并留下可归因下一阻塞为准

## 目标（按序）
1. 落地最小可用 `nn::socket::Initialize` HLE：按 ABI 接受 work_buffer / sizes / concurrency，基线成功返回 0 可接受；必要时按真机下一阻塞补紧随 socket 符号，但先以越过 import 273 并留下可归因下一阻塞为准
2. 子线程（GameThread）路径上的导入分派须继续走已有 `native_boundary` 槽表 / `guest_process` 绑定
3. 宿主契约能覆盖最小 socket Initialize（及你一并补的 socket）用例；RG477M 复跑 `--boot`（沿用 `--guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216`），日志证明越过 `nn::socket::Initialize`；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 捕获新日志到 `.local/logs/`（建议 `rg477m-boot-v26-*.log`）+ `.exitcode`；更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点（清晰 message）；回报：HEAD、是否越过 socket::Initialize、新阻塞符号、日志路径、exitcode
6. 做完停下汇报，等项目经理下一派工；不要向用户 ask_user

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、大包 Pak/IoStore 读闭环未完成

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据；不 root/刷机
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志

做完停下汇报，等项目经理下一派工。

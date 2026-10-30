# 工单 P-AG-13｜Antigravity｜主路径：nn::os::InitializeEvent 及 Event 同步原语推进

你是实现 Agent。上一单 P-AG-12 已闭环：HEAD `79721fc`（分支 `work/rg477m-native`，工作区 clean）。真机全量 1890/1890 ctor、nnMain、CreateThread/StartThread/SetThreadName(GameThread)、子线程 guest TP + native_boundary、GetHostArgc、`nn::fs::SetAllocator`、`nn::fs::QueryMountRomCacheSize`（64KB）、`nn::fs::MountRom`（name=`rom`，host_path=`romfs`）、`nn::fs::OpenFile`/`GetFileSize`/`ReadFile`/`CloseFile`（`rom:/UECommandLine.txt` 38B）、`nn::aoc::CountAddOnContent`（0）、`nn::aoc::ListAddOnContent`（0）、`nn::nifm::Initialize`（0）、`nn::socket::Initialize`（0）、`nn::account::Initialize`（0）、`nn::fs::OpenDirectory`/`GetDirectoryEntryCount`/`ReadDirectory`/`CloseDirectory`（`rom:/` 枚举 7 项）均已越过。新阻塞在 OS Event 初始化。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v28-opendirectory.log`
- 对照已落地 HLE 模式：`nn::os::CreateThread` / mutex / RWLock（typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约）
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓 clean 时可正常提交检查点（仅在验证成功的 HLE 后）
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题
- 勿抢写已声明保护的并行路径；本单在主树 `work/rg477m-native` 推进
- 勿动 `ProtectRelro` 桩，除非越过本阻塞硬性需要；默认保持现状
- 不要伪造或下载游戏本体/密钥；只使用设备上已有 romfs/素材路径

## 当前阻塞（证据）
- GameThread 已完整越过 OpenDirectory 枚举链后 fault
- `[fault] signal=5`（未绑定 import 的 sentinel brk）；例 pc 见 v28 日志
- 符号：`_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE`（`nn::os::InitializeEvent(nn::os::EventType*, bool, nn::os::EventClearMode)`）
- symbol/import index=**132**；GOT≈`0x7772418`；caller_offset≈`0x25bcc` / lr_offset≈`0x25bd0`
- 实参（v28）：`x0=0x6c0649a330`（EventType*）、`x1=0x0`（initially_signaled=false）、`x2=0x1`（EventClearMode_AutoClear）
- exit **133**；日志：`.local/logs/rg477m-boot-v28-opendirectory.log` + `.exitcode`
- 阶段：`T02_FS_OPENDIRECTORY_PASSED_OS_INITIALIZEEVENT_REACHED_ON_RG477M`
- 基线合理行为：按 Horizon/SDK ABI 初始化 EventType（含 clear mode / signaled 初态）；失败返回可归因 Result，并打 `[hle] nn::os::InitializeEvent: ...` 日志；若紧随必现 SignalEvent/WaitEvent/ClearEvent/FinalizeEvent/TimedWait 等，可一并最小落地，但以越过 import 132 并留下可归因下一阻塞为准

## 目标（按序）
1. 落地最小可用 `nn::os::InitializeEvent` HLE；必要时一并补紧随必现的 Event 相关 import，但先以越过 import 132 为准
2. 子线程（GameThread）路径上的导入分派须继续走已有 `native_boundary` 槽表 / `guest_process` 绑定
3. 宿主契约覆盖最小 InitializeEvent（及你一并补的 Event 原语）用例；RG477M 复跑 `--boot`（沿用 `--guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216`），日志证明越过 `nn::os::InitializeEvent`；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 捕获新日志到 `.local/logs/`（建议 `rg477m-boot-v29-*.log`）+ `.exitcode`；更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点（清晰 message）；回报：HEAD、是否越过 InitializeEvent、新阻塞符号、日志路径、exitcode
6. 做完停下汇报，等项目经理下一派工；不要向用户 ask_user

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、大包 Pak/IoStore 读闭环未完成
- `rom:/FFRS/FFRS.uproject` 在 GetEntryType 上曾报缺失（errno=2）；记录证据即可，不要伪造文件冒充可玩

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据；不 root/刷机
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志
- 不要下载/拷贝未授权游戏本体或密钥进仓库

做完停下汇报，等项目经理下一派工。

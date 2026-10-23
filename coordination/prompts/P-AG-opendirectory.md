# 工单 P-AG-12｜Antigravity｜主路径：nn::fs::OpenDirectory 及目录枚举推进

你是实现 Agent。上一单 P-AG-11 已闭环：HEAD `0c319c2`（分支 `work/rg477m-native`，工作区 clean）。真机全量 1890/1890 ctor、nnMain、CreateThread/StartThread/SetThreadName(GameThread)、子线程 guest TP + native_boundary、GetHostArgc、`nn::fs::SetAllocator`、`nn::fs::QueryMountRomCacheSize`（64KB）、`nn::fs::MountRom`（name=`rom`，host_path=`romfs`）、`nn::fs::OpenFile`/`GetFileSize`/`ReadFile`/`CloseFile`（`rom:/UECommandLine.txt` 38B）、`nn::aoc::CountAddOnContent`（0）、`nn::aoc::ListAddOnContent`（0）、`nn::nifm::Initialize`（0）、`nn::socket::Initialize`（0）、`nn::account::Initialize`（0）均已越过。`nn::fs::GetEntryType` 已有 HLE：对 `rom:/FFRS/FFRS.uproject` 记录 stat failed errno=2。新阻塞在目录打开。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v27-accountinit.log`
- 对照已落地 HLE 模式：`nn::fs::OpenFile` / `GetEntryType` / `MountRom`（typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约）
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓 clean 时可正常提交检查点（仅在验证成功的 HLE 后）
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题
- 勿抢写已声明保护的并行路径；本单在主树 `work/rg477m-native` 推进
- 勿动 `ProtectRelro` 桩，除非越过本阻塞硬性需要；默认保持现状
- 不要伪造或下载游戏本体/密钥；只使用设备上已有 romfs/素材路径。若缺 `FFRS.uproject` 等文件，在日志中可归因说明，优先让 OpenDirectory 链路可诊断地前进

## 当前阻塞（证据）
- GameThread 已完整：MountRom → OpenFile 链 → CountAddOnContent(0) → ListAddOnContent(0) → nifm::Initialize(0) → socket::Initialize(0) → account::Initialize(0) → GetEntryType(`rom:/FFRS/FFRS.uproject`，errno=2) → fault
- `[fault] signal=5`（未绑定 import 的 sentinel brk）；例 pc `0x72491d4128`
- 符号：`_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci`（`nn::fs::OpenDirectory(DirectoryHandle*, char const*, int)`）
- symbol/import index=37；GOT≈`0x7772120`；caller_offset≈`0x11c40` / lr_offset≈`0x11c44`
- 实参（v27）：`x0=0x69df680758`（&DirectoryHandle）、`x1=0x69df680630`（path）、`x2=0x3`（`OpenDirectoryMode_All`）；`x7` 含 UTF-16LE `rom:`
- exit **133**；日志：`.local/logs/rg477m-boot-v27-accountinit.log` + `.exitcode`
- 阶段：`T02_ACCOUNT_INIT_PASSED_FS_OPENDIRECTORY_REACHED_ON_RG477M`
- 基线合理行为：按 path/mode 打开已挂载 `rom` 下目录，写出合法 DirectoryHandle；失败返回可归因 nn::fs Result（如 PathNotFound），并打 `[hle] nn::fs::OpenDirectory: path=... mode=...` 日志；勿空桩返回 0 后立刻不可诊断崩溃

## 目标（按序）
1. 落地最小可用 `nn::fs::OpenDirectory` HLE：校验 DirectoryHandle*、path、mode；在已挂载 `rom` 上打开目录；必要时一并补紧随必现的 `ReadDirectory` / `ReadDirectoryEntry` / `GetDirectoryEntryCount` / `CloseDirectory` 等目录枚举 import，但先以越过 import 37 并留下可归因下一阻塞为准
2. 子线程（GameThread）路径上的导入分派须继续走已有 `native_boundary` 槽表 / `guest_process` 绑定；复用 MountRom 挂载表与 OpenFile/GetEntryType 的 host_path 解析
3. 宿主契约能覆盖最小 OpenDirectory（及你一并补的目录枚举）用例；RG477M 复跑 `--boot`（沿用 `--guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216`），日志证明越过 `nn::fs::OpenDirectory`；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 捕获新日志到 `.local/logs/`（建议 `rg477m-boot-v28-*.log`）+ `.exitcode`；更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点（清晰 message）；回报：HEAD、是否越过 OpenDirectory、新阻塞符号、日志路径、exitcode
6. 做完停下汇报，等项目经理下一派工；不要向用户 ask_user

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、大包 Pak/IoStore 读闭环未完成
- `rom:/FFRS/FFRS.uproject` 在 v27 上 GetEntryType 已报缺失（errno=2）；若真机 romfs 缺内容，记录证据，不要伪造文件冒充可玩

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据；不 root/刷机
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志（至少打印 path/mode/handle）
- 不要下载/拷贝未授权游戏本体或密钥进仓库

做完停下汇报，等项目经理下一派工。

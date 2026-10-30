# 工单 P-AG-7｜Antigravity｜主路径：nn::fs::OpenFile + RomFS 读文件推进

你是实现 Agent。上一单 P-AG-6 已闭环：HEAD `880be5f`（分支 `work/rg477m-native`，工作区 clean）。真机全量 1890/1890 ctor、nnMain、CreateThread/StartThread/SetThreadName(GameThread)、子线程 guest TP + native_boundary、GetHostArgc、`nn::fs::SetAllocator`、`nn::fs::QueryMountRomCacheSize`（64KB）、`nn::fs::MountRom`（name=`rom`，buffer 已分配，返回 0）均已越过。新阻塞在 RomFS 文件打开。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v22-mountrom.log`
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓 clean 时可正常提交检查点
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题
- 勿抢写已声明保护的并行路径（其他 Codex/Claude worktree 的未提交 WIP 勿覆盖）；主树当前 clean，本单在主树 `work/rg477m-native` 推进

## 当前阻塞（证据）
- `[hle] nn::fs::MountRom: mounted 'rom' buffer=0x69f0c10000 size=65536 (0x10000) host_path='.'` 已成功（GameThread）
- 随后 `[fault] signal=5`（未绑定 import 的 sentinel brk）pc=`0x74b7e54040`
- 符号：`_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci`（`nn::Result OpenFile(nn::fs::FileHandle*, char const*, int)`）
- symbol index=10；caller_offset≈`0x5864` / lr≈`0x5868`（相对 base，例 base=`0x7210d74000` lr=`0x7210d79868`）；PLT≈`0x5e67b40`
- 实参：`x0=&handle`（例 `0x6c51080d48`）、`x1=path`（例 `0x6c51080c20`）、`x2=1`（`OpenMode_Read`）；`x7` 含 UTF-16LE `rom:`
- exit **133**；日志：`.local/logs/rg477m-boot-v22-mountrom.log` + `.exitcode`（约 04:57 CST）
- 注意：当前 MountRom 的 `host_path='.'` 可能不足以支撑真实 RomFS 读；OpenFile 落地时须把挂载根指到仓库已有合法素材目录（只使用已有路径/环境变量约定，不获取新游戏/密钥），并记录实际打开的 path 字符串到 HLE 日志，便于下一阻塞归因

## 目标（按序）
1. 落地最小可用 `nn::fs::OpenFile` HLE：按签名校验 `FileHandle*`、path、mode；在已挂载的 `rom` 上打开可读文件，写出合法 handle；必要时一并补紧随后必现的 `GetFileSize` / `ReadFile` / `CloseFile` / `GetEntryType` 等 fs 读路径 import，但先以越过本符号并留下可归因下一阻塞为准
2. 子线程（GameThread）路径上的导入分派须继续走已有 `native_boundary` 槽表 / `guest_process` 绑定；继续复用 MountRom 挂载表与 SetAllocator 回调
3. 宿主契约能覆盖最小 OpenFile（及你一并补的读路径）用例；RG477M 复跑 `--boot`（沿用 `--guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216`），日志证明越过 OpenFile；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点；回报：HEAD、是否越过 OpenFile、新阻塞符号、日志路径、exitcode

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、RomFS/IoStore 完整读闭环未完成

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志（至少打印 path/mode/handle）

做完停下汇报，等项目经理下一派工。

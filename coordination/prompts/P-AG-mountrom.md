# 工单 P-AG-6｜Antigravity｜主路径：nn::fs::MountRom + RomFS 挂载推进

你是实现 Agent。上一单 P-AG-5 已闭环：HEAD `5dec1ac`（分支 `work/rg477m-native`，工作区 clean）。真机全量 1890/1890 ctor、nnMain、CreateThread/StartThread/SetThreadName(GameThread)、子线程 guest TP + native_boundary、GetHostArgc、`nn::fs::SetAllocator`、`nn::fs::QueryMountRomCacheSize`（写出 64KB）及 UE 分配 64KB 缓存页均已越过。新阻塞在 RomFS 正式挂载。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v21-querymountrom.log`
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓 clean 时可正常提交检查点
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题
- 勿抢写已声明保护的并行路径（若有其他 worktree 的未提交 WIP 勿覆盖）

## 当前阻塞（证据）
- `[hle] nn::fs::QueryMountRomCacheSize: out_size=… -> 65536 (0x10000)` 已成功（GameThread）
- `[page-port-policy] op=map address=… bytes=65536 result=0` 缓存页已分配
- 随后 `[fault] signal=5`（未绑定 import 的 sentinel brk）
- 符号：`_ZN2nn2fs8MountRomEPKcPvm`（`nn::fs::MountRom(char const*, void*, unsigned long)`）
- index=22；caller_offset≈0x5590；lr≈0x5594（相对 base，例 base=`0x746eb80000` lr=`0x746eb85594`）；PLT≈0x5e62700
- 实参：`name="rom"`（x0）、`buffer`（x1，例 `0x6c4ea10000`）、`size=0x10000`（x2）
- exit **133**；日志：`.local/logs/rg477m-boot-v21-querymountrom.log` + `.exitcode`（约 04:22 CST）

## 目标（按序）
1. 落地最小可用 `nn::fs::MountRom` HLE：按签名 `nn::Result MountRom(char const* name, void* buffer, size_t size)` 校验参数，挂载合法本地 RomFS/素材路径（只使用仓库已有合法素材，不获取新游戏/密钥），使 GameThread 能继续启动序列；必要时一并补紧随其后必现的 fs/open 相关 import，但先以越过本符号并留下可归因下一阻塞为准
2. 子线程（GameThread）路径上的导入分派须继续走已有 `native_boundary` 槽表 / `guest_process` 绑定；继续复用已保存的 `SetAllocator` 回调与 QueryMountRomCacheSize 约定的 64KB 缓存语义
3. 宿主契约能覆盖最小路径；RG477M 复跑 `--boot`（沿用既有 `--guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216` 等已验证参数），日志证明越过 MountRom；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点；回报：HEAD、是否越过 MountRom、新阻塞符号、日志路径、exitcode

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、RomFS/IoStore 运行时完整挂载未闭环

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志

做完停下汇报，等项目经理下一派工。

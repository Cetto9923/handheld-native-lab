# 工单 P-AG-8｜Antigravity｜主路径：nn::aoc::CountAddOnContent 及 AOC/DLC 集群推进

你是实现 Agent。上一单 P-AG-7 已闭环：HEAD `e2896e6`（分支 `work/rg477m-native`，工作区 clean）。真机全量 1890/1890 ctor、nnMain、CreateThread/StartThread/SetThreadName(GameThread)、子线程 guest TP + native_boundary、GetHostArgc、`nn::fs::SetAllocator`、`nn::fs::QueryMountRomCacheSize`（64KB）、`nn::fs::MountRom`（name=`rom`，host_path=`romfs`）、`nn::fs::OpenFile`/`GetFileSize`/`ReadFile`/`CloseFile`（已读取 `rom:/UECommandLine.txt` 38B）均已越过。新阻塞在 AOC/DLC 计数。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单、`.local/logs/rg477m-boot-v23-openfile.log`
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓 clean 时可正常提交检查点
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题
- 勿抢写已声明保护的并行路径；主树当前 clean，本单在主树 `work/rg477m-native` 推进

## 当前阻塞（证据）
- GameThread 已完整：MountRom → OpenFile(`rom:/UECommandLine.txt`) → GetFileSize → ReadFile(38) → CloseFile → deferred atexit
- 随后 `[fault] signal=5`（未绑定 import 的 sentinel brk）pc 例 `0x7cf3782198`；lr 例 `0x7a4d5343dc`
- 符号：`_ZN2nn3aoc17CountAddOnContentEv`（`nn::aoc::CountAddOnContent()`）
- symbol index=51；caller_offset≈`0x93dc` / lr≈`0x7a4d5343dc`（例 base=`0x7a4d52b000`）；JMPREL 槽≈`0x7772190`；实参 `x0=0`
- 相关 AOC/DLC 集群（可按真机下一阻塞按需补，勿一次瞎桩全开）：
  - `_ZN2nn3aoc17CountAddOnContentEv`（51）
  - `_ZN2nn3aoc16ListAddOnContentEPiii`（52）
  - `_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi`（53）
  - `_ZN2nn2fs17MountAddOnContentEPKciPvm`（54）
- exit **133**；日志：`.local/logs/rg477m-boot-v23-openfile.log` + `.exitcode`（约 05:26 CST）
- 基线无 DLC 时合理返回：Count=0；List 写 0 项；勿伪造已购 DLC；失败须可归因日志

## 目标（按序）
1. 落地最小可用 `nn::aoc::CountAddOnContent` HLE：无参/按 ABI 返回 addon 数量（基线 0 可接受）；必要时按真机下一阻塞补 `ListAddOnContent` / `QueryMountAddOnContentCacheSize` / `MountAddOnContent`，但先以越过本符号并留下可归因下一阻塞为准
2. 子线程（GameThread）路径上的导入分派须继续走已有 `native_boundary` 槽表 / `guest_process` 绑定
3. 宿主契约能覆盖最小 CountAddOnContent（及你一并补的 AOC）用例；RG477M 复跑 `--boot`（沿用 `--guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216`），日志证明越过 CountAddOnContent；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点；回报：HEAD、是否越过 CountAddOnContent、新阻塞符号、日志路径、exitcode

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、大包 Pak/IoStore 读闭环未完成

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志

做完停下汇报，等项目经理下一派工。

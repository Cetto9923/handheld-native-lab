# RG477M 新建游戏退出复核

## 实际构建与复现

- 回归包为 `a0f402e`，1.0.11-preview，APK SHA-256 `f2375446c3567967f7f45d606b292b6823ededa066b637feb06d93257c42c75e`。
- 设备为 RG477M01445122，Mali-G615 MC6，Android Vulkan API 1.3.247。
- 用户澄清入口为新建游戏。PID7985、9495 都主动退出70。根在同一APK将 `gfx-subgroup-narrow` 从1改0，新建游戏PID13124仍复现相同退出。
- 当前会话的 `engine.log.fatal` 为 `Texel format lacks formatless-write support`。退出前 `b673e10f3da2451c` 的 b41/b47 typed-storage 请求失败。
- 原始取证位于 `.local/codex-recovery-20261114/crash-a0f402e-20261114-175815`，同包控制位于 `crash-ab-narrow0`。engine.log追加多个会话，guest_boot/系统退出记录/PID锚用于分离。engine.log.prev属于更早进程，不作为本次对照。

## 直接原因与修复

真实模块经整数修复后，b41需要R32ui。游戏已经为b53声明了相同的R32ui类型，但该声明位于b41变量之后。复用检查拒绝该顺序，模块维持formatless，随后触发设备不支持formatless texel写入的fatal。

`03ab3e6` 在transactional declare的candidate中前移既有image及pointer声明，保持ID及依赖合法，再重新分析全部绑定与use。失败时保留原始模块。首次拷贝、写入值、坐标、原子操作及opaque-use拒绝均保留。narrow不是该拒绝的首次引入点，整数修复后已可独立复现。

独立worker对整数、整数+narrow、完整优化三阶段直接调用生产header，typed转换成功且spirv-val均退出0。完整模块仍125732字节、ID bound7231。8026条函数指令仅一处OpLoad类型ID变更，b53原子操作逐字相同。证据位于 `.local/worker-crash-repro/updated-header/verification.json`。

宿主76项检查通过，补测了image/pointer都较晚、仅pointer较晚、opaque CopyObject拒绝、后续失败不发布半成品。该次未配置的可选真实模块检查不计入76项。证据位于 `.local/codex-recovery-20261114/crash-host-final/host/summary.json`。

## 真机覆盖

- 修复包1.0.12-preview来自03ab3e6，SHA-256 `afd244a47309dc492a24f8b0543456afd06ad0540e80a9da0235db86a5a2ed59`，构建退出0，安装后回读摘要一致。
- 安装前后存档tar摘要均为 `dd66a544555a9843bafc0485db5b011d50354f008a0525cc51e7aefd8ee0130a`。
- PID18110实际执行新建游戏、普通难度、开场晶体/战争/火场过场并到达可操作火场阶梯，已越过原b673退出点。当前guest_boot有typed variant成功且无FATAL。用户独立反馈不闪退。
- 根在可操作火场做短距离上/下走动，角色及小地图方向变化，PID维持18110。记录位于 `crash-build-final/movement`。
- 本结论仅覆盖该新建游戏流程。第三章读档、水都闪烁、战斗、长期稳定性和其他芯片仍待回归。新建游戏的正常自动存档另保留备份，手动01号档未由测试写入。

## 仍在处理的卡顿与断音

用户现场反馈短暂卡顿式断音。PID18110的frame2761..4560共85.914秒、20.951FPS，P99为70.753ms。该暖态窗口无新pipeline/shader，主6声道xrun继续增加。之后火场可操作机位约30FPS，场景负载不同，不能将差异算作修复收益。

8帧pass-live改变了GPUresident诊断执行路径，仅作该诊断图的结构数据。常态178pass与诊断39pass之间的差异尚不能全部归因某个shader。已派工验证uniform-texel pending重复copy的开销，保持首次copy及屏障并做同包开关对照。

当前主声轨在扬声器DEEP_BUFFER路由。独立shell UID的AAudio组件probe只在2ch callback+LOW请求得到actual LOW；6ch blocking请求LOW实际仍NONE且burst变化。本应用资格和断音改善仍需同包实测，组件probe不作为音频验收。

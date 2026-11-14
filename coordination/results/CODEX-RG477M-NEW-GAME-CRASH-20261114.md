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
- 本结论仅覆盖该新建游戏流程。第三章读档、水都闪烁、战斗、长期稳定性和其他芯片仍待回归。截至首段走动，存档备份tar摘要仍与安装前相同，手动01号档未由测试写入。

## 仍在处理的卡顿与断音

用户现场反馈短暂卡顿式断音。PID18110的frame2761..4560共85.914秒、20.951FPS，P99为70.753ms。该暖态窗口无新pipeline/shader，主6声道xrun继续增加。之后火场可操作机位约30FPS，场景负载不同，不能将差异算作修复收益。

8帧pass-live改变了GPUresident诊断执行路径，仅作该诊断图的结构数据。常态178pass与诊断39pass之间的差异尚不能全部归因某个shader。已派工验证uniform-texel pending重复copy的开销，保持首次copy及屏障并做同包开关对照。

当前主声轨在扬声器DEEP_BUFFER路由。独立shell UID的AAudio组件probe只在2ch callback+LOW请求得到actual LOW；6ch blocking请求LOW实际仍NONE且burst变化。本应用资格和断音改善仍需同包实测，组件probe不作为音频验收。

## 后续BOSS场景退出仍未修复

用户澄清故障在首个新型魔导兵器击败后继续前进，后续大型人形装甲敌人的场景。首战胜利并回到火场台阶尚未覆盖故障点。

- `576d5cc` / 1.0.13-preview，PID7041，frame15297退出70，fatal为 `Invalid indirect draw parameters: -3`。当时缺原始参数，不能确定是哪一个字段越界。证据为 `.local/codex-recovery-20261114/boss-crash-576d5cc-20261114-185144/`。
- `8b13bed` / 1.0.15-preview，PID22127，frame27510退出70，fatal为 `Invalid or excessive buffer size`。退出前请求地址0x6fe16b0000、8053063680字节。该运行关闭texelcopy复用，音频source nice0、LOW请求0。证据为 `.local/codex-recovery-20261114/boss-crash-8b13bed-20261114-192517/`。
- 两次均紧邻924b、5bf9、d56三个compute与7ca7/0a3图形着色器载入。当前控制组完整会话的 `INDIRECT_EMPTY` 计数为0。零绘制语义修正未触发本次故障，将它视为本次BOSS根因的推测撤回。copied-texel缓存关闭仍复现，也不能依据先后关系归因该优化。

5bf9的B29静态写入布局为68字节记录，两处五word绘制参数在字节12和32，indexCount均为 `3*M*K*B mod 2^32`，K另存记录word14。d56的B35写入是打包索引展开。现场draw参数地址与该记录的对应关系尚未取得，8GB索引请求也尚待明确记录index type/count，不能只凭字节数反推归因。

独立检查另确认，native R32_UINT view上部分float sampled-buffer消费者因strict整数改写形状拒绝而保持浮点模块。Vulkan要求view数值类型与OpTypeImage Sampled Type匹配，见 [VUID format-07753](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDispatchBase.html)。本轮准备保持原始float消费指令的direct integer fetch加bitcast修正，需通过宿主、真实SPV及GPU契约后再装包，同一BOSS位置尚待回归。

组合integer与typed-storage变体还有独立身份缺陷，现runtime的typed identity丢失ib配方，旧stb解析前缀长度也错误。当前两次运行的三compute均无对应warm记录且无观察到的身份并存碰撞，故该缺陷未被认定为这两次BOSS根因。修复将保留组合配方，避免后续缓存恢复错误模块。

## 1.0.16更早流程命令缓冲容量退出

`26bb311`候选1.0.16-preview的APK摘要为 `ecc7114a154ca2c6f6b6d3f44c8c064b7aad9d1bfc49022b83597ffe87853b4f`。宿主95项、Android构建及真实RG477M GPU三case24项契约均通过，安装前后存档摘要一致。用户自行推进，根只读记录。

PID22836于20:02:08.724主动退出134，最后完成约frame4480。该会话尚未载入5bf9/924b/d56/7ca目标着色器，原BOSS故障点仍未覆盖。同次guest_boot第85103行起的错误链为命令缓冲表满、Initialize失败、EndRecording返回空handle、QueueSubmit拒绝handle0、SyncWait超时，随后游戏在NVNSubmission.cpp315调用AbortImpl退出。

`nvn_command_buffer.c`将同时活动的录制器限制在静态32槽，已有槽及finalized槽可复用，32槽均活动时直接拒绝。当前准备保持槽地址稳定的分块扩展，保留包寿命和finalize复用，回调解锁期间也保留原slot地址。

本次证据在 `.local/codex-recovery-20261114/boss-crash-26bb311-20261114-200232/`，完整补采在 `exit-26bb311-full-recapture-20261114-200651/`。初次本地engine.log与pull命令输出同名发生覆盖，已从设备完整重新提取41414779字节，命令输出改为独立文件，源日志摘要与metadata已保留。`engine.log.fatal`的mtime仍属于PID22127旧会话，该文件的8GB错误不得用于本次归因。

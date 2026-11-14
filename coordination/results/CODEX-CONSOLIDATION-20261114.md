# 2026-11-14 分支归并及回归修复记录

用户要求本项目收敛到 `work/rg477m-native` 交付主干和 `task/T-codex-recovery-20261114` 开发分支，保留历史 APK。用户授权本轮使用 RG477M01445122，并已停止两个外部写者。

## 输入与恢复点

- Git bundle、各工作树 index/worktree 差分及未跟踪文件备份在 Honor 树 `.local/codex-recovery-20261114/branch-archive/`。
- 完整分支、提交和 patch 等价审查索引为该目录的 `branch-audit.json`。
- 用户确认角色正常的 MiniMax APK SHA256 为 `9083174963c40ce1c7d1726671fa2ffd4e00f573efad516d80def5b3a10cf881`。已从设备回读备份，构建源码对应 `ef33490` 加随后提交到 `ddeadec` 的源代码改动。
- Antigravity APK SHA256 为 `6eec4e94d3ff9cc7544c16b2fc6c793494103aab5986cdfdc2e4a9d05753f40f`，已单独备份。
- 两次采集之间 MiniMax 安装了新包，原 `perf-sample` 开关 A/B 作废，后续同包重测。

## 归并规则

历史提交全部保存在归并历史和 bundle。当前代码逐项选择有效实现，重复 patch 保留当前实现，过时协调记录作为历史输入留存。

| 来源 | 当前实现处置 |
|---|---|
| stability、sync、shared-texel-split、texelfix | 已有等价 patch，保留当前主干实现 |
| transit、rescue | 定向 event/readback 退休、预热节流和配方落盘已等价合入 |
| crash 分支 `92d672a` | 吸收非对齐 Event 字段 memcpy、8 字节对齐契约和可检索诊断 |
| `e4fe8ff` 粒子修复 | 吸收五组粒子 uint fetch 事实，补正常入口、其它 float binding 保持和真实 SPIR-V 验证 |
| `e4fe8ff` 全局 RGBA32UI 回退 | 保留原生 R16_UINT/R16ui，原始绑定与地址步长有直接证据 |
| `ddeadec` 深度修复 | 保留游戏原深度，撤回固定 depth bias。其新自动判定器存在 mixed-store、CompositeExtract 和 specialization 问题，本轮正常路径完整保留游戏深度 |
| `b6e487b` 多设备修补 | 采用主干已有严格事件同步和已确认音频行为，设备归因继续等待目标机日志 |
| `d19a037` 格式支持 | 按当前接口补 Rg32/Rg16 扩展格式能力门禁及重复类型处理，保留原生位宽和 shared split |
| 旧 moon/draw/report 专用诊断 | 现有 probe/ubtrace 已覆盖主要用途，完整代码保存在历史 |
| package 分支旧协调 WIP | 保存差分及报告，旧 state/HANDOFF 保留为历史资料 |

`3b6302a58b86a34a` 的原始绑定为 R16_UINT。两次 OpImageWrite 的 vec4 载体均为同值复制，不能由载体分量数推导为 16 字节 RGBA32UI texel。

## 当前验收状态

- 粒子宿主正常入口、ASan/UBSan、十份原始或重写模块 spirv-val 已执行通过。
- 角色正常依据用户对 MiniMax 包的反馈，本轮整合包待独立复测。
- 闪烁、卡顿、召唤及跨设备闪退继续定位和验收。
- 验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。

## a49c62c 整合构建检查点

- 完整宿主验证86/86通过，包含真实模块夹具与粒子重写。Android APK、签名、ELF及打包检查退出0。构建前后源码及工作区一致。
- 候选 APK 为 `.local/codex-recovery-20261114/apk-history/ffrs-1.0.11-preview-a49c62c.apk`，SHA256 `cda987f9a51b142ccc148a62cf44e7d7de2e3ee547da35cf50ec9fdcabb30df5`。构建和输入环境保存在 `.local/codex-recovery-20261114/integration-a49c62c/plan.json`，普通日志在同目录。
- 本候选尚未安装。当前设备继续运行参考包 `90831749`、PID28827，保持用户走到的水都复现场景。
- 预热 `Pending::take(out)` 复用输出向量会把旧配方重新带入队列，已用 `out.clear()` 及确定性回归修复。当前生产 flush 使用新向量，未把该问题归因为水都卡顿。

## 固定参考包的水都证据

- `.local/codex-recovery-20261114/baseline-908-water-present-runs/` 保留10组、每组9个连续呈现帧，共90帧。每组有帧头、大小和摘要，采集前后 PID28827 一致。
- 左铺地 ROI `(260,420)-(360,500)` 的80个组内相邻帧对中33个亮度差大于3，最大10.23。右铺地 ROI `(900,490)-(1050,580)` 最大0.86，没有大于3的帧对。同步回读扰动性能，这组数据仅验证画面现象。
- `.local/codex-recovery-20261114/baseline-908-water-probe/capture/` 为同PID的present55574。正确解码RGBA16F后，g140 的 `431a/6e2d` 单绘制、2实例给左铺地区域加入灰白透明层，右侧基本中性。g149 的 `f55c` 合成符合 `scene.rgb * trans.a + trans.rgb`，左侧逐像素MAE为0.00127。
- 单帧探针改变调度，尚缺连续实际输入/输出证明时间闪烁根因。旧PID7533四帧路径与本次不同，最终左地面跨度仅1.16，不能与当前数据混用。
- 已显式撤回“旧聚合draw census相同可排除所有绘制/顺序差异”和“旧uniform哈希相同可排除所有实际输入变化”。下一步使用有界、现有slot fence退休的定向连续采样。

## 16KiB 主机页与4KiB客体子页

- 提交 `3c42368` 支持在一个16KiB主机页上保留多个4KiB客体分配。释放共享边界时只清目标字节，整页匿名内存继续保持懒分配。失败回滚只恢复新开放的主机页。
- 实际 Mac 主机页为16384。实现者及总控分别执行 guest normal 128项、recovery 244项检查，8种注入失败情形全部通过，最终ticket、charged和raw region归零。旧owned-page和address-region契约通过。
- 特制负控把rollback恢复为覆盖活邻居的整段span，新测试以退出1拒绝，证明能够捕获原b6类错误。证据在 `.local/worker-host-page-20261114/checkpoint-final.json`，总控复跑在 `.local/codex-recovery-20261114/host-page-review/manifest.json`。
- 物理MMU权限仍按主机页生效。同主机页的活邻居保持RW时，已释放4KiB子页无法独立设为NONE。Android16KiB及天玑9400实际设备尚未测试，不能据宿主契约宣称该设备游戏问题已修复。

## RG477M 16通道子组归约实证

- 公开上游 `enjoyerofslop/droidplay-core` revision `51a20c51efb9125c0e344ba21aa4d4dafcaffa31` 提供MIT许可的 `dm/android/spv_narrow.h` 及独立GPU归约用例。已保存URL、revision、许可证和源码摘要。
- 固定 RG477M01445122、Mali-G615 MC6、subgroup16，在游戏PID28827保持运行期间，独立用例三次原始求和均为 `0x463c3a`，期待 `0x231e1d`，重复两倍。上游修正后三轮求和、最小值、最大值和OR均通过。原始及重写模块都通过spirv-val。
- 对本次水都present55574实际使用的27个compute模块，5个需要窄子组改写，其中2个匹配归约步骤，包含粒子计数 `b673e10f3da2451c`。5份原始和5份重写均通过验证，没有未匹配步骤。
- 独立设备用例和日志在 `.local/codex-recovery-20261114/upstream-narrow/device-plan.json`；当前场景模块检查在 `.local/codex-recovery-20261114/narrow-active-modules/summary.json`。本问题与水都时间闪烁的关联仍待同包A/B。
- 该上游也记录本游戏Odin2实际开场及三次战斗，包含Bahamut召唤，仍有近景火焰块、岩石条纹和水平线。应复用其逐格式、子组、内存的可重复测试方法，跨设备结果继续按具体芯片与驱动登记。[游戏记录](https://github.com/enjoyerofslop/droidplay-core/blob/51a20c51efb9125c0e344ba21aa4d4dafcaffa31/dm/games/0100606026F12000/README.md)

## a0f402e 最新交付检查点

- 仅compute且实际subgroup小于32时启用窄子组修正，默认开启，`gfx-subgroup-narrow=0` 供同包A/B。warm与runtime共用改写入口，typed变体复用配置一致。未知helper形状保持原模块并记录日志。
- 独立review构造了上游误匹配的合法SPIR-V输入。新增守卫核实际shuffle源、canonical predicate、u32 index，拒绝Function指针别名、CopyMemory及隐式out-pointer写入。9个合法负例保持原字节，上游源码和MIT许可证保留原样。
- 最终守卫对GPU微测试生成的字节与先前三轮RG477M测试完全相同，摘要为 `87dad46bdc8e4db9a9f6bd80f90b6e7c86df5a591821eb99bdc80604148aa501`。
- 全库2934个compute模块检查，1195个改写，309个匹配归约步骤，没有未匹配步骤，原始及重写spirv-val零失败。逐模块输入/输出摘要和验证日志在 `.local/codex-recovery-20261114/narrow-all-compute/`。
- 完整宿主87项及Android构建、签名、ELF、打包均通过。APK为 `.local/codex-recovery-20261114/apk-history/ffrs-1.0.11-preview-a0f402e.apk`，3589186字节，SHA256 `f2375446c3567967f7f45d606b292b6823ededa066b637feb06d93257c42c75e`。构建前后源码一致，普通日志及命令在 `integration-a0f402e/`。
- 设备随后断开，ADB列表为空，新包尚未安装。水都闪烁、角色/技能/移动、跨场景性能与蓝牙音频游戏验收继续等待连接恢复。其它芯片实际设备回归仍为NOT_RUN。13:12再次备份存档，与初始tar摘要完全相同。

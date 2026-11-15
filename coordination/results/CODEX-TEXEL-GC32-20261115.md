# 16槽缓存周期清理缺口与32候选

## 源码问题及修正

recording_slot退休当前槽后，以slot_serial低4位小于2决定texel_evict。slot_index与slot_serial同增、slot_count16，初始serial1/index0，使周期清理只调度slot0和slot15。其余14槽一直漏过该回收入口。slot_count4、8等配置也会出现固定槽饥饿。

32候选以(slot_serial/slot_count+slot_index)低3位为0轮转回收。每槽每8次复用获得一次回收机会，分散相位。回收入口仍在现有retire_slot与跨槽readback保护之后，texel_evict原有pending/recording/ops保护保持。每槽64MiB容量上限与128提交未使用淘汰条件沿用。

新增既有PERF中的全槽entries/largest/live/holes及gc_mask汇总，核对16槽覆盖。原resident=仅slot0/1的旧字段沿用，分析须用新全槽汇总。

## 自测与产物

生产源码提取契约遍历2至16槽各512轮，所有槽均回收64次，disabled关闭分支不回收。恢复旧固定相位的同一契约能检出缺口。原Arena精确保护句柄及失效代数契约一起运行，3项ASan/UBSan通过。全量108项宿主检查及Android构建均退出0，57项源码输入构建前后SHA一致。

APK在.local/texel-gc32-20261115/build/FFRS-1.0.32-preview-texel-gc.apk，3703949字节，SHA256 0bdf02d3bc5074357084387fea7edf68e798e8555b55e9bd12aaf585f1450a70。native SHA256 6db94b8b8f212379948bcc8c0a90d3449489b5741fe75861bac52485ad5cbf6f。

已覆盖安装荣耀A42BBB5109000333，APK回读一致，codex-local-save和ffrs-quicksave两个目录完整tar前后逐字相同。启动PID13973，标题阶段多窗gc_mask=0xffff。实际场景、长时间缓存压力及帧率收益继续验证，不能从标题外推稳30FPS。用户已明确本轮使用荣耀平板调试。

## 31参照及后续

31源码e37fcb3，APK363e59a1b31d9d87ec610acd3d7d37dd78fc3339df982e969002145958627a98已安装并验证保存归档一致。PID12238土之神殿调查点普通1740帧均值29.9999FPS、P9944.99ms、最大75.67ms、11帧超过50ms。30旧PID5199另一静机位窗口1140帧28.13FPS、P9973.20ms，用户操作与运行时长未完全控制，draw量也不同，不能宣称该差值是代码收益。

31现有CPU标记新增可选第三项1，仅接通已有descriptor细分。默认关闭。三帧5012至5014未加载shader、管线缓存165hit/0miss，descriptor独占24.65ms，其中image_lookup8.17ms、remaining9.14ms，覆盖标记OK。继续调查持续CPU构建和重GPU通道。

31另取45帧GPU通道计时，边界22983至23027，5625行、全部query完整，无预算跳过。第一重7附件1384x780通道均值5.92ms，随后两个单draw双附件通道4.79ms与3.74ms。该采样改变部分resident/indirect路径，仅用于通道排序，不能把其FPS当普通性能。完整记录在.local/honor30-perf31-20261115/compare31/。

当前32正在继续读取同存档。后续先做同一调查点普通采样，再记录移动和战斗。进一步检查gfx-shader-reuse目前0造成的warm/runtime重复改写，沿现成模式2逐字对比，再决定模式1的实机使用。外部unit-names及金手指已核包，用户明确先搁置该项。

所有证据在.local/texel-gc32-20261115/，构建/安装命令和日志分别在build/plan.json、install/plan.json。当前未发布32，RG477M未操作。全场景稳30FPS、候选长时间收益和独立QA仍未验收。


## 2026-11-15 备份覆盖补核更正

初次exec-out tar未读到mode600的GameSaveData2至5，错误流还会混入归档。此前本报告的完整归档覆盖表述撤回。33已由应用读取全9文件并逐个SHA核对，源文件元数据保持，完整证据见CODEX-PAD-CHORD33-20261115.md。

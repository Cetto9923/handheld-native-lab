# 成熟 Switch 原生移植对照与本轮针对性优化

## 1.0.21女主闪烁实玩反馈

RG477M已安装21并回读APK一致，PID32442同次运行。用户完成开场复测后明确反馈“不闪烁了”，女主身体闪烁本轮用户实玩确认消失。包内同时含位置精度和clear覆盖修正，单项因果尚未分离。当前包保留为后续性能优化的画面对照。

同次日志、9300帧CSV与截图保存在 `.local/codex-recovery-20261114/visual-pass21/`。截至开场首战时间轴教程弹窗的末30秒约30FPS，P95 34.20ms，P99 39.76ms。有声流尾部18次progress的xruns累计20恒定。该教程窗口的测量范围有限，移动、战斗特效与开场短断音仍待实际覆盖。

## 1.0.21候选产物

源码780ebc67d4b9cfe41c7df35bc0e1a7280e0796a1，98项宿主检查与Android构建通过，28份输入构建前后摘要一致。APK `ffrs-1.0.21-preview-position-precision.apk` 保存在本地APK历史目录，3601404字节，SHA256为07f5f0368183b7fb9228c9f744fd835bf623c05fd0e86a1ecb71e13f7ec1419e。native LOAD段均16KiB对齐，ZIP检查通过。独立18份SPV校验、ASan/UBSan及注解目标集合、函数体/Fma字节、幂等核对通过，review125afed9/e2cd142d。有效fixture覆盖VS/FS/CS，Tess/Geometry有效fixture待补。两项候选修正为完整clear目标覆盖及通用位置浮点约束。

RG477M当前断开，21待安装。女主闪烁因果、实际GPU性能、低帧与短断音回归均待同设备实测。构建与产物验证记录在 `.local/codex-recovery-20261114/precision-build/`。

## 女主身体闪烁最新对照

通用位置精度处理已按固定DroidPlay实现接入本项目的共享优化入口，位于tidy之后，覆盖runtime、warm与integer变体。typed变体沿用已优化模块的装饰。现存真实7ca7原始与castback两份VS各有492处所选运算，现有NoContraction覆盖为0，另外各有100条显式Fma。当前实现补充分步运算的融合约束，显式Fma保持原语义。默认启用，启动标记gfx-position-precise优先于DM_SPV_PRECISE，0可作同包对照。配置在预热线程启动前读取，新进程重新构建模块。独立接线静态审查未见阻塞问题，冻结helper已独立通过ASan/UBSan与18份SPIR-V校验。此VS尚未映射到开场女主，离线缺口与录屏闪烁的因果仍待实机。[固定精度实现](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/android/spv_precise.h)

用户确认1.0.20仍轻微闪烁，随后恢复原1.0.18 APK，用户也确认同一类闪烁。原18包a6c9fbe2的恢复、回读和存档备份证据位于 `.local/codex-recovery-20261114/install-baseline18-after20-20261114-221843/`。撤回19 UBO改动是唯一引入原因的假设。生产UBO继续保持18路径，性能收益与闪烁因果各待实际验证。

用户最新31.091519秒录屏已保全，SHA256 a5977386beee0c52bdcac7bd2f7724335033604a6a2a39ccd2301ae9d2c2bbe8。24.235044、24.268700、24.302256秒三帧的女主裙身局部黑像素占比为3.50%、16.64%、0.009%，邻近背景亮度稳定。女主脸部与裙身黑色栅状缺口已定位，尚缺引擎帧与绘制数据同步。音轨22至27秒未检出预设短静音或强重复候选，听感验收待复测。详细证据在 `ubo-regression-control/user-video/analysis-perf/review.md`，该报告SHA297d742422f3462582a1698d8df806c2607f3370c06477228def646aaa2cff5b。

分层clear进一步复核发现实际缺口。folded clear沿普通draw的附件交集规划，目标view大于其他附件时，p.layers及宽高会缩小，目标剩余slice或像素未清。3D image.layers为1也漏掉子view首次CPU内容保留。8文件修复已冻结，宿主组件及Android语法自测通过，独立复审和全量宿主检查进行中。该缺口与女主闪烁的联系待正常路径命中和同场景A/B。另核对成熟上游位置NoContraction与当前shader的已有覆盖。[DroidPlay精度实现](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/android/spv_precise.h)

15秒Perfetto采样保存在 `baseline18-flicker-current/frequency/`。实际protobuf的failed_ftrace_events列出power/gpu_frequency，CPU频率轨有效，GPU频率无法据本次trace判定。掌机断开前电量4%，已通知用户充电。目前ADB只有荣耀平板，RG477M实机验收待重新连接。[Perfetto错误字段定义](https://github.com/google/perfetto/blob/v59.1/protos/perfetto/trace/ftrace/ftrace_stats.proto)

## 1.0.19 实玩反馈与控制验证

1.0.19 源码为 `3e0c9bc`，98项宿主检查与 Android 构建通过，APK SHA256 为 `d0b8f181a8c87cf9e64670090550e9a036982fa046b264be1665fe98cde5468e`。安装回读一致，存档前后摘要一致。PID30282 实际新建游戏开场约25–26FPS。

用户确认整体流畅性提升，画面整体卡顿减轻，仍有声音短卡、技能特效与人物移动不顺。用户同时报告剧情人物轻微闪烁，并对照1.0.18当前这轮无闪烁。该反馈将19的画面验收改为未通过，静态组件及上游逐条一致不能证明本游戏完整运行语义正确。

根已保全 `performance-3e0c9bc/regression-user/` 的同次日志和帧数据，启动单变量控制。1.0.20 将 uniform 路径精确恢复至7de9e39，保留完整宿主页对齐查询优化。19的完整源码和测试在3e0c9bc及 `.local/codex-recovery-20261114/ubo-regression-control/source19/` 可恢复。当前尚未证明闪烁与UBO的因果关系。此前优先吸收该范围分析的决定按此次用户反馈进入回归隔离。

## 21时34分实施前判断（后续对照已修订）

用户已确认 1.0.18-preview 当前这轮画面无闪烁、运行无闪退，卡顿仍明显。该包作为稳定性对照。当前优化优先处理 UBO 快照复制与页映射查询，采用已取得源码和本项目实际热点的对应关系确定范围。

本项目现有路径由原始 ARM64 模块执行、平台 API 兼容层、NVN 录制与 Vulkan 回放组成。图形后端来源在 `src/graphics/nvn_vulkan/README.md` 记录为 D2R `a8782caa` 与 nxnative `7a029f66` 的适配。此次对照补充了后续上游同游戏实现的演进。

当前性能证据来自 `7de9e39`、RG477M01445122、PID26260。此前城内窗口为 18.7–20.3FPS，GPU 26–28ms，三个读档长帧合计 55.467 秒。21时后的新采集结束于第三章任务菜单，不能与此前城内移动窗口直接组成跨构建 A/B。原始证据分别在 `.local/codex-recovery-20261114/performance-7de9e39/` 和 `.local/upstream-switch-zelda-20261114/baseline-7de9e39/`。

## 固定上游

| 项目 | 固定 revision | 自有源码许可及覆盖 |
|---|---|---|
| BOTWRecompAndroid | `65c3d4783acbb843c1a6a011030cfc06d991b091` | LASRecomp 自有部分 MIT。0.4.0-beta 含 LSFG 的整体 APK 另声明 GPL-3.0-or-later |
| TotK Native | `afdcd6beeb5387194b3f27fc81d37a608dc28605` | 自有部分 MIT，第三方组件按 notices。Android 路线面向 Adreno/Turnip |
| TOTKDroid | `27be1909d2aa0941755bec84fb642f9163ca74f0` | 自有部分 MIT，beta，Mali 支持的实测范围有限 |
| DroidPlay Core | `a72a85c59cac54f3428827cfb72e1adf63b1521a` | 自有部分 MIT，已有同游戏 Android 适配与实玩记录 |
| nxnative | `5a4e23a2e952dae92e1ccf0e6b7bf24e3266d4e8` | 自有部分 MIT，用于来源与演进对照 |

各仓公开元数据、许可证、固定 tree 和文件 SHA256/Git blob SHA1 保存在 `.local/upstream-switch-zelda-20261114/`。完整源码产物留在本地，Git 保存本索引。引用时以具体文件版权和第三方依赖为准。[BOTW 源码](https://github.com/mamcjr/BOTWRecompAndroid/tree/65c3d4783acbb843c1a6a011030cfc06d991b091)、[TotK Native 源码](https://github.com/AurelioB/totk-android/tree/afdcd6beeb5387194b3f27fc81d37a608dc28605)、[TOTKDroid 源码](https://github.com/mamcjr/TOTKDroid/tree/27be1909d2aa0941755bec84fb642f9163ca74f0)、[DroidPlay 源码](https://github.com/enjoyerofslop/droidplay-core/tree/a72a85c59cac54f3428827cfb72e1adf63b1521a)

## 发布与真实帧率范围

- BOTWRecompAndroid 已有多轮发布，作者记录包含特定 Adreno 设备和场景的 60FPS。0.4.0-beta 同时提供可选插帧，overlay 区分游戏与显示帧率。Mali 的公开用户数据仍有明显低帧，设备覆盖需逐项核对。[发布记录](https://github.com/mamcjr/BOTWRecompAndroid/releases/tag/v0.4.0-beta)
- TotK Native 公开九格真机基线。Odin 3 的 Hateno、Lookout 室内与 Shrine 分别为 42.1、50.9、59.9FPS。路线采用直接 ARM64 执行及 Vulkan，设备/驱动前提明确。BOTW 与 TOTK 的 60FPS 时间补丁分别包含各自游戏版本的逻辑时间兼容处理。[TotK Native 基线](https://github.com/AurelioB/totk-android/blob/afdcd6beeb5387194b3f27fc81d37a608dc28605/README.md)
- DroidPlay 的《共鸣》1.0.1 在 Odin 2 实玩开场和前三场战斗，作者记录 chain6 的约30秒窗口中位 52.1FPS、最低 43.1。作者同时列出了图像细节和音频问题，覆盖范围仍是这段流程。[同游戏记录](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/games/0100606026F12000/README.md)

## 本项目与成熟实现的具体差距

| 本轮问题 | 上游机制 | 本项目实际情况与接入选择 |
|---|---|---|
| 每帧大量 memcpy、descriptor/snapshot 开销 | DroidPlay `spv_cb_ext2` 按 SPIR-V ID 操作数识别 UBO 使用范围并校验布局 | 当前 `spirv_uniform_extent` 扫描字面 word，literal 与变量 ID 相等会误保留整块。优先吸收按操作数的保守范围分析 |
| 读档大量 4KiB map | TotK Native 页状态索引、TOTKDroid 邻接区间合并、DroidPlay 位图批处理 | 当前每次 map 重扫历史 allocation/accessible 链。先消除完整宿主页范围的外侧共享扫描，再按实际聚合耗时决定索引改造 |
| 数据重复复制与 GPU 往返 | DroidPlay 按真实地址范围同步、dispatch 输出在 GPU 继续读取、同次写入的 alias 共用副本 | 本项目已有 pending view、GPU resident 和 texel snapshot 路径。接入时按缺失契约逐项对照 |
| 跨帧资源复用 | BOTW 与 DroidPlay 完整 dirty-page tracker，host/syscall/GPU write prepare 配套 | 当前 bridge 的通用 tracker 是 stub。完整接入涉及现有 wtrack/evguard 与 signal handler 的写入归属 |
| 串行翻译 | BOTW 的提交 FIFO、DroidPlay 的高层 NVN scheduler | 当前 recorder 已有不可变 package 和递归 dependency lease。异步缺口在提交返回后提前 release lease，以及 direct_record 的释放时机 |
| Vulkan driver 录制成本 | DroidPlay 额外 Vulkan recorder 深拷参数、顺序录制、ticket/fence/drain | 该层默认关闭。当前样本支持先处理复制热点，再据更完整 driver 成本决定是否接入 |
| 多芯片兼容 | 按实际 Vulkan feature/format/subgroup/driver 选择实现，缓存包含驱动身份 | 保留 Mali/Adreno 的差异配置与明确 fallback，9300/9400/8E5 的真机兼容仍待对应设备证据 |

UBO 来源为固定版本 `gfx_vk_draw.inc` 的 `spv_cb_ext2` 和 `spv_lit.h`。其他机制分别有实际源码和数据前提，具体对照见本地各项报告。[UBO 范围实现](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/android/gfx_vk_draw.inc#L263)、[内存实现](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/host/hle_svc.c#L459)、[Vulkan recorder](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/android/gfx_vk_rec.inc)

## 已核验的复用边界

本项目实际 main Build ID 为 `736837afd390442ed7c74a7de730e71b`。DroidPlay 1.0.1 profile 的 main 为 `84e182f72ee1da4f477804bd05b54c15`。UE allocator、region、60FPS 的地址补丁需要先对当前模块识别并验证。当前 62GiB reservation 已成功，单凭虚拟 reservation 大小无法归因这次55秒加载。host allocator 替换涉及 UE5.6 32槽 ABI、64KiB 对齐、线程栈与跨堆释放配套。[游戏身份](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/games/0100606026F12000/game.tsv)、[allocator 实现](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/host/hle_uemalloc.c)

本项目保持 submission-owned 不可变快照与真实 fence。TotK 实现仍有同类生命周期要求，TOTKDroid 直接读取 UBO 的默认关闭路径记录过 CPU 下一帧重写引发的画面错误。BOTW DescriptorPlan 的当前固定版本也有动态 UBO 覆盖与测试缺口。这些实验路径的成熟度已经逐项记录。

上游分层 clear 修复也已对照。当前 `clear_attachment` 已使用 `p.layers`，有明确层数规划。其他项目32层光照体积的修复不能单独证明当前水都闪烁的根因。用户本轮反馈以实际游玩范围记录。

此前研究报告将异步问题简化为“缺少命令副本”，现已显式撤回。当前源码具有自有 package、inline update 数据和递归 pin，后续应复用这些所有权机制。

## 本轮实施和验收

1. `CODEX-UPSTREAM-UBO-20261114` 由 perf_cpu 实施。保留 shader 指令字节，使用精确操作数、合法布局与保守未知处理，减少源数据复制长度。真实模块离线比较为27份输入、26份唯一模块、126条唯一 Uniform 绑定，其中24条从 full 改为 bounded。静态每绑定一次的声明字节合计由2,189,168降为635,584。生产入口只裁剪 `SOURCE_UNIFORM`，已核对 reflection 的五个适用绑定在同模型中少319,632字节，其余仍需来源匹配。实际每帧收益还受绑定大小、draw次数和缓存命中影响。
2. `CODEX-UPSTREAM-PAGE-20261114` 由 load_profile 实施。完整宿主页对齐范围直接采用确定的边界，保留非对齐共享页、allocation ticket、额度和失败恢复。此处借鉴请求范围内查询策略，优化为本项目局部实现。
3. 根总控串行集成、独立复验与打包。保留1.0.18的APK、日志和存档检查点。先覆盖正常入口、原闪退流程与当前实玩画面，再以相同存档/机位的冷暖窗口确认帧时间和加载收益。

本报告初次落盘时，上述实现正在进行，性能与真机收益为待验。

## 本地详细证据

- `.local/upstream-switch-zelda-20261114/botw-review/review.md` 与 `addon.md`
- `.local/upstream-switch-zelda-20261114/totk-review/review.md` 与 `droidplay-memory-addon.md`
- `.local/upstream-switch-zelda-20261114/cbuf-review/review.md` 与可重跑离线组件
- `.local/upstream-switch-zelda-20261114/droidplay/source-manifest.json`
- `.local/upstream-switch-zelda-20261114/baseline-7de9e39/plan.json` 与 `summary.json`

本轮上游核查补齐了同游戏后续实现的比较。之前对成熟上游的核查不够完整，现按固定源码、实际支持范围与本机数据决定吸收顺序。

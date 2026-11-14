# 成熟 Switch 原生移植对照与本轮针对性优化

## 当前结论

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

# Honor dark/Limit capture source notes

2026-11-07，来源与本机证据分开。未复制上游实现或继承兼容性/PASS。

- [nxnative shaders, revision 8e0916bf04f6fb8542c52ed01b166a8796158ca0](https://github.com/seedds/nxnative/blob/8e0916bf04f6fb8542c52ed01b166a8796158ca0/docs/reference/shaders.md#bindless-reads-whose-handle-phi-also-carries-other-values)：本轮重新实际读取。上游记录混合 Phi 的 bindless 读取被删、结果置零，粒子缩放为零造成网格塌缩；修复在 vendored BindlessElimination 的 TryTableHandle，而非改深度。此前本仓库 OSS-NOTES 已核该 revision MIT。仅作为本次 GPU 追踪后的机制线索；本次尚未读取对应 Maxwell 原始指令或运行重译工具，不能把上游案例代替本 shader 的源语义证明。
- [VkBufferViewCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferViewCreateInfo.html)：R32_UINT explicit range2 不满足 texel block 大小倍数要求（00929）。只限域把捕获的两 view 改为 R16_UINT/range2 做零值隔离；不能据此把 half 非零/负零转换当作已修。
- [Vulkan shader interfaces](https://docs.vulkan.org/spec/latest/chapters/interfaces.html)：数值类型与描述符视图语义须一致，不能用零值相同证明格式解释相同。
- [VkPhysicalDeviceFeatures](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html)、[VkDescriptorSetLayoutBinding](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutBinding.html)：Position/逐scalar探针要求 vertexPipelineStoresAndAtomics 与 binding10 的 vertex stage visibility。实际捕获 feature=true；两精确 layout 仅 stage16→17。使用既有 ALL_COMMANDS/MEMORY_WRITE→MEMORY_READ|WRITE barrier、SSBO独立区及原重放 fence；没有全局 waitIdle 修复。

GFXReconstruct 仍复用已有 c081f1b 派生 replay APK（1d30...），未重建/重新下载工具。既有源码目录原路径缺失，普通备份与实际 APK/输入/日志保留；不自行触碰禁止的 RG/codex-black-screen 目录寻找替代。

## 2026-11-07 用户允许后的单译器准备与实际证据

- nxnative 固定 revision `8e0916bf04f6fb8542c52ed01b166a8796158ca0`，只在 Honor `.local/honor-device-profiles/flame-20261104/dark-shader-retranslate-20261107/upstream` 准备；根 MIT 与 vendored Ryujinx license 已读取。实际编译副本 `translator/src`，`translator/manifest.json`/构建日志保存版本和成功状态。沿用项目已有 `prepare_rg477m_effect_xlate.py` Vulkan texture-offset fallback，没有再扩 guarded-Phi 分支或重建捕获工具。
- 已实际读取 vendored `src/Ryujinx.Graphics.Shader/Translation/Optimizations/BindlessElimination.cs` 的 `TryTableHandle`：PhiLeaves 收集原 c[2] texture/sampler 表词，按候选匹配拆读取，不能转换的读取才被 Copy(0) 删除。恢复组数来自本机译器日志，不等于所有原 Maxwell 路径已经逐条审计。原四VS的 NVN code/C1/job 已从平板现有容器只读复制并提取；`maxwell-texture-load-audit.json` 记录 TLD.B 原字、offset、寄存器、predicate/mask，未硬编码六个 Position 值。
- Microsoft .NET SDK 10.0.403 由官方 [发布元数据](https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json) 解析，私有目录运行；下载 tar 230759804 bytes、SHA512 `5893c66b48ea3674c7da18501059d606e96f3672fabe4d5e41762e692c7c83df3ee0b2b9dc5a0786191da2dc413d7335158101c14c935ab217f31ac2529770f3` 与元数据一致。首次SDK运行未预隔离的开发证书创建副作用已在RESULT如实记录，未设置信任/读取导出凭据；其后全部使用私有DOTNET_CLI_HOME/NUGET_PACKAGES并禁首次证书生成。
- [uilman/ooz](https://github.com/uilman/ooz/tree/9c67c6e2f892c462689592a28908576fe1a6a511)，固定 `9c67c6e2f892c462689592a28908576fe1a6a511`；实际 kraken.cpp GPL3+ 头已读，仅本机CLI解压自己的既有数据，不进APK，不作为游戏运行时。CLI构建命令/日志/退出码及decoder SHA保存在同一私有证据目录。
- 生产候选只换两份 color XGS，pre两份重译被reflect以runtime数组拒绝后未强行部署；旧C1/vertex input逐字保留。暗魔法普通marker0录屏已恢复结构颜色，Limit用户实测碎冰段仍黑，不能继承上游PASS或用重译成功覆盖该FAIL。
- 真实 Adreno735 Vulkan组件16项 half/UINT对照通过，native R16_SFLOAT range2的1/负零/边界/inf语义有GPU输出。暗魔法活动f42543实测两B18原生R16_SFLOAT/vk76/2字节，恢复B54原生R32_SFLOAT/vk100/float；无盲扩range。所有其他binding整数事实保留，Limit活动variant范围仍待独立实测。
- 残留碎冰定向包只向普通0545... APK加入已有GFXR capture layer，所有原非签名ZIP entry逐字同SHA；新增layer SHA `61abd21138b02fae5c1eabef36c9cbd79c2b8737acd0b007f9cf129e4e16e28a`，整包 `d364f4a26669397aed35196fdf573702cac57bbf2560ba9b4c30b379032cb46e`。此为诊断捕获包，不作普通音频/视觉PASS。

## Boss missing-output initialization candidate — 2026-11-07T19:09:44.972153+08:00

Primary owning source: [pinned nxnative Ryujinx Translator.cs](https://github.com/seedds/nxnative/blob/8e0916bf04f6fb8542c52ed01b166a8796158ca0/dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader/Translation/Translator.cs#L182), revision8e0916bf04f6fb8542c52ed01b166a8796158ca0, existing private copy SHA256 79e43f0461fee587bc2a445d6a971e5e3654147142293e0d1e59cc1be3a8f803. MIT Ryujinx license already retained with private translator. EmitOutputsInitialization iterates NextInputAttributesComponents; InitializeOutputComponent stores0 forxyz/1forw. This is linked-stage initialization policy, not evidence that this game's flash is caused by location9. Existing single-stage generated boss VS omitted output9; runtime's declaration-only workaround left its three FS reads undefined. New exact-module initialization candidate preserves original instructions; host module roundtrip/spirv-val pass, GPU/visual pending. No generic emulator runtime or copied game assets are added to source.

Other research: [Yuzu SPIR-V attribute emitter](https://github.com/yuzu-emu-mirror/yuzu-mainline/blob/master/src/shader_recompiler/backend/spirv/emit_spirv_context_get_set.cpp) was accessible through raw URL; inspected as context, unpinned mirror master and not used as normative guest proof or implementation source. An old history mirror timed out; search snippet is not evidence.

Honor400Pro packaging reference: [manufacturer specs](https://www.honor.com/cn/phones/honor-400-pro/spec/), consulted for user-named target. No device compatibility or60Hz acceptance inferred from specifications or Honor tablet results.

## 2026-11-07 可移植交付限域

- 官方 Honor 400 Pro [区域规格页](https://www.honor.com/sd/phones/honor-400-pro/spec/)本会话已读：Snapdragon 8 Gen 3、Adreno 750。据此增加SM8650配置选择及精确Adreno750 helper-inline门，并复用已有精确模块形状守卫的histogram/interface修复；不外推735的深度复用特殊处理；本机未连接400Pro，HOST选择测试不是实机验收。
- JNI保留现有游戏数据自动发现/存档根，只在未给显式gfx-shader-archive marker时选择NSO旁已有非空正规shaders.hnla。数据源不变，显式0仍禁用；loose app shaders、loose game shaders均先于归档。已有XbgShaderArchive::open严格检查magic/sorted keys/无重叠连续extent/文件末尾，缺项不伪造。
- 60目标暖野外实测32.980182 distinct work fps，未达标；无重复呈现、跳过、失败。CPU/GPU串行等待只提供性能线索，不能据此删同步。后续已有defer-events=2 opt-in只作受控对照，不自动成为修复或交付默认。

## 2026-11-08 raw附件布局与实际导出器核对

- [固定c081f1b的VulkanResourcesUtil](https://raw.githubusercontent.com/LunarG/gfxreconstruct/c081f1b/framework/graphics/vulkan_resources_util.cpp)：CopyImageBuffer采用rowLength/imageHeight=0；子资源步距来自临时image分配尺寸。[VkBufferImageCopy](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html)说明0表示按extent紧密排列。此次也核对了现用1d30 APK实际ARM64零值写入，不能仅因源码相似继承结论。逻辑前缀与分配尾分别核算，后者不作像素。
- [固定c081f1b DumpImageToFile](https://raw.githubusercontent.com/LunarG/gfxreconstruct/c081f1b/framework/decode/vulkan_replay_dump_resources_common.cpp)原版跳过stencil；现用1d30二进制已加raw条件，且两组8个stencil文件实际存在。以上游原版代替修改后工具作出的“stencil缺失”推断已否定。实际binary-layout-contract/88文件核验位于postbaseline-20261108/raw-layout-audit。只做离线解出现有APK，没有重建/下载工具。
- [固定c081f1b资源导出说明](https://raw.githubusercontent.com/LunarG/gfxreconstruct/c081f1b/vulkan_dump_resources.md)的功能范围是重放；本次两组附件仍均属重放，原运行独立附件/世代链缺口不能据此改为CAPTURE_COMPLETE。

## 2026-11-08 ADPF受控性能候选（默认关，未作交付）

- 已读Android官方[性能提示说明](https://source.android.com/docs/core/perf/performance-hint-api)与[NDK API](https://developer.android.com/ndk/reference/group/a-performance-hint)：为周期性工作线程组创建session，报告真实完成时长并更新目标；由系统决定调度/热策略。只作当前35fps稳态调度假设的候选，不能把API可创建称作性能PASS。未采用固定核心/忙循环或改变游戏时间。
- 自有xbg_performance_hint.h按实际submit/present的Linux TID建组、在主机pace前取monotonic真实区间；缺API/nullsession/调用错误有界退回普通路径，显式gfx-performance-hint且仅Honor735可启用、默认关。公共C签名通过本机NDK26.1以API33静态类型核对；实际Android28应用通过dlsym可选加载，ELF没有必需的APerformanceHint导入。API28签名检查首次因新函数标记不可用失败属预期编译可用性，原错误保留后改API33只做声明核验。
- 候选APK65f3d601...正常构建exit0/install-r保留数据，GPUshader/FS/深度/同步/音频/存档未改；ABI与host生命周期用例不替代实机稳态FPS、视觉或人耳验收。数据和旧普通2a4恢复包完整保留。

ADPF最终状态（2026-11-08T01:48:43.369145+08:00）：同候选ABA51.9177/52.0336/52.1293无可辨识收益，且该取时缺present后的GPU等待，仅为本入口负对照。原2a4恢复后38.0693，跨包CPU/core/power/window差异未定因，不能归因API或源码布局。自有候选header/test/glue已移coordination/experiments/honor-adpf-20261108，生产源恢复27摘要匹配，未作默认/交付修复。

## 2026-11-08 native full raw capture

Own default-off D/E implementation reuses original c081f1b/61abd GFXR layer and existing1d30 replay APK; no upstream binary rebuild/download. Raw image rows use zero rowLength/imageHeight and distinct D/S aspects; BC4 Vk139 is4×4/8B blocks. Buffer diagnostic alignment padding is permitted only within proven VkBuffer object extent, never inferred from VkMemory requirements; final shader view range is unchanged. Primary references verified: [VkBufferImageCopy](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html), [copy buffer ranges](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html), [format definitions](https://docs.vulkan.org/refpages/latest/refpages/source/VkFormat.html). Unknown formats/samples/ranges explicitly fail capture completeness. Native frame8691 raw subresource/binding/file audit passed; actual API identity and replay agreement remain separate pending gates.

## 2026-11-08 same-draw replay closure and freeform profile

With existing c081f1b/1d30, requesting both targets truncates the earlier target CB after its requested draw. Changing only the request to color-only removes all 14 Dark attachment differences: 112/112 exact, plus the separately verified pre 78/78. Limit pre/color-only are 22/22 and 115/115 exact, representing 126 unique native GPU readbacks plus aliases. Original traces remain untouched; VK_NOT_READY warnings are not described as fixed. Texel GPU whole-buffer files are sliced separately by each actual binding's offset/range. Reproduction index: coordination/experiments/honor-capture-20261108.

On the Honor SM8635 tablet, a freeform window reports Activity smallestScreenWidthDp=580. Old code selects handheld_mid and disables required GPU shader compatibility; both candidate and old baseline fault at the same driver PC. Disabling warm builds or selecting a fresh cache filename does not remove the fault; those experiments and their new markers were removed. The production candidate derives hardware form factor from physical display dimensions and stable density, preserving explicit overrides. Primary API definitions: [physical display mode](https://developer.android.com/reference/android/view/Display.Mode#getPhysicalWidth()), [stable device density](https://developer.android.com/reference/android/util/DisplayMetrics#DENSITY_DEVICE_STABLE). This selects existing presets without changing game time. On device, the same 580dp window selects hardware 768dp/tablet_sd8s/inline1 with original cache and warm builds enabled and reaches the title; scene acceptance is tracked separately.

## 2026-11-08 stream feasibility, not capture acceptance

Pinned [FileOutputStream](https://raw.githubusercontent.com/LunarG/gfxreconstruct/c081f1b/framework/util/file_output_stream.cpp) uses stdio writes. [Capture manager](https://raw.githubusercontent.com/LunarG/gfxreconstruct/c081f1b/framework/encode/capture_manager.cpp) adds `_trim_trigger`; timestamp can be disabled. GetOffset is used by the separate asset-file path; a normal capture contains no seek in this manager. [Pinned Android options](https://raw.githubusercontent.com/LunarG/gfxreconstruct/c081f1b/USAGE_android.md) document ZSTD and trigger frame limits. These support investigating FIFO output, not claiming it works in the installed binary. Existing61abd layer strings include ZSTD; no tool rebuilt/downloaded. A1MiB zero-data FIFO through run-as in a new own-package directory passed exact SHA/read+write exit0. Actual layer FIFO execution and compressed replay remain NOT_RUN. Old trace frame-boundary offsets were scanned without payload changes: most of the total size is per-frame unassisted writes, not the state snapshot. Do not switch to assisted tracking to shrink data without proving mapped-write coverage.


## 2026-11-08T07:09:02.230521+08:00 actual stream failure and bounded memory-tracking alternative

Actual fixed-runtime245d with unchanged61abd layer, unassisted/ZSTD/two-frame FIFO, PID6699: guest explicitly aborts with `GameThread timed out waiting for RenderThread after 120.00 secs` (RenderingThread.cpp:1163), exit134. Host reader0 merely reflects FIFO writer death. File2080603698B SHA1c5d4b06f48e9cc8ecea4d161e7ac379964fe2b486b0aade4e105ce3ffb96e8c has822242 complete blocks then236423 trailing bytes of a declared1386132B compressed metadata block; state restoration6248 complete, no frame end. Native6248 pair logs/copy scheduling exist but no written copies/END: strict auditorFAIL. This is capture overhead, not a new visual GPU root cause or quota failure. All originals remain in half-fact-fix/fixed-stream-20261108.

[Pinned Android memory-capture contract](https://raw.githubusercontent.com/LunarG/gfxreconstruct/c081f1b/USAGE_android.md) explains unassisted dumps all mapped contents at each queue submit; page_guard tracks dirty pages using shadow memory and read/write faults, including refreshing device-produced data on reads. Same245d restarted PID8407 with only memory_tracking_mode=page_guard, fresh owned FIFO and filenames. Existing process signal ordering/source inspected; no new handler/runtime/tool build, no game watchdog/sync change. Actual engine target input/output equality remains required: documentation and successful startup alone cannot validate page_guard. Assisted mode not used, since engine coherent mapped writes do not guarantee vkFlushMappedMemoryRanges.

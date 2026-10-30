# RG477M F62 绘制被拒绝的位置仍待确认

增加默认关闭的单帧诊断：记录 raster discard、transform feedback、正反面、depth range/bounds、scissor、swizzle 与条件跳过状态；仅观察 F62 已经在运行的真实遮挡查询，绘制后保存原句柄，在下次 probe_flush 的真实 GPU fence 后、创建图像读回命令前取 64-bit 结果及 availability。不创建、重置新 query，不伪造可见计数；batch 查询不可直接当作逐 draw 独占计数。正常帧不记录、不读回 query。

参考 RenderDoc v1.38 / MIT 的 [pixel history 文档](https://github.com/baldurk/renderdoc/blob/34c3c40787f440da9c2947cd63a41e6c4d1f95b9/docs/how/how_inspect_pixel.rst)，以及 Khronos Vulkan-Docs v1.3.296 [`7353f6c` query 规范](https://github.com/KhronosGroup/Vulkan-Docs/blob/7353f6ced118815d61852a739b9eae6f0055d77a/chapters/queries.adoc)。仅借鉴测试定位方法，未复制外部代码。细节见 OSS-NOTES。

实际 RG477M01445122，org.ffrs.perfisolated SHA256 `d227a982cae71ce115bbc64b35e9e19efd9d4296fd14aa96285642861185304f`，原568 wrapper/classes.dex，native 观察功能；主包 a1c82634、资源与存档未动。构建56717 exit0，安装SHA核对；trial72652 exit0，PID8767、frame4670、150groups、42,535,592bytes、END完整。F62 group110 两次绘制均1188 indices / 2 instances，实际已有 precise query 为 VK_SUCCESS/available1/samples0。状态为 discard0/tf0、cullBACK、frontCCW、全1024×576 scissor、depth GEQUAL、depth range0/1、boundsOFF、swizzle0；不能因零样本直接认定是哪种原因。

进一步做了仅该抓帧/F62 的反事实：临时禁用 cull 和 depth test，禁用 fast pipeline memo 查找以避免原状态缓存污染实验；shader/stencil 未修改。实验APK `827a531a63bf70cfd67fa9df338336f7f40e2e4352d127dffdf0d86d50264121`，trial86837 exit0，PID12335/frame4364，实际 mode3/cullNONE/depthOFF 确认，两次真实 query 仍 available1/samples0。因此没有依据把关闭深度/剔除当修复。实验自有源码修改已精确反向撤回，patch 仅留 coordination/experiments/；已装回 d227 并核对源码SHA与构建manifest一致，marker清回0。

此前临时 opaque-red shader trial7326 已清理启用文件，并以正常13298进程实际加载F62、无override日志复验。red group113与前组112同目标的中心341×288 ROI和128×72全视口网格均字节相同，stencil ROI也相同；不能扩张成完整分辨率证明或护罩身份已确认。VS vp_c6 真实binding66，非84；顶点颜色alpha渐变完整，不沿用恒取0的误判。上述零样本将下一步指向实际几何覆盖/变换与shader身份核查，尚未证明F62就是两个法师的身体护罩。

本地证据 .local/rg477m-opt-30hz-game-mode/audio-paced-transport-20261030/{fx-raster-state,fx-raster-isolate,shield-f62-raster17,shield-f62-isolate3,fx-f62-coverage}/。原始SPIRV、uniform、视频、APK与原始数据仅.local。GPU抓帧有明显开销，不能当帧率/音频验收。常设音频完整清单在总 RESULT 中保留；真实A2DP两段各≥2分钟耳听、设备断连重开、物理AV同步、战斗稳定30与身体护罩仍未全过。无可玩声明，未push。

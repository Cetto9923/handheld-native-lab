# 索尔转场 R32 线性采样兼容修复

## 输入与范围

任务 CODEX-THOR-LINEAR62-20261117，工作树 /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro，开发分支 task/T-codex-recovery-20261114。用户明确先暂停小米15 Pro长停顿排查，优先交付闪退修复 APK。Magic Vs3 临时无声按用户反馈暂缓。

索尔诊断 ZIP 的 SHA256 为 fb01e9e9c390b5ac4abec36110153def4dae3732ef1f9cc3e98ae9798c78393b。报告对应 AYN Thor、Android 13、QCS8550、APK 59、旧 736837 主模块，末段应用主动 exit70，错误为 R32_SFLOAT 不支持线性过滤。完整输入、视频和原日志见 .local/thor-filter61-20261116/。该证据范围没有系统低内存终止结论。

## 撤回 61 对完整问题的覆盖推断

RG477M 的 61 版本 PID9963 日志在同一转场记录 ceb27d63df2eaf0a、同一 R32 serial14907210，绑定52为真正 Sample，绑定112为 Gather。绑定52不属于61的过滤无关修复。RG477M Mali-G615 的 R32 格式具备线性过滤能力，用户也说明该机原来就没有此闪退。其通过转场仅作既有路径回归，索尔缺失能力的路径仍需62补齐。

61只覆盖 Fetch/Gather 等读取的结论继续保留。撤回任何将61描述为已覆盖索尔全部转场故障的推断。原 Thor 报告缺少具体失败绑定，绑定52属于相同场景的支持证据，目标机实际命中及修复确认仍须复测。

## 实现

新增通用 SPIR-V 改写，仅接受 float32、2D、非深度、非数组、单采样、显式常量 LOD0 且没有额外 Image Operands 的普通采样。同一绑定仍有其他依赖过滤的用途时事务性拒绝。真实 ceb27 模块可处理绑定52、106、118，原模块及三组输出均通过 Vulkan1.1 spirv-val。

运行时根据真实绑定图像的 R32_SFLOAT 视图、设备缺失线性过滤能力、单采样以及实际 sampler 参数选择变体。min/mag均为线性、比较关闭、各向异性为1且LOD夹取范围包含0才适用。用四次分量Gather及float32权重恢复双线性插值。图像仍为原R32，保留原UV、view base mip、寻址、边界色、原游戏速度和位置精度约束。已具备硬件过滤能力的设备继续使用原模块。

变体在integer/typed/vertex处理后选择，RAII恢复上层shader状态。lf1持久化recipe严格验证Vk100及升序绑定，完整identity贯通runtime、warm加载、module注册和发行学习表检查。仅需要的源码以现有压缩模块表示保留，已有module_words直接复用。

## 已执行的回归

- ASan/UBSan生产函数提取测试，以真实ceb27绑定52检验选择、缓存复用、拒绝不适用的sampler/资源、压缩源码恢复及RAII恢复。
- 自有SPIR-V样例验证既有与缺失GLSL导入两种模块。实际52、106、118分别改写并spirv-val。持久化组合recipe及错误顺序、格式、重复绑定事务性拒绝通过。
- 发行inspector的15项测试通过，覆盖lf1与ib1/stb1/vin1组合。
- RG477M上独立原生Vulkan组件，500组采样、1000次dispatch，对照原硬件线性采样及生产改写模块。覆盖两种view base mip、repeat/clampEdge/clampBorder、部分通道swizzle及边界UV。四分量最大差0，测试坐标采用可精确表达的分数。每次dispatch先填NaN，两个GPU结果还分别对照独立CPU双线性计算，阈值1e-5。该组件直接测试改写，与游戏是否在Mali自动选择变体分别记录。

首次加强CPU参考时对R32边界色缺失alpha的假定过窄，组件在硬件原路径报错。Vulkan允许缺失边界分量沿正常RGBA转换，参考已接受允许的alpha0/1两类结果，两个GPU路径仍须彼此一致。失败日志、随后一次格式警告构建失败及最终通过均保留。依据为Vulkan采样规范的Texel Replacement与Format Conversion章节。[Vulkan规范](https://docs.vulkan.org/spec/latest/chapters/textures.html)

GPU编译和执行的实际命令、源码与二进制摘要、进程号、退出码见 .local/thor-linear62-20261117/gpu-plan-hardened.json，最终原日志为gpu-final2-3.log。原始模块与派生SPV只在.local中，Git中测试汇编为自有样例。

## 构建与待验项目

最终构建记录后续追加。1.0.62仍为修复预览，Thor目标机转场、完整游戏场景及独立QA为NOT_RUN。RG正常转场与GPU组件均不等同于Thor实机验收。小米15 Pro长停顿按用户要求暂缓，稳定30帧目标继续开放。

# AYN Thor开场转场采样退出

用户提供9.728秒录屏和诊断ZIP，录屏显示传送门剧情后黑屏、加载图标、退桌面。原视频只读，分析帧暂存在系统临时目录。诊断ZIP本地归档于.local/thor-filter61-20261116/，320490B，SHA256 fb01e9e9c390b5ac4abec36110153def4dae3732ef1f9cc3e98ae9798c78393b，完整CRC读取通过。

## 日志事实与覆盖

metadata记录AYN Thor、Android13/API33、QCS8550、总内存15996096512B、APK1.0.59-preview/59。报告生成时available约10.37GB、low_memory=false，属于导出时快照。用户称安装当前最新版，报告明确版本59；60源码也有同一sampler检查，因此直接制作61候选。guest头部主模块BuildID736837afd390442ed7c74a7de730e71b，用户说明旧无DLC数据。AOC枚举未在采样段中保留，有无DLC不由缺失的日志推定。

engine.log.fatal完整保留133B，错误为NVN sampler linear filtering unsupported，min=3 mag=1 compare=0/2 format=0xa vk=100 optimalFeatures=0x8001ed87 texture=2683317。NVN0xa映射R32_SFLOAT即Vulkan100，功能位缺0x1000。guest与engine尾部相同错误。系统最近主进程PID32182退出reason1/status70，退出时间与三份日志最后修改时间相差小于1秒。旧PID20027也有status70。报告内engine为头尾采样，头部PID19512/20027属于先前启动，最新尾部启动头已截去。不能把头部PID当成本次进程。头部设备能力为Adreno740 driver0x802a4035，同设备历史能力记录。上述证据定位到应用的采样能力检查退出，当前没有对应系统低内存终止记录。

## 最小修复候选

Vulkan官方采样章节中Image Operation Steps和Texel Gathering明确，Fetch逐点读取，Gather返回基级相邻四texel，两者不执行sampler的texel插值。原sampler_slow只按min/mag设置拒绝不具备线性过滤的格式，忽略绑定的实际指令用途。

新增xbg_spirv_filter_independent，按最终生产SPIR-V逐绑定追踪UniformConstant变量、Load/AccessChain/CopyObject/SampledImage/Image别名。只有已证明全体用途均为Fetch、2D Gather或不受过滤影响的查询才归类。普通Sample、QueryLod、调用、Phi/Select、扩展或其它未证明用途保持原约束。数组索引沿资源根处理，未知形状保守拒绝。Cube Gather保持原路径。

普通加载、integer、typed-storage、vertex-input变体分别从最终模块取事实。仅在绑定已证明不使用过滤、且实际视图格式缺线性过滤能力时，将sampler的未使用filter/aniso字段规范化。缓存按规范化后的实际参数取键，普通采样不能复用该键绕过能力检查。原像素格式、着色器字节、寻址/边界/比较参数保持原值。新增有界R32绑定说明，并在仍需过滤且能力缺失时保留失败前shader/binding上下文。

从平板原始shaders.hnla只读获取末尾最后加载的ceb27d63df2eaf0a模块，原模块通过spirv-val。其中76/94/100仅Fetch，88/112仅Gather，106同时Sample与Gather，分类契约逐一核对。原日志未保留2683317到shader/binding的映射，此模块并不能单独证明它就是失败draw，当前修复对Thor本次退出的因果仍待候选包复测。

三项新增宿主测试使用ASan/UBSan，覆盖普通Sample、Fetch/Gather、混合使用、别名、调用/合流/未知指令、截断模块及sampler缓存交叉使用。抽取实际sampler入口的负对照禁用规范化后返回9重现不支持过滤的拒绝，修复路径通过。首轮测试的模拟slow函数观测值在cache hit时保持旧值，已将逐min值检查前清空模拟cache，真实缓存隔离检查另行保留。

代码复核进一步发现前置img_cache跨shader复用ImageView和Sampler，该键也须带绑定的filter-independent事实，否则可能在sampler入口前复用另一用途的结果。已补入kind的独立位，抽取生产键构造并验证同一纹理/原sampler两类用途必须成为不同条目，移除此位负对照返回10。首轮构建启动后补入此修正，首轮产物不交付，最终构建需重建并重新核验全部源码SHA。

参考固定于本轮读取的Vulkan规范页面，https://docs.vulkan.org/spec/latest/chapters/textures.html 的Image Operation Steps及Texel Gathering，和https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDraw.html 的04553/04770。规范页面为动态在线文档，未复制上游实现代码，无新增第三方代码许可依赖。

## 工作树恢复与验收边界

开始时本树.git仍指向旧移动目录，原common/worktrees/ff-resonance-honor-gtpro元数据也已缺失。共享objects和task/T-codex-recovery-20261114的23138b3提交仍完整。保留原.git链接副本后，为本树重建独立worktree管理目录及23138b3索引，git status初始干净。未切换分支或覆盖工作文件，其它worktree元数据保留。

当前平板前台为空之轨迹应用，本轮未向其发送按键或退出请求。Thor未接入，独立QA、Thor实机同转场回归与新包平板场景回归均为NOT_RUN。长期性能目标保持paused。本轮交付定位为61修复候选，后续由用户将同一APK发给Thor用户复测并补诊断报告。

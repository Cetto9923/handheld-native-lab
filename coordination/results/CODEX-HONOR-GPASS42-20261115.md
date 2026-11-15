# 神殿逐通道计时与闲置深度附件候选

## 输入与实测

源25b0bd3，记录HEAD36cc83e，荣耀A42BBB5109000333，41 APK fe5b9323，PID14303。采集前新鲜截图为神殿内部南侧桥段，采集后已进入随机战斗，故单列GPU诊断，不计探索移动FPS。实时过滤输入日志保留，结束UP已释放。

原子发布gfx-pass-live=6，armed日志从69452，实际GPASS边界为69453至69458共六帧。各帧draw约669至680、dispatch80至82。逐pass与compute计时合计平均31.7327ms/采样帧，诊断路线可能改变合批和精确计时，普通FPS仍引用41单独移动窗。

两个主要通道均1384×780。VSb009dd491d2ec334/FS21ee00f1479f5931为单次全屏三角形，平均4.970ms，att2、dz1，实际GPASS_DRAW显示depth=0/0/7。VS5ba911886ad3bb39/FS5b15464cecde76d5对应通道平均4.788ms。CS073ce354a4f65e2f为groups11,7,1，平均4.242ms。另有1728×972后处理与1920×1080输出通道。完整原始日志、六帧边界、排序脚本和JSON位于.local/honor-gpass42-20261115/。

## 对照与候选

固定DroidPlay a72a85c59cac54f3428827cfb72e1adf63b1521a的buf_fwd直接GPU读取与本项目pending_view逐项比对。本项目gpu_clear_pending_buffer仍可CPU直接改写pending源映射，简单去除只读texel不可变副本可能影响已记录GPU读取，当前未改该路径。已有高层async缺口涉及lease返回释放、direct_record和guest backing时序，未直接开启。

针对本次实际dz1通道，实现普通单色draw的闲置深度附件省略。仅在depth、stencil、bounds均禁用且存在深度及单色目标时使用，clear、folded-clear、soft、深度单独绘制和capture/provenance保持原入口。原全部附件extent/layers/samples验证先执行，再省略深度绑定，绘制范围保持原规划。Pass记录实际depth_attached与depth_omitted，last_pass命中加入后者，防止同RT指针下启用深度或模板后复用色彩单独通道。OpenPass使用实际附件信息。原着色器指令保持现状。

缺省启用候选，现成gfx-live-ab=depthomit恢复旧绑定，none恢复候选。GPU诊断应核对同进程旧路径att2/dz1与候选att1，并记录冷管线创建和正常帧率、显示/声音回归。候选性能与实机尚待测，不能宣称稳30。

Vulkan规定depthTestEnable为false时深度写入关闭。依据为本轮查阅的官方参考及片段操作规范。[深度状态参考](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineDepthStencilStateCreateInfo.html) [片段操作规范](https://docs.vulkan.org/spec/latest/chapters/fragops.html)

## 组件覆盖与未完成

生产谓词和last_pass缓存资格表达式提取编译的ASan/UBSan覆盖测试状态切换、清除、单色/多色/深度单独、诊断、已有AB强制原路，以及同RT启用深度/模板/bounds必须miss缓存。源码位置核对extent先规划再省略及两个OpenPass入口的实际附件字段。该项不是完整Vulkan渲染或独立QA。宿主全集、Android、装包、GPU对照、移动和音频验收仍待执行，验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

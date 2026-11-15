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

## 构建后边界复查

初次42源8e66fb5已通过114宿主及Android，APK b8298b9e，80输入一致。尚未安装。复查补充depth bias限制，偏置计算可能依赖深度格式，即使深度测试关闭，FragmentCoord深度也可能被shader读取，因此开启polygon offset时保留原深度附件。单项反例同步加入，再构建后安装。初次APK保留历史。

## 受保护候选构建与安装

源a47c250，114项宿主及Android返回0，80输入构建前后摘要一致。build42-bias APK 3720407B，SHA256 0a502482ffd5ceeff8633d186b3ee6189ec4acfe88ed0360ce6926f94a0a9a34。装包前后9个私有存档文件逐项SHA相同，正式codex-local-save目录文件metadata相同，回读设备APK摘要匹配。复用唯一461私有目录，PID25813，通过标题Continue读至神殿入口，随后往内部导航。初次未带偏置保护b8298包保留未安装。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md，当前GPU与移动及主观蓝牙门未验。

## 同进程GPU与真实移动复核

42源a47c250/APK0a502482，25813复用461。114宿主/Android0/80源SHA/安装9文件及APK回读通过。GPU场景复核后B1/A2/B2/A3/B3纯神殿内部同机位，94与96pass/帧，计时波动不能全归收益。普通持续移动B11520..11640/120间隔23.3551FPS/P9958.02/max69.62，对照A12720..12780/60间隔23.5045FPS/P9955.49/max55.49，均前后探索非零轴及最终归零，未稳30且未见候选FPS收益。设备force=depthomit恢复原深度路径。moving-first与cpu42-moving遇敌混合单列诊断。用户再次确认松手，root独占输入。已正常逃离私有副本随机战斗，现启动6秒Perfetto+持续移动profile42，计划/符号/日志落.local/honor-gpass42-20261115/profile-moving42/，工具session48640。

old42-A1在手柄交叠前战斗菜单，27.36ms仅保留诊断，不能与后续探索比较。后续B1/A2/B2/A3/B3已回到同一内部桥段，前后截图留存。逐pass总GPU依次10.877/11.347/10.689/17.866/12.172ms，候选2次/帧合并得到94pass，原路径96pass，draw数量仍随粒子少量变化。A3未修改的7附件主通道也同比变慢，不能把全部时差归省略深度。普通帧率两窗无改善证据，因此当前设备恢复原路径，候选未验收。显示截图未见新异常，无法替代运动闪烁和全场景/声音回归。

## 撤回候选

深度省略未取得普通移动收益，43候选源码已恢复36cc83e的原附件实现，专属测试及宿主门同步撤回，实验提交/42两个APK及原始证据完整保留。42设备在撤回前已用depthomit原子切回原路。下一项沿原附件实现定位整数格式注册查询。

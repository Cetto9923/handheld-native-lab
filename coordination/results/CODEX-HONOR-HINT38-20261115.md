# 荣耀目标30帧预算提示候选

## 源码与恢复点

候选从4f16fa7构建，37 APK5966ae67为恢复点。CPU热点证据沿用当前同设备入口采样和34/36 descriptor记录。37 PID21784普通入口660帧25.6301FPS，P99 95.02ms，超50ms共41，最大103.49ms。该窗口和36旧窗口分别保留，跨会话差异未隔离，不据此声称37退化。

## 最小实现

使用Android官方Performance Hint API，将真实呈现线程注册为周期工作负载，目标沿现有帧预算，当前33333333ns。每帧以实际单调时钟周期减去本帧已有pacer等待后报告，首帧仅建立基线。关闭、失焦、无有效目标、线程切换时关闭或重建session。更新目标、创建及报告失败均退避到原调度，不逐帧重试。API33以下不加载符号，API33以上动态解析libandroid既有导出，APK仍支持API28。

仅用户开启FFRS_RENDER_POLICY=1时启用。复用现有gfx-live-set中的androidhint布尔项作同进程对照。游戏、shader、复制、barrier及原速时钟语义继续沿37。

生产helper与API33符号装载路径的2项ASan/UBSan宿主契约通过，覆盖自身限帧等待扣除、首帧/恢复基线、目标/线程变化、空能力/空manager/创建/报告/更新失败退避、时钟回退/溢出、关闭幂等与旧API不查符号。现有3项render policy组件也通过。

Android构建、设备实际session、同进程AB和声音显示回归尚待执行。当前仅候选，无收益结论。独立QA未运行。[Android官方接口](https://developer.android.com/ndk/reference/group/a-performance-hint)

## 神殿内部实测与撤回

用户指出应前进到神殿内部低帧区。入口对照在A-off后终止，未完成的B窗口保留为中止样本。root沿北向6秒及5秒进入水晶断桥区域，截图和输入日志在hint38/navigation，后续完整对照位于hint38/deep。

38 APK d261cce32c06329be3c8ae9f47f63e87ff3b1c11fef9a4a9c78c7f14db29c39c，3720407字节。112宿主及Android均0，78输入SHA前后一致。native 181e4ec8037b625271f7d574ff1694324ed993a693d7f0729016a1dcb1c5ecb6，动态符号表无API33强依赖。安装前后私有9文件SHA和正式目录元数据相同，设备APK回读一致。

同PID24174、同水晶断桥机位，四次25秒窗口分别为关闭17.1334FPS/P99114.93ms/149帧超50ms，开启14.8999FPS/P99117.37ms/249帧超50ms，关闭19.1949FPS/P99108.28ms/117帧超50ms，开启17.9080FPS/P99114.75ms/163帧超50ms。零present错误，开启两轮均未取得收益。系统服务实际记录本应用UID10322、PID24174、TID24292、目标33333333ns，native亦有持续报告，能力接线确实生效。

本轮把ADPF候选从生产源撤回，相关实现与测试可在b3ea111及38 APK历史恢复。设备已实时关闭androidhint。当前仍为38测试进程，下一次覆盖包将移除该实验路径。原先下一步ADPF可带来稳定收益的预期已撤回。

关闭后另采6帧CPU细分，实际帧7412至7417、无新shader，6402 draw/402 dispatch/54 submissions。descriptor exclusive119316861ns，约19.89ms/帧，其中image_lookup约6.06ms/帧、snapshot约3.58ms/帧。descriptor coverage=OK。该诊断窗口独立保留，未混入前述普通FPS。上次入口CPU样本仍按入口范围保存。下一步优先处理同次数据支持的纹理注册表查询开销。

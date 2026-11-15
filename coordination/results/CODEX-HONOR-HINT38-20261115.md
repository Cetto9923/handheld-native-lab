# 荣耀目标30帧预算提示候选

## 源码与恢复点

候选从4f16fa7构建，37 APK5966ae67为恢复点。CPU热点证据沿用当前同设备入口采样和34/36 descriptor记录。37 PID21784普通入口660帧25.6301FPS，P99 95.02ms，超50ms共41，最大103.49ms。该窗口和36旧窗口分别保留，跨会话差异未隔离，不据此声称37退化。

## 最小实现

使用Android官方Performance Hint API，将真实呈现线程注册为周期工作负载，目标沿现有帧预算，当前33333333ns。每帧以实际单调时钟周期减去本帧已有pacer等待后报告，首帧仅建立基线。关闭、失焦、无有效目标、线程切换时关闭或重建session。更新目标、创建及报告失败均退避到原调度，不逐帧重试。API33以下不加载符号，API33以上动态解析libandroid既有导出，APK仍支持API28。

仅用户开启FFRS_RENDER_POLICY=1时启用。复用现有gfx-live-set中的androidhint布尔项作同进程对照。游戏、shader、复制、barrier及原速时钟语义继续沿37。

生产helper与API33符号装载路径的2项ASan/UBSan宿主契约通过，覆盖自身限帧等待扣除、首帧/恢复基线、目标/线程变化、空能力/空manager/创建/报告/更新失败退避、时钟回退/溢出、关闭幂等与旧API不查符号。现有3项render policy组件也通过。

Android构建、设备实际session、同进程AB和声音显示回归尚待执行。当前仅候选，无收益结论。独立QA未运行。[Android官方接口](https://developer.android.com/ndk/reference/group/a-performance-hint)

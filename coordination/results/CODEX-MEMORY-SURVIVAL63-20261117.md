# 63低内存存活候选

用户要求内存压力大时允许卡顿，优先维持游戏进程。报告9a58a9d4中的62 PID17941和2427均为reason3，系统最后RSS采样3.64/3.04GiB。旧fatal文件仍停在00:47，不能用来解释新退出。原报告及展开日志见.local/memory-survival63-20261117/。

## 预先约束

采用渲染安全边界回收，GPU生成且缺少CPU权威副本的图像继续保留。原分辨率、目标帧率配置及游戏逻辑时间沿既有设置执行。回调只投递原子压力信号，渲染线程在提交/呈现入口实际处理。

Android34起前台RUNNING_MEMORY及onLowMemory通知已停止发送，因此每半秒主动读取本进程VmRSS与MemAvailable。初始RSS门槛2GiB触发回收、2.5GiB触发更强回收，系统可用内存低于总量10%/5%也会触发。门槛依据本次3GiB以上的退出样本提前留余量，属于首版工程选值，尚待原机校准。回收间隔按5秒/2秒限流，压力升级可提前执行。

压力到来先停止后续推测预热，立即释放未使用预热pipeline和压缩改写缓存。完成GPU提交、写回和异步池复位后，核对所有槽已无pending/recording/读者/等待者，随后清理空闲descriptor池、输入和uniform arenas、未受guard保护的readback chunks及长期未用的CPU权威纹理。强压力时清理可重建pipeline对象。预热模块、布局和pass仅在GPU安全且所有预热线程离开驱动调用后清理。最后调用bionic M_PURGE归还分配器空闲页。

该候选使用新分配重建被回收缓存，可能增加后续首次技能或场景的编译/上传停顿。内存存活收益及目标设备压力场景实测尚待执行。

## 参考及当前检查点

Android回调范围取自ComponentCallbacks2官方文档，https://developer.android.com/reference/android/content/ComponentCallbacks2。分配器purge参数由当前NDK26.1的malloc.h定义核对，M_PURGE从API28可用。未引入第三方实现。

首轮Android native编译退出0。生产回收、原子请求和worker生命周期提取测试覆盖GPU权威图像保全、在途资源延后、读回guard、临界门槛与限流，并含删除GPU完成/权威保护/在途保护/worker保护的负对照。当前完整APK构建和真机验收待执行。索尔62人物闪烁另见CODEX-VIDEO-FLICKER-20261117.md，未将其归因于当前内存报告。

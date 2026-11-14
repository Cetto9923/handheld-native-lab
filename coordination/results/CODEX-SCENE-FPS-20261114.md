# 各场景性能推进与开场录屏检查

## 本轮目标及画面对照

用户要求把已修显示问题沉淀到经验库，当前重点改为提升游戏各场景帧率。coordination/EXPERIENCE.md已追加L-010至L-015、21包的验证范围以及可复用的显示回归检查点。原工程目标为原速真实60FPS，当前30限帧配置属于已有测试条件。

21源码780ebc6，APK07f5f036，RG477M实际PID32442。用户确认本轮女主不闪烁，画面回归以该包及对应证据为对照。当前未宣称所有场景或所有机型显示已验收。

## 两分钟录屏

原片screen-20261114-231601.mp4已从RG477M取回，时长119.606522秒，H264 1280×960，AAC mono44100Hz。原片SHA256为0999ddd4d29c9c1803c7a9752fb0c81cf1660f5cdce4a65898bebe69aa1f4e69，大小110468286字节。内容为水晶、巨龙及大范围特效的开场影片。当前同PID原生日志存在1920×1080的MTK VP9解码器配置。

分析生成contact-sheet.png及48kHz mono PCM，近静音阈值为绝对样本值≤4、持续至少15ms。仅片头0至0.348625秒命中，后续未命中此类缺口。该检测对重复、爆音、非静音遮盖和更短缺口的覆盖有限，听感改善尚待实测。

AAudio日志按每次real AudioOut stream ready划分生命周期，输出key可复用、xrun会重置。首个有声6ch周期从0增至21，后续一个周期累计至3。跨重开累加不能用来定位录屏中某一瞬间。实际设备输出burst3075帧，capacity6150帧，buf3075帧，48kHz下约64.1ms/128.1ms容量，当前performance NONE。下一步区分客机供给间隙、source pacing/FIFO、transport及路由重开，而后按事实选择优化点。

录屏date_taken/creation_time与最后修改时间均为23:16:03，文件名为23:16:01，原始采集完成于23:17:53。最初将creation_time作为片头的计算与采集时刻矛盾，已在clip-frame-summary.json显式撤回。以完成时间作为片尾的粗对齐已另存，仍缺同步画面标记，分段数值不作为该影片的确定FPS或欠载归因。编码帧率只描述录制媒体。

## 同次运行的性能证据

engine.log确有旧PID19148与新PID32442两段。engine-session-32442.log从本轮CODEX_FRAME metrics入口截取，frame CSV文件名固定32442。末段60帧窗口约27至28FPS、CPU提交约18ms、GPU约29至30ms，完整数据在engine-current-summary.json。各段场景映射尚未完成，教程、影片与游戏实景分别记录。

已有历史城内移动记录约18至20FPS，CPU descriptor/snapshot及顶点复制占有明显成本。现有vertex-prefix只读评估提供按真实fetch终点缩短复制的候选，包含first/baseInstance、格式末端、quad转换、GPU indirect未知与缓存键退化边界。后续按实际CPU样本及复制分类决定是否接入，收益由同场景A/B验证。

## 场景覆盖与优化顺序

| 场景 | 当前材料 | 下一项可核查指标 |
| --- | --- | --- |
| 开场影片 | 本次119.6秒原片、VP9路径、音频日志 | 音频生命周期与欠载、解码/上传/同步耗时、冷暖长帧 |
| 火场走动 | 本轮教程及同PID CSV，历史移动CPU样本 | 连续移动FPS、P95/P99、descriptor/快照独占CPU及实际复制量 |
| 战斗技能 | 本轮同进程日志与历史原故障流程 | 实际技能输入段、GPU pass成本、新pipeline编译与长帧 |
| 室内/副本 | 历史旅店同机18至20FPS | 相同存档、相同机位、固定分辨率、冷暖对照 |
| 第三章水都 | 既有真实阶梯闪烁记录及定位报告 | 画面回归和连续移动帧时间分别覆盖 |

本轮已尝试复用现有gfx-cpu-profile采3帧，执行时RG477M从ADB断开，返回device not found，未写入标记、未取得该次CPU样本。cpu3-plan.json及cpu3-arm.log保留失败。生产源码及运行配置本轮未变，21继续作为画面对照。设备恢复后先确认PID和场景，再采样已登记的热点。

## 证据位置

原片、ffprobe、音频分析、按生命周期拆分的xrun记录、同PID CSV、engine及guest原日志、时钟校准、时间假设撤回、截图和命令计划均在.local/codex-recovery-20261114/scene-fps21/。当前完成经验沉淀及取证，实际场景帧率改善与声音卡顿修复仍进行中。

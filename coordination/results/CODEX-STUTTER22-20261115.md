# 22版战斗录屏卡顿与23预热候选

## 构建和输入

设备RG477M01445122，应用org.ffrs.gamewindow，当前22进程PID18193。22生产源码0bd4dc54587885e5453fed71fe1159fd7fd3e687，APK SHA256为2e4c354ed1aa8f2d6bc0b281069c84e3f931922e339e3d10b83a16459faf4774。本轮用户要求检查新录屏，随后确认已再次释放技能，并反馈其它技能也卡。root始终把游戏输入权留在用户。

原片screen-20261115-005332.mp4，36.717133秒，30,747,857字节，SHA256为6d9fc05ad78fc4fdd0d7ba84450a9a252585d29c0c56a3ffeb23b9178f727905。捕获、命令、同PID日志、视频分析和后续包均在.local/codex-recovery-20261114/stutter22-user-video/各子目录。原片和游戏输入保留。

## 视频观察

起始为世界地图，随后黑屏转入森林战斗。最后全黑帧在6.001733秒，6.233756秒已开始战斗淡入，6.366056至7.556589秒另有1.191秒的采样空档。战斗主动攻击段按7.556589至34.9989秒取499个相邻视频间隔，其中超过100ms有26次、超过250ms有16次、超过500ms有8次、超过1秒有1次。

| 画面内容 | 视频PTS两端 | 相邻样本间隔 |
| --- | --- | --- |
| 英勇斩击闪电 | 16.879578至17.937789秒 | 1.058211秒 |
| 反击物理攻击白光 | 23.656022至24.484600秒 | 0.828578秒 |
| 机械陆行鸟起手 | 30.633589至31.494567秒 | 0.860978秒 |

617个视频sample和相邻编码PTS描述录制媒体，游戏真实帧率由原生日志单独核查。AAC有1580帧，PTS连续，上述时段持续有音轨能量，短咔或重复声音的听感不能用近静音检测排除。没有把creation/modified直接当片头时间，也没有将视频秒点与日志强行绑定。

视频报告video-review/report.md SHA为e7fc0467ea275b3171a792c993ac250467ef7cfcd69a10c1035cdc3bc2981db1。gap-pairs.jpg及原生关键帧支持复核。

## 同进程实际长帧

engine.log含旧PID历史，本轮从codex-frame-18193.csv入口切分。按两次AAudio重开事件的monotonic区间25183.485至25220.360独立取窗，CSV33015至33589共575个间隔为15.618FPS，P95为149.52ms、P99为843.76ms、最大2024.12ms。超过250ms有23次，超过1秒有4次。该36.875秒窗口与原视频尚无绝对起止同步证据。

| frame | 总间隔ms | pipeline创建ms | 实际创建数 | shader加载ms |
| --- | --- | --- | --- | --- |
| 33061 | 2024.1 | 0 | 0 | 0 |
| 33081 | 1177.3 | 1041.7 | 6 | 73.1 |
| 33277 | 1046.9 | 924.6 | 7 | 59.7 |
| 33336 | 619.7 | 507.2 | 4 | 39.5 |
| 33548 | 863.5 | 794.3 | 3 | 37.1 |
| 33569 | 861.6 | 740.7 | 3 | 35.5 |
| 33571 | 1275.1 | 1094.6 | 5 | 103.9 |

23条超过250ms的长帧中，pipeline创建累计10.252秒，共66次实际创建。该窗口全部67条已打印stall累计pipeline创建10.414秒、shader加载0.887秒，共73次创建。同步pipeline创建是已实测的大停顿来源。

最大33061帧的2.024秒中，pipeline与shader计数均0，submit CPU4.42ms、GPU41.71ms，仍待定位。该时间没有归入编译收益。主6声道音频流在两次重建之间xrun从0增至4，重开后的0不能覆盖此前增长。

末600个暖态战斗菜单间隔为30.00FPS，P95为40.11ms、P99为49.57ms，仍有6帧超过50ms。对应GPU约27至28ms、submit CPU约13.7至13.9ms、vertex快照约18.79MB/frame。这个末段不能代替此前技能施放表现。

后续CPU-only三帧为39122至39124，共1569draw、192dispatch、27submit、0shader。归一后wall33.259ms/frame、measured20.613ms/frame、uncovered12.646ms/frame，other exclusive8.036ms、descriptor4.842ms、snapshot2.676ms、draw2.194ms、hash0.840ms。旧PID的56162至56164样本没有混入。

日志复核报告log-review/review.md SHA为678488e343d1e5c5946d8abc5629cdf215478e60ca825b30a516c429f2f3010b。

## 再次释放及其它技能

用户确认再次释放后，root保全repeat-skills/新包。新CSV到62640，原1至36660是其完整逐字段前缀。frame55380至55410的31个连续长帧均为shader0、pipeline创建0，间隔约61至74ms，GPU报告多数62至67ms，另有109至112ms读数。新数据支持继续处理特效实际绘制开销。

frame61692总间隔1213.7ms，其中pipeline1048.0ms。61708总间隔1008.2ms，其中pipeline889.0ms。61694、61701、61711还分别有435.9、118.5、510.5ms的pipeline创建，五条共18次创建、3001.9ms。每次用户技能的精确输入时点未独立同步，未给这些帧猜测技能名。

root另用既有gfx-pass-live采90个有界GPU pass帧，输入在capture/gpu90/。诊断窗口会改变部分resident/精确计时路径，原始普通帧与profile数据分别解释。marker已消费，pass_prof_active按采样帧过期。该采样用于pass成本定位，不能充当普通运行的FPS收益。

## 已完成的预热链修正

本进程确实使用Mali driver cache，载入约45.3MB，后续成功保存并增长，旧平台注释不能据此推断该设备缓存无效。已有warm列表1036条曾全部重建成功。新见的管线仍在战斗线程同步创建。

发行inspector旧版只识别裸key与ib1，拒绝现有stb1、vin1及组合identity，导致新版实际学习表无法进入发行种子。现已同步canonical语法、类型格式集合、族顺序、边界、有序唯一及完整identity hash验证。15项单测、真实学习表及archive索引验证通过，源key缺失0。

23源码dd76193d9fab006e7318737b2eecf54250b933d8新增可选APK预热asset接线。GuestBootTask后台启动前，将pipeline-warm-list.bin原子落地为实际legacy/managed runtime中的apk-pipeline-warm.seed。缺失或损坏时记录并继续游戏启动。现有学习表、shader seed与数据侧seed保持各自的加载入口，warm.start通过完整serialized key将APK补充表合并进现有预热队列。每台设备自行创建兼容的Vulkan管线。

Java stage使用有界复制、外层完整record framing、fsync与原子替换，完整recipe语义由发行inspector及native reader核对。失败、截断、超限、异常与旧seed保留的JVM契约通过。独立离线QA报告pipeline-review/qa/review.md SHA为1ed2122231f289b2e3d94a89438d4632139fc4050630bc8b09d1005aad0b95b2。

随包清单来自本机正常运行成功记录，最终1162条且全部唯一，1084graphics、78compute。较22安装前1036条增加126条，原学习表字节前缀完整保留。种子1,220,416字节，SHA256为f7285c7eb3071e04074eb2e848bddeef568655b4ee1127edf7ae867d74207acc。shader archive仅读索引校验，1114个源key均存在。该APKasset包含portable管线状态recipe。

## 23候选及未完成范围

101项宿主检查、Android构建退出0，38份构建输入前后摘要一致。APK为.local/codex-recovery-20261114/apk-history/ffrs-1.0.23-preview-battle-prewarm.apk，3,679,303字节，SHA256为845d1c4c57272a429edc918be71389b8ab25f4fe5c0b431d4fc335af20763fe3。包内seed已重新解出核对，native SHA为f5af6463d1914fbc87c6ca01cae6b0630c92f9b5738f8d0422204faeeebdc33a，LOAD段16KiB对齐。

用户随后确认“已结束并保存，现在安装”。23于2026-11-15 01:25覆盖安装，APK重新拉回核对摘要一致，安装前后备份tar SHA均为86d8a9f5f9a77604ff5d6ebde5e716169f2ac804b15306d26e72b17f26a93ae8。安装后PID19646，修复下述保存文件权限并从标题重开后的PID为23537。包内seed在设备实际runtime的SHA核对为f7285c7e。

PID23537启动日志确认学习表1162条、APK补充seed同字节1220416且added0、原数据侧seed added0，最终union1162。全部warm_built1162、warm_failed0，warm_ms计数1181。城内读档阶段可见实际warm_hits，并保留一条未覆盖的新管线创建351.6ms。APK seed路径加载与已学recipe预热已实际执行，首次技能收益仍需同技能复测。

持续GPU重负荷、未知2秒停顿、音频短卡、移动与其它场景帧率仍需各自验证。地图接口、深度与精度修复沿当前生产路径保留。root已退出刚打开的读档界面，将游戏输入权留给用户，在23新PID上继续取证。

## 保存权限遗漏与恢复

此前仅核对安装前后存档备份一致，遗漏了正常游戏写入权限的检查。本轮before-guest_boot.log原行99517明确记录GameSaveData0以mode2写打开失败，errno13。实际文件GameSaveData0、GameSaveData1为shell UID2000所有、mode644，应用UID10174的test-w失败。这也解释了本轮多次备份tar一直保持旧摘要的现象，写入失败的那次进度未落盘。

root确认23处于标题画面后停止进程，保全原文件字节、摘要、UID、mode、mtime。通过run-as创建同字节stage，核对原文件与stage摘要一致、保留原mtime，再原子替换为应用UID所有的文件。恢复后UID10174、mode640。两个文件修改前后各自的内容SHA保持一致，应用UID实际读写打开并关闭成功，操作没有写入数据。原文件备份与逐命令恢复记录在seed-build/save-owner-repair/。

首次stat命令的format参数被ADB远端shell按空格拆分，进程已停且文件尚未处理。已保留attempt1日志，改为无空格format后恢复流程完成。随后重新启动23，PID23537。

权限恢复与读写打开测试已通过。游戏内正常保存落盘尚待用户操作确认，已请求使用游戏内系统菜单保存并反馈结果。此前失败的内存进度未宣称恢复。

## GPU90离线复核

GPU pass采样为72843至72932连续90帧，每帧46pass及63compute，9810项data与1038项summary完整，recorded等于complete、skipped0、result0。每帧218个查询，限制1024，最大12slot，限制32。该采样属于22当前战斗菜单的诊断路径，主动技能覆盖尚未确认。

最重项为第10个1024×576、7附件、fmt122的混合pass，145draw，平均15.1497ms/frame。其完整first/last shader组合及mixed标记保留在gpu-review/。第1混合深度pass为138draw、5.3144ms/frame。所有pass累计平均43.1133ms/frame，compute合计5.6533ms/frame。此计时包含profile路径条件，普通运行FPS与GPU成本沿前述normal日志解释。

GPU报告gpu-review/review.md SHA为9f5d2dabc305a1679f55af1f268433d590af15c0ae939450ff5e03cfb9d76c8e。独立复核没有按first/last key猜测每个draw对应的角色或特效。

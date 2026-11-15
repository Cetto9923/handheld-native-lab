# 23版设备恢复、保存回归与性能对照

## 当前检查点

设备为RG477M01445122，USB充电，恢复时电量65%。荣耀平板也在线，所有设备命令均显式选择RG477M。已安装1.0.23-preview，APK SHA256为845d1c4c57272a429edc918be71389b8ab25f4fe5c0b431d4fc335af20763fe3，构建源码dd76193。新游戏进程PID9359。

本轮证据位于`.local/codex-recovery-20261114/resume23-20261115/`。设备仍使用已发布23包。源码另外修了一处诊断日志参数类型，尚未构建或替换设备包。

## 正常保存和读回

启动前完整备份存档，`device/saves-before.tar`为1496064字节，SHA256为fb089cfdd5df227e7fbee07ee1cdc77078d0ff804bc7baae0e4df1fd8cd4e42b。

通过标题Load Game读取第三章01槽，进入水都旅店的紫色保存点。经系统菜单的保存入口，明确核对SAVE DATA后正常保存01，随后通过LOAD DATA重新读回并返回相同位置。

| 项目 | 结果 |
| --- | --- |
| 游戏时间 | 7:38:52更新为7:40:33 |
| 实际文件 | GameSaveData1，414000字节，UID10174 |
| 原SHA256 | 634ac0b016ae5236924516bc48d79afb66fcdcbb6cfaa4e3de81ce41b27c1004 |
| 新SHA256 | 完整摘要见device/save-comparison.json，前缀b50e62976451 |
| 写入入口 | guest-save-full.log第80958–80960行，mode2打开、WriteFile414000字节及关闭 |
| 重新读取 | 第83569–83571行，mode1打开并关闭，readback-room.png回到旅店 |
| 其它存档和设置 | 此次前后摘要均保持 |

本轮只更新了已备份的01槽，另在原本空的02槽正常保存世界地图位置，游戏时间7:47:53，作为后续同机位对照恢复点。存档原件和world02的完整备份均保留。此前因权限失败没有落盘的旧内存进度仍无恢复证据。

engine.log在本次启动前为34854515字节，按该偏移保全新进程段，并核对CODEX_FRAME路径9359。guest_boot.log本次启动会重置，使用新进程完整日志。曾按旧长度切片得到空文件，已在plan.json标明并改用完整当次日志。

## 主动技能复测

在第三章城外森林战斗释放机械陆行鸟两次，采用现成scene tag关联引擎帧号。每次采样约15.5秒，窗口中不截图或录屏。第一次有三名敌人，第二次只剩一名，结束后进入结算。窗口包含菜单和结算，不能用整段FPS直接评价纯技能或比较两次性能收益。

| 窗口 | 帧界限 | 间隔数 | P95 | P99 | 最大间隔 |
| --- | --- | --- | --- | --- | --- |
| 首次机械陆行鸟窗口 | 25276→25723 | 447 | 47.965ms | 77.337ms | 296.013ms |
| 重复机械陆行鸟窗口 | 35394→35856 | 462 | 46.077ms | 55.986ms | 77.520ms |

重复窗口frame35477的逐帧日志仍有gap68.6ms、GPU71.62ms、submit24.22ms，shader和pipeline创建均0。整段另有一条0.4ms的pipeline创建。此结果保留暖态GPU负荷问题，预热收益与持续GPU耗时分别继续检验。

独立本地复核见`log-review/review.md`及summary.json。所有结果限定PID9359、23包及对应采样窗口。

## 日志打印修正

复核发现scene::close_window的shader_slow_ms格式为%.1f，实参却传unsigned long long，导致此后的浮点和整数参数错位。因此旧包scene leave中从shader_slow_ms起的聚合字段不能按标签采用。after-chocobo中所谓79559ms及17次创建均属于受影响字段，撤回其字面解释。对应原始逐帧stall累计为8次创建、1228.3ms，未受到该聚合打印错误影响。

源码仅把实参改为double(win_slow_ms)。`scene-log-contract/run_contract.py`抽取真实生产函数，编译器格式检查会拒绝旧实参，修正后通过，并对11个数字字段逐项核值。日志和命令计划保留。已发布23仍含旧打印，后续分析沿逐帧stall和CSV核对。

## 性能对照输入与范围

源码审阅确认基线LDC component fold和forward temps为0，position precision为1。现成组件契约和真实模块联合校验完成后，以同一23 APK、相同世界地图02机位执行startup开关对照，B10、C11及原配置回验分别使用新PID。原配置及driver cache备份保留，冷编译、预热命中与稳态GPU数据分别记录。

CPU顶点快照候选仍为只读方案。固定1/8前缀为未经实测的启发式，现有逐draw资料缺少完整bound及layout，暂未实施。两份只读源码报告分别在perf-review和gpu-source-review目录。

候选的真机结论见下表，完整游戏场景覆盖和动态显示回归仍未完成。

## 联合组件检查

既有precise和indirect契约实跑退出0。5个真实VS/FS、10条LDC或LDC+temps后接precise的路径通过ASan/UBSan，25次spirv-val退出0。VS目标浮点算术NoContraction完整，显式FMA和062df的FragDepth写入来源保持。5个真实模块的LDC折叠均为0，覆盖了实际temps转发；非零LDC联合分支和typed/VI变体仍超出这组检查范围。local结构检查器首跑误判AccessChain，已修复并复跑，失败证据保留。生产helpers前后SHA一致。

报告tidy-joint-review/report.md的SHA256为7c39528a55f2dc905a058ddf3c64fe8dce83eb8b79b84f7baed37961240a6693。B10新启动PID7005已实际命中734次LDC，warm_built1202、failed0。初次B采样起于加载画面，已排除并保留原数据，等待场景稳定后重新采样。

## 现成开关对照的阶段结果

各臂均使用已发布23 APK、30FPS诊断上限、02存档及同机位正常运行。窗口内无录屏或GPU读回。B10为PID7005，C11为PID10849。每臂采用13组完整60帧NVN PERF窗口，边界组剔除，CSV保留全部长帧。

| 配置 | GPU中位 | submit CPU中位 | draw中位 | dispatch中位 | 间隔P99 | 大于50ms间隔 |
| --- | --- | --- | --- | --- | --- | --- |
| B10，仅LDC | 25.09ms | 11.59ms | 354.1 | 53.5 | 44.745ms | 5/902 |
| C11，LDC与temps | 24.90ms | 11.85ms | 353.9 | 53.5 | 43.278ms | 2/903 |
| 恢复原配置，新PID13530 | 24.90ms | 11.53ms | 354.0 | 53.6 | 45.283ms | 6/903 |

C相对B的GPU差约0.8%，恢复原配置的GPU中位与C相同，未量到可靠稳态收益。原A00长会话GPU23.96ms，采样时刻及进程资源历史不同，因果结论采用后续新进程回验。新PID13530已完成02同机位30秒普通呈现回验。

B启动warm_built1202、failed0、warm_ms8665。C为1202、0、106221，累计temps_forwarded704863、ldc_folds748。warm_ms是源码计数的预热耗时，完整启动时长另外测量。首次修改后驱动缓存状态不同，单次耗时也不能预测每次启动。当前稳定GPU收益不足，候选暂不纳入默认配置或发布包。

设备中途断开后用户重新连接，首轮计划在写开关前中止，失败证据保留。exec-out备份配置时未传播cat失败退出码，错误文本进入stdout，已据No such file纠正两个marker原本不存在的记录。恢复时只移除本轮创建的两个marker，并以应用UID的test -e退出1核实原存在状态恢复。driver cache及存档备份保留。

本轮主动技能及世界地图采样不覆盖全部游戏场景。当前性能问题继续开放，静态截图也不能证明开场人物的动态闪烁回归通过。

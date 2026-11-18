# 兵工厂卡顿：接手与实测检查点

尚未修复或发布。用户允许RG调试，荣耀已分配统一游戏库。

输入ZIP SHA97b18209e99b7fe55fea4a7fddfc4044fd7f36f549fc1efed0979719761f56ea，视频SHAe7488b8e68acd25826809597a8737cd04f93f092bf311c7dcd285b7c20f44f2c。原始与哈希保留在/Users/yuyan9923/.cetto-arsenal-inputs/。导出设备OnePlus PMS110。视频编码30fps不是游戏帧率。

RG原67的PID24992成功Continue进入第4章兵工厂存档点。30.179秒静止窗口901独立呈现，29.82fps，P99 40.38ms、max44.22ms，0帧超过100ms。短移动6.49秒27.73fps但窗口含截图，不当作正式移动性能验收。CPU6帧中descriptor54.18ms、snapshot26.49ms、other29.90ms；普通窗口GPU约27.5ms。CPU/GPU不同计时区间不可直接相加。旧analyze_stalls.py的submit_cpu_ms英文注释不是当前计时语义，不能直接归为vkQueueSubmit驱动卡顿。

原RG67 APK与12个私有文件已归档逐SHA核对，四文件玩家副本以应用UID放在shield-regression-20261118095001。升级后原12文件未变。首次安装72工作目录根候选发现SHA2e3ebda4非最终版，未作性能验收即停止，纠正为主发布目录最终72 SHA0fb453ad73e8d18944a2ccb2d1039ad6635b6f94b6b9998d51c5fa7920588371并实机回读一致。最终72 PID31689进入同场景，进行无截图移动窗口。

未改源码，既有移植经验指南WIP保留。最终根因、OnePlus复现、战斗、音频蓝牙、修复候选及新发布全部未完成。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 2026-11-18 后续探索，尚未验收
最终72 PID31689同存档移动到随机遇敌，31.1388秒25.05fps，9次>100ms；frame4064间隔1259.9ms，其中新管线创建909.2ms、31新shader80.6ms。证实该次长帧主要贡献之一为首次编译，不等于全部持续卡顿根因。另一随机敌组PID2692仍有404ms编译长帧，敌组不同不能A/B。
私有候选仅APK预热资产修改：旧1750配方扩为1898（148增量），1971681字节，SHA c7a1d89d498e013d9a8407268892f913193626fcb83e0e91fa5c416e702dd287。shader索引核对无缺项；native/Java字节不变。Android及host测试exit0；APK SHA90e031dd38d8921d765ccb0292da31d56ed32137d27a0205dabdd10c4aed1164。没有发版/上传。
RG原运行缓存保留为vk-pipeline-{list,cache}.bin.arsenal-original；冷A生成物另存.arsenal-coldA，均未删除。现候选PID6973，原正式存档未动，仍使用独立导入存档。
冷A生产版移动随机遇敌20.335秒17.70fps、max1944.6ms。冷B候选首个19.196秒移动窗口28.13fps、max67.97ms，但未遇敌且启动预热等待更久：这不是受控同场景对照，不能据此称修复！后续需相同预热阶段/相同敌组对比，并完成全回归与音频验收。
统一库目前只有SO模块更新不能包含该资产，需受控签名资产manifest及退出后激活/回滚支持；不得将本机候选说成线上热更新。

### 候选未过关，恢复生产72
PID6973预热最终1898成功/0失败，累计warm_ms257639。移动第二窗口28.829秒27.06fps、max52.23ms，但进入战斗在窗口末附近，不能当战斗验收。随后真实连续攻击21.901秒21.92fps、max329.77ms、5次>100ms；frame10510中pipeline_create255.7ms+shader23.9ms。继续攻击28.248秒23.36fps、max105.28ms。预热资产补充只缓解已覆盖管线的首次编译，不能修复全部持续负载；不发布该候选。
RG测试结束读取HAL CPU87.252°C、SKIN50.494/status2、SoC88.495/status3，而缓存ThermalStatus0不一致，已停止游戏冷却；没有改散热或频率设置。候选缓存另存.arsenal-coldB，原缓存复制恢复且SHA复核。恢复主发布目录生产72，不启动长时间空跑。
只读审查：vertex约17.62MiB/帧、index0.96MiB/帧是snapshot主要复制来源；已有46/47完整字节比较vcache负收益历史，不重复。正在审查不可变descriptor结构计划，不能跳过动态内容、revision、barrier或提交快照；尚未改源码。

## 用户OnePlus诊断改变优先级
Library上传FFRS-report-20261118-094949.zip（本地SHA496bfdb52cae172de2e1a3ef6026d2c2cd4aeed7db409213e040ece9be12b2d9）仅读取。metadata：2026-11-18 09:49:56+08、APK71、OnePlus PMS110/SM8850、Android17/API37，导出时可用内存约2.81GB且low_memory=false。设备profile标签phone_sd8g3/60/1920x1080是配置而非实际帧率或内部画质验收。
日志GPU明确Adreno840，driver0x8034a035；guest头BuildID736837afd390442ed7c74a7de730e71b对应旧1.0.0，RG当前b7f965…是1.0.1，同存档不等于同数据输入。
当前engine.log采样内160次相同graphics pipeline创建-13，缓存失败后VK_NULL_HANDLE重试仍失败。实际VS17c6ba1e659fec3a/FSab0a6325b8496fa2；pstat字符串另含compute45d6f0410c6e92a1只是状态列表，不能误称compute加入graphics stage。末60帧日志attempt FPS5.38、submit_cpu159.49ms/GPU26.07ms；不是独立呈现CSV验收。代码成功后才记pipeline_create计数，故失败反复编译时create_ms=0不能排除编译阻塞。
旧engine.log.1还包含VS5a97a7e1a99cce45/FS350ac5272e645d62（769次）和VS3bce5482c09be3fb/FSc9c4f100629aeee7（25次）。engine.log.fatal修改时间2026-11-15 01:13，与本次导出不同，不能认定当前崩溃。日志都有head/tail截断，缺中段及失败管线recipe，不能凭-13确认SPIRV或驱动根因。
父级确认无其他原FF writer后登记diagnostic scope：仅增加独立有界失败recipe日志及创建耗时，加入报告allowlist；不把失败塞入成功warm列表、不缓存跳绘制、不修改渲染语义。诊断候选构建中，shader静态检查并行。RG原12私有文件再次逐SHA确认全相等。

### 诊断候选完成（不是修复发布）
新增warm::record_failure独立pipeline-failures.log：最多8个唯一管线、512KiB文件，O_NOFOLLOW/普通文件校验；只写模块身份+portable recipe，不写游戏数据/存档，不放进正常预热列表，不缓存失败或跳过新的调用。失败现有日志补failed_create_ms，仍执行原创建/重试逻辑。DiagnosticReport白名单包含该日志。
.local/arsenal-diagnostic-20261118/FFRS-1.0.72-arsenal-diagnostic.apk SHA e3ca01816f93773ec272c2df214b0aea95f49edeae324199989ff5bd8715391b。Android0、host passed=true且659源码输入构建前后相同。新增生产提取record_failure测试8项ASan/UBSan通过（重复/8条上限/文件512KiB/未知模块/溢出/invalid/符号链接/配方往返），报告测试4项通过并实际断言新白名单导出。不是完整游戏验收。
RG PID19868覆盖安装全文SHA回读匹配，独立用户存档Continue进入第4章兵工厂并短距离移动正常，完成烟测后停游戏、恢复正式72。没有操作荣耀。没有上传社区，没有称该版本修好了840卡顿。
本地归档含6个失败VS/FS：原始SPIRV全部spirv-val Vulkan1.3通过。按日志启用的通用stencil/inline/quad/precise链离线应用后仍全通过；三VS仅分别增加700/747/680个precise约束，inline/quad没有改动。此离线链未覆盖实际运行时vertex/input/typed-format变体或真实失败管线状态，不等于完整driver验证。故仍需840的精确失败配方或该设备验证，不能凭通用SPIRV通过就归咎驱动。

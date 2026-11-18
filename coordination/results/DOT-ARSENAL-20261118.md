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

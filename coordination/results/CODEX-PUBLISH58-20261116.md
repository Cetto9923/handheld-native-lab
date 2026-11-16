# 1.0.58优化预览发布记录

用户先要求收口提交当前版本，随后明确今天发布一版优化。本次授权沿既有GitHub流程推送当前开发分支、精确构建源标签及APK附件。目标仓库为Cetto9923/handheld-native-lab，版本v1.0.58-preview。

## 当前产物

- 实际构建源码d79ffa60c33e7712efbaffd7fa84690b58633348，构建时工作区干净。
- 119项宿主检查、Android构建退出0，89份构建输入SHA前后相同。
- APK保存在.local/releases/1.0.58/FFRS-1.0.58-preview-20261116.apk，3728599字节，SHA256为a01c2f64a744ddfd8706b46184d8f358d012a4cdff33dcb196148a7a5df4c4bf。
- Native BuildID为5726910f565387d2d80f0d8a54cf7a6c63457859，包内ELF与保留符号一致。

## 已有实机证据

荣耀PID10580已完成覆盖安装、全部9份测试存档逐文件保全、APK回读、读档、神殿移动及进程内首次消失突袭采样。移动短窗约25至29FPS，首次技能完整8秒19.6385FPS/max260.13ms。稳定30、常量池稳定收益、全场景显示、长期内存与A2DP完整听感回归尚未通过。完整原始帧、输入及归因校正见CODEX-HONOR-SUBMIT44-20261116.md。

收口时荣耀ADB离线，最后确认的触控已归零。设备末次实验臂为none，即58新常量池，不能把启动时old-mixed名称当末次状态。没有运行中的本轮构建、采样或自动输入进程。

## 发布过程

发布计划、附件检查、差异历史审计、命令及普通日志保存在.local/releases/1.0.58/publication/。发布说明使用优化预览版，包含已知掉帧及未验证范围。远端源分支初始aabc9fb68bc685cb61f0ab41a96fbbdae6eb34ec为本地HEAD祖先。Main当前ecb9b57保持现状。

## 发布与回读结果

GitHub发布已完成，Release为[v1.0.58-preview](https://github.com/Cetto9923/handheld-native-lab/releases/tag/v1.0.58-preview)。APK及SHA256附件均上传成功，发布类型为预览版。

标签精确指向d79ffa60c33e7712efbaffd7fa84690b58633348，开发分支首次发布推送为ca05526cf30559278fbe70d134e034f84bba32af。远端下载APK重新核对3728599字节及完整SHA256，与上述本地产物相同，下载的校验文件逐字一致。正式发布前最终自审覆盖423个新增可达blob共154274750字节，所选高置信凭据标记、排除载荷路径及GitHub对象大小检查0发现。

发布日志状态PUBLISHED_VERIFIED，完整命令、退出码、Release API结果、远端下载及摘要在.local/releases/1.0.58/publication/。当前远端Main仍为ecb9b57fe90d5a5d24299980095bc49953c26aaa。最后追加本发布结果提交后，再次推送同一开发分支。

本次发布任务完成。用户要求先收口，性能优化主任务按用户要求暂停在58检查点，稳定30目标保留未完成状态。

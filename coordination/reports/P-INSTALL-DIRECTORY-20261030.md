# P-INSTALL 目录直读候选003（实现检查点，非游戏验收）

用户最新要求优先解压拷贝目录，依据公开Xash3D固定目录+file permission与EasyRPG/ScummVM SAF作法，原生stdio路径和Android受限存储边界已写OSS-NOTES。当前使用固定HandheldNativeLab/data等有限路径，不递归扫全盘；首次如需授权显示系统设置，数据只读，runtime/cache/log/save独立于拷贝目录。ZIP导入另作为备选候选002。

APK003: dev.handheldnativelab.preview v3，SHA1b39328af6d8b05099576685e488e377fe18f68c9ffae7bc711e13424431b910，native56c50ff3fe0c5841611330592c003accce54ccda62a3ac67983dc82ad90a5235（与002一致）。构建99536 exit0，完整本地目录导出15998 exit0，73511文件/15192663714bytes逐一SHA核对，indexda8f5877。只用用户本地冻结输入；产物均.local，不上传游戏数据。

主机23项真实文件系统检查通过（data-contract-v5.log），包括坏包、截断、版本/大小/路径、取消与恢复、旧版本完整再校验、目录只读/缺SDK/元数据symlink。测试fixture自写，不替代游戏。原Java首启门控保留，原nativeStartGuest ABI不变，新增稳定saveRoot只影响路径；GPU/音频/游戏时间代码本阶段未改。

RG477M01445122，Android34：新包更新003成功，缺数据setup可见；all-files当场原已有allow，设置页已查，无appops改写。不能据此称首次授权完整通过。002真实SAF错误版本和不足空间拒绝有XML/截图/logcat。完整目录正new-only拷贝到SD根（先确认原不存在），session90475/PID/命令/cwd/log见.local/installer-20261030/android-directory-003/transfer-plan.json；结束后必须查退出码、设备SHA和首次启动校验。

当前完整目录真机启动、旧save恢复/升级、运动/CG/战斗、游戏模式30Hz、完整常设音频和视觉、暂停恢复、独立QA、其他机型均未验收。完整门和旧枚举0→实际NONE10纠正已列.local/installer-20261030/RESULT.md并附原常设清单全文。原TAG d66706e、所有冻结备份、用户主包/原隔离包和原存档不动，无push，不声称可玩。

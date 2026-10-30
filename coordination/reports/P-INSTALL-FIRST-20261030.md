# 下一阶段首个检查点：启动门控与数据包核心

用户要求开启目标模式、开始一目录安装/数据包/多机型适配。旧目标仍为unfinished+paused，create_goal只尝试一次并失败；工具无恢复接口，CUA明确拒绝操作Codex app。持久目标**未启用**；没有伪造完成旧目标或绕过系统限制。已实际开展工程，主基线和所有原备份不动。

## 实际改动

- GameActivity首启必须满足resumed、window focus、PowerManager interactive、有效Surface和正尺寸；onResume/onWindowFocusChanged/surfaceChanged统一重检，surfaceDestroyed清除surfaceReady。原run_guest开关、Intent和数据候选仍保留。只约束首次启动，不声称已运行游戏的全暂停/恢复流程修好。
- APK构建器允许显式覆盖资源/manifest/输出位置，默认原构建路径不变。独立构建脚本只产生 `dev.handheldnativelab.preview`，中性自绘图标和名称，不安装/覆盖基线包。
- 新的无Android依赖Java数据导入核心、Python索引/打包器与格式说明：流式ZIP64、可信APK合同与native/index配对、长度/SHA、空间检查、路径约束、版本目录与最后的原子active指针。旧完整版本与存档不被覆盖。
- 这是组件实现：SAF/UI接入、APK合同资源、真实Android大包导入、稳定存档路径/版本切换与恢复界面仍待完成。失败暂存保留、已存在版本复用和断电持久性也未收口，不能称一键安装成品。

方法来源先记录于OSS-NOTES：官方AOSP SurfaceView回调交错与Android Surface API；SAF及Zip Path Traversal说明。未引入第三方新代码，未降低targetSDK或改SELinux权限以绕过数据问题。

## 构建与真机

工作树task/T02-codex-black-screen，起点572b0d5，原基线TAG仍d66706e。实际测试源码SHA见 `.local/installer-20261030/preview-001/manifest.json`，本报告所在提交保存该源改动。构建45857 exit0，APK SHA256 `1320fb9291ccde78d5f3f4de92d740f59b86a62e0bcd0aafa191f82e2b4f0ef6`。native SHA `7246fea4c13df689bb744d68c2a525941991621e02d41fb0c2382a71efe95ae8` 与冻结版本完全相同，音频/GPU等待/材质代码没有随本单元改变。

唯一设备RG477M01445122，新包独立安装。两个失败完整保留：

1. lifecycle-001 /95451：熄屏80s存活、未启guest；唤醒后Android拒绝读取旧 `/data/local/tmp/ffrs-60fps/romfs` 的shell_data_file符号链接，明确显示缺Paks的setup，而未启动游戏。adb exec-out返回的“文件不存在”文字不当真实guest日志。
2. lifecycle-002 /18700：换到main SHA09cc8f55相同的真实目录，guest启动，但部分296文件fixture缺首次亮度设置的 `b33659227a17c1a9.xgs`，真实FATAL，未跳绘制或补假shader。

随后从冻结归档一次展开完整73495份canonical XGS+SDK，共73496文件6098980888 bytes；34803展开、41356部署均exit0。仅写新预览包，基线包/存档未覆盖。数据源使用原真实路径，未操作其他设备或放宽权限。

lifecycle-003 /28841 exit0：新包PID27833，熄屏80s存活且无guest线程/新日志；唤醒同一PID只启动一次guest，1020条真实present记录，无该次fault/无Surface60秒失败。实际截图为**原游戏首次亮度设置**，核实后只在新预览包确认默认亮度，15秒后核实真实New Game/Option标题画面。NONE实读10、paced1、target30。不是以菜单认定游戏30Hz或完整蓝牙听感PASS。

## 数据组件检查

- JDK对实际Java解包器的16项检查通过：完整导入、同长度内容损坏、截断、额外条目、错误native/index/header、重复路径、缺关键数据、越界/绝对/Windows/保留路径、开始/复制中取消、升级保留旧版本与原子指针。全部使用自有假数据，不是游戏兼容测试。
- Python打包→Java实际导入互通通过；新增BMP私用字符和非BMP字符路径，打包端按UTF-16顺序与Java一致，避免中英文/补充字符排序不一致。
- 完整真实输入索引已准备（52338 exit0）：73511文件、15192663714 bytes，索引SHA256 `da8f58773bc1809a16796fc2701d6095a737062b18258b64ce2ed0b9eb9b7129`。原游戏内容只读取本地备份，不进Git。
- 内置存储起核仅约16.5GB可用，另有可移除卷约114GB可用；这只是只读容量信息。正式导入需计入压缩包+展开数据+保留旧版本的空间，选择策略与UI待实现，不删用户数据凑空间。

证据均在 `.local/installer-20261030/`：preview-001、lifecycle-001/002/003、runtime-input、data-index-001、data-contract-v2.log、bundle-interop-unicode.log；自有fixture原始输出在各installer-contract/installer-interop目录。

## 继续点与硬门

下一步将可信数据合同资源、Android SAF选择/进度/空间/错误恢复界面和版本安装根目录接上，保留稳定存档目录。再生成同目录匹配APK+完整本地数据包，在独立包做真实安装、升级/回滚及冷/暖启动。

完整常设音频清单、真实A2DP野外/战斗耳听≥2分钟、断连重开/AV同步、战斗/转场长帧、人物条纹、内部720P及独立QA仍OPEN。只在RG477M实际验证首启路径；没有其他机型PASS，没有通用安装或完整可玩声明。该轮未推送新工程改动，原私有仓库与基线TAG保持原状。

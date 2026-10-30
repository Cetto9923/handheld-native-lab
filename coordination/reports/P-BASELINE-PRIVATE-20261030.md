# 冻结基线、本地备份与私有源码发布

用户要求特效暂以当前观感收口，保存本地TAG与完整恢复材料，并新建名称区别于游戏商标的GitHub私有项目。随后再做多机型、一目录APK+数据包的安装方案。本次没有继续修改游戏特效或把开放验收项改为PASS。

## 已完成

- 原工作树/分支保留；工程基线 commit `d66706e7e521ad295ac17d965c6ed66cbf23d465`，annotated TAG `rg477m-baseline-2026.10.30`。认领的已有源码WIP随基线保存，未加入untracked荣耀工单，未reset/clean/stash、未切主树/main。
- 本地完整目录 `.local/baselines/rg477m-baseline-2026.10.30/`，`RESTORE.md`、`runtime-manifest.json`、`source-manifest.json`、`archive-verification.json` 和 `github-publication.json` 为恢复入口。实际备份脚本20968 exit0，source打包1605 exit0，归档独立遍历14238 exit0。
- 实测APK b3730d59…、实际guest main、SDK、全部外部shader/配置/cache/隔离存档，以及独立APFS克隆的RomFS均保存。外部归档1654464415 bytes，SHA256 `d63331396fb26c48f84c12834b917597d3b092461bcbf4cdefe9b0b1b645df75`；93818个归档条目、93756个shader目录普通文件，无符号链接，包含5份隔离存档文件。护罩shader9528在归档内核SHA7d20ab51…通过。
- 关键数据与实机相同：UCAS5976848928 bytes SHA `69586840383b838b9ee13946f7d8ef38fd47566fc110d14fae353d4a3b82bd8e`，PAK2958593334 bytes SHA `ebbccff014eceaa94070c33c210502eb5bc5d472def68ecf09074e63f6e2886b`，UTOC与UECommandLine逐项相同。源快照2594683 bytes SHA `a940d1a965602eacf6a008ec496fddb1b9256cbbd2e5b7262dc62a434d77e05a`；Git bundle3898886 bytes SHA `b81602c16bf2e9ef8a6f797c8b12b9a5cc701d1b742162f7af7c1f8fe1d0493a`，bundle verify0。
- 新仓库 [Cetto9923/handheld-native-lab](https://github.com/Cetto9923/handheld-native-lab) 已通过GitHub元数据确认 `isPrivate=true`，默认分支 `codex/rg477m-baseline`。来源代码仅8.32MB/669文件，先审计后通过已登录gh创建和推送；GitHub连接器无建仓入口，未修改全局Git认证。
- 发布为新初始历史，初始 commit `f08cc731e6699be793d98634d1a2acc20923ae00` 与远端同名TAG均已核对。`PUBLICATION.json` 映射到原工程d66706e。两边SHA不同是源码快照与品牌资源差异导致，不把GitHub提交误称本地原提交。
- 未上传游戏资源、原始或生成游戏模块、APK、存档、密钥、`.local`或原Git历史。六张现有游戏美术PNG只保留在本地备份；发布副本替换为自制中性vector图标及应用展示名。中性资源aapt2 compile/link均0。依赖原许可证保留，未擅自赋予全仓MIT许可。
- 推送92568 exit0，远端分支/TAG、私有可见性和完整blob树669文件核验22048 exit0。仅推新私有仓库，没有forcepush或修改旧远端。后续协调完成记录作为独立文档提交，不移动基线TAG。

## 仍开放

人物倒地后的局部暗条纹原因未查明；完整常设音频门、真实蓝牙断连/重开与物理AV同步、战斗/转场稳定30、内部原生720P和独立QA仍未全部通过。本次用户接受的是视觉阶段性基线。APK仍保留研究诊断能力且默认关闭；抓帧数据不冒充正常帧率。没有声称完整可玩、普遍机型适配或已有一键安装成品。

后续安装要求见 [PACKAGING-NEXT.md](../../docs/PACKAGING-NEXT.md)，先核基线和匹配输入，再做安装/数据识别/权限/机型画像与实机矩阵，不覆盖此TAG/备份。

## 发布后启动补充记录

备份后首次可选复启，设备电源实读 `mWakefulness=Asleep`；60秒内未得到有效platform window，随后guest signal11、offset0x1455f34。原始日志和电源快照已保留本地备份目录。仅发送唤醒键224后重启同一APK，PID18388、1080条真实present记录，target30/probe disabled/paced transport/NONE10均实读，无fault或surface失败，并核实标题画面。没有改代码或移动TAG；不是完整启动生命周期修复。后续安装包必须覆盖熄屏/Surface就绪与错误恢复，不能把唤醒后成功覆盖掉这次失败。

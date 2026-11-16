# 荣耀400 Pro神殿GPU等待退出与新旧数据统一APK

用户提供荣耀400 Pro、1.0.30、无DLC数据下土之神殿走动退出的三份实际日志，并要求发布兼容新旧数据、有无DLC的一份APK。手机目前无法接入。当前仅荣耀平板在线，可做相同APK的新旧数据组合自测，手机修复确认须由用户复测。

三份日志由用户明确提供的微信文件路径只读获取。guest_boot.log为11965795B/SHA5e30cdb7e60c63ed28fa9443611b94ab58bc2496e968a3cecba6eba68ffcbc5a，engine.log为9448054B/SHA07ae840fccae9e6a80eff60c4c69c9c0d67dbdd6d0d4a3ce373235a3d3706cdb，engine.log.fatal为105B/SHA412c1d8964d18eded8038e05eb64cd6e12a0952a50aea3a40e18c0b09d573389。撤回初读图形日志头部PID26198代表本次退出的表述。engine.log实际追加了7次运行，最新段从61860行起为PID25691，与guest_boot.log的唯一CODEX_FRAME行匹配，Adreno750、driver0x802fa031、客机BuildID736837。以下只使用该末段归因。错误为readback_sync内hybrid_wait_fence(owner->fence,至少10秒)返回2即VK_TIMEOUT。错误发生前约17939呈现帧，末窗口19.59FPS，纹理live753.8MB。没有对应系统退出报告和整机内存快照，不能认定为OOM。

发现确定的可达时序缺口。readback_sync先按pending快照决定需要等待，然后retire_completed_slots会清理包括owner在内的已完成提交。retire_slot将栅栏reset且pending置false。随后代码仍无条件等待owner栅栏，已经reset且无后续提交的栅栏会一直等到超时。这与用户FATAL位置一致，需以生产函数抽取测试先复现旧实现，再修复清理后的owner状态复核。实际手机是否仅此原因仍须复测。

计划输出1.0.60统一APK，保留59已核验的旧736837和新b7f965支持以及按真实DLC目录枚举机制。覆盖新旧数据、有无DLC的实际可用组合，原数据不必为APK升级重新下载。修复、回归、源SHA、构建ID和最终附件校验均记录在.local/honor400-fence60-20261116/。根长期帧率目标继续保持暂停。

Git管理路径在仓库移动后仍指向不存在的02-游戏原生移植目录，使用当前实际主仓库下同一worktree管理目录显式GIT_DIR/GIT_WORK_TREE进行本任务读写，避免改动其它worktree元数据。起点127ca20，分支task/T-codex-recovery-20261114，工作区初始干净。

## 同步时序回归与60构建

生产readback_sync抽取测试在原代码上以返回9稳定复现reset fence timeout，修复后6种场景通过ASan/UBSan。另自动移除修复guard的负对照仍返回9，证明契约能检出原缺口。保留真实pending提交的栅栏等待与10秒超时，只在清理已退休owner后返回已完成状态。未增加等待时间或跳过实际未完成GPU任务。

完整Android构建及宿主检查退出0，所有src/scripts/tests源码输入全文SHA构建前后相同。APK为FFRS-1.0.60-preview-universal.apk，SHAa9dafd07821d93d46783c582c1e6903b2284c75ed64f24997d71c507a49e7d9d。此前临时build59、FFRS主模块本机副本等目录已不在原路径，本轮从已安装59 APK只读恢复现有assets，再从平板原FFRS只读获取main100.nso作宿主检查。SaveKey内容只做本地相等核对，没有写入报告。新APK沿同一包名、签名与资源，兼容旧736837和新b7f965主模块，DLC仍由实际目录扫描。

构建、原日志复制、输入SHA、执行命令/环境/PID/普通日志均在.local/honor400-fence60-20261116/。旧全局Git路径失效时仅在宿主验证子进程提供显式GIT_DIR，Android构建沿实际第三方工具仓库取许可证，未修改Git元数据。当前已开始覆盖安装及9存档保全校验，计划先以原FFRS无DLC数据启动，再以原独立stage的1.0.1+DLC启动同一个APK。手机本身修复确认仍为NOT_RUN。

## 通用APK交付检查点

构建源码提交d2a01e8，native BuildID为bd811c1da6c9f0aa19377b72157333b07917ef7a。638份src/scripts/tests输入构建前后全文SHA一致，107项宿主检查与Android构建退出0。6项可选真实shader fixture因原临时输入缺失标记SKIPPED，未计入107项通过。抽取生产函数的栅栏生命周期回归、渲染策略及AOC契约均在通过范围。独立QA为NOT_RUN。

荣耀A42BBB5109000333覆盖安装后回读APK全文SHA与交付文件一致。安装前后9份私有测试存档逐文件SHA相同。正式codex-local-save目录文件元数据在安装前、安装后及两组测试结束后相同。全部运行使用显式私有存档副本。

| 同一APK的验证组合 | 进程与输入 | 实际结果 |
| --- | --- | --- |
| 游戏1.0.0，无DLC | PID11406，/sdcard/FFRS，main736837 | 标题1.0.0.0、DLC枚举0，继续游戏读取旧自动存档，关闭教程后进入神殿门前世界地图 |
| 游戏1.0.1，含DLC | PID12251，实体game101-stage-20261116，main b7f965 | 标题1.0.1.0，DLC枚举1、索引2只读挂载aoc0成功，继续游戏读取同源旧自动存档进入相同世界地图，render-policy对象链校验通过 |

两组最后日志均无FATAL，截图、case plan及同PID原日志分别保存在old100-no-dlc/与new101-dlc/。测试只覆盖所列启动、读档及DLC识别挂载。荣耀400 Pro本机、长时间土之神殿走动、技能、全章节、DLC奖励领取和稳定30帧为NOT_RUN或未达标。用户提供的手机末段日志没有系统低内存退出证据，本轮不将其归为OOM。

安装脚本首次在am start后立即pidof返回空而提前结束，之后进程10570正常启动。复用曾由1.0.1写入选项的461测试副本启动1.0.0时出现选项读取提示，跨游戏版本的选项差异为当前推测。随后两组各自用正式源文件新建私有测试副本，正常完成启动读档且未再出现该提示。失败记录和原私有副本备份均保留，正式文件未重置。

交付FFRS-1.0.60-preview-universal.apk，3728599B，SHA256 a9dafd07821d93d46783c582c1e6903b2284c75ed64f24997d71c507a49e7d9d。用户只需统一发布此APK，旧无DLC数据用户直接覆盖安装即可，新升级数据按既有覆盖说明导入。说明文件为coordination/docs/RELEASE60-UNIVERSAL-20261116.md，副本在本地RELEASE.md。此前9.1GB覆盖ZIP当前不在原临时路径，本轮没有重新生成或声称更新ZIP。若外部已保留的ZIP带59 APK，发布安装入口应以60为准。

本轮源码与清理后的证据索引本地提交，APK及游戏派生产物只在.local/。当前平板仍为新数据测试进程12251，输入已抬起，正式游戏数据没有覆盖，长期性能目标保持用户要求的暂停状态。

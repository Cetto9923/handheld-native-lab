# P-INSTALL-ONEFOLDER-20261030

角色：Codex /root，唯一协调与Android写者，唯一RG477M操作员；不虚构独立QA或并行执行。

目标：保存冻结基线，交付同目录APK、匹配的合法本地数据包及简单安装/恢复入口，实现可见的导入/版本/空间/权限错误、可靠启动与设备适配。用户已确认Android本机完成安装与导入，无电脑/ADB/root作为最终用户条件。完整要求见 docs/PACKAGING-NEXT.md，不能以脚本生成成功替代端到端安装/真实游戏验收。

输入：原工作树task/T02-codex-black-screen @572b0d5；TAG rg477m-baseline-2026.10.30 @d66706e；.local/baselines/rg477m-baseline-2026.10.30/ 的SHA已核备份；用户本地游戏输入；Android官方Surface/SAF/Zip资料，见OSS-NOTES。

输出：scripts/distribution/、Android导入/启动代码、设备兼容矩阵、.local/installer-20261030/RESULT.md、版本化本地交付目录及原始验收证据。游戏数据/生成模块/APK/存档/密钥均不入Git或GitHub。

依赖：基线和写入占用核对已完成；实现可启动，完整验收仍以全常设音频/视觉/数据门为前置，旧未通过项不能继承PASS。

写入范围：state中该任务paths为准。沿用唯一现有worktree，不改TAG或备份、不碰原主包/原隔离包和存档、不操作荣耀/平板、不提交untracked荣耀工单。新候选使用独立包ID，未授权其他设备不操作。

第一单元：guest启动须满足resumed/focused/interactive/有效Surface；destroy时复位标记；原始Intent与三候选保留。验证先熄屏启动等待≥65秒不启动guest/不崩溃，再唤醒进入真正标题；正常冷启与缺数据setup，原始失败日志保留。

验收：安装/导入/升级/回滚，匹配的版本/长度/SHA，失败保留旧版本/存档并可恢复；RG477M冷启/Continue/运动/剧情/战斗，完整引用并执行_STANDING-audio-acceptance.md（NONE实际枚举10、缓冲、TIMEOUT、DISCONNECTED、A2DP耳听、fence0、包+SHA），无黑屏/异常UI/建筑特效回退/RG16FATAL。内部渲染和输出分辨率分开记录。其他机型无实测只标未验证，不声称通用/完整可玩。

恢复点：原TAG与完整备份始终不动；候选包独立数据，不覆盖基线包。只回退本任务具体变更，禁止reset/clean/stash。长进程先记录命令/cwd/日志再补PID/session；每个原子修改/实际构建测试后更新state/HANDOFF并生成STATUS。

目标工具：用户已在界面清除旧目标；本轮get_goal=null后create_goal一次成功，持久目标ACTIVE。旧阻塞记录作为历史保留，不再作为当前状态。

最新用户调整（2026-10-30）：优先已解压目录，一次拷贝到手机或SD后APK自动查找、直接读取；ZIP导入为备选，不强制二次复制。公开成熟路径/权限做法记录OSS-NOTES（Xash3D3daa67、EasyRPG096c004、ScummVM/Android官方）。固定候选有界查找，首次文件授权如系统要求；不承诺无权限遍历任意目录。完整原有音频/视觉/30Hz底线仍需实测。

# Dots 接续与并行修复检查点

## 当前目标

2026-11-18 用户要求接续受额度限制的 Dots，并明确要求启动多个子代理并行修复。已直接读取蒂法当前对话，最高优先级是两款游戏的多设备适配。FF 天玑 9400 闪退、第四章副本战斗低帧优先，游戏库与《时之笛》功能接入继续推进。

本轮在当前聊天协调。此前关于“开三个独立会话”的表述有误，实际启动的是本聊天的三个子代理。

## 已定位的工程

| 工作 | 实际位置 | 接续依据 |
| --- | --- | --- |
| FF 多设备兼容与性能 | `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro` | `coordination/results/DOT-ARSENAL-20261118.md` 与当前未提交生命周期候选 |
| 游戏库 APP | `/Users/yuyan9923/GitHub/02-游戏原生移植/switch-android-port-rules` | `.local/COORDINATOR-LATEST-20261118.txt`，099 平板 UI 已做有限验证，100 安全基线，薄运行时链路待继续 |
| 《时之笛》 | `/Users/yuyan9923/GitHub/02-游戏原生移植/oot-port` | `.local/main-host-adapter-v2/`，内部 T30 包标题与 CG 通过，主库下载和启动待闭环 |
| 《空之轨迹》 | `/Users/yuyan9923/GitHub/snk-the1st-portable/.local/landscape-fix-20261116` | 当前 state 的 T92 实时金手指有限验收，更多效果及跨机型兼容仍有开放项 |

## FF 输入与保全

- 分支为 `task/T-codex-recovery-20261114`，HEAD 为 `273c240155f95db92e2f08050b61164aec401f29`。
- 接手时已有生命周期源码和测试 WIP，另有移植经验指南的无关 WIP。本轮保全这些内容。
- 15 个集成文件与 `.local/arsenal-lifetime-followup/proposal/BASELINES.json` 的 proposal SHA256 逐一一致。索引为 `.local/codex-dots-takeover-20261118/source-identity.json`。
- 内部候选 APK 为 `975552550821f64b1c1b145dd4e94d92a559929f977ea67964792abea5b1bb1f`，3,798,300 字节。原有 Android 构建退出码为 0。本轮校验实际 APK、签名及安装回读。
- 原 1.0.73 APK 为 `7f3e71552aee32c3b0bbb2cd765d50f6be3960d7ae0af511ba23a660210184c4`，候选测试结束后覆盖恢复并回读一致。

## 宿主验证

首次执行 `python3 scripts/verify_host.py --output .local/codex-dots-takeover-20261118/host`，98/99 检查通过，退出码 1。唯一未执行项为默认 `.local/uncompressed/main.bin` 缺失。

从荣耀既有 `/sdcard/FFRS/main.nso` 只读复制到本轮独立目录，并用本轮构建的 `nso_call --info` 补测，退出码 0。模块大小为 125,582,072 字节，SHA256 为 `4efa217d9bb2442405a7e88ffc69df3de1a44021d515a38cd06abf246ff1e62b`，Build ID 为 `b7f965b9d7227db8e29f04e84ea7331b`。该检查只覆盖加载和元数据。

四组定向检查均退出 0，包含实际生命周期头 ASan/UBSan、窗口/FPS 入口、序列化回调和 backend 所有权。后两组先逐字确认当前生产函数与测试提取来源一致。

完整 `--runtime-contracts` 回归已按正确模块路径另行启动，日志为 `.local/codex-dots-takeover-20261118/full-host.log`，工具 session 为 21396。结果待追加。

## 荣耀有限实机回归

设备为 `A42BBB5109000333`，ADB 使用端口 5038。首次查询默认端口为空导致连接判断错误，随后发现 5038 下两台设备均在线，已更正记录。

本轮只操作荣耀。测试前核实没有运行中的 FF 或 OOT 游戏进程。以原隔离档 `shield-regression-20261118094001` 建立新副本 `shield-regression-20261118174201`，候选实际安装 SHA256 与本地一致。

- PID 7401 成功进入第四章兵器工厂，画面、人物及任务信息可见。
- 三次向右长按输入后，人物离开紫色恢复圈，前后截图留存。
- Home 后重新进入同 Activity，PID 仍为 7401，场景与人物可见。
- `guest_boot.log` 中当前进程句柄明确对应 PID 7401，日志记录独立存档路径。未混用其他会话日志。
- 测试后停止候选，恢复原 1.0.73，实际已安装 SHA256 与恢复包一致。
- 测试前的 111 个 app-private 存档/设置文件及 47 个外部存档/设置文件，测试后 SHA256 均相同。只在新隔离副本发生测试写入。

覆盖范围是启动、读档、短移动和 Android 后台恢复。未验证 G925 原机、完整战斗性能、长时间运行、声音及独立 QA，未确认原始天玑 9400 闪退已修复。原正式发布文件保持原字节。

## 实际并行任务

| 任务 | 子代理 | 写入范围 |
| --- | --- | --- |
| `CODEX-PARALLEL-MALI-20261118` | `/root/mali_crash_fix` | `.local/worktrees/codex-mali-20261118` 隔离分支及自己的报告 |
| `CODEX-PARALLEL-BATTLE-20261118` | `/root/battle_performance_fix` | `.worktrees/codex-parallel-battle-20261118` 隔离分支及自己的报告 |
| `CODEX-PARALLEL-LIBRARY-20261118` | `/root/library_oot_fix` | KIT 的 `.local/codex-library-oot-20261118/` 独立候选及自己的报告 |

root 负责 state、HANDOFF、串行集成、设备调度和统一资源安排。三个子代理已实际启动并回报路径。完整构建与设备测试先协调，避免冲突。

## 恢复位置

本轮计划、源码快照补丁、命令、退出码、截图、存档摘要、已安装 APK 摘要和原始日志集中保存在 `.local/codex-dots-takeover-20261118/`。

先检查完整宿主回归 `full-host/summary.json`，再接收三个子代理的证据和补丁。荣耀已恢复原包，后续设备窗口由 root 分配。G925 原报告与第四章实际战斗继续作为优先验证目标。

## 完整宿主回归追加

正确模块路径的 `--runtime-contracts` 回归已完成，114 项全部通过，退出码 0。摘要与各项真实命令在 `full-host/summary.json`。

当前 root 为性能代理补采真正战斗的 RHIThread 栈。候选 APK 与含调试信息 SO 的 ELF Build ID 均为 `497ce1b6b962e156c7c803f70add57f90c4447a8`，计划保存在本轮 `combat/profile-plan.json`，使用新独立存档副本。

## 并行首轮集成追加

三个实际子代理均交回首轮可复验修复。root独立复跑OOT操作槽回收的111项断言及正负对照全部符合预期，副本与日志保存在本轮library-review目录。

已串行合入FF indexed纹理注册节点缓存复用与暖layout配方基础校验，并在verify_host中接入两个对应回归。移出生命周期候选后，主组合116项宿主检查全部通过，Android构建退出0。实际源码输入摘要与构建前清单一致，候选路径为combined/FFRS-parallel-test-20261118.apk，内部版本仍为1.0.73。尚未作G925修复或性能收益结论。

独立生命周期审查确认第二窗口Initialize返回行为改变，并找到异步通知栈对象寿命竞态。该候选已从主组合移出，原源码和补丁完整保留。详情由Mali代理追加独立报告。

root已采集真实第四章火焰敌人遭遇的两组五秒RHI样本，PID9958/TID10265，BuildID497ce1b6精确匹配归档调试SO。第一组为战斗命令选择，第二组为持续A输入后敌人HP下降的攻击窗口，前后截图均在战斗。已按窗口分开交性能代理归因，未将该遭遇与先前机械敌人作FPS因果比较。

## 两项小修复统一候选荣耀回归

APK SHA256 `75ba36f1bfd152d9e82027752f6781a9d40815a23d1991e9dedd4d06e24f91a9`，源码不含已隔离生命周期候选。荣耀实际PID14056成功读独立存档进入第四章兵器工厂，触屏持续右方向后画面位置变化，Home返回仍为同PID且实际场景可见。原73已覆盖恢复且安装摘要一致，测试前119个私有和47个外部存档/设置文件摘要全部保全。证据在combined-device目录。该回归只覆盖列出流程，未量化新优化的FPS收益，G925原机与声音/完整战斗/独立真机QA继续待验。

## 2026-11-18 增量边界候选实测收口

独立候选APK `ad04cd4df33228ac0ef9359218cc086e5244e8d2b13591016e2d881da538bd41` 已在荣耀PID20161测试。根主源码在安装前已逐文件SHA核验恢复eb6b7ad，实验源码留独立worktree。

本轮keyevent往返未证实有效位移，随后触屏持续方向确认实际移动到兵器工厂走道。未进入战斗，故采样明确为静止探索窗口。bboxready关/开/关/开各8秒，实际不同渲染帧分别19.5546/19.0235/20.2109/19.4491 FPS，p95分别58.3532/59.0844/53.9656/57.0470ms。窗口含全部帧，未剔除长帧。两次开启均未显示收益，候选暂不集成，不能用该结果宣称战斗优化或作严格整包前后比较。原始窗口时间、CSV、截图、温控与LIVE-SET确认在bounds-device目录。

测试后gfx-live-set等既有标记按原字节恢复，原73覆盖安装摘要回读一致。123个原私有存档/设置文件和47个外部文件SHA逐项保全。本轮新隔离存档留作证据。设备交图标流程。

## 游戏库图标实际替换

沿用Dots已准备的Cetto深蓝底青绿C标。安全100原APK `697e323d` binary manifest缺少icon与roundIcon引用，根因已实证。纯图标内部包 `6ee483541961913e8bc89efb25727261f3f96ced29a6ca050ca90cd7cd96694e` 保留code84/0.10.0-fallback071。root独立验证原30个运行与封面条目逐字节一致，基础manifest仅新增两项图标引用，签名相同。

荣耀已同签名install-r并回读安装SHA一致，桌面第4页实际显示新Cetto标，包标签为Cetto移植库。安装前后外部文件摘要核对通过。该包非debuggable，run-as私有文件摘要不可用，未更改debuggable设置，私有逐字节校验为NOT_RUN。设备证据在icon-review目录。后续code87功能候选继续保留图标接线。

## 诊断包真实导出补验

诊断源码459b6ad已串行接入，本轮内部APK为 `c1fdfb06f5747ccf5693195b35cf13f6a9c3651527d6e31cf68cd80d3b52f0ef`，code73，versionName为1.0.73-mali-diag。使用现有构建flag1，默认源码配置继续为0。首次完整宿主116/117通过，新增诊断回调导致已有descriptor提取夹具缺声明，root补有状态计数回调并保留原负对照后，第二次完整117/117通过。首次日志保留。生产源码摘要与Android构建前清单一致。

荣耀PID25897实际进入第四章，目标模块FNV为dcd609ecefa41335、9023words，真实descriptor六项完整记录，binding35 range32及dynamic offset已捕获，dispatch为9,5,1。API日志19行，包含同PID、TID及每行monotonic，五组begin/end均配对。用户正常游戏菜单→导出诊断报告→SAF保存→成功提示实际执行，导出10文件476371字节，ZIP SHA为a5d41a9e42ad272b6b7be1467d7548e13fdfe83a5c2e452e81f13c972a3e402d。ZIP内api-breadcrumbs.log与当次目标字段核对通过。设备恢复原73，原私有/外部档摘要按diagnostic-device/device.json核验。G925原机根因与修复验收仍开放，荣耀流程不替代原机。

## 图标正常入口补验

root根据桌面UI观察到的Cetto图标位置点击，实际进入CETTO我的游戏页，两游戏卡片及开始游戏可见。图标任务完成实际替换与入口验收。安全100纯图标运行代码与原100逐字一致。私有文件hash因非debuggable未运行，安装方式为同签名覆盖，外部文件hash通过。

## 荣耀空轨主库新建卡顿专项

用户确认问题设备为荣耀，并要求起专门代理逐项查且不耽误主线。已新开snk_library_startup_fix，写KIT独立候选与自有报告。OOT代理恢复原主线，SNK T93完整构建继续独立完成。专项先比对100与T92的真实模块、DeviceProfile、帧率/分辨率、启动路径及同会话日志。尚未把SO不同或新增presentation timing查询直接归因于卡顿。

诊断集成正式检查点为commit7109848。源代码输入摘要与已验证Android构建一致，完整宿主第二次117/117和独立QA通过。当前另两主线为OOT产品接线、在线更新恢复，新卡顿由专责代理推进。

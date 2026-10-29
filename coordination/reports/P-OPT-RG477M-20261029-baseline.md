# RG477M 当前成果基线（用户授权提交）

2026-10-29 Asia/Shanghai。分支 `task/T02-codex-black-screen`；父提交 `0444c81`；现有脏工作已按用户明确指令认领。此次保存现状，**不是完整验收通过或可玩声明**。不 push，不另开 worktree，不改主树、原始游戏或用户存档。

## 保存范围与复现入口

保存当前运行时/NVN→Vulkan/Android 音频与输入、数据引导、相关脚本与测试、协调记录及应用小型图标。包括接手前尚未提交的 WIP，并非全部为本轮新实现。不提交 APK/游戏容器/原始模块/密钥/存档/大型原始日志；本地产物在 `.local/`。

- 当前证据：`.local/rg477m-opt-30hz-game-mode/RESULT.md`，原始 CSV、截图、guest 日志同目录。
- 备份位置由 `.local/rg477m-opt-30hz-game-mode/baseline-backup-path.txt` 记录，含源码归档/manifest、任务证据归档和本次提交 Git bundle；提交 SHA 在备份 manifest 中保存。
- 原主包 `org.ffrs.gamewindow` APK SHA256 `a1c82634ed2636a3b20c3f98fb92a941988d4cfe212add84b6f610380a313aaf`，本轮未替换。五个存档/配置与接手备份逐字节一致，另存独立有效备份和 SHA manifest。
- 原隔离包 `org.ffrs.perfisolated` SHA256 `5683f7e725978d0750899a2b6d3c392a607fdf756275ac1e308dd06fca4612d9` 已保存，可恢复。
- 当前隔离候选 APK SHA256 `723f82f93eb24a319e31237ced21b203533683e1da0408d03854414c0f2f7b32`。本轮 Java 修改：target30 请求屏幕60Hz；target>30/0 请求同物理分辨率最高可用刷新率，下一次 Activity 启动生效。原生音频库未改。不能把屏幕120Hz当游戏120fps。

## 实测与限制

只操作 `RG477M01445122`。系统快捷面板截图明确“游戏/开启”，同窗 cpu_mode=2；未改系统档。用户随后明确允许40、60及不限帧对照，按实际玩家流畅度选择；原始工单禁抬帧条款已被该后续授权替代，其他硬门不减。

| 范围 | 结果 | 限制 |
|---|---|---|
| 旧隔离包30、CG134秒 | 29.9835 present/s，p95 36.270ms，max46.238ms | 后续流程signal11退出，整局稳定性FAIL |
| 同旧隔离包40、CG124秒 | 30.9298 present/s，p95 37.426ms，max47.529ms | 未达到40 |
| 当前主包native隔离40、CG尾115秒 | 25.5295 present/s，max52.854ms | 供电、温度、运行模式与旧包不匹配，非单变量因果证据 |
| 候选30、实际display60、CG95.7秒 | 25.6961 present/s，p95 43.585ms，max51.158ms；>50ms三次 | 用户确认蓝牙声音连续但仍有细微卡顿；未达到30 |
| 数据缺失Intent | setup屏明确显示未找到数据，无静默挂死 | 自动原始RG目录与五个Paks另已真机确认 |
| Java/d8/签名 | 全部exit0 | 非整树重建或独立QA |
| 帧指标测试 | 六项通过，含稀有200ms停顿在p99良好时仍保留 | 合成指标测试不替代真机验收 |

全启动/转场所有长帧仍在 CSV 与 `*-full-stalls.json`；限定 CG 另有明确范围，不能删去卡顿后冒充完整流程。用户反馈整体比之前流畅、完整流程约4–5次细微卡顿，尚未建立每次感知与日志事件的一一对应。

## 待验收与恢复点

- 验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`，蓝牙野外/战斗各≥2分钟主观连续和运行中DISCONNECTED重开流仍未完成；NONE、burst缓冲、TIMEOUT重试、fence0保留；xruns不能替代人耳。
- 常设文案枚举有冲突：AOSP/本地NDK实际NONE=10、LOW_LATENCY=12，当前performance10符合NONE；不伪造performance0，不改为错误参数。出处见本报告旁OSS-NOTES。
- 野外建筑/战斗特效、无UI遮盖/异常叠层、无新全黑/中央黑块、RG16不复发、游戏速度、720P内部渲染及独立QA均未完整通过。
- 旧30后段signal11定位到匹配BuildID库的C++ demangle解析链，未确定根因或修复。
- 最新发现：旧隔离日志GetOperationMode=0(Handheld)，当前native=1(Docked)，当前CG阶段present source1920x1080再缩放至1280x720；源码当前默认1且只读主包marker。尚未做匹配模式对照，不能把两包差异仅归于FIFO/刷新率。冻结后优先修正本包模式解析，再复测；本基准不顺手改动。
- 供电仍仅电脑USB500mA，运行4%降到3%，游戏已停止、屏幕已关闭充电；用户确认暂时只能电脑USB。60/不限帧完整对照NOT_RUN，不宣称性能PASS。

恢复优先从本基准提交继续，不 reset/clean/stash 掉后续 WIP。APK回滚只安装已保存隔离APK，禁止卸载主存档包；音频一坏回30+fence0+已知顺滑包。设备当前隔离target30，fence0，系统游戏档未改。

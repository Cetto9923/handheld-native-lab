# 接续提词｜P-CX-TERRA-HONOR-render-correctness-resume

写手：Codex。目标：在荣耀上修复真实画面、音频和性能，最终达到 1080P 稳定约 60Hz；不能影响已经适配的掌机。当前持续目标由用户暂停；此文件是接续任务卡，实际执行须在用户恢复目标后开始。用户已授权直接操作游戏移动，不必再逐次询问；若设备断连，明确报告连接问题。

## 1. 边界与真源

- 工作树 ONLY：`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`；设备 ONLY：`adb -s A42BBB5109000333`。
- 禁 `adb kill-server`，禁操作 RG477M01445122，禁写/清理 `ff-resonance-rg477m-native`，禁双写或认领 `.worktrees/codex-black-screen`，禁 push、reset/clean/stash 覆盖 WIP。
- 根 AGENTS.md 是唯一工程规范，先完整阅读。产品真源 `coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md`；当前目标修订 `coordination/prompts/P-CX-TERRA-CONTINUOUS-GOAL-honor-20261101.md`。旧任务卡的荣耀30目标已被用户修订为1080P/60；30仅是音频失败后的临时对照，不能把终点改成30。
- 顺序仍是 device-profiles → RESULT → 荣耀1080P/60及音频 → RESULT。现有结果仍 INCOMPLETE，不得补写 PASS。先修正确性/恢复音频，再冲60；40不插队。
- 站在现有 RG477M 可玩代码上扩展；先 OSS-NOTES，记录 URL、不可变 commit、许可证和适用范围。无出处大改=FAIL。荣耀变化必须有明确配置/GPU门控，保留 handheld_mid 默认与已有覆写优先级；主机测试不等于掌机实机回归。禁止为了通过测试隐藏火焰、跳过绘制或移除必要同步。
- 沿用已 PASS 的 P-AGY-INSTALL-autodiscover；不重做安装器，冷启动必须继续自动 resolveGameDir。

## 2. 已知资产与存档

数据包只读真源：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/release-pack-20261028/FFRS/`；荣耀目标 `/storage/emulated/0/FFRS/`。不得再问用户数据包路径。荣耀 `.local/romfs` 为只读 symlink。

73,496 个 loose shader 文件的名称及 SHA 已逐一源/设备比对一致；archive 6,089,256,224字节，SHA256 `529aede4fc1a1e3bb165757e5011332fb4d4088553587ecb76772013221c0ed8`。缺件问题已处理，不重复推16GB整包。

应用 `org.ffrs.gamewindow`，Activity `org.ffrs.diagnostic.GameActivity`。自动存档 `files/codex-local-save/GameSaveData0` 最后观察304144字节，Continue 已真实读入。shell/run-as读取存档均 Permission denied；之前tar备份无效，不能称已有备份。只能保留数据覆盖安装 `install -r`，不能清数据、卸载或替换存档。

## 3. 最近构建与运行状态（恢复时重查）

- 分支 `work/honor-gtpro-30hz`；HEAD `032e5f68172c101cb2b6e44ee06f02974dab2da8` 加大量既有 installer/profile WIP；不要切换或清理。branch 名不代表当前60目标变化。
- 最近安装包 `.local/honor-device-profiles/runtime-inline-proof/profile60-inline-loglimit.apk`，SHA256 `3bd9e07e00b1a2af996646959c9c8a47a25d7a83cf4613e69be9fe6903ae7eab`，安装后设备SHA一致；最后进程 PID10866，尚未核实暂停后是否仍在运行。
- 该包包含可选 SPIRV-Tools runtime inline及荣耀重复日志限流。构建命令 `runtime-inline-proof/loglimit-build-command.txt`。仅tablet profile+Qualcomm+精确Adreno735启用inline；默认无依赖构建不含优化器。上游MIT d2r-mobile `f1e4b081af8ea65d95f0bf490a568cb657c3917e`，Khronos Apache-2.0/headers许可证已随APK打包。
- 我方 `files/gfx-target-fps=30` 当前为声音失败后的保护性对照，默认profile仍60。`gfx-codex-frame-metrics=1`、`gfx-shader-archive=1` 是本任务创建的标记。
- 1725个旧两pass shader候选完整保存在 `files/spv-override-batch-preserved-20261101-runtime-ab`；六个早期候选在 `spv-override-six-backup`。活动 `spv-override` 不存在。新三pass与旧两pass有1489个输出不同，不能当语义/画面等价。
- 上一进程临时累计关闭过 imgcache/xmemo/descmemo/bindcache，黑画面均未恢复；这些开关在重启后复位。标记已移为 `gfx-live-off-used-*`，没有活动 gfx-live-off。
- 抓帧标记已移到 `gfx-codex-probe-used-27508` 和 `gfx-codex-probe-used-10866`；没有活动 gfx-codex-probe。既有 gfx-pass-live 三帧采样已被程序消费。不要误以为诊断仍在采集。

## 4. 实机失败与关键新证据

用户原话：启动界面正常有声音；开局动画声音和画面都很卡；降到30依然“很卡”；特效异常、火焰时有时无；用户确认主体大面积黑色而小地图仍显示。声音/画面/性能均未过关。

- 原始开场后验30秒窗口：267次队列成功/29.9559秒，约8.88Hz，p95 147.33ms，p99 155.98ms，187个间隔>100ms。只是队列回调，不是屏幕扫描帧率。30后仍约7–10次/秒。
- 1920×1080是观察到的最终present源；GPU采样中有1384×780、单帧抓取中有1037×584等内部viewport。不能把最终输出当成完整内部1080P验收！需明确动态内部渲染、输出和UI三者。
- 3帧GPASS（22713–22715）各12slot/318query，无跳过；重绘制约4.2–4.8ms，compute `073ce354a4f65e2f` 约3.24–3.29ms。总帧瓶颈未定，不能删除阶段换帧率。
- 日志限流把大量纹理回收诊断降下来，但同场景仍黑、约7–8次/秒；没有证明性能改善，不能包装成修复。真实回收调用仍逐次执行，handheld日志路径保留。

**最重要的新线索：读回图与实际显示不一致。**

A. 全过程probe：PID27508 frame7667，221事件、61,384,180字节，1133文件已pull。`codex-probe-27508/capture.txt` 有END。group209→212及最终图采样能见街道、角色和火焰；普通运行截图抓帧前后均黑。

B. normal-recording/final-only probe：PID10866 frame10174，`FINAL_ONLY normalFrameRecording=1`，仅最终图readback，36864字节。此模式不关闭正常绘制快速路径，最终图采样也能见街道、角色和火焰，但有局部异常；物理画面此前/普通运行仍黑。

可视对比 `.local/honor-device-profiles/runtime-inline-proof/probe-mode-comparison.png`，原始 `.bin` 与元数据在两个 `codex-probe-*`。这是两个不同进程/帧，且readback本身增加同步，不能当严格同帧对照或断言“缓存就是原因”。下一步优先追踪最终源图、同步/生命周期、crop/scissor/viewport状态、swapchain拷贝及合成结果之间的差异。不要只盯shader优化器，也不要直接加全局waitIdle作为正式修复。

证据根目录：`.local/honor-device-profiles/runtime-inline-proof/`。含 `honor-run11288-before-loglimit-install.*`、`honor-run23570-before-probe.*`、`honor-run27508-fullprobe.log`、黑屏截图、`passlive-summary.json`。PID10866的完整guest log若还在设备，应在任何重启前先保存；恢复时检查现态，不能按旧PID盲操作。

## 5. 精确接续步骤

1. 完整阅读AGENTS、state/HANDOFF及本卡；核对分支/HEAD/dirty、现有进程与写入占用。只在本树登记实际写入范围。用户恢复目标前只整理资料。
2. 恢复后仅 `adb -s A42BBB5109000333 get-state`，检查包路径/SHA、pid、前台、标记、存档元数据。保留当前日志/CSV后再决定重启；不要删缓存或存档以求“干净”。
3. 先复用本轮两个probe与源码，建立最终present源→Vulkan拷贝→swapchain→显示的证据链。若需再抓帧，现有 `gfx-codex-probe` 需启动前0待命：进入同一场景后1为全过程，4为normal-recording/final-only；每进程一次，产物有END后pull并归档标记。严格区分采样改时序的效应，避免反复重跑长开场；使用Continue。
4. 先查对应OSS/规范写OSS-NOTES，再做最小、荣耀门控的修复。记录单变量、相同位置、正常帧与诊断帧；不要继续堆累计关闭缓存以猜原因。
5. 修复后先30对照恢复完整画面及外放连续性，再测60；不把30当交付目标。复核火焰持续、透明混合/遮挡/角色/UI、移动/战斗/过场正常速度。不得把读回小图正常替代物理画面正常。
6. 每次构建/安装/测试保存命令、版本、SHA、真实退出码和日志；更新RESULT/HANDOFF/state，运行 `python3 scripts/project_status.py --write`。没有独立QA能力如实NOT_RUN。

## 6. 音频与最终交付

验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。用户明确免除蓝牙，本次外放验收；不要再要求耳机。NONE、burst缓冲、TIMEOUT重试、断连重开及fence_spin=0救回点不能回退。注意NDK实际NONE=10/LOW_LATENCY=12，站立文档旧数字存在错误，不能按错误数字改实现。

用户耳听断续即FAIL，xruns少不能反驳；音频失败时保持30保护对照，恢复并复测前不抬60。外放野外/战斗各≥2分钟、重开/场景切换及音画速度均须明确记录；蓝牙项标USER_WAIVED。当前这部分未完成。

最终仍需device-profiles RESULT与荣耀1080P稳定约60Hz+正确画面+外放RESULT；未达到就写清差距并继续目标，不能只交标题/构建/限帧配置。达到后停止，不转做40。全文交付不宣称掌机实机回归通过；本轮未操作掌机。

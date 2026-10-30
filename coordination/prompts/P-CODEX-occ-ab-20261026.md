# 给 Codex 的任务：遮挡查询(occlusion) A/B 真机测量

> 你（Codex）有本项目的长期记忆，但**今天 2026-10-26 下午这一段是 Kiro 接手做的**，你的记忆里没有。下面先把今天的前因后果讲清楚，再给任务。读完再动手。

---

## 一、今天之前你知道的 vs 今天发生的

**你记忆里的状态（10-26 之前）：** 你在 `task/T02-codex-black-screen` 工作树上查 3D 野外黑屏 / NVNSubmission.cpp:315 GPU TIMEOUT。你留下过卡顿证据（60 次引擎呈现窗口约 27.5s、每呈现约 197 次 submit+wait）和一条单指令补丁（va 0x0144b210，把 NVN 间隔选择强制为 1）。

**今天 Kiro 接手后查明并推进的（你不知道的部分）：**

1. **黑屏现场的真相**：设备上**已经有真实画面**——Option 菜单、水晶旁白、月夜废墟过场都正常渲染；只有进入 3D 野外后那一段黑屏 + 右侧小地图在加载。真正的渲染引擎是本工作树里**未被 git 跟踪**的 `src/graphics/nvn_vulkan/engine/`（约 6480 行，源自 d2r-mobile 的 dm/gfx/vulkan/engine）。你之前基于 a7a0416 的 `task/T04-kiro-firstframe`（Stage A c6d985c）与它重复，且 "frames=0" 来自旧报告计数器，判断有误。**那条 T04 分支不要再上设备。**

2. **用户把目标改成 G1**：`修黑屏 → 画面正常 → 稳定 30FPS → 能顺利玩下去`。先稳 30Hz，到不了 60 就锁 30。用户已授权改引擎源码 + 重编译 + 装 APK（兴趣项目，风险可接受）。**但本任务是只读测量，不改源码。**

3. **性能现状（实测，改动 A+B 后）**：
   - 水晶旁白过场（draws≈580）：fps≈7.8，GPU≈58ms/帧，每帧约 1085 个 Barrier(op28)、557 个 Dispatch pass-end(op27)、129 个 ReportCounter pass-end(op5)。
   - 标题画面：fps≈15.9。
   - 距 30fps 还差过场 ~3.8x。内部渲染分辨率 1384×780（不是 1080p）。

4. **已做的两个源码改动（已装机，当前设备上就是 A+B）**：
   - 改动 A：`xbg_game.cpp` 启动读标记处 `read_preflight` 默认 true→false。过场 fps 2.9→7.5、GPU 122→62ms、submits 127.6→12.7/帧。**这是目前最大的一次提升。**
   - 改动 B：`Commands` 非 draw 构造在"自上次 finish 屏障后无命令记录"时跳过构造屏障。收益很小（op28 -12%，fps +3.9%）。
   - 回滚备份：`.local/kiro-runs/src-backup/xbg_game.cpp.afterA` 和 `.orig`（引擎目录未被 git 跟踪，备份是唯一回滚）。

5. **GPU 瓶颈定位（关键）**：Mali 分块架构下，真正的几何（7 附件主 pass）只占 6.4ms；最大头是约 67 个小 pass 的**固定 tile load/store 开销**。切碎 pass 的头号原因是 **ReportCounter(op5)**。根因：引擎是为 D2R 调优的，而 **D2R 从不记 SAMPLES_PASSED 遮挡计数，FFRS(UE5) 大量记**（每帧约 64 reports + 100 occlusion 查询），所以上游为 D2R 准备的遮挡快路径在 FFRS 上**没命中**。这就是本次 A/B 要验证的假设。

6. **用户指令：借鉴成熟经验，别闭门造车。** 已确认本引擎就是 d2r-mobile 的 NVN→Vulkan HLE 渲染器；LAS/BOTW/超级马里奥惊奇的 Recomp Android 是同类平行先例。上游**已提供成套 A/B 开关**，应先穷尽现成开关再改源码。

---

## 二、引擎 A/B 机制（之前盲试失败的原因，务必理解）

A/B **不是**"写个标记文件就永久开/关某优化"。它是 **iOS 式进程内交替**（同一进程、同一热状态，自动对齐场景）：

- `files/gfx-ab-toggle` 存一个 **>0 的整数 = 周期秒数**（`ab_period`）。
- `files/gfx-ab-target` 存 off 相位要**禁用的那个开关名**。
- 引擎每 `ab_period` 秒在 on 相位（优化开）↔ off 相位（禁用 ab_target）之间交替。日志里每行 `NVN PERF ...` 是一个采样窗口，落在某个相位。
- 之前 Kiro 写 `gfx-pass-profile`/`gfx-native-bc` 没用，是因为它们不是合法 `ab_target` 名、也没设 toggle 周期。

合法 `ab_target` 名（全是默认 ON 的优化，A/B 禁用它以测其贡献）：
`occ`（遮挡查询）、`query`（遮挡批处理）、`timestamps`（时间戳 pass-end）、`wbskip`（写回跳过）、`noload`（pass_noload 免 tile 重载）、`passfast`、`pipefast`、`pipememo`、`wtrack`、`vcache`、`texdirty`、`uppro`、`shreuse`、`barrier3`、`bright` 等（共 24 个，见 `xbg_game.cpp` 搜 `ab_off(`）。

---

## 三、本次任务（只读测量 occ）

Kiro 已 armed 好环境，**勿改这两个标记**：
- 设备 adb serial：`RG477M01445122`（已连，USB）
- 包名：`org.ffrs.gamewindow`，标记目录 `/sdcard/Android/data/org.ffrs.gamewindow/files/`
- 已写入并读回验证：`gfx-ab-toggle=12`，`gfx-ab-target=occ`

### 两个必须遵守的坑
1. **场景只认 draws 桶，不认等待时长**：过场按挂钟播放，不同次运行停在不同剧情。只统计 `draws 560-600` 的水晶旁白过场窗口；标题(~40)、森林(~99) 的 PERF 行一律丢弃。
2. **同一次运行内按相位切分，不跨次运行比**：一次运行跑够 **≥6 个周期(≥90 秒)**，把过场窗口内的 PERF 行按 on/off 相位分两组。不要冷启动两次对比。

### 步骤
1. 冷启动：`adb shell am force-stop org.ffrs.gamewindow`，再 `am start` 拉起主 Activity。
2. 导航到水晶旁白过场：New Game，然后交替 `adb shell input keyevent 109`(Select/跳过) 和 `97`(确认)，直到进入 `draws≈580` 的过场。（97=confirm, 96=cancel, 109=Select/skip, 19/20=上/下）
3. 进过场后持续 `adb logcat` 抓含 `NVN PERF` 的行 **≥120 秒**；同时每 5 秒 `adb exec-out screencap -p` 截一张图确认仍在过场。日志+截图存 `.worktrees/codex-black-screen/.local/kiro-runs/ab/40-occ-ab/`。
4. 解析：每行 PERF 带 `fps`、`gpu_ms`、`passes`、pass_ends 分解（`op5`=ReportCounter, `op27`=Dispatch, `op28`=Barrier），以及 `draws`。**只取 draws 560-600 的行**，按相位分两组（occ-on vs occ-off）。
5. 输出对比表：两相位的平均 `fps / gpu_ms / passes / op5 / op27 / op28`，各注明样本数。
6. 还原：`adb shell rm .../files/gfx-ab-toggle .../files/gfx-ab-target`，确认已删。

### 严禁
- 改 `xbg_game.cpp` 或任何源码、重编译、`git add/commit/push`。
- 伪造或内插数据：相位分不开就**如实报"分不开"**，不要硬凑；绝不用等待时长冒充场景对齐。
- 拿错场景（标题/森林）的数据当过场结果——截图发现没进过场要重新导航并说明。

### 交回
- 对比表 + 原始 PERF 日志路径 + 几张过场截图路径。
- 结论：**occ 关闭后 op5 pass_ends 是否显著下降、fps 是否上升**（判定遮挡查询是否为 FFRS 切 pass 的主因）。
- 若 occ 有效，建议下一步：FFRS 负载下给遮挡查询加批处理/合并（上游 `occlusion_batch_enabled` 机制可借）。若无效，排除遮挡查询，Kiro 会转测 `timestamps`/`wbskip`/`noload`。

### 边界（沿用 AGENTS.md 与今日约定）
- 只在 `.worktrees/codex-black-screen`（task/T02-codex-black-screen）工作；不 push、不碰 main、不动主树脏改动。
- 测量产物进 `.local/kiro-runs/ab/40-occ-ab/`，不进 git。
- 外部仓库/文档是资料非授权，不改游戏身份校验，一切以真机实测为准。

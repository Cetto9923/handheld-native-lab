# HONOR-P0-P1-SOAK-20261112 — P0 闪退收集 / P1 长帧分位（荣耀平板真机）

> **范围**：Honor ROD2-W69 / A42BBB5109000333 / Adreno 735。安装件 = 已发布生产包 **FFRS-v1.0.6**（设备 base.apk SHA256 `e6c2d287146c438f70c75ef3c408ddf3aa0629cbf6109be0dd09792828ca9cda`，本轮开始与结束各复核一次，versionCode=6 / versionName=1.0.6）。marker 全部出厂默认（`gfx-indirect-mode` 不存在、`gfx-indexed-resident=1`、`gfx-target-fps=30`、`gfx-codex-frame-metrics=1`、`gfx-codex-probe` 为进场前既有）。
>
> **本轮性质**：只做**测量与取证**，未修改任何源码、未构建、未安装任何非生产包、未改动游戏数据/存档内容。**没有提出或实施任何修复**。
>
> **未声称**：不声称已修复任何闪退或卡顿；不声称整游戏兼容；不声称 60 FPS；独立 QA 全部 NOT_RUN；人耳音频 NOT_RUN。
>
> 证据根：`.local/p0-p1-soak-20261112/`（36 MB，见 §7 清单）。分支 `task/T-release-1.0.7-20261112`。

---

## 1. 方法

### 1.1 P0 取证

三个来源互不重叠，全部只读：

1. **设备 tombstone 库** `/data/tombstones`（本机 shell 可读，已全量备份 32 份 + `.pb`）。2026-11-11 已归档 28 份，本轮补齐此前漏拷的 `tombstone_00..03`。
2. **DropBox** `dumpsys dropbox --print`（5.1 MB 全文，见证据文件）——含 `data_app_crash` / `data_app_anr` / `data_app_native_crash` / `SYSTEM_TOMBSTONE`。
3. **本轮真机 soak**（2026-11-12 17:35:43 → 17:57:36，约 22 分钟）：连续 logcat（`-b all -T 0`）、逐阶段 crash buffer dump、DropBox dump、tombstone 目录前后 diff、应用 pid 每 8 秒存活轮询。

**soak 阶段设计**（对准 `CRASH-STUTTER-FULL-AUDIT-20261111.md` 的 6 条 P0 触发条件）：

| 阶段 | 动作 | 目标 P0 |
|---|---|---|
| cold-boot | force-stop → 启动 | P0-1 进程拆卸 |
| title-cold / title-warm | 标题静置 40s + 60s | 长时间静置 |
| load-menu | 导航到 LOAD DATA 菜单，静置 45s | P0-4 事件等待 |
| scene01-run1 | 进入 01 月亮场景，静置 120s | P0-2/P0-5 |
| bgfg-in-scene | 场景内 HOME→回前台 ×3（各 away 20s / back 25s） | **P0-3 vkDeviceWaitIdle、P0-6 ANativeWindow 双锁竞态** |
| reload-loop | force-stop→启动→进 01→静置，×4（换 4 个 pid） | P0-1/P0-5 场景装卸 churn |
| scene01-run2-warm | 同一进程暖跑 150s | 暖缓存稳态 |
| scene01-long | 同一进程长驻 242s | 长时间稳定 |

共 6 个进程（pid 22642 / 23343 / 25828 / 26133 / 26645 / 27016），全部为生产包启动。

### 1.2 P1 帧分位

生产包自带 `gfx-codex-frame-metrics=1` 的逐帧 CSV（`codex-frame-<pid>.csv`，列 `frame,monotonic_ns,texture_serial,image_writer,draws,dispatches,submissions,present_result`）。

**分段方法**：按**CSV 行号边界**切段（每次阶段边界取 `wc -l` 行号快照），**不依赖任何时钟基线假设**（设备 `/proc/uptime` 是 CLOCK_BOOTTIME，CSV `monotonic_ns` 是 CLOCK_MONOTONIC，二者在本轮有 36 天 suspend 差值，直接换算会错）。
每个区间同时给出：
- **ALL**：全帧（含切场景瞬态）
- **STEADY**：丢掉段首 45 帧（1.5 s）后的稳态
- **TRANSIT**：段首 45 帧单独统计

长帧（>50 ms）逐帧归因：对应帧的 draws / dispatches / submissions 增量 + `present_result`。

---

## 2. P0 闪退清单（历史证据，全部有原始日志）

### 2.1 属于本工程（`org.ffrs.gamewindow`）

| ID | 时间 | 进程/pid | 签名 | 证据 | 状态 |
|---|---|---|---|---|---|
| **C1** | 2026-10-29 21:39 | 22145 / RHIThread | SIGABRT，`Scudo ERROR: internal map failure (Out of memory)`。栈：`descriptors_build` → `descriptors` → `dispatch` → `execute` → `xbg_submit_now` → `nvn_engine_submit` → `nvn_queue_submit_commands`；再向下是**驱动侧 `vkCreateBufferView` → `calloc` 失败**。进程 uptime 517 s | `.local/tombstones-all-20261111/tombstone_17` | **设备侧真实崩溃**。根因类别：descriptor 构建期无界分配 → 64 位地址空间耗尽 → 驱动内 `calloc` 失败 → 进程 abort |
| **C2** | 2026-10-29 22:24 | 28868 / RHIThread | 同上，失败点为 `operator new`（`descriptors_build` ← `draw` ← `execute`）。进程 uptime 575 s | `tombstone_18` | 同类第二次；同一函数 `descriptors_build` |
| **C3** | 2026-11-08 13:37 | 1866011 / NativeActivity | **ANR**：`Input dispatching timed out (… Waited 5001ms for MotionEvent)` | DropBox `data_app_anr` | 已发生；未定位（发生在 gfxreconstruct 回放上下文，需与回放噪声区分） |
| **C4** | 2026-11-11 11:38、11:38、13:35、13:36、13:36、13:37（**6 次**） | 14411 等 | **Java 崩溃**：`java.lang.RuntimeException: Unable to start activity ComponentInfo{org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity}: java.lang.IllegalStateException: The specified child already has a parent. You must call removeView() on the child's parent first.` | DropBox `data_app_crash` ×6 | **本工程真实崩溃，6 次**。源码事实：`GameActivity.java` **全文没有任何 `removeView` 调用**；`AndroidManifest.xml:17` `launchMode="singleTask"`、`:19` `configChanges` 含 orientation/screenSize/uiMode 等全量。复现路径尚未定位（见 §6） |
| **C5** | 2026-11-04/05 | 26439 | **SIGSEGV @ `descriptors_build`**：partial storage texel 同步可能退休仍活跃的 descriptor slot | `state.json honor_battle_crash_20261104` + `.local/honor-device-profiles/flame-20261104/first-battle-crash-25315.log` | 用户报告首战崩溃；诊断日志定位到 `descriptors_build`；**与 C1/C2 同一函数** |
| **C6** | 2026-11-04/07 | 28310 / 29521 / 31613 | `vkCreateGraphicsPipelines` 返回 **-13 (VK_ERROR_UNKNOWN)** → 引擎 fatal → abort（Boss 出现瞬间）。30 FPS 非必要条件 | `state.json honor_boss_crash_20261104` + `boss-crash.log` / `boss-30-diagnostic.log` | **用户确认可重复**；已试 `VK_NULL_HANDLE` pipeline cache 重试与 VS `fe033d948255d9ff` SPIR-V 优化，**均未解决**；tombstone_25 显示离线回放同一 capture 时该调用返回 `VK_ERROR_UNKNOWN` 而 capture 记录为 `VK_SUCCESS` |

### 2.2 同 runtime 家族的姊妹包（`org.snk.the1st.gamewindow`，同一 `libffrs_game_window.so`）

| ID | 时间 | 签名 | 证据 |
|---|---|---|---|
| C7-a | 2026-11-10 19:42 | SIGABRT，`Pointer tag for 0x6fe9eae9f0 was truncated` @ `snk_allocator_free` ← `nb_import_gateway` | `tombstone_01` |
| C7-b | 2026-11-10 20:00 | SIGABRT，`Scudo ERROR: corrupted chunk header` @ `snk_allocator_free` ← `nb_import_gateway` | `tombstone_02` |
| C7-c | 2026-11-11 03:28 | SIGABRT，`Scudo ERROR: corrupted chunk header` @ `snk_sized_delete` ← `nb_import_gateway` | `tombstone_03` + DropBox `SYSTEM_TOMBSTONE` |
| C7-d | 2026-11-05 14:54 | SIGABRT @ `snk_abort_impl` ← `nb_import_gateway`，进程 uptime **2 s**（启动期） | `tombstone_20` |

⇒ 内存破坏类崩溃在姊妹包上 4 次，**在 FFRS 包的 32 份 tombstone 中未出现同类**；FFRS 侧对应的是**分配失败**（C1/C2）而非**堆破坏**。两者根因可能不同，不能合并。

### 2.3 明确排除（非本工程）

| 来源 | 数量 | 说明 |
|---|---:|---|
| `com.sangfor.vdi.client.tablet` | 13 | 深信服 VDI 客户端，`SocketImpl` fd 归属断言，与本工程无关 |
| `libgfxrecon-replay.so`（`.so` 在 `org.ffrs.gamewindow` 包内运行） | 9 | **RenderDoc/gfxreconstruct 离线回放工具自身 abort**（`dump resources` 断言、`vkCreateShaderModule` 回放不一致、`.json` 缺失等）。**不是设备侧游戏崩溃**。tombstone_25 属此类，但它同时**反证了 C6 的 -13 是回放期不一致**，仍作为 C6 的佐证保留 |
| `com.miHoYo.Yuanshen` | 2 | 原神模拟器，与本工程无关 |
| `/system/bin/sh` | 1 | shell |
| `com.android.bluetooth` | 1 | `The Bluetooth HAL died`（与 P2 蓝牙相关，但**不是本应用**） |

### 2.4 本轮 live soak 结果

**未复现。** 22 分钟 / 6 进程 / 8 阶段（含 3 次切后台回前台、4 次场景重载、242 s 长驻）后：

- `/data/tombstones` 前后 diff：**无新增**（前后 32 份全量一致，见 `tombstone-baseline.txt` / `tombstone-after.txt`）
- DropBox：**无新增**本应用条目（ANR 仍停在 2026-11-08 13:37，`data_app_crash` 仍停在 2026-11-11 13:37）
- pid 存活轮询：**无意外死亡**（6 次 force-stop 均发生在明确预期点，见 `soak-marks-and-crashes.json` `crashes[]` 为空）

**结论（必须保留的失败记录）**：本轮**没有在 v1.0.6 上复现任何一条历史闪退**。C1–C6 仍是**历史已发生、未在当前版本再现**。按项目规范，不能据此宣称"已修复"，也不能据此降低验收门槛。

---

## 3. P1 长帧分位（生产 v1.0.6，真机实测）

### 3.1 分场景汇总

| 场景（分段） | 帧数 | 均值 ms | **FPS** | P50 | P95 | P99 | max | >50 ms | >100 ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 标题（冷启动后 40 s） | 1199 | 33.33 | **30.00** | 33.33 | 33.87 | 34.04 | 34.38 | 0 | 0 |
| 标题（暖 60 s） | 1799 | 33.33 | **30.00** | 33.34 | 33.85 | 34.08 | 35.79 | 0 | 0 |
| LOAD DATA 菜单 54 s | 1619 | 33.51 | 29.84 | 33.35 | 33.86 | 34.04 | **313.55** | 1 | 1 |
| **01 场景 首进（冷管线缓存）122 s** | 2939 | **41.62** | **24.03** | 41.17 | 46.09 | 53.46 | **1408.55** | **45** | **9** |
| 01 场景 重载后 dwell（新进程）22 s | 659 | 33.33 | **30.00** | 33.33 | 33.87 | 34.08 | 34.62 | 0 | 0 |
| **01 场景 暖跑 150 s** | 4499 | 33.33 | **30.00** | 33.33 | 33.87 | 34.05 | 34.59 | 0 | 0 |
| **01 场景 长驻 242 s** | 7259 | 33.33 | **30.00** | 33.33 | 33.84 | 34.03 | 34.51 | 0 | 0 |
| **切后台（HOME 中）20 s** | 179 | **116.22** | **8.60** | 144.88 | 159.86 | 178.28 | 187.20 | 126 | **125** |
| 回前台 #0（72 s） | 1499 | 48.15 | 20.77 | 42.41 | **130.28** | 154.80 | 215.80 | 87 | **80** |
| 回前台 #1（75 s） | 1499 | 50.28 | 19.89 | 42.42 | **146.50** | 159.07 | 180.41 | 126 | **113** |
| 回前台 #2（59 s） | 1379 | 42.72 | 23.41 | 42.55 | 46.61 | 48.23 | 57.54 | 5 | 0 |

### 3.2 三条结论

**① 30 FPS 在 01 场景是可达的，长帧不是渲染管线常态。**
同一场景、同一存档、同一包：首进（管线/着色器缓存冷）24.03 FPS；而缓存热后重进并连跑 **150 s + 242 s = 392 s，锁定 30.00 FPS，max 34.59 ms，长帧 0 个**。
⇒ 卡顿集中在**冷加载**，不是稳定态。

**② 冷加载长帧的形态很清晰。**
首进段 45 个 >50 ms 帧中，最大 4 笔全部落在**进入后的前 7 秒**：
- 1408.55 ms（t_rel 3.6 s，draws 12 / dispatch 24）——场景资源构建
- 1156.70 ms（t_rel 1.2 s，draws 2 / dispatch 0）——几乎无绘制的等待段（着色器编译/加载）
- 862.46 ms（t_rel 2.6 s，draws 2 / dispatch 0）
- 641.57 ms（t_rel 6.7 s，draws 950 / dispatch 152）——场景首帧提交

draws 极低而耗时极长 ⇒ 阻塞在**提交线程同步做的磁盘读 + SPIR-V 重写 + `vkCreateShaderModule`**，与静态审计 `CRASH-STUTTER-FULL-AUDIT-20261111.md` P1 档 `xbg_game.cpp:2501-2533`（`shader_reuse=0` 时实测重写 76.62 ms / 48.02 ms）**指向同一处**。静态审计与本轮真机测量在此交叉验证。

**③ 切后台 / 回前台是当前最严重的卡顿源，且与 present 失败强相关。**
- 后台 20 s 内均值 116.22 ms（**8.6 FPS**），126 帧 >50 ms、125 帧 >100 ms。
- 回前台后 **P95 抬到 130–146 ms**，单次回前台有 80–113 帧 >100 ms。
- 这些长帧的 `present_result` 全部为 **1**，而正常场景恒为 **0**（见 `p1-report.json` `long_frames[]`）。
⇒ present 失败/跳过是后台切换路径上的直接可观测信号；该路径正是静态审计 P0-3（`vkDeviceWaitIdle` 无超时）与 P0-6（ANativeWindow 双锁竞态）所在的 surface 重建路径。**这是本轮 P1 最值得下一步动刀的位置**，但按 systematic-debugging 规则，**未定位根因前不提修复**。

---

## 4. 月亮闪烁现状（用户 2026-11-12 反馈"又开始闪烁了"）

**结论：这是已知未闭环缺陷的原始行为，不是本轮引入的回归。**

- 设备当前安装包 = 生产 v1.0.6，SHA256 `e6c2d287…9cda`（本轮开始/结束各复核一次，与已发布资产逐字节一致）。
- marker 全为出厂默认；`gfx-indirect-mode` 不存在、`gfx-indexed-resident=1`。
- 本轮**未安装任何非生产包、未改任何渲染代码**。

此前已闭环的部分：闪烁本体 = **月亮外圈大光晕的开/关**（月盘逐像素相同，差异图为同心圆环，峰值 r=50–60 px，幅度 81 灰阶）；引擎逐帧下发的 45 项状态与 6 对间接绘制参数**逐帧恒定**；四档变体对照显示只有"从客体内存取参数"的两条路径渲染出**稳定且无大光晕**的月亮，但两条都要每帧 GPU 同步（`NVN PERF block_ms legacy_wait=33.32 ms/帧` → 约 10 FPS），**不可发布**。

用户先前看到的"月亮正常了"**不是修复，是辉光整体消失**（ROI 恒定 147.3–147.6，闪烁态 147.6↔169.1 两态）；生产包从未包含该改动，因此闪烁自然仍在。

**唯一解锁路径**：该场景的 **RenderDoc `.gfxr` GPU 级抓帧**（设备上已有工具链：设备现存 `codex-shrine-blocks-20261111_trim_trigger.gfxr` 186 MB 可证明可用），定位 GPU 常驻间接路径下辉光批次所读的顶点/实例数据差异。

---

## 5. 存档安全性（用户 2026-11-12 提问"存档是否保存在安全的地方"）

### 5.1 存档实际位置 —— **安全，覆盖安装不会删**

本轮 4 次进程启动的 logcat 均打印：

`@
I FFRS-GameWindow: live_save_root=/storage/emulated/0/Android/data/org.ffrs.gamewindow/files/codex-local-save
`@

目录内容（`codex-local-save/`）：

| 文件 | 大小 | mtime | 属主 |
|---|---:|---|---|
| `GameSaveData0` | 336992 | 2026-11-12 13:38 | u0_a322 |
| `GameSaveData0.orig` | 309216 | 2026-11-11 17:16 | u0_a322 |
| `GameSaveData1..4` | ~307–320 KB | 2026-11-12 13:38 | u0_a322 |
| `OptionSaveData` | 32704 | 2026-11-12 17:50（本次 soak 期间仍在写） | u0_a322 |
| `EnhancedInputUserSettings` | 11812 | 2026-11-12 13:38 | shell |

⇒ 位于**外部应用专属存储** `/sdcard/Android/data/<pkg>/`：`install -r` 覆盖安装**保留**该目录；`am force-stop`、`adb install -r` 都不删它。
⇒ 另有人工备份 `/sdcard/saves-bak-107/`（2026-11-12 10:44）。

### 5.2 丢失的真实机制（不是"位置不安全"，是"落盘时机"）

**主因：游戏只在特定存档点落盘，进程被杀则该段进度丢失。**
本轮实证：soak 从 17:35 玩到 17:57（22 分钟，含 4 次 force-stop），而 `GameSaveData0` 的 mtime **仍停在 13:38**（只有 `OptionSaveData` 在 17:50 更新）。
⇒ **覆盖安装会杀掉正在运行的应用** ⇒ 距上一个游戏内存档点之间的进度会丢。这就是"每次更新存档经常会丢失"的直接成因：**不是存档文件被删，而是没落盘的那段没了。**

**次因 1：从 Download 恢复会用旧快照覆盖新存档。**
`GameActivity.onStop()` 每次离开应用都会导出一份快照到 `/sdcard/Download/FFRS-save-<yyyyMMdd-HHmmss>/`（本轮已累积 **36 个**目录，最新 `FFRS-save-20261112-174450`）。"从 Download 恢复存档"（`GameActivity.java:1810-1860`）会**把所选目录覆盖写回 live save root**，也包含 `GameSaveData0.bak`（金手指单文件备份）入口 ⇒ 选了旧快照 = 进度倒退。
快照内容可核对：`FFRS-save-20261111-114023/` 内 `GameSaveData0` = 311040 B / 11-11 11:40，与 live 的 336992 B / 11-12 13:38 不同 ⇒ 快照确实是**更旧的存档**。

**次因 2（潜在，当前未触发）：存档根会随安装器状态切换。**
`GameActivity.java:2624-2627`：当数据处于 **managed** 模式时 `saveRoot = getFilesDir()/saves`（应用内部存储），否则为 `null` → 回落到外部 `codex-local-save`。
本机当前为 **unmanaged**（内部 `/data/data/org.ffrs.gamewindow/files/saves` **不存在**，logcat 4 次启动均为外部路径）。
⇒ 一旦某次更新后安装器状态翻到 managed，游戏会**改写到另一个目录**，旧存档仍在但游戏读不到 —— 表现为"存档丢失"。当前**未触发**，但这是一个真实的脆弱点。

**次因 3：`isolated_regression_save` intent 开关。**
`GameActivity.java:2628-2639` + `RegressionSaveRoot.java`：只有显式带该 intent extra 才会切到临时隔离目录。本轮无该 extra（logcat 无 `isolated regression save root=` 行）⇒ **不是本轮原因**。

### 5.3 现在可用的保命操作（不改代码）

1. **更新前先在游戏内触发一次存档**（或在设置里先退出到标题，让 `onStop` 导出快照）—— `/sdcard/Download/FFRS-save-*` 会留下一份带时间戳的完整拷贝。
2. **恢复时挑最新的那个目录**，不要挑旧的。
3. 不要把 `/sdcard/Android/data/org.ffrs.gamewindow/` 整个目录清掉（那才是真删）。

---

## 6. 本轮未解决 / 需用户决策

| 编号 | 事项 | 状态 | 需要的动作 |
|---|---|---|---|
| O1 | 月亮闪烁根因 | **blocked** | 授权 RenderDoc `.gfxr` GPU 级抓帧（设备工具链已具备） |
| O2 | C4（`IllegalStateException: The specified child already has a parent`，6 次） | 根因未定位 | 当前源码 `GameActivity` **零 `removeView`**；`singleTask` + 全量 `configChanges`。下步：在真机做"连续两次 `am start`"定向复现，拿到完整栈；不改代码先复现 |
| O3 | C1/C2/C5（`descriptors_build` 分配失败 / SIGSEGV） | 根因未定位 | 同一函数三处崩溃。建议先做"单变量：限制 descriptor 构建期分配上限"的 A/B，验证内存曲线 |
| O4 | C6（`vkCreateGraphicsPipelines` -13） | 用户可重复，未解决 | 已排除 null-cache 重试与 VS 优化；需 driver 侧进一步证据 |
| O5 | 切后台 8.6 FPS / 回前台 P95 130–146 ms，`present_result=1` | **本轮 P1 最严重项** | 需定位 present 失败原因（surface 重建路径），根因未明前不提修复 |
| O6 | 冷加载 1408 ms 长帧 | 定位到"提交线程同步着色器编译" | 与静态审计 `xbg_game.cpp:2501-2533` 同源；修复需预热线程化（改动面大，需用户拍板） |
| O7 | 存档：落盘时机导致的"更新丢进度" | 已定位机制 | 可先按 §5.3 操作规避；根治需在装包前触发 guest 落盘或改安装流程（需用户决定是否改） |
| O8 | 存档根 managed/unmanaged 切换脆弱点 | 潜在，未触发 | 建议加"两处根同时存在"的提示或固定根（改代码，需授权） |
| O9 | P2 蓝牙音频延迟 | **用户已暂缓** | 本轮不处理（另注：设备 tombstone_21 有 `The Bluetooth HAL died`，属系统蓝牙非本应用） |

---

## 7. 证据清单（`.local/p0-p1-soak-20261112/`，未入库）

| 文件 | 字节 | SHA256 前 16 |
|---|---:|---|
| `soak-marks-and-crashes.json` | 8805 | 30359e5822a9bd74 |
| `soak-log.txt` | 2458 | 1c1f4e90ff65ceb0 |
| `p1-report.json`（全部分段 + 逐帧长帧归因） | 112187 | ecd9c099478a6f65 |
| `logcat-20261112-1737-1800.txt` | 30017213 | c0e8859d42956bc8 |
| `dropbox-full-20261112.txt` | 5123032 | 77db2ff78c306e7e |
| `tombstone-baseline.txt` / `tombstone-after.txt` | 4707 / 2371 | 773e33aafa60efac / c22c55cccc2781f9 |
| `frame-csv/codex-frame-{22642,23343,25828,26133,26645,27016}.csv` | 78 KB–871 KB | 见目录 |
| `soak_harness.py`（soak 脚本） | 7656 | b29d773f070857b0 |
| `an_soak.py`（P1 分析脚本） | 5510 | f13463450830a9d9 |

**再生成命令**

`@
# P0/P1 soak（仅 A42BBB5109000333；不动其他设备/包）
python3 /tmp/soak2.py                       # ≈22 min，落盘 /tmp/soak2/
python3 /tmp/an_soak.py /tmp/soak2          # 分段分位 + 长帧归因
`@

设备侧历史证据（只读补齐，不删除）：`.local/tombstones-all-20261111/tombstone_{00,01,02,03}`。

---

## 8. 声明

- 本轮**未修改任何源码**、**未构建**、**未安装任何非生产包**、**未推送**、**未改动 main/master**、**未触碰 Antigravity 的目录与 WIP**、**未改动存档内容**、**未删除设备数据**（一切归档用 `mv`）。
- 本轮**没有复现任何历史闪退**；C1–C7 状态为"历史已发生、未在 v1.0.6 再现"，**不等于已修复**。
- P1 数字全部来自生产 v1.0.6 真机、生产包自带 CSV，无录像开销、无逐帧打印扰动。
- 未声称：整游戏兼容、60 FPS、独立 QA 通过、人耳音频通过、战斗/特效正确。
# HONOR-P1-CORRECTION-20261112 — P1 长帧分位报告更正 / 场景分段重述

> **性质**：对 `coordination/results/HONOR-P0-P1-SOAK-20261112.md` 的**更正与补充**，不替代原报告。
> 原报告的 P0 闪退清单部分**不受影响**，本文件只更正 P1 长帧分位相关的**结论**。
> 分支 `task/T-release-1.0.7-20261112`。设备 A42BBB5109000333（Adreno 735）。
> 证据根：`.local/wm-perf-20261112/`（本轮）+ `.local/p0-p1-soak-20261112/`（原报告）。

---

## 0. 为什么需要这份更正

原报告 §3.2③ 与 §7 表格 O5 的两条结论，经本轮**只读源码核对 + 真机重测**后被证伪或需要重述。
按项目纪律"未达门槛保留失败记录、不宣称解决"，以及"结论被推翻必须回写"，本文件逐条更正并给出替代口径。

---

## 1. 更正一：`present_result=1` **不是 present 失败**

**原报告 §3.2③ 原文**：
> 这些长帧的 `present_result` 全部为 **1**，而正常场景恒为 **0** …… ⇒ present 失败/跳过是后台切换路径上的直接可观测信号

**更正**：`present_result=1` 语义是 **SKIPPED（这一帧被主动丢弃、不呈现）**，不是 FAILED。

**证据（源码）**：

- `src/graphics/nvn_device/nvn_engine.c:70 / 77 / 80` —— 引擎回调三态映射：
  `0 = QUEUED`、`1 = SKIPPED`、`-1 = FAILED`。
- `src/graphics/nvn_device/nvn_backend_vulkan.c:1162-1170` —— 后台窗口走 `usleep(16000)` 并返回 `NVN_PRESENT_SKIPPED`。
- `src/graphics/nvn_device/nvn_backend_vulkan.c:1178-1192` —— swapchain/acquire 失败 → SKIPPED。

**真机复核**：原报告全部 6 个 pid、36,867 帧中 `present_result = -1` **一次未出现**。

**影响**：`present_result=1` 是"这一帧没显示"，不能推出"present 失败"，也不能推出"surface 重建路径出错"。
原文据此把切后台路径列为"本轮 P1 最值得下一步动刀的位置"，该动刀理由**不成立**。

> 注：原报告 §3.2③ 引用的静态审计 P0-3（`nvn_backend_vulkan.c:990` `vkDeviceWaitIdle` 无超时）与
> P0-6（`game_window_jni.cpp:114-139` 双锁 / `native_window.c:17-19` 无锁消费）**路径本身仍然正确**，
> 但它们与本症状**不在同一根因轴上**，修它们不会改善切后台帧率。

---

## 2. 更正二："回前台 P95 130–146 ms / 80–113 帧 >100 ms / 60 秒劣化" 是**分段口径错误**

**原报告 §3.1 表格**：

| 回前台 #0（72 s） | 1499 | 48.15 | 20.77 | 42.41 | **130.28** | 154.80 | 215.80 | 87 | **80** |
| 回前台 #1（75 s） | 1499 | 50.28 | 19.89 | 42.42 | **146.50** | 159.07 | 180.41 | 126 | **113** |

**更正**：报告段边界**未与真实的 `clear_surface` / `set_surface` 对齐**，把后台帧混进了前台段。

**重测真值（边界与真实 surface 事件对齐后）**：

| 指标 | 回前台 #0 | 回前台 #1 |
|---|---:|---:|
| mean | 41.87 ms | 41.10 ms |
| P95 | 46.63 ms | 46.05 ms |
| >100 ms 帧数 | **2** | **4** |

**恢复行为**：**1 帧内完成** —— `set_surface` 后首帧即 `present_result=0`。

**另外两条澄清**：

1. **"后台 8.6 FPS" 是分段口径 + 设计内丢帧**：后台帧 149.11 ms（纯后台 6.71 FPS），其中 Vulkan present 路径只贡献
   `usleep(16000)` = 16 ms（`nvn_backend_vulkan.c:1168`），**其余 ~93 ms/帧来自 OS 级后台冻结/限频**
   （logcat `HwActivityManagerServiceEx: unfreezeForSpeedUp pkg=org.ffrs.gamewindow`）。这是系统行为，不是引擎缺陷。
2. **后台帧的 GPU 工作量与前景完全一致**（draws/帧 736.8 vs 739.4 等）⇒ **无重建、无重复 submit**。

**影响**：原报告"回前台后 60 秒持续劣化"**不成立**。切后台/回前台在本轮**不是**最严重的卡顿源。

---

## 3. 更正三："大地图 24 FPS" —— 场景分段口径必须拆开（本轮新增）

原报告未把"世界地图"整体当作一个场景测量。本轮按 **draws/帧自动分段 + 墙钟对齐**重建了时间线：

| 时段 | 场景 | draws/帧 | FPS | 备注 |
|---|---|---:|---:|---|
| 18:18:10–18:19:59 | 菜单 / 加载 | 38 | 26.82 | — |
| 18:20:01–18:23:00 | **世界地图入图过场**（飞空艇 / 巨龙） | 717 | **24.32** | mean 41.12 / p50 41.60 / p95 47.50 / p99 53.41 / >50ms 82 帧。**未被录像污染**（录像 18:24 才开始） |
| 18:23:05–18:23:29 | 过场轻段 | 508 | 28.63 | — |
| 18:23:42–18:25:47 | 过场 + 教学弹窗 | 704 | 23.70 | 部分被 screenrecord 污染 |
| 18:26 之后 | **世界地图主界面 / 战场** | 521 | **29.76** | p50 33.33 / >50ms 仅 17 帧。未污染 |

**结论**：**世界地图主界面本身是稳 30 的**；真正掉帧的是**入图过场动画**。
把两者合并称作"大地图 24 FPS"**不准确**。用户体感的"大地图卡"对应的是过场。

**方法学纪律（本轮新增，必须遵守）**：

- **screenrecord（3000×1920 60fps）本身严重污染帧率**。量化闪烁可以用它，但**帧率测量期间禁止录像**。
- 分场景结论必须用 **draws/帧 自动分段 + 墙钟对齐**，不得按"感觉"或单一时间窗切段。
- **3Hz 截图采样会混叠**，不能用来判断"闪烁"；闪烁必须 60fps + ROI 裁切。
- **全局亮度指标抓不到局部光晕**；必须按 ROI 裁切后逐帧统计。

---

## 4. 帧时间结构根因（本轮新证据，引擎自证）

引擎 `NVN PERF v=1`（每 60 帧窗口）+ `NVN PERF block_ms` 分解：

| 场景 | fps | submit_cpu_ms | gpu_ms | legacy_wait ms/帧 |
|---|---:|---:|---:|---:|
| 过场 717 draws | 24.3 | **26.04** | 18.61 | 16.6–18.5 |
| 战场 521 draws | 30.00 | 18.9–20.4 | 12.4–21.6 | 13.0–19.3 |
| 副本 / 菜单 | 30.00 | 12–15 | 12–18 | ~14 |

**相关系数**：`corr(fps, submit_cpu) = -0.945`（首要因子）；`corr(fps, gpu_ms) = -0.771`；
`corr(gpu_ms, legacy_wait) = +0.916`。

**模型**：`frame_time = CPU 翻译 + 等 GPU`，两者**完全串行，不重叠**。
副本内 11.9 + 14.8 = 26.7 ms（进 33.3 ms 预算）→ 30 FPS；过场 26.0 + 18.6 = **44.7 ms**（超预算）→ 24 FPS。

**机制定位到源码**（`src/graphics/nvn_vulkan/engine/xbg_game.cpp`）：

- `:6452` `xbg_submit_now()`：`for(i<n) execute(handles[i])` 翻译 guest 命令进 recording slot，**此处不做 vkQueueSubmit**；`perf.submit_cpu_ns` 在此累计。
- 真正的 `submit_slots(false)` 在 **present 路径 `:6715`** → **每帧只有 1 次真实 vkQueueSubmit，且发生在整帧翻译完成之后** ⇒ GPU 只能在 CPU 翻译完 26 ms 后才开工。
- `arena_flush_bytes = 96MB`（`:501`）的帧内提交在真实负载下永不触发。
- `:5832`/`:5847` 帧内提交仅在 arena 溢出或 `OP_SIGNAL_EVENT`（需 `defer_events`）时发生。
- `xbg_legacy_wait()`（`:6464`）新路径（`legacy_waitall=0`，当前生效）只等"覆盖该 fence 的那一批"。
- `slot_count = 16`（`:775`/`:6285`，`gfx-slot-count` 默认满）→ 槽位深度不是瓶颈。

**修复方向**：让 GPU 提前开工（而非降画质 / 删绘制）。

---

## 5. 已验证有效的开关：`gfx-defer-events=1`

**单变量 A/B（同一过场场景，draws 匹配）**：

| | 基线 `defer_events=0` | 实验 `defer_events=1` |
|---|---:|---:|
| 过场 FPS | **24.32** | **30.00**（连续多个 60 帧窗口） |
| p50 帧时间 | 41.60 ms | **33.34 ms** |
| >50ms 帧 | 82 | **19** |
| submit_cpu_ms | 26.04 | **14.7–17.2** |

- `gfx-event-guard` 同时开启作为安全网：**evguard 违规 = 0**（guards=0 / writes=0），无 guest 越过延迟发布读事件值。
- 用户主观确认：**"流畅多了"**。
- **用户同时反馈"加载也太久了吧"** —— 该代价的定位**尚未闭环**（调查进行中，见 §7）。

**开关语义**（源码 `:5844` + `:610-613` 注释）：

- `defer_events=0`：游戏每帧 4 次 `SignalEvent` 会执行 `complete_all()` → `submit_slots(true)` → CPU 在每个 signal 点强制同步等 GPU。
- `defer_events=1`：改为"提交但不等待，事件值在该 slot 退休时发布"。
- `defer_events=2`：不提交，事件值留在正在录制的 slot，slot 退休时发布（一帧仍是一次提交）。

**marker 目录纪律**：引擎 marker base = **parent(guest log_path)** = `/sdcard/Android/data/org.ffrs.gamewindow/files/`。
写到 `/sdcard/FFRS/` **引擎不读**（曾踩坑，日志仍显示 `defer events=0`，易误判为"开关无效"）。

---

## 6. 本轮其他已确认根因

### 6.1 月亮光晕闪烁 = **配置漂移**（已修复）

引擎 `xbg_game.cpp` 约 6177-6196：

> ```cpp
> // Adreno 735: reusing the open depth/stencil pass flickers the moon
> // halo in the real scene. Keep the existing end-pass/barrier path.
> bool honor = tablet && vendorID==0x5143 && deviceName=="Adreno (TM) 735";
> depth_aspect_enabled = !honor;              // Honor 默认关闭
> if(m){depth_aspect_enabled=fgetc(m)!='0';}  // marker 覆盖
> ```

但 `src/android/app/java/org/ffrs/diagnostic/GameActivity.java:1990`：

> ```java
> writeMarkerIfMissing(new File(dir, "gfx-depth-aspect"), "1\n");
> ```

`writeMarkerIfMissing` 在文件缺失时写入 ⇒ App 每次启动都会**重建**该 marker，**永久覆盖引擎在 Honor 上的安全默认值**。
真机日志自证：`NVN depth-aspect sample reuse=1 (honor=1; marker overrides)`，且有实际生效记录
`NVN depth-aspect sample kept serial=… count=N`。

**处置**：设备上改 marker 值为 `0`（**保留文件**，删除会被 App 重建为 `1`）→ 引擎改打印
`reuse=0` ⇒ **用户确认月亮不再闪烁**。代码侧默认值修复已派工。

### 6.2 战斗特效 / 召唤兽放大卡顿 = **管线在特效释放那一帧同步创建**

引擎自证（`NVN PERF pipelines_new=…`，行 `:1280`）：

| 窗口 | created | create_ms | create_max_ms |
|---|---:|---:|---:|
| 1 | 5 | 750.5 | 212.7 |
| 2 | 13 | 782.4 | 188.6 |
| 3 | 8 | 1424.3 | 359.8 |
| 4 | 10 | 1478.4 | 271.1 |

- 预热机制本身正常：`warm_built=1492 warm_failed=0`，`warm_hits` 有命中，`evicted=0`（**排除** `pipeline_object_limit=4096` 驱逐）。
- 关键点：`warm::record()`（`:2907`）在**创建成功后**才记录；`warm::take()`（`:3176`）按 key 字节取。
  ⇒ **特效管线第一次必然同步建**；能否被下一次预热命中，取决于该 key 是否稳定。
- 异步管线路径 `:3228` 只对 `may_skip=true` 生效（`gfx-async-pipelines`），而召唤兽放大是**必须立刻可见**的 draw，不能走"跳过"。

**已排除的测量污染**：`gfx-flame-diag` 开启时，present 路径 `:6715` 会 `gpu_sync(perf.sync_readback)` 做 ROI 回读
（**每帧全设备 wait**）⇒ 副本一度测到 17 FPS 是**测量污染**，不是真实性能。
关闭后副本稳态 **28.3 FPS**（p50 33.33 ms / p95 43.33 ms）。
真实长帧集中在特效释放瞬间，呈**簇状爆发**（t=17.0–17.2 s、33.1 s、34.6–35.5 s），单帧峰值 1199 ms。

### 6.3 冷加载长帧与特效卡顿同族

`descriptors_build` / 冷加载调查（同轮完成）结论：冷加载的 1408.55 / 1156.70 / 862.46 / 641.57 ms 四帧，
阻塞在**提交线程同步做的磁盘读 + SPIR-V 改写 + `vkCreateShaderModule` + 管线创建**（`xbg_game.cpp:2501-2533`、
静态审计 `CRASH-STUTTER-FULL-AUDIT-20261111.md` P1 档交叉验证）。
⇒ **冷加载长帧与特效卡顿是同一族问题**：重活落在渲染线程关键路径，而不是被并行化。

---

## 7. 状态表更正（原报告 §7 表格）

| 原编号 | 原描述 | **更正后状态** |
|---|---|---|
| O5 | 切后台 8.6 FPS / 回前台 P95 130–146 ms，`present_result=1` | **§1 §2 已证伪**。后台 6.71 FPS 为 OS 冻结/限频 + 16 ms `usleep`；回前台真值 mean 41.87 / P95 46.63 / >100 ms 仅 2 帧，1 帧内恢复。`present_result=1` = SKIPPED 而非失败 |
| O6 | 冷加载 1408 ms 长帧 | **成立**，且与 §6.3 特效卡顿同族 |
| O1 | 月亮闪烁根因 | **已定位并修复（§6.1）**：App 自动重建 `gfx-depth-aspect=1` 覆盖了引擎在 Honor 上的安全默认值 |
| — | "大地图 24 FPS" | **§3 口径更正**：主界面 29.76–30.00 FPS；24.32 FPS 的是**入图过场** |

---

## 8. 本轮未做 / 未确认（保留失败记录）

- **NOT_DONE**：`defer_events=1` "加载变长"的代价定位（调查进行中，未闭环）。
- **NOT_DONE**：特效管线卡顿的代码修复（已定位到同步创建机制，修复实现中）。
- **NOT_CONFIRMED**：`present_result=1` 在**切后台**之外是否还有别的触发场景。
- **NOT_CONFIRMED**：`vkCreateGraphicsPipelines` -13（C6）未解决。
- **NOT_CONFIRMED**：神殿外方块（O10）需重新定位。
- **NOT_RUN**：P2 蓝牙音频延迟（用户暂缓）、独立 QA、音频人耳验收、战斗与特效全面验收。
- **NOT_CLAIMED**：不声称整游戏兼容、不声称 60 FPS、不声称 P0 闪退已修复。

---

## 9. 证据清单

本轮（`.local/wm-perf-20261112/`）：

| 路径 | 内容 |
|---|---|
| `cheat/cheat-dialog-works.png` | 金手指对话框真机验证：真实存档数据已解密 |
| `cheat/menu-panel.png` | 功能菜单面板（8 项，第 2 项为金手指） |
| `logs/battle-depth-aspect0-engine.log` | depth-aspect=0 的副本引擎日志 |
| `logs/battle-depth-aspect0-frame.csv` | depth-aspect=0 的副本逐帧 CSV |
| `logs/battle-depth-aspect1-engine.log` | depth-aspect=1 的对照引擎日志 |

既有：`.local/p0-p1-soak-20261112/`（原报告证据根）。

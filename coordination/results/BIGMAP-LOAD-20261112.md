# 大地图进入卡顿 — 离线归因分析（2026-11-12）

任务线：只做离线分析 + 必要的测量设施。不接触任何设备。
工作区：`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/release-1.0.9-20261112`
分支：`task/T-release-1.0.9-20261112`

## 0. 用户现场诉求（一线测试者原话）

> 「目前卡顿的地方主要还是**大地图**，每次进入大地图得卡一小会，**可能是加载着色器**，之后也就流畅了。其他的像城镇内、副本内都很流畅。」

即：进入大地图时卡一小会（怀疑加载着色器），进入之后流畅；城镇内、副本内全程流畅。

## 1. 已知事实（直接采用，不重做）

- 基线（Honor GT Pro 1.0.7，pid 7742）：7260 帧 / 295.16 s / 平均 24.59 FPS / 稳态中位 33.34 ms / 93 个 >100 ms 尖峰吃掉 40.1 s（13.6%）/ 最坏帧 #29 = 6506 ms。
- 尖峰归因已修正三次：不是同步建管线（全会话单条最大建管线 1.3 ms）；也不是 seed 预热（巨帧在预热开/关/无缓存三种情况下都在：4800/4690/4489 ms）。当前结论：**CPU 卡在提交路径**（`submit_cpu_ms` 28.09 vs 13.47），GPU 更闲（13.48 vs 17.25），慢窗标记 `sync_rb`/`sync_wb` drain。
- 那笔 4.5–4.8 s 冷启动巨帧**至今未解释**：约 133 ms/帧花在引擎所有计数器之外，报告结论是「场景与资源加载，不是图形路径」。
- readback 定向退休实测：readback drain 阻塞只有 1.79–3.85 ms/帧，`submit_cpu_ms` 是 22.5–37.7 ms/帧；readback 这根杠杆只值 4–6 ms/帧，单靠它达不到 30 FPS。
- 管线创建在慢窗更高：`created` 13.7/帧 vs 快窗 0.14/帧，`create_ms` 合计 6.8 ms/窗。
- A 类缺陷已修（1.0.9 已合入）：引擎原来只有「创建耗时 ≥ 2 ms」的管线才让 `pipelines_unsaved` 增加，而 `warm::flush()` 写在 `save_pipeline_cache()` 的守卫之后 → **学到的管线配方记进内存却从不落盘，下次启动全部重建**。现在 warm list 有独立 300 帧节流落盘，shutdown 也会 flush。
- 引擎已有 `NVN shader slow-load identity=%s stage=%u frame=%llu monotonic_begin_ns=... total_ms=%.2f`（阈值 5 ms）。
- 引擎已有 `NVN PERF pipelines_new=... warm_hits=... created=... create_ms=... create_max_ms=... warm_built=... warm_ms=...`。

## 2. 本线要回答的三个问题

### Q1「进入大地图」这一笔时间花在哪
逐窗口/逐帧给出拆分：**着色器加载 / 管线创建 / 提交阻塞 / 其它**。

### Q2「第二次进入大地图」是否仍然卡
同一会话内大地图的**首次**进入 vs **再次**进入，尖峰/巨帧差多少？
这直接检验「A 类落盘缺陷是否让每次进大地图都重建管线」这个假设。**成立或不成立都要按数据写，不顺着猜测写。**

### Q3 城镇内 / 副本内为什么流畅
若日志能区分场景则给对照；不能则明确说不能，并给出补救测量口径。

## 3. 数据与方法

已有真机数据（只读，不碰设备）：
- `.local/rescue-1.0.7-20261112/device/guest_boot-16129-warm-off.log`（10,034,155 B，pid 16129，126 个 60 帧窗口）
- `.local/rescue-1.0.7-20261112/device/codex-frame-{11545,14135,15089,16129}.csv`
- `.local/rescue-1.0.7-20261112/windows-warm-off.json`
- 分析器：`scripts/graphics/analyze_stalls.py`（含 `--perf-log` 模式）、`.local/tools/analyze_frames.py`

### 已完成的对齐标定（本线自做）

1. **时钟一致**：CSV 的 `monotonic_ns` 与引擎日志的 `monotonic_begin_ns` 同为 CLOCK_MONOTONIC，可直接对齐。
2. **帧号口径一致**：引擎 `stats.presents` == CSV `frame` 列（逐条 slow-load 抽查，偏移 ≤ 1 帧）。
3. **PERF 窗口 ↔ 帧区间**：PERF `v=1` 行不带帧号，但每窗口固定 60 帧且按顺序发射。标定结果：**窗口 S 覆盖帧 60(S-1)+1 … 60S**；用 CSV 帧间隔独立复核，126 个窗口的 `wall_ms` 与对应 CSV 区间跨度比值全部落在 1±0.15（0 个失配）。
4. 因此：`analyze_stalls.py --perf-log` 的窗口计数与 CSV 帧可以严格互查，慢窗/快窗归因可以精确落到帧。

## 4. 执行步骤

- [x] S1 读取并对齐已有日志/CSV（完成）
- [x] S2 窗口↔帧映射标定（完成）
- [ ] S3 事件簇提取：把 shader slow-load 突发、>100 ms 帧尖峰、`pipelines_new/created` 三条线按帧号对齐
- [ ] S4 逐笔拆分：进入大地图那一笔 = 着色器 / 管线创建 / 提交阻塞 / 其它
- [ ] S5 首次 vs 再次进入大地图的对比（检验 A 类假设）
- [ ] S6 城镇/副本对照（场景识别可行性与结论）
- [ ] S7 若现有计数器无法区分首次/再次进入 → 最小 marker 门控测量（只改 `xbg_game.cpp` 统计/日志区，走**新的一行 PERF**，不改主 `NVN PERF v=1` 行）
- [ ] S8 1.0.9 可验证预期 + 设备侧测量清单

## 5. 写权与边界

可写：`src/graphics/nvn_vulkan/engine/xbg_game.cpp`、`scripts/graphics/`、`coordination/results/`。
不碰：任何设备；主 worktree；其它 worker 的 worktree；渲染路径与等待策略（过场线写权）；warm/pipeline 落盘逻辑（特效/seed 线写权）。
提交：精确 `git add <具体文件>`，禁 `git add .`；禁 reset --hard / clean / stash。

## 6. 社区出处硬门

若借鉴社区做法（着色器预编译、PSO 缓存、场景资源异步加载等），写进 `coordination/results/BIGMAP-LOAD-20261112-OSS-NOTES.md`：URL、不可变 revision、许可证、实际支持范围、**是否真取到**（打开正文才算）。搜索摘要不算取到。

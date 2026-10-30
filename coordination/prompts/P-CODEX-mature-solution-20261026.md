# 给 Codex 的补充提词：成熟方案已定位，别再自己从头研究

> 这份是补充，配合 `P-CODEX-occ-ab-v2-20261026.md` 一起看。目的：把 Kiro 今天（2026-10-26）已经查实的「成熟移植方案」直接交给你，**你不要再自己开一轮网络搜索/调研**——方向已经定了，照用即可。省时间、避免重复劳动。

---

## 一、结论先行：我们用的就是成熟方案本身

**本项目的渲染引擎 `src/graphics/nvn_vulkan/engine/`（约 6480 行，本工作树里未被 git 跟踪）就是 d2r-mobile 的 NVN→Vulkan HLE 渲染器源码。**

证据（引擎源码注释里到处是）：
- 遍布 `D2R camp` 帧实测数据、`Redmi K50 Ultra`、`Adreno 730`、`dm/gfx/nvn` 路径。
- 这是一套**本来就是为了在 Android / Mali / Adreno 上高帧率跑一个 3A 大作（暗黑破坏神2重制版手机版）而调优过的成熟代码**。

**含义**：op27/op28 屏障、writeback、occlusion 这些机制都是上游权衡过的设计，不是随手堆的。我们不是在从零造移植器，而是在**把一套为 D2R 调好的成熟引擎，适配到一个负载特征不同的 UE5 游戏（FFRS）上**。所以——

**不要去另找移植框架、不要去研究 Yuzu/模拟器、不要从头搜「怎么把 Switch 游戏移植到安卓」。** 成熟方案就在手里，任务是对齐负载差异。

---

## 二、负载差异（这是全部问题的根）

同一套引擎，D2R 和 FFRS 的 GPU 负载不同：

| | D2R（引擎为它调优） | FFRS（我们的游戏） |
|---|---|---|
| SAMPLES_PASSED 遮挡计数 | **从不记**（samples_report_seen=false） | **大量记**（每帧约 64 reports + 100 occlusion 查询） |
| 结果 | 上游遮挡快路径命中，pass 不被切碎 | 上游快路径**没命中**，ReportCounter(op5) 成为切碎 pass 的头号原因 |

Mali 分块架构下，每个 pass 都要装载/存储 tile，**过度切 pass = 头号性能杀手**（这点有 ARM 官方 Vulkan-Samples 的 pipeline_barriers 样例、Godot issue #115442 佐证，是业界公认）。FFRS 过场每帧约 1085 Barrier + 557 Dispatch pass-end + 129 ReportCounter pass-end，真正几何只占 6.4ms，大头是约 67 个小 pass 的固定开销。

**所以优化方向不是猜，而是**：用上游**已经提供的成套开关**去匹配 FFRS 的负载，关掉/合并那些为 D2R 设计但 FFRS 不需要的 pass 切分。当前正在测的 `gfx-no-occlusion` 就是第一刀。

---

## 三、同类平行先例（只作思路参考，代码不同源）

以下是**和本项目同一条技术路线**（Switch 代码静态重编译成 ARM64 原生、NVN 翻译 Vulkan、跑在 Android/Mali/Adreno，**非模拟器**）的公开项目，已经跑到 30-60fps。**仅供借鉴思路，不是让你去 clone 来改**：

- `mamcjr/LASRecompAndroid`（塞尔达织梦岛）—— 共享一套 NVN→Vulkan 渲染器，与 BOTW/超级马里奥惊奇 Recomp Android 同源。
- `BelmanteGu/SuperMarioWonderRecomp`（超级马里奥惊奇，含 docs/nvn/ANDROID-PORT）。

它们公开记录的、**可借鉴到 FFRS 的经验**：
1. **动态分辨率按 GPU 自身耗时调**（不是按 draw/shader 工作量）——FFRS GPU≈58ms/帧，这是奔 30fps 可能最立竿见影的一条路（但注意：本项目 ScreenPercentage 实测无效，因为瓶颈是 pass 固定开销不是像素填充，所以降分辨率要先验证真能降 pass 开销）。
2. **render target 压缩（Turnip UBWC）、减少颜色拷贝、小辉光层按自身尺寸渲染**。
3. **Mali 驱动缺扩展的回退路径**（无 synchronization2 走 Vulkan 1.0 屏障、无 dynamic_rendering 走经典 render pass）——本项目正用经典 render pass + 全屏障，很可能就是这套共享渲染器的 Mali 回退路径，印证「减 pass/减屏障」方向正确。
4. shader 预编译降首次卡顿。

---

## 四、给你的明确指令

1. **不要再开网络搜索/调研「移植方案」**——方向已定，就是「d2r-mobile 成熟引擎 + 用上游开关匹配 FFRS 负载」。
2. 当前任务仍是 `P-CODEX-occ-ab-v2` 那个：驻留场景两次独立运行测 `gfx-no-occlusion`，只读测量。
3. 后续候选开关（都用驻留两跑法，不用过场交替）：`gfx-query-per-draw`（关遮挡批处理）、`timestamps`、`wbskip`、`noload`。
4. 「按 GPU 耗时的动态分辨率」「render target 压缩」作为中期候选改动，等开关实测结果出来后再排优先级，**现在不要动手改源码**。
5. 一切以真机 PERF 实测为准；上面的平行先例是资料非授权，不改游戏身份校验，不 clone 外部仓库来改本项目。

---

## 五、边界

沿用 `AGENTS.md` 与今日约定：只在 `.worktrees/codex-black-screen`（task/T02）工作；不 push、不碰 main、不动主树脏改动；产物进 `.local/kiro-runs/`，不进 git。

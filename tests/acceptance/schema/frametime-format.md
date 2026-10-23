# 帧时间原始数据格式说明 (T06)

本文件是 `frametime-sample.schema.json` 的散文规范。JSON Schema 只约束结构，
本文件约束**语义**：时钟来源、测量点、分位数算法、场景分段、冷/暖缓存标记。

引用规则：任何 T06 用例的判定必须引用本文件定义的口径。口径若变更，必须升版本号
并重跑受影响的用例；不得在同一份数据上换口径得出不同结论。

---

## 1. 时钟来源 (`clock`)

| 字段 | 含义 |
|---|---|
| `clock.source` | 实际传给 `clock_gettime()` 的 clock id。**首选 `CLOCK_MONOTONIC_RAW`**。 |
| `clock.clock` | 具体调用形式，例如 `clock_gettime(CLOCK_MONOTONIC_RAW)`（Android `clock_gettime64`）。 |
| `clock.resolution_ns` | `clock_getres()` 返回值，单位 ns。 |
| `clock.monotonic_anchor_ns` | 本次采集的第一条原始时间戳（同一 clock 单位）。 |
| `clock.wallclock_anchor_iso` | 单调时钟锚点对应的墙上时间（设备时钟），用于与日志交叉引用。 |
| `clock.suspend_aware` | 该时钟是否跨设备休眠继续推进（`CLOCK_BOOTTIME` 是；`CLOCK_MONOTONIC` 在 Android 上也包含 suspend）。若采集跨越休眠，必须为 `true` 或把采集拆成两段。 |
| `clock.drift_correction` | 做过任何校正必须写明方法；未校正填 `null`。**不允许事后用 wall clock 修正单调时钟**——那会引入 NTP 步进伪影。 |

**口径**：
- 帧间隔 = 相邻两条 `ts_ns` 之差，**单位 ns，换算成 ms 时除以 1e6**。
- 采集端不得对 `ts_ns` 做平滑、插值或去重；去重只允许在"是否为新游戏帧"的判定层
  （见第 3 节），且必须保留原始记录。
- 若 `clock.source` 为 `unknown`，该记录**不能**用于 PASS/FAIL，只能作为参考数据。

---

## 2. 测量点 (`measurement_point`)

| 字段 | 含义 |
|---|---|
| `measurement_point.location` | 时间戳的确切取点位置，逐字写明。 |
| `measurement_point.method` | 取点方法。 |
| `measurement_point.timestamp_semantics` | 时间戳代表什么：`present` / `submit` / `render_complete` / `input_to_present` / `unknown`。 |
| `measurement_point.overhead_ns` | **实测**的采集开销（纳秒）。未实测填 `-1`，并在 `limitations` 写明。 |
| `measurement_point.overhead_measured` | true/false。 |
| `measurement_point.sampling.every_n_frames` | 每 N 帧记一次。**必须为 1**，除非明确说明统计口径是抽样。 |
| `measurement_point.sampling.interval_ns` | 采样间隔。 |

**口径**：
- **优先 `present`（实际呈现时刻）**。`submit` 只是把帧交给驱动，不代表用户看到；
  用 `submit` 得到的帧间隔会系统性偏乐观，必须在报告里写明 `timestamp_semantics`。
- `overhead_ns` 的实测方法：同样的采集代码在空场景（不加载游戏内容）下连续运行，
  取采集器自身引入的额外耗时中位数；或对比"开采集/关采集"两次运行的帧间隔差。
- `overhead_ns` 超过 1 ms 时，采集开销本身可能影响 P99，必须在结论里显式说明。

---

## 3. "新游戏帧" 判定（PROJECT.md 强制）

PROJECT.md：「重复呈现同一游戏帧、系统 120Hz 刷新率和 UI overlay 刷新不计作新游戏帧」。

判定规则（按优先级）：

1. `samples[].game_frame_id` 相同 ⇒ 同一游戏帧被重复呈现 ⇒ `new_game_frame = false`。
2. `samples[].presented_content` 为 `ui_overlay` 或 `external_texture` ⇒ 不是游戏帧 ⇒ `new_game_frame = false`。
3. `samples[].presented_content` 为 `video` ⇒ 属于源素材视频，**单独分段统计**，不计入游戏帧指标。
4. 其余 ⇒ `new_game_frame = true`。

统计口径：
- 帧间隔序列**只由 `new_game_frame = true` 的相邻样本构成**（取它们之间的 `ts_ns` 差）。
- `new_game_frame = false` 的样本仍然完整保留在 `samples[]` 中（可审计），但不参与分位数计算。
- `stats.new_game_frame_count` 必须与参与计算的样本数一致。
- **面板刷新率 120 Hz 不构成理由去重**：去重依据是"是否同一 game_frame_id"，不是"时间戳间隔小"。
  一个真实 60 FPS 的游戏在 120 Hz 面板上会呈现两次同一帧 —— 这正是规则 1 要处理的情况。
- 反之，如果实现层无法提供 `game_frame_id`，则必须改为用**帧序号自增**作为等价证据，
  并在 `limitations` 写明"无法区分重复呈现与真实新帧"，此时该档位的结论降级为参考。

---

## 4. 分位数算法

固定算法 id：`nearest-rank-v1`。

```
输入：X = 排序后的帧间隔数组（升序），长度 N
P95 = X[ceil(0.95 * N) - 1]      # 1-based 秩，0-based 索引
P99 = X[ceil(0.99 * N) - 1]
P50 = X[ceil(0.50 * N) - 1]
```

- `nearest-rank` 是**不含插值**的秩法：报告值一定是**某个实测样本**，不会被平滑成不存在的数值。
- 必须写入 `stats.algorithm`。
- **1% low 与 P99 不是同一个量**。本项目固定口径：
  `p1_low_fps = 100 / p99_interval_ms`（即"最慢 1% 帧所对应的等效帧率"）。
  必须在 `stats.p1_low_formula` 写明该公式。禁止把 `p1_low_fps` 与 `p99_interval_ms`
  混着讲，或用"1% low = P99"的说法。
- 平均帧率 `mean_fps` = `new_game_frame_count / (最后一条 ts - 第一条 ts) * 1e9`，
  **不是** `1000 / mean(interval)`（两者在有长尾时不等）。
- `over_100ms_count` = 帧间隔 `> 100 ms` 的样本个数。

**PROJECT.md 阈值（暖缓存游玩区间）**：

| 指标 | 目标 |
|---|---|
| 平均实际游戏帧率 | ≥ 29 FPS，且正确限帧至 30 |
| P95 帧呈现间隔 | ≤ 35 ms |
| P99 帧呈现间隔 | ≤ 50 ms |
| > 100 ms 的游玩帧间隔 | 每 5 分钟区间 ≤ 1 次，逐次定位；反复同点卡顿仍 FAIL |

---

## 5. 场景分段 (`scenario.segments`)

| 字段 | 含义 |
|---|---|
| `scenario.category` | `movement` / `battle` / `cutscene` / `menu` / `loading` / `mixed` / `other`。PROJECT.md 要求 movement、battle、cutscene **分别**测量。 |
| `scenario.load_boundary_policy` | **测试前**写死的 loading / 视频 / 场景切换边界规则。 |
| `segments[].kind` | `playing` / `loading` / `cutscene` / `video` / `menu` / `warmup` / `transition` / `unknown`。 |
| `segments[].excluded_from_playing_stats` | 该段是否排除在"游玩"统计之外。 |
| `segments[].exclusion_reason` | 排除理由。 |
| `segments[].source_video_fps` | 若该段是源素材视频，其原生帧率。 |

口径：
- **端到端结果与游玩分段都要报告**，不能只报好的那段，也不能事后删掉慢帧。
  - 端到端 = 整段采集（含 loading）的全部 `new_game_frame` 样本统计。
  - 游玩分段 = `excluded_from_playing_stats = false` 的段落的统计。
- `load_boundary_policy` 必须在**运行前**写定并随数据保存。**事后调整分段以改善结论 = 违规**。
- `video` 段必须单独成段并单列统计；其帧率与游戏帧率不同，不得混入。
- `warmup` 段（启动后前若干帧）必须显式标记并给出 `cache.warmup_frames_discarded`。
- 至少一个 segment 的 `kind` 为 `unknown` 时，该次采集只能作为参考，不得用于 PASS。

---

## 6. 冷 / 暖缓存标记 (`cache`)

| 字段 | 含义 |
|---|---|
| `cache.state` | `cold` / `warm` / `unknown`。 |
| `cache.cold_reason` | `first_install` / `first_launch_after_install` / `asset_cache_cleared` / `first_launch_after_boot` / `page_cache_dropped` / `other` / `null`。 |
| `cache.app_data_cleared` | 是否清过 app data。 |
| `cache.system_page_cache_dropped` | 是否丢过系统页缓存。 |
| `cache.shader_compile_pause_ms` | 冷启动时着色器首次编译停顿（实测 ms）。 |
| `cache.warmup_frames_discarded` | 丢弃的预热帧数。 |

口径：
- **冷、暖缓存必须分开测、分开报**，不得用暖缓存结果掩盖冷启动体验。
- 冷启动另需覆盖：首次安装后首次进入、清除资源缓存后首次进入、开机后首次进入。
- `shader_compile_pause_ms` 必须实测并单列；它不计入帧率分位数，但必须在报告中出现。
- `cache.state = unknown` 的采集不能用于冷/暖对比结论。

---

## 7. 采集开销与自身失真

- 采集器必须与渲染线程分离（独立线程 / Choreographer 回调），不得在渲染线程内做 I/O。
- 原始记录建议落 `.local/runs/<run_id>/frametime.json`（大文件不进 Git）；
  Git 只保存 schema、摘要与索引。
- 采集器异常（丢帧、线程被抢占）必须在 `limitations` 写明，并单独标注该次采集的可用性。

---

## 8. 最小示例（结构示意，数值仅为占位）

```json
{
  "schema_version": "1.0.0",
  "run_id": "t06-20261023t140000z-battle-a",
  "captured_at": "2026-10-23T14:00:00+08:00",
  "clock": {
    "source": "CLOCK_MONOTONIC_RAW",
    "clock": "clock_gettime64(CLOCK_MONOTONIC_RAW)",
    "resolution_ns": 1,
    "monotonic_anchor_ns": 1234567890,
    "wallclock_anchor_iso": "2026-10-23T14:00:00+08:00",
    "suspend_aware": true,
    "drift_correction": null
  },
  "measurement_point": {
    "location": "eglSwapBuffers 返回后，渲染线程立即 clock_gettime",
    "method": "eglSwapBuffers + clock_gettime(CLOCK_MONOTONIC_RAW)",
    "timestamp_semantics": "present",
    "overhead_ns": 412,
    "overhead_measured": true,
    "sampling": { "every_n_frames": 1, "interval_ns": 0 }
  },
  "scenario": {
    "name": "combat-encounter-A",
    "category": "battle",
    "planned_duration_s": 300,
    "load_boundary_policy": "loading 画面可见期间的呈现不计入 playing 段，单独记 kind=loading",
    "segments": [
      { "index": 0, "label": "pre-load", "start_ns": 0, "end_ns": 1200000000, "kind": "loading",
        "excluded_from_playing_stats": true, "exclusion_reason": "loading", "source_video_fps": null }
    ]
  },
  "cache": { "state": "warm", "cold_reason": null, "app_data_cleared": false,
             "system_page_cache_dropped": false, "shader_compile_pause_ms": null,
             "warmup_frames_discarded": 60 },
  "samples": [
    { "index": 0, "ts_ns": 1000000000, "game_frame_id": 0, "new_game_frame": true,
      "presented_content": "game", "input_to_present_ns": null, "notes": null }
  ]
}
```

> 上例只展示字段形状，**不是**任何一次真实采集，不得作为测试数据引用。

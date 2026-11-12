# 1.0.9 一次性真机验收方案

状态：待执行（三条实现线交回后一次性跑完，不再分批碰设备）
产物目标：一份 APK，versionCode 9 / versionName 1.0.9，签名证书 d11967a31fcef92f3d6b44adf9d9678c8d2b07bf8f3246d51a791617ed61f36e
适用设备：RG477M01445122（Mali-G615，MT6897）；A42BBB5109000333（Honor GT Pro，Adreno，**需用户许可后才可使用**）

---

## 0. 硬约束

1. **只装不卸**：`adb install -r`，两台设备签名一致，不需要卸载，因此存档与数据目录不受影响。
2. **不测性能时不带任何观察者**：禁用 `gfx-flame-diag`、`screenrecord`、逐帧打印。计时只走缓冲 CSV 的 `monotonic_ns` 与引擎自带的 60 帧窗口。
3. **冷/暖分开测**：每台设备至少两轮（清缓存 = 冷、保留 = 暖），分别记录，不合并。
4. **取证前对齐 pid 与采集时刻**：设备日志会被后续会话覆盖，跨会话行不得混用。
5. **平板已归还用户**：未获明确许可前，任何 worker 与总控都不得碰 A42BBB5109000333。
6. **477M 设备独占**：在 texel 线交回之前，总控不碰 RG477M01445122。

---

## 1. 477M 掌机（首要目标：进得去游戏）

| 步骤 | 动作 | 通过判据 | 证据 |
|---|---|---|---|
| 1.1 | `adb -s RG477M01445122 install -r <1.0.9 apk>` | `Success`，未卸载 | install 输出 |
| 1.2 | 冷启动 App，进标题 | **无 `FATAL: Texel format lacks formatless-write support`**；`engine.log.fatal` 不存在；日志出现 `NVN texel format=98 bufferFeatures=...` 且不再是致命路径 | guest_boot.log 全文 |
| 1.3 | 标题画面 | `render_tier=1280x720 operation_mode=handheld`、`target_fps=30`、4:3 pillarbox（`bars_top=120 bars_bottom=120 stretch=no`） | 同上 |
| 1.4 | 进新游戏 → 地图 → 一场战斗 | 能进场、能出招、召唤兽能出 | 截图 + 帧 CSV |
| 1.5 | 存档完整性 | 4 个 live save 的 SHA-1 与 `.local/rg477m-adapt/backup-20261112/` 逐字节一致，时间戳未变 | sha1sum 对比 |

**通过定义**：1.1–1.5 全部满足。若 texel 修复判定为 BLOCKED，则 1.2 失败，必须写明 477M 的可接受目标深度（进标题 / 进战斗 / 只进菜单），由用户拍板，不允许用「能进标题」冒充「游戏可玩」。

---

## 2. 战斗特效卡顿（Honor 平板，需用户许可）

### 2.1 基线（已存档，可直接比对）

`codex-frame-7742.csv`（pid 7742，1.0.7）：

| 指标 | 基线 |
|---|---|
| 帧数 / 时长 | 7260 / 295.16 s |
| 平均 FPS | 24.59 |
| 稳态中位帧间隔 | 33.34 ms（= 30.0 FPS） |
| 稳态占比 | 93.0% |
| >100 ms 尖峰个数 | 93 |
| 尖峰总耗时 | 40.1 s（13.6%） |
| 最坏帧 | #29 = 6506 ms，#6037 = 2691 ms，#3690 = 2328 ms |

### 2.2 判定口径（与新引擎指标一一对应）

| 指标 | 基线（慢窗 vs 快窗） | 通过判据 |
|---|---|---|
| `submit_cpu_ms` | 28.09 vs 13.47 | 慢窗 `submit_cpu_ms` 明显下降（目标 ≤ 20 ms/帧） |
| `sync_rb` / `sync_wb` | 9.80 / 10.40 vs 0.03 / 0.00 | 慢窗 drain 计数显著下降 |
| `created` / `create_max_ms` | 17.2 / 0.55 | 全程单条建管线 ≤ 100 ms（回归红线） |
| `gfx-event-retire` / readback 定向退休计数 | 新增 `targeted=` `slots=` `fallback=` | 目标路径命中为主，fallback 不应是主要路径 |
| 尖峰 | 93 个 / 40.1 s / 13.6% | 尖峰耗时占比 < 6% 且最坏帧 < 2 s |
| 稳态 | 33.34 ms | 稳态仍为 30.0 FPS，**不得因降载而变慢** |

### 2.3 行为不变量（红线，任一不满足即 FAIL）

1. 同一存档、同一路径、同一操作下，**过场段时长与 defer=0 参考相差 ≤ 5%**（只统计过场段，暖机尖峰与稳态分开计数）。
2. 每场景的 `draws` / `passes` / `dispatches` 与参考一致，**不得跳 draw**。
3. 特效精度不得下降，**不得全局关 depth**，不得引入占位 module。
4. 帧率提升不得改变战斗、动画、物理、音频、输入与过场速度。

---

## 3. 8E5 闪退（无设备，只做等价路径验证）

8E5 手机不在手边，**不写 8E5 PASS**。能做的是：

1. 在 477M 或平板上跑等价路径：召唤兽出场 → 击败对手 → 结算切场景，覆盖 P0-1（FinalizeEvent × WaitEvent UAF，触发条件含场景卸载）与 P0-4 / P0-5 涉及的代码路径。
2. 宿主契约：修复后的代码在 ASan/TSan 下跑宿主契约，确认不再触发 UAF（`verify_host.py` 的 event-finalize-asan / tsan 两步）。
3. 报告写 **NOT_RUN（8E5 真机）**，并附「要确诊还需要什么」清单。

### 3.1 向报告人索取的清单（缺一不可确诊）

| 项 | 为什么需要 |
|---|---|
| 崩溃类型：SIGSEGV / abort / ANR / 静默退出 | 决定是内存错误还是主动 fatal |
| `engine.log.fatal` 全文 | 引擎自己的 fatal 记录，直接指到断点 |
| logcat tombstone（`adb logcat -b crash -d`） | 原生崩溃栈 |
| 复现步骤：哪只召唤兽、第几回合、是否必现、是否只在 8E5 | 区分偶发与必然 |
| 当时的版本号与 APK 来源 | 判断是否与 1.0.7 特有 |

---

## 4. 证据归档

每台设备一个目录，路径 `.local/acceptance-1.0.9-<device>-<date>/`：

- `install.log`、`guest_boot.log`（含 pid 与起始时刻）、`engine.log`、`engine.log.fatal`（若有）
- `codex-frame-<pid>.csv`（原始，未做任何筛选）
- `frames.json`（`analyze_frames.py --baseline` 输出）、`windows.json`（`analyze_stalls.py` 输出）
- 截图（标题 / 地图 / 战斗 / 结算）
- `SHA256SUMS` 与设备侧原始文件的 sha256

**每个 PASS 必须挂上**：commit / dirty 状态、输入版本、build id、测试条件、原始日志路径、退出码、覆盖范围。禁止用恒真断言，禁止删测试改绿。

---

## 5. 当前状态（2026-11-12 23:50）

- 合流主干 `task/T-release-1.0.9-20261112` @ `ad2941b`：477M 分档 + 特效 warm::flush 修复 + seed 分发 + 事件定向退休 + 指标工具，**宿主契约 19/19 绿，APK 18/18 步构建通过**（预览包 sha256 `3d427ff777439a994451389e666521a2bd3a561bbd441915606125eecd7609fd`）。
- 尚未并入：readback 定向退休（transit 线）、Mali FATAL 修复（texel 线）、战斗结算闪退 P0（crash 线）。
- **本方案在三条线交回并合流后才执行。**
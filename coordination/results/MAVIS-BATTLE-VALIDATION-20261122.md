# 场景 C 验证报告：战斗掉帧归因（荣耀 GT Pro / 最稳定版 a45430a8）

日期：2026-11-22
执行：Mavis（task/T-mavis-perf-20261122，基线 9bff3f4）
设备：Honor ROD2-W69 / A42BBB5109000333（Adreno 735 / Android 16）
被测包：`FFRS-1.0.78-stable.apk`
SHA256：`a45430a8e4d5a9eb1510051b70836219e330c45128bf5d299ea17e7c15c2aaa8`
（= Round 9 `3f7c02e6` + GPASS 修复 `74e7b96`）

---

## 0. 结论先行

用户实机反馈的「战斗掉帧」在本包上**可复现、可量化**，并且**不是 GPU 瓶颈**。它由三层叠加：

| 层 | 现象 | 实测量级 | 归因 |
|---|---|---|---|
| ① 战斗稳态帧率 | 战斗菜单 31–33 FPS，探索区 39–40 FPS | 帧预算 25.3 ms → 31.3 ms | **CPU 侧每帧多 6 ms**：snapshot 整表重建 +1.96 ms、draw +1.82 ms、descriptor +1.56 ms（draws 526 → 1182 翻倍） |
| ② 战斗入场卡顿 | 战斗场景实例化期连续 74 帧 ~110 ms，其中单帧冻结 **24.96 s** | submit_cpu 24892 ms，同帧 GPU 仅 37 ms | **CPU 侧提交阻塞**（GPU 同期空闲） |
| ③ 战斗特效首帧 | 新特效首次出现时 65–146 ms 尖峰 | shader 编译 18.5–124 ms，新建管线 1–7 条 | 一次性着色器/管线创建成本，第二次不再出现 |

画面完整性（缺面 / 黑白闪）在本场战斗中**未发现自发缺陷**：225 帧引擎侧转储、25 段 × 9 连续呈现帧中，
唯一近白尖峰（2.66%）经目视确认为「撕咬」全屏粒子技能特效，**不是**画面缺陷。

---

## 1. 场景与进入方式

- 地点：第 1 章「探索的开端」废墟区（橙色水晶、断桥、雾）
- 敌人：变异鼠 Lv.2 ×2（顶部槽位 A / B）
- 我方：雷因、拉斯韦尔（战斗后拉斯韦尔 Lv.4 → Lv.5，破坏者，习得「敏锐」）
- **进入方式：读档续战**（隔离存档 `shield-regression-20261122073707` 重载后直接落在战斗中）
- 完整流程已跑通：战斗入场 → 指令菜单 → 目标选择 → 物理攻击 → 敌方「撕咬」全屏特效 → 胜利 → 升级结算

> ⚠️ 诚实边界：本场战斗是**读档进入战斗**，**不是**野外随机遇敌进入。
> 正常遇敌入场的卡顿幅度未单独实测（见 §6 未验证项）。

## 2. 取证通道（无任何诊断标记，干净基线）

- 引擎日志：`/sdcard/Android/data/org.ffrs.gamewindow/files/engine.log`
  - `NVN PERF v=1`（每 60 帧窗口的 fps / submit_cpu_ms / present_cpu_ms / gpu_ms / passes / draws）
  - `NVN PERF stall v=1`（长帧明细：gap_ms / submit_cpu_ms / gpu_ms / shader_ms / pipelines_created）
  - `NVN PERF parts_ms_per_frame`（**CPU 时间分解，本报告的根因证据**）
- 引擎侧帧转储：`gfx-live-framedump=9` → `files/frame-dump/f<present>.bin`（1280×720 RGBA）
- 采集脚本：`/tmp/battle_capture.py`（边抓边算，按 present 增量拉取，设备端目录不可删故不重复下载历史帧）
- 全程**未打开** `gfx-perf-sample` 等任何插桩标记（app 侧 `DM_GFX_PERF_SAMPLE=0`，保持关闭）
- 稳态基线取自**关闭帧转储**后的窗口，避免 3.7 MB/帧落盘污染 CPU 侧读数

## 3. 战斗入场（present 103900–103978）—— 最严重

连续 74 帧全部被记为 stall：

| 指标 | 范围 |
|---|---|
| gap_ms | 84.9 – 176.4（均值约 110） |
| submit_cpu_ms | 59.7 – 127.0 |
| gpu_ms | 36.2 – 72.6 |

峰值单帧：

```
present=103974  gap_ms=24957.4  submit_cpu_ms=24892.63  gpu_ms=37.34  shader_ms=0
```

对应两个 60 帧汇总窗：

| 窗口 | wall_ms | fps | submit_cpu_ms | present_cpu_ms | gpu_ms |
|---|---|---|---|---|---|
| 入场前段 | 4952.6 | 12.11 | 61.59 | 11.65 | 23.65 |
| 入场后段 | 31822.6 | **1.89** | **487.65** | 27.35 | 35.34 |

**判读**：CPU 侧卡了 24.9 秒，GPU 同期只忙 37 ms → 这不是 GPU 慢，是**提交路径把 CPU 卡住了**
（资源/管线实例化 + L-028 铁律要求的严格 fence 等待叠加）。此处的 `other_ms` 与 `submit_cpu_ms`
几乎相等，说明阻塞就发生在 submit 内部而非 present。

## 4. 战斗稳态与动作帧

**稳态（关闭帧转储，60 帧窗口）**

| 指标 | 探索区 | 战斗 |
|---|---|---|
| fps | 39.04 – 40.07 | 30.82 – 33.34 |
| submit_cpu_ms | 21.32 – 21.95 | 26.38 – 27.92 |
| present_cpu_ms | — | 0.79 – 1.06 |
| gpu_ms | 12.03 – 14.00 | 12.57 – 17.12 |
| passes | 93 | 66 |
| draws | 536 – 543 | 1171 |

**攻击动作帧**（present 110796–110799，连续 4 帧）：gap 76.8 / 94.3 / 87.2 / 90.9 ms，
submit_cpu 35.2–51.2 ms，gpu 39.7–40.7 ms，`sync_rb=1`。

**特效首帧管线/着色器编译**

| present | gap_ms | shader_ms | pipelines_created |
|---|---|---|---|
| 105309 | 145.9 | 124.0 | 4 |
| 105320 | 127.3 | 100.0 | 3 |
| 111120 | 67.2 | 28.3 | 2 |
| 111133 | 113.2 | 45.5 | 7 |
| 111135 | 64.6 | 18.5 | 1 |

## 5. 帧转储闪烁判据（225 帧 / 25 段 × 9 连续呈现帧）

present 107070 – 111938，每段 9 帧连续、逐段 `contig=8`。

| 指标 | 全窗口 | 常态段（24 段） | 异常段（round 22） |
|---|---|---|---|
| nearblack | 0.0056 – 0.0089 | 0.0056 – 0.0059 | 0.0089 |
| nearwhite | 0.0021 – 0.0266 | ≤ 0.0030 | **0.0266** |
| green | 0.0064 – 0.0403 | 0.0270 – 0.0403 | **0.0064 – 0.0119** |
| lap | 695.5 – 1312.5 | 1217.7 – 1312.5 | **695.5 – 793.4** |

round 22（present 111150–111158）已导出 PNG 目视核对：**「撕咬」技能全屏粒子**
（白色碎片 + 红色能量爆 + 伤害数字 1212 + TELE 预告），属**合法特效**，非缺面 / 非黑白闪。

> 判据沿用场景 A 的门槛（nearblack/nearwhite、green 缺面签名、相邻帧局部差分），
> 但 green 只作**同场景内**参考（探索区 0.0253 / 战斗菜单 0.030–0.040 / 撕咬特效 0.0064，
> 全部随画面内容变化），**不作为跨场景绝对阈值**。

## 6. 根因：战斗 vs 探索区 CPU 时间分解（parts_ms_per_frame，同为 60 帧窗口）

| 项 (ms/帧) | 探索 draws 526.5 | 战斗 draws 1182.1 | Δ |
|---|---|---|---|
| draw | 3.28 | 5.10 | **+1.82** |
| descriptor | 6.03 (643.2 次/帧) | 7.59 (1269.1 次/帧) | **+1.56** |
| **snapshot** | **2.83 (4050.5 次/帧)** | **4.79 (8153.1 次/帧)** | **+1.96** |
| other | 3.09 | 3.44 | +0.35 |
| image | 0.27 (2772.9) | 0.56 (6983.0) | +0.29 |
| cleanup | 1.11 | 1.13 | +0.02 |
| hash | 0.88 | 1.09 | +0.21 |
| pass | 0.39 | 0.42 | +0.03 |
| dispatch | 0.63 | 0.56 | −0.07 |
| 合计 | ≈ 21.5 | ≈ 27.5 | **+6.0** |

25.3 ms → 31.3 ms 的帧预算，对应 39.6 FPS → 32.0 FPS，与实测 39.0–40.1 → 30.8–33.3 完全吻合。

**结论**：战斗掉帧的稳态部分**完全由 CPU 提交成本随 draw 数翻倍造成**，GPU 侧（12–17 ms）
不构成瓶颈。三项 CPU 大头（snapshot 整表重建、draw、descriptor）恰好与既有优化排序
（P0 descriptor → P1 snapshot → P2 cleanup WAIT_BIT → P3 pass 合并）一致，
本轮为该排序补上了**战斗场景下的量化证据**。

## 7. 优化优先级（按本轮实测重排）

1. **P0 descriptor 路径** — 战斗 1269 次/帧、7.59 ms；探索 643 次/帧、6.03 ms。战斗把这项放大近一倍。
2. **P1 snapshot 每帧整表重建** — 战斗 8153 次/帧、4.79 ms；探索 4050 次/帧、2.83 ms。单帧绝对值最大。
3. **P2 cleanup 的 WAIT_BIT 同步阻塞 + 战斗入场 24.9 s 冻结** — 需单独追 fence 等待链
   （`XBG_WAIT fence=… target=…` 日志与 `target_fence == fence` 铁律的交互），
   **不得**用取消等待 / 去特效 / 降画质换帧数。
4. **P3 GPU pass 合并** — GPU 仅 12–17 ms，优先级确实最低。

## 8. 未验证 / 遗留

- **正常随机遇敌入场**未单独实测：本场战斗是读档续战进入。需在野外撞怪复测入场卡顿幅度。
- **场景 B（开场火场）**、**第四章兵工厂** 仍未实测。已把设备上的另一个存档槽
  （`codex-local-save/GameSaveData1`，11-15 写入）复制成隔离副本 `shield-regression-20261122231500`
  单独加载，结果**同样落在第 1 章「探索的开端」**——本机游戏进度就停在第 1 章，
  这两个场景在平板上目前不存在，需要先把游戏推进到相应章节或拿到对应存档。
- **RG477M 双机回归**未做（权限外）→ 本轮成果**不合入** `work/rg477m-native`。
- 设备端 `files/frame-dump/` 目录 shell/run-as 均无法删除（Android scoped storage），
  帧文件只能累积，需后续用应用自身权限或卸载时清理。
- 金手指「不遇敌」开关实测为**未启用**（SharedPreferences `ffrs-cheats.xml` 中该 build 的集合为空），
  且本包 APK 未内置 `assets/ffrs-cheats.json`，因此本轮无「不遇敌」干扰。

## 9. 复现步骤

```bash
# 1) 平板连接（本轮用独立端口 adb server 绕开损坏的 5037）
adb -P 5041 connect 192.168.1.121:44689
# 2) 进入隔离存档（该存档落在战斗中）
bash /tmp/ffrs-validate/enter_scene.sh shield-regression-20261122073707
# 3) 战斗稳定后取引擎侧性能（无需任何标记）
adb -P 5041 shell "grep 'NVN PERF v=1' /sdcard/Android/data/org.ffrs.gamewindow/files/engine.log | tail"
adb -P 5041 shell "grep 'NVN PERF stall' /sdcard/Android/data/org.ffrs.gamewindow/files/engine.log | tail"
adb -P 5041 shell "grep 'NVN PERF parts_ms_per_frame' /sdcard/Android/data/org.ffrs.gamewindow/files/engine.log | tail -1"
# 4) 帧转储（闪烁判据）
adb -P 5041 shell "printf '9' > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-live-framedump"
python3 /tmp/battle_capture.py 45
```

## 10. 证据归档

- `coordination/results/MAVIS-BATTLE-VALIDATION-20261122.md`（本报告）
- `.local/ffrs-validate/battle/`：`frame_metrics.csv`（战斗 225 帧指标）、
  `frame_metrics_explore.csv`（探索区 621 帧）、`engine_tail.log`（引擎日志切片）、
  `frames/impact_sheet.png`（撕咬特效目视核对图）、`frames/f*.png`（关键帧）

# 工单 P-OPT-RG477M-30hz-game-mode｜RG477M 专用：游戏模式稳定 30Hz

> **状态**：正式派工（新开短线程；禁止肥会话续聊）  
> **设备 ONLY**：Anbernic **RG477M** `adb -s RG477M01445122`（MT6897 / Mali-G615 MC6）。**本刀禁止碰荣耀 GT Pro / 平板 A42BBB5109000333。**  
> **工作树**：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen`  
> **分支**：`task/T02-codex-black-screen` @ `0444c81`（其上已有大量脏改与 untracked；**认领现有脏工作，勿 reset、勿另开平行树、勿漂主树 `work/`**）  
> **用户现场（Asia/Shanghai 2026-10-29）**：设备已连接；用户已切回 **游戏模式**（**不是**狂暴模式）。本刀验收必须在游戏模式下完成。  
> **用户优先级（硬）**：
> 1. **游戏模式稳定 ~30Hz present**（运动中稳；禁用狂暴模式数字冒充）
> 2. **音频不回退**——验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`（蓝牙 A2DP、NONE、fence_spin=0、DISCONNECTED 重开流等）
> 3. **视觉不回退**——无 UI 遮盖/异常叠层；建筑与特效正常；无新全黑/中央黑块；不复发 RG16 FATAL
> 4. **数据目录可达**——`resolveGameDir` / 三候选 / Paks / setup 屏；禁静默挂死
> 5. **遇阻塞先查行业/社区公开案例取经再改**（写 `OSS-NOTES.md`；无出处大改 = FAIL）  
> 6. **禁止本刀冲 40Hz / 60Hz**；禁止声称可玩；禁止双写；不 push

---

## 0. 你是谁 / 先占位再动手

你是写手（Antigravity / Codex / 同等均可）。刀号 **P-OPT-RG477M-30hz-game-mode**。

### 0.1 开工前必做所有权检查（读-only，5 分钟内完成）

1. `git -C .worktrees/codex-black-screen status -sb` + `git rev-parse --short HEAD`（期望仍 `0444c81` 之上脏树）。
2. 读真源（**不要**通读千行 HANDOFF）：
   - **本文件**
   - **`coordination/prompts/_STANDING-audio-acceptance.md`（常设音频硬门；必须完整阅读）**
   - 上一刀参考：`coordination/prompts/P-OPT-RG477M-40hz-audio.md`（本刀目标改为锁 30，不是抬 40）
   - 主树账本（若 worktree `.local` 无副本）：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/rg477m-perf-20261028/checkpoint.md`
   - `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`（`hasGame` / `candidateGameDirs` / `resolveGameDir` / `refreshSetup` / `startGuest`）
   - `src/runtime/hle/audio_bridge.c` + `audio_bridge.h`（确认仍为 `PERFORMANCE_MODE_NONE` 等救回点）
   - `src/graphics/nvn_vulkan/engine/xbg_game.cpp` 内 `xbg_refresh_target_fps`（**源码默认可能是 60，勿假设 30**；以日志 + `gfx-target-fps` marker 为准）
3. 查设备谁在前台、装了哪些包：
   - `org.ffrs.gamewindow`（用户主包；安装 SHA 以当场 `sha256sum` / `adb shell pm path` + 本地算为准）
   - `org.ffrs.perfisolated` / `org.ffrs.perfscale`（隔离实验包；**勿卸用户存档包**）
4. **若发现另一 agent 仍在写同一 worktree / 同一 `gamewindow` APK**（文件 mtime 刚变、checkpoint 仍在追加、用户说 AGY 还在跑）：**停下写 HANDOFF，不要双写**。优先：
   - 等用户确认 AGY 已停；或
   - 只在 **隔离包** `org.ffrs.perfisolated` / 新建隔离包上做本刀实验，**不覆盖**用户正在玩的 `org.ffrs.gamewindow`，直到用户明确授权装回主包。

### 0.1b 数据目录硬预检（装包后、测帧前必过）

本刀**不是**完整傻瓜安装器，但**硬预检必须过**，否则 30Hz/音频/视觉验收无意义。

复用现有钩子（先读再动，勿重写一套）：

- `GameActivity.java`
  - `hasGame(File)`：`main.nso` 可读 + `romfs/` 为目录
  - `candidateGameDirs()`：三候选（顺序）——`/sdcard/FFRS` → `getExternalFilesDir()/game` → `/data/local/tmp/ffrs-60fps`
  - `resolveGameDir()`：Intent extra `game_dir` 优先，否则扫三候选
  - `refreshSetup()` / `startGuest()`：找不到则中文 setup 引导；找到则启 guest

硬预检步骤：

1. 冷启本刀 APK（或确认当前主包/隔离包）；**禁止**只靠旧 adb 手工 `game_dir` 绕过验收。
2. `resolveGameDir` **必须非空**；`main.nso` 与 `romfs/` 可读；guest/logcat 打出实际 `game_dir=…`。
3. 关键容器在位（至少存在且非空体积合理）：
   - `romfs/FFRS/Content/Paks/FFRS-Switch.pak`
   - `romfs/FFRS/Content/Paks/FFRS-Switch.ucas`
   - `romfs/FFRS/Content/Paks/FFRS-Switch.utoc`
   - `romfs/FFRS/Content/Paks/global.ucas` / `global.utoc`（若缺，记入 RESULT，勿假装齐全）
4. 找不到数据：**必须**出 `refreshSetup` 引导屏；**禁止静默挂死 / 黑屏无提示**。
5. 若预检 FAIL：允许 §6.5 最小修复；完整「傻瓜安装 + 递归深扫整盘」**另刀**。

### 0.2 硬禁止

- 不 push、不 force、不改 `main`、不碰其他 worktree。
- **不声称可玩 / 通关 / 已达发行质量。**
- **本刀禁止冲 40Hz / 60Hz**；禁止部署 60FPS derived `game_dir`；禁止拿 interval=1 / 狂暴模式短窗当验收。
- **禁止用狂暴模式（或任何更高系统性能档）下的帧率数字冒充游戏模式验收。** 用户已切回游戏模式；本刀数字必须来自游戏模式。
- **不重做 RG16 设计 / LUT ABI / attachment proxy**（除非本刀复测新炸同款 FATAL 才只记证据）。
- **不双写**：不同时改隔离快照 + 共享 worktree 同一功能，不与另一活动 agent 抢 `gamewindow`。
- 不改用户存档路径内容；隔离实验用拷贝。
- 不把 title/present 计数当性能验收；必须 **运动中 + 音频连续（蓝牙）+ 截图/视觉**。
- **不得回退音频救回点**：默认 `PERFORMANCE_MODE_NONE`、合理缓冲、TIMEOUT 重试、DISCONNECTED 重开流、`fence_spin_us=0`。禁默认 `LOW_LATENCY`；禁为冲帧永久砍薄缓冲；禁 `fence_spin>0` 无蓝牙 A/B。
- 荣耀 / 自适应分辨率 / 高通+天玑双 SoC / PS5+NS / 完整傻瓜安装深扫：**本刀范围外**（只要求不要弄坏现有路径）。

### 0.3 游戏模式确认（验收前置）

用户明确：**已切回游戏模式，不是狂暴模式**。写手必须：

1. **开工与验收前**向用户确认（或在 RESULT 写明用户已确认）当前系统档为 **游戏模式**。
2. 若设备有系统性能档 UI（Anbernic / 厂商设置里的游戏模式 vs 狂暴/性能模式）：**只读确认，勿擅自改系统档**。
3. 在 RESULT 写清确认方式（用户口述 / 设置页截图路径 / 可读 prop 若有且可信）。无可机读 prop 时以用户确认为准。
4. **任何在狂暴模式下采到的 fps 数字一律作废**，不得写入 PASS 证据。

---

## 1. 现场事实（开工时再核，勿当永恒真理）

| 项 | 已知（Asia/Shanghai 2026-10-29 ~15:04 核） |
|---|---|
| 设备 | `RG477M01445122` **device**；`wm size` **1280x960** dens 280；model RG477M |
| 系统档 | 用户已切 **游戏模式**（非狂暴）；验收必须在此档 |
| 工作树 | `.worktrees/codex-black-screen` @ `0444c81` + 大量 dirty/untracked |
| 源码默认 | `xbg_refresh_target_fps` 内 `target_fps = 60`（**不是 30**）；`FFRS_TARGET_FPS` / marker `gfx-target-fps` 可覆盖；`0`=去掉主机 limiter |
| 设备 marker（核时） | `org.ffrs.gamewindow` 与 `org.ffrs.perfisolated` 均见 `gfx-target-fps=40`、`gfx-fence-spin-us=0`——本刀应把目标锁回 **30** 并验证 |
| 音频 | 2026-10-29 已救回（用户确认蓝牙顺）；常设见 `_STANDING-audio-acceptance.md`；源码侧 `PERFORMANCE_MODE_NONE` |
| 上一刀 | P-OPT-40hz-audio：音频 PASS；40Hz **未完成**（曾见同窗 ~21fps）。**本刀不继续冲 40**，改为游戏模式稳 30 |
| AGY 账本 | 主树 `.local/rg477m-perf-20261028/checkpoint.md`（worktree `.local` 可能无副本——读主树路径） |
| 包 | `org.ffrs.gamewindow` / `perfisolated` / `perfscale` / `diagnostic` |

---

## 2. 本单目标（一句话）

在 **RG477M only + 游戏模式** 上：

0. **数据目录硬预检 PASS**（§0.1b）；  
1. **验收必须完整包含 `_STANDING-audio-acceptance.md`**（蓝牙野外/战斗各 ≥2 分钟等；任一项不过 = 整刀 FAIL）；  
2. **视觉不回退**（§5.3）；  
3. 把 present **锁稳在 ~30Hz**（运动中 p50≈30；无长时间明显卡顿；游戏速度正常）；  
4. 分辨率保持 **720P 口径**（面板 1280x960 可接受；勿升 1080）；  
5. 写清回滚点；**不 push**；**不声称可玩**。

**验收口径**：  
- 加载不到数据 = **未开干 / FAIL**  
- 音频常设清单任一项不过 = **整刀 FAIL**  
- 视觉回退（UI 遮盖 / 建筑特效缺失 / 新黑屏 / RG16 FATAL）= **整刀 FAIL**  
- 用狂暴模式数字、或冲到 40/60 当本刀成功 = **FAIL**  
- 帧率「够高」但声音断续 / 视觉坏 = **FAIL**  
- 无 `OSS-NOTES.md` 出处的大改 / 空口「业界都这样」= **FAIL**

---

## 3. 方法：先查行业/社区案例，再锁 30、保音频、保视觉（勿抬帧）

### 3.1 必做调研（硬门；写进 OSS-NOTES；禁止空口「业界都这样」）

**遇到任何阻塞都必须先查行业案例与社区公开案例取经，再改代码。** 包括但不限于：帧不稳、音频回退、视觉回退、数据目录加载失败、限帧 marker/env 无效、游戏模式 vs 性能档调度差异、AAudio underrun、fence/spin 副作用。

先扫（只读，勿大 clone 改别人仓库）：

- **同路线静态重编译 / NVN→Vulkan Android**：`seedds/nxnative`、`d2r-mobile/d2r-mobile`（frame pacing / present / writeback；checkpoint 已引用过 d2r `a8782caa…`，可续读）  
- **Switch 模拟器音频与帧步进**（思路借鉴，非照搬）：Yuzu / Sudachi / Suyu / Ryujinx 的 audio sink、stretch、underrun、frame limiter  
- **Android AAudio**：Google AAudio underrun / `PERFORMANCE_MODE_LOW_LATENCY` vs `NONE`/`POWER_SAVING`、burst/capacity、设备变更与 `DISCONNECTED`  
- **Unreal on Mali**：frame pacing、动态分辨率坑（AGY resolution50 已踩过 symlink 权限——**不要重复**）  
- **手持/社区公开笔记**（相关则引用）：Anbernic / RG 系列性能模式 vs 游戏模式、热节流与帧抖动讨论；相关 GitHub issues / PR / 公开帖（必须带 URL）

调研产出 **必须先写**：`.local/rg477m-opt-30hz-game-mode/OSS-NOTES.md`（URL/commit/issue 引用）。**无出处的大改 = FAIL**；提出风险改动前 OSS-NOTES 必须已落盘。

### 3.2 推荐步骤（按序；每步复测音频+视觉）

1. **确认游戏模式**（§0.3）+ **数据目录预检**（§0.1b）。  
2. **§3.1 调研落盘 OSS-NOTES**（遇阻必先补查再改；开干前至少有一版笔记）。  
3. **读清当前限帧真相**：  
   - 源码默认可能是 **60**；  
   - 设备 marker 核时曾是 **40**；  
   - guest 日志应打出实际 target（或自行加临时日志）；**以实测为准，勿假设 30**。  
4. **锁到 30**：优先安全手段——  
   - 写/改 `gfx-target-fps` → **30**（主包或隔离包 files 目录）；和/或  
   - `FFRS_TARGET_FPS=30`；和/或  
   - 若本刀必须改源码默认：把默认从 60 改为 30（**最小 diff**；可 marker 覆盖）；  
   - 清掉残留的「冲 40/去 limiter（0）/冲 60」配置，避免游戏模式被旧 marker 带跑。  
5. **A/B**：30 vs 当前残留 40/60/0；同一场景运动中采 `gfx-codex-frame-metrics` + `scripts/graphics/analyze_frame_metrics.py`（p50/p95/max、>100ms）。  
6. **每步强制**：完整勾选 `_STANDING-audio-acceptance.md` + 视觉 checklist；音频一坏立刻回滚，禁止继续拧。  
7. **禁止**：假信号 fence、砍必要 wait、默认 LOW_LATENCY、`fence_spin>0` 无蓝牙 A/B、60FPS derived NSO、为冲帧砍音频缓冲；**禁止无 OSS-NOTES 出处的大改**。

### 3.3 视觉抽检节奏

每步装包/改 marker 后：标题 → Continue/进野外 → 战斗；截图归档；对照有无 UI 遮盖、建筑缺失、特效消失、新黑屏。

---

## 4. 必查文件 / 目录

```
coordination/prompts/_STANDING-audio-acceptance.md   # 常设音频硬门（强制）
coordination/prompts/P-OPT-RG477M-40hz-audio.md       # 上一刀参考（本刀改锁30）
src/android/app/java/org/ffrs/diagnostic/GameActivity.java
src/android/native/guest_boot_jni.cpp
src/runtime/hle/audio_bridge.c
src/runtime/hle/audio_bridge.h
src/graphics/nvn_vulkan/engine/xbg_game.cpp          # xbg_refresh_target_fps（默认或为60）
src/graphics/nvn_device/nvn_window.c
src/graphics/nvn_device/nvn_queue.c
scripts/graphics/analyze_frame_metrics.py
scripts/graphics/prepare_ffrs_60fps.py               # 只读；本刀禁用 derived 部署
/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/rg477m-perf-20261028/checkpoint.md
```

设备侧（读）：

```
/sdcard/Android/data/org.ffrs.gamewindow/files/gfx-target-fps
/sdcard/Android/data/org.ffrs.gamewindow/files/gfx-fence-spin-us
（及 perfisolated 对应路径）
```

新建本刀证据目录：

```
.local/rg477m-opt-30hz-game-mode/
  mode-confirm.md          # 游戏模式确认方式与证据
  OSS-NOTES.md             # 强制：URL/commit 引用；无出处大改=FAIL
  baseline-30hz/           # 游戏模式 30 限帧对照
  ab-vs-40-or-60/          # 与残留 40/60/0 的 A/B
  screenshots/
  RESULT.md                # 最终结论（可中文）
```

---

## 5. 验收测试（全过才算本刀阶段性成功）

> 用户要求：**「音频视频我提到的那些问题都要避免」「所以都要写进去」。**  
> 下列各节 **全部强制**；不可只测帧率。

### 5.0 数据目录（硬预检 / 回归门）

- [ ] 冷启后 `resolveGameDir` 非空；日志有 `game_dir=…`  
- [ ] `main.nso` 可读 + `romfs/` 为目录（`hasGame` 语义）  
- [ ] `romfs/FFRS/Content/Paks/` 关键容器在位；缺哪个写进 `RESULT.md`  
- [ ] **未破坏**三候选路径顺序与语义，也未破坏 Intent extra `game_dir` 覆盖  
- [ ] 故意移走/找不到数据时：**必须仍出** `refreshSetup` 引导屏；**禁止静默挂死 / 黑屏无提示**  
- [ ] 本刀若做了最小修复：只限补候选路径 / 存储权限引导 / Paks 完整性提示——**禁止**产品化递归深扫整盘

### 5.1 音频（硬门｜常设清单强制）

> **验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。**  
> 清单任一项未过 = **整刀 FAIL**。下列与常设清单 **叠加**，不替代。

- [ ] 已完整阅读并 **逐项勾选** `_STANDING-audio-acceptance.md`：  
  - 默认 AAudio `PERFORMANCE_MODE_NONE`（禁默认 `LOW_LATENCY`）；guest 日志 `performance=0`  
  - **蓝牙耳机（A2DP）** 野外/战斗各 ≥2 分钟主观连续无断续；仅扬声器顺滑不算过  
  - 运行中连接/断开蓝牙：自动重开流；禁因 `DISCONNECTED(-899)` 永久停音频或拖死 UE tick  
  - `fence_spin_us` / `gfx-fence-spin-us` 默认 **0**；非 0 须蓝牙 A/B，音频不过则回滚  
  - AAudio write：`TIMEOUT` 可重试；禁「write failed → 停 worker / 拒收满队列」  
  - 缓冲不得为冲帧永久砍薄；改 `gfx-target-fps` / 限帧 **每步** 复测蓝牙音频  
  - `xruns=0` 不能单独当 PASS；人耳断续即 FAIL  
  - 记录包名 + APK SHA；确认桌面图标不是旧包  
  - 音频一坏：立刻回 30 限帧或已知顺滑 APK；复测 PASS 前禁止继续拧帧  
- [ ] guest 日志：`AAudio write failed` = 0；`released void overflow` = 0  
- [ ] `xruns` 不在短时窗内持续单调大增（记起止差值）

### 5.2 游戏模式 + 帧率 / 呈现（本刀核心）

- [ ] **已确认系统档为游戏模式**（§0.3）；RESULT 写明确认方式  
- [ ] **未使用**狂暴模式（或更高性能档）下的数字作为 PASS 证据  
- [ ] 运动中 present **稳 ~30Hz**（p50≈30；允许短抖；无长时间掉到明显卡顿；记冷/暖）  
- [ ] 实际生效限帧为 30（marker / env / 源码默认之一；日志可证）；残留 40/60/0 已清或已证明被 30 覆盖  
- [ ] **不是** title 屏、不是纯 present 计数吹牛  
- [ ] 游戏速度未明显加速/减速（禁止靠拨 clock / 狂暴档假 FPS）  
- [ ] **本刀未冲 40 / 未冲 60**；未部署 60FPS derived

### 5.3 视觉 / 功能（用户点名必须避免的问题）

- [ ] 无 UI 遮盖、无异常 overlay / 叠层  
- [ ] 建筑与特效正常出现（至少野外 + 战斗各一张对比图，路径写入 RESULT）  
- [ ] 无新的全黑 / 中央黑块  
- [ ] 旧 **RG16 FATAL** 不复发  
- [ ] 分辨率仍为 RG477M 当前 **720P 口径**路径（面板 1280x960 可接受）

### 5.4 行业/社区取经（硬门）

- [ ] `.local/rg477m-opt-30hz-game-mode/OSS-NOTES.md` **已写**，含具体 URL / commit / issue 引用  
- [ ] 本刀每一处风险改动（限帧、音频、同步、数据目录最小修复等）在 OSS-NOTES 或 RESULT 中有出处；**无出处的大改 = FAIL**  
- [ ] 遇阻塞时先查案例再改（§3.1）；未出现空口「业界都这样」

### 5.5 工程纪律

- [ ] 未 push；未双写；未动荣耀 / 未做自适应分辨率 / 未做双 SoC / 未做 PS5+NS / 未做完整傻瓜安装深扫  
- [ ] **未声称可玩**  
- [ ] 若改坏：能明确步骤回到「音频顺滑 + 游戏模式 30 限帧」；数据目录回归仍过 §5.0  
- [ ] `RESULT.md` + 短 HANDOFF 条目写完（含：游戏模式确认、实际 `game_dir`、Paks、音频常设勾选摘要、30Hz 数字、APK SHA、截图路径、OSS-NOTES 摘要）

---

## 6. 回滚策略（写死）

1. **音频一坏就停**：立刻恢复已知顺滑配置（`gfx-target-fps`→30、`fence_spin`→0、NONE 缓冲策略、或已知顺滑 APK SHA）；复测常设音频清单 PASS 前禁止继续改。  
2. 帧率不稳但音频/视觉绿：可停在「游戏模式 + 音频 PASS + 视觉 PASS + 尽力 30」并写明未达点——**可接受停点**；仍禁止改去冲 40/60。  
3. 隔离实验优先：`org.ffrs.perfisolated`；确认后再授权覆盖 `org.ffrs.gamewindow`。  
4. 源码 diff 保持最小；限帧优先用 marker/env，改默认须可回滚。  
5. 禁止「为了稳帧永久砍音频缓冲」；宁可略低于 30 但音频/视觉完整，不要破音或黑屏。

### 6.5 数据目录现场已挂时的最小修复（允许 / 禁止）

**允许（本刀）：**

- 补/修正 `candidateGameDirs` 合理候选，保持三候选 + Intent `game_dir` 语义；  
- 修好存储权限引导与 `refreshSetup` 文案；  
- 启动前检查 `Content/Paks` 关键文件缺失并在 setup/日志里**点名缺哪个**。

**禁止（另刀）：**

- 完整傻瓜安装器产品化、递归深扫整盘、NSP 自动解包流水线；  
- 荣耀 / 自适应分辨率 / 双 SoC / PS5+NS；  
- 本刀范围内的冲 40/60。

---

## 7. 完成时交付

1. `.local/rg477m-opt-30hz-game-mode/OSS-NOTES.md`（强制；URL/commit 引用）  
2. `.local/rg477m-opt-30hz-game-mode/RESULT.md`（游戏模式确认、做了什么、测了什么、PASS/FAIL、APK SHA、marker、音频常设勾选、视觉截图、30Hz 数字、OSS 出处摘要）  
3. `coordination/HANDOFF.md` 顶部追加本刀摘要（中文可）  
4. 可接受停点示例：「游戏模式确认 / 音频常设 PASS / 视觉 PASS / 30Hz 基本稳（或未完全稳但已锁 30）/ 下一刀建议」  
5. 停；等用户派下一刀（傻瓜安装/深扫、自适应分辨率、荣耀、双 SoC、PS5+NS、或再议 40Hz）

---

## 8. 粘贴开场（给下一写手）

见派工时用户消息；核心必须点名：

- RG477M only；用户已切 **游戏模式**（非狂暴）；目标 **稳 ~30Hz present**；**禁冲 40/60**；狂暴模式数字作废。  
- **遇问题先查行业/社区公开案例取经再改**（§3.1）；写 `OSS-NOTES.md`（URL/commit）；无出处大改 = FAIL。  
- **验收必须完整包含** `coordination/prompts/_STANDING-audio-acceptance.md`（蓝牙 / NONE / fence_spin=0 / DISCONNECTED 重开流等）。  
- **视觉验收**：无 UI 遮盖、建筑特效正常、无新黑屏、不复发 RG16 FATAL。  
- **数据目录硬预检**：`resolveGameDir` / 三候选 / Paks / setup 屏；禁静默挂死。  
- 源码 `xbg_refresh_target_fps` 默认可能是 **60**；设备 marker 或为 **40**——先核实再锁 **30**。  
- 先查所有权防双写；不声称可玩；不 push。

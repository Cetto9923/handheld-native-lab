# 工单 P-OPT-RG477M-40hz-audio｜RG477M 专用：先救音频，再稳 720P/40Hz

> **状态**：正式派工（新开短线程；禁止肥会话续聊）
> **设备 ONLY**：Anbernic **RG477M** `adb -s RG477M01445122`（MT6897 / Mali-G615 MC6）。**本刀禁止碰荣耀 GT Pro / 平板 A42BBB5109000333。**
> **工作树**：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen`
> **分支**：`task/T02-codex-black-screen` @ `0444c81`（其上已有大量脏改与 untracked；**认领现有脏工作，勿 reset、勿另开平行树、勿漂主树 `work/`**）
> **用户优先级（硬）**：
> 1. **进游戏必须能加载到数据**（`resolveGameDir` 可达；找不到数据 = 本刀未开干）——完整傻瓜安装 / 深扫整盘另刀
> 2. **音频不卡**（30Hz 时代流畅音频 = 基线；帧率上去但声音断续 = **FAIL**）
> 3. **可玩性 / 视觉正确** > 帧率数字
> 4. 本机目标：**720P 口径 + 稳定 ~40Hz present**（面板实测 `wm size`=1280x960；勿擅自冲 1080P）
> 5. **禁止本刀冲 60Hz**（60 仅留给日后 8Gen3 探路，未知能否稳）

---

## 0. 你是谁 / 先占位再动手

你是写手（Antigravity / Codex / 同等均可）。刀号 **P-OPT-RG477M-40hz-audio**。

### 0.1 开工前必做所有权检查（读-only，5 分钟内完成）

1. `git -C .worktrees/codex-black-screen status -sb` + `git rev-parse --short HEAD`（期望仍 `0444c81` 之上脏树）。
2. 读真源（**不要**通读千行 HANDOFF）：
   - 本文件
   - `.local/rg477m-perf-20261028/checkpoint.md`（Antigravity 2026-10-28 RG477M 性能实验账本；**LEDGER.md 当前缺失**，以此为准）
   - `.local/rg477m-perf-20261028/motion2/RESULT.md`、`motion3/RESULT.md`
   - `coordination/HANDOFF.md` **仅顶部 + 搜** `audio` / `60FPS` / `stutter` / `xruns`
   - `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`（`hasGame` / `candidateGameDirs` / `resolveGameDir` / `refreshSetup` / `startGuest`）
   - `src/runtime/hle/audio_bridge.c` + `audio_bridge.h`
   - `src/graphics/nvn_vulkan/engine/xbg_game.cpp` 内 `xbg_refresh_target_fps`（默认 30；marker `gfx-target-fps`）
3. 查设备谁在前台、装了哪些包：
   - `org.ffrs.gamewindow`（用户主包；安装 SHA 曾变，**以当场 `sha256sum` 为准**；2026-10-29 ~12:31 Asia/Shanghai 曾见 SHA `ac34ba1511949538849bf93ca2155b294db8924658b7d03a7128158a59107bca`）
   - `org.ffrs.perfisolated` / `org.ffrs.perfscale`（AGY 隔离实验包；**勿卸用户存档包**）
4. **若发现另一 agent 仍在写同一 worktree / 同一 `gamewindow` APK**（文件 mtime 刚变、checkpoint 仍在追加、用户说 AGY 还在跑）：**停下写 HANDOFF，不要双写**。优先：
   - 等用户确认 AGY 已停；或
   - 只在 **隔离包** `org.ffrs.perfisolated` / 新建隔离包上做本刀实验，**不覆盖**用户正在玩的 `org.ffrs.gamewindow`，直到用户明确授权装回主包。

### 0.1b 数据目录硬预检（装包后、抬帧前必过；用户 2026-10-29 缺口）

用户确认：**进入游戏后要能自动找到游戏数据目录**；现场曾「打开加载不到数据」。本刀**不是**做完整傻瓜安装器，但**硬预检必须过**，否则音频/40Hz 验收无意义。

复用现有钩子（先读再动，勿重写一套）：

- `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`
  - `hasGame(File)`：`main.nso` 可读 + `romfs/` 为目录
  - `candidateGameDirs()`：三候选（顺序）——`/sdcard/FFRS` → `getExternalFilesDir()/game` → `/data/local/tmp/ffrs-60fps`
  - `resolveGameDir()`：Intent extra `game_dir` 优先，否则扫三候选
  - `refreshSetup()` / `startGuest()`：找不到则中文 setup 引导；找到则启 guest

硬预检步骤：

1. 装本刀 APK（或确认当前主包/隔离包）后冷启；**禁止**只靠旧 adb 手工 `game_dir` 绕过验收。
2. `resolveGameDir` **必须非空**；`main.nso` 与 `romfs/` 可读；guest/logcat 打出实际 `game_dir=…`。
3. 进标题 / Continue 前确认关键容器在位（至少存在且非空体积合理）：
   - `romfs/FFRS/Content/Paks/FFRS-Switch.pak`
   - `romfs/FFRS/Content/Paks/FFRS-Switch.ucas`
   - `romfs/FFRS/Content/Paks/FFRS-Switch.utoc`
   - `romfs/FFRS/Content/Paks/global.ucas` / `global.utoc`（若缺，记入 RESULT，勿假装齐全）
4. 若预检 FAIL：先做 **§6.5 最小修复**，再谈音频/40Hz；完整「傻瓜安装 + 递归深扫整盘」**另刀**，本刀不做。

### 0.2 硬禁止

- 不 push、不 force、不改 `main`、不碰其他 worktree（`p-mm-*` / `t04-*` / 主树业务）。
- **不声称可玩 / 通关 / 已达发行质量。**
- **不冲 60Hz / 不部署 60FPS derived `game_dir` / 不拿 interval=1 当 60 证据**（历史：WSI 常忽略 present interval；derived NSO ≠ 真 60）。
- **不重做 RG16 设计 / LUT ABI / attachment proxy**（已越过；除非本刀复测新炸同款 FATAL 才只记证据）。
- **不双写**：不同时改隔离快照 + 共享 worktree 同一功能，不与另一活动 agent 抢 `gamewindow`。
- 不改用户存档路径内容；隔离实验用拷贝。
- 不把 title/present 计数当性能验收；必须 **运动中 + 音频连续 + 截图/视觉**。
- 荣耀 GT Pro / 自适应分辨率 / 高通+天玑双 SoC 打包 / PS5+NS 手柄深化：**本刀范围外**（只要求 **不要弄坏现有路径与手柄已通部分**）；另刀再做。
- **完整傻瓜安装器产品化 / 递归深扫整盘找数据**：**本刀范围外**。但 **数据目录可达是硬预检**（见 §0.1b）：不得破坏三候选 + Intent `game_dir`；现场已挂允许 §6.5 最小修复。

---

## 1. 现场事实（开工时再核，勿当永恒真理）

| 项 | 已知 |
|---|---|
| 设备 | `RG477M01445122` online；`wm size` 1280x960 dens 280 |
| 工作树 | `.worktrees/codex-black-screen` @ `0444c81` + 大量 dirty/untracked（含 `audio_bridge.*`、`xbg_game.cpp`、nvn_*） |
| AGY 账本 | `.local/rg477m-perf-20261028/checkpoint.md`（止于 resolution50 / UECommandLine 50% 实验） |
| 运动基线 | motion2 ~**7.89** fps（短窗）；motion3 ~**29.67** fps（短窗、不同场景）——**都不是 40Hz 验收**，且 **未测音频卡顿** |
| 主机限帧 | `xbg_refresh_target_fps` 默认 **30**；`gfx-target-fps` marker / `FFRS_TARGET_FPS` 可改；`0`=去掉主机 limiter（≠自动 40） |
| 音频栈 | `audio_bridge.c`：AAudio SHARED + `LOW_LATENCY` + PCM_I16；日志带 `xruns=`；历史 previous-guest 里多见 `xruns=0`，**用户主观断续仍可能发生**（调度/缓冲/与 GPU 抢核） |
| 用户投诉（硬） | Antigravity 最近冲帧率后：**声音断断续续**；现在 **帧率也不高了**。30Hz 时音频不卡 = 回归基线 |
| 产品后续（勿本刀做整包） | 高通+天玑双 SoC；完整傻瓜安装/深扫；自适应分辨率；GT Pro 1080P/40（60 后探）；PS5+NS 深化。**本刀只吃**：数据目录硬预检 + RG477M 音频→720P/40 + 视觉；另项另刀 |

---

## 2. 本单目标（一句话）

在 **RG477M only** 上：

0. **数据目录硬预检 PASS**（§0.1b）：能自动经现有候选/`game_dir` 找到数据并进到可测画面；否则先最小修复，再谈后面；  
1. **先恢复「30Hz 时代」级别的连续音频**（战斗/过场/野外走动 ≥2 分钟无主观卡顿；日志 `xruns` 不持续爬升；无 `AAudio write failed`）；  
2. 在音频不回退的前提下，把 **稳定 present 抬到 ~40Hz**（运动中短测不够；至少野外走动 + 战斗各一段，冷/暖分开记）；  
3. **视觉验收**：无 UI 遮盖/异常叠层；建筑与特效正常加载；无新全黑/中央黑块回潮；  
4. 分辨率保持用户口径 **720P / 当前已知可玩路径**（勿升 1080）；  
5. 写清回滚点与下一刀；**不 push**。

**验收口径**：加载不到数据 = **未开干 / FAIL**；画面/帧率过了但声音断续 = **整刀 FAIL**。完整傻瓜安装 / 深扫另刀。

---

## 3. 方法：先调研成熟做法，再拧旋钮

### 3.1 必做调研（写进 scratch，引用具体 URL/commit；禁止空口「业界都这样」）

先扫（只读，勿大 clone 改别人仓库）：

- **同路线静态重编译 / NVN→Vulkan Android**：`seedds/nxnative`、`d2r-mobile/d2r-mobile`（尤其 frame-overlap / writeback / present 笔记；checkpoint 已引用过 d2r `a8782caa…`，可续读）  
- **Switch 模拟器音频与帧步进**（思路借鉴，非照搬）：Yuzu / Sudachi / Suyu / Ryujinx 的 audio sink、stretch、underrun 处理  
- **Android AAudio**：Google AAudio underrun / `PERFORMANCE_MODE_LOW_LATENCY` vs `POWER_SAVING`、burst/capacity、回调 vs blocking write  
- **Unreal on Mali**：frame pacing、`r.ScreenPercentage`、动态分辨率坑（AGY resolution50 已踩过 symlink 权限坑——**不要重复**）

调研产出写到：`.local/rg477m-opt-40hz-audio/OSS-NOTES.md`（新建目录）。

### 3.2 假设排序（按此顺序证伪，勿一上来狂改）

**A. 音频优先回归**

1. 确认当前主包/隔离包实际 `gfx-target-fps` / `FFRS_TARGET_FPS` / 任何 AGY 帧节奏改动。  
2. **回滚策略（默认）**：先回到「音频顺滑」配置——通常是 **主机限帧 30**（或 AGY 改坏前的已知好 APK/标记组合），证明音频恢复。  
3. 仅在音频恢复后，再 **小步** 调到目标 40（例如 marker `40`，或等效 present pacing），每步都复测音频。  
4. 查 `audio_bridge.c`：`LOW_LATENCY` 在 Mali 手持 + 重 GPU 负载下是否导致 underrun；对比 `POWER_SAVING` / 增大 buffer capacity / 避免与 GPU 同核饿死写线程。改动必须有 A/B 日志（`xruns`、`written_frames` vs `played`、reject/overflow）。

**B. 帧率到 ~40（仅音频绿之后）**

1. 用已有 `gfx-codex-frame-metrics` + `scripts/graphics/analyze_frame_metrics.py` 做 **运动中** 统计（p50/p95/max、>100ms 次数）。  
2. 优先安全手段：去掉错误的主机限帧、减少同步空转、**已证明安全**的 defer/skip（checkpoint 明确：**flags 0xe2 = CPU_UNCACHED，禁止套用 D2R CPU_NO_ACCESS-only writeback skip**）。  
3. 禁止：假信号 fence、砍必要 wait、未证明的 clear-overwrite、60FPS derived NSO。

**C. 视觉**

每步装包后：标题 → Continue/进野外 → 战斗；截图归档；对照有无 UI 遮盖、建筑缺失、特效消失、新黑屏。

---

## 4. 必查文件 / 目录

```
src/android/app/java/org/ffrs/diagnostic/GameActivity.java  # hasGame / candidateGameDirs / resolveGameDir / refreshSetup / startGuest
src/android/native/guest_boot_jni.cpp                 # NN_FS_ROM_PATH
src/runtime/hle/audio_bridge.c
src/runtime/hle/audio_bridge.h
tests/native/audio_out_contract.c
src/graphics/nvn_vulkan/engine/xbg_game.cpp          # xbg_refresh_target_fps
src/graphics/nvn_device/nvn_window.c                 # present_interval（注意：历史 WSI 可能忽略）
src/graphics/nvn_device/nvn_queue.c
scripts/graphics/analyze_frame_metrics.py
scripts/graphics/prepare_ffrs_60fps.py               # 只读；本刀禁用其 derived 部署
.local/rg477m-perf-20261028/checkpoint.md
.local/rg477m-perf-20261028/motion{2,3}/RESULT.md
.local/rg477m-perf-20261028/previous-guest.log       # 搜 [audio] / xruns
```

新建本刀证据目录：

```
.local/rg477m-opt-40hz-audio/
  OSS-NOTES.md
  baseline-audio/          # 30Hz 顺滑对照
  step-40hz/               # 抬到 40 的 A/B
  screenshots/
  RESULT.md                # 最终结论（可中文）
```

---

## 5. 验收测试（全过才算本刀阶段性成功）

### 5.0 数据目录（硬预检 / 回归门）

- [ ] 冷启后 `resolveGameDir` 非空；日志有 `game_dir=…`  
- [ ] `main.nso` 可读 + `romfs/` 为目录（`hasGame` 语义）  
- [ ] `romfs/FFRS/Content/Paks/` 关键容器在位；缺哪个写进 `RESULT.md`  
- [ ] **未破坏**三候选路径顺序与语义，也未破坏 Intent extra `game_dir` 覆盖  
- [ ] 故意移走/找不到数据时：**必须仍出** `refreshSetup` 引导屏；**禁止静默挂死 / 黑屏无提示**  
- [ ] 本刀若做了最小修复：只限补候选路径 / 存储权限引导 / Paks 完整性提示——**禁止**产品化递归深扫整盘

### 5.1 音频（硬门）

> **常设已知音频坑（强制）**：完整清单见同目录 `_STANDING-audio-acceptance.md`。  
> 后续一切抬帧/装包验收必须引用该文件；**清单任一项未过 = 整刀 FAIL**。  
> 引用句：「验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`」。  
> 下列条目与常设清单 **叠加**，不替代。

- [ ] 已完整阅读并勾选 `coordination/prompts/_STANDING-audio-acceptance.md`（蓝牙 A2DP + NONE + fence_spin=0 + DISCONNECTED 自愈等）  
- [ ] 野外走动 **≥2 分钟** 连续音频，无主观断续/机械抖动  
- [ ] 战斗或过场 **≥2 分钟** 同上  
- [ ] guest 日志：`AAudio write failed` = 0；`released queue overflow` = 0  
- [ ] `xruns` 不在短时窗内持续单调大增（记录起止差值）；若 xruns=0 但仍卡，记「调度/缓冲滞后」，继续查，不得宣称 PASS  
- [ ] 与「回滚到 30 限帧」对照：40 配置不得明显更卡

### 5.2 帧率 / 呈现

- [ ] 运动中 present ≈ **40Hz**（允许短抖；p95 合理；记冷/暖）  
- [ ] **不是** title 屏、不是纯 present 计数吹牛  
- [ ] 游戏速度未明显加速/减速（禁止靠拨 clock 假 FPS）

### 5.3 视觉 / 功能

- [ ] 无 UI 遮盖、无异常 overlay  
- [ ] 建筑与特效正常出现（至少野外 + 战斗各一张对比图）  
- [ ] 无新的全黑/中央黑块；旧 RG16 FATAL 不复发  
- [ ] 分辨率仍为 RG477M 当前 720P 口径路径（面板 1280x960 可接受）

### 5.4 工程纪律

- [ ] 未 push；未双写；未动荣耀 / 未做自适应分辨率 / 未做双 SoC 打包 / 未做 PS5+NS 深化（另刀）  
- [ ] 若改坏：能一键/明确步骤回到「音频顺滑」配置（30 限帧或上一好 APK SHA）；数据目录回归仍过 §5.0  
- [ ] `RESULT.md` + 短 HANDOFF 条目写完（含实际 `game_dir` 与 Paks 检查结果）

---

## 6. 回滚策略（写死）

1. **音频一坏就停抬帧**：立刻恢复 `gfx-target-fps`→30（或删除错误 marker）、或重装已知音频顺滑 APK SHA；复测音频 PASS 后再谈 40。  
2. 隔离实验优先：`org.ffrs.perfisolated` / 新包名；确认后再授权覆盖 `org.ffrs.gamewindow`。  
3. 源码 diff 保持最小；危险同步优化必须可 marker 关掉。  
4. 禁止「为了救帧率永久砍音频缓冲」；宁可 35–40 稳 + 音频顺，不要 45+ 破音。

### 6.5 数据目录现场已挂时的最小修复（允许 / 禁止）

**允许（本刀）：**

- 补/修正 `candidateGameDirs` 里合理候选（例如用户实际放置路径），保持三候选 + Intent `game_dir` 语义；  
- 修好「所有文件访问」/存储权限引导与 `refreshSetup` 文案，使找不到数据时仍有可操作引导；  
- 启动前检查 `Content/Paks` 关键文件缺失并在 setup/日志里**点名缺哪个**（完整性提示）。

**禁止（另刀）：**

- 完整傻瓜安装器产品化、递归深扫整盘/多卷、自动从 NSP 解包安装流水线；  
- 荣耀 / 自适应分辨率 / 高通+天玑双 SoC 打包 / PS5+NS 手柄深化。

---

## 7. 完成时交付

1. `.local/rg477m-opt-40hz-audio/RESULT.md`（做了什么、测了什么、PASS/FAIL、APK SHA、marker、音频与帧率数字、截图路径）  
2. `coordination/HANDOFF.md` 顶部追加本刀摘要（中文可）  
3. 若未达 40 但音频已恢复：明确写「音频门 PASS / 40Hz 未达 / 下一刀建议」——**这也是可接受停点**  
4. 停；等用户派下一刀（完整傻瓜安装/深扫、荣耀 1080P/40、自适应分辨率、双 SoC、PS5+NS 等）

---

## 8. 粘贴开场（给下一写手）

见同目录派工时用户消息；核心四句：

- RG477M only；**数据目录硬预检先过**（`resolveGameDir`/`hasGame`/三候选 + Intent `game_dir`；Paks 关键容器在位；找不到须 setup 屏，禁静默挂死）；完整傻瓜安装/深扫另刀。  
- **音频不卡是硬门**；目标 **720P + 稳 40Hz**；禁 60；荣耀 / 自适应分辨率 / 双 SoC / PS5+NS 另刀。  
- 先读本文件 + `.local/rg477m-perf-20261028/checkpoint.md`；先查所有权防双写。  
- 先数据预检（必要时 §6.5 最小修复）→ 调研 OSS/AAudio → 回滚到音频顺滑 → 小步抬到 40 → 视觉验收 → 写 RESULT。

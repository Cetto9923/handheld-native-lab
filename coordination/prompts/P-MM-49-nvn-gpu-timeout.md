# 工单 P-MM-49｜MiniMax Code｜GPU TIMEOUT_EXPIRED → 定性下一可玩阻塞

> **状态**：正式派工（P-MM-48 半成功之后的下一刀；**禁止**重做 RG16 / 帧率 / 读回 A/B）
> **工作树**：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen`
> **分支**：`task/T02-codex-black-screen` @ `0444c81`（其上已有 P-MM-48 脏改与 APK59 产物，**认领现有脏工作，勿 reset、勿另开平行树**）
> **设备**：`adb -s RG477M01445122`
> **用户优先级（硬）**：先能玩 → 再谈帧率。本单**禁止** 30/60 FPS、readback_cached、interval、adaptive、60FPS derived `game_dir`。
> **会话建议**：在 **新的空 MiniMax / Claude Desktop 会话**粘贴开场；旧会话 `bd1392e4-d70b-4ce9-b787-8996841a5e76`（标题「RG16 附件量化闪退」、实为 MiniMax-M3.1-Flash、cwd `sleepy-shamir` 空壳）可杀可留，**不要**继续往肥上下文里塞。

---

## 0. 你是谁 / 边界

你是 MiniMax Code 写手。本单主目标是 **定性**：把「进 3D 场景后 GPU interrupt wait TIMEOUT_EXPIRED」写成可移交的下一阻塞，并 **关闭 P-MM-48 / 打开 P-MM-49** 的 HANDOFF。  
**不 push、不改 main、不碰其他 worktree（`p-mm-*`）、不声称标题通关/可玩/游戏帧已稳。**  
不要调用 agy / Claude；不要重开 RG16 设计讨论。

**可选修复门槛（极严）**：仅当根因是现有 NVN 路径里 **一眼可见的一行级错误**（例如明确 `handle 0x0` 拒提后仍继续 wait、明显漏 fence/漏 retire、与 run49 同款「lost handle → TIMEOUT」且修复点已在源码旁写死）时，才允许最小 diff。否则 **只写 HANDOFF + 下一刀建议后停下**。

---

## 1. P-MM-48 半成功（先认账，再动手）

| 项 | 证据 |
|---|---|
| 组件 `rg16_proxy_contract --gpu-proxy` | **PASS exit0**，binary SHA `778ec5fe51771985686573ac09f6162fe79ec734b15a424924fae5338fe3dc47`（约 Asia/Shanghai 20:47），日志 `.local/codex-rg16-proxy/device-root-gpu-rerun.log` |
| APK | `.local/android-apk/ffrs-game-window-debug.apk` SHA `cf72f13754a596dce629c7f40a3c51003072b06bf6bc1e370aa5c6763ee0ceb7`，mtime ~20:48，已装 `org.ffrs.gamewindow`；`game_dir=original`（**非** 60FPS derived） |
| UI 路径 | 标题 → NewGame → 确认 → 普通难度 → **3D 飞艇甲板场景**（`.local/codex-runs/59/07-scene-3d.png`；另有 `01-title`…`06-difficulty-normal`） |
| 旧 FATAL | run59 `guest.log`：**0×** `Converted RG16_UNORM attachment writes require UNORM16 quantization`；RG16 仅见 LUT `serial=29` canonical+proxy 日志，**无** run58 `serial=7226` attachment FATAL |
| HANDOFF / STATUS | **尚未更新**（仍停在旧 RG16 / run58 叙事）——本单必须补 |

**新的第一致命（现场观察，Asia/Shanghai ~21:35）**：

```
[guest-abort] LowLevelFatalError
NVNSubmission.cpp Line 315
GPU interrupt wait returned "TIMEOUT_EXPIRED"
```

监视：guest pid=`5964` @ 21:34:47 → APP_GONE @ 21:35:13。之后设备多半回到 `org.ffrs.diagnostic`；gamewindow guest 已死。

**同款历史（只读对照，勿当本单已修好）**：`.local/codex-runs/49/guest-end.log` 在 `nvnQueueSubmitCommands: the handle 0x0 names no command package` 之后，同一句：

```
LowLevelFatalError [File:.\../Platforms/Switch/Source/Runtime/NVNRHI/Private/NVNSubmission.cpp] [Line: 315]
GPU interrupt wait returned "TIMEOUT_EXPIRED". This should never happen.
```

HANDOFF 旧文也记过：command registry 65536/256MiB 耗尽 → lost handle 0 → guest GPU wait abort —— **勿再误标成「随机驱动超时」**，先用 run59 完整日志验证是否同族。

---

## 2. 本单目标（一句话）

1. **拉全**设备侧 `guest_boot.log`（及必要的 crash/logcat），归档到 `.local/codex-runs/59/`；  
2. 在 TIMEOUT / AbortImpl **之前**找出 **第一可操作原因**（lost handle / registry 满 / shadow missing / sync 永不 signal / submit refused / …）；  
3. **更新** `coordination/HANDOFF.md` + `coordination/STATUS.md` + `coordination/state.json`：关闭 P-MM-48、打开 P-MM-49；  
4. 仅当根因 one-line 明显时做最小修 + 复测；否则停并给出 **下一刀** 建议文案。

---

## 3. 必读 / 可碰 / 禁止

### 3.1 必读（先读再动）

- 本文件 `coordination/prompts/P-MM-49-nvn-gpu-timeout.md`
- `.local/codex-runs/59/manifest.json`
- `.local/codex-runs/59/guest.log`（本地已有 ~150k 行；**注意末尾可能截断**，缺 abort 原文）
- `.local/codex-runs/59/07-scene-3d.png` 及前后截图
- `.local/codex-runs/49/guest-end.log`（同款 TIMEOUT 先例，搜 `NVNSubmission` / `handle 0x0`）
- `coordination/HANDOFF.md` 顶部 + 内文「command registry65536」「GPU wait abort」段
- `coordination/prompts/P-MM-48-rg16-attachment-newgame.md`（只认账，不重做）

### 3.2 设备日志路径（沿用本仓习惯）

```
设备：/sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log
设备：/sdcard/Android/data/org.ffrs.gamewindow/files/codex-frame-5964.csv  （pid=5964）
包名：org.ffrs.gamewindow
拉日志示例（优先完整 pull，勿只 tail）：
  adb -s RG477M01445122 pull /sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log \
    .local/codex-runs/59/guest_boot-device-full.log
  adb -s RG477M01445122 logcat -b crash -d -t 400 > .local/codex-runs/59/crash.txt
  adb -s RG477M01445122 logcat -d -t 2000 | rg -n 'TIMEOUT_EXPIRED|NVNSubmission|guest-abort|APP_GONE|ffrs' \
    > .local/codex-runs/59/logcat-timeout.txt
```

若 sdcard 文件已被冲掉 / 变短 / 无 abort：在 HANDOFF 写明；**不要假装有全文**。可对照本地 `guest.log` + run49 先例定性，并建议「下次用监控脚本在 APP_GONE 瞬时冻结 guest_boot」。

装包脚本参考（只读）：`scripts/android/install_game_window_apk.sh`、`.local/codex-runs/install_run.py`。

### 3.3 源码只读巡视（找根因时）

优先这些（**默认只读**；仅 one-line 明显才改）：

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp`（submit / flush / sync / command lifetime）
- `src/graphics/nvn_device/nvn_queue.c` / `.h`（`nvnQueueSubmitCommands`、refuse/lost handle）
- `src/graphics/nvn_device/nvn_sync.c` / `.h`（`nvnSyncWait` / `TIMEOUT_EXPIRED`）
- `src/graphics/nvn_vulkan/engine/xbg_completion_wait.h` 及 queue-flush / command-lifetime 相关
- 任何现有 `command registry` / package retire / grantcursor / lease 日志路径

### 3.4 禁止改

- RG16 proxy 设计、canonical UNORM16、`xbg_rg16_proxy.*` 方向性改动
- FPS / 60FPS derived NSO / `game_dir` 覆盖 / readback A/B / WSI interval
- 其他 `.worktrees/p-mm-*`、主树业务逻辑（本单只允许把 **同一份** prompt 副本拷到主树 `coordination/prompts/`）
- `git reset --hard`、force push、push、改 main
- 声称「可玩 / 标题通关 / 稳定场景」

---

## 4. 执行步骤（按序）

### Step A — 认领现场（≤5 分钟）

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen
git status -sb
git rev-parse HEAD   # 期望 0444c81 之上脏工作
test -f .local/codex-runs/59/manifest.json && cat .local/codex-runs/59/manifest.json
adb -s RG477M01445122 get-state
adb -s RG477M01445122 shell 'ps -A | grep -E ffrs|gamewindow|diagnostic || true'
```

**勿 reset。** 若 HEAD 不是 `0444c81` 或脏改消失：停下写进 HANDOFF，不要自行恢复。

### Step B — 冻结日志（本单核心）

1. `mkdir -p .local/codex-runs/59`
2. pull 设备 `guest_boot.log` → `.local/codex-runs/59/guest_boot-device-full.log`（保留已有 `guest.log`，**不要覆盖**）
3. 尝试 pull `codex-frame-5964.csv`（若 Permission denied：`run-as org.ffrs.gamewindow cat ...` 或记 NOT_READABLE）
4. crash + logcat 摘 TIMEOUT / AbortImpl
5. 在 **device-full**（或可用的最完整副本）上检索：

```bash
rg -n 'guest-abort|TIMEOUT_EXPIRED|NVNSubmission|LowLevelFatal|handle 0x0|names no command package|registry|exhaust|shadow missing|refused=' \
  .local/codex-runs/59/guest_boot-device-full.log \
  .local/codex-runs/59/guest.log
```

6. 对 **第一条** `guest-abort` / `TIMEOUT_EXPIRED`：**向上取 ≥80 行、向下取 ≥20 行**，原样写入 `.local/codex-runs/59/timeout-window.txt`
7. 另列「TIMEOUT 之前的嫌疑」清单（有计数）：`handle 0x0`、`refused=`、`shadow missing`、`nvnSyncWait: TIMEOUT`（区分 `timeout=0` 轮询 vs 真超时）、command memory / registry 告警

### Step C — 定性（必须写出一类）

从下面选一类（可多选，但要标 **主因**）：

| 类 | 含义 |
|---|---|
| A | lost/null command handle → submit refused → guest 仍 wait interrupt → TIMEOUT（run49 同族） |
| B | command registry / 256MiB 包上限耗尽 |
| C | sync/fence 未 signal，真 wait 超时（非 timeout=0 轮询） |
| D | GPU 工作未 retire / flush 缺口 |
| E | 其它（必须贴原文）；证据不足则标 **INCONCLUSIVE** 并说明缺哪份日志 |

**禁止**在没有 A/B 类硬证据时写成「Mali 驱动随机超时」。

### Step D — 可选最小修（默认跳过）

仅当 Step C 主因 = A/B/C/D 且修复点在现有 NVN 路径 **一眼可见**（例如明确对 `handle==0` 仍进入 wait、retire 漏调一行、registry 满后应失败却返回成功）时：

1. 最小 diff，**不**顺手改 RG16 / FPS
2. 若改了引擎：按仓库现有脚本重打 APK → 记 SHA → 装机 → 再走一遍「标题→NewGame→普通→进场景」**短复测**（进 3D 后观察是否再次 TIMEOUT；不必追求长稳）
3. 否则 **禁止**为「可能有用」大改

### Step E — 关闭 P-MM-48 / 打开 P-MM-49（必做）

更新：

- `coordination/HANDOFF.md` **顶部**插入回报块（模板见 §7）
- `coordination/STATUS.md`：阶段改为 P-MM-49 / GPU TIMEOUT 定性（或「已定位待修」）
- `coordination/state.json` blockers：去掉 RG16 attachment FATAL；加上 NVNSubmission TIMEOUT（或你的主因类）

可选：`git commit`（feat/fix 惯例）；**不 push**。

---

## 5. 验收清单

- [ ] `.local/codex-runs/59/` 含 device-full 或明确 NOT_AVAILABLE 说明 + `timeout-window.txt`
- [ ] 写出 TIMEOUT **之前**的第一嫌疑（类 A–E）+ 原文引用
- [ ] HANDOFF 顶部关闭 P-MM-48（含 APK sha、组件 PASS sha、旧 FATAL 已清、3D 截图路径、Asia/Shanghai 时间）并打开 P-MM-49
- [ ] STATUS / state.json 与 HANDOFF 一致
- [ ] **未**重做 RG16；**未**做 FPS/readback；**未** push / reset
- [ ] **未**声称可玩；若未做修复，写明下一刀建议一句

---

## 6. 粘贴开场（给 **新的空** MiniMax 对话框）

```
你是 MiniMax Code 写手。工单 P-MM-49：定性「进 3D 后 GPU interrupt wait TIMEOUT_EXPIRED」并关闭 P-MM-48。

工作树：/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen
分支 task/T02-codex-black-screen @ 0444c81（认领现有脏改，勿 reset、勿另开树）。

先完整阅读：
coordination/prompts/P-MM-49-nvn-gpu-timeout.md
.local/codex-runs/59/manifest.json
.local/codex-runs/59/guest.log（可能缺 abort 尾）
.local/codex-runs/49/guest-end.log（同款 NVNSubmission.cpp:315 先例）

P-MM-48 已半成功：组件 --gpu-proxy PASS sha778ec5fe…；APK sha cf72f137…；UI 到 3D（07-scene-3d.png）；旧 RG16 attachment FATAL=0。新致命：NVNSubmission.cpp:315 GPU interrupt wait TIMEOUT_EXPIRED（~21:35 Asia/Shanghai，pid5964 APP_GONE）。

立刻执行：pull 完整 guest_boot.log → 截 TIMEOUT 前因窗口 → 定性 A–E → 更新 HANDOFF/STATUS 关闭48打开49。仅 one-line 明显才最小修，否则停下给下一刀。禁止 RG16重做/FPS/readback/push。
```

---

## 7. 给调度的回报模板（写进 HANDOFF 顶部）

```
P-MM-48 CLOSED (half-success → playability advanced past RG16 RT):
- component --gpu-proxy: PASS SHA=778ec5fe51771985686573ac09f6162fe79ec734b15a424924fae5338fe3dc47 (~20:47 Asia/Shanghai)
- APK59 SHA=cf72f13754a596dce629c7f40a3c51003072b06bf6bc1e370aa5c6763ee0ceb7 game_dir=original
- NewGame/Normal: 越过 RG16 FATAL=Y；进 3D 飞艇甲板（.local/codex-runs/59/07-scene-3d.png）
- 旧 FATAL 已清：0× Converted RG16_UNORM attachment … UNORM16 quantization（对比 run58 L101429–101431）
- 未做: FPS / push / 可玩声称

P-MM-49 OPEN:
- 新第一致命原文: LowLevelFatalError NVNSubmission.cpp:315 GPU interrupt wait returned "TIMEOUT_EXPIRED"
- 时间: ~21:34:47–21:35:13 Asia/Shanghai pid=5964 APP_GONE
- 完整日志: guest_boot-device-full=… / 或 NOT_AVAILABLE 原因=…
- TIMEOUT 前因窗口: .local/codex-runs/59/timeout-window.txt
- 主因类: A/B/C/D/E/INCONCLUSIVE — 一句话 …
- 最小修: DONE(SHA=…)/SKIPPED
- 下一刀建议: …
- 未做: RG16重做 / FPS / push / 可玩声称
```

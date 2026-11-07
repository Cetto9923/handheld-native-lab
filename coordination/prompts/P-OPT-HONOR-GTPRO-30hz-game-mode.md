# 工单 P-OPT-HONOR-GTPRO-30hz-game-mode｜荣耀 GT Pro 专用：游戏模式稳 30Hz 卡顿排查

> **已迁移**：新开会话请用并行安全修订版 `coordination/prompts/P-CX-TERRA-HONOR-30hz.md`（写手=Codex Terra 高；工作树优先 `ff-resonance-honor-gtpro`；本文件保留作史实与排查细节附录）。 产品真源见 `coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md` §0.1。
>

> **对齐发行 SSOT（2026-10-31）**：本刀服从 `coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md`——**Universal APK + External Game Data + Runtime Device Profile**。荣耀 = **mid** 档（1080P，游戏模式先稳 ~30 再冲 40；禁 60）。换机不换数据包；不为荣耀另打正式 APK。前置/并行：`P-DIST-1-foolproof-install-autodiscover.md`（自动发现）、`P-DIST-2-device-profile-runtime.md`（档位骨架）。派工见 `coordination/docs/DISPATCH-PLAN-honor-after-baseline.md`。  
> **状态**：正式派工（新开短线程；禁止肥会话续聊；**禁止塞进 RG477M 验收会话**）  
> **写手**：**Codex**（本刀指定；可开多 Agent 团队，但文件所有权不重叠）  
> **设备 ONLY**：荣耀平板 **GT Pro** `adb -s A42BBB5109000333`（HONOR **ROD2-W69** / `ro.soc.model=SM8635` / **Adreno 735** / Android 16；用户口述骁龙 8 Gen3，官方规格常标第三代骁龙 8s——以 ADB 实测为准）。**本刀禁止碰 RG477M `RG477M01445122`（禁安装/卸载/抢前台/清数据）。禁 `adb kill-server`。**  
> **工作树（2026-10-31 更新）**：
> 1. **优先** `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`（若目录已存在则 **必须** 在此开发，勿写基线仓）。  
> 2. 若 honor 目录尚不存在：在基线仓内新建独立 worktree `.worktrees/honor-gtpro-30hz`（从当前可玩检查点拉分支如 `work/honor-gtpro-30hz`），**禁止**认领/双写 RG477M 正在测的脏树当唯一交付。  
> 3. 旧提示里的 `.worktrees/codex-black-screen` 仅作历史参考；**并行阶段不要再往该树塞荣耀提交**。  
> **并行隔离（硬）**：新开 Codex 会话；与 RG477M 基线测试互不踩；共享 romfs **只读**；RESULT/APK 产出落 honor 树 `.local/`。  
> **用户现场（Asia/Shanghai 2026-10-29，基线进度 2026-10-31）**：平板侧曾「很卡」；RG477M 侧已基本稳 30Hz 可玩（战斗略卡）作 low 档基线。本刀验收必须在 **游戏模式（非狂暴）** 下完成。  
> **用户优先级（硬）**：
> 1. **傻瓜安装 / 自动发现数据目录**——对齐 P-DIST-1 / SSOT；用户把游戏数据拷到内存卡/外置存储任意常见路径，只装 APK → App **自动扫描发现**并启动；**换机（RG477M ↔ 荣耀）同一套「数据在卡上 + APK」可顺利安装启动**；禁手填绝对路径、禁按机型改配置  
> 2. **游戏模式 1080P 稳 ~30Hz present 可玩为第一闭环**（运动中稳；禁用狂暴模式数字冒充）；Profile 期望 **mid**  
> 3. **同窗证明后再冲 1080P/40Hz**；**本刀禁直接冲 60Hz**  
> 4. **音频不回退**——验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`（蓝牙 A2DP、NONE、fence_spin=0、DISCONNECTED 重开流等）  
> 5. **视觉不回退**——无 UI 遮盖/异常叠层；建筑与特效正常；无新全黑/中央黑块；不复发 RG16 FATAL；面板画幅等比例留边（勿拉伸裁切字幕/按钮）  
> 6. **遇阻塞先查行业/社区公开案例取经再改**（写 `OSS-NOTES.md`；无出处大改 = FAIL）  
> 7. **禁止声称可玩/通关**；禁止双写；不 push；不改 `main`


---

## 0. 你是谁 / 先占位再动手

你是写手 **Codex**（可带多 Agent 团队，但见 §0.4）。刀号 **P-OPT-HONOR-GTPRO-30hz-game-mode**。

### 0.1 开工前必做所有权检查（读-only，5 分钟内完成）

1. `cd` 到 honor 工作树（优先 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`，否则 `.worktrees/honor-gtpro-30hz`）后：`git status -sb` + `git rev-parse --short HEAD`。**禁止**在 RG477M 基线脏树上开写。
2. 读真源（**不要**通读千行 HANDOFF）：
   - **本文件**
   - **`coordination/prompts/_STANDING-audio-acceptance.md`（常设音频硬门；必须完整阅读）**
   - 结构参考：`coordination/prompts/P-OPT-RG477M-30hz-game-mode.md`（本刀设备/分辨率/目标换成荣耀；骨架对齐）
   - 数据目录预检参考：`coordination/prompts/P-OPT-RG477M-40hz-audio.md` §0.1b / §5.0 / §6.5（**本刀要把「自动发现」升为硬闭环，不只是三候选预检**）
   - 平板现场账本：`.local/tablet-test-20261028/REPORT.md`（卡顿证据、present/s、Adreno 735、shader crash 史）
   - `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`（`hasGame` / `candidateGameDirs` / `resolveGameDir` / `refreshSetup` / `startGuest` / `allFilesAccess` / 存储权限）
   - `src/runtime/hle/audio_bridge.c` + `audio_bridge.h`（确认仍为 `PERFORMANCE_MODE_NONE` 等救回点）
   - `src/graphics/nvn_vulkan/engine/xbg_game.cpp` 内 `xbg_refresh_target_fps`（**源码默认可能是 60，勿假设 30**；以日志 + `gfx-target-fps` marker 为准）
   - Adreno / 高通相关：`xbg_spirv_*`、fence / present / swapchain、legacy wait、native BC 开关路径（只读先摸清，勿盲开）
3. 查设备谁在前台、装了哪些包（**仅** `A42BBB5109000333`）：
   - `org.ffrs.gamewindow`（用户主包；安装 SHA 以当场 `sha256sum` / `adb shell pm path` + 本地算为准）
   - `org.ffrs.perfisolated` / `org.ffrs.perfscale` / diagnostic（隔离实验包；**勿卸用户存档包**）
4. **若发现另一 agent 仍在写同一 worktree / 同一 `gamewindow` APK**（文件 mtime 刚变、checkpoint 仍在追加）：**停下写 HANDOFF，不要双写**。优先：
   - 等用户确认另一写手已停；或
   - 只在 **隔离包** `org.ffrs.perfisolated` / 新建隔离包上做本刀实验，**不覆盖**用户正在玩的 `org.ffrs.gamewindow`，直到用户明确授权装回主包。

### 0.1b 数据目录硬预检 + 傻瓜安装闭环（装包后、测帧前必过｜用户 2026-10-29 补充硬门）

用户原话要点：**「游戏 APP 要求自动发现资源数据路径；部署时拷贝数据到内存卡，然后安装游戏就可以；换了设备也能顺利安装启动游戏。」**

本刀**必须**把下列当成 **硬门验收**（缺自动发现或只能写死路径 = **整刀 FAIL**），不能弱化成「可选」或「另刀再说」。

#### 产品闭环（写死）

1. 用户把完整游戏数据（至少 `main.nso` + `romfs/`，含 `romfs/FFRS/Content/Paks/` 关键容器）拷到 **外置/内存卡/内置存储的任意常见路径**。  
2. 用户 **只安装 APK**（冷启；清空旧 Intent/`game_dir` 配置）。  
3. App 启动后 **自动扫描/发现** 数据目录 → `resolveGameDir` 非空 → 进 setup/加载屏 → 能启 guest。  
4. **禁止**要求用户手填绝对路径、按机型改配置、或只靠 `adb am start --es game_dir …` 冒充验收。  
5. **换机可移植**：同一套「数据在卡上 + APK」在 **RG477M 与荣耀 GT Pro** 都能自动找到并启动（本刀真机验收以荣耀为主；路径扫描逻辑必须**机型无关**；若改了扫描，须在 RESULT 写明为何 RG477M 侧不会被写死路径破坏，必要时用文档/最小对照说明）。

#### 现有钩子（先读再动）

复用并 **扩展**（先读再动，勿另起一套无关安装器）：

- `GameActivity.java`
  - `hasGame(File)`：`main.nso` 可读 + `romfs/` 为目录（或可读）
  - `candidateGameDirs()`：当前含若干候选（含 `Environment.getExternalStorageDirectory()/FFRS`、`getExternalFilesDir()/game`、`/data/local/tmp/ffrs-*`、以及历史硬编码 RG477M 路径等）——**开工时以源码为准再核**
  - `resolveGameDir()`：Intent extra `game_dir` 优先，否则扫候选
  - `refreshSetup()` / `startGuest()`：找不到则中文 setup 引导；找到则启 guest
  - `allFilesAccess()` / 存储权限引导

#### 硬预检步骤（荣耀真机）

1. **清空旧配置**：卸除/忽略旧 Intent `game_dir`；勿用 adb 手工注入路径作为 PASS 证据。  
2. 将完整游戏数据拷到 **至少两种** 位置做复测（建议）：  
   - 内置可见路径：如 `/sdcard/FFRS` 或 `Environment.getExternalStorageDirectory()/FFRS`  
   - **外置 SD/TF / secondary storage** 常见挂载点之一（Android scoped storage / `getExternalFilesDirs` / `StorageVolume` / `/storage/<UUID>/…` 等——以设备实测挂载为准）  
3. 冷启本刀 APK；`resolveGameDir` **必须非空**；guest/logcat 打出实际 `game_dir=…`。  
4. 关键容器在位（至少存在且非空体积合理）：  
   - `romfs/FFRS/Content/Paks/FFRS-Switch.pak`  
   - `romfs/FFRS/Content/Paks/FFRS-Switch.ucas`  
   - `romfs/FFRS/Content/Paks/FFRS-Switch.utoc`  
   - `romfs/FFRS/Content/Paks/global.ucas` / `global.utoc`（若缺，记入 RESULT，勿假装齐全）  
5. 找不到数据：**必须**出 `refreshSetup` 引导屏；**禁止静默挂死 / 黑屏无提示**。  
6. 若现有候选 **不够覆盖外置卡**：Codex **必须先 §3.1 查行业/社区案例**（Android 多存储卷扫描、GameActivity/SAF、模拟器/移植项目如何找 game dir），再扩扫描；改动写进 `OSS-NOTES.md`；无出处大改 = FAIL。  
7. **允许本刀做**：扩候选路径、secondary storage / `getExternalFilesDirs`、浅层按名扫描（如找含 `main.nso`+`romfs` 的 `FFRS` 目录）、权限引导与 setup 文案、Paks 缺文件点名。  
8. **禁止本刀做**：完整 NSP 自动解包流水线、无界递归深扫整盘拖死启动、按机型写死唯一绝对路径、要求用户改配置文件。

### 0.2 硬禁止

- 不 push、不 force、不改 `main`、不碰其他 worktree、**不碰 RG477M 真机**。  
- **不声称可玩 / 通关 / 已达发行质量。**  
- **本刀第一闭环禁直接冲 60Hz**；禁止拿 interval=1 / 狂暴模式短窗当验收。  
- **同窗 30Hz 未证明前，禁止把 40Hz 当本刀成功**；40Hz 仅允许在 30Hz 同窗 PASS 后作为 **可选抬档实验**，且每步复测音频+视觉。  
- **禁止用狂暴模式（或任何更高系统性能档）下的帧率数字冒充游戏模式验收。**  
- **不重做 RG16 设计 / LUT ABI / attachment proxy**（除非本刀复测新炸同款 FATAL 才只记证据）。  
- **不双写**：不同时改隔离快照 + 共享 worktree 同一功能，不与另一活动 agent 抢 `gamewindow`。  
- 不改用户存档路径内容；隔离实验用拷贝。  
- 不把 title/present 计数当性能验收；必须 **运动中 + 音频连续（蓝牙）+ 截图/视觉**。  
- **不得回退音频救回点**：默认 `PERFORMANCE_MODE_NONE`、合理缓冲、TIMEOUT 重试、DISCONNECTED 重开流、`fence_spin_us=0`。禁默认 `LOW_LATENCY`；禁为冲帧永久砍薄缓冲；禁 `fence_spin>0` 无蓝牙 A/B。  
- **禁止**要求用户手填 `game_dir` / 按机型改配置冒充傻瓜安装 PASS。  
- 自适应分辨率产品化 / 高通+天玑双 SoC 整包发行 / PS5+NS 手柄深化：**本刀范围外**（只要求不要弄坏现有路径；分辨率本刀锁 **1080P 口径** 验收）。

### 0.3 游戏模式确认（验收前置）

用户要 **目标模式**。写手必须：

1. **开工与验收前**向用户确认（或在 RESULT 写明用户已确认）当前系统档为 **游戏模式（非狂暴）**。荣耀/MagicOS 可能有性能/游戏助手档——**只读确认，勿擅自改系统档**。  
2. 在 RESULT 写清确认方式（用户口述 / 设置页截图路径 / 可读 prop 若有且可信）。无可机读 prop 时以用户确认为准。  
3. **任何在狂暴/性能狂暴档下采到的 fps 数字一律作废**，不得写入 PASS 证据。

### 0.4 多 Agent 团队（可选）

可开多 Agent，但 **文件所有权不重叠**：

| Agent 角色 | 可写范围（示例） | 禁写 |
|---|---|---|
| Lead / 帧率 | `xbg_game.cpp` 限帧、marker 读写、frame metrics 脚本、RESULT 帧率节 | `audio_bridge.*`、GameActivity 数据目录 |
| Audio | `audio_bridge.c/h`、常设音频勾选证据 | gfx 限帧默认、swapchain |
| DataDir / 安装 | `GameActivity.java` 扫描/权限/setup、OSS-NOTES 安装节 | 音频救回点、无出处大改 gfx |
| Visual QA | 截图归档、视觉 checklist、不改生产逻辑 | 任意生产源码（只读） |

冲突时：**停下**，在 HANDOFF 写清谁持有哪条路径，等用户裁决。禁止两人同时改同一文件。

---

## 1. 现场事实（开工时再核，勿当永恒真理）

| 项 | 已知（Asia/Shanghai 2026-10-28~29 核） |
|---|---|
| 设备 | `A42BBB5109000333`；HONOR **ROD2-W69**；Android 16；`ro.soc.model=SM8635`；GPU **Adreno 735**；面板约 **3000×1920**（官方规格；以 `wm size` 实测为准） |
| SoC 口径 | 用户口述骁龙 **8 Gen 3**；荣耀规格页常写 **第三代骁龙 8s**——RESULT 写实测 prop，勿空吵型号名 |
| 系统档 | 本刀要求 **游戏模式（非狂暴）**；验收前确认 |
| 工作树 | `.worktrees/codex-black-screen` @ `0444c81` + 大量 dirty/untracked |
| 源码默认 | `xbg_refresh_target_fps` 内 `target_fps` **可能是 60**（**不是 30**）；`FFRS_TARGET_FPS` / marker `gfx-target-fps` 可覆盖；`0`=去掉主机 limiter |
| 设备 marker | **开工时再 `adb` 读** `gfx-target-fps` / `gfx-fence-spin-us` 等；平板史曾有 `gfx-warp32`、`gfx-native-bc`、`gfx-legacy-waitall`、`gfx-ldc-fold` 等实验标记——**本刀先盘点再决定保留/清掉** |
| 卡顿证据（硬） | `.local/tablet-test-20261028/REPORT.md`：用户确认「一卡一卡」「还是很卡」；开场多窗 **约 6.8–9.8 present/s**（远低于 30）；曾有 Adreno `libllvm-qgl.so` signal11 / 多 shader override 史；720p ResX/Y、ScreenPercentage、定向 wait、community shader、native BC 等实验 **未证明把卡顿拉到可玩** |
| 音频 | 常设见 `_STANDING-audio-acceptance.md`（救回点在 RG477M 侧已确认）；**荣耀侧必须整表复测**，不得假设已过 |
| 包 | `org.ffrs.gamewindow` / `perfisolated` / `perfscale` / diagnostic；平板史 APK SHA 见 REPORT（以当场为准） |
| 数据 | 史载资源曾在 `/sdcard/FFRS`；本刀要证明 **拷卡 + 自动发现**，不能只认这一条写死路径 |

---

## 2. 本单目标（一句话）

在 **荣耀 GT Pro only + 游戏模式** 上：

0. **傻瓜安装 / 自动发现数据目录硬门 PASS**（§0.1b：仅拷卡+装包、清空旧配置、换路径复测；换机路径逻辑机型无关）；  
1. **验收必须完整包含 `_STANDING-audio-acceptance.md`**（蓝牙野外/战斗各 ≥2 分钟等；任一项不过 = 整刀 FAIL）；  
2. **视觉不回退**（§5.3）；  
3. 把 present **锁稳在 ~30Hz / 1080P 口径**（运动中 p50≈30；无长时间明显卡顿；游戏速度正常）——**第一闭环**；  
4. **同窗 30 证明后** 才允许可选冲 **1080P/40Hz**（每步复测音频+视觉）；**禁直接冲 60**；  
5. 写清回滚点；**不 push**；**不声称可玩**。

**验收口径**：  
- 不能自动发现数据 / 只能手填路径 = **FAIL**  
- 音频常设清单任一项不过 = **整刀 FAIL**  
- 视觉回退（UI 遮盖 / 建筑特效缺失 / 新黑屏 / RG16 FATAL / 拉伸裁切字幕按钮）= **整刀 FAIL**  
- 用狂暴模式数字、或未过 30 同窗就宣称 40/60 成功 = **FAIL**  
- 帧率「够高」但声音断续 / 视觉坏 = **FAIL**  
- 无 `OSS-NOTES.md` 出处的大改 / 空口「业界都这样」= **FAIL**

---

## 3. 方法：先查行业/社区案例，再排卡、锁 30、保音频、保视觉、保自动发现

### 3.1 必做调研（硬门；写进 OSS-NOTES；禁止空口「业界都这样」）

**遇到任何阻塞都必须先查行业案例与社区公开案例取经，再改代码。** 包括但不限于：平板严重卡顿（~7–10 present/s）、Adreno GPU timeout / fence、音频回退、视觉回退、**外置 SD/secondary storage 数据自动发现**、限帧 marker/env 无效、游戏模式 vs 性能档调度差异、AAudio underrun、swapchain/分辨率、subgroup/warp 实验副作用。

先扫（只读，勿大 clone 改别人仓库）：

- **同路线静态重编译 / NVN→Vulkan Android**：`seedds/nxnative`、`d2r-mobile/d2r-mobile`（frame pacing / present / writeback / Adreno notes；平板 REPORT 已引 `a8782caa…`，可续读）  
- **Switch 模拟器音频与帧步进**（思路借鉴，非照搬）：Yuzu / Sudachi / Suyu / Ryujinx 的 audio sink、stretch、underrun、frame limiter  
- **Android AAudio**：Google AAudio underrun / `PERFORMANCE_MODE_LOW_LATENCY` vs `NONE`/`POWER_SAVING`、burst/capacity、设备变更与 `DISCONNECTED`  
- **Unreal / Adreno / 高通**：frame pacing、UBWC、BC 纹理、GPU fence；Qualcomm / Adreno known issues；Google Vulkan-Errata（归档）仅作线索  
- **Android 存储发现**：`getExternalFilesDirs`、`StorageManager`/`StorageVolume`、scoped storage、MANAGE_EXTERNAL_STORAGE、SAF；开源 Android 移植/模拟器如何扫描 game data 目录（必须带 URL）  
- **平板/社区公开笔记**：荣耀 MagicOS 游戏模式、热节流；相关 GitHub issues / PR / 公开帖（必须带 URL）

调研产出 **必须先写**：`.local/honor-gtpro-opt-30hz-game-mode/OSS-NOTES.md`（URL/commit/issue 引用）。**无出处的大改 = FAIL**；提出风险改动前 OSS-NOTES 必须已落盘。

### 3.2 推荐排查清单（按序；每步复测音频+视觉+数据发现）

**A. 基线与目标模式**

1. 确认 **游戏模式**（§0.3）+ **§0.1b 傻瓜安装预检**（至少内置 + 外置各一路径）。  
2. §3.1 调研落盘 OSS-NOTES（开干前至少一版）。  
3. 读清当前限帧真相：源码默认？设备 `gfx-target-fps`？guest 日志实际 target？**以实测为准，勿假设 30**。  
4. 盘点平板实验残留标记（`gfx-warp32`、`gfx-native-bc`、`gfx-legacy-waitall`、`gfx-ldc-fold`、`gfx-forward-temps`、`gfx-pass-live`、ue-commandline override、spv-override 等）：记入 RESULT；**无证据勿默开**；干扰 30Hz 基线的先隔离/关闭做对照。

**B. 锁 30 / 1080P**

5. **锁到 30**：优先安全手段——写/改 `gfx-target-fps`→**30**；和/或 `FFRS_TARGET_FPS=30`；和/或最小 diff 改源码默认（可 marker 覆盖）。清掉「冲 60 / 去 limiter（0）」配置。  
6. **分辨率 1080P 口径**：内部/源图以 ~1920×1080 或等价 16:9 档为目标验收；面板 3000×1920 **等比例居中留边**，不拉伸、不裁切字幕/按钮。勿把面板原生分辨率直接当游戏 RT。历史 720p Res 实验未救卡——本刀不要无对照地重复空转。  
7. A/B：30 vs 残留 40/60/0；同一场景运动中采 `gfx-codex-frame-metrics` + `scripts/graphics/analyze_frame_metrics.py`（p50/p95/max、>100ms）。  
8. 系统化看卡点：logcat、gfx markers、GPU timeout、fence / legacy_wait / ring / gpu_sync、systrace/Perfetto（若环境允许）、Adreno 驱动崩溃是否复发。

**C. 卡顿根因候选（按证据拧，勿一次全开）**

9. Present / fence / wait 策略（对照 d2r-mobile；禁假信号跳过真实同步）。  
10. CPU/GPU 负载与热节流（游戏模式档）。  
11. Shader 编译/缓存/override 是否仍必要；无证据勿批量开 warp32。  
12. 音频路径是否饿死渲染（`fence_spin` 必须 0；蓝牙 A/B）。  
13. 数据目录在慢存储（外置卡）上的 I/O——发现逻辑过了仍要确认不是卡在读 Pak。

**D. 抬档规则**

14. **仅当** 游戏模式 + 自动发现 + 音频常设 + 视觉 + **同窗稳 30** 均 PASS 后，才允许可选冲 **40Hz**；每步复测蓝牙音频；一坏立刻回 30 / 已知好包。  
15. **禁止本刀冲 60Hz**（60 后置产品探路）。

**E. 每步强制**

16. 完整勾选 `_STANDING-audio-acceptance.md` + 视觉 checklist + 数据发现 checklist；音频一坏立刻回滚，禁止继续拧。  
17. 禁止：假信号 fence、砍必要 wait、默认 LOW_LATENCY、`fence_spin>0` 无蓝牙 A/B、为冲帧砍音频缓冲、手填路径冒充安装 PASS、无 OSS-NOTES 出处的大改。

### 3.3 视觉抽检节奏

每步装包/改 marker 后：标题 → Continue/进野外 → 战斗；截图归档；对照有无 UI 遮盖、建筑缺失、特效消失、新黑屏、拉伸裁切。

---

## 4. 必查文件 / 目录

```
coordination/prompts/_STANDING-audio-acceptance.md   # 常设音频硬门（强制）
coordination/prompts/P-OPT-RG477M-30hz-game-mode.md   # 结构参考（设备不同）
coordination/prompts/P-OPT-RG477M-40hz-audio.md       # 数据目录预检条款参考
.local/tablet-test-20261028/REPORT.md                 # 荣耀卡顿现场账本
src/android/app/java/org/ffrs/diagnostic/GameActivity.java
src/android/native/guest_boot_jni.cpp
src/runtime/hle/audio_bridge.c
src/runtime/hle/audio_bridge.h
src/graphics/nvn_vulkan/engine/xbg_game.cpp          # xbg_refresh_target_fps；Adreno/BC/同步相关
src/graphics/nvn_device/nvn_window.c
src/graphics/nvn_device/nvn_queue.c
# Adreno / SPIR-V 相关（只读先摸清；改前必须 OSS 出处）
src/graphics/nvn_vulkan/engine/xbg_spirv_*.cpp       # 若路径有出入以树内实际为准
scripts/graphics/analyze_frame_metrics.py
scripts/graphics/prepare_ffrs_60fps.py               # 只读；本刀禁用 derived 部署冒充
```

设备侧（读，serial `A42BBB5109000333`）：

```
/sdcard/Android/data/org.ffrs.gamewindow/files/gfx-target-fps
/sdcard/Android/data/org.ffrs.gamewindow/files/gfx-fence-spin-us
（及 perfisolated 对应路径；以及外置卷上的同名 files 目录若存在）
adb shell wm size；getprop ro.soc.model；dumpsys SurfaceFlinger | 查 GPU
```

新建本刀证据目录：

```
.local/honor-gtpro-opt-30hz-game-mode/
  mode-confirm.md          # 游戏模式确认方式与证据
  OSS-NOTES.md             # 强制：URL/commit 引用；无出处大改=FAIL
  data-discovery/          # 拷卡+装包+换路径复测证据
  baseline-30hz/           # 游戏模式 30 限帧对照
  stutter-triage/          # 卡顿排查（logcat/metrics/fence/GPU）
  ab-vs-40-or-60/          # 仅 30 同窗 PASS 后的可选抬档
  screenshots/
  RESULT.md                # 最终结论（可中文）
```

---

## 5. 验收测试（全过才算本刀阶段性成功）

> 用户要求：目标模式 + 卡顿排查 + 音频/视觉/自动发现都要写进去。  
> 下列各节 **全部强制**；不可只测帧率。

### 5.0 傻瓜安装 / 自动发现数据目录（硬门）

- [ ] **仅拷卡 + 装包**：无手填路径、无 adb `game_dir` 注入作为 PASS  
- [ ] 冷启后 `resolveGameDir` 非空；日志有 `game_dir=…`  
- [ ] `main.nso` 可读 + `romfs/` 为目录（`hasGame` 语义）  
- [ ] `romfs/FFRS/Content/Paks/` 关键容器在位；缺哪个写进 `RESULT.md`  
- [ ] **内置路径**与 **外置 SD/TF/secondary** 至少各一次发现成功（换路径复测）  
- [ ] **清空旧配置**后再测仍能发现  
- [ ] 路径扫描逻辑 **机型无关**（不写死仅荣耀或仅 RG477M 绝对路径当唯一手段；历史 RG477M 硬编码路径若保留，不得成为唯一依赖）  
- [ ] 故意移走/找不到数据时：**必须仍出** `refreshSetup` 引导屏；**禁止静默挂死 / 黑屏无提示**  
- [ ] 若扩了扫描：OSS-NOTES 有 Android 存储/社区出处；启动不被无界深扫拖死

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
- [ ] 运动中 present **稳 ~30Hz**（p50≈30；允许短抖；无长时间掉到明显卡顿；记冷/暖）——相对史载 ~7–10 present/s 须有可复现改善证据  
- [ ] 实际生效限帧为 30（marker / env / 源码默认之一；日志可证）；残留 60/0 已清或已证明被 30 覆盖  
- [ ] **1080P 口径**（内部/源图）；面板等比例留边  
- [ ] **不是** title 屏、不是纯 present 计数吹牛  
- [ ] 游戏速度未明显加速/减速（禁止靠拨 clock / 狂暴档假 FPS）  
- [ ] **未直接冲 60**；40Hz 仅在 30 同窗 PASS 后可选，且单独记节

### 5.3 视觉 / 功能（用户点名必须避免的问题）

- [ ] 无 UI 遮盖、无异常 overlay / 叠层  
- [ ] 建筑与特效正常出现（至少野外 + 战斗各一张对比图，路径写入 RESULT）  
- [ ] 无新的全黑 / 中央黑块  
- [ ] 旧 **RG16 FATAL** 不复发  
- [ ] 不拉伸、不裁切字幕/文字/按钮（居中留边可接受）  
- [ ] Adreno 史载 signal11 / 已知坏 shader 路径：若复发，记证据并优先隔离，禁止「黑屏不闪退」冒充修复

### 5.4 行业/社区取经（硬门）

- [ ] `.local/honor-gtpro-opt-30hz-game-mode/OSS-NOTES.md` **已写**，含具体 URL / commit / issue 引用  
- [ ] 本刀每一处风险改动（限帧、音频、同步、数据目录扫描扩展、Adreno 相关）在 OSS-NOTES 或 RESULT 中有出处；**无出处的大改 = FAIL**  
- [ ] 遇阻塞时先查案例再改（§3.1）；未出现空口「业界都这样」

### 5.5 工程纪律

- [ ] 未 push；未双写；未动 RG477M 真机；未做自适应分辨率产品化 / 双 SoC 整包 / PS5+NS 深化  
- [ ] **未声称可玩**  
- [ ] 若改坏：能明确步骤回到「自动发现 PASS + 音频顺滑 + 游戏模式 30 限帧」  
- [ ] `RESULT.md` + 短 HANDOFF 条目写完（含：游戏模式确认、实际 `game_dir` 多路径证据、Paks、音频常设勾选摘要、30Hz 数字、APK SHA、截图路径、OSS-NOTES 摘要、卡顿排查结论）

---

## 6. 回滚策略（写死）

1. **音频一坏就停**：立刻恢复已知顺滑配置（`gfx-target-fps`→30、`fence_spin`→0、NONE 缓冲策略、或已知顺滑 APK SHA）；复测常设音频清单 PASS 前禁止继续改。  
2. **数据发现一坏就停**：回滚扫描改动到上一可发现版本；禁止留下「只能 adb 注入路径」的中间态当交付。  
3. 帧率不稳但音频/视觉/自动发现绿：可停在「游戏模式 + 自动发现 PASS + 音频 PASS + 视觉 PASS + 尽力 30」并写明未达点——**可接受停点**；仍禁止改去直接冲 60。  
4. 隔离实验优先：`org.ffrs.perfisolated`；确认后再授权覆盖 `org.ffrs.gamewindow`。  
5. 源码 diff 保持最小；限帧优先用 marker/env，改默认须可回滚。  
6. 禁止「为了稳帧永久砍音频缓冲」；宁可略低于 30 但音频/视觉/自动发现完整，不要破音或黑屏或手填路径。

### 6.5 数据目录 / 傻瓜安装：允许与禁止

**允许（本刀）：**

- 扩展 `candidateGameDirs` / 扫描覆盖 **内置 + 外置 SD/TF/secondary** 常见挂载；保持 Intent `game_dir` 仅作调试覆盖，**不得作为唯一正式路径**；  
- 使用 `getExternalFilesDirs` / `StorageVolume` 等公开 API；浅层按名查找含 `main.nso`+`romfs` 的目录；  
- 修好存储权限引导与 `refreshSetup` 文案；  
- 启动前检查 `Content/Paks` 关键文件缺失并在 setup/日志里**点名缺哪个**；  
- 去掉「仅 RG477M 绝对路径才能玩」的隐性依赖（可保留为候选之一，不可唯一）。

**禁止（另刀或永远禁止）：**

- 完整 NSP 自动解包安装流水线、无界递归深扫整盘拖死启动；  
- 要求用户手填绝对路径 / 按机型改配置才算「安装成功」；  
- 自适应分辨率产品化 / 双 SoC 整包发行 / PS5+NS 深化；  
- 本刀范围内直接冲 60Hz。

---

## 7. 完成时交付

1. `.local/honor-gtpro-opt-30hz-game-mode/OSS-NOTES.md`（强制；URL/commit 引用）  
2. `.local/honor-gtpro-opt-30hz-game-mode/RESULT.md`（游戏模式确认、傻瓜安装换路径证据、做了什么、测了什么、PASS/FAIL、APK SHA、marker、音频常设勾选、视觉截图、30Hz 数字、卡顿排查结论、OSS 出处摘要）  
3. `coordination/HANDOFF.md` 顶部追加本刀摘要（中文可）  
4. 可接受停点示例：「游戏模式确认 / 自动发现 PASS（内置+外置）/ 音频常设 PASS / 视觉 PASS / 30Hz 基本稳（或未完全稳但已锁 30 并排除 X）/ 下一刀建议（40Hz 或继续排卡）」  
5. 停；等用户派下一刀（40Hz 同窗抬档、自适应分辨率、双 SoC、PS5+NS、或 60 探路）

---

## 8. SESSION_OPENER（可粘贴给 Codex）

```
刀号 P-OPT-HONOR-GTPRO-30hz-game-mode｜写手=Codex。
对齐 SSOT：coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md（Universal APK + External Game Data + Runtime Device Profile；荣耀=mid）。引用 P-DIST-1 / P-DIST-2；派工见 coordination/docs/DISPATCH-PLAN-honor-after-baseline.md。

工作树：优先 /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro；若不存在则新建 .worktrees/honor-gtpro-30hz（独立分支）。禁止写入 RG477M 基线验收脏树；禁止塞进 RG477M Codex 会话。

设备 ONLY：荣耀 GT Pro adb -s A42BBB5109000333（ROD2-W69 / SM8635 / Adreno 735）。禁碰 RG477M；禁 adb kill-server；禁卸 RG477M 包。

目标模式：游戏模式（非狂暴）下，1080P 稳 ~30Hz present 可玩为第一闭环；同窗证明后再冲 1080P/40Hz；禁直接冲 60Hz。狂暴档数字作废。Profile 期望 mid。

傻瓜安装硬门：自动发现数据目录（拷数据到常见路径 + 只装 APK → 自动扫描启动；换机同包）。禁手填绝对路径；内置+外置换路径复测；不够盖外置则先查行业案例再扩扫描，写 OSS-NOTES。

验收必须完整包含 coordination/prompts/_STANDING-audio-acceptance.md（蓝牙 A2DP / NONE / fence_spin=0 / DISCONNECTED 重开流等）。视觉：无 UI 遮盖、建筑特效正常、无新黑屏、不复发 RG16、等比例留边不拉伸裁切。

遇问题先查行业/社区公开案例取经再改；写 .local/honor-gtpro-opt-30hz-game-mode/OSS-NOTES.md（URL/commit）；无出处大改 = FAIL。

先查所有权防双写；多 Agent 可开但文件不重叠；不声称可玩；不 push。完整提词：coordination/prompts/P-OPT-HONOR-GTPRO-30hz-game-mode.md
```

---

## 9. 优先级速查（给写手）

1. 所有权 / 新会话 / honor 工作树 / 不双写 / 不碰 RG477M / 禁 adb kill-server  
2. 傻瓜安装自动发现（§0.1b / §5.0；对齐 P-DIST-1）  
3. 常设音频（`_STANDING-audio-acceptance.md`）  
4. 视觉不回退  
5. 游戏模式确认 + 锁 30 / 1080P + 卡顿排查  
6. OSS-NOTES 出处  
7. （可选）同窗后再 40Hz  
8. 交付 RESULT + HANDOFF；停

# FFRS Android 移植｜业界最佳实践（单 APK + 数据包 + 运行时档位）

> **日期**：Asia/Shanghai **2026-10-31**  
> **适用范围**：Final Fantasy 共鸣 / FFRS 主机→Android 移植；目标「换机只换机、不换包」。  
> **仓位**：本文件主份写在基线仓 `ff-resonance-rg477m-native`；若日后 `ff-resonance-honor-gtpro` 就绪，复制一份到新仓同路径。  
> **状态**：产品真源（可执行）。具体刀号见 `coordination/product/DISPATCH-QUEUE-2026-10-31.md` 与 `coordination/prompts/`。  
> **写手分工硬约定**：复杂实现 → **Codex GPT-6.1 Terra（高）**；基础活 → **Antigravity（agy）** 吃额度。

---

## 0. 一句话模型

**交付 = 一个通用 arm64 APK + 一份机型无关游戏数据包。**  
换机 = 把数据包拷到新机常见路径 + 安装同一 APK。  
机型差异 **不** 拆多套安装包，而用 **运行时档位 profile**（分辨率 / 目标 fps / 可选图形开关）自动或手动选择。

这与市面常见「移植手游 / 主机→Android」做法对齐：安装器负责发现数据；引擎负责按档位跑；音频与视觉有硬门，禁止为冲帧永久砍缓冲。

---


## 0.1 演进原则（硬｜站在 RG477M 可玩基线上）

用户补充硬约束（Asia/Shanghai **2026-10-31**）：

1. **站在已完成的 RG477M 可玩基线上演进**  
   - 当前基线仓 `ff-resonance-rg477m-native` 已达约 **游戏模式稳 ~30Hz 可玩**（战斗略卡可接受的里程碑）。  
   - 一切新机（荣耀等）与产品化工作 **禁止闭门造车 / 重写无关子系统**。  
   - **承认现有实现有不少 BUG**：修复与扩展都必须基于 **现码 + 现 APK/数据模型**，不是另起一套「理想架构」。

2. **遇问题先查业界 / 开源 / 社区公开案例**  
   - 与现有 `P-OPT-*` §3.1 同级：先调研再大改；写 `OSS-NOTES.md`（须含 **URL / commit / 帖子日期**）。  
   - **无出处大改 = 整刀 FAIL**。

3. **复用已有钩子，禁止为「干净架构」推倒重来**  
   - 安装发现：扩 `resolveGameDir` / `candidateGameDirs` / `hasGame` / `refreshSetup` / `allFilesAccess`，勿另起无关安装器。  
   - 音频：守 `coordination/prompts/_STANDING-audio-acceptance.md` 救回点（NONE、缓冲、TIMEOUT 重试、DISCONNECTED 重开、`fence_spin=0`），勿重写音频栈。  
   - 限帧：接现有 `gfx-target-fps` / `FFRS_TARGET_FPS` / `xbg_refresh_target_fps`，勿另造第二套帧节奏真相源。  
   - **先对照基线行为再改荣耀**：同一 APK/数据模型下，RG477M 已知可玩路径是对照；荣耀侧差分只解释「同模型为何卡/为何扫不到」，禁止借机重写基线测树。

4. **验收口吻**  
   - 可说「相对基线：未回退 / 扩展了 X」；禁止声称「从零重做且发行级通关」。

---

## 0.2 与并行 SSOT 对齐（同日另一份）

同日另有发行 SSOT：`coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md`，刀序 `P-DIST-1` / `P-DIST-2` / 旧 `P-OPT-HONOR-*`，派工见 `coordination/docs/DISPATCH-PLAN-honor-after-baseline.md`。

| 本文档体系 | 并行体系 | 关系 |
|---|---|---|
| 本文件（product/） | `docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md` | **同模型**（单 APK + 外置数据 + 运行时档位）；本文补强 **§0.1 演进原则** 与 **Terra/agy 写手分工** |
| `P-AGY-INSTALL-autodiscover` | `P-DIST-1-foolproof-install-autodiscover` | **同范围**（自动发现）；本刀写手指定 **Antigravity**；若 DIST-1 已派则勿重复开发现刀 |
| `P-CX-TERRA-device-profiles` | `P-DIST-2-device-profile-runtime` | **同范围**（profile）；本刀写手指定 **Codex Terra 高**；命名 `handheld_mid`/`tablet_sd8s` 与 DIST 的 low/mid/high **对齐映射**（handheld_mid≈low，tablet_sd8s≈mid，odin_elite≈high） |
| `P-CX-TERRA-HONOR-30hz` | 旧 `P-OPT-HONOR-GTPRO-30hz-game-mode` | 本刀为 **并行安全修订**（新仓+Terra）；旧文作细节附录 |

**派工规则**：同一范围只开一刀。优先已开的 DIST 会话；若尚未开，用本文 **agy/Terra** 提词（带模型与 §0.1）。两套文档冲突时：交付物模型以发行 SSOT 为准，演进原则与写手模型以本文为准。

---
## 1. 交付物模型

| 交付物 | 内容 | 是否随机型变 |
|---|---|---|
| **APK** | 单一通用 **arm64-v8a** 包（主包建议 `org.ffrs.gamewindow`；隔离实验可用 `perfisolated` 等） | **否**（同一 APK 装所有目标机） |
| **数据包** | 至少 `main.nso` + `romfs/`（含 `romfs/FFRS/Content/Paks/` 关键容器：`FFRS-Switch.pak/.ucas/.utoc`、`global.ucas/.utoc` 等） | **否**（与 SoC/GPU 无关） |
| **可选优化包** | shader / PSO 缓存等，按 **GPU 驱动版本** 分目录缓存 | 可随驱动变；**非**必装数据包；换机可重建 |

**硬规则**

1. 禁止为 RG477M / 荣耀 / Odin 各打一份「必装」APK 或必装数据包。  
2. 禁止把绝对路径写进发行配置冒充「安装完成」。  
3. 基线仓（当前 `ff-resonance-rg477m-native`）冻住 **RG477M 可玩 ~30Hz** 对照；新机（荣耀等）在 **独立项目目录 / 独立会话** 推进，禁止双写基线测树。

---

## 2. 安装与自动发现

### 2.1 用户路径（产品话术）

1. 用户把完整数据包拷到内存卡 / 外置存储 / 内置常见路径（如 `/sdcard/FFRS` 或 secondary volume 上的 `FFRS`）。  
2. 用户 **只安装 APK**（冷启；不依赖手填路径）。  
3. App 启动 → **自动扫描** → `resolveGameDir` 非空 → setup/加载 → 启 guest。  
4. 换机重复 1–3，**同一 APK + 同一份数据**。

### 2.2 工程钩子（现有，扩不要另起炉灶）

以源码为准再核，当前主入口在：

- `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`
  - `hasGame(File)`：`main.nso` 可读 + `romfs/` 为目录  
  - `candidateGameDirs()`：候选列表（须覆盖内置 + 外置常见挂载）  
  - `resolveGameDir()`：Intent `game_dir` 可调试优先，**验收禁止只靠 adb 注入**  
  - `refreshSetup()` / `startGuest()`：找不到必须中文引导，禁静默黑屏  
  - `allFilesAccess()` / 存储权限引导

### 2.3 All files access / SAF 策略

| 策略 | 何时用 | 注意 |
|---|---|---|
| **MANAGE_EXTERNAL_STORAGE（All files access）** | 深扫外置卡、按名找 `main.nso`+`romfs` 的首选路径（Android 11+） | 须引导用户到系统设置授权；RESULT 记授权状态 |
| **SAF / Storage Access Framework** | 用户拒绝 All files、或厂商锁死外置可见性时的兜底 | 选一次目录后可持久化 URI；仍禁止「只有 SAF 才算 PASS」弱化自动发现 |
| **App-specific dirs** | `getExternalFilesDir` / `getExternalFilesDirs` 作候选之一 | 便于调试，**不能**当唯一发行路径 |

**禁止**

- 手填绝对路径作为发行验收。  
- 无界递归扫整盘拖死冷启。  
- 按机型写死唯一路径。  
- 完整 NSP 自动解包流水线塞进安装刀（另刀）。

### 2.4 换机验收（P0 硬门）

- 冷启同一 APK，内置路径 + 至少一种外置/secondary 路径各复测一次，`resolveGameDir` 非空且日志有 `game_dir=…`。  
- 逻辑机型无关：改扫描后须说明为何不会破坏 RG477M 侧发现。  
- 本刀真机可先在荣耀 `A42BBB5109000333`；**禁止** `adb kill-server`、禁止抢 RG477M 前台测包。

---

## 3. 机型差异 = 运行时档位 profile（非多 APK）

### 3.1 探测输入（自动）

启动时采集（能读则读，读不到则降级到「保守默认 + 用户覆写」）：

- SoC：`ro.soc.model` / `ro.board.platform` 等  
- GPU：`GL_RENDERER` / Vulkan device name（如 Adreno 735、Mali-G615）  
- 面板：`wm size`、短边、刷新率  
- 可选：可用内存、热节流迹象（只作日志，不作唯一判据）

### 3.2 Preset 表（最小集；可扩）

| profile id | 典型机 | 目标分辨率口径 | 目标 fps | 默认系统档假设 |
|---|---|---|---|---|
| `handheld_mid` | Anbernic **RG477M**（MT6897 / Mali-G615） | **720P** 量级 | **30**（可玩基线）；同窗后再冲 **40** | **游戏模式**（非狂暴） |
| `tablet_sd8s` | 荣耀 GT Pro（SM8635 / Adreno 735） | **1080P** 量级 | **30** 第一闭环；同窗后再 **40** | **游戏模式**（非狂暴） |
| `odin_elite` | Odin 3 / 同类旗舰掌机（预留） | TBD（通常 ≥1080P） | 预留；**禁默认 60** | 游戏模式 |

**接线**

- 目标 fps → 现有 `gfx-target-fps` marker / `FFRS_TARGET_FPS` / `xbg_refresh_target_fps`（源码默认可能是 60——**以日志+marker 为准，勿假设**）。  
- 分辨率口径 → 现有 render/scale 路径；未实现自适应前，用档位固定验收口径。  
- **用户可覆写** profile（设置项或 marker）；覆写须写入 RESULT。  
- **禁止默认冲 60Hz**；狂暴/性能狂暴档下的 fps **作废**，不得写 PASS。

### 3.3 默认模式

验收与发行默认：**游戏模式**，不是狂暴。写手只读确认系统档，勿擅自改厂商性能档。

---

## 4. 音频硬门（常设）

**验收必须完整包含** `coordination/prompts/_STANDING-audio-acceptance.md`。

若新仓树尚无该文件：从基线仓复制到新仓同路径后再开工；提词里写明复制来源。

救回点摘要（细节以 standing 文件为准）：

- AAudio 默认 **`PERFORMANCE_MODE_NONE`**（禁默认 `LOW_LATENCY`）  
- 按 burst 设缓冲；蓝牙大 burst 吃得下 A2DP 抖动  
- `TIMEOUT` 可重试；`DISCONNECTED(-899)` 自动重开流  
- **`fence_spin_us` / `gfx-fence-spin-us` 默认 0**  
- **30Hz 流畅为音频+可玩基线**；抬帧每步复测蓝牙；禁为冲帧永久砍薄缓冲  
- 仅扬声器顺滑 **不算** PASS；须蓝牙 A2DP 野外/战斗各 ≥2 分钟主观连续

---

## 5. 图形与性能

1. **验收帧率**以运动中 present / 约定 marker 为准；title 计数不作唯一 PASS。  
2. 锁档顺序：**先稳 30（游戏模式）→ 同窗证明 → 再冲 40**；禁本阶段直接冲 60。  
3. 禁为冲帧永久砍音频缓冲或默认打开 `fence_spin>0`（无蓝牙 A/B 则 FAIL）。  
4. **shader / PSO 缓存**：按 GPU 驱动版本分目录缓存；属可选优化包，丢了可重建，不进「必装数据包」。  
5. 遇卡顿：先读 logcat / gfx markers / 热节流 / 残留实验标记，再改；大改前写 `OSS-NOTES.md`（URL/commit）；无出处大改 = FAIL。

---

## 6. 输入（可后置）

目标映射：**PS5（DualSense）+ Nintendo Switch Pro / 同类**。  
分期放到 **P3**；前期刀禁止弄坏现有触控/键鼠路径即可。

---

## 7. 视觉验收（硬）

- 无 UI 遮盖 / 异常叠层  
- 建筑与特效正常  
- 无新全黑 / 中央黑块；不复发 RG16 FATAL  
- 面板画幅 **等比例留边**（勿拉伸裁切字幕/按钮）

截图归档进当刀 `.local/...`；主观「可玩」口语 **不等于** 声称发行质量通关。

---

## 8. 工程纪律

| 规则 | 说明 |
|---|---|
| OSS / 社区先调研 | 阻塞时先查公开案例，写 `OSS-NOTES.md`；无出处大改 = FAIL |
| 基线演进 / 禁推倒重来 | 见 §0.1：站在 RG477M 可玩基线上扩现码；复用 resolveGameDir/音频救回点/gfx-target-fps；禁闭门造车 |
| 防双写 | 一功能一树一会话；文件所有权不重叠 |
| adb 带序列号 | 荣耀只用 `adb -s A42BBB5109000333`；RG477M 用 `adb -s RG477M01445122`；禁 `adb kill-server` 抢总线 |
| 基线只读保可玩 | `ff-resonance-rg477m-native`（及其上正在测的 RG477M 树）不因新机实验被破坏 |
| 新机独立目录 | 优先 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`；未就绪则等目录，或临时 staging，禁止塞进 RG477M 验收会话 |
| 不 push / 不改 main | 除非用户另授 |
| 共享大数据只读 | `.local/romfs` 等大 pak/ucas **只读**；禁删 |

---

## 9. 分期路线图

| 期 | 目标 | 主写手 |
|---|---|---|
| **P0** | 安装器 + 自动发现 + **双机同一 APK/数据** 冷启可达 | **agy**（扫描/脚本/复测清单）；Terra 仅在权限/存储 API 卡死时介入 |
| **P1** | 档位表落地 + 荣耀游戏模式 **稳 ~30**（1080P 口径） | **Terra**：profile 引擎 + 荣耀卡顿根因；agy 采集/装包协助 |
| **P2** | 同窗后再冲 **40**；自适应分辨率产品化 | **Terra** 主导；agy 采集对照 |
| **P3** | PS5+NS 手柄深化；更多机型 profile（含 `odin_elite`） | 按刀拆：映射可 agy 骨架，复杂输入/震动 Terra |

---

## 10. 写手分工表

| 角色 | 模型 | 适合 | 不适合 |
|---|---|---|---|
| **Codex Terra** | GPT-6.1 Terra（高） | NVN/Vulkan 路径、音频救回点、device profile 引擎、荣耀卡顿根因、限帧/present | 纯扫路径清单、重复装包、机械 log 归档（浪费额度） |
| **Antigravity（agy）** | 用户 Antigravity 额度 | `resolveGameDir` 深扫候选、profile JSON 骨架、装包脚本、采集 log/截图、`OSS-NOTES` 骨架、换路径复测清单 | 无出处大改渲染/音频内核；默认冲 60 |

**并行安全四条**

1. 新开会话，不塞进正在跑的 RG477M 验收线程。  
2. 独立 git 目录/分支（荣耀优先新仓；禁止与基线测树双写）。  
3. adb 一律 `-s <serial>`；禁 kill-server；禁抢 RG477M 前台。  
4. 共享大数据只读。

---

## 11. 相关真源

- 常设音频：`coordination/prompts/_STANDING-audio-acceptance.md`  
- 派工队列：`coordination/product/DISPATCH-QUEUE-2026-10-31.md`  
- P0 安装发现提词：`coordination/prompts/P-AGY-INSTALL-autodiscover.md`  
- P1 档位提词：`coordination/prompts/P-CX-TERRA-device-profiles.md`  
- P1 荣耀 30Hz：`coordination/prompts/P-CX-TERRA-HONOR-30hz.md`（并行安全修订；旧 `P-OPT-HONOR-GTPRO-30hz-game-mode.md` 顶部已标迁移）  
- RG477M 30Hz 对照：`coordination/prompts/P-OPT-RG477M-30hz-game-mode.md`

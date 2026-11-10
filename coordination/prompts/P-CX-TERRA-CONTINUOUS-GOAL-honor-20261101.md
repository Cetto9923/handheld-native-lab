# 工单 P-CX-TERRA-CONTINUOUS-GOAL｜Codex 持续推进目标（荣耀仓）

> **状态**：可粘贴给 Codex 持续干（用户授权持续推进）  
> **写手**：**Codex · GPT-6.1 Terra（高）**  
> **工作树 ONLY**：`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`  
> **设备 ONLY**：`adb -s A42BBB5109000333`；禁 `adb kill-server`；禁碰 `RG477M01445122`；禁写/清理 `ff-resonance-rg477m-native`  
> **会话**：独立新会话；禁止塞进 RG477M 验收线程  
> **前置已 PASS**：`P-AGY-INSTALL-autodiscover`（RESULT：`coordination/results/P-AGY-INSTALL-autodiscover-RESULT.md`）  
> **修订**：2026-11-01 — 荣耀验收 **1080P / 60Hz**；数据包真源写全，**禁止再向用户索要路径**

---

## 0. 数据包 SSOT（写死；缺件自补齐后继续）

### 0.1 PC 完整发行包（只读真源）

根目录：

`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/release-pack-20261028/FFRS/`

| 项 | 路径 | 约大小（2026-11-01） |
|---|---|---|
| 主程序 | `main.nso` | ~120 MB |
| SDK | `sdk.nso` | ~12 MB |
| 资源 | `romfs/` | ~10 GB |
| Shader 缓存目录 | `shaders/` | ~5.8 GB |
| **整包** | 上表合计 | **~16 GB** |

荣耀仓软链（只读）：

`ff-resonance-honor-gtpro/.local/romfs` → `…/release-pack-20261028/FFRS/romfs`

### 0.2 荣耀平板目标布局

目标根：`/storage/emulated/0/FFRS/`（与 PC 包同构）

| 项 | 平板路径 | 2026-11-01 现场备注 |
|---|---|---|
| `main.nso` | `/storage/emulated/0/FFRS/main.nso` | 已有 |
| `sdk.nso` | `/storage/emulated/0/FFRS/sdk.nso` | 已有 |
| `romfs/` | `/storage/emulated/0/FFRS/romfs/` | 约 10G，已有 |
| `shaders/` | `/storage/emulated/0/FFRS/shaders/` | **现场约 2G，小于 PC 5.8G → 视为不完整，须从 PC `shaders/` 补推** |

候补路径（自动发现已通，勿当唯一）：  
`/storage/emulated/0/Android/data/org.ffrs.gamewindow/files/game`（同构拷贝亦可）

### 0.3 `shaders.hnla`（单文件形态）

- 引擎若要 **单文件** `shaders.hnla`（非目录）：PC 只读副本  
  `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen/.local/recovered-delivery-018-staging/HandheldNativeLab/data/shaders.hnla`  
  （约 **6.0 GB**，2026-11-01）  
- **优先**确认运行时是否只需 `FFRS/shaders/` 目录；目录不够再 push/放置 `shaders.hnla` 到平板 `FFRS/` 根或引擎约定位置  
- **禁止**为找包而改写/删除基线 recovery 目录；只读复制或 `adb push`

### 0.4 缺件补齐标准动作（自执行，勿问用户）

```bash
# 示例：补齐平板 shaders 目录（设备 ONLY）
adb -s A42BBB5109000333 push \
  /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/release-pack-20261028/FFRS/shaders/. \
  /storage/emulated/0/FFRS/shaders/

# 若确认需要 hnla：
adb -s A42BBB5109000333 push \
  /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen/.local/recovered-delivery-018-staging/HandheldNativeLab/data/shaders.hnla \
  /storage/emulated/0/FFRS/shaders.hnla
```

补齐后冷启验证 `resolveGameDir` 仍非空，再继续 Phase A/B。  
**禁止** `adb kill-server`；**禁止**对 `RG477M01445122` 操作；**禁止**修改基线仓业务文件（push 源只读）。

---

## 1. 持续目标（按序）

### Phase A — device profiles
`coordination/prompts/P-CX-TERRA-device-profiles.md`（数字冲突以本文为准）

- `handheld_mid`：720P / **30**（文档对照）  
- `tablet_sd8s`：荣耀 → **1080P / 60**  
- `odin_elite`：预留  
- 接现有 fps/分辨率钩子；不重做安装器；音频救回点不回退；OSS-NOTES → RESULT → B

### Phase B — 荣耀 1080P/60Hz 可玩
参考 `P-CX-TERRA-HONOR-30hz.md` / `P-OPT-HONOR-GTPRO-30hz-game-mode.md`，**验收改为 1080P 稳 ~60Hz**

- 常设音频：`_STANDING-audio-acceptance.md`（扬声器可先过并注明；有蓝牙再补）  
- 不破坏 autodiscover；视觉不回退；狂暴数字不作 PASS  
- RESULT 后停

### 硬约束
站在 RG477M 可玩基线演进；禁闭门造车；无出处大改=FAIL；romfs/发行包只读；不 push git、不改 main、不双写基线脏树。

---

## 2. SESSION_OPENER（整段粘贴）

```
持续目标｜P-CX-TERRA-CONTINUOUS-GOAL（修订）｜Codex GPT-6.1 Terra（高）｜用户授权持续推进；禁止再向用户索要数据包路径。

工作树 ONLY：/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro
设备 ONLY：adb -s A42BBB5109000333
禁：adb kill-server；禁碰 RG477M01445122；禁写/清理 ff-resonance-rg477m-native；禁双写基线 .worktrees/codex-black-screen。

荣耀验收：游戏模式 1080P 稳 ~60Hz。

数据包 SSOT（已写死）：
- PC完整包只读：/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/release-pack-20261028/FFRS/（main.nso+sdk.nso+romfs~10G+shaders~5.8G，整包~16G）
- 平板目标：/storage/emulated/0/FFRS/（同构）。现场 shaders 曾约2G<PC5.8G → 从 PC shaders/ adb push 补齐后继续
- 荣耀仓 .local/romfs → 该包 romfs（只读 symlink）
- shaders.hnla 若需要：…/codex-black-screen/.local/recovered-delivery-018-staging/HandheldNativeLab/data/shaders.hnla（~6GB只读）push 到平板 /storage/emulated/0/FFRS/shaders.hnla；优先验证 shaders/ 目录是否已够
- 缺件自补齐，勿停问用户。全文见 coordination/prompts/P-CX-TERRA-CONTINUOUS-GOAL-honor-20261101.md §0

前置 PASS：P-AGY-INSTALL RESULT=coordination/results/P-AGY-INSTALL-autodiscover-RESULT.md。勿重做安装器。

按序：1) device-profiles（tablet_sd8s=1080P/60）→RESULT  2) 荣耀1080P/60+常设音频→RESULT停
产品真源：coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md
常设音频：coordination/prompts/_STANDING-audio-acceptance.md
演进：RG477M可玩基线；OSS-NOTES；无出处大改=FAIL；音频救回点不回退。
```

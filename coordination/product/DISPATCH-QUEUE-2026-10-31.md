# 派工单草稿｜DISPATCH-QUEUE-2026-10-31

> **日期**：Asia/Shanghai **2026-10-31**  
> **用途**：给助手派工跟进；顺序建议 **agy 安装发现 → Terra 档位 → Terra 荣耀 30Hz**。  
> **产品真源**：`coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md`  
> **演进硬约束（写入每刀）**：站在 RG477M 可玩基线上演进；禁闭门造车/推倒重来；遇事先查 OSS 写 OSS-NOTES（URL/commit）；无出处大改=FAIL；复用 `resolveGameDir` / 音频救回点 / `gfx-target-fps`。  
> **仓位**：基线 `ff-resonance-rg477m-native`（本队列主份）；荣耀新仓 `ff-resonance-honor-gtpro` **尚未就绪**（2026-10-31 核）——P0/P1 工作树写「优先新仓，未就绪则 PENDING」。  
> **写手**：基础 → Antigravity；复杂 → Codex **GPT-6.1 Terra（高）**。

---

## 建议顺序

### D1｜P-AGY-INSTALL-autodiscover

| 项 | 值 |
|---|---|
| 写手 | Antigravity（agy） |
| 模型 | 用户 Antigravity 额度 |
| 提词 | `coordination/prompts/P-AGY-INSTALL-autodiscover.md` |
| opener 首行 | `刀号 P-AGY-INSTALL-autodiscover｜写手=Antigravity（基础活）｜产品真源 coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md §0.1+§2。` |
| 工作树 | 优先 `…/ff-resonance-honor-gtpro`；未就绪 → PENDING，不改基线业务源码 |
| 设备 | 荣耀 `A42BBB5109000333`；禁 kill-server；禁碰 RG477M |
| 依赖 | 无（可先于档位）；只读基线对照 |
| 验收一句 | 冷启同一 APK，内置+外置各一路径自动 `resolveGameDir` 非空，禁手填路径冒充 PASS |

### D2｜P-CX-TERRA-device-profiles

| 项 | 值 |
|---|---|
| 写手 | Codex |
| 模型 | GPT-6.1 Terra（高） |
| 提词 | `coordination/prompts/P-CX-TERRA-device-profiles.md` |
| opener 首行 | `刀号 P-CX-TERRA-device-profiles｜写手=Codex｜模型=GPT-6.1 Terra（高）｜产品真源 coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md §0.1+§3。` |
| 工作树 | 优先荣耀新仓或独立分支；禁与 RG477M 测树双写 |
| 设备 | 实现机型无关；真机荣耀 `-s A42BBB5109000333`；对照 `handheld_mid` 不打断 RG477M 前台 |
| 依赖 | 建议 D1 发现路径可用（或明确沿用现候选） |
| 验收一句 | 自动/覆写选档后接到 `gfx-target-fps` 等，荣耀落到 ~1080P/30，禁默认 60，音频不回退 |

### D3｜P-CX-TERRA-HONOR-30hz

| 项 | 值 |
|---|---|
| 写手 | Codex |
| 模型 | GPT-6.1 Terra（高） |
| 提词 | `coordination/prompts/P-CX-TERRA-HONOR-30hz.md`（并行安全修订；旧 `P-OPT-HONOR-GTPRO-30hz-game-mode.md` 仅作细节附录） |
| opener 首行 | `刀号 P-CX-TERRA-HONOR-30hz｜写手=Codex｜模型=GPT-6.1 Terra（高）｜并行安全修订版。产品真源 coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md §0.1+§3–§5。常设音频 coordination/prompts/_STANDING-audio-acceptance.md（新树若无从基线复制）。` |
| 工作树 | 优先 `…/ff-resonance-honor-gtpro`；**禁止**认领基线 `codex-black-screen` |
| 设备 | ONLY `A42BBB5109000333` |
| 依赖 | D1 至少不破坏发现；D2 档位可并行或先锁 marker=30 |
| 验收一句 | 游戏模式 1080P 稳~30 present 可玩闭环 + standing 音频 + 视觉不回退；不回归基线树 |

---

## 助手跟进备忘

- RG477M 基线测继续独立进行；本队列三刀均不得 `adb kill-server` / 抢 RG477M 前台。  
- 新仓一旦出现：把 `coordination/product/` 与上述三提词 + standing audio **复制到新仓同路径**，再派 D1。  
- 派工时粘对应 SESSION_OPENER；模型选 Terra 高（D2/D3）或 Antigravity（D1）。


## 与并行派工计划对齐

另见 `coordination/docs/DISPATCH-PLAN-honor-after-baseline.md`（P-DIST-1 → P-DIST-2 → P-OPT-HONOR）。

- **D1** ↔ P-DIST-1（同发现范围；本队列指定 **agy**）  
- **D2** ↔ P-DIST-2（同档位范围；本队列指定 **Terra 高**）  
- **D3** ↔ 旧 P-OPT-HONOR / 本队列 `P-CX-TERRA-HONOR-30hz`（并行安全+Terra）  

若 DIST 刀已在跑：**不要**再派同范围 D1/D2；助手跟进时合并验收清单，并强制补上 §0.1 演进原则勾选。



## Dispatch log
- **2026-10-31 23:36 CST** · P-AGY-INSTALL-autodiscover · agy PID **30477** · cwd `ff-resonance-honor-gtpro` · device `A42BBB5109000333` only · scratch `.local/scratch/p-agy-install-20261031-233559`

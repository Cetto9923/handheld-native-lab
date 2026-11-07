# P-CX-TERRA-dark-limit-black-20261106

写手 = Codex GPT-6.1 Terra（高档）。目标：**只修 Honor 平板「暗魔法 + Limit 大招」特效异常纯黑**；先正确性，不追 60Hz / 内部 1080，不扩同步/性能优化。

## 0. 工作树与禁区

- **ONLY** `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`，接续现有脏树 `work/honor-gtpro-30hz`（HEAD 约 `032e5f6` + 大量 WIP）。**认领脏改**，禁止 `reset` / `clean` / `stash -u` / 另开 worktree 覆盖 WIP。
- 开干前必读：`AGENTS.md`、`AGENTS-HONOR.md`、`coordination/HANDOFF.md` **顶部 + 暗魔法/Limit 相关段（约 Nov 5 depth/query/texel/cull 条目）**、本提词。
- 证据资产（只读复用，勿删）：
  - `.local/honor-device-profiles/flame-20261104/dark-depth-pair/`（四套 VS/FS 的 `.xgs` / `.spv` / `.spvasm`）
  - `.local/honor-device-profiles/flame-20261104/shield-candidate/`（present / depth / cull / query 图与 log）
  - `.local/honor-device-profiles/flame-20261104/codex-probe-*/`（同帧 capture）
  - `.local/baselines/honor-flame-stable-20261104/`（shield/fire 基线，勿破坏）
  - `coordination/prompts/_STANDING-audio-acceptance.md`（音频硬门）
- **禁**：碰 `ff-resonance-rg477m-native` 主仓与 `.worktrees/codex-black-screen`；禁 `adb kill-server`；禁清 `/storage/emulated/0/FFRS` / app files / 存档；禁卸载重装清数据；禁把旧 RG16 / 最终合成 alpha=0 / LUT ABI 当成本刀首因重做。
- 设备：`adb -s A42BBB5109000333` ONLY（Honor ROD2-W69 / Adreno 735）。**真机若不确定空闲，先只做离线分析**；需要 install/冷启时写明最小操作与影响，等协调后再动。占用期间只改代码不碰机。

## 1. 已定性（勿再绕）

用户症状：暗魔法、Limit 大招特效显示为**异常纯黑**（圆盘/块），其它场景大体可玩。

HANDOFF / 抓帧已证明：

1. **黑块在 HDR color0 / 中间图已存在**，早于最终 present/compositor。不是 SurfaceView alpha / RGBX 合成那类「整世界被乘黑」（那类已修）。
2. **暗魔法**候选对（同帧 index 6960×2）：
   - 深度预通道：VS `db5f1e2dd1c9ef3d` / FS `c0adc062e4b8e8fa`（写深度）
   - 颜色通道：VS `41a5a80e5242a26c` / FS `b7dd0f48aea00196`（depth EQUAL、不写深度）
   - 独立 samples-passed：预通道可达 **~十万～百万**，颜色 **0**（VK_SUCCESS / available1）。
   - 两 FS 都显式写 `FragDepth`（含 `FMin(FragCoord.z, computed)` / Fma）。
3. 已证伪/不够：
   - EQUAL→ALWAYS：颜色可有几百样本，预通道仍几十万～百万，**黑圆仍在** → 不能只改 compare 交付。
   - cull NONE：颜色仍 0 → 不是单纯背面剔除。
   - 多项 UBO / texel GPU 读回成对相等；vertex/index CPU 快照成对相等 → **输入快照匹配 ≠ 输出正确**。
4. **Limit** 是**另一对** shader（例：prepass VS`085afbc955a0744f`/FS`0066ad2ea935247d`；color VS`632c207422d773c5`/FS`17ab021c45c63390`），颜色用 **GEQUAL**，与暗魔法不同。可同刀修，**禁止假定同一根因**；先打穿暗魔法，再对 Limit 单独定性。
5. 暂停采集常**不执行目标 effect draw** → 禁止再用「暂停后抓帧」当同相位证据。
6. 火焰 R32_SINT/R16_UINT、SSBO17 overlay、同步世代等可参考，**不得**再泛化删同步 / 全局 waitIdle / 空 FS / 跳 draw 来宣布修复。

## 2. 本刀唯一主线

**把「颜色 samples=0」拆成可证伪的第一错误 GPU 输出。**

优先顺序（每次只验一个假设）：

1. **离线 / 受控同帧重放**：复用已有有效 capture（如 `codex-probe-24194` / `3882` / `1031` / `31699` 等含暗魔法对的帧），不要依赖暂停错相位。
2. 在相同几何 / instance / UBO / texture 前提下，对比：
   - 两 VS 实际 `Position`（及关键 varying）
   - 两 FS 实际 `FragDepth` / discard / kill
   - 逐像素：没 rasterize vs FS discard vs depth fail vs 颜色写出为 0
3. 补齐尚未闭合的输入缝：binding18 的 2 字节（需 GPU 可见证明，勿信可能 stale 的 CPU map）、binding42 sampled image 语义、descriptor view/format、producer→consumer barrier / 资源世代。
4. Limit：在暗魔法有「第一错误输出」定性后，单独对 Limit pair 做同样拆分（注意 GEQUAL），禁止直接套暗魔法补丁。

**禁止**作为「修复」提交：ALWAYS/删预通道/全局关 depth/空 FS/禁颜色写/泛化关缓存/全局 waitIdle/跳 effect draw。诊断探针必须 opt-in（marker），默认路径保持原语义。

## 3. 开源对照（先查再改）

- 先查 Switch 模拟器 / NVN→Vulkan / UE 粒子 additive 相关：depth-only prepass + color EQUAL、FragDepth 显式写、MRT/HDR 格式、blend DstAlpha 在 RGBX host 上的语义（参考 Ryujinx #3553 / commit `ad47bd2`：RGBA 模拟 RGBX 时 DstAlpha→One、OneMinusDstAlpha→Zero，且 RT 格式变更要刷新 blend）。
- 落 `OSS-NOTES` 短条目：出处 URL + 与本刀假设的对应关系。**无出处的大改 = FAIL**（站在现有 WIP/抓帧上升级，禁止闭门推倒）。

## 4. 验收（硬）

在**普通非 probe** 运行（marker0）：

1. 暗魔法特效可见且**非异常纯黑圆/块**；遮挡与场景行为正确（不以「黑像素比例下降」单独 PASS）。
2. Limit 大招同标准；若本刀只打穿暗魔法，Limit 必须写明「未过 + 下一刀假设」，不得冒充共因已修。
3. 对照同阶段 Switch 参考或用户确认；保留 shield / fire / boss 已知好行为。
4. 音频：遵守 `_STANDING-audio-acceptance.md`（默认 AAudio PERFORMANCE_MODE_NONE；野外/战斗各 ≥2 分钟主观顺滑；设备变更重开流；不回退已知好缓冲策略）。帧率上去但声音断 = 整刀 FAIL。
5. 交付：`coordination/results/P-CX-TERRA-dark-limit-black-RESULT.md`（修复前后同条件证据路径、APK/source SHA、已过/未过项、OSS-NOTES 指针）+ 更新 `coordination/HANDOFF.md` 顶部。

## 5. 停点

- 打穿暗魔法「第一错误 GPU 输出」并有最小正确性补丁 + 真机非 probe 验收；或
- 证明当前证据不足以闭合流，列出唯一缺口与最小设备操作请求后停下汇报。

不要空转堆诊断 APK。不要宣称 60 / 内部 1080。做完停下，等助手独立核对。

# 工单 P-CX-TERRA-HONOR-30hz｜Codex Terra：荣耀 GT Pro 游戏模式稳 ~30Hz（并行安全修订版）

> **状态**：正式可派（复杂刀；**并行安全修订**，取代旧 opener 路径）  
> **写手**：**Codex**  
> **模型**：**GPT-6.1 Terra（高）**  
> **取代/迁移**：旧提词 `coordination/prompts/P-OPT-HONOR-GTPRO-30hz-game-mode.md` 仍保留作史实与细节附录；**新开会话请用本文件**。旧文件顶部已标迁移。  
> **产品真源**：`coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md`（§0.1 演进原则 + §3–§5 + §8）  
> **音频硬门**：验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`（新树若无 → **从基线仓复制**后再开工）  
> **对照基线**：RG477M 可玩 ~30Hz（游戏模式）为行为对照；**不回归 / 不双写**基线测树  
> **工作树**：优先 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`；**禁止**再认领基线 `.worktrees/codex-black-screen` 做荣耀冲刺（该树留给/或已服务 RG477M 基线）  
> **设备 ONLY**：荣耀 `adb -s A42BBB5109000333`（ROD2-W69 / SM8635 / Adreno 735）。禁碰 `RG477M01445122`；禁 `adb kill-server`  
> **会话**：独立新会话；禁止塞进正在跑的 RG477M 验收线程  
> **日期**：Asia/Shanghai **2026-10-31**

---

## 0. 演进原则（硬｜用户 2026-10-31）

1. **站在已完成的 RG477M 可玩基线上演进**；承认现实现多 BUG；修复/扩展基于现码 + 现 APK/数据模型。  
2. **禁止闭门造车 / 重写无关子系统**；禁止为「干净架构」推倒重来。  
3. 遇卡顿/黑屏/权限问题：先查业界/开源/社区公开案例，写 `.local/honor-gtpro-opt-30hz-game-mode/OSS-NOTES.md`（URL/commit）；**无出处大改 = FAIL**（与旧 P-OPT §3.1 同级）。  
4. **复用钩子**：`resolveGameDir` 族、音频救回点、`gfx-target-fps` / `xbg_refresh_target_fps`；先对照基线行为再解释荣耀差分。  
5. 旧提词里工作树曾指向 `…/ff-resonance-rg477m-native/.worktrees/codex-black-screen`——**本修订纠正**：荣耀刀改走新项目目录；基线树只读对照。

---

## 1. 用户优先级（硬）

1. 傻瓜安装 / 自动发现（可依赖 agy 刀成果；本刀至少不破坏，且冷启能发现）  
2. **游戏模式 1080P 稳 ~30Hz present 可玩**为第一闭环（运动中稳；禁用狂暴数字冒充）  
3. 同窗证明后再冲 1080P/40；**禁直接冲 60**  
4. 音频不回退（standing 全文）  
5. 视觉不回退（无遮盖、特效/建筑正常、等比例留边、不复发 RG16）  
6. OSS-NOTES 先于无出处大改  
7. 不声称发行级通关；不双写；不 push；不改 main

---

## 2. 并行安全门禁

| 规则 | 要求 |
|---|---|
| 目录 | 新仓 `ff-resonance-honor-gtpro` 优先；未就绪则等或独立 clone/worktree，**勿写**基线 dirty 测树 |
| 会话 | 新开 Codex 会话；不塞 RG477M 验收线程 |
| adb | 仅 `-s A42BBB5109000333`；禁 kill-server；禁抢 RG477M 前台 |
| 大数据 | 共享 romfs/pak **只读** |
| 对照 | RESULT 写明相对 RG477M 基线：未回退哪些钩子；荣耀差分假设 |

细节排查步骤可继续参考旧 `P-OPT-HONOR-GTPRO-30hz-game-mode.md` §1–§7（卡顿账本、marker、隔离包等），但 **工作树/opener/写手模型以本文件为准**。

---

## 3. 验收清单（摘要）

- [ ] 游戏模式确认（非狂暴）；狂暴数字作废  
- [ ] `gfx-target-fps`（或等价）锁 **30**；日志非默认 60 冒充  
- [ ] 运动中稳 ~30 present 可玩闭环；战斗卡顿有记录与下一步  
- [ ] standing audio 全文勾选（蓝牙 A2DP）  
- [ ] 视觉 checklist  
- [ ] 自动发现未破坏（或注明依赖 agy 刀状态）  
- [ ] OSS-NOTES 有出处  
- [ ] 未操作 RG477M；未双写基线  
- [ ] RESULT + HANDOFF；停

---

## 8. SESSION_OPENER（可粘贴给 Codex Terra）

```
刀号 P-CX-TERRA-HONOR-30hz｜写手=Codex｜模型=GPT-6.1 Terra（高）｜并行安全修订版。产品真源 coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md §0.1+§3–§5。常设音频 coordination/prompts/_STANDING-audio-acceptance.md（新树若无从基线复制）。

演进硬约束：站在 RG477M 可玩~30Hz 基线上扩现码；禁闭门造车/推倒重来；遇事先查 OSS 写 OSS-NOTES（URL/commit）；无出处大改=FAIL。复用 resolveGameDir/音频救回点/gfx-target-fps；先对照基线再改荣耀。

工作树优先 /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro；禁止认领基线 .worktrees/codex-black-screen 做本刀；独立新会话；禁止塞进 RG477M 验收线程。

设备 ONLY：荣耀 adb -s A42BBB5109000333（ROD2-W69/SM8635/Adreno735）。禁碰 RG477M；禁 adb kill-server。

目标：游戏模式 1080P 稳~30Hz present 可玩第一闭环；同窗后再40；禁直接60；狂暴档数字作废。音频/视觉不回退。卡顿排查可参考旧 P-OPT-HONOR-GTPRO-30hz-game-mode.md 细节，但路径与模型以本文件为准。完整提词：coordination/prompts/P-CX-TERRA-HONOR-30hz.md
```

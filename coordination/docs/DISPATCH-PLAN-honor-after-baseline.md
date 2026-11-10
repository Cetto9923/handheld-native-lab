# 派工计划｜基线冻结后荣耀推进（给 PM）

> **日期**：Asia/Shanghai **2026-10-31**  
> **SSOT**：`coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md`  
> **用户意图**：未来 **一个 APK + 一份数据包** 适配多机；RG477M 作 low 档基线；荣耀 GT Pro 作 mid 档下一目标。

---

## 1. 目录与所有权

| 目录 | 用途 | 派工规则 |
|---|---|---|
| `ff-resonance-rg477m-native`（或已 rename 的 `*-BASELINE-20261031`） | **RG477M 基线**：代码 + APK + 数据只测/只留 | **禁止**派荣耀写入；战斗卡顿测试可继续只读验收 |
| `ff-resonance-honor-gtpro`（由并行任务创建） | 荣耀开发树 | 一切荣耀写手 **只在此目录**（或其后的独立 worktree） |
| 共享 `.local/romfs`（若软链） | 只读数据包 | 禁删大 pak/ucas |

## 2. 会话规则

1. **RG477M 基线目录只测/只留**，不派荣耀写入。  
2. **荣耀新开 Codex 会话**，工作目录 = honor 项目（勿塞进 `01a186a6` 等 RG477M 验收线程）。  
3. `adb` 一律带序列号：荣耀 `A42BBB5109000333`；RG477M 另一序列号；**禁 `adb kill-server`**。  
4. 提词文件可在两边 `coordination/prompts` / `docs` 同步；写手改代码只在 honor 树。

## 3. 建议刀序

1. **P-DIST-1（写手=Antigravity）**（若傻瓜安装/自动发现未过）→ 见 `coordination/prompts/P-DIST-1-foolproof-install-autodiscover.md`  
2. 若发现已硬过：可 **直接 P-OPT-HONOR**（内含发现硬门）→ `P-OPT-HONOR-GTPRO-30hz-game-mode.md`  
3. **P-DIST-2** 可与上并行（所有权：profile 模块 ≠ 扫描函数）→ `P-DIST-2-device-profile-runtime.md`  
4. mid 档游戏模式 **稳 ~30** PASS → 同窗 **40** → **双机回归**（同 APK SHA + 同数据包）→ 手柄  

音频：每一刀 RESULT 必须引用并勾选 `_STANDING-audio-acceptance.md`。

## 4. 跟进节奏（助手）

1. 写手停点 → 交 `RESULT.md` + APK SHA + `adb -s` 证据。  
2. 助手核对：序列号是否仅荣耀、是否手填路径、游戏模式否、音频常设、视觉、profile 日志。  
3. PASS → 派下一刀；FAIL → 回滚点写清后再开短线程（禁肥会话续聊堆料）。  
4. **本文件不代替提词**；开写手前把对应 `SESSION_OPENER` 整段粘进新 Codex 会话。

## 5. 一句话给用户

基线冻住玩 RG477M；荣耀另树另会话冲「同 APK + 同数据 + mid 档 1080P/30」；发现与 Profile 两刀打底，再抬 40，最后双机回归。

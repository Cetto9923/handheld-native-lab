# 工单 P-AGY-NIGHT-GOAL｜Antigravity 夜间目标模式（荣耀 GT Pro）

> **状态**：可粘贴开跑（用户已暂停红果/WorkBuddy，设备夜间归 FFRS）  
> **写手**：Antigravity（agy）  
> **工作树**：`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`（分支 `work/honor-gtpro-30hz`）  
> **设备**：仅 `adb -s A42BBB5109000333`；**禁** `adb kill-server`；**禁**碰 `RG477M01445122` / `ff-resonance-rg477m-native` 业务与 `.local`  
> **基线只读**：共享 `.local/romfs` 只读；禁止清理/移动基线测包产物  
> **日期**：Asia/Shanghai **2026-10-31 夜间 → 次日早**

---

## 0. 夜间总目标（按序，做完停）

1. **收口** `P-AGY-INSTALL-autodiscover`（傻瓜安装 / 自动发现）→ 写 RESULT  
2. 若 1 已 PASS：只做**准备态**读 `P-CX-TERRA-device-profiles.md` + OSS 骨架，**不**大改渲染/音频；复杂档位实现留给次日 Codex Terra  
3. **禁止**冲 40/60、禁止重写 NVN/音频救回点、禁止双写基线仓

---

## 1. 已知现场（2026-10-31 ~23:55，接着干勿推倒）

- 装包脚本已有：`scripts/android/install_honor_game_window_apk.sh`  
- 荣耀 APK 已 Success；冷启 `GameActivity` OK  
- 自动发现已证据：`resolveGameDir: autodiscovered game_dir=/storage/emulated/0/FFRS`（logcat）  
- 曾在把数据 `mv` 到应用 `files` 做第二路径；若中断，先查路径现状再继续，**勿无界删盘**  
- 完整验收见：`coordination/prompts/P-AGY-INSTALL-autodiscover.md`  
- 产品真源：`coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md` §0.1 + §2  
- 音频常设（本刀不大改内核，但触播放须知晓）：`coordination/prompts/_STANDING-audio-acceptance.md`

演进硬约束：站在 RG477M 可玩基线上扩现码；禁闭门造车；遇事先查 OSS 写 `OSS-NOTES.md`（URL/commit）；无出处大改=FAIL。复用 `resolveGameDir` / `candidateGameDirs` / `hasGame` / `refreshSetup` / `allFilesAccess`。

---

## 2. 本夜必交付（P-AGY-INSTALL）

- [ ] 冷启**不**靠 `am start --es game_dir` 冒充 PASS  
- [ ] 至少 **两条** 数据路径各自冷启一次，`resolveGameDir` 非空并记 logcat：  
  - 内置常见（如 `/storage/emulated/0/FFRS`）  
  - 应用私有或外置/secondary 之一（已有 mv 计划则完成并复测）  
- [ ] 缺数据 → setup 引导屏（可临时移走数据验证，测完还原）  
- [ ] 装包脚本带 `-s` 序列号；换路径复测清单落盘  
- [ ] `OSS-NOTES.md` ≥2 条公开出处（若扩了扫描 API）  
- [ ] `RESULT.md` 写入：`coordination/results/P-AGY-INSTALL-autodiscover-RESULT.md`（或 `.local/scratch/.../RESULT.md`）含包名、APK SHA、两条 `game_dir=` 证据、未碰 RG477M 声明  
- [ ] **停**。勿自动开冲帧刀。

---

## 3. 设备与并行门禁

| 项 | 规则 |
|---|---|
| 荣耀 serial | `A42BBB5109000333` only |
| RG477M | 禁 adb / 禁装卸 / 禁前台 |
| 基线仓 | 只读对照文档与源码；**不写** |
| 红果/WorkBuddy | 用户已暂停；若发现前台非 FFRS 且长时间占机，暂停真机步骤写 NOTE，改做文档/脚本 |
| 旧会话 | 若已有白天 agy PID 仍在跑：先读其 scratch/`RESULT`，**勿开第二会话双写同一工作树**；空转/卡死再新开本夜会话 |

---

## 4. SESSION_OPENER（整段粘贴给 Antigravity）

```
夜间目标模式｜刀号 P-AGY-NIGHT-GOAL｜写手=Antigravity｜用户已暂停红果，设备夜间归 FFRS。

工作树 ONLY：/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro
设备 ONLY：adb -s A42BBB5109000333
禁：adb kill-server；禁碰 RG477M01445122；禁写/清理 ff-resonance-rg477m-native 及其 .local；禁冲帧；禁大改 NVN/音频救回点。

先读并收口：coordination/prompts/P-AGY-INSTALL-autodiscover.md
已知：荣耀装包 Success；冷启已 autodiscover game_dir=/storage/emulated/0/FFRS。接着做第二路径复测+缺数据 setup 屏+OSS-NOTES+RESULT，做完停。
完整夜间说明：coordination/prompts/P-AGY-NIGHT-GOAL-honor-20261031.md
产品真源：coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md §0.1+§2
常设音频（知晓即可）：coordination/prompts/_STANDING-audio-acceptance.md

若同树已有白天 agy 仍在跑：接续其 scratch，禁止双写。空转再新开。交付 RESULT 后停止，勿自动开 Terra 改码。
```

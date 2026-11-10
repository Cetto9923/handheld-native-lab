# 工单 P-AGY-INSTALL-autodiscover｜Antigravity：傻瓜安装 / 自动发现数据目录

> **状态**：正式可派（基础刀；吃 Antigravity 额度）  
> **写手**：**Antigravity（agy）**  
> **模型**：用户 Antigravity 额度（基础活）  
> **产品真源**：`coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md`（必读 §0.1 演进原则 + §2 安装发现）  
> **常设音频**：本刀 **不大改** 音频内核；若装包/冷启触及播放，仍须知晓 `coordination/prompts/_STANDING-audio-acceptance.md`（新树若无则从基线复制）  
> **设备优先**：荣耀 GT Pro `adb -s A42BBB5109000333`；**禁** `adb kill-server`；**禁**碰正在测的 RG477M 前台 / `RG477M01445122` 装卸包  
> **工作树**：优先 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`；**若尚无** → 停在准备态，opener 用占位路径，等新目录就绪再动手；**禁止**往基线 RG477M 验收会话/脏测树里塞本刀  
> **日期**：Asia/Shanghai **2026-10-31**

---

## 0. 演进原则（硬｜用户 2026-10-31）

1. **站在已完成的 RG477M 可玩基线上演进**；承认现码有不少 BUG；修复/扩展基于 **现码 + 现 APK/数据模型**。  
2. **禁止闭门造车 / 重写无关子系统**；禁止为「干净架构」推倒重来。  
3. 遇问题先查业界/开源/社区公开案例，写 `OSS-NOTES.md`（URL/commit）；**无出处大改 = FAIL**（与 P-OPT §3.1 同级）。  
4. **复用** `GameActivity` 的 `hasGame` / `candidateGameDirs` / `resolveGameDir` / `refreshSetup` / `startGuest` / `allFilesAccess`；扩扫描，勿另起安装器。  
5. 先对照基线行为再改荣耀侧发现逻辑；勿破坏 RG477M 已知可达路径。

---

## 1. 范围（做 / 不做）

### 做

- 完善/验证 `resolveGameDir` 深扫与候选路径（内置 + 外置卡 / secondary `StorageVolume` / `getExternalFilesDirs`）。  
- 浅层按名找含 `main.nso`+`romfs` 的 `FFRS` 目录（禁无界整盘递归拖死冷启）。  
- All files access / 权限引导与 setup 中文文案；Paks 缺文件点名。  
- 装包脚本（adb `-s` 带序列号）；换路径复测清单（内置 + 外置至少各一）。  
- `OSS-NOTES.md` 骨架（Android 多存储卷扫描、SAF、同类移植/模拟器如何找 game dir）。  
- RESULT：冷启自动发现证据（日志 `game_dir=…`）；说明为何机型无关、不破坏 RG477M。

### 不做

- **不**大改渲染 / NVN / present / 限帧内核。  
- **不**大改音频救回点实现（`audio_bridge.*`）。  
- **不**实现 device profile 引擎（那是 `P-CX-TERRA-device-profiles`）。  
- **不**冲帧、不默认 60、不碰 RG477M 测包前台。  
- 完整 NSP 解包流水线、按机型写死唯一路径、要求用户手填绝对路径冒充 PASS。

---

## 2. 工作树与设备门禁

| 项 | 值 |
|---|---|
| 新仓（优先） | `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro` |
| 基线（只读对照） | `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` |
| 荣耀 serial | `A42BBB5109000333` |
| RG477M serial | `RG477M01445122`（**本刀禁操作**） |
| 新仓未就绪 | opener 写 `WORKTREE_PENDING=/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`；只读基线文档；**不写**基线业务源码 |

共享 `.local/romfs` 大文件 **只读**；禁删 pak/ucas。

---

## 3. 验收清单

- [ ] 读过最佳实践 §0.1 / §2；读过本文件  
- [ ] 工作树为荣耀新仓（或明确 PENDING 未改基线源码）  
- [ ] 冷启同一 APK：至少 **两种** 数据路径（内置常见 + 外置/secondary）`resolveGameDir` 非空  
- [ ] **未**使用 `adb am start --es game_dir …` 作为唯一 PASS 证据  
- [ ] 找不到数据 → setup 引导屏；无静默黑屏  
- [ ] OSS-NOTES 至少 2 条带 URL/commit 的公开出处（若扩了扫描 API）  
- [ ] 未 `adb kill-server`；未碰 RG477M 前台  
- [ ] RESULT + 换路径复测清单落盘；停

---

## 8. SESSION_OPENER（可粘贴给 Antigravity）

```
刀号 P-AGY-INSTALL-autodiscover｜写手=Antigravity（基础活）｜产品真源 coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md §0.1+§2。

演进硬约束：站在 RG477M 可玩基线上扩现码；禁闭门造车/推倒重来；遇事先查 OSS 写 OSS-NOTES（URL/commit）；无出处大改=FAIL。复用 resolveGameDir/candidateGameDirs/hasGame/refreshSetup/allFilesAccess。

工作树优先 /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro；若尚无则 WORKTREE_PENDING，只读基线文档，禁止改基线业务源码/禁止塞进 RG477M 验收会话。

设备：荣耀 adb -s A42BBB5109000333 only。禁 adb kill-server；禁碰 RG477M01445122 前台/装卸。

范围：完善自动发现+候选/外置卡+装包脚本+换路径复测清单。不大改渲染/音频内核；不做 profile 引擎；不冲帧。

验收：冷启只装 APK，内置+外置各一路径 resolveGameDir 非空；禁手填绝对路径冒充 PASS；缺数据须 setup 屏。完整提词：coordination/prompts/P-AGY-INSTALL-autodiscover.md
```

# 工单 P-DIST-1｜傻瓜安装自动发现（Universal APK + External Game Data）

> **状态**：正式派工（新开短线程；禁止肥会话续聊）  
> **写手**：**Codex**  
> **对齐 SSOT**：`coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md`（Universal APK + External Game Data + Runtime Device Profile）  
> **设备优先验收**：荣耀平板 **GT Pro** `adb -s A42BBB5109000333`（ROD2-W69 / SM8635 / Adreno 735）。**扫描逻辑必须机型无关**，不得破坏 RG477M 侧发现；本刀 **禁止碰 RG477M 真机写操作**（可用文档/逻辑论证 RG477M 不被写死路径破坏）。  
> **工作树**：若已存在 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro` → **优先用该目录**；否则写「待 PM 指定 `ff-resonance-honor-gtpro` 或独立 worktree（如 `.worktrees/honor-dist-1`）」——**禁止写入正在做 RG477M 验收的脏树当唯一交付**。  
> **并行隔离**：新 Codex 会话；`adb -s` 仅荣耀；禁 `adb kill-server`；禁卸/重装 RG477M 包；禁抢 RG477M 前台。  
> **音频**：验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`（本刀若只改扫描且不装可玩包，至少确认未回退音频源码救回点；一旦装包进游戏须全勾）。  
> **硬禁止**：不 push；不改 `main`；不 `reset --hard` / `clean -fd`；不删 `.local/romfs/**`；不手填路径冒充 PASS；不为每机分包。

---

## 0. 目标（写死）

闭环：**用户把游戏数据拷到常见路径 + 只装 APK → App 自动扫描发现并启动**。

1. 内置存储常见路径 **至少一种** PASS（建议 `/sdcard/FFRS` 或等价）。  
2. 外置 / secondary storage 常见挂载 **至少一种** PASS（以设备实测为准）。  
3. 冷启、无 Intent `game_dir`、无手填绝对路径。  
4. 扫描逻辑机型无关；不得依赖「仅 RG477M 绝对路径」。  
5. 找不到数据 → `refreshSetup` 中文引导；禁静默挂死。

---

## 0.1 开工前（读-only）

1. 读本文件 + SSOT + `_STANDING-audio-acceptance.md`。  
2. 读 `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`：`hasGame` / `candidateGameDirs` / `resolveGameDir` / `refreshSetup` / `startGuest` / 存储权限。  
3. 确认工作目录归属（honor 项目 vs 待 PM 指定）；`git status -sb`；若另一 agent 在写同一文件 → **停**。  
4. 查行业/社区：Android 多存储卷扫描、`StorageVolume` / `getExternalFilesDirs`、移植/模拟器如何发现 game dir → 写入 `OSS-NOTES.md`（URL/commit）；无出处大改 = FAIL。

---

## 1. 允许改动

- 扩展 `candidateGameDirs` / 浅层按名扫描（含 `main.nso` + `romfs/`）。  
- `getExternalFilesDirs` / `StorageVolume` / secondary 挂载枚举。  
- 权限引导与 setup 文案；Paks 缺文件点名。  
- 去掉「仅某机绝对路径才能玩」的隐性唯一依赖（可保留为候选之一）。

## 2. 禁止

- 完整 NSP 解包流水线；无界整盘深扫。  
- 手填路径 / 按机型改配置当验收。  
- Intent `game_dir` 当正式 PASS。  
- 改数据包布局；删 romfs；为机型拆 APK。  
- 本刀范围内大改 gfx / 冲帧（交给 P-OPT-HONOR）。

---

## 3. 验收清单

- [ ] 荣耀：内置路径冷启自动发现 PASS；logcat 打出 `game_dir=…`  
- [ ] 荣耀：外置/secondary 路径冷启自动发现 PASS  
- [ ] 无 `adb am start --es game_dir` 冒充  
- [ ] 缺数据 → setup 引导可见  
- [ ] RESULT 写明为何 RG477M 扫描不被破坏（逻辑论证；本刀不强制上 RG477M 真机）  
- [ ] `OSS-NOTES.md` 有出处  
- [ ] APK SHA-256 记入 RESULT（若本刀产出 APK）  
- [ ] 未回退音频救回点源码；若进游戏则常设音频全勾  
- [ ] 未 push；未碰 RG477M 写操作；未 `adb kill-server`

---

## 4. 交付

1. `.local/dist-1-foolproof-install/OSS-NOTES.md`  
2. `.local/dist-1-foolproof-install/RESULT.md`  
3. `coordination/HANDOFF.md` 顶部短摘要  
4. 停；等 PM 派 P-DIST-2 / P-OPT-HONOR

---

## 5. SESSION_OPENER（可粘贴给 Codex）

```
刀号 P-DIST-1-foolproof-install-autodiscover｜写手=Antigravity。
对齐 SSOT：coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md（Universal APK + External Game Data + Runtime Device Profile）。

工作树：若存在 /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro 则用之；否则待 PM 指定 honor 目录或独立 worktree（.worktrees/honor-dist-1）。禁止写入正在做 RG477M 验收的脏树当唯一交付。

设备：优先荣耀 GT Pro adb -s A42BBB5109000333。禁碰 RG477M 写操作；禁 adb kill-server；新会话并行隔离。

目标：拷数据到常见路径 + 只装 APK → 自动发现启动。内置+外置至少两种路径 PASS。扩展 GameActivity hasGame/candidateGameDirs/resolveGameDir；禁手填路径；Intent game_dir 仅调试。扫描必须机型无关，不得破坏 RG477M 发现逻辑。

验收必须完整包含 coordination/prompts/_STANDING-audio-acceptance.md（若装包进游戏则全勾；仅改扫描也不得回退音频救回点源码）。遇事先查行业案例写 OSS-NOTES；无出处大改=FAIL。不 push；不删 romfs。完整提词：coordination/prompts/P-DIST-1-foolproof-install-autodiscover.md
```

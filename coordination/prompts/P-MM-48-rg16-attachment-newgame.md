# 工单 P-MM-48｜MiniMax Code｜RG16 绘制目标 → 越过「选难度闪退」

> **状态**：正式派工（从 Codex 肥会话摘出的下一刀；禁止继续帧率/读回 A/B）
> **工作树**：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen`
> **分支**：`task/T02-codex-black-screen` @ `0444c81`（其上已有未提交的 RG16 proxy 改动，**认领现有脏工作，勿另开平行树覆盖**）
> **设备**：`adb -s RG477M01445122`（在线）
> **用户优先级（硬）**：先能玩（选难度进场景）→ 再谈帧率。本单**禁止** 30/60 FPS、readback_cached、interval 补丁、adaptive。

---

## 0. 你是谁 / 边界

你是 MiniMax Code 写手。只做本单闭环，做完停下写 HANDOFF。  
**不 push、不改 main、不碰其他 worktree（p-mm-*）、不声称标题/可玩/游戏帧已通。**  
不要调用 agy / Claude；不要再去「找 NSO / 找头文件」——本单不需要。

---

## 1. 现状一句话（先读证据，再动手）

用户路径：**开始游戏 → 选普通难度 → 确认** 后闪退/退出。

APK58 guest 已越过标题与难度 UI，加载到游戏内纹理时 fatal：

```
.local/codex-runs/58/guest-final.log
101429:[engine] NVN RG16_UNORM filterable representation RG32_SFLOAT serial=7226 guest_bpp=4 image_bpp=8 normalized_bits=16
101430:[engine] FATAL: Converted RG16_UNORM attachment writes require UNORM16 quantization
101431:[engine] FATAL: Converted RG16_UNORM attachment writes require UNORM16 quantization
```

对应纹理：`nvnTextureInitialize serial=7226 fmt=0x12(RG16) 1384x780`（同文件 L100964）。

**设计已定（不要改方向）**：canonical 仍是 `VK_FORMAT_R16G16_UNORM`（4 B/texel）给 attachment / storage / clear / resolve / transfer / CPU readback；仅当采样需要线性过滤且设备对 RG16 无 LINEAR、对 RG32_SFLOAT 有 SAMPLED|LINEAR 时，才启用私有 **RG32_SFLOAT proxy** 给采样。  
**禁止**：把 RT 改成 float、降精度到 RG8、改 nearest 糊弄、跳过绘制。

参考文档（必读）：

1. `.local/codex-rg16-proxy/design-and-checkpoint.md`
2. `.local/codex-rg16-proxy/pool-fix.md`
3. `.local/codex-rg16-proxy/freeze-manifest.json`
4. `coordination/HANDOFF.md` 顶部 +「User priority update 2026-10-25」段

---

## 2. Codex 已做到哪、你从哪接

| 项 | 状态 |
|---|---|
| 采样侧 RG16→RG32 proxy 设计与引擎改动 | 已在工作树 `xbg_game.cpp` 等（见 freeze sources SHA） |
| 组件 `rg16_proxy_contract.android` SHA `778ec5fe…` | 已冻；host / ASan / NDK 链过 |
| 真机 `--gpu-proxy` 旧二进制 | **失败** SIGSEGV139，`fault addr 0x10`，null-pool 解引用（见 `device-root-crash.log`） |
| pool-fix（null-safe warning + 真实 pool fixture + 负例） | **源码已修，纠正后的组件真机复测 = NOT_RUN** |
| APK59 + NewGame 复测 | **未做** |

你的任务 = **把纠正后的组件在真机跑绿 → 打进 APK → 复测选难度路径 → 记录新的第一致命（若有）**。

---

## 3. 必改 / 可碰文件（只这些）

**主路径（已有脏改，继续）：**

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp`
- `src/graphics/nvn_vulkan/engine/xbg_texture.c` / `.h`（freeze 写明本 unit 未再改；仅在证据要求时动）
- `src/graphics/nvn_vulkan/engine/xbg_rg16_proxy.comp` + `xbg_rg16_proxy_spv.h`
- `tests/native/rg16_filter_contract.cpp`

**构建 / 装包（按仓库现有脚本，勿发明新流水线）：**

- 现有 Android / game-window CMake 与 APK 打包脚本（先在仓库内 `rg` 找到当前 APK58 是怎么打的，复用同一条）
- 日志目录：`.local/codex-runs/59/`（新建）

**只读证据：**

- `.local/codex-runs/58/guest-final.log`、`newgame-confirmed.json`、截图 `title.png` / `difficulty.png`
- `.local/codex-rg16-proxy/*`
- `.local/codex-layer/device-formats.log`（Mali：`RG16_UNORM linear=0`，`RG32_SFLOAT linear=1`）

**禁止改：**

- `work/rg477m-native` 主树、其他 `.worktrees/p-mm-*`
- 60FPS derived NSO / `game_dir` 覆盖
- FPS / readback_cached / WSI interval 相关逻辑
- Layer 大补丁乱合（`freeze-manifest` 写 pending；本单不主动合 Layer）

---

## 4. 执行步骤（按序，写完再下一步）

### Step A — 认领现场（5 分钟内完成）

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen
git status -sb
git rev-parse HEAD
shasum -a 256 src/graphics/nvn_vulkan/engine/xbg_game.cpp
# 期望接近 freeze：4c8b15648bc9d7453b989a6672669a59ab68faf0ec04a7eaba1883776cb4b398
```

若 SHA 对不上：先 `diff` 对 `.local/codex-rg16-proxy/engine-before-pool-fix.cpp` 与 freeze，**不要 reset --hard**。报告差异后在现有改动上修。

### Step B — 真机跑纠正后的组件（必须先绿）

1. 按 `.local/codex-rg16-proxy/run-command.txt` + `build-commands.txt` 重链 `rg16_proxy_contract.android`（确认二进制 SHA ≠ 旧崩溃那份；目标是 freeze 的 `778ec5fe…` 或你重编后的新 SHA，写入 manifest）。
2. push 到设备临时目录（可沿用 `/data/local/tmp/ffrs-rg16-proxy-20261025/` 或新建带日期目录）。
3. 跑 `--gpu-proxy`，**必须 exit 0**。验收点（来自 design）：
   - 原生 RG16 attachment + 多次 blended draw
   - 禁止「预混合量化 / float-target 末尾量化」那两条错误参考曲线
   - canonical/proxy 多 mip/layer 读回与采样一致
   - refresh/dispatch 计数符合设计
4. 保存：`.local/codex-rg16-proxy/device-root-gpu-rerun.log` + `device-root-rerun-manifest.json`（含二进制 SHA、exit、时间）。

若仍 SIGSEGV：用 `adb logcat -b crash` 对照是否还是 `pool->size` 空指针；修 fixture/生产路径后重跑，**不要跳过组件直接打 APK**。

### Step C — 打 APK59（原始 30FPS 资源）

1. **必须**用原始 `main` / 默认 `game_dir`（**不要** `--es game_dir ...60fps...`）。
2. 安装到 `RG477M01445122`，记录 APK SHA256 → `.local/codex-runs/59/manifest.json`。
3. 自动化或半自动复现：`开始游戏 → 普通难度 → 确认`（可参考 run58 的输入脚本；`confirm-yes.png` 曾误判，**以 guest 日志是否越过 serial=7226 FATAL 为准**）。
4. 拉回：
   - `.local/codex-runs/59/guest.log`（完整）
   - 标题/难度/确认后截图各一张
   - `adb logcat -b crash -d -t 200` → `crash.txt`

### Step D — 判定

| 结果 | 你做什么 |
|---|---|
| 不再出现 `Converted RG16_UNORM attachment` / 同类 FATAL，guest 继续加载 | 在 HANDOFF 记「RG16 RT 越过」；把**新的第一致命/停点**原文贴出（含前后 20 行）。**停。不要顺手优化帧率。** |
| 仍同一 FATAL | 对照 design：确认 attachment 走 canonical 77 而非 proxy；补齐量化/写回；组件加回归后再装包。 |
| 新崩溃（Vulkan/校验层/别的 format） | 原样记录；本单可修「明显同族的下一张 RT/采样」若仍是 RG16/BC 族；否则停下交 HANDOFF。 |

---

## 5. 验收清单（全部勾上才算本单完成）

- [ ] 组件 `--gpu-proxy` 真机 exit 0，日志入 `.local/codex-rg16-proxy/`
- [ ] APK59 安装，manifest 含 SHA；**未**使用 60FPS derived game_dir
- [ ] 复测「开始→普通难度→确认」；guest 日志证明越过原 FATAL（或证明仍在并给出修复差分）
- [ ] 更新 `coordination/HANDOFF.md` 顶部 5–10 行 + `coordination/state.json` blockers
- [ ] 可选：`git commit`（信息用仓库惯例的 feat/fix 句式）；**不 push**
- [ ] **未**声称可玩/标题通关；**未**做 FPS 优化

---

## 6. 粘贴开场（给 MiniMax 对话框）

```
你是 MiniMax Code 写手。工单 P-MM-48：越过「选难度后闪退」——RG16 绘制目标（canonical UNORM16 + 采样用 RG32 proxy）。

工作树：/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen
分支 task/T02-codex-black-screen @ 0444c81（认领现有脏改，勿 reset、勿另开树）。

先完整阅读：
coordination/prompts/P-MM-48-rg16-attachment-newgame.md
.local/codex-rg16-proxy/design-and-checkpoint.md
.local/codex-rg16-proxy/pool-fix.md
.local/codex-rg16-proxy/freeze-manifest.json

证据：.local/codex-runs/58/guest-final.log L101429–101431 FATAL Converted RG16_UNORM attachment writes require UNORM16 quantization（serial=7226 fmt=0x12）。

立刻执行：真机复跑纠正后的 rg16_proxy_contract --gpu-proxy（必须 exit0）→ 打 APK59（原始30FPS game_dir）→ 复测开始游戏/普通难度/确认 → 记录是否越过该 FATAL 及新的第一致命。禁止帧率/60FPS补丁/readback A/B。不 push。做完停下写 HANDOFF。
```

---

## 7. 给调度的回报模板（写进 HANDOFF 顶部）

```
P-MM-48:
- component --gpu-proxy: PASS/FAIL (SHA=…, log=…)
- APK59 SHA=… game_dir=default/original
- NewGame/Normal: 越过 RG16 FATAL? Y/N
- 新第一致命原文: …
- 未做: FPS / push / 可玩声称
```

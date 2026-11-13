# RG477M：第 3 章水都卡顿与画面异常 — 遗留诊断标记拖垮整会话（task/T-rg477m-passprof-window-20261113）

- 日期：2026-11-13
- 设备 ONLY：RG477M（`RG477M01445122`，Mali-G615 MC6，Android ARM64，1280×960）。荣耀平板 `A42BBB5109000333` 不在本分支范围，未做任何操作。
- 仓库：`ff-resonance-rg477m-native`；分支：`task/T-rg477m-passprof-window-20261113`；worktree：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`
- 基线：`2af4652`（`work/rg477m-native` HEAD）
- 本分支提交：**`0228e3c`**（诊断开关改为窗口/帧作用域）
- 改动文件：仅 `src/graphics/nvn_vulkan/engine/xbg_game.cpp`（+13 / −3）
- 构建：`.local/android-apk/ffrs-game-window-debug.apk`，sha256 `8bda346dbd6d58ac72661bbf8b2a5ccb0f71e39deb68789911229d7df25b6657`
- 宿主契约：`python3 scripts/verify_host.py --runtime-contracts` → **exit 0，全部契约 passed**
- 设备安装：`adb install -r -t` → Success（签名沿用荣耀树同一把开发密钥，cert SHA-256 `d11967a3…`，存档保留）

## 1. 一句话结论

设备上遗留的一个 `gfx-pass-profile` 诊断标记，让引擎**整个会话**都留在慢路径上：间接参数每帧被 CPU 回读 → 命中待写回区间 → 每帧排干整个提交队列（fence 约 46 ms/帧）；而且 CPU 读到的是还没落地的旧值，**那个每帧一次的 compute dispatch 被整段跳过**。

带着标记 **11.4 FPS**，去掉标记 **28.6 FPS**。修复后**带着同一个标记**回到 **30.1 FPS**。

## 2. 根因链（代码 + 真机数据）

### 2.1 触发源：CPU 回读间接参数

`xbg_game.cpp:5482`（compute dispatch 的间接参数）：

```cpp
if(!gpu_indirect){cpu_access(indirect_address,12,5);uint32_t xyz[3];memcpy(xyz,...,12);p.x=xyz[0];p.y=xyz[1];p.z=xyz[2];
```

`cpu_access(..., why=5)` → `resolve_pending()`（`:2213`）→ `gpu_sync()`（`:1498`）→ `submit_slots(true)`（`:1280`）→
`:1359-1361` **排干全部 16 个 slot** → 每个 slot 在 `retire_slot()`（`:1175`）里阻塞 `vkWaitForFences`。

真机日志（修复前，水都室内场景，每 60 帧窗口）：

```
NVN PERF v=1 ... fps=11.36 submit_cpu_ms=69.74 gpu_ms=63.26 submits=11.10 waits=11.10 sync_wb=120 sync_rb=0 ...
NVN PERF parts_ms_per_frame ... fence=46.12/3.0 ...
NVN PERF dispatch indirect=1.0 gpu=0.0 cpu_fallback=1.0 cpu_zero=1.0 cpu_xyz=0/1/1
[engine] NVN CPU_ACCESS_SYNC frame=39 why=5 cur_op=27 addr=0x71f5750000 bytes=12 pending=0x71f5750000+65532
[engine] NVN RESOLVE_PENDING frame=39 n_wb=5 n_rep=3
```

- `why=5` 的 CPU 读命中 `pending` 写回区间 → 触发全队列排干。
- `cpu_zero=1.0` / `cpu_xyz=0/1/1`：CPU 读到的间接参数是**全 0**（GPU 还没写），于是
  `if(!p.x||!p.y||!p.z)return;` —— **该 compute dispatch 每帧被整段跳过**（GPU 上少跑一个 dispatch）。

### 2.2 为什么 GPU 常驻路径没走

`indirect_dispatch_inputs_resident()`（`:5300`）的门禁里有一项 `pass_prof_enabled`。这个标志由
`gfx-pass-profile` 标记文件置位（`:6879`），而且**一旦置位就整个会话不再清零**。

于是这条链成立：

```
设备上留着一个 gfx-pass-profile 标记
  → pass_prof_enabled = true（整个会话）
  → indirect_dispatch_inputs_resident() 恒返回 false
  → compute dispatch 只能走 CPU 回读（gpu_indirect = false）
  → 命中待写回区间 → resolve_pending() → 排干 16 槽 → fence 46 ms/帧
  → 且读到旧值（全 0）→ dispatch 被跳过
```

标记文件本身是 2026-11-13 16:13 留下的 `0 1 4`（通道分析器的采样窗口：第 0 帧起每帧采 4 帧）。
它本该只影响 4 帧，却因为上面那个常开标志影响了整个会话。

## 3. A/B 实验（只改标记，不改代码）

同一台设备、同一存档、同一场景（`第3章水都的守护者们 / 在水都奥尔德里昂收集情报` 室内），只切换 `gfx-pass-profile` 是否存在：

| 指标 | 带标记（修复前代码） | 不带标记（修复前代码） | 带标记（**修复后代码**） |
|---|---|---|---|
| fps | **11.36 / 11.55** | 28.55 / 29.21 | **30.14 / 30.17** |
| submit_cpu_ms | 57.9 ~ 69.7 | 11.3 ~ 12.2 | 10.46 ~ 10.81 |
| `parts` fence | 46.12 ~ 51.87 ms | 0.00 | 0.00 |
| sync_wb | 120 / 60帧 | 0 | 0 |
| dispatch indirect gpu | 0.0 | 1.0 | **1.0** |
| dispatch cpu_fallback | 1.0 | 0.0 | **0.0** |
| dispatch cpu_zero | 1.0 | 0.0 | **0.0** |

第三列是本次修复的验收点：**同一个遗留标记不再拖垮会话**。

## 4. 修复

`src/graphics/nvn_vulkan/engine/xbg_game.cpp`：

```cpp
// 通道分析器是有界的诊断：只有正在被采样的那一帧才允许走精确计时路径；
// 窗口一过，GPU 常驻间接参数 / GPU 常驻索引绘制这些快路径必须立刻恢复。
inline bool pass_prof_active(){
    return pass_prof_enabled && pass_prof_frame!=UINT64_MAX && stats.presents.load()+1==pass_prof_frame;
}
```

两个快路径门禁从 `pass_prof_enabled` 改为 `pass_prof_active()`：

- `:5300` `indirect_dispatch_inputs_resident()`（GPU 常驻间接 dispatch 参数）
- `:6156` `indexed_graphics_ready()`（GPU 常驻索引绘制）

并在启动横幅里打出通道分析器的真实状态，避免下一次再被静默关掉：

```
[engine] NVN pass profiler enabled=1 next=0 period=1 until=4 (marker gfx-pass-profile; only the sampled frames take the exact-timing path)
```

**没有改的东西**：Descriptor / Barrier / Pipeline 的既有行为、`readback_retire_targeted`、`legacy_waitall`、occlusion 强制可见策略、渲染分辨率与呈现路径，全部未动。没有引入任何机型判断。

## 5. 验收（真机，修复后构建）

帧曲线来自设备 `codex-frame-14585.csv`（本次会话，10320 → 3000 帧窗口，稳态段）：

```
t+100s n=300 mean=33.33ms fps=30.0 p95=37.8 max=56.0  >2帧(66.7ms)=0
t+110s n=300 mean=33.33ms fps=30.0 p95=37.3 max=48.0  >2帧=0
t+120s n=300 mean=33.33ms fps=30.0 p95=36.7 max=57.6  >2帧=0
t+130s n=300 mean=33.33ms fps=30.0 p95=38.9 max=51.8  >2帧=0
t+140s n=188 mean=33.33ms fps=30.0 p95=36.0 max=52.9  >2帧=0
```

- **稳态 30.0 FPS**（目标 25~30 FPS）✔
- **稳态无任何单帧超过 2 帧预算（66.7 ms）**；整段会话里连续超 2 帧的最长连跑是 5 帧，全部落在关卡加载段 ✔
- 关卡加载段仍有一次 4.0 s 的冻结（t+50s），那是 UE 侧的资源加载，不是渲染循环的帧保持。

## 6. 原定三个靶点的代码侧结论

| 靶点 | 结论 | 证据 |
|---|---|---|
| A：CPU 强制等 GPU / fence / resolve_pending 锁死 | **成立且是主因** | `fence=46.12 ms/帧`；`sync_wb=120`；`cpu_access why=5 → resolve_pending → submit_slots(true) → 排干 16 槽` |
| B：水体折射抓屏导致 Render Pass 频繁断裂 | **本场景不成立** | `sync_rb=0`、`readback targeted=0 nowait=0 fallback=0 inflight=0 drain=0` → 本场景零次纹理→缓冲回读；`pass_ends` 合计 64.0 == `passes=64.0`，每 pass 恰好断一次，无额外断裂 |
| C：视锥裁剪缺失导致海量不可见 Draw | **属实但不可修、也不是本场景瓶颈** | NVN draw record 不带包围盒，引擎没有 per-object AABB 数据源；唯一可见性机制是游戏自己发起的 GPU occlusion query（`occlusion=70.9 reports=26.4`）。冻结样本 `gpu_ms=64` 而 `fps=1.65` 说明 GPU 远未饱和 |

详细只读分析见 `.local/reports/perf-fence-analysis-20261113.md`（321 行，含 18 个 marker 的 A/B 矩阵）。

## 7. 未完成 / 风险

1. **陆行鸟画面异常尚未定位**。用户报告：召唤陆行鸟后有概率视角卡成小窗口且无法操作，走路时也会遇到。本次会话中该状态**未复现**，因此没有结论，也没有改动任何相关代码。
   - 已排除：呈现几何（`present source=1280x720 → dst=1280x720 at (0,120) aspect=preserve`，11 次采样一致）、guest 渲染目标（`NVN GPASS 1280x720 att=1`）、视口转换（`xbg_nvn.cpp:5-14` 与 Metal 参考一致）、黑边（4:3 面板上下各 120 px 是 `docs/PROJECT.md` 明文设计，与平板端对比一致）。
   - 仍存活的候选：`nn::oe::GetOperationMode()` 在 RG477M 上返回 **0**（`[port] operation_mode=0 source=default-handheld`），而 P-MM-6 记录 guest 会把 `mode==1` 缓存成字节标志，4 个读点据此选择屏幕缩放因子 `0.026873588`（置位）或 `0.0`（清零）；`HANDOFF.md:2362` 又记录 RG477M 必须 Handheld(0)（Docked → 1920×1080 → 17.3 FPS）。两者冲突，需设备实验判定，尚未做。
2. **`resolve_pending()` 仍排干全部 slot**。定向退役（`retire_through()`）的方案见分析报告 §4.1，语义风险在于：写回必须按提交序落地，较晚 slot 的写回内容此时还不是终值。本次**未实施**，因为水都场景 `sync_wb=0`，收益为 0 而改动面大；它是「其他设备 3.8 FPS」类问题的通用解，需要单独一张工单。
3. **`perf.submits` 指标被污染**：`:6197` 的 owned indexed input copy 不调用 `vkQueueSubmit` 却递增了 `perf.submits` 与 `submit_by[SITE_OTHER]`。真实提交次数要看 `parts_ms_per_frame` 的 `submit=`（水都 3.0/帧）。诊断时不要读 `submits=`。
4. **16 FPS 的一段未解释**：2026-11-13 16:31–16:42，同一场景、同一命令流（draws=403.6 / passes=74 / dispatches=59 / fence=0 / sync_wb=0）却稳定在 16 FPS、`gpu_ms` 59~61；重启后同样状态回到 30 FPS、`gpu_ms` 26。GPU 温度 57.8 ℃、thermal status 0，非降频；devfreq 节点无读权限，未能取证。**待查**。

## 8. 复现与再生成命令

```bash
# 构建（签名需与设备上一致：荣耀树 .local/signing/gamewindow-debug.p12）
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
bash scripts/android/build_game_window_apk.sh

# 宿主契约
python3 scripts/verify_host.py --runtime-contracts   # exit 0

# 设备：装包 → 复现遗留标记 → 进第3章水都
adb -s RG477M01445122 install -r -t .local/android-apk/ffrs-game-window-debug.apk
adb -s RG477M01445122 shell "echo '0 1 4' > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-pass-profile"
adb -s RG477M01445122 shell "am force-stop org.ffrs.gamewindow; am start -n org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity"
adb -s RG477M01445122 shell input keyevent 96          # A = Continue
adb -s RG477M01445122 shell "grep -aE 'PERF v=1|PERF dispatch' /sdcard/Android/data/org.ffrs.gamewindow/files/engine.log | tail -3"
```
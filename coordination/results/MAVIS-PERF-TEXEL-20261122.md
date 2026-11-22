# 帧率优化第 1 版：关闭 texel 驻留副本（Honor GT Pro）

- **设备**：`A42BBB5109000333`（ROD2-W69 / Adreno 735 / vendorID 0x5143），独立 adb 端口 5041 + 无线 192.168.1.121:44689。
- **分支**：`task/T-mavis-perf-20261122`，worktree `.local/worktrees/mavis-perf-20261122`。
- **交付包**：`ffrs-texel-20261122.apk`，SHA-256 `2d60e8b4e90e3201a23b5341d6569c814e000f5cf942ae91ec853ad8ff22ff3b`，
  与在装稳定包 `a45430a8…` **同签名**（同一 `.local/signing/gamewindow-debug.p12`），`install -r` 覆盖安装成功，
  **未卸载、存档未触碰**；设备侧 `texel memo=0` 已确认生效。
- **目标**：在不影响任何特效与显示的前提下提升帧率，稳步推进到所有场景 40 Hz 以上。

## 0. 一句话结论

**第 1 版改动：关闭 texel 驻留副本（跨帧缓存）与逐帧 texel-view memo。**
同场景交错 A/B 证实 **每帧 submit CPU 少约 0.3–1.1 ms、fps +0.5 ~ +1.1**，
**显示回归 PASS（像素级无差异）**。改动零风险、零画面变化，可以出货。
**但 40 Hz 尚未达成**：兵工厂场景冷机 36.0–37.7 fps，连续跑 2 分钟后因热降频掉到 29 fps。
下一步入口已按实测排好（见 §8）。

## 1. 先纠正仪表口径（否则会优化错地方）

上一轮的性能读数有三个坑，这一轮全部踩过并修正，**结论已推翻重写**：

| 坑 | 现象 | 真相 |
| --- | --- | --- |
| **读到后台节流窗口** | `parts_ms_per_frame` 里 `present=16.24 ms/帧`、`descriptor=28 ms/帧`，各部件求和 92 ms 远超 37 ms 的帧时间 | 那些行取自**游戏被切到后台**的窗口（前台是红果短剧），系统节流后 CPU 忙 84 ms/帧。**必须确认前台是游戏再取数** |
| **parts 是 exclusive_ns** | 看起来像可加总 | 确实是 exclusive（每帧口径），但**读错窗口就会自相矛盾**；窗口正确时各部件求和 ≈ submit_cpu，自洽 |
| **DESC_TIMING 用的是累计计数** | `descriptor_ns=423158720` 除以 60 帧 = 7.05 ms/帧，与 parts 的 28 ms 对不上 | `descriptor_report()` 取 `timing.counters[...].exclusive_ns`（**进程启动累计**），而 `first/last` 只是报告窗口；两者不能相除 |

**校正后的场景内基线**（兵工厂，draws 679.8 / dispatches 165 / passes 110）：

```
NVN PERF v=1  fps=36.02  submit_cpu_ms=17.63  present_cpu_ms=0.97  gpu_ms=17.34  submits=9.52  waits=9.52
NVN PERF parts_ms_per_frame timed=60
  other=4.22/9.5  draw=1.86/685.0  dispatch=0.46/165.0  descriptor=5.45/1029.0
  pass=0.21/705.0  buffer=0.20/1.1  pipeline=0.22/204.7  submit=0.01/1.0  fence=0.00/6.6
  present=0.32/1.0  cleanup=1.27/6.6  hash=0.37/392.0  image=0.15/1801.0  log=0.01/0.8
  program=0.18/583.0  cpuwrite=1.21/395.4  snapshot=2.60/5635.2
NVN PERF desc_ms_per_frame  memo_key=0.47  tmpl_update=0.76  alloc=0.12  setkey=0.02  build=5.38  hit_total=2.00
NVN PERF keydiff  tex=154/帧  saddr=12  same=2
```

**帧预算（冷机）**：帧长 27.4 ms = CPU 忙 21.5 ms + 约 6 ms 等 GPU。
CPU 大头排序：**descriptor 5.5–6.6 ms（未命中的整表重建，179 次/帧）> other 4.1 ms（客机帧回调，每 submit 一次，
共 9.5 次/帧）> snapshot 2.6–2.9 ms（每帧 12.4 MB 拷贝，其中顶点 10.15 MB）> draw 1.9–2.5 > cleanup 1.3–1.5 > cpuwrite 1.2–1.3**。

## 2. 屏幕与帧率上限（此前没确认过）

- 平板**内置屏当前跑 90 Hz**（`renderFrameRate=90.0`，模式 4；支持 60/90/120/144）。
- 轻场景（第 1 章探索区、标题画面）实测 **59.9–60.0 fps**，即游戏侧有 60 fps 的呈现上限。
- 兵工厂只有 32–38 fps，**远低于任何刷新率上限 ⇒ 纯 CPU/GPU 瓶颈，省 CPU 直接换帧率**，不存在"等 vsync"借口。

## 3. 零编译交错 A/B：21 个开关全扫一遍

用 `files/gfx-live-set` 逐个开关做 **ON/OFF/ON/OFF** 交错，每个开关 4 个窗口，
判定用**整帧指标**（`submit_cpu_ms` + `fps`），不看单 part（L-042）。

| 开关 | dCpu(ON−OFF) | dFps | 判定 |
| --- | --- | --- | --- |
| **texmemo=0**（关跨帧 texel 驻留副本） | **+1.86 ms** | **−3.34** | **关掉更好** |
| **texelframe=0**（关逐帧 texel-view memo） | **+1.58 ms** | **−3.03** | **关掉更好** |
| vcache=0（关顶点快照缓存） | +1.08 ms | −0.71 | 关掉略好，但与 texel 开关重叠，单独不可靠 |
| dynpartial=0 | +0.86 ms | −0.97 | 保持 ON |
| pipefast=0 | −0.96 ms | +1.41 | 保持 ON |
| xmemo=0 | −0.97 ms | +0.83 | 保持 ON |
| pipememo=0 | −1.09 ms | +0.49 | 保持 ON |
| imgcache / honorsetbypass / honordescrefill / bindcache / ownedinput / rbfullcopy / uppro / gpuindexed / bboxready / texrecord / texdirty / adpf | −1.60 ~ +0.34 ms | −0.46 ~ +0.74 | **全部在噪声内，不动** |

> **扫描值偏大**：单开关扫描的 +3.3 fps 被热漂移放大了（见 §4）。
> 这正是"必须交错"的另一个理由。

## 4. 交错确认（抗热漂移）

### 4.1 组合 A/B（3 配置 × 3 轮交错）

| 配置 | submit_cpu 中位数 | fps 中位数 |
| --- | --- | --- |
| 基线 | 25.18 ms | 31.78 |
| texmemo=0 + texelframe=0 | 24.63 ms | 32.87（**+1.09**） |
| 再加 vcache=0 | 24.88 ms | 33.41（**+1.63**） |

### 4.2 出货包上的最终 A/B（新默认 vs 运行时恢复旧行为，同一个 APK，4 轮配对）

| 轮 | NEW（texel memo 关） | OLD（texel memo 开） | Δcpu | Δfps |
| --- | --- | --- | --- | --- |
| 1 | 18.91 / 36.01 | 19.09 / 35.97 | −0.18 | +0.04 |
| 2 | 18.81 / 36.66 | 18.97 / 37.67 | −0.16 | −1.01 |
| 3 | 28.66 / 29.04 | 29.00 / 28.13 | −0.34 | +0.91 |
| 4 | 28.77 / 29.01 | 30.31 / 26.89 | −1.54 | +2.12 |

配对中位数：**Δcpu = −0.26 ms/帧，Δfps = +0.5**；4 轮里 NEW 的 CPU 全部占优，fps 3/4 轮占优。
诚实结论：**收益是每帧 0.3–1.1 ms、fps 0.5–1.1，属于真实但不大的改进。**

### 4.3 热漂移（本轮最重要的发现之一）

同一轮测试内，**2 分钟里** `submit_cpu` 从 **18.8 ms 涨到 28.8 ms**、fps 从 **36.7 掉到 29.0**（−21%）。
所以：

- **所有对比必须交错**（顺序测量会把漂移读成差异，反之亦然）；
- 报告绝对帧率时必须注明**冷机还是连续运行后**；
- 这也意味着"减少 CPU 工作"有**复合收益**：少干活 → 少发热 → 少降频。

## 5. 改动内容

`src/graphics/nvn_vulkan/engine/xbg_game.cpp`，3 处语义改动 + 注释：

1. `texel_memo_enabled` 默认 **true → false**（关掉跨帧 texel 驻留副本）。
   驻留副本每帧要 **refresh 178 次**（`pass_ends ... refresh=178.4`），而直接绑 guest 内存本来就是新鲜的。
2. `honor_frame_texel_memo_enabled` 默认 **true → false**（关掉逐帧 texel-view memo）。
3. 标记语义**反转**：`gfx-no-texel-memo`（"关"标记）原本是唯一能关它的入口；
   现在默认就是关的，改成显式 opt-in 的 `gfx-texel-memo=1`。
   > 踩坑记录：只改 `bool texel_memo_enabled=true;` **完全无效**——初始化时
   > `texel_memo_enabled=marker("/gfx-no-texel-memo")<0;` 会把它覆盖回去。
   > 同理 `honor_frame_texel_memo_enabled` 在 init 处被无条件赋 `true`，改声明无效。

**保留的开关（可随时回退）**：`files/gfx-live-set "texmemo=1 texelframe=1"` 同会话即时恢复旧行为。

## 6. 显示回归（证明不改画面）

### 6.1 开关对照（同场景，引擎侧帧转储）

| 指标 | texel memo 开（90 帧 / 10 段） | texel memo 关（90 帧 / 10 段） |
| --- | --- | --- |
| nearblack | 0.1151 – 0.1179 | 0.1153 – 0.1181 |
| nearwhite | 恒 0.0034 | 恒 0.0034 |
| green | 0.00003 – 0.00524 | 0.00003 – 0.00524 |
| 亮度均值 | 61.5 – 61.8 | 61.4 – 61.7 |
| lap | 818.5 – 825.1 | 818.7 – 825.6 |

每段 9 帧 present 号**严格连续**（19/19 段 contig_pairs=8）。
**像素级**：跨状态 mean_abs 2.02–3.02，**落在同状态自然动画漂移带 2.32–3.26 内** ⇒ 差异由熔岩/粒子动画解释，不是开关。
PNG 目视（`tv_texon.png` / `tv_texoff.png`）：熔岩、人物、紫色调查点、小地图、任务文字、系统栏全部完整，两帧仅熔岩相位不同。

### 6.2 出货包自身的显示回归（兵工厂）

**180 帧 / 21 段 × 9 连续呈现帧**（两轮采集合并：81 帧 + 99 帧）：nearblack 0.1006–0.1127、nearwhite 恒 0.0034、
green 0.00003–0.01157、亮度 61.4–62.4、lap 793.5–800.6 —— 与旧包基线（blk 0.107–0.112、lum 61.5–62.3、lap 793–798）一致，
**无缺面、无黑白闪**。

## 7. 出货状态

| 项 | 值 |
| --- | --- |
| APK | `ffrs-texel-20261122.apk` |
| SHA-256 | `2d60e8b4e90e3201a23b5341d6569c814e000f5cf942ae91ec853ad8ff22ff3b` |
| 签名 | 与 `a45430a8…` 同一证书（`d11967a3…`），`install -r` 覆盖安装，**未卸载、未丢档** |
| 引擎自报 | `NVN persistent framebuffers=1 … texel memo=0` |
| 兵工厂 fps | 冷机 **36.0–37.7**（旧包 34.5–35.9）；连续运行 2 分钟后 29（旧包同条件 26.9–28.1） |
| 第 1 章探索区 | 60.0 fps（已达呈现上限） |
| 合入状态 | **仍不合入 `work/rg477m-native`**（RG477M 双机回归未做，权限外） |

## 8. 下一轮入口（按实测收益/风险排序，均来自本轮现场数据）

1. **descriptor 未命中的整表重建：179 次/帧**（占 CPU 5.5–6.6 ms 的主体）。
   `keydiff tex=154/帧` ⇒ **绑定槽里的纹理句柄每帧在变**，所以键变了、set 必须重写。
   方向：只重写**变化的那一个 binding**，而不是整表重建（要把扁平键拆成按 binding 结构）——**中高风险，需逐 binding 画面取证**。
2. **每帧 9.5 次 submit**（客机帧回调 `other` 4.1 ms/帧 ≈ 每次 440 µs）。
   方向：合并 submit（延后 vkQueueSubmit 到帧末）——**P3，风险最高但收益最大**。
3. **snapshot 每帧 12.4 MB**（顶点 10.15 MB）+ 988 次/帧 front 失效。
   方向：顶点快照跨帧缓存的命中率（`vcache` 目前是净收益，保持 ON，但可研究失效原因）。
4. **cleanup 1.3–1.5 ms/帧**（6.6–8.4 次/帧）——P2 WAIT_BIT 同步阻塞。
5. **热降频**：CPU 每帧少 1 ms 不只是 1 ms，还能少发热、少降频（复合收益），值得优先做 CPU 侧。

## 9. 未完成项与风险

- **战斗场景尚未用新包复测**（隔离存档已推进到第 4 章，战斗需重新推进进度才能复现）。
  兵工厂含 469 次/帧 texel 绑定（火山/火焰特效走 texel buffer），显示回归已证明无画面差异，
  但**战斗特效场景的帧率收益/回退尚未单独验证**。
- **场景B（月夜火场）仍缺**：需要把游戏推进到相应章节或拿到对应存档。
- **RG477M 双机回归未做（权限外）⇒ 本分支仍不合入 `work/rg477m-native`。**
- **40 Hz 目标未达成**：兵工厂冷机 36–38 fps，热机 29 fps；目标需要 CPU 再降约 4–6 ms/帧。
- 未审查产物不作绝对保证：本报告只覆盖本机本存档本场景，不外推到其它机型/进度。
- 设备端 `files/frame-dump/` 已累积 **3132 个 .bin（11 GB）**，本轮未清理（属遗留项）。

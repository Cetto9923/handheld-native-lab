# P0 descriptor 路径优化（Honor GT Pro）——第一版被实机证伪，附精确的下一步入口

- **设备**：`A42BBB5109000333`（ROD2-W69 / Adreno 735 / vendorID 0x5143），独立 adb 端口 5041 + 无线 192.168.1.121:44689。
- **分支**：`task/T-mavis-perf-20261122`，worktree `.local/worktrees/mavis-perf-20261122`，基线提交 `876f056`。
- **被测包**：本轮新构建 `ffrs-desccompute-20261122.apk`，SHA-256 `2b5bce720de2476fcca9d81253412301d73f8701aa6882d31e4de4c87e7721a0`；
  签名证书 `d11967a31fcef92f3d6b44adf9d9678c8d2b07bf8f3246d51a791617ed61f36e`，与在装稳定包 `a45430a8…` 同源，`install -r` 覆盖安装成功，**未卸载、存档未触碰**；
  设备回读 APK SHA 与构建产物逐位一致。
- **场景**：第 4 章「向这面旗帜起誓」熔岩兵工厂（隔离存档 `shield-regression-20261122073707`），
  draws 683、dispatches 165、passes 110 —— 比上一轮的探索区重得多，是本机当前最重的场景。

## 0. 一句话结论

**P0 第一版假设「扩大 descriptor memo 的覆盖面到 compute dispatch」被实机数据证伪：收益 ≈ 0。**
但这一轮拿到了两样东西：① descriptor 路径的**逐子项耗时分解**（引擎自带明细 profiler，coverage=OK）；
② 阻塞点的**精确位置**——**163/165 的 dispatch 因为绑定 texel buffer 而无法进 memo**，不是键构造、不是 set 写回。
按 L-029 的判据（上限收益小于测量噪声就不占用构建周期），本轮**不宣称任何性能改进**；
交付物是「已证伪的方案 + 可复用的 A/B 开关与计数器 + 被守卫修复的契约测试 + 精确的下一步入口」。
**显示回归 PASS**：`desccompute` 开关在同一场景 ON/OFF 各 ≥171 帧、19/19 段严格连续，
段级指标一致、像素级差异落在同状态自然漂移带内 ⇒ **开关既不改画面也不提速，是中性开关**（§7.1）。

## 1. 分解证据：descriptor 路径的钱花在哪

用引擎自带的 `gfx-cpu-profile` 明细模式（`descriptor_detail`）取 60 帧，`coverage=OK`：

```
NVN DESC_TIMING v=2 first=1652 last=1711 descriptor_ns=423158720 tagged_ns=389147337 remaining_ns=34011383 coverage=OK clock_reads=2462186
```

descriptor 合计 **7.05 ms/帧**（423.16 ms / 60 帧），逐子项（ms/帧，括号内为该窗口调用数/帧）：

| 子项 | ms/帧 | 调用/帧 | 占比 |
| --- | --- | --- | --- |
| image_lookup | 1.652 | 2460 | 23% |
| set_finalize | 0.894 | 348 | 13% |
| element_rest | 0.658 | 5802 | 10% |
| buffer_bind | 0.573 | 3342 | 8% |
（未归类 remaining） | 0.567 | — | 8% |
| memo_verify | 0.517 | 523 | 7% |
| texel_resident_acquire | 0.448 | 584 | 6% |
| memo_key | 0.435 | 694 | 6% |
| texel_view_create | 0.300 | 668 | 4% |
| memo_snapshot | 0.252 | 523 | 4% |
| setup | 0.175 | 348 | 2% |
| texel 其余五项合计 | ~0.47 | — | 7% |
| readback_* 合计 | ~0.10 | — | 1% |
| sampler_bind / image_transition / image_view | 0.012 | 26 / 26 / 26 | <1% |

结构事实（同一场景，实机日志）：

- `descriptor` 调用 **1188/帧**；draws 683 + dispatches 165 = 848 ⇒ **约 340 次/帧根本不进 memo 块**（compute 与被排除的调用）。
- 绘制侧 memo：**命中 507/帧、未命中 177/帧（74% 命中）**；`keydiff` 定位到 **texture 句柄变化 148/帧** 是未命中的主因。
- **slot-local set_cache 在 Honor 上恒 0 命中**（`setcache hits=0.0`）：生产条件已与 `desc_memo_enabled` 耦合，
  Honor 供应商下恒被旁路，`set_key` 构造+查找实测仅 **0.01 ms/帧** ⇒ **它不是优化入口**（此前怀疑「set_cache 白建」的方向作废）。

## 2. 动手前排除的方向（沿用既有实测，不重复烧构建）

- **memo_front / pipe_memo 扩容 256→1024**：回退到 map 查找只有 121 次/帧，上限收益 < 0.02 ms/帧（L-029，已否决）。
- **`desc_memo_invalidate_local` 取代 `desc_memo_reset`**：等于让 img_cache 跨客机写入存活（L-032 红线，已否决）。
- **FNV 增量哈希 / `desc_memo_reset` 函数拆分**：语义不变、收益 0.2 ms 量级（L-029，留待与主线合并）。

## 3. P0 实现：descriptor memo 覆盖 compute dispatch

`src/graphics/nvn_vulkan/engine/xbg_game.cpp`，`descriptors()`：

1. **键形状按 build 的选段规则对齐**：绘制键仍是 `{shader[0..4]}`；dispatch 键改为 `{shader[5], 0x434f4d5055544501}`。
   两者长度与内容都不同 ⇒ 在同一张 memo map 里**永不别名**（契约测试里有行为级编译验证）。
2. **动态 UBO 模式与 build 同参**：`dyn_mode_for(compute,&no_ubo)`（原来写死 `false`）。
3. **绑定遍历与 build 同选段**：`(st==5)==compute`。
4. **dispatch 的 texel 绑定不进 memo**：dispatch 的 texel view 是逐 dispatch 现建的，memo 不追踪它的生命周期。
5. **需要 indirect draw 参数的 dispatch 不进 memo**：那些参数只有 `descriptors_build()` 会拷出来。
6. **运行时 A/B 标记**：`files/gfx-live-set "desccompute=0|1"`，同会话内即时切换（不改 APK、不重编）。
7. **新增计数器** `desc_memo_compute_hits / _misses / _why[6]`，并入 `NVN PERF setcache` 行。

> 副作用（已在台账标注）：绘制侧的 `desc_memo_why[]` 现在也计入了 dispatch 的排除原因，因为两类调用共用同一组计数器。
> `desc_memo_compute_why[]` 是 dispatch 的独立计数，读数时用它。

## 4. 实机 A/B：收益 ≈ 0（方案被证伪）

同一会话内交错 `desccompute=1 → 0 → 1 → 0`，同场景、同存档、同起点：

| 状态 | descriptor ms/帧（调用/帧） | compute 命中/帧 | compute 未命中/帧 | compute 排除原因（texel/simg/dm/dynbuf） |
| --- | --- | --- | --- | --- |
| ON（新路径） | 9.92 / 10.38 / 10.72 / 10.80（1187.8） | **1.0** | 0.0 | 143.0 / 14.0 / 6.0 / 0.0 |
| OFF（旧路径） | 10.84 / 10.63 / 10.83 / 10.74（1024.9） | 0.0 | 0.0 | — |

**结论：descriptor 耗时两者无差异（差值在噪声内）。** 原因很直接：
**165 个 dispatch/帧里有 163 个因为绑定 texel buffer（或 storage image / 写存储）被挡在 memo 之外**，
真正能进 memo 的只有约 2 个/帧，其中命中 1 次 ⇒ 每帧省下约 13 µs ≈ 0.013 ms，量不出来。

**这一步的价值不在收益，在定位**：它把「dispatch 为什么不能被复用」从猜测变成了计数，
并顺带证明 **draw 侧的 177 次/帧整表重建才是 descriptor 路径的大头**（tex 句柄变化驱动）。

## 5. 零编译开关扫描（同一会话交错，顺便排除其它方向）

用 `gfx-live-set` 逐个开关 ON/OFF，各取 2 个窗口：

| 开关 | 结果 | 判定 |
| --- | --- | --- |
| `vcache=0`（关顶点快照缓存） | snapshot part **稳定降 1.3–1.7 ms/帧**（5.27–5.54 → 3.80–4.43），但 submit_cpu 未体现 | **净收益不可证**，按 L-029 不采纳 |
| `ownedinput=0` | submit 29.3–29.4 → 29.7–33.6 | 当前默认更优，保持 |
| `texmemo=0` | descriptor 调用 1187.9 → 1289.8、snapshot 调用 5534 → 6210，submit 基本持平 | 持平，不动 |
| `depthaspect=0` / `rbfullcopy=0` / `uppro=0` | 全部落在噪声内 | 不动 |

> vcache 这条值得单独记住：**一个「省拷贝」的缓存，在这台机器上算哈希比拷贝还贵**；
> 但它属于 snapshot 路径（P1），不是 descriptor 路径，且净收益未在 submit 指标上体现，本轮不动。

## 6. 契约测试

- **修复既有守卫**：`tests/device/test_fresh_descriptor_cache.py` 此前**已经失效**（不是本轮改坏的）：
  1. fixture 缺 `desc_memo_enabled` 声明 → 从生产代码抽出的片段编译不过 → 整个 descriptor 契约测试在跑之前就废了；
  2. `ab_off` stub 只认 `setfresh`，而生产代码还问 `setbypass` → 运行期断言挂掉；
  3. bypass 期望仍按已被生产侧删除的 driverVersion 条件写的。
  修完后 3 个用例全部通过 —— 这条路径的红线守卫现在**真的在跑**。
- **新增** `tests/device/test_descriptor_compute_memo.py`（9 个用例）：键形状、选段规则、dmode 一致性、
  indirect/texel 排除、开关注册与作用域、计数器上报，以及**行为级**的
  「绘制键与 dispatch 键永不别名」（抽取生产源码里的键构造语句编译运行，断言两键恒不等）。

## 7. 显示回归

### 7.1 同场景单变量对照：`desccompute` ON vs OFF（兵工厂，游戏场景，非菜单）

采集：引擎侧 `gfx-live-framedump`（1280×720 RGBA），同一会话内交错 ON→OFF→ON→OFF，
每状态 10 段 × 9 **连续**呈现帧；ON 共 180 帧（20 段），OFF 共 171 帧（19 段，末段少 1 段）。
**每一段的 present 号都严格连续（contig_pairs = 段内帧数−1，19/19 段全部为 8，无空洞）**。

| 指标 | ON（180 帧） | OFF（171 帧） | 判定 |
| --- | --- | --- | --- |
| nearblack | 0.11460 – 0.11820（均值 0.11664） | 0.11490 – 0.11840（均值 0.11686） | 一致 |
| nearwhite | 恒 0.00340 | 恒 0.00340 | 一致 |
| green | 0.00003 – 0.00524（均值 0.00256） | 0.00003 – 0.00527（均值 0.00250） | 一致 |
| 亮度均值 | 61.47 – 61.84（均值 61.660） | 61.42 – 61.85（均值 61.661） | 一致 |
| lap（锐度） | 818.5 – 825.7（均值 821.61） | 818.0 – 825.6（均值 821.57） | 一致 |

**闪烁判据**：段内相邻帧 **ΔLum 最大 0.06、Δlap 最大 3.7**（ON/OFF 同量级），
无 nearblack/nearwhite 跳变 ⇒ **无黑白闪、无缺面**。

**像素级交叉验证（防止把"熔岩在动"误判成"开关改了画面"）**：
熔岩与粒子持续动画，直接跨状态比像素一定会有差，所以用**同状态随时间的自然漂移**作参照曲线：

| 比较 | present 间隔 | mean_abs | >8 的像素占比 |
| --- | --- | --- | --- |
| 同状态相邻帧（噪声底） | 1 | 0.46–0.47 | 0.002 |
| **同状态**自然漂移参照 | 150–1260 | **2.05 – 3.74** | 0.125–0.240 |
| **跨状态** ON↔OFF 实测 | 510–2910 | **1.46 – 3.12** | 0.055–0.085 |

跨状态差异**完全落在同状态自然漂移带内**（甚至更低）⇒ 差异由场景动画解释，不是开关带来的。

**PNG 目视**（`ab_on_frame.png` / `ab_off_frame.png`）：熔岩、人物、紫色调查点、小地图、
任务文字、A 调查 提示、左侧系统栏全部完整，两帧仅熔岩相位不同 ⇒ **无可见差异**。

> **结论：`desccompute` 开关在本场景不产生任何可测的画面差异（显示回归 PASS）。**
> 这与 §4 的性能结论一致：**该开关既不提速也不改画面**，是一个零风险、零收益的中性开关。

### 7.2 其它显示取证

- **兵工厂（静态，desccompute=ON）**：108 帧 / 12 段 × 9 连续呈现帧。nearblack 0.107–0.112、
  nearwhite 恒 0.0034、green 0.00003–0.0114、亮度均值 61.5–62.3、lap 793–798 —— 段间无闪烁特征。
  导出 PNG 目视：**熔岩、人物、紫色调查点、小地图与按钮全部完整，无缺面、无黑白闪**。
- **场景A（神殿）本轮未取到有效对照**：隔离存档 `shield-regression-20261122073233` 当前实际落在兵工厂，
  且 `enter_scene.sh` 的按键序列把画面停在 SAVE POINT 菜单上（深蓝半透明面板占屏约 1/3，
  这正是那组 189 帧 nearblack 0.114–0.116 的来源——**不是渲染缺陷**，PNG 目视已确认）。
  ⇒ 该组帧**不作为场景A对照**，场景A 仍维持上一轮稳定包的结论。
- **本轮踩到的取证污染（已记为 L-043）**：引擎内绘制的弹窗/菜单是**游戏画面的一部分**，
  用 `input keyevent 4`（BACK）想关它会**直接把 Activity finish 掉退到桌面**；
  端口的"返回"是 **KEYCODE_BUTTON_B(97)**，确认是 **BUTTON_A(96)**。
  第一次采集（`bg_8d61fbbb`）因平板前台被切走作废，第二次（`bg_27391817`）画面停在 SAVE DATA 弹窗上
  （lum 恒 36.5、nearblack≈0.221、lap≈1390，是**另一套数**），也判为无效对照。
  本节数据是**用 B 键逐层退出菜单、截图确认无菜单后重采**的结果。

## 8. 下一轮 P0 的入口（本轮测出来的，按收益/风险排序）

1. **让 dispatch 的 texel 绑定可 memo 化**（阻塞 163/165 的 dispatch）。
   前提不是「去掉 `!compute`」这么简单：dispatch 的 texel 数据可能由 compute 写回，
   resident 副本一旦过期就是**画错**，所以必须先证明 resident 拷贝对 dispatch 的写入是安全的，
   否则就是 L-032 说的「提速换画面异常」。**建议单独一轮，先做写入跟踪的证据，不要直接改。**
2. **减少 draw 侧 177 次/帧的整表重建**（tex 句柄变化驱动，keydiff tex=148/帧）：
   方向是「只写变化的 binding，而不是整表重写」——这是 GPASS 报告 §6 原本的 P0 方向，
   代价是要把扁平 key 拆成按 binding 的结构，**属于中高风险改动**。
3. **P1 snapshot 路径**：每 submit 整表重建 + vcache 哈希比拷贝贵（见 §5）。
4. **P2 cleanup 的 WAIT_BIT 同步阻塞**；**P3 GPU pass 合并**（风险最高，仍然排最后）。

## 9. 本轮结论、未完成项与风险

### 9.1 结论（可对外引用）

1. **P0 第一版（descriptor memo 覆盖 compute dispatch）被实机数据证伪：收益 ≈ 0。**
   阻塞点是**可计量的**：165 个 dispatch/帧里 163 个绑定 texel buffer（143 texel / 14 storage image / 6 写存储），
   按设计不能进 memo；真正能进的只有 ~2 个/帧、命中 1 次 ⇒ 每帧省约 13 µs，量不出来。
2. **显示回归 PASS**：`desccompute` ON/OFF 在同一场景各 ≥171 帧、19/19 段严格连续，
   段级指标一致（nearblack 0.11664 vs 0.11686、亮度 61.660 vs 61.661、lap 821.61 vs 821.57），
   像素级跨状态差异落在同状态自然漂移带内 ⇒ **开关既不改画面也不提速，是零风险零收益的中性开关**。
3. **下一步入口已精确定位**（见 §8）：要么证明「compute 写入 vs resident 副本」安全后放开 texel memo，
   要么转向 draw 侧 177 次/帧的整表重建（按 binding 拆键，中高风险）。
4. **附带修复了一条长期空转的红线守卫**（`test_fresh_descriptor_cache.py`），并新增 9 个行为级契约用例。

### 9.2 未完成项

- 场景B（月夜火场）**仍未测**；战斗特效场景因隔离存档已推进到第 4 章，需要重新推进进度才能复现。
- 正常随机遇敌的入场卡顿**未复测**（同上，进度问题）。
- **RG477M 双机回归未做（权限外）⇒ 本分支仍不合入 `work/rg477m-native`，本轮成果仅在 Honor 单机上成立。**
- 场景A（神殿）本轮未取到有效对照，维持上一轮稳定包结论。

### 9.3 风险与口径提醒

- 本轮改动虽为中性，但**改变了共享的 `desc_memo_why[]` 计数口径**（见 §3 注），
  后续读旧日志时要注意区分（dispatch 用 `desc_memo_compute_why[]`）。
- 取证污染已记录为 **L-043**：引擎内菜单会污染帧指标（SAVE DATA 面板 lum 恒 36.5 / nearblack≈0.221 /
  lap≈1390，与游戏场景完全是两套数），**"指标很稳定"不等于"场景正确"**，必须先看图再看数；
  关弹窗要用 `keyevent 97`（B），`keyevent 4` 会把游戏退出到桌面。
- 设备端 `files/frame-dump/` 累积约 1900+ 个 `.bin`（3.7 MB/个），目录不可删，属遗留项，未处理。
- 未审查产物不作绝对保证：本报告只覆盖本机本存档本场景，不外推到其它机型/进度；
  §7 的显示结论是**单场景、单存档、引擎侧帧转储**的结果。

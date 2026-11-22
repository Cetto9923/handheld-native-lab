# 帧率优化第 2 轮：仪表全拆 + 路径开关扫全 + texel memo 化探针（净负，止损）

- **设备**：`A42BBB5109000333`（ROD2-W69 / Adreno 735 / vendorID 0x5143），独立 adb 端口 5041 + 无线 192.168.1.121:44689。
- **分支**：主交付分支 `task/T-mavis-perf-20261122` 仍停在已出货的第 1 版 `caa0cff`；
  本轮探针代码单独存于侧分支 `task/T-mavis-perf-20261122-v2probe`（commit `2123237`），**未合入主交付分支**。
- **目标**：在不影响任何特效与显示的前提下提升帧率，稳步推进到所有场景 40 Hz 以上。

## 0. 一句话结论

**本轮没有出货新版本**：把引擎的性能仪表拆到子域级别后，确认第 1 版之后**已经没有"免费"的收益**，
而唯一看起来足够大的结构性杠杆（让 texel 绑定进 descriptor memo）**实机 A/B 净负 2.49 ms/帧**，按 L-029 止损。
设备已恢复为已出货的第 1 版包（`2d60e8b4…`，`texel memo=0`），实机复核正常。

本轮产出三样东西：**① 一份可复用的仪表全景**（哪 5 个部件吃掉 26 ms，各自的子域明细）；
**② 43 个从未测过的路径开关的全量扫描**（结论：没有免费收益）；**③ 一个写完、装上机、量过、被否决的探针**，
以及它指向的真正瓶颈（memo 键的稳定性，而不是 texel 的处理方式）。

## 1. 仪表全景：26 ms/帧花在哪（兵工厂，热机，第 1 版包）

```
NVN PERF parts_ms_per_frame timed=60
  other=4.57/9.2   draw=3.13/687.5  dispatch=0.82/165.0  descriptor=9.14/1296.4
  pass=0.40/706.8  buffer=0.16/1.0  pipeline=0.37/202.5   submit=0.03/1.0    fence=0.00/8.8
  present=0.50/1.0 cleanup=1.82/8.8 hash=0.16/245.3       image=0.19/1792.7  log=0.02/0.8
  program=0.25/597.0 cpuwrite=0.16/399.6 snapshot=4.59/6241.6
NVN PERF cleanup_ms_per_frame queries=0.07 writebacks=1.05(9.6MB) views=0.51 retire=0.46 arena_pools=0.18
NVN PERF block_ms legacy_wait=3.42/0.40 legacy_wait_total=3.42 submit_by other=8.08 present=1.00
NVN PERF desc_ms_per_frame memo_key=0.22 tmpl_update=1.35 alloc=0.17 setkey=0.03 build=11.32 hit_total=1.25
NVN PERF setcache hits=0.0 misses=444.9 template=444.9 memo_hits=407.5 memo_misses=0.9 memo_front=339.1
             why=texel423.0/simg14.0/dm6.0/dynbuf0.9/store0.0
NVN PERF snapfront front_hits=3834.8 map_hits=11.0 invalidations=988.3
NVN PERF vcache hits=149.1(3.77MB) fills=6.1 refills=5.93 volatile=11386 flushes=14 entries=612 arena=252.7MB
```

排序后的 CPU 去向（热机，submit_cpu 26–27 ms/帧）：

| 部件 | ms/帧 | 占比 | 可动性 |
| --- | --- | --- | --- |
| **descriptor 整表重建** | **9.14** | 35% | 445 次重建/帧 × 20.6 µs；命中 memo 只要 ~3 µs ⇒ 理论上限 ~7 ms |
| snapshot（顶点/UBO 拷贝） | 4.59 | 18% | 12.4 MB/帧、6241 次/帧；vcache 只命中 149 次/帧，arena 已膨胀到 252 MB |
| other（客机每 op 翻译） | 4.57 | 18% | 2664 个客机 op/帧，1.7 µs/op，暂不动 |
| draw（vkCmdDraw） | 3.13 | 12% | 683 次/帧 × 4.6 µs，驱动侧 |
| cleanup | 1.82 | 7% | writeback 1.05（9.6 MB/帧，带宽）+ 销毁 668 个 buffer view 0.51 |
| 另：dispatch 0.82 / present 0.50 / pipeline 0.37 / program 0.25 / image 0.19 / hash 0.16 / cpuwrite 0.16 | | | |

descriptor 的 27 个子域（`gfx-cpu-profile`，60 帧窗口，折算 ms/帧）：

```
image_lookup 1.65 | set_finalize 0.89 | element_rest 0.66 | buffer_bind 0.57 | remaining 0.57
memo_verify 0.52   | texel_resident_acquire 0.45 | memo_key 0.43 | texel_view_create 0.30
memo_snapshot 0.25 | setup 0.18 | texel_pending 0.11 | texel_readback 0.08 | texel_alias 0.06 …
```

**两个此前没看见的事实**：

1. `set_cache hits=0.0` —— 当 memo 打开时，slot 内的 `set_cache` 被整条绕过
   （`experiment_cache_skip` 条件里含 `desc_memo_enabled`），所以**每次重建都要新分配 set + 全量写回**。
2. `cleanup` 里的 `views=0.51 ms` —— 每帧创建并销毁 **668 个 VkBufferView**，
   因为 texel 目标缓冲每帧换新，视图缓存（`recording_slot().texel_views`）永远 miss。
   这两条都指向同一个根因：**texel 绑定每帧换缓冲 ⇒ 它既进不了 memo，也吃满视图缓存**。

## 2. 43 个 `ab_off` 路径开关：全量扫过，没有免费收益

上一轮只扫过 `gfx-live-set` 的 29 个布尔开关；`gfx-live-ab` 的 43 个**路径开关**（`ab_off("…")`）
从未测过。挑了其中 20 个可能影响 CPU 的做交错扫描（ON/OFF/ON/OFF，判据=整帧 submit_cpu + fps）。

- 扫描设计有个已知偏差：**基线取在每轮末尾**（设备最热时），所以所有候选都带 +0.5~1.2 fps 的系统性正偏；
  只有明显跳出该带的信号算数。
- **跳出信号只有两个，方向都是"这些路径在帮忙"**：
  `passfast` 关掉 → −3.34 fps；`texelcopy` 关掉 → −2.77 fps ⇒ 保持默认开启。
- `ibrecord`(−0.75) / `memobuffer`(−0.89) 关掉略快，但落在偏差带内，按 L-029 不动。
- 其余（memofront / hitgen / roimgfast / descreserve / ktag / snapinval / vcache / wtrack /
  iblookup / shreuse / progcache / poolreset / uboarena / xguard / texelframe / setbypass /
  setfresh / drivergen / clearfold / occ / query / noload / rbfullcopy…）**全部在噪声内**。

**结论**：引擎里剩下的开销不是"某条路径写错了"，而是结构性成本。继续扫开关已经没有收益。

## 3. 探针设计：让 texel 绑定进 descriptor memo（V2）

**动机**：descriptor 是最大头（9.14 ms/帧），而它之所以贵，是因为 **423/帧的 draw 因为绑定了一个
texel buffer 而被排除在 memo 之外** ⇒ 每帧 445 次整表重建（每次 ~20 µs），而 memo 命中只要 ~3 µs。

**为什么 texel 一直被排除**：memo 缓存的是"descriptor set 本身"。texel 绑定指向一个每帧换新的目标缓冲，
memo 化后 set 里会握着上一天的 view ⇒ 画面会错。源码注释写得很直白：
*"Texel view 属于 slot，退休时销毁；memo 不追踪其快照或生命周期，不能保留旧 set。"*

**探针做法**（分支 `task/T-mavis-perf-20261122-v2probe`，commit `2123237`，约 200 行）：

1. **每 slot 一个 texel 目标缓冲池**：键 `(guest range, format)`，缓冲一旦建立就不再变 ⇒
   绑定的 view 稳定 ⇒ memo 化后 set 仍然有效。
2. **池内缓冲每次使用都刷新**：来源仍是"待回写的 GPU 结果"或"guest 内存"，拷贝粒度与原来完全一样
   （同一套 per-submit 拷贝缓存 `texel_pool_cache`），所以**读到的字节与原路径一致**。
3. **memo 侧**：把 `UNIFORM_TEXEL_BUFFER` 从排除名单里放出来（storage texel 与 compute 仍排除）；
   memo 命中时按记录刷新池内缓冲；池化过的 range 不再进 `dm.stor`（否则会拿快照缓冲去比对而恒失配）。
4. 开关 `gfx-live-set "texelpool=0/1"` 可同进程 A/B。

## 4. 实测：净负 2.49 ms/帧

同进程交错 3 轮（设备已进入热降频态，绝对帧率偏低，但两态同条件可比）：

```
texelpool=1 (V2): fps 中位 27.55（24.17–27.79）| submit_cpu 31.10 ms | gpu 21.57 ms
    parts 中位: desc=10.93 snap=5.04 draw=4.01 cleanup=2.27 other=5.31
texelpool=0 (V1): fps 中位 28.57（28.30–31.05）| submit_cpu 28.61 ms | gpu 20.41 ms
    parts 中位: desc=9.80 snap=5.17 draw=3.31 cleanup=2.08 other=5.03
DELTA: Δfps = −1.02    Δsubmit_cpu = +2.49 ms/帧
```

探针侧计数（每帧）：池命中 1561–1630、池未命中 0、刷新 160–170、跳过 84–94（池 4 MB/slot 上限到了）。
memo 侧：命中 407 → 506、未命中 0.9 → 96.8、setcache 重建 444.9 → 341.9、texel 排除 423 → 224
（其中 143 是 compute 的 storage-texel，仍排除）。

**归因**（用上面的计数反推）：

| 项 | 每帧 | 折算 |
| --- | --- | --- |
| 重建次数减少 | 445 → 342（−103 次 × 20.6 µs） | **−1.62 ms** |
| 命中路径新增刷新 | 1629 次 × 1.69 µs | **+2.75 ms** |
| 合计（descriptor 域） | | **+1.13 ms**（实测 +2.49，含 GPU/热漂移） |

**两个结论**：

1. **净负的直接原因是刷新没摊开**：memo 命中时每个 texel 绑定都要重新解析来源 + 查拷贝缓存，
   1629 次/帧 × 1.69 µs 把省下的重建成本吃光还倒欠。
2. **即使刷新免费，天花板也只有 ~1.7 fps**：423 个被解除排除的 draw 里，只有约 103 个真能命中
   （新 memo 未命中 96.8）。真正的瓶颈是**键的稳定性**，不是 texel 的处理方式——
   `keydiff tex=154/帧` 说明很多 draw 的纹理句柄每帧在变，键一变就必然 miss。

## 5. 下一版该怎么做（探针留下的设计要点）

1. **刷新摊到"每资源每提交一次"**：用 `TexelPooled.refreshed=slot_serial` 做门，
   1629 → 170 次/帧。但**不能就此把版本检查省掉**：原路径每次 draw 都比对来源版本，
   跨提交复用拷贝需要"脏标记"来等价替代（guest 写内存走 `snapshot_invalidate`→`texel_touch`，
   GPU 新结果走 pending writeback 的 version 变化），两处都要挂上，失效强度不得低于原路径（L-032）。
2. **pending_writeback 查找到 O(1)**：今天是线性扫 `pending_writebacks`，
   命中路径每次刷新都要扫一遍；给范围做索引（或按范围记脏）才能让刷新真正便宜。
3. **池的内存要算账**：4 MB/slot × 16 slot = 64 MB；共享池可降到 4–6 MB，但要重新验证跨 slot 的
   提交顺序（队列内有序 ⇒ 后提交的刷新不会污染先提交的 draw，这点可证，但要写进注释）。
4. **优先修 memo 键的稳定性**：键里的 tex 项用的是完整句柄 `state.tex[ns][slot]`（含每帧变化的代次），
   而 memo 的 verify 已经检查了"注册版本 + serial + 存活 image 身份"⇒ 键也许只该用稳定的纹理 id
   （池指针/偏移已在键里）。这是**下一版最值得先试的一枪**：改动小、命中率高的话收益远超 V2。


## 8. 追加验证：memo 键的 tex 分量（同一轮内追加的第二个探针，已证伪）

第 5 节列的"下一版第一枪"（把 memo 键里的 tex 分量从完整句柄换成稳定纹理 id）在本轮剩余时间里直接试了：

- **改动**：键里的 `state.tex[ns][slot]`（含每帧可能变化的代次）换成 `handle & 0xfffff`（池内 id）。
  依据是 `register_texture_record` 只在**纹理内容真的变了**时才 `revision++`
  （`changed=!slot.revision || !registered_texture_equal(slot,next)`），
  而 memo 的 verify 已经校验"注册版本 + serial + 存活 image 身份"⇒ 语义等价。
  做成运行时开关 `gfx-live-set "memokey=1"`，同进程交错 A/B。
- **实机结果（兵工厂，交错 3 轮）**：

  ```
  memokey=1: fps 中位 33.88 | cpu 25.19 ms | memo_hits 405.1 | memo_misses 0.9 | setcache_misses 442.9
  memokey=0: fps 中位 33.71 | cpu 25.20 ms | memo_hits 405.0 | memo_misses 0.9 | setcache_misses 442.9
  DELTA: Δfps +0.17   Δcpu −0.01 ms/帧
  ```

  **完全没有任何变化**：memo 命中数一模一样（405.1 vs 405.0），重建数也一样。
  ⇒ **纹理句柄不是 memo 未命中的原因**，上一轮基于 `keydiff tex=154/帧` 的推论被实机证伪。
- **探针代码**：侧分支 `task/T-mavis-perf-20261122-memokeyprobe`（默认关闭，不影响出厂行为）。
- **修正后的判断**：memo 的 443 次/帧重建**几乎全部来自 texel 绑定被排除**（423），
  其余是 storage image(14)/dynamic mode(6)/dynbuf(1)。也就是说，
  **"让 texel 绑定进 memo"仍然是唯一的大杠杆**，但它必须做到
  **刷新摊到每资源每提交一次 + 失效强度不低于原路径 + pending_writeback 查找 O(1)**，
  否则就会被命中路径的新增成本吃光（V2 的实测已经证明）。

## 6. 设备与仓库状态

- 设备：已装回已出货的第 1 版包 `ffrs-texel-20261122.apk`（SHA-256 `2d60e8b4…`，同签名 `install -r`），
  `texel memo=0` 已回读确认；游戏前台停在第 4 章兵工厂（隔离存档 `shield-regression-20261122073707`），
  冷机瞬态 fps 37.8 / submit_cpu 17.5 ms。
- 仓库：主交付分支 `task/T-mavis-perf-20261122` = `caa0cff`（第 1 版），工作树干净；
  探针代码在侧分支 `task/T-mavis-perf-20261122-v2probe` = `2123237`（未合入 `work/rg477m-native`）。
- 本轮**未做**：战斗特效场景复测、场景B（月夜火场）、RG477M 双机回归 ⇒ 仍不合入 `work/rg477m-native`。

## 7. 出处

- 仪表与开关扫描：`/tmp/ffrs-validate/abscan.py`、`/tmp/ffrs-validate/pool_ab.py`（设备直采，原始行见本报告引用）。
- 探针：`task/T-mavis-perf-20261122-v2probe` 的 `src/graphics/nvn_vulkan/engine/xbg_game.cpp`（20 处改动）。
- 经验库：EXPERIENCE.md L-048 ~ L-051。

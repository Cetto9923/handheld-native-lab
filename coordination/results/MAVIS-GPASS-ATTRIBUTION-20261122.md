# Honor 第四章兵工厂：GPU pass 归因与 CPU 热点地图（Round 10 诊断轮）

日期：2026-11-22 13:00–14:20 北京时间
执行：Mavis（分支 task/T-mavis-perf-20261122，worktree .local/worktrees/mavis-perf-20261122）
设备：HONOR ROD2-W69 / A42BBB5109000333 / Adreno 735 / Android 16（仅此一台，未触碰 RG477M）
隔离存档：shield-regression-20261122073707（第四章兵器工厂），formal 存档未写入

---

## 1. 本轮做了什么

1. **打通被拦住的构建链路**。Codex 07:50 记录的「isolated APK build was blocked by an automated safety check」
   是 Codex 侧的平台限制，不是项目限制。用项目自带的 scripts/android/build_game_window_apk.sh 全量构建通过
   （含 keytool/apksigner 自签调试证书）。
   **必须注意**：脚本会在每个 worktree 内 keytool -genkeypair 生成**新的**调试密钥，
   与设备上已装包的签名证书不同，adb install -r 会失败。
   解法（不改设备数据、不卸载）：把已装包同源 keystore 软链进本 worktree，再用 apksigner sign 重签 aligned.apk。
   本轮实测：Codex keystore 证书 SHA-256 = d11967a31fcef92f3d6b44adf9d9678c8d2b07bf8f3246d51a791617ed61f36e。

2. **修好并实机验证 GPASS 崩溃**。07:46 的崩溃点 pass_prof_draw（xbg_game.cpp:6113，NULL 解引用，
   ldr w9,[x10]，x10=0）已用 Codex 复核过的补丁修复：只在校验器仍在作用域内时拷贝静态元数据标量，
   缺失的 blend attachment 记为 unknown 而非当作「关闭」。
   实机验证：**不再崩溃**，NVN GPASS ... result=0（VK_SUCCESS），每帧拿到完整 pass 明细。

3. **拿到第四章兵工厂的第一份真实 GPU pass 归因**（此前从未有过）。

## 2. 产物与指纹

| 项 | 值 |
| --- | --- |
| 诊断 APK | .local/android-apk/ffrs-gpass-diag.apk（worktree 内） |
| APK SHA-256 | a45430a8e4d5a9eb1510051b70836219e330c45128bf5d299ea17e7c15c2aaa8 |
| 设备端 base.apk SHA-256 | a45430a8e4d5a9eb1510051b70836219e330c45128bf5d299ea17e7c15c2aaa8（逐字节一致） |
| versionCode / versionName | 78 / 1.0.78-test（debuggable，隔离存档需要） |
| 基线包（Round 9，未被替换） | ffrs-cand-round3-3f7c02e6.apk SHA 3f7c02e6…，本地仍在，可随时回装 |
| GPASS 原始数据 | .local/mavis-gpass-20261122/gpass-raw.txt（4963 行，18 帧） |

## 3. 干净基线（无任何插桩残留）

兵工厂场景，静止，683 draws / 2630 ops / 110 guest passes：

| 采样 | FPS | submit CPU (ms) | GPU (ms) |
| --- | ---: | ---: | ---: |
| 1 | 20.84 | 35.65 | 30.27 |
| 2 | 24.24 | 33.40 | 22.97 |
| 3 | 24.56 | 32.79 | 17.87 |

**结论：这一场景是 CPU 受限**（submit CPU 33–36 ms > 16.66 ms 的 VSync 预算），
GPU 18–30 ms 尚在预算内但也不宽裕。
对照 Codex 07:41 同场景（基线包、行走 12.61 s）测得 33.44 FPS、且末段掉到 23/23/24/28 ——
本轮静止取样落在同一劣化区间，两边口径不同（静止 vs 行走），不作直接差值结论。

### CPU 热点地图（每帧，插桩采样，方向性数据）

descriptor 的子项来自 NVN PERF desc_ms_per_frame（**插桩采样，不能当性能基线**，见 L-019）。

| 桶 | ms/帧 | 次数/帧 | 说明 |
| --- | ---: | ---: | --- |
| descriptor | 12.8 | 1025 | 其中 build 12.3 / hit_total 3.7 / tmpl_update 1.45 / memo_key 0.70 / alloc 0.30 / setkey 0.02 |
| other | 5.4–7.2 | 9.3 | 提交路径未被任何具名子域覆盖的余量（约 700 us/次） |
| snapshot | 5.0 | 5529 | 快照表查找/建立 |
| draw | 4.3 | 683 | draw 翻译 |
| cleanup | 2.0 | 6.8–8.4 | 含 vkGetQueryPoolResults(WAIT_BIT)，是 CPU-GPU 同步阻塞点 |
| cpuwrite | 0.8–1.2 | 396 | CPU→GPU buffer 写 |
| dispatch/hash/pass/pipeline/present/program/image/buffer | 合计约 2.5 | — | 其余 |
| **合计** | **约 33** | — | 与 submit_cpu_ms 33–36 对得上 |

descriptor 子项里 tmpl_update = 342 次/帧的 vkUpdateDescriptorSetWithTemplate（1.45 ms），
memo_misses = 177/帧（26% 未命中，需要真正重建 set），
memo_hits = 505.8/帧、其中 memo_front 直接命中 384.8/帧。

## 4. GPU pass 归因（首次拿到）

每帧 **111 个 render pass**，其中 78 个在 768x436、3 个在 1280x720、1 个在 800x512。
每帧 GPU pass span 合计 **约 32 ms**（6 个 slot 合计）。

按 pass 形态聚合（17 帧样本，us 合计 / 次数 = 单次均值）：

| pass 形态 | 合计 us | 次数 | 单次 | 备注 |
| --- | ---: | ---: | ---: | --- |
| 768x436 att=2 fmt=122 draws=1 | 70031 | 511 | **137 us** | **每帧约 30 个**，合计约 4.1 ms/帧 |
| 768x436 att=7 fmt=122 draws=102 | 44862 | 17 | **2640 us** | 单帧最贵 pass（verts=244074） |
| 768x436 att=2 fmt=97 draws=1 | 43811 | 557 | 79 us | 每帧约 33 个 |
| 768x436 att=2 fmt=77 draws=52 | 23501 | 17 | 1382 us | |
| 768x436 att=1 fmt=37 draws=1 | 20602 | 36 | 572 us | |
| 800x512 att=1 fmt=124 draws=74 | 17788 | 17 | 1046 us | |
| 768x436 att=1 fmt=9 draws=1 | 15143 | 17 | 891 us | |
| 768x436 att=7 fmt=122 draws=52 | 15025 | 17 | 884 us | |
| 768x436 att=1 fmt=129 draws=102 | 14275 | 17 | 840 us | |

**fmt=122（R16G16B16A16_SFLOAT）合计约 7.6 ms/帧**，是 GPU 侧最大单一族。
「每帧约 30 个单 draw 的 768x436 R16G16B16A16 pass」是明显可优化的形态，
但**合并 pass 会改变渲染语义，属于画面异常高风险区，本轮只记录不动手**。

## 5. 本轮**没有采纳**的三项 Round 10 计划改动（按实测证伪）

Codex 的 Round 10 WIP（未提交，停在 xbg_game.cpp）包含三项改动，本轮**全部不采纳**，理由如下。

| 计划改动 | 实测依据 | 决定 |
| --- | --- | --- |
| memo_front 256 → 1024 槽 | front 命中 384.8，回退到 map 查找只有 memo_hits(505.8) - memo_front(384.8) = **121 次/帧**；按整 key 哈希一次 0.2–0.4 us 计，省不到 **0.02 ms/帧**（占 33 ms 的 0.06%） | **不做**：量不出来，纯属白跑一轮 |
| pipe_memo 256 → 1024 槽 | 命中率已 95.9%（hits=391.3 / misses=16.8），最多再省 17 次 map 查找/帧 | **不做**：同样量不出来 |
| snapshot_invalidate 里用 desc_memo_invalidate_local() 取代 desc_memo_reset() | 语义差别是：**不再清 img_cache**。等于让图像缓存在客机写入内存后继续存活 —— 这正是 07:46 审计明确禁止的方向（Do not widen cross-frame image caches；历史 8c009f3 减少调用次数但帧率没降、bd87e09 暴露 sampler/view 身份隐患） | **不做**：属于「提速换画面异常」的重灾区 |

另两项**安全**的 Round 10 改动（FNV 增量哈希 PUSH_KEY、desc_memo_reset 的函数拆分）
语义完全不变，但收益同样在 0.2 ms 量级，单独不值一次构建周期，留到与 P0 合并时一起上。

## 6. 下一步优化排序（按实测收益/风险）

**P0 — descriptor 路径（本场景 39% 的 CPU 时间）**
目标：vkUpdateDescriptorSetWithTemplate 342 次/帧（1.45 ms）与 177 次/帧的 set 重建。
方向：只写变化的 binding，而不是每帧重写整套；模板 set 跨帧缓存。
约束：**必须配运行时 A/B 标记**（L-004），同机位同存档同 APK 差一个布尔；配显示回归。

**P1 — snapshot 路径（15%）**
snapshot_reset() 在**每帧开头**整表重建（snapshot_maps_reset() 重建两张 map + arena rewind），
5529 次/帧调用因此全是冷的。
方向：把 O(log n) 的 std::map 换成 O(1) 结构，但**必须保留按范围失效的能力**
（snapshot_invalidate 靠遍历 map 找到要作废的 front 槽；直接去掉 map 会让 guest 写入后
front 仍返回旧快照 = 画面异常）。

**P2 — cleanup（6%）**
vkGetQueryPoolResults(..., VK_QUERY_RESULT_WAIT_BIT) 在提交线程上同步等 GPU，是 CPU-GPU 阻塞点。
方向：走已存在的持久 host-visible 结果缓冲快路径。

**P3 — GPU pass 数（收益大，风险最高）**
每帧 111 个 pass、约 30 个单 draw 的 R16G16B16A16 pass（4.1 ms/帧）。
方向：合并同目标同格式的连续 pass。**必须逐个 pass 核对附件状态与 barrier，并对每个合并项做画面取证**
——这是最容易「提速但画面异常」的地方。

## 7. 状态卫生

- 设备上：安装的是本轮诊断包（SHA 与本地一致），formal 存档未触碰。
  回退方法：adb install -r .local/worktrees/no-key-delivery-20261120/.local/honor-benchmark-20261120/ffrs-cand-round3-3f7c02e6.apk
- 一次性标记 gfx-pass-live / gfx-cpu-profile 均已自删除（读后即删），设备上无插桩残留。
- 未触碰 RG477M；未 push；未改 main。
- Codex 的 worktree 未被修改（其 Round 10 WIP 原样保留），本轮全部改动在本轮自己的分支上。

## 8. 未验证 / 明确不下结论的部分

- 第四章**战斗遭遇战**（基线 20.67 FPS）本轮未进入，只测到兵工厂探索场景；战斗场景的 pass 归因仍缺。
- 插入战斗的技能释放长帧未取证。
- 「每帧约 30 个单 draw R16G16B16A16 pass」的业务含义未定位（需要对着 pass 构建代码反查）。
- GPASS 的 pass 明细行未逐条核对 blend=unknown 的比例（本轮只看了汇总）。
- 与 Codex 07:41 数字口径不同（静止 vs 行走、是否插桩），**未做差值结论**。

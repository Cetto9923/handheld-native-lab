# Honor 特效首次释放卡顿：warm list 落盘缺陷修复 + seed 分发通路

- 任务：T-rescue-1.0.7-20261112（工程实现）
- 分支：`task/T-rescue-1.0.7-20261112»，基线 HEAD `e7ddc44»
- 设备：Honor 平板（Adreno / VK_QCOM_* 驱动），本会话**未接触任何设备**
- 本文所有"实测"均指在 Mac 宿主上对**真机日志**的复算，或对**真实交付产物**的读写验证；凡真机未跑的项一律标 NOT_RUN

---

## 0. 结论速览

1. **A 类卡顿（已修）**：引擎自己的 warm list 落盘有一个守卫缺陷 —— 只有"创建耗时 ≥ 2 ms"的管线才会触发落盘，快创建的管线被记录进内存却从不写盘，下一次启动重建。修复后 warm list 有**独立的 300 帧节流落盘**，与 `pipelines_unsaved» 完全解耦；shutdown 路径也会 flush。
2. **B 类卡顿（通路已建，效果待真机）**：全新特效的**首次**同步创建（单条 100–380 ms）无法靠 A 修复消除。A+B 合起来的效果是：**任何设备上已经建过的特效管线**，在新设备上第一次进战斗时就不再同步创建。已建立 seed 分发通路，并把 1560 条已校验记录作为 seed 实体增量写入现有 16.9 GB 交付 zip（未重打包）。
3. **C 项（把首次同步创建移出特效释放那一帧）：NOT_DONE**，原因见 §2.3，附可行方向与风险。
4. **对外口径**：本文**不写**"seed 已解决卡顿"。seed 只保证"记录格式与 shader 身份校验通过"，实际命中率与耗时需真机验证。

---

## 1. 根因（实测）

### 1.1 数据来源

| 项 | 值 |
|---|---|
| 真机引擎日志 | `.local/honor-motion-20261112/build8-spells-engine.log»（65.6 MB，13 次会话） |
| 统计脚本 | `.local/honor-motion-20261112/verify_sessions.py»（本次会话重跑，数字可复现） |
| 解析范围 | `NVN PERF pipelines_new» 行 **14420 条，全部匹配**；`NVN warm list loaded» **13 条**（每会话一次） |
| 交叉证据 | 驱动侧 `NVN pipeline cache save bytes=» 从 22360028 增长到 26002985 字节（同一份日志） |

### 1.2 两类卡顿必须分开

| | A 类：记录丢失导致重建 | B 类：全新特效首次创建 |
|---|---|---|
| 现象 | 每次都重建同一批特效 | 某批特效第一次出现时集体卡顿 |
| 单条耗时 | **0.5–1.7 ms**（驱动 pipeline cache 已命中） | **100–380 ms**（真编译） |
| 复现位置 | 会话 10 末批 `created=3 create_ms=1.7 max=0.6»；会话 11 `created=3 create_ms=1.2 max=0.5» | 会话 9 `created=60 create_ms=8576.7 max=372.8» |
| 用户感知 | 单条不卡，但"每场都重建" | **"卡爆了"** |
| 归因 | `warm::flush()» 落盘守卫缺陷（引擎侧） | 全新 key，驱动侧无缓存可命中（设备侧） |
| 本次修复 | **已修**（宿主测试覆盖） | **通路已建，命中效果 NOT_RUN** |

会话 9 的 60 条是**全新 key 且成功落盘**的：会话 8 启动时表为 1212 条，会话 10 启动时表为 **1272 条，增量恰好 60**。即这 60 条确实进了文件 —— 它们不是"记录丢失"，而是"当次确实全新"。这同时构成 §4.1 的 seed 覆盖性证据。

### 1.3 A 类的精确机制（代码级）

- `xbg_game.cpp:3220» 附近创建点：`if(dt>=compile_ns)++pipelines_unsaved;» 且 `if(cacheable)warm::record(*graphics,key);» —— **record 无条件，unsaved 有条件**。
- `compile_ns = 2000000»（2 ms，`xbg_game.cpp:1194»）。因此**任何快创建的管线（<2 ms）都不会让 `pipelines_unsaved» 增加**。
- `save_pipeline_cache()» 的守卫是 `if(!pipeline_cache || !pipelines_unsaved || cache_save_busy.load())return;»，而 `warm::flush()» 原本写在这个守卫**之后** → 快创建的管线被记录进内存 pending，却永远不落盘。
- `warm::flush()» 失败保留 pending 的语义原本是对的，本次**保留**（改为 take/restore，见 §5.1）。
- 触发点只有一处节流：`perf_frame()» 内 `if(pipelines_unsaved && presents>=pipeline_save_frame+300){...save_pipeline_cache();}»（`xbg_game.cpp:1239»）→ 守卫不满足时，节流窗口到了也不写。
- **shutdown 路径**：`xbg_game_shutdown()»（`xbg_game.cpp:6704»）调用一次 `save_pipeline_cache()» → 走同一守卫 → 若本场只有快创建，**进程结束时 pending 全丢**。这会放大 A 类问题（被强杀时整批记录消失）。

---

## 2. 方案与理由

### 2.1 A 的修法：让 warm list 的落盘有自己的节流

把"何时写 warm list"从 `pipelines_unsaved»（驱动 pipeline cache 的状态）里解耦出来，改成一个只依赖**已呈现帧数**的独立判定：

- 新增 `src/graphics/nvn_vulkan/engine/xbg_warm_flush.h»（83 行，头文件、零依赖、可在宿主编译）：
  - `xbg_warm_flush::due(presents, last_frame)»：帧数间隔判定，间隔常量 `kIntervalFrames = 300»（与 pipeline cache 同频但**独立**）。
  - `xbg_warm_flush::Pending»：线程安全的 pending 缓冲，`append/take/restore»。**take 出来的字节若写失败必须 restore**，从而"失败保留 pending"的语义逐字保留。
  - `xbg_warm_flush::append_file()»：追加写文件，失败返回 false。
- `perf_frame()» 内新增：`if(xbg_warm_flush::due(presents,warm_save_frame)){warm_save_frame=presents;warm::flush();}»（`xbg_game.cpp:1244»）。
- `save_pipeline_cache()» 内把 `warm::flush();» **移到守卫之前**（`xbg_game.cpp:1221»）→ shutdown 路径也随之 flush 掉 pending。

理由：

- **不引入每次 draw 写文件**：仍是 300 帧节流，pending 为空时 `take()» 直接返回 false，不产生任何 I/O。
- **不与驱动 pipeline cache 耦合**：两者的状态不同（一个是"驱动缓存里有变化"，一个是"我学到了新配方"），耦合正是缺陷本身。
- **flush 失败不丢数据**：take/restore 语义与原 `if(ok)pending.clear();» 等价，但把"组装记录"移出锁外，避免并发 flush 写半个配方。

### 2.2 B 的修法：seed 分发通路

引擎**已有**两条读 seed 的通路，本次只补"实体如何到达设备"，不新增引擎分支：

| 通路 | 落点 | 谁负责 |
|---|---|---|
| 环境变量 | `FFRS_WARM_LIST_SEED» → `<数据根>/pipeline-warm-list.bin» | `src/android/native/guest_boot_jni.cpp»（约 :296–308，已存在，按数据根推导，不硬编码） |
| 文件 | `<runtime>/shaders/vk-pipeline-list.seed» | `NativeDataBundle.stagePipelineWarmSeed()»（已存在，带 sha256 校验，设备已有历史表则保留历史） |

引擎侧 `warm::start()» 会把「设备自己的 list + `vk-pipeline-list.seed» + `FFRS_WARM_LIST_SEED»」三处记录用 `known»（以 record 的 words 字节为键）去重后合并，后台线程预建 → 画到时 `warm::take()» 命中即用。

本次新增（脚本侧）：

1. `scripts/distribution/package_honor_portable.py»
   - 白名单从**精确相等**改为「必须包含 5 项 + **可选** `pipeline-warm-list.bin»」→ 旧数据包（无 seed）仍能打包。
   - seed 放在 zip 的 `FFRS/» 根下、与 `FFRS/shaders.hnla» 同级（**不是** `FFRS/shaders/» 下），与 Android 侧 `<数据根>/pipeline-warm-list.bin» 的期望一致。
   - seed 进包前做校验（记录框架、唯一性、shader key 必须在归档索引内），并写进 manifest 的 `pipeline_warm_seed» 块，校验口径写明"仅记录框架与 shader 身份，不构成 GPU/性能验收"。
   - **`--add-to <existing.zip> --seed <file>»**：只追加 `FFRS/pipeline-warm-list.bin» 一个条目并重写中央目录，**不重算、不重写**既有 40547 条目的载荷 SHA。
   - **`--restore <zip>»**：按追加前落盘的"目录尾部备份 + 目录起始偏移"逐字节还原。
   - **`--refresh-delivery <dir> --add-to <zip>»**：把交付目录里的 `FFRS-data.zip.sha256» 与 `data-manifest.json» 同步到新包，避免团队按旧 SHA 校验失败。
2. `scripts/distribution/inspect_pipeline_warm_list.py»：新增 `archive_keys_from_file(handle, size)»，使 seed 可以**对 zip 内的归档**做校验（只读索引，不读载荷）；`archive_keys(path)» 行为不变。

### 2.3 C 项：把首次同步创建移出特效释放那一帧 —— NOT_DONE

**结论：在当前架构下无法可靠实现，本次不做半吊子实现。** 理由按证据分三条：

1. **技术前提不成立**：管线 key（`XbgPipelineKey»）是在**画出的那一刻**才可从实时图形状态算出来的 —— 依赖当前 render pass、pipeline layout、shader module（懒加载自归档）、以及 blend/depth/stencil/viewport 等全部状态。提交点 `xbg_submit_now()» 虽拿到了整批命令句柄，但没有"无副作用地再走一遍状态机"的现成设施；要做前瞻就得复制一份状态机，属于在最热路径上做大规模改造。
2. **无法验证**：本会话没有 Vulkan 设备，工程规则要求"真机性能 PASS 必须来自真机真实场景"；这种改造在宿主上无法证明它把耗时挪到了哪一帧，也证明不了没改动画/输入/过场时序 —— 属于"改完也证明不了"的高风险改动。
3. **现有替代路线已被红线排除**：引擎里唯一"画前不建管线"的机制是 `warm::async_request»，它**跳过该 draw**，让特效晚几帧出现 —— 这改变了游戏可观察行为，任务明确排除，故保持默认关闭（`gfx-async-pipelines» 标记，默认 0）。

**C 的实际归宿 = A + B**：C 想达到的效果（"首次创建不要压在特效释放那一帧"）由"配方进 warm list → 后台线程提前建好 → draw 时 `take()» 命中"实现，seed 只是把**别的设备**已经建过的配方搬过来。**seed 覆盖不到"全世界第一次出现"的全新特效**（会话 9 的 60 条就属于这一类：当时全新，事后才被记住）。

**可执行的后续方向（本次不做）**：给 `warm::start()» 的构建顺序加策略（例如把 seed 中**设备自己没有**的记录提到本机热记录之前，或按"上次战斗实际用到的顺序"排），因为当前顺序是「设备自己的表（新的在前）→ seed 追加在后」，seed 独有的管线排在最后才建（见 §4.3）。

## 3. seed 命中后是否仍要同步创建？—— 结论

**会。seed 命中不等于零成本。** 代码依据（`xbg_game.cpp:3172»，`warm::take»）：

````
bool take(const std::string &bytes,VkPipeline &out){
    std::lock_guard<std::mutex> l(mu);
    if(reuse_rejected)return false;              // 着色器复用被拒 → 不命中
    auto it=ready.find(bytes);
    if(it==ready.end())return false;             // 后台还没建完 → 不命中
    out=it->second;ready.erase(it);return true;}
```

- 命中 = **后台线程已经把这张管线建完并放进 `ready»**。命中则该 draw 不再同步创建。
- **未命中 → 该 draw 同步创建**（即原卡顿路径）。未命中的三种情况：
  1. 后台还在建（seed 有 1560 条、后台只有 3 个线程，见 §4.3）；
  2. 后台建失败（`warm::failed++»）—— 例如该配方在**当前设备/驱动**上建不出来；
  3. `reuse_rejected» 被置位。
- 即使命中，**成本是被挪走而不是被消除**：那张管线照样花了 100–380 ms（Honor 实测），只是花在后台线程（nice 5，低于游戏与图形线程）。
- 因此正确表述是：**"已建过的特效，第一次进战斗不再压在特效释放那一帧"**，而不是"零成本"。

---

## 4. seed 的量化证据与边界

### 4.1 覆盖性（实测，来自真机日志）

- 会话 9 `created=60»，会话 8 启动表 1212 条 → 会话 10 启动表 **1272 条**，增量 **恰好 60** → 这 60 条**全部成功落盘**，seed 格式完全能覆盖它们。
- seed 实体本身（已校验）：**1560 条记录 / 1560 条唯一 / 1375 个 shader key / graphics 1449 / compute 111 / `missing_shader_keys=[]»**，SHA-256 `34bcecdf527cddea1f73105ff24bd3215397de40d9ec34887d70c368bdc87117»，1638144 字节。

### 4.2 边界（必须写清）

- **seed 只覆盖"已经在某台设备上出现过"的特效管线**。从未在任何设备出现的全新特效，第一次仍会同步创建一次。
- **seed 是 device-specific 状态**：记录里含 render pass 的 attachment 格式/数量/loadOp、pipeline layout 与全部管线状态 → 在**同一台 Honor 设备**上一定可重建；**跨 GPU 型号（477M 的 Mali-G615）可能建不出来** → 那条记录会被丢掉（引擎 `read_record()» 校验不过就整条丢弃，`build()» 失败计入 `warm::failed»），该 draw 退回同步创建。**不得宣称 seed 跨设备通用。**
- **shader key 必须在目标归档里能解析**：引擎 `read_record()» 对每个 shader key 调 `shader_identity()»，解析失败整条记录丢弃 → 所以打包时用 `missing_shader_keys» 拦（本次为 0）。
- **管线创建耗时在 Mali 上可能显著不同**：创建耗时是**显式参数**（引擎里是 `compile_ns» 阈值与实测 `create_ms»），不是常数；报告与验收指标一律用实测值。

### 4.3 一个已知且未解决的限制（推断，需真机验证）

`warm::start()» 的顺序是：先读设备自己的 list → `std::reverse»（新的在前）→ 再读 seed 文件与环境变量 seed（**追加在后面**）。后台线程按顺序建。因此 **seed 独有的管线排在最后才建**。按 Honor 实测均值约 143 ms/条、3 个后台线程估算，1560 条 seed 全量建完约需**数十秒**的后台驱动时间。**第一场战斗中能吃到多少覆盖，取决于构建顺序与开打时机** → 这是一个**需要真机测量**的点，也是 §2.3 里"构建顺序策略"这条后续方向的价值所在。

---

## 5. 改动清单

### 5.1 引擎（进 1.0.8）

| 文件 | 改动 |
|---|---|
| `src/graphics/nvn_vulkan/engine/xbg_warm_flush.h»（新增，83 行） | 落盘判定 `due()»、线程安全 `Pending»（take/restore）、`append_file()» |
| `src/graphics/nvn_vulkan/engine/xbg_game.cpp:20» | 引入新头文件 |
| `xbg_game.cpp:1194» | 新增 `warm_save_frame»（与 `pipeline_save_frame» 独立） |
| `xbg_game.cpp:1217-1221» | `save_pipeline_cache()»：**`warm::flush()» 移到守卫之前** → shutdown 也能 flush |
| `xbg_game.cpp:1239-1244» | `perf_frame()»：**独立 300 帧节流**写 warm list |
| `xbg_game.cpp:2868/2901» | `pending» 改用 `xbg_warm_flush::Pending»；`flush()» 改为 take→写→失败 restore（**"失败保留 pending"语义不变**） |

### 5.2 脚本与测试（进 1.0.8）

| 文件 | 改动 |
|---|---|
| `scripts/distribution/package_honor_portable.py» | 白名单允许可选 seed；seed 校验与 manifest 块；**`--add-to»**（增量追加 + 全量 CRC 回读 + 目录尾部备份）；**`--restore»**；**`--refresh-delivery»** |
| `scripts/distribution/inspect_pipeline_warm_list.py» | 新增 `archive_keys_from_file()»（流式读归档索引），`archive_keys()» 行为不变 |
| `tests/graphics/test_warm_flush_contract.cpp»（新增，204 行） | 落盘判定、pending 字节、**失败不丢数据**、并发 record/flush 不丢不重、文件追加、引擎接线断言 |
| `tests/installer/test_package_honor_portable.py» | 打包白名单、seed 随包、坏 seed/未知 shader/软链被拒；**增量追加、payload 未被重写、二次追加被拒、逐字节还原、交付清单同步** |
| `scripts/verify_host.py» | 接入 warm flush 契约与打包测试 |

### 5.3 本地产物（不入 Git，».local/»）

- seed 实体：».local/seed/pipeline-warm-list.bin»（rescue worktree）、交付包内 `FFRS/pipeline-warm-list.bin»。
- **真实交付 zip 已增量更新**：».local/FFRS-data.zip»，40547 → **40548** 条目，16918705179 → **16920343485** 字节（+1638306 = seed 1638144 + 头/目录 162），SHA-256 `01a338c5…10411af» → `873067146d9899891d0e1c69b9a6696bb9bf1f739b9797869c2ceeea52d78347»。
- 交付目录 ».local/honor-delivery-20261107/release/» 的 `FFRS-data.zip.sha256» 与 `data-manifest.json» 已同步（file_count 40548，新增 `pipeline_warm_seed» 块）。
- 还原点：».local/FFRS-data.zip.tail.bak»（3973524 B）+ `FFRS-data.zip.prewarm-seed.json» 里的 `restore_command»，可把 zip **逐字节**还原到追加前。

---

## 6. 公开出处（先查出处再改）

本修复**不修改驱动 pipeline cache 的语义**，只修引擎自己的配方表落盘；以下出处用于确认"驱动 cache 的用法前提"与"管线应提前创建"的规范依据。

| 出处 | URL / revision | 许可证 | 支持范围 | 是否真的取到 |
|---|---|---|---|---|
| Vulkan 规范正文（`chapters/pipelines.adoc»） | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/chapters/pipelines.adoc ，commit `f7de774cace1509af0f10b8d859c973b5354ff62»（2026-11-12） | 仓库 Apache-2.0（正文 adoc 文件内标 `SPDX-License-Identifier: CC-BY-4.0»） | 全 Vulkan 1.0+ 核心 | **已取到原文**（HTTP 200，11018 行，关键段落已引用） |
| `vkGetPipelineCacheData» 语义 | 同上，正文第 8481 行附近 | 同上 | 核心 | **已取到**：*"Two calls to vkGetPipelineCacheData with the same parameters must retrieve the same data unless a command that modifies the contents of the cache is called between them"*；*"Any data written to pData is valid and can be provided as the pInitialData member of VkPipelineCacheCreateInfo"* |
| `vkDestroyPipelineCache» 有效用法 | 同上（VUID-vkDestroyPipelineCache-pipelineCache-00771/00772） | 同上 | 核心 | **已取到**：规范对该销毁只约束 allocator 回调，**未**规定与 `vkGetPipelineCacheData» 的先后；本仓库的 `write_pipeline_cache()»（用 `vkGetPipelineCacheData»）在 `xbg_game_shutdown()» 中先于 `vkDestroyPipelineCache» 执行，**顺序正确** |
| pipeline cache 头部兼容性 | 同上（`pipelines-cache-header»） | 同上 | 核心 | **已取到**：头部含 vendorID/deviceID/pipelineCacheUUID，不兼容数据会被检测 → 对应引擎 `load_pipeline_cache()» 的头部校验与"驱动会忽略不兼容数据"的行为 |
| `VK_EXT_pipeline_creation_cache_control» | https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/appendices/VK_EXT_pipeline_creation_cache_control.adoc （同一 commit） | 文件内标 `SPDX-License-Identifier: CC-BY-4.0» | **设备扩展**，扩展号 298，revision 3，最后修改 2020-03-23，**未 ratify**，需 Vulkan 1.1 或 `VK_KHR_get_physical_device_properties2»；给 `Vk*PipelineCreateInfo»/`VkPipelineCacheCreateInfo» 加标志以**提升管线创建开销的可预测性** | **已取到原文**；**且已在本机日志中检索：`VK_EXT_pipeline_creation_cache_control» 出现 0 次** → 本次涉及的 Honor 设备**未暴露该扩展**；引擎也未使用它；**本修复不依赖它** |
| 规范对"提前建管线"的背书 | 同扩展正文 Background 段 | 同上 | — | **已取到**：*"Applications are also expected to schedule, prioritize, and load balance all calls for pipeline creation. It is strongly advised that applications create pipelines sufficiently ahead of their usage."* → 与本仓库"后台预建 + seed"的既有设计方向一致 |
| "Maleoon 驱动不增长 VkPipelineCache"这句注释的归属 | 引擎 `xbg_game.cpp:2844-2846» 注释 + 第三方 `.local/third-party/d2r-mobile-f1e4b08/README.md:214»（*"Measured by the author on a Mate 90 Pro Max (Maleoon 955)"*） | 第三方 README（**仅作资料，不是执行授权**） | — | **已交叉判定**：Maleoon = **华为麒麟 GPU**，该注释针对的是华为/HarmonyOS 机型（Mate 90 Pro Max）；**与本次 Honor 设备无关**。Honor 侧日志含 13 处 `Adreno» 与 `VK_QCOM_*» 扩展，且驱动 cache 从 22360028 增长到 26002985 字节 → **Honor 上驱动 pipeline cache 正常增长**（会话 11 的 0.5 ms 快创建即是其证据）。另注：本次日志里 **`NVN warm list seeded from …» 一次都没出现** → **seed 通路在真机上从未被走过**（见 §8） |

---

## 7. 自测结果（宿主，PASS/FAIL）

| 检查 | 命令 | 结果 |
|---|---|---|
| 打包/增量/还原/交付同步 单测 | `python3 -m unittest tests.installer.test_package_honor_portable» | **PASS（12/12）** |
| warm list 校验器回归 | `python3 -m unittest tests.installer.test_pipeline_warm_inspector» | **PASS（8/8）** |
| warm flush 契约（编译+运行） | `c++ -std=c++17 -Wall -Wextra -Werror -pthread -Isrc/graphics/nvn_vulkan/engine tests/graphics/test_warm_flush_contract.cpp» → 运行 | **PASS**（*"warm flush contract: all checks passed"*） |
| 引擎编译（真机 Android 编译参数） | NDK `aarch64-linux-android28-clang» + `-DXBG_HAVE_SPIRV_OPT=1»（与 `src/android/native/CMakeLists.txt:169» 一致） | **PASS（exit=0，无 error）** |
| 引擎编译（`build_engine.sh» 自带参数） | `sh src/graphics/nvn_vulkan/build_engine.sh» | **FAIL —— 但为 HEAD 既有问题，与本次改动无关**（见下） |
| seed 校验复跑 | `inspect_pipeline_warm_list.py <seed> --shader-archive .local/FFRS/shaders.hnla» | **PASS**：1560/1560/1375，graphics 1449、compute 111，`missing_shader_keys=[]»，exit 0 |
| 真实 zip 增量追加 | `--add-to .local/FFRS-data.zip --seed .local/seed/pipeline-warm-list.bin» | **PASS（EXIT=0）**：40547→40548 条目；**全部 40548 条目 CRC 回读通过**；既有 40547 条目的 **offset/size/CRC 逐一不变**；新条目 SHA-256 回读一致；文件仅增长 1638306 字节 |
| zip 独立抽检（第二个读取器） | `python3 -c "zipfile…"» | **PASS**：40548 条目、seed 在 `FFRS/pipeline-warm-list.bin»、SHA 一致、`FFRS/shaders.hnla» 头正常、`FFRS/main.nso» 125577976 B、romfs 40533 条目 |
| 交付清单同步 | `--refresh-delivery …/honor-delivery-20261107/release --add-to .local/FFRS-data.zip» | **PASS（EXIT=0）**：file_count 40548、zip_sha256 更新、`pipeline_warm_seed» 块写入 |

**关于 `build_engine.sh» 的 FAIL（既有缺陷，非本次引入）**：该脚本不定义 `XBG_HAVE_SPIRV_OPT»，而 `xbg_game.cpp» 在 `#ifdef» **之外**使用了 `honor_inline_enabled»（声明在 `#ifdef XBG_HAVE_SPIRV_OPT» 内，行 2522；使用在行 2683）。
证据：把 **HEAD 原版** `xbg_game.cpp»（未含本次改动）放到同目录、用**完全相同的脚本参数**编译，同样报 `use of undeclared identifier 'honor_inline_enabled'»（HEAD 行号 2672，本次改动后行号 2683）。
→ **这是 HEAD 既有的构建脚本缺陷**，按"不擅自修改"原则**本次未改**，登记给总控；真实 Android 构建（CMake 定义了该宏）不受影响，本次改动在该路径下编译通过。

---

## 8. 需总控安排的真机验证（全部 NOT_RUN）

> 本会话**未接触任何设备**（Honor 平板 `A42BBB5109000333» 由总控独占）。以下为**必须**在真机上跑的项与判据。

### 8.1 A 类（落盘修复）

| 项 | 做法 | 通过判据 |
|---|---|---|
| A-1 落盘 | Honor 装带本修复的包，进战斗触发特效，**正常退出** | `files/vk-pipeline-list.bin» 的 **added 条数**增长，且增长条数 = 本场新增管线数（不再只增长"≥2 ms"的那部分） |
| A-2 无强杀 | 战斗中用系统回收/强杀进程，再启动 | 记录**丢失**（当前缺陷的直接症状）消失；A-1 的表不再回退 |
| A-3 跨会话 | 连续 3 次冷启动进战斗 | 快速创建批次（`create_ms<2»）消失；`NVN PERF pipelines_new» 的 `created=» 逐会话趋近 0 |

### 8.2 B 类（seed）

| 项 | 做法 | 通过判据 |
|---|---|---|
| B-1 seed 被读取 | 把 `pipeline-warm-list.bin» 放进数据根，重装 | 日志出现 `NVN warm list seeded from … ok=1»（**本次日志中该行从未出现**，说明这条通路此前从未被走通过） |
| B-2 首次战斗不卡 | 装 seed 后**第一次**进战斗 | `NVN PERF pipelines_new» 行：`created=» 显著下降、`warm_hits=» 上升、`create_max_ms» 不再有 >100 ms 尖峰 |
| B-3 覆盖窗口 | 记录 seed 后**立刻**开打 vs **静置 N 秒**后开打 | 量化 §4.3 的构建顺序影响（决定 seed 的实际收益） |

### 8.3 取证口径（必须遵守）

- 只用 `NVN PERF pipelines_new» 行的 `created=» / `warm_hits=» / `create_ms=» / `create_max_ms=» 与 `NVN warm list loaded … added=»。
- **禁止逐笔打印**（会改帧节奏）；日志走缓冲文件或引擎内 ROI 指标。
- **冷缓存与暖缓存分开测**，并记录设备型号、驱动版本、构建 ID、脏/净状态、原始日志路径。
- 帧率 PASS 必须来自真机真实战斗场景；菜单/空场景/屏幕刷新率不可替代。

### 8.4 477M（Mali-G615）需补测（本修复在 477M 上的未验证假设）

未验证假设（**全部为推断，需实测**）：

1. **假设**：Mali 上 seed 里 Honor 学到的记录**可能建不出来**（render pass attachment 格式/layout 与 Adreno 不同）→ 那部分记录会被丢弃或 `warm::failed»。
2. **假设**：Mali 的**管线创建耗时与 Honor 不同**（本修复所有耗时口径都是 Honor 实测值，不能外推）。
3. **假设**：Mali 驱动 `VkPipelineCache» 的增长行为与 Adreno 不同 —— 477M 侧 `vk-pipeline-cache.bin» 仅 279199 B，而 Honor 侧达 22–26 MB，**这是已观察到的跨设备差异证据**，但其成因（缓存大小 vs 命中率）未查清。
4. **假设**：477M 已有自己的 warm list（`/sdcard/HandheldNativeLab/data/pipeline-warm-list.bin»，905028 B）与 `vk-pipeline-list.bin»（19880 B），A 类修复对它是"少重建"而非"从零开始"。

需在 477M 上补测：

| 项 | 判据 |
|---|---|
| C-1 A 类生效 | 快创建批次消失、`created=» 趋近 0、跨会话表增长条数 = 新增管线数 |
| C-2 seed 兼容性 | 装 Honor seed 后 `warm_built/warm_failed» 比例；若 `warm_failed» 显著 → **确认假设 1**，seed 对 477M 基本无效（应改为按设备分别收集 seed） |
| C-3 真实战斗耗时 | Mali 上单条创建耗时（用于替换 Honor 的 100–380 ms 口径） |
| C-4 477M 专属 seed | 若 C-2 失败，应在 477M 上单独采集自己的 seed（**不要**把 Honor 的 seed 发给 477M 用户） |

---

## 9. 未完成项与风险

| 项 | 状态 | 风险/说明 |
|---|---|---|
| C：首次同步创建移出特效释放帧 | **NOT_DONE** | 理由见 §2.3；需真机才能验证的架构级改造 |
| seed 构建顺序策略（seed 独有记录提前建） | 未做 | §4.3：当前 seed 记录排在最后，收益取决于开打时机 → 建议作为 1.0.9 的独立改动，**需真机 A/B** |
| 真机验收 | **NOT_RUN** | 见 §8（需总控安排设备） |
| `build_engine.sh» 缺 `-DXBG_HAVE_SPIRV_OPT» | **未修（既有缺陷）** | HEAD 即失败；已定位到行号与规范依据，供总控决定是否单独修 |
| 477M 侧适配 | **未验证** | §8.4 |
| 主仓集成冲突 | **需总控处理** | 主仓 `src/graphics/nvn_vulkan/engine/xbg_game.cpp» 有他人未提交 WIP（MM 状态）。本次同样改了这个文件的 `warm» 命名空间与 `save_pipeline_cache»/`perf_frame» 区域 → **同文件同区域，集成时需人工合并** |
| 交付 zip 的 SHA 已变 | 已同步交付目录 | `01a338c5…» → `873067146d…»。**`coordination/state.json:6039» 与 `HANDOFF.md» 里仍写着旧 SHA** → 那是总控的文件，本次**未改**，请总控更新 |
| seed 是 Honor 学习所得 | 事实 | 跨 GPU 不保证可建（§4.2）；发给 477M 用户前应先过 §8.4 的 C-2 |

---

## 10. 结论性质标注

**实测（本会话亲自跑出，可复现）**

- 真机日志 13 会话的 `created/create_ms/create_max_ms» 统计（§1.1/§1.2）；会话 9 的 60 条 → 会话 10 表 1212→1272 的落盘闭环。
- seed 校验复跑（1560/1560/1375、`missing_shader_keys=[]»）。
- 打包单测 12/12、校验器单测 8/8、warm flush 契约编译并运行通过。
- 引擎在**真机 Android 编译参数**下编译通过（exit=0）；HEAD 原版在 `build_engine.sh» 参数下同样失败（证明既有问题）。
- 真实 16.9 GB zip 的增量追加：条目数、字节数、前后 SHA、全量 CRC 回读、既有条目元数据逐一不变、独立读取器抽检、交付清单同步。

**推断（基于代码/日志，未在真机验证）**

- "A 类每场重建 3 条"的因果链（<2 ms → `pipelines_unsaved» 不增 → 不落盘 → 下次重建）——代码路径明确，但修复后的真机表现 NOT_RUN。
- seed 的实际命中率、B-2 的 `created=» 下降幅度、B-3 的覆盖窗口。
- seed 独有的管线排在最后建 → 首个战斗的覆盖可能不足（§4.3）。
- 477M 上 Honor seed 可能建不出来（§8.4 假设 1）。

**未做（明确不做）**

- C 项的架构改造（§2.3）。
- 机型/GPU 字符串分支：**未新增任何按 GPU 分支的逻辑**。引擎里既有的 `strstr(deviceName,"Adreno")»（`xbg_game.cpp:6501»，用于 Honor 内联实验）**不是本次引入**，本次也未触碰；判定设备归属只用了日志里的扩展表证据（§6）。
- 未修改主仓 / `release-1.0.7» / `package-1.0.7» 两个 worktree；未 push；未动 tag；未操作设备。
- 未重打包 16.9 GB 全包（只做增量追加 + 全量 CRC 回读）。

---

## 11. 1.0.8 建议

| 部件 | 是否进 1.0.8 | 理由 |
|---|---|---|
| A：warm list 独立节流落盘 + shutdown flush | **建议进** | 改动小、自包含、宿主可测；直接消除"记录丢失 → 每场重建" |
| B：`--add-to» / `--restore» / `--refresh-delivery» / 白名单可选 seed | **建议进** | 打包通路是交付质量的一部分，且不改变游戏行为 |
| B：seed **实体** | **不进 Git**，走 ».local/» 与交付包 | seed 是设备学习产物（含设备相关状态），按工程规则属"生成物"，不入仓库；每个发布批次重新采集/复核 |
| C | 不进 | 未实现 |
| `build_engine.sh» 补宏 | 建议**单独**修（可与 1.0.8 无关） | 既有缺陷，与本任务无耦合 |

**集成提示**：本分支改动集中在 `src/graphics/nvn_vulkan/engine/xbg_game.cpp»（`warm» 命名空间 + `save_pipeline_cache» + `perf_frame» 三处）与两个分发脚本 + 两个测试文件；主仓同文件有他人 WIP，请按区域人工合并。


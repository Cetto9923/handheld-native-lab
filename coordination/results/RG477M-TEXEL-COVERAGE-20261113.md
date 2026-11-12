# RG477M Mali typed-storage 改写：全量离线覆盖统计（20261113）

分支 `task/T-rg477m-texel-20261112`，本文件对应 HEAD `ad1d820`（基线 `db5218e`）。

## 0. 本文件的定位与边界

- 这是**离线统计**：它回答「Mali typed-storage 改写规则能把多少模块改成 typed 形态，
  剩下的是哪些、为什么」，不回答「游戏在 RG477M 上能不能跑」。
- **真机腿 NOT_RUN**：设备 `RG477M01445122` 本轮不在线，脚本 `scripts/android/verify_rg477m_typed_storage.sh`
  在设备未连接时以 `BLOCKED_DEVICE` 退出 2。本文件不含任何真机 PASS，也不含任何推测出来的真机结论。
- 所有判定都来自**设备上运行的同一份代码**：离线分析器直接 `#include` 生产头
  `src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h` 与 `xbg_spirv_stencil.h`，
  不是二次实现。
- 严格限定：模块能不能被 typed，取决于**运行时绑定的 VkFormat**。离线只能给出上界与条件，
  见 §7。本文件所有「覆盖」都是*条件性覆盖*。

## 1. 输入与出处

| 项 | 值 |
|---|---|
| 归档 | `/Users/yuyan9923/GitHub/ZSPACE/sata1-18061991088/03-游戏资源/00-移植游戏/01-最终幻想-共鸣/FFRS-data.zip`（NAS 网络挂载，16.9 GB） |
| 条目前缀 | `FFRS/romfs/shaders/<16hex>.xgs` |
| 真实 shader 条目 | **20258** |
| 被排除的伪条目 | 20258 个 macOS AppleDouble `._*` 条目 |
| 归档内 `FFRS/shaders.hnla` | 6089256224 字节（设备侧同一份归档） |
| 解压方式 | **不整包解压**：`zipfile` 逐条目流式读取，按需喂给分析器 |

来源校验（provenance）：归档内 `664748e31a2dfad9.xgs` 与设备 6 GB `shaders.hnla` 抽出的同名文件
**逐字节相同**，双方 SHA-256 均为 `863bfb1723a5fde5f8877199e0ce24fc35efc5eb48534b235a592f993519f0f7`；
对应 SPIR-V 的 SHA-256 为 `495b8b7f1151bcca639273bc7c7b1b8732e53306fefe7bfcffe2a988a8705ec9`，
与前序报告 `RG477M-TEXEL-20261112-RESULT.md` 记录一致。

## 2. 方法

1. **判定来自生产代码**：分析器 `tools/texel-coverage/typed_storage_analyzer.cpp` 直接包含生产头，
   每个 binding 的结论由真正的 `xbg_typed_storage_rewrite` 给出。
2. **复刻引擎的 load-time 顺序**：先 `xbg_spirv_ffrs_stencil`，再复刻 stencil 成功后
   `xbg_ffrs_integer_fact` / `xbg_ffrs_uint_binding` / `xbg_ffrs_uint_store_binding` 对
   package flags 的 scalar 覆写。其它 load-time 改写（quad/warp/ldc/temps/compaction）不触碰
   `OpTypeImage` 与 image 操作码，grep 实证，因此不改变本口径。
3. **模块级口径**：与 `typed_storage_variant` 一致——只要有一个 formatless-write target 失败，
   整个模块退回 formatless。binding 级明细同时保留。
4. **候选格式**：SPIR-V 能表达的 9 个 VkFormat（98/99/100/101/102/103/107/108/109）。
   R16_UINT 之类 SPIR-V 无法表达的格式不在候选集内。
5. **独立诊断镜像**：分析器另写一份逐步镜像的诊断器解释每次拒绝，并**逐例断言**
   「诊断结论 == 生产返回值」「诊断原因 == 生产返回的原因」。
   全量 20258 模块、3836 个 target、9×3836 次候选改写：**verdict 分歧 0，原因分歧 0**
   （`analyzer.stderr: diagnostic mismatches: 0`，分析器退出码 0）。
6. **spirv-val 验证**：每个被接受的改写都落盘为 `.spv`，逐个跑
   `spirv-val --target-env vulkan1.1`。

## 3. 全量硬数字（改写后，HEAD `ad1d820`）

| 指标 | 值 |
|---|---|
| 模块总数 | 20258 |
| 解析成功 / 失败 | 20258 / **0** |
| stage 分布 | 0→2157，3→2，4→15406，5→2693 |
| 含 storage binding 的模块 | 2498 |
| 含 formatless **write** 的模块 | 2476 |
| 含 formatless **read** 的模块 | 2 |
| storage binding 总数 | 4835 |
| formatless write binding | 3836 |
| formatless read binding | 2 |
| `count != 1` 的 binding | 0 |
| 模块里没有对应 image 的 binding | 0 |
| 模块自身声明 Unknown（formatless）的 binding | 3836 |
| 模块自身已 typed 的 binding | 999 |
| typed 变体 target 数 | 3836 |
| **target 覆盖数** | **3832**（未覆盖 4） |
| **模块级覆盖** | **covered 2472 / uncovered 4** |
| 其中引擎真正能建变体（保留 `module_words`） | **1082**（另 1390 个 storage-image-only 模块被引擎丢弃模块字节，见 §9） |
| 拒绝原因计数（按 binding × 候选格式） | `image-type-shared-with-other-format` **16**、`formatless-read-on-same-image` **18**、`duplicate-type-not-reusable` **2** |

可接受格式集合（binding 级）：`{98}`×2、`{98,101,107}`×8、`{99,102,108}`×3、`{100}`×2442、
`{100,103,109}`×1376、`{101,107}`×1。

**设备不启用 `StorageImageExtendedFormats` 时，模块级覆盖完全相同**（2472 / 4），只是候选集里
不再出现 RG32 三兄弟（`extended-format-not-supported` 计 12 次）。即：覆盖结论不依赖该特性。

## 4. `spirv-val` 验证结果

| 规则版本 | 改写后模块数 | 通过 | 失败 |
|---|---|---|---|
| 基线 `db5218e` | 5520 | 4165 | **1355** |
| 改写后 `ad1d820` | **9079** | **9079** | **0** |

验证器：`/opt/homebrew/bin/spirv-val`，`SPIRV-Tools v2026.4 unknown hash, 2026-09-10T13:28:18+00:00`，
目标环境 `--target-env vulkan1.1`。

基线的 1355 个失败由两类构成（这是本轮最重要的发现）：

| 类型 | 数量 | spirv-val 原文（样例） |
|---|---|---|
| 扩展格式缺 capability | **1353** | `error: line 114: Operand 8 of TypeImage requires one of these capabilities: StorageImageExtendedFormats`（`%61 = OpTypeImage %float Buffer 0 0 0 2 Rg32f`） |
| 重复类型声明 | **2** | `error: line 109: Duplicate non-aggregate type declarations are not allowed. Opcode: TypeImage id: 46`（模块 `b673e10f3da2451c`） |

两者都发生在**设备上会真的创建模块**的路径上：SPIR-V 无效即违反 Vulkan 对模块有效性的要求。

## 5. 基线与改写后的对比

| 指标 | 基线 `db5218e` | 改写后 `ad1d820` |
|---|---|---|
| 模块级 covered | 1356 | **2472** |
| 模块级 uncovered | 1120 | **4** |
| 未覆盖 binding | 2448 | **4** |
| 拒绝 `image-type-shared-by-multiple-descriptors` | 22014 | 0（改为 `image-type-shared-with-other-format` 16） |
| 拒绝 `sampled-type-class-mismatch` | 12 | 0 |
| 拒绝 `formatless-read-on-same-image` | 6 | 18 |
| 拒绝 `duplicate-type-not-reusable` | — | 2 |
| 改写后模块 spirv-val | 4165/5520 | **9079/9079** |

基线数字是用 `db5218e` 的规则头 + `38957a0` 的分析器**重新跑一遍**得到的（见 §10），
不是记忆值。

## 6. 未覆盖模块清单（全量，仅 4 个）

| key | stage | identity | 引擎可用 | 原因 | binding | 描述符类型 | sampled class | 共享 image type 数 | 可接受格式 |
|---|---|---|---|---|---|---|---|---|---|
| `090014edce599091` | 5 | `090014edce599091` | 否（storage image only） | formatless-read-on-same-image | 53 | storage image | 0 (float) | 1 | 无 |
| `6ff5fc1ca693f953` | 5 | `6ff5fc1ca693f953` | 否（storage image only） | formatless-read-on-same-image | 35 | storage image | 0 (float) | 1 | 无 |
| `9bee7f09d6c655c3` | 5 | `9bee7f09d6c655c3` | 是 | duplicate-type-not-reusable | 29, 35 | storage texel buffer | 2 (uint) | 2 | 无 |
| `b673e10f3da2451c` | 5 | `b673e10f3da2451c` | 是 | duplicate-type-not-reusable | 41, 47 | storage texel buffer | 2 / 1 | 1 / 1 | 41: {101,107}，47: {99,102,108} |

前两个模块在同一 image 上既有 formatless write 又有 formatless read，typed 形态会改变读语义，
按规则必然被拒（见 §8.1）。后两个模块的模块里已经存在同形状的 typed image type，
改写会造成重复类型声明，而复用现有类型所需的指针类型在该变量之后才声明（SPIR-V 要求先定义后使用），
因此也保持 formatless。

完整的 binding 级明细在 `.local/rg477m-texel-20261112/coverage/uncovered_bindings.tsv`
（4 行）与 `module_verdicts.tsv`（2476 行，含每个模块的 identity 与每个 binding 的候选格式）。

## 7. 条件性：运行时绑定格式决定实际覆盖

离线**无法**确定绑定格式：

- `texel_binding_format` 依赖运行时纹理池的原生视图格式；package 的 scalar 字段还会强制
  `R32_UINT`（scalar==1）或 `R16_UINT`（scalar==2，SPIR-V 无法表达 → 该 binding 直接出局）。
- 本文件的离线模型取「模块 sampled type 的标量类别 → 单分量族」作为每个 target 的绑定格式：
  uint→98、signed→99、float→100。**这是上界**：真实绑定格式若不同，覆盖结论可能变小。
- 共享 image type 的组额外要求：运行时**所有 sharer 解析出同一个 VkFormat**。引擎用
  `XbgStorageFormatMap` 证明这一点；只要有一对不一致，整模块退回 formatless（引擎拒绝，不是崩溃）。
- storage image 的绑定格式是 image view 格式（多为 RGBA8 之类），通常**不在** 9 个可表达格式里，
  因此 storage image target 的实际覆盖大概率低于本表；texel buffer target（1990 个，分布在 1082 个
  引擎可用模块里）才是这条路径的主要受益面。

一句话：`3832/3836` 是「存在一种运行时格式分配使改写成立」的上界，不是「必然成立」。

## 8. P1：改写规则的扩展与论证

改动文件：`src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h`、
`src/graphics/nvn_vulkan/engine/xbg_game.cpp`（提交 `d19a037`、`ad1d820`）。

### 8.1 共享 image type（覆盖提升的主力：22014 → 16）

- 规则：共享同一 `OpTypeImage` 的多个 descriptor，只有在**每个 sharer 都声明同一格式**时才允许 typed。
- 证明由调用方提供：引擎用运行时解析出的格式填 `XbgStorageFormatMap`（binding → SPIR-V image format），
  改写时逐个 sharer 校验；不满足即拒绝，模块保持不变。
- 语义论证：被 typed 的 image type 的声明格式 == 该 descriptor 实际绑定的 view 格式
  （引擎绑定时用 `xbg_typed_storage_matches` 强制精确一致），因此写入字节数、texel 地址步长、
  被写分量数与 formatless 形态完全一致（与既有 typed 路径同一条论证）。
  其余 sharer 的声明格式未变（本次只改一个类型操作数），访问语义不变。
- **不改判定条件换取通过**：旧规则「image type 必须独占」被**放宽**，不是把失败条件改掉；
  阴性对照（另一个 sharer 声明不同格式 → 拒绝，模块逐字节不变）写在宿主契约里。

### 8.2 扩展格式的 capability（1353 个无效模块）

- Rg32f / Rg32i / Rg32ui 在 SPIR-V 里必须配 `StorageImageExtendedFormats` capability，否则模块无效。
- 改写现在会在模块首段插入 `OpCapability StorageImageExtendedFormats`（capability 号 49，
  与引擎 `validate_capabilities` 里接受的编号一致）；该 capability 无 result id，模块 id bound 不变。
- 设备不支持该特性时（`cfg.enabled_core.shaderStorageImageExtendedFormats` 为假）这些格式被拒绝，
  模块保持 formatless；引擎在改写后重新跑一次 `validate_capabilities`。

### 8.3 重复类型声明（2 个无效模块）

- 若 typed 后的 image type 与模块里已有类型同形，改写会让 SPIR-V 出现两条相同的非聚合类型声明
  （SPIR-V 禁止）。此时变量改为**取用**已存在的类型（同时更新该变量的 `OpLoad` 结果类型），
  而不是声明第二份；找不到可用的 UniformConstant 指针类型时拒绝。
- 只有在「该类型独占 + 该变量不读该 image + 指针类型先于变量声明」时才允许复用，否则保持拒绝。

### 8.4 宿主契约与阴性对照

`tests/device/test_typed_storage.cpp`（+ 新 fixture `tests/device/typed_storage_duplicate_type.spvasm`）：

- 绿：Unknown → R32ui 且只有 Image Format 一个操作数变化；幂等；共享 image type 在两方同格式时被 typed；
  扩展格式在特性可用时被 typed 并带上 capability；重复类型复用现有类型。
- 红（阴性对照）：共享 image type 的另一方声明不同格式 → 拒绝且模块逐字节不变；无 map 时拒绝；
  扩展格式在无特性时拒绝且模块不变；formatless read 拒绝；标量类别不符拒绝；未知 binding / 未知格式 /
  畸形模块拒绝且不变。

`scripts/verify_host.py` **此前没有** typed-storage 组（任务简报称已接入，实测未接入），本轮补上：
装配 4 个 fixture、编译契约、跑契约、对 4 个改写结果各跑一次 `spirv-val --target-env vulkan1.1`。

## 9. 未完成与风险

1. **真机腿 NOT_RUN**：设备 `RG477M01445122` 不在线。进标题、进战斗、画面正确性、FATAL 缺席
   全部无证据。脚本已就绪，设备上线后一次运行即出结论（§10）。
2. **2 个模块无法被 typed 路径覆盖**：`090014edce599091`、`6ff5fc1ca693f953` 在同一 image 上
   formatless write + formatless read。Mali 没有 `STORAGE_{READ,WRITE}_WITHOUT_FORMAT` 位，
   这两个模块在真机上仍可能触发 FATAL。本轮未改（改写会改变读语义）。
3. **1390 个 storage-image-only 模块拿不到 typed 变体**：`load_shader` 只在包里有
   `STORAGE_TEXEL_BUFFER` binding 时保留 `module_words`，而 typed 变体以 `module_words` 为源。
   离线看这 1390 个模块的 1768 个 target 全部可改写，但引擎根本不会建变体。
   本轮**未改**这个前提：它属于引擎加载路径而非改写规则，且 storage image 的绑定格式（view 格式）
   多半不可表达，实际收益不确定，需要真机日志再定。
4. **共享组的运行时同格式要求**是条件性的，见 §7。
5. `AGENTS-HONOR.md` 在主仓根目录**不存在**（ENOENT），主仓与 honor 仓均不可动，故本轮只以
   `AGENTS.md` 为规范来源。

## 10. 复现命令（工作区 `.worktrees/rg477m-texel-20261112`）

```sh
ZIP="/Users/yuyan9923/GitHub/ZSPACE/sata1-18061991088/03-游戏资源/00-移植游戏/01-最终幻想-共鸣/FFRS-data.zip"

# 分析器（直接 include 生产头）
clang++ -std=c++17 -O2 -g -Wall -Wextra -o .local/rg477m-texel-20261112/typed_storage_analyzer \
  tools/texel-coverage/typed_storage_analyzer.cpp

# 全量扫描（不整包解压）
python3 tools/texel-coverage/scan_xgs_coverage.py --zip "$ZIP" \
  --analyzer .local/rg477m-texel-20261112/typed_storage_analyzer \
  --out .local/rg477m-texel-20261112/coverage \
  --dump-dir .local/rg477m-texel-20261112/coverage_dump
# -> shader bundles: 20258 / analyzed: 20258 in 17.0s (analyzer exit 0)
# -> analyzer.stderr: diagnostic mismatches: 0

# 汇总
python3 tools/texel-coverage/aggregate_coverage.py \
  --jsonl .local/rg477m-texel-20261112/coverage/modules.jsonl \
  --out .local/rg477m-texel-20261112/coverage

# spirv-val 全量验证
python3 tools/texel-coverage/validate_spirv.py \
  --dump-dir .local/rg477m-texel-20261112/coverage_dump \
  --out .local/rg477m-texel-20261112/coverage/spirv_val.json \
  --validator /opt/homebrew/bin/spirv-val --target-env vulkan1.1 --jobs 8
# -> modules_validated: 9079, passed: 9079, failed: 0

# 宿主契约 + 改写结果的 SPIR-V 有效性（verify_host.py 的 typed-storage 组）
PATH=/opt/homebrew/bin:$PATH python3 scripts/verify_host.py \
  --output .local/rg477m-texel-20261112/host-verification
# -> typed-storage-* 全部 return_code=0；仅 module-load-info 因本 worktree 没有
#    .local/uncompressed/main.bin 而 blocked（与本改动无关）

# 基线复现（用 db5218e 的规则头 + 38957a0 的分析器）
git show db5218e:src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h > $B/src/graphics/nvn_vulkan/engine/
git show 38957a0:tools/texel-coverage/typed_storage_analyzer.cpp > $B/tools/texel-coverage/
# -> covered 1356 / uncovered 1120；spirv-val 5520 -> 4165 通过 / 1355 失败

# 真机腿（设备上线后）
bash scripts/android/verify_rg477m_typed_storage.sh RG477M01445122
# 设备不在线时：BLOCKED_DEVICE，退出码 2（本轮即此状态）
```

## 11. 与任务简报的偏差（如实记录）

1. 简报称 typed-storage 契约「已接入 `scripts/verify_host.py`」——**实测未接入**，本轮补上（§8.4）。
2. 简报要求「追加」`RG477M-TEXEL-20261112-OSS-NOTES.md`——该文件**原本不存在**（前序报告引用了它），
   本轮按新建处理。
3. `AGENTS-HONOR.md` 在主仓根目录不存在（ENOENT），未取得。
4. 简报未提及的两个 SPIR-V 无效性缺陷（capability、重复类型）是本轮离线全量扫描的新发现，
   已按 P1 修复并配契约测试。

## 12. 相关文件

- 工具：`tools/texel-coverage/{typed_storage_analyzer.cpp,scan_xgs_coverage.py,aggregate_coverage.py,validate_spirv.py,shared_image_probe.cpp}`
- 产物（不进 Git，`.local/rg477m-texel-20261112/`）：
  `coverage/modules.jsonl`、`coverage/coverage_stats.json`、`coverage/uncovered_bindings.{json,tsv}`、
  `coverage/module_verdicts.{json,tsv}`、`coverage/spirv_val.json`、`coverage_dump/*.spv`（9079 个改写后模块）
- 出处：`coordination/results/RG477M-TEXEL-20261112-OSS-NOTES.md`
- 前序报告：`coordination/results/RG477M-TEXEL-20261112-RESULT.md`

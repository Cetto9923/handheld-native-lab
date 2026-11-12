# RG477M Texel formatless-write FATAL — 归因与实现计划

任务：RG477M（MT6897 / Mali-G615 MC6）启动约 5 秒、5 次 present 后必 FATAL，进不去游戏。

```
[engine] NVN texel format=98 bufferFeatures=0x78 formatFlags2=1
FATAL: Texel format lacks formatless-write support
```

## 0. 本文件的定位

这是**开工前的归因与计划**（抗中断纪律要求先落盘再改码）。实现与验收结果在文末
「实施记录」追加。社区/上游出处见 `RG477M-TEXEL-20261112-OSS-NOTES.md`。

## 1. 根因（已在设备字节上确认）

### 1.1 FATAL 判定点

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp:3864-3870`
  - `texel_buffer_features(tf)` 读到 `VkFormatProperties3::bufferFeatures`（64 位，`%#llx` 打印，无 32 位截断）。
  - `b.flags&1`（formatless write）缺 `VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT` 即 `xbg_fatalf`。
- `src/graphics/nvn_vulkan/engine/xbg_game.cpp:2700-2716`：`texel_buffer_features()`，缓存 + 打印 `NVN texel format=%u bufferFeatures=%#llx`。
- `src/graphics/nvn_vulkan/engine/xbg_game.cpp:4057-4060`：storage image 路径同款检查。

### 1.2 Adreno vs Mali 横向差异

| 设备 | format 98 (R32_UINT) bufferFeatures |
|---|---|
| Adreno 735（荣耀平板） | `0x180000078` |
| Mali-G615 MC6（RG477M） | `0x78` |

高 32 位差 `0x100000000` = `STORAGE_WRITE_WITHOUT_FORMAT`、`0x80000000` = `READ_WITHOUT_FORMAT`。
**Mali 整族缺这两个 optional 位**；低 32 位相同（`0x78` 含 `STORAGE_TEXEL_BUFFER_BIT(0x10)`，带格式 typed 路径可用）。

### 1.3 触发者

compute shader `664748e31a2dfad9`（stage=5）。从设备 6 GB `shaders.hnla` 归档按字节偏移抽出
（本 worktree `.local/rg477m-texel-20261112/`，SHA-256
`495b8b7f1151bcca639273bc7c7b1b8732e53306fefe7bfcffe2a988a8705ec9`）：

- `664748e31a2dfad9.xgs`：magic `0x31534758`(XGS1) version 1 stage=5 binding_count=3
  - `b=5  type=6(STORAGE_BUFFER) source=1`
  - `b=11 type=5(STORAGE_TEXEL_BUFFER) source=8(XBG_SOURCE_IMAGE) count=1 flags=0x80000501`
    → bit0 = formatless write；bits8..10 = dim 5(Buffer)；bit31 = image shape present
  - `b=17 type=6(STORAGE_BUFFER) source=2`
- SPIR-V（v1.3，`<spirv-val --target-env vulkan1.1` 通过`）：

```spirv
%20 = OpTypeImage %float Buffer 0 0 0 2 Unknown
%_ptr_UniformConstant_20 = OpTypePointer UniformConstant %20
%cp_i_cb2_48_ = OpVariable %_ptr_UniformConstant_20 UniformConstant   ; Binding 11
...
%115 = OpLoad %20 %cp_i_cb2_48_
%116 = OpLoad %int %57
%126 = OpCompositeConstruct %v4float %118 %120 %122 %125
OpImageWrite %115 %116 %126 None
```

即：**formatless storage texel buffer**（`Image Format = Unknown`），单条 `OpImageWrite`，坐标为 int。

## 2. 规范依据（决定性原文）

### 2.1 formatless 写入必须 WITHOUT_FORMAT 位 —— 引擎判定正确

Vulkan spec, *Vulkan Environment for SPIR-V*（"Image Format and Type Matching" 之后）：

> The Image Format of an OpTypeImage declaration must not be Unknown, for variables which are
> used for OpImageRead, OpImageSparseRead, or OpImageWrite operations, except under the following
> conditions: For OpImageWrite, if the image format supports
> VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT and the shader module declares the
> StorageImageWriteWithoutFormat capability.

对应 VUID `vkCmdDispatch-OpTypeImage-07027/07029`（见 vulkano `shader/mod.rs` 的
VUID 汇总：`For a storage image/texel buffer declared with OpTypeImage with an Unknown format:
If it is written to in the shader, the format of the bound image view or buffer view must have
the FormatFeatures::STORAGE_WRITE_WITHOUT_FORMAT format feature.`）。

→ **引擎的 FATAL 判定是正确的，不改判定逻辑去绕过。**

### 2.2 typed 形态的合法性：Sampled Type 必须与格式匹配

Vulkan spec, *Image Format and Type Matching*：

> When specifying the Image Format of an OpTypeImage, the converted bit width and type, as shown
> in the table below, must match the Sampled Type. The signedness must match the signedness of
> any access to the image.

| Image Format | Type-Declaration | Bit Width | Signedness |
|---|---|---|---|
| Unknown | Any | Any | Any |
| R32ui | OpTypeInt 32 | 32 | 0(unsigned) |
| Rgba32ui | OpTypeInt 32 | 32 | 0(unsigned) |

VUID `StandaloneSpirv-Image-04965`：*The SPIR-V Type of the Image Format operand of an
OpTypeImage must match the Sampled Type*。
VUID `vkCmdDispatch-format-07753`：*The numeric type of the format of the bound image view or
buffer view must match the Sampled Type operand of the OpTypeImage.*

→ **不能**把 `Image Format` 直接改成 `R32ui` 而保留 `Sampled Type = float`（违反 04965/07753）。

### 2.3 组件数与地址由「绑定视图的格式」决定，formatless 与 typed 相同

VUID `vkCmdDispatch-OpImageWrite-04469`（vulkano 同源引用）：
*For every OpImageWrite instruction, the type of the Texel operand must have at least as many
components as the format of the bound image view or buffer view.*

VUID `vkCmdDispatch-OpImageWrite-08795/08796`：typed 形态下 SPIR-V 的 Image Format 必须与
绑定 buffer view 的 VkFormat **精确一致**。

→ 写出的**字节数、地址步长、写入分量数**都由绑定的 VkFormat 决定，formatless 与 typed 相同。
这是本次改写「逐字节等价」的基础。

## 3. 关键发现：仓库里已有半成品

`src/graphics/nvn_vulkan/engine/xbg_spirv_stencil.h:231 xbg_spirv_integer_storage()`
（经 `xbg_spirv_ffrs_stencil` → `stencil_rewrite` 调用）已经对
`{"664748e31a2dfad9",11,1,true}` 这条 fact 生效：

1. 把该 binding 的 image type 从 `OpTypeImage %float Buffer … Unknown` 克隆成
   `OpTypeImage %uint Buffer … Unknown`（Sampled Type 改 uint，**Image Format 仍是 Unknown**）；
2. 在 `OpImageWrite` 前插入 `OpBitcast %v4uint %v4float`，把 texel **按位**搬到 uint。

所以 Adreno 参考形态 = **formatless + uint Sampled Type + 位保留的 uvec4 texel**，
descriptor 格式由 `xbg_game.cpp:2689 texel_binding_format()` 给出，日志实测 `98 = R32_UINT`。

**缺口只有一个**：`Image Format` 仍是 `Unknown` → 仍要求 WITHOUT_FORMAT 位 → Mali 仍 FATAL。

## 4. 实现计划

### 4.1 改写规则

把该 storage texel buffer 的 `OpTypeImage` 的 **Image Format 操作数** 从 `Unknown` 改成与
**实际绑定的 VkFormat** 精确对应的 SPIR-V image format：

- 描述符格式 **完全不变**（仍由 `texel_binding_format()` 得出），
- Sampled Type 已是 uint（现有 integer_storage 改写负责），
- texel 已是位保留的 uvec4（现有 integer_storage 改写负责），
- **不新增/删除/修改任何数据流指令**；只改一个类型声明的操作数。

### 4.2 为什么必须按「实际绑定格式」选模块

SPIR-V 模块按 shader 缓存，而绑定格式按 draw 变化。仓库已有同类机制：
`xbg_game.cpp:2746 integer_buffer_variant()`（uniform texel buffer 的每格式变体）
+`IntegerBufferSelection`（在 `draw()`/`dispatch()` 入口切换）。
本次沿用同一模式：为 storage texel buffer 按 (binding, 绑定格式) 生成变体，
`identity` 前缀 `stb1:` 与整数缓冲变体的 `ib1:` 区分。

### 4.3 引擎侧能力检查的处置（不放松检查）

原检查用**包内 binding.flags** 判断「是否 formatless 写入」。改写后模块不再做 formatless 访问，
若仍按 flags 检查就是**假失败**。处置方式：

- 在 shader 载入时记录**模块实际形态**：`typed_storage_formats[binding] = spirv_image_format`，
  以及仍为 formatless 的 binding 集合；
- 描述符写入时按**模块实际形态**检查：
  - 模块仍 formatless → 要求 WITHOUT_FORMAT 位（与今天完全一致）；
  - 模块已 typed → 要求 ①`STORAGE_TEXEL_BUFFER_BIT` ②**SPIR-V 声明格式与绑定 VkFormat 精确一致**
    （新增的、更严的检查）。
- 检查逻辑本身不删、不放宽；只是把「依据」从包内 flag 换成模块事实，并**加严** typed 分支。

### 4.4 作用域

- 新增 `src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h`（纯函数、宿主可测）。
- 改 `src/graphics/nvn_vulkan/engine/xbg_game.cpp`：形态记录、变体生成/选择、描述符检查按形态分支。
- **不碰** `src/runtime/hle/nso_host.c`、事件退休路径（1.0.9 批次另一条崩溃线负责）。
- 不动荣耀平板仓的任何 worktree；不动 RG477M 主 worktree。

## 5. 验收计划（用户已提高底线）

底线 = **能进标题，且能进战斗实际玩**；不接受「只是不 FATAL」。

1. **宿主契约**（红/绿自证，变异体必须红）
   - 自造 fixture 驱动改写：断言 formatless → typed、已经是 typed 的**不被**改动、
     共享 image type 时**拒绝**改写、Sampled Type 与格式不匹配时**拒绝**。
   - 用真实模块 `664748e31a2dfad9` 做 golden：改写后 `spirv-val` 通过、
     image type 变 R32ui、`.x/.y/.z/.w` 数据流逐指令未变。
2. **真机（RG477M01445122，独占）**
   - `adb install -r` 覆盖安装 → 启动 → 无 `FATAL: Texel format lacks formatless-write support`、
     无 `engine.log.fatal`；
   - **进标题**；
   - **进战斗**：进场、出招、召唤兽出场、结算；截取战斗画面；
   - 画面正确性：检查黑块、错色、缺失粒子、几何崩解。
3. **对照**：荣耀平板**未获授权，不碰**。拿不到平板参照就把「画面正确性需平板侧比对」
   明确写成待办，不自己下结论。
4. **覆盖率**：改写命中了哪些 shader / 哪些没命中，附证据。

## 6. 已知阻塞（开工时）

- **设备 RG477M01445122 当前不在线**：`adb devices -l` 只有 `A42BBB5109000333`（荣耀平板，
  禁止触碰）。已执行 `adb reconnect`（未 `kill-server`），RG477M 未出现。
  → 真机验收（进标题 / 进战斗 / 截图）需设备上线后进行。
- 宿主侧实现与契约测试可独立完成。

## 7. 实施记录

### §1 改写规则伪代码与改动清单（先落盘，再改第一行代码）

#### §1.1 已在本机证伪/证实的前置事实

- SPIR-V Image Format 枚举值（用 `spirv-dis` 对同一模块逐值反查得到，非记忆）：
  `Unknown=0, R32f=3, R32i=24, R32ui=33, Rg32f=6, Rg32i=25, Rg32ui=35,
  Rgba32f=1, Rgba32i=21, Rgba32ui=30`。
- VkFormat 数值（NDK `vulkan_core.h`）：`R32_UINT=98, R32_SINT=99, R32_SFLOAT=100,
  R32G32_UINT=101, R32G32_SINT=102, R32G32_SFLOAT=103,
  R32G32B32A32_UINT=107, R32G32B32A32_SINT=108, R32G32B32A32_SFLOAT=109`。
- 对本仓库真实模块 `664748e31a2dfad9` 实测（`spirv-val --target-env vulkan1.1`）：
  1. 现状（`xbg_spirv_ffrs_stencil` 改写后、Sampled Type=uint、Image Format=Unknown）：**val 通过**；
  2. 只把该 `OpTypeImage` 的 Image Format 操作数改成 `R32ui(33)`、**其余指令一字不动**：**val 仍通过**。
  → 证明「只改一个类型操作数」在 SPIR-V 层面成立，且 `v4uint` texel 对 `R32ui` 合法（GLSL imageStore 同形）。
  证据文件：`.local/rg477m-texel-20261112/exp/after_integer.spv`、`.../typed_r32ui.spv`。

#### §1.2 改写规则伪代码（宿主纯函数，新文件 `src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h`）

```text
xbg_spirv_image_format(vk_format) -> spirv_image_format          # 0 = 不支持/保持 Unknown
    98->33(R32ui) 99->24(R32i) 100->3(R32f)
    101->35(Rg32ui) 102->25(Rg32i) 103->6(Rg32f)
    107->30(Rgba32ui) 108->21(Rgba32i) 109->1(Rgba32f)
    其它 -> 0（不支持：保持 formatless，绝不猜）

xbg_spirv_image_format_scalar(spirv_format) -> 0(float)|1(int)|2(uint)
    R32f/Rg32f/Rgba32f -> 0 ; R32i/... -> 1 ; R32ui/... -> 2

xbg_typed_storage_rewrite(words, binding, spirv_format) -> 1 | 0 | -1
    1. 校验 magic==0x07230203、spirv_format 合法且 != Unknown，否则 -1
    2. 找 OpDecorate Binding==binding 且 OpVariable StorageClass==UniformConstant 的变量 -> var
    3. var -> OpTypePointer(UniformConstant) -> OpTypeImage -> image_type
       要求 Dim==Buffer（texel buffer 路径）
    4. 若 image_type 的 Image Format 操作数 != Unknown：
         已是 typed：同 spirv_format -> 返回 0（幂等，字节不变）；不同 -> -1
    5. 要求 image_type 只被这一个变量引用（共享类型会连带改变别的 binding）否则 -1
    6. 要求该 image 上没有 OpImageRead（只有写；改成 typed 会改变读语义）否则 -1
    7. 要求 image_type 的 Sampled Type 的标量类别 == xbg_spirv_image_format_scalar(spirv_format)
       否则 -1（本仓库的 uint 存储图只允许 R32ui）
    8. 只改一个操作数：image_type 的 Image Format = spirv_format
       其它指令、数据流、ID、bound 全部不动
    9. 返回 1

xbg_spirv_declared_image_format(words, binding) -> spirv_image_format
    0 = formatless（Unknown）；其它 = 模块实际声明的格式

xbg_typed_storage_matches(spirv_format, vk_format) -> bool
    spirv_format != 0 && spirv_format == xbg_spirv_image_format(vk_format)   # 精确一致
```

#### §1.3 引擎改动清单（`src/graphics/nvn_vulkan/engine/xbg_game.cpp`）

| # | 位置（函数） | 改动 |
|---|---|---|
| 1 | `struct Shader`（约 2288 行） | 新增 `storage_image_formats`（binding -> 模块实际声明的 SPIR-V image format）、`module_words`（仅对含 storage texel/image binding 的着色器保留）、`typed_variants` |
| 2 | `load_shader`（约 2620 行，`stencil_rewrite` 之后） | 用 `xbg_spirv_declared_image_format` 记录每个 storage texel/image binding 的**模块实际形态**；必要时保留 `module_words` |
| 3 | 新函数 `typed_storage_variant(Shader&)`（紧邻 `integer_buffer_variant`，约 2745 行） | 按 **(binding, 绑定 VkFormat)** 生成模块变体：identity = `key + ":stb1:" + "binding_format,..."`（与整数缓冲变体的 `:ib1:` 区分）；改写用 `xbg_typed_storage_rewrite` |
| 4 | 新函数 `typed_storage_binding_format(Shader&, const xbg_shader_binding&)` | 解析该 binding 当前**实际绑定**的 VkFormat（复用 `texel_binding_format`） |
| 5 | 新结构 `TypedStorageSelection`（RAII，紧邻 `IntegerBufferSelection`，约 2791 行） | 在 `draw()`/`dispatch()` 入口把 `state.shader[stage]` 换成 typed 变体，退出时还原 |
| 6 | `descriptors_build` 的 storage texel 分支（约 3862-3870 行） | **按模块实际形态**判定：<br>· 模块仍 formatless（声明格式==Unknown）→ **完全保留今天的要求**（`STORAGE_WRITE_WITHOUT_FORMAT` / `STORAGE_READ_WITHOUT_FORMAT`）<br>· 模块已 typed → 要求 ①`STORAGE_TEXEL_BUFFER_BIT` ②**声明的 SPIR-V 格式与绑定 VkFormat 精确一致**（更严） |
| 7 | `shader_identity`（约 2570 行）与 `load_module`（约 2950 行） | 识别 `:stb1:` identity，使 warm 预热路径能重建同一 typed 模块（不认识时只是退回运行期创建，不影响正确性） |
| 8 | `dispatch()`（约 4886 行）、`draw()`（约 4348 行） | 各加一个 `TypedStorageSelection` RAII |

### §2 已实现的改动（提交 `d161525`、`bed62cc`）

#### §2.1 文件与函数（行号为 `bed62cc` 时的 `src/graphics/nvn_vulkan/engine/xbg_game.cpp`）

| 文件 | 位置 | 改动 |
|---|---|---|
| `src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h`（新，178 行） | `xbg_spirv_image_format`(53) / `xbg_spirv_image_format_scalar`(69) / `xbg_typed_storage_matches`(80) / `xbg_spirv_binding_image`(95) / `xbg_spirv_declared_image_format`(119) / `xbg_typed_storage_rewrite`(134) | 纯函数、宿主可测、无 Vulkan 头依赖 |
| `xbg_game.cpp` | 21-27 | include 新头 + `static_assert` 把 VkFormat 数值钉在 vulkan_core.h 上 |
| `xbg_game.cpp` | 2311/2314/2315 | `struct Shader` 新增 `storage_image_formats`、`module_words`、`typed_variants` |
| `xbg_game.cpp` | 2700-2706（`load_shader`） | 记录模块实际形态（每个 storage texel/image binding 的 SPIR-V image format），并在需要时保留最终模块字节 |
| `xbg_game.cpp` | 2821（`integer_buffer_variant`） | 把 `storage_image_formats` / `module_words` 传给整数缓冲变体 |
| `xbg_game.cpp` | 2591（`shader_identity`） | identity 增加 `:stb1:` 前缀解析（与 `:ib1:` 区分） |
| `xbg_game.cpp` | 2845 `typed_storage_binding_format` | 解析 storage texel buffer 当前实际绑定的 VkFormat（复用 `texel_binding_format`） |
| `xbg_game.cpp` | 2871 `storage_texel_formatless_write_ok` / 2878 `storage_image_formatless_write_ok` | 设备是否仍支持 formatless 写（格式特性位） |
| `xbg_game.cpp` | 2891 `typed_storage_image_format` | 解析 storage image 当前绑定的 view 格式（与描述符路径同一函数 `im.view_format`） |
| `xbg_game.cpp` | 2917 `typed_storage_variant` | 按 (binding, 绑定格式) 生成模块变体；identity = key+":stb1:"+"binding_format,..."；改写用 `xbg_typed_storage_rewrite`；被拒绝时记录 `NVN typed storage UNSUPPORTED ... reason=rewrite-refused` 并保持原模块 |
| `xbg_game.cpp` | 2970 `TypedStorageSelection` | RAII：在 `draw()`/`dispatch()` 入口换入 typed 变体，退出还原 |
| `xbg_game.cpp` | 4046-4060（`descriptors_build`，storage texel buffer） | 按模块实际形态判定：formatless → 保留今天的 WITHOUT_FORMAT 要求；typed → 要求 `STORAGE_TEXEL_BUFFER_BIT` 且声明格式与绑定 VkFormat 精确一致 |
| `xbg_game.cpp` | 4250-4262（`descriptors_build`，storage image） | 同上（`STORAGE_IMAGE_BIT` 已在原处检查） |
| `xbg_game.cpp` | 4550（`draw`）/5088（`dispatch`） | 各加一个 `TypedStorageSelection` RAII |
| `xbg_game.cpp` warm 路径 `load_module` | — | 识别 `:stb1:` identity 并按同一顺序（stencil → specialize → optimize → typed）重建同一模块 |
| `tests/device/test_typed_storage.cpp` + 3 个 `.spvasm` fixture（新） | — | 宿主契约 |
| `scripts/android/verify_rg477m_typed_storage.sh`（新） | — | 真机安装/启动/取证脚本，只允许 RG477M01445122 |

#### §2.2 逐字节等价论证（写出的字节、地址步长、分量数）

1. 写出的字节与地址由绑定视图决定，与模块是否声明格式无关。
   - VUID `vkCmdDispatch-OpImageWrite-04469`：*For every OpImageWrite instruction, the type of the
     Texel operand must have at least as many components as the format of the bound image view or
     buffer view.* → 分量数下限来自绑定视图的格式。
   - VUID `vkCmdDispatch-OpImageWrite-08795/08796`：typed 形态下 SPIR-V 声明的 Image Format 必须与
     绑定的 buffer/image view 的 VkFormat 精确一致。
   - formatless（Image Format=Unknown）只表示模块不声明格式，视图本身仍然是 R32_UINT 的视图，
     所以每 texel 仍是 1 个 32 位分量、4 字节，地址步长仍是 4 字节。
   → 两种形态写入的字节数、地址步长、被写入的分量（.x）完全相同。
2. 本次的具体绑定：设备日志 `NVN texel format=98`（`VK_FORMAT_R32_UINT`，1 分量 × 4 字节）。
   - formatless 形态：写 .x 分量的 4 字节。
   - typed 形态（R32ui）：R32ui 即 R32_UINT 的 SPIR-V 表达（1 分量无符号 32 位），仍写 .x 分量的 4 字节。
   → 逐字节相同，无精度变化、无分量丢失。
3. 数据流没有被本次改动触碰：模块里 `OpImageWrite` 的 texel 早已由既有的
   `xbg_spirv_integer_storage` 改写为位保留的 `OpBitcast %v4uint %v4float`（本次未改）。
   宿主契约 `assert_only_format_changed` 逐字比较证明：改写前后只有 `OpTypeImage` 的
   Image Format 一个操作数不同，指令、ID、数据流、bound 全部不变。
4. SPIR-V 合法性：R32ui 要求 Sampled Type = uint（VUID StandaloneSpirv-Image-04965），改写后
   模块的 Sampled Type 正是 uint（既有 integer storage 改写负责）；texel 为 4 分量 uint 向量
   ≥ R32ui 的 1 分量（04469）。实测 `spirv-val --target-env vulkan1.1` 通过。
5. 能力检查没有放松：formatless 分支一字未改；typed 分支更严（额外要求声明格式与绑定
   VkFormat 精确一致，不一致即 FATAL）。变体只在设备确实缺 formatless 特性位时生成，
   支持 formatless 的设备行为完全不变。

#### §2.3 宿主契约（命令与退出码）

```sh
cd <worktree>
D=.local/rg477m-texel-20261112/exp
spirv-as --target-env vulkan1.1 tests/device/typed_storage_uint.spvasm -o $D/typed_storage_uint.spv
spirv-as --target-env vulkan1.1 tests/device/typed_storage_shared_image.spvasm -o $D/typed_storage_shared_image.spv
spirv-as --target-env vulkan1.1 tests/device/typed_storage_formatless_read.spvasm -o $D/typed_storage_formatless_read.spv
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -o $D/test_typed_storage tests/device/test_typed_storage.cpp
$D/test_typed_storage $D/typed_storage_uint.spv $D/typed_storage_shared_image.spv $D/typed_storage_formatless_read.spv $D/typed_storage_out.spv .local/rg477m-texel-20261112/extracted.spv $D/real_typed.spv
spirv-val --target-env vulkan1.1 $D/typed_storage_out.spv
spirv-val --target-env vulkan1.1 $D/real_typed.spv
```

结果：`test_typed_storage` 退出码 0（ASan/UBSan 无告警），输出
`Typed storage texel buffer: formatless to R32ui, idempotent, shared image and formatless read refused, bound format mismatch red PASS`；
两次 `spirv-val` 退出码 0。

覆盖的断言（绿）：Unknown → R32ui 且只有 Image Format 一个操作数变化；已是 typed 的模块
不被重复改写（返回 0，字节不变）；同名不同格式的 typed 模块被拒；共享 image type 被拒；
同一 image 上存在 formatless read 被拒；Sampled Type 与格式标量类别不符被拒；未知 binding 被拒；
Unknown 格式被拒；畸形模块被拒且字节不变。
覆盖的断言（红，变异体必须红）：把已改写模块的声明格式改成 R32i（绑定仍是 R32_UINT）后，
`xbg_typed_storage_matches(24,98)` 为 false → 引擎的 `need(...)` 必然失败。

真实模块金样（`.local/rg477m-texel-20261112/extracted.spv`，游戏派生内容，不入库）：
先跑既有 `xbg_spirv_ffrs_stencil`（返回 1），再跑 typed 改写（返回 1），
`spirv-dis` 显示 `%127 = OpTypeImage %uint Buffer 0 0 0 2 R32ui`，`spirv-val` 通过。

#### §2.4 NDK 编译

- `scripts/android/build_game_window_apk.sh`：全步骤退出码均为 0
  （cmake-configure / cmake-build-game-window / android-elf-header / android-compile-native-window /
  host-compile-native-window / javac / jar / d8 / aapt2-compile / aapt2-link / payload-zip /
  zipalign / apksigner-sign / apksigner-verify / keytool / strip / sha256）。
- `xbg_game.cpp` 用 NDK clang `-Wall -Wextra` 单独编译：0 error；warning 集合与
  `HEAD` 基线逐条一致（diff 无差异；基线 192 条 `-Wmissing-field-initializers` 等，
  新代码用 `{}`+显式 sType 初始化，不新增 warning）。
- 产物：`.local/android-apk/ffrs-game-window-debug.apk`（不入库）。
  最终 APK SHA-256：`f2891eb202b49272cbbb1f27e924ae35b2af8c226bdf2b98e2fe82662f6490d1`（第三次构建，含 `c9f087e`）。

#### §2.5 真机（BLOCKED：设备不在线）

- 计划阶段已记录：`RG477M01445122` 当时不在线；本轮复查仍然只有荣耀平板
  `A42BBB5109000333`（禁止触碰，未做任何操作；`adb reconnect` 只重连了平板，未 `adb kill-server`）。
- 因此 进标题 / 进战斗 / 截图 尚未执行，不能声称真机通过。
- 设备上线后一条命令即可取证：
```sh
bash scripts/android/verify_rg477m_typed_storage.sh RG477M01445122
```
  脚本会：只对 `RG477M01445122` 做 `adb install -r` → force-stop → 清 `engine.log.fatal` →
  启动 → 等待 → 拉 `engine.log` / `engine.log.fatal` / logcat / 截屏，并打印
  `FATAL_ABSENT`、`FORMATLESS_WRITE_FATAL_ABSENT`、typed storage 变体日志行。

#### §2.6 覆盖率：命中与仍未覆盖的 shader（附证据）

**已命中并有真实证据**：§664748e31a2dfad9`（compute，binding 11，STORAGE_TEXEL_BUFFER，
绑定格式 `VK_FORMAT_R32_UINT(98)` → 声明 `R32ui(33)`）。证据：`.local/rg477m-texel-20261112/extracted.spv`
（设备 6GB `shaders.hnla` 按字节偏移抽出的真实模块，游戏派生内容，不入库）经既有
`xbg_spirv_ffrs_stencil` + 本次 `xbg_typed_storage_rewrite` 后，
`spirv-dis` 显示 `%127 = OpTypeImage %uint Buffer 0 0 0 2 R32ui`，`spirv-val --target-env vulkan1.1` 通过。

**仍未覆盖（每条都有明确证据或可执行判据）**：

1. **其它 formatless 的 storage texel buffer / storage image shader**：宿主**无法枚举**，
   因为着色器归档（设备上的 6GB `shaders.hnla`）不在仓库里（`.local/` 只有本任务抽出的 1 个模块）。
   运行期判据（设备一跑就有）：每次成功生成变体都有
   `NVN typed storage variant shader=<key> identity=<key>:stb1:<binding>_<format>`；
   每次被拒都有 `NVN typed storage UNSUPPORTED shader=<key> ... reason=rewrite-refused unchanged=1`
   或 `reason=identity-size`。**跑完一次真机即可统计命中/未命中的全集**。
2. **formatless 读**（`binding.flags&2`，同一 image 上有 `OpImageRead`）：改写**主动拒绝**
   （改成 typed 会改变读语义），仍走原 formatless 检查 → 设备若缺
   `STORAGE_READ_WITHOUT_FORMAT` 位仍会 FATAL。拒绝路径由
   `tests/device/typed_storage_formatless_read.spvasm` 在宿主侧覆盖。
3. **count>1 的数组绑定**：本次不生成变体（数组元素可能格式不同，无法共用一个声明格式），
   仍走原检查。
4. **SPIR-V 无法表达的绑定格式**（如 `R32G32B32_UINT` 没有对应 image format、`R16` 系列）：
   不生成变体，仍走原 formatless 检查。格式表见 `xbg_spirv_image_format`。
5. **image type 被多个变量共享**、或同 image 上有 formatless 读：拒绝改写（宿主契约覆盖），
   仍走原检查。

**未改动的路径**（保持原样，不是遗漏）：`xbg_game.cpp:4057` 附近的 storage image formatless 检查
已改成按模块形态分支，但 formatless 分支的**要求一字未改**；`honor_inline` 路径、事件退休路径、
`src/runtime/hle/nso_host.c` 均未触碰。

#### §2.7 风险

1. **真机未验证**：设备 `RG477M01445122` 全程不在线，进标题/进战斗/画面正确性**尚未取得证据**，
   不能声称通过。```2.5``` 的脚本是上机后的取证入口。
2. **变体数量**：同一 shader 若在不同 draw 上以不同格式绑定，会生成多个模块/管线（冷启动成本，
   之后走缓存）。这与既有的整数缓冲变体机制同构。
3. **warm 预热列表**：`:stb1:` identity 会写进设备上的 `vk-pipeline-list.bin`；
   `load_module` 已能按同一顺序重建同一模块（stencil → specialize → optimize → typed），
   重建失败时只是退回运行期创建，不影响正确性。
4. **支持 formatless 的设备行为不变**：变体只在设备**确实缺** formatless 特性位时生成，
   所以 Adreno 等设备的路径完全没变（本次改动的仓库是 RG477M 专用，荣耀平板仓未动）。

#### §1.4 硬约束自检

- 不降精度、不跳 draw、不全局关 depth、不用占位 module：改写只改一个类型操作数，**不新增/删除/修改任何数据流指令**。
- 不改游戏逻辑时间/动画/物理/音频/输入/过场速度：改写不触碰任何 CPU 侧逻辑。
- 能力检查不放松：formatless 分支一字不改；typed 分支比 formatless 分支**更严**（额外要求格式精确一致）。
- 变体只在**设备确实缺 formatless 位**时生成（缺位才需要 typed），支持 formatless 的设备行为完全不变。

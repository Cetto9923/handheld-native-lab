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

（待补）

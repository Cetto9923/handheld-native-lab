# 工单 P-MM-21｜worker｜NVN sampler builder 族（25 名）+ texture builder 族（44 名）

> **状态**：正式派工（总控：guest 已越过 `nvnCommandBufferInitialize`，新的第一致命是 `nvnSamplerBuilderSetDefaults`）
> 基线：`work/rg477m-native` @ **`064634f`**（工作区不干净：命令缓冲族与 shader 身份工具尚未提交，**不要动它们**）
> 真机证据：`.local/logs/p-mm-22-guest-transcript.log`
> ```
> [nvn] nvnCommandBufferInitialize: buffer=0x51b9a63638; the commands of this buffer are recorded on the host
> [nvn] nvnMemoryPoolInitialize: pool=0x5219a6dd00 builder=0x5a39e80450 device=0x5639a03810 storage=0x5a5ea00000 size=524288 flags=0xe2 -> true
> [nvn] unimplemented NVN entry "nvnSamplerBuilderSetDefaults" was called (trap slot 116, caller lr=0x7214f08b04)
> [nvn]   args x0=0x5a39e80630 x1=0x5a39e80430 x2=0x60 x3=0x10 x4=0x2000074aeff08e0 x5=0xb40000744ef74090 x6=0xa x7=0x7f7f7f7f7f7f7f7f
> ```
> 前置：P-MM-17～19 已交付 memory/sampler/texture **pool** 族与 render/blend state 族、program 族；命令缓冲族已接入并越过。帧数仍为 0；未达标题/可玩。

---

## 0. 仓库与环境（必须）

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- 设备：`adb -s RG477M01445122`；不 root / 不刷机 / 不清数据；**不 push**
- 先读：`AGENTS.md` → `coordination/HANDOFF.md` 顶部 → 本工单 → **`src/graphics/nvn_device/nvn_memory_pool.{h,c}`（builder 形态模板，本族照它写）** → `src/graphics/nvn_device/nvn_sampler_pool.c` / `nvn_texture_pool.c`（同族下游） → `src/graphics/nvn/include/nvn/nvn.h`（签名来源）
- `.local/romfs/**` 只读；`task/T04-nvn-firstframe` / `.claude/worktrees/*` 勿改
- **不要调用 agy**；**不要 push**；不要 `git add .`
- **真机 boot / 装包前 `touch .local/agent-pm/BOOT_LOCK_MM_A`，结束后删掉**（这是协作标记，先确认没有别的 owner 在用设备）
- 验收不降级；**禁止声称标题/可玩/已出游戏帧**

---

## 1. 写入范围（只有这三个文件，别的一律不碰）

| 文件 | 动作 |
|---|---|
| `src/graphics/nvn_device/nvn_sampler_builder.h` | 新建 |
| `src/graphics/nvn_device/nvn_sampler_builder.c` | 新建 |
| `src/graphics/nvn_device/nvn_texture_builder.h` | 新建 |
| `src/graphics/nvn_device/nvn_texture_builder.c` | 新建 |

**禁止修改**：`src/graphics/nvn_device/nvn_device.c`（dispatch）、`src/android/native/CMakeLists.txt`、`tests/native/guest_process_contract.c`（契约）、`scripts/runtime/build_guest_process_contract.sh`、`scripts/runtime/selfproof_*`、`coordination/**`、`src/graphics/nvn_device/nvn_command_buffer.*`、`src/graphics/nvn_device/nvn_program.*`、`scripts/graphics/**`。
这些由总控接线。你只交上面四个文件 + 一份**纯文本**报告（写在你的最终回答里，不要写成仓库文件）。

---

## 2. 本族 69 个名字（只做这些）

**sampler builder 25 名**
```
nvnSamplerBuilderSetDefaults        nvnSamplerBuilderSetDevice
nvnSamplerBuilderSetCompare         nvnSamplerBuilderSetMinMagFilter
nvnSamplerBuilderSetReductionFilter nvnSamplerBuilderSetWrapMode
nvnSamplerBuilderSetLodBias         nvnSamplerBuilderSetLodClamp
nvnSamplerBuilderSetLodSnap         nvnSamplerBuilderSetMaxAnisotropy
nvnSamplerBuilderSetBorderColor     nvnSamplerBuilderSetBorderColori
nvnSamplerBuilderSetBorderColorui
nvnSamplerBuilderGetDevice          nvnSamplerBuilderGetCompare
nvnSamplerBuilderGetMinMagFilter    nvnSamplerBuilderGetReductionFilter
nvnSamplerBuilderGetWrapMode        nvnSamplerBuilderGetLodBias
nvnSamplerBuilderGetLodClamp        nvnSamplerBuilderGetLodSnap
nvnSamplerBuilderGetMaxAnisotropy   nvnSamplerBuilderGetBorderColor
nvnSamplerBuilderGetBorderColori    nvnSamplerBuilderGetBorderColorui
```

**texture builder 44 名**
```
nvnTextureBuilderSetDefaults        nvnTextureBuilderSetDevice
nvnTextureBuilderSetFormat          nvnTextureBuilderSetTarget
nvnTextureBuilderSetFlags           nvnTextureBuilderSetWidth
nvnTextureBuilderSetHeight          nvnTextureBuilderSetDepth
nvnTextureBuilderSetLevels          nvnTextureBuilderSetSamples
nvnTextureBuilderSetStride          nvnTextureBuilderSetStorage
nvnTextureBuilderSetSize1D          nvnTextureBuilderSetSize2D
nvnTextureBuilderSetSize3D          nvnTextureBuilderSetSwizzle
nvnTextureBuilderSetDepthStencilMode
nvnTextureBuilderSetGLTextureName
nvnTextureBuilderSetPackagedTextureData
nvnTextureBuilderSetPackagedTextureLayout
nvnTextureBuilderGetDevice          nvnTextureBuilderGetFormat
nvnTextureBuilderGetTarget          nvnTextureBuilderGetFlags
nvnTextureBuilderGetWidth           nvnTextureBuilderGetHeight
nvnTextureBuilderGetDepth           nvnTextureBuilderGetLevels
nvnTextureBuilderGetSamples         nvnTextureBuilderGetStride
nvnTextureBuilderGetSwizzle         nvnTextureBuilderGetDepthStencilMode
nvnTextureBuilderGetGLTextureName
nvnTextureBuilderGetMemoryPool      nvnTextureBuilderGetMemoryOffset
nvnTextureBuilderGetStorageSize     nvnTextureBuilderGetStorageAlignment
nvnTextureBuilderGetStorageClass    nvnTextureBuilderGetRawStorageClass
nvnTextureBuilderGetViewOffset      nvnTextureBuilderGetSparseTileLayout
nvnTextureBuilderGetZCullStorageSize
nvnTextureBuilderGetPackagedTextureData
nvnTextureBuilderGetPackagedTextureLayout
```

名字清单以 `evidence/nvn/main_nvn_ids.txt` 为准（grep `^nvnSamplerBuilder` / `^nvnTextureBuilder`）。

---

## 3. 签名来源，以及一个必须说清楚的风险

签名从 `src/graphics/nvn/include/nvn/nvn.h` 取（`PFNNVNSAMPLERBUILDER*` / `PFNNVNTEXTUREBUILDER*`，该头约 64 条 typedef，可能缺 5 条）。

**这个头是别的游戏的重建件**：它的注释写明「NVN types reconstructed from Darksiders II DWARF information … used for NVN 53.105」。本游戏是 Unreal Engine 5.6.1，NVN 版本更高。所以：

- 头里的签名**对本游戏没有被证实**。不要把它当既定事实。
- 凡是头里缺失的名字，按 `nvn_command_buffer.generated.c` 里那 16 个的做法处理：**记录原始实参，不猜签名，不解引用**，并在报告里点名。
- 如果头里的签名与 guest 调用点明显矛盾（例如参数个数对不上），**以 guest 为准**，并在报告里写明矛盾点。

---

## 4. 实现方向（照 memory pool builder 的形态，最小可证伪）

### 4.1 形态

- **状态全在宿主**，按 guest 的 builder 地址建键；**绝不写入 guest 对象内存**（SDK 布局未知）。用 0xAA 哨兵证明。
- 形态模板：`src/graphics/nvn_device/nvn_memory_pool.c`（builder 表 + defaults + 往返 getter/setter）。
- 导出给总控接线的最小接口（名字照现有族风格）：
  - `nvn_sampler_builder_family_count()` / `nvn_sampler_builder_family_entry_at(i)`（结构体字段 `name` / `fn`）
  - 同上 `nvn_texture_builder_*`
  - 每族一组 `*_get_stats(struct*)`，字段至少：`calls`、`set_defaults`、`live_builders`、`refused`。
- 入口点函数名必须**与 guest 名字逐字相同**（`nvnSamplerBuilderSetDefaults` 等），因为 dispatch 是按名字查表的。

### 4.2 语义

1. **SetDefaults**：把 builder 的字段恢复成默认值，**并且必须真的重置**（现有 render state 族踩过这个坑：`apply_defaults` 整体 `memset` 把占用标志也清了，导致「defaults 真的重置」是靠错误原因通过的）。
2. **Set/Get 往返**：每个 setter 写进去的值，getter 必须原样读回。`Get*` 的出参顺序与 `Set*` 的入参顺序一致。
3. **SetDevice**：记录 device 指针；`GetDevice` 读回。
4. **纹理 builder 的 storage**：记录 pool 指针 + offset + size + alignment，**不要**在 builder 阶段分配 GPU 内存——那是 `nvnTexturePool`/纹理创建阶段的事。`GetStorageSize` 读回记录值。
5. **枚举值**取 vendored 头里的定义（wrap mode、min/mag filter、reduction filter、compare func、target、format、swizzle、storage class 等），不要自造数字。
6. **默认值**同样取 vendored 头或参考树（`.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h` 的 `FITS` 校验过的布局；注意那是**同一款游戏**的参考实现，MIT，见 `.local/refs/nxnative/dm/games/0100606026F12000/`）。取值来源必须在代码注释里写清楚是哪一个。
7. **不猜的**：头里没有签名的名字，走「读原始实参、不解引用」的记录器，并计数（放进 stats 的 `raw_calls`）。
8. **不实现的**：如果某个名字的语义在本轮无法从证据确定（例如 `SetPackagedTextureData` 的数据布局），**点名拒绝 + 打日志说明**，不要伪造成功值。

### 4.3 日志

```
[nvn] nvnSamplerBuilderSetDefaults: builder=%p
[nvn] nvnTextureBuilderSetFormat: builder=%p format=0x%x
```
失败必须说明原因。不要每条都刷屏——只在**首次**和**异常**时打印。

---

## 5. 你自己要做���自测（必做）

**你只交源码，但必须自证**，方式照 `scripts/runtime/make_nvn_render_state_family.py` + `scripts/runtime/selfproof_nvn_render_state.sh` 的路子（**读它们，不要改它们**）：

- 写一个**自带的**小 harness（放 `.local/` 下，不要进 git），编译你这两个 `.c`（需要 stub 掉 `nvn_device.h` 依赖），验证：
  - 每个名字都能 resolve 到自己的函数；
  - Set→Get 往返；
  - `SetDefaults` 之后字段真的回到默认值（**故意把 defaults 改成 no-op，harness 必须变红**）；
  - 0xAA 哨兵：builder 对象内存没被写；
  - **三变体**：A 改坏期望→红；B 去末门 + 硬编码 0→假绿；C 契约不改、只把桩改成撒谎→红。
- 把 harness 的源码与**运行输出**贴进你的最终报告。

**不要**改 `tests/native/guest_process_contract.c` 和 `scripts/runtime/build_guest_process_contract.sh`——总控来写契约与三变体脚本。

---

## 6. 验收

- 四个文件能编译通过（`clang -std=c11 -Wall -Wextra -Werror -I src/graphics/nvn_device`），无警告。
- 自测 harness 三变体：红 / 假绿 / 红。
- 报告里写清楚：69 个名字里，**几个有头文件签名、几个没有**；**哪些默认值来自 vendored 头、哪些来自 nxnative 参考**；**哪些是点名拒绝的**。

---

## 7. 收尾

1. **不要 commit**（总控统一提交）。把四个文件留在工作区即可。
2. 报告写在最终回答里（纯文本，≤ 60 行）：文件清单、自测结果、签名来源统计、默认值来源、拒绝项清单、你对本族是否够用的判断。
3. 不装包、不跑真机——真机由总控统一跑。

---

## 8. 禁止

- 空转「找头文件 / 找 NSO」（都在仓库里）
- 猜 ABI、万能成功桩
- 写入 guest builder 对象内存
- 夹带 command buffer / program / shader / Vulkan 回放
- 声称标题/可玩/游戏帧
- push / force / 动 main / 调 agy / 删 romfs / 改 `nvn_device.c` / 改契约 / 改 CMake
- `git add .` / `git commit` / `git reset` / `git clean` / `git stash`

做完停下汇报。

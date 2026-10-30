# T-TEXTURE-object — NVN 纹理对象族 + 纹理 view 族 + 三个 device 句柄函数（48 名）

> 工单：P-MM-26（`coordination/prompts/P-MM-26-texture-object-family.md`）
> 分支：`task/P-MM-26-texture-object`
> 基线：`work/rg477m-native` @ `c4a496c`（含 P-MM-25 sampler 对象族、池编号 base）
> 日期：2026-10-25
> 宿主：macOS 14 / darwin arm64（`Apple clang 17.0.2`），NDK r26b（26.1.10909125）

---

## 0. 一句话

交付 48 个名字：31 个纹理对象族 + 14 个纹理 view 族 + 3 个 device 句柄函数。状态全在宿主，
按 guest 地址建键，**没有写 guest 对象内存一个字节**（契约用 0xAA 哨兵证明）。纹理对象是
builder 在 `nvnTextureInitialize` 时刻的一次快照。三变体自证 A 红 / B 假绿 / C 红，脚本自己判
四个退出码。NDK `aarch64-linux-android34` + `-Wall -Wextra -Werror` 交叉编译通过。

**没有**证明帧、像素、draw 消费、真机行为 —— 见第 6 节。

---

## 1. 交付物

| 文件 | 作用 |
|---|---|
| `src/graphics/nvn_device/nvn_texture.h` | 纹理族 + device 句柄族的表与统计 |
| `src/graphics/nvn_device/nvn_texture.c` | 31 个纹理名字 + 3 个 device 句柄函数的实现 |
| `src/graphics/nvn_device/nvn_texture_view.h` | view 族的表与统计 |
| `src/graphics/nvn_device/nvn_texture_view.c` | 14 个 view 名字的实现 |
| `tests/native/nvn_texture_contract.c` | 纹理族契约（开关 `--nvn-texture`，报 `nvn_texture_errors`） |
| `tests/native/nvn_texture_view_contract.c` | view 族契约（开关 `--nvn-texture-view`，报 `nvn_texture_view_errors`） |
| `scripts/runtime/make_nvn_texture_variants.py` | 三个变体 + 接线后的 `nvn_device.c` 副本 |
| `scripts/runtime/make_nvn_texture_view_variants.py` | 同上（view） |
| `scripts/runtime/selfproof_nvn_texture.sh` | 纹理族自证脚本 |
| `scripts/runtime/selfproof_nvn_texture_view.sh` | view 族自证脚本 |
| `coordination/reports/T-TEXTURE-object-2026-10-25.md` | 本报告 |

**没有改动**工单 5.2 里的任何文件，也没有改动 `nvn_texture_builder.{h,c}`、`nvn_memory_pool.{h,c}`
（理由见第 4 节）。

---

## 2. 名字清单与逐名来源

48 个名字全部来自 guest 自己的解析记录（`.local/logs/p-mm-29-guest-transcript.log`，设备
run 2026-10-25 的 trap slot 0–71 段）。guest 解析到的纹理 + view 名字实测 **45** 个
（31 + 14），与工单 §2.1/§2.2 完全一致；三个 device 句柄函数在同一个 transcript 的
slot 0 / 2 / 3。

**vendored 头** = `src/graphics/nvn/include/nvn/nvn.h`。这是**别的游戏（Darksiders II）的
NVN 53.105 重建件**，签名**未对本作证实**。
**参考树** = `.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`（同款游戏的参考宿主，MIT）。

| name | vendored 头 | 参考树 |
|---|---|---|
| `nvnTextureInitialize` | nvn.h:1320 | hle_nvn.c:437 |
| `nvnTextureFinalize` | nvn.h:1322 | hle_nvn.c:465 |
| `nvnTextureSetDebugLabel` | nvn.h:1323 | hle_nvn.c:1964 |
| `nvnTextureGetDebugID` | nvn.h:1360 | **参考树没有** |
| `nvnTextureGetFlags` | nvn.h:1326 | hle_nvn.c:466 |
| `nvnTextureGetTarget` | nvn.h:1327 | hle_nvn.c:1963 |
| `nvnTextureGetWidth` | nvn.h:1328 | hle_nvn.c:467 |
| `nvnTextureGetHeight` | nvn.h:1329 | hle_nvn.c:468 |
| `nvnTextureGetDepth` | nvn.h:1330 | hle_nvn.c:469 |
| `nvnTextureGetLevels` | nvn.h:1331 | hle_nvn.c:473 |
| `nvnTextureGetSamples` | nvn.h:1333 | hle_nvn.c:474 |
| `nvnTextureGetFormat` | nvn.h:1332 | hle_nvn.c:1962 |
| `nvnTextureGetStride` | nvn.h:1338 | hle_nvn.c:471 |
| `nvnTextureGetMemoryPool` | nvn.h:1356 | hle_nvn.c:475 |
| `nvnTextureGetMemoryOffset` | nvn.h:1357 | hle_nvn.c:476 |
| `nvnTextureGetTextureAddress` | nvn.h:1339 | hle_nvn.c:478 |
| `nvnTextureGetViewOffset` | nvn.h:1325 | hle_nvn.c:507 |
| `nvnTextureGetStorageClass` | nvn.h:1324 | hle_nvn.c:1958 |
| `nvnTextureGetRawStorageClass` | **头里没有签名** | hle_nvn.c:497 |
| `nvnTextureGetStorageSize` | nvn.h:1358 | hle_nvn.c:470 |
| `nvnTextureGetZCullStorageSize` | nvn.h:1321 | hle_nvn.c:489 |
| `nvnTextureGetSwizzle` | nvn.h:1334 | hle_nvn.c:1954 |
| `nvnTextureGetDepthStencilMode` | nvn.h:1337 | hle_nvn.c:2039 |
| `nvnTextureGetSparseTileLayout` | nvn.h:1340 | **参考树没有** |
| `nvnTextureCompare` | nvn.h:1359 | hle_nvn.c:492 |
| `nvnTextureReadTexels` | nvn.h:1347 | hle_nvn.c:523 |
| `nvnTextureReadTexelsStrided` | nvn.h:1349 | hle_nvn.c:526 |
| `nvnTextureWriteTexels` | nvn.h:1342 | hle_nvn.c:2320 |
| `nvnTextureWriteTexelsStrided` | nvn.h:1344 | hle_nvn.c:2322 |
| `nvnTextureFlushTexels` | nvn.h:1352 | hle_nvn.c:2342 |
| `nvnTextureInvalidateTexels` | nvn.h:1354 | hle_nvn.c:529 |
| `nvnTextureViewSetDefaults` | nvn.h:1300 | hle_nvn.c:499 |
| `nvnTextureViewSetLevels` | nvn.h:1301 | hle_nvn.c:500 |
| `nvnTextureViewSetLayers` | nvn.h:1302 | hle_nvn.c:501 |
| `nvnTextureViewSetFormat` | nvn.h:1303 | hle_nvn.c:502 |
| `nvnTextureViewSetTarget` | nvn.h:1309 | hle_nvn.c:503 |
| `nvnTextureViewSetSwizzle` | nvn.h:1304 | hle_nvn.c:1968 |
| `nvnTextureViewSetDepthStencilMode` | nvn.h:1307 | hle_nvn.c:1967 |
| `nvnTextureViewGetLevels` | nvn.h:1310 | hle_nvn.c:1965 |
| `nvnTextureViewGetLayers` | nvn.h:1311 | hle_nvn.c:504 |
| `nvnTextureViewGetFormat` | nvn.h:1312 | hle_nvn.c:2032 |
| `nvnTextureViewGetTarget` | nvn.h:1318 | hle_nvn.c:1966 |
| `nvnTextureViewGetSwizzle` | nvn.h:1313 | hle_nvn.c:2034 |
| `nvnTextureViewGetDepthStencilMode` | nvn.h:1316 | hle_nvn.c:2033 |
| `nvnTextureViewCompare` | nvn.h:1319 | **参考树没有** |
| `nvnDeviceGetTextureHandle` | nvn.h:1130 | hle_nvn.c:2059 |
| `nvnDeviceGetImageHandle` | nvn.h:1132 | hle_nvn.c:181 |
| `nvnDeviceGetSeparateTextureHandle` | nvn.h:1151 | hle_nvn.c:183 |

### 2.1 参考树没有的三个名字（点名）

工单 §3.2 点名的名单比实际多。**实测**参考树 `hle_nvn.c` 里**只有三个**名字不存在：

1. **`nvnTextureGetDebugID`** —— 参考树没有实现。vendored 头有签名（`uint64_t`）。
   本实现用本项目自己的 device debug domain id 生成器
   （`nvn_device_generate_debug_domain_id`），和 sampler 对象族同一来源。
   **不声称与参考一致。**
2. **`nvnTextureGetSparseTileLayout`** —— 参考树没有实现。vendored 头有签名
   （`void(const NVNtexture*, NVNtextureSparseTileLayout*)`），但那个结构在头里是
   `char dummy[1]` 的不透明块。实现**不填**调用方的结构，只在 stderr 说明。
   **不声称与参考一致。**
3. **`nvnTextureViewCompare`** —— 参考树没有实现。vendored 头有签名
   （`NVNboolean(const NVNtextureView*, const NVNtextureView*)`）。本实现的语义是**本运行时
   自己的**：两个 view 携带同样的描述（同样的字段被设置 + 同样的值）即为同一个 view，
   形状与纹理族的比较一致（参考树的纹理比较是整个描述 + 存储 + swizzle，hle_nvn.c:492）。
   **不声称与参考一致。**

### 2.2 vendored 头里没有签名的一个名字

- **`nvnTextureGetRawStorageClass`** —— vendored 头**没有**这个 PFN typedef。
  参考树有，用的是 `NVN_NOPROTO` 形式：`int nvnTextureGetRawStorageClass(const NVNtexture*)`，
  **返回 1**，并在原处注明"Meaning unverified; return 1 for now"，且注明调用点（如
  `main 0x174899c`）把返回值和 1 比较。本实现采用**参考树的签名和值**（1），因为参考树是
  真正跑这款游戏的宿主；纹理不存在时按本项目的拒绝约定返回 0 并记日志。

### 2.3 三处与 vendored 头不一致、按参考树实现的地方（点名）

| 名字 | vendored 头 | 参考树 | 本实现 | 理由 |
|---|---|---|---|---|
| `nvnTextureGetMemoryPool` | `nvn.h:1356` 返回 `NVNmemoryPool`（结构体，0x100 字节，**按值**） | `hle_nvn.c:475` 返回 `NVNmemoryPool *`（**指针**） | 返回**指针** | 头里的 `NVNmemoryPool` 是 256 字节不透明结构，按值返回与 guest 调用点期待的 ABI 不符；参考树和真实 NVN SDK 都是指针。本项目 builder 族 `nvnTextureBuilderGetMemoryPool` 也返回指针。 |
| `nvnTextureGetRawStorageClass` | 头里没有签名 | `NVN_NOPROTO`，返回 1 | 按参考树 | 见 2.2 |
| `nvnTextureViewGet*` / `Set*` | `NVNboolean`（1 字节）返回 | 同样的 `NVNboolean` | `NVNboolean` | 一致 |

---

## 3. 语义来源（本次实测引用）

- **纹理对象的字段**：参考树的 `tex_builder_t`（hle_nvn.c:437 复制的内容）：target、format、
  flags、三维尺寸、levels、samples、stride、memory pool 与池内偏移、swizzle、depth stencil mode。
- **默认值**：参考树 hle_nvn.c:280 —— target 2D，width/height/depth/levels = 1，swizzle 为
  恒等 R G B A（hle_nvn.c:490），depth stencil mode = DEPTH。
- **比较**：`nvnTextureCompare`（hle_nvn.c:492）比较整个描述 + 存储 + swizzle。本实现的
  "存储" 用 **(memory_pool, memory_offset) 这一对**代替参考树比较的已解析内存指针
  `x->mem == y->mem`，因为本运行时里纹理的存储就是这一对。debug label 与 debug id
  **不**参与比较。
- **ZCull 保存区**：参考树 hle_nvn.c:489 的公式 —— 每 32×32 tile 每层 8 字节，向上取整到
  256 字节，无 tile 时 256 字节。**参考树在同一处说明硬件公式未知**，本运行时用同一公式，
  **不声称它是硬件公式**。契约里 256×128×4 的纹理算出 **1024** 字节（8×4×4 tile × 8 B = 1024 B）。
- **view 的字段与设置方式**：参考树 hle_nvn.c:499–507、1965–1968、2032–2034 —— 每个字段带一个
  "guest 是否设置过"的标志；getter **同时**返回该标志。view 的默认值是整块清零
  （hle_nvn.c:499）。没设过 swizzle 的 view，swizzle 是恒等 R G B A（hle_nvn.c:2034）。
- **三个 handle**：hle_nvn.c:181 / 183 / 2059。handle **不是指针**，是池内编号：
  `tex | samp << 20`。**池编号 base 不在 handle 里**，base 是在读取 handle 时才加的
  （参考树的 `dpool_base`），本实现因此**没有**加 base。

---

## 4. 状态放在宿主（铁律）与两个设计决定

### 4.1 状态与快照

纹理对象、view、builder 的全部状态在宿主，按 guest 地址建键（固定表 + 互斥锁）。
**绝不写 guest 对象内存**：契约用 0xAA 哨兵覆盖 builder / texture / view / pool 对象，
并在整个流程之后再查一次。

纹理对象 = **builder 在 Initialize 时刻的快照**：Initialize 之后再改 builder，纹理的 getter
仍然返回旧值（契约第 5 节实测）。

### 4.2 为什么没有给 builder 加一个 `copy_parameters`

sampler 族有 `nvn_sampler_builder_copy_parameters`，而 **texture builder 族没有**对应物，
而且 `nvn_texture_builder.{h,c}` 不在工单 5.1 的"你写的文件"里。所以纹理族**不改 builder
族**，而是**通过 builder 族自己的名字表**去问 builder 族要值（和 guest 问它的方式一样）：

```
nvn_texture_builder_family_entry_at(i) -> "nvnTextureBuilderGetWidth" -> 转换 -> 调用
```

涉及的 builder getter（vendored 头行号）：GetFlags 1279、GetTarget 1280、GetWidth 1281、
GetHeight 1282、GetDepth 1283、GetLevels 1284、GetFormat 1285、GetSamples 1286、
GetSwizzle 1287、GetDepthStencilMode 1290、GetStride 1293、GetMemoryPool 1298、
GetMemoryOffset 1299。device 用 builder 族已经公开的 `nvn_texture_builder_device_of()`。

**所有这些枚举的 `LARGE` 成员都是 0x7FFFFFFF**，所以它们的底层类型都是 `int`，
一个 `int (*)(void *)` 的形状覆盖 format / target / flags 全部三个。

代价（明说）：纹理族每次 Initialize 会调用 builder 族的 getter，于是 **builder 族的
"只记第一次调用"记账会被纹理族先用掉**，builder 族的 `gets` 统计也会多出纹理族的调用。
这不影响任何契约断言（builder 族没有断言精确计数），但日志里 builder 的第一次行会提前出现。

### 4.3 三个回答是"拒绝并点名"，不是臆造

| 名字 | 本实现的答案 | 理由 |
|---|---|---|
| `nvnTextureGetStorageSize` | 0 + stderr 说明 | 参考树的答案是它算出来的 layout 大小（hle_nvn.c:470）。layout 算法在参考树的 `nvn_tex.c`（deko3d `dk_image.cpp` 的移植），本项目没有。**编一个数会比真数小**，会让 guest 把两张纹理叠在同一块池内存上。和本项目 builder 族的 `nvnTextureBuilderGetStorageSize` 同一个处理。 |
| `nvnTextureGetViewOffset` | 0 + stderr 说明 | level 偏移和 layer 大小都是 layout（hle_nvn.c:507）。同样处理。 |
| `nvnTextureGetSparseTileLayout` | 不填结构 + stderr 说明 | 结构在头里不透明，参考树没有此函数。 |
| `nvnTextureReadTexels` / `ReadTexelsStrided` / `WriteTexels` / `WriteTexelsStrided` | 拒绝并点名，**不动调用方的缓冲区** | 参考树经 GPU 做，并要知道每个 format 的 block 大小（`nvn_fmt_bpb`，hle_nvn.c:523 / 2320）。本运行时既没有创建好的纹理，也没有 format 表。**猜 block 大小会把错的字节写进 guest 的池里。** |
| `nvnTextureFlushTexels` / `InvalidateTexels` | 空操作 | 参考树这里也是空操作（hle_nvn.c:2342 / 529），主机不做统一内存也不跟踪脏页。 |

`nvnTextureGetTextureAddress` = **池的 buffer address + 纹理在池里的偏移**（hle_nvn.c:478）。
本运行时的 memory pool 会发出自己的 buffer address，所以用同一个地址空间。

---

## 5. 验收逐条实测

命令：`bash scripts/runtime/selfproof_nvn_texture.sh` / `bash scripts/runtime/selfproof_nvn_texture_view.sh`
（工作树 `.worktrees/p-mm-26`，HEAD `c4a496c`）。

### 5.1 名字齐全（48 名）

**实测**：纹理族表 31 项、view 族表 14 项、device handle 族表 3 项；契约通过 guest 自己的
`nvnDeviceGetProcAddress` 解析每一个名字，且**每次解析到同一入口**
（`resolved == lookup(NULL, name)`），且 `resolved == entry->fn`；三张表**两两不重名**、
表内每个名字只出现一次。

- 纹理契约 stdout：`{"nvn_texture_errors":0,"names":31,...}`
- view 契约 stdout：`{"nvn_texture_view_errors":0,"names":14,...}`
- 31 + 14 + 3 = **48**

### 5.2 真实对象走一遍 lookup

**实测**（绿）：填 builder（format 0x38 / target 0x8 / flags 0x50 / 256×128×4 / levels 5 /
samples 2 / stride 1024 / pool+offset 4096 / swizzle ZERO,ONE,R,A / DSMode STENCIL）→
`nvnTextureInitialize` 返回 1 → 每一个 getter 都读回 builder 填的值。
view 契约同样：7 个 setter 的每个字段都被 7 个 getter 读回，getter 同时回报"guest 设过没有"。

### 5.3 0xAA 哨兵

**实测**（绿）：builder、texture、view、memory pool 对象在整个流程前后**全是 0xAA**
（`nvn-texture-builder-not-written`、`nvn-texture-object-not-written`、
`nvn-texture-pool-object-not-written`、`nvn-texture-view-object-not-written`，以及
`...-afterwards` 版本）。另外 `nvnTextureGetSparseTileLayout` 之后调用方的结构仍是 0xDD
（没有被填）。

### 5.4 快照语义

**实测**（绿）：Initialize 之后把 builder 的 12 个字段全部改掉（format、target、flags、
宽高深、levels、samples、stride、storage、swizzle、depth stencil mode），纹理的所有 getter
仍然返回旧值。

### 5.5 handle

**实测**（绿）：

| 调用 | 结果 |
|---|---|
| `nvnDeviceGetTextureHandle(device, 7, 3)` | `7 \| (3 << 20)` = `0x300007` |
| `nvnDeviceGetTextureHandle(device, 0, 0)` | `0` |
| `nvnDeviceGetImageHandle(device, 11)` | `11` |
| `nvnDeviceGetSeparateTextureHandle(device, 11)` | `11`（`.value`） |
| `nvnDeviceGetTextureHandle(device, 11, 1)` | `!= 11`（采样器进 bit 20 以上） |

**没有**把池编号 base 加进去（`nvn-texture-handle-does-not-carry-the-pool-base`：
`GetTextureHandle(device,7,3) == 7 | (3<<20)`，与池 base 无关）。

### 5.6 三变体自证（四个退出码）

**纹理族**（日志 `.local/logs/p-mm-26-texture-contract-selfproof.log`）：

| 变体 | 做法 | 实测 exit | 期望 | stdout | stderr |
|---|---|---|---|---|---|
| baseline | 原样 | **0** | 0 | `{"nvn_texture_errors":0,...}` | 无 `[check-fail]` |
| A | 改坏一个期望值 | **1** | 1 | 空 | 有 `[check-fail] nvn-texture-width-round-trips actual=0x100 expected=0x40` |
| B | 同处改坏 + 去掉最终 CHECK + 上报错误数硬编码 0 | **0** | 0 | `{"nvn_texture_errors":0,...}`（**谎报**） | 有 `[check-fail]`（真话留在 stderr） |
| C | 契约不动，桩不复制 builder 的描述 | **1** | 1 | 空 | 有 8 条 `[check-fail]`（flags/target/format/宽高深/levels/samples 全是默认值） |

脚本判四个退出码 + baseline 的 stdout 谎报检查 + A/B/C 的 `[check-fail]` 检查，
**脚本自身 exit 0**。

**view 族**（日志 `.local/logs/p-mm-26-texture-view-contract-selfproof.log`）：

| 变体 | 做法 | 实测 exit | 期望 | stdout | stderr |
|---|---|---|---|---|---|
| baseline | 原样 | **0** | 0 | `{"nvn_texture_view_errors":0,...}` | 无 `[check-fail]` |
| A | 改坏一个期望值 | **1** | 1 | 空 | 有 `[check-fail] nvn-texture-view-base-level-round-trips` |
| B | 同处改坏 + 去 CHECK + 硬编码 0 | **0** | 0 | `{"nvn_texture_view_errors":0,...}`（**谎报**） | 有 `[check-fail]` |
| C | 契约不动，桩不记录 view 的 levels | **1** | 1 | 空 | 有 4 条 `[check-fail]` |

脚本自身 **exit 0**。

### 5.7 Android 交叉编译

**实测**：NDK `26.1.10909125`，
`aarch64-linux-android34-clang --target=aarch64-linux-android34 --sysroot=<ndk>/sysroot
-std=c11 -O2 -g -fPIC -Wall -Wextra -Werror`，两个源文件**都 exit 0**：

- `nvn_texture.android.o` —— ELF 64-bit LSB relocatable, **ARM aarch64**，
  sha256 `bb5ad0e3650ca3400fbe98b9ac9c7592cedc7029b9286fe3ebe678e5f004fdf9`
- `nvn_texture_view.android.o` —— ELF 64-bit LSB relocatable, **ARM aarch64**，
  sha256 `f83f46585529af4ed4fdb9dce17f7c4e351e02d57005615611de2c003793bdca`

日志：`.local/logs/p-mm-26-android-crosscompile.log`。

两个契约 TU 也用 `build_guest_process_contract.sh` 的严格档
（`-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror`，不带
`-DNSO_WITH_CXX_GUARDS=1`）单独编译通过。

### 5.8 报告

本文件。含逐名来源、参考树没有的三个名字、签名不一致的三处、以及"没有证明什么"。

---

## 6. **没有**证明的东西

这一节是工单要求的，也是最重要的。

1. **没有帧。** 本次没有任何一帧被画出来。帧数仍然是 0。
2. **没有像素。** 没有一个像素被写入 framebuffer 或任何 swapchain 图像。
3. **没有 draw 消费纹理。** 这 48 个名字里没有一个被 draw 调用读过。
   `nvnTextureInitialize` 只是把描述记在宿主上；**GPU 上没有创建纹理**，没有 Vulkan 图像，
   没有采样器/纹理的绑定。
4. **没有真机行为。** 本次**没有**碰设备：没有 adb、没有装 APK、没有 boot lock，
   没有在 RG477M 上跑过一行本代码。上面的所有数字都来自 **Mac 宿主**的契约二进制。
5. **没有标题画面，没有可玩，没有帧率。** 一个字都不能说。
6. **没有证明 guest 在真机上会走到 `nvnTextureInitialize` 之后。** 上一轮真机 transcript
   （`.local/logs/p-mm-29-guest-transcript.log`）里 `nvnTextureInitialize` 是 trap slot 40，
   **还没有真机证据**表明总控接线之后 guest 会越过它。
7. **签名没有对本作证实。** vendored 头是 Darksiders II 的 NVN 53.105 重建件。
   本报告里"参考树"的语义只覆盖参考树自己跑的那些游戏。
8. **`nvnTextureGetTextureAddress` 的加法没有在有真实池的环境里跑过。** 宿主的契约构建
   没有 Vulkan 设备，所以没有 memory pool，地址恒为 0（契约按本项目 memory pool 契约的
   同一条规则分支：`#if defined(__ANDROID__)` 期望有真池，宿主期望没有）。**池地址 + 偏移
   这条算式只在 Android 构建上才会被跑到。**
9. **没有验证 ABI。** 这些函数是按 vendored 头 / 参考树的形状写的，
   **没有**和真实 NVN SDK 的二进制 ABI 逐字节比对过。`NVNmemoryPool` 按值返回的
   头文件写法（见 2.3）说明这份重建件本身就有过一处错误。
10. **参考树没有的三个名字**（`nvnTextureGetDebugID`、`nvnTextureGetSparseTileLayout`、
    `nvnTextureViewCompare`）的语义是本运行时自己的，**没有**任何外部对照。
11. **ZCull 公式是参考树的近似**，参考树自己说硬件公式未知。
12. **拒绝的条目不是"实现了"**：`GetStorageSize` / `GetViewOffset` /
    `ReadTexels*` / `WriteTexels*` / `GetSparseTileLayout` 都**没有**实现功能，
    只是拒绝并记日志。guest 若依赖它们读回像素，拿到的会是旧数据。

---

## 7. 总控之后要接的线

本工单**没有**改任何别人的文件。接线是下面四步。

### 7.1 `src/graphics/nvn_device/nvn_device.c`（总控写权限）

在 `#include "nvn_texture_builder.h"` 之后加：

```c
#include "nvn_texture.h"
#include "nvn_texture_view.h"
```

在 `nvnDeviceGetProcAddress` 的 trap 块之前（形状照 `nvn_sampler_family_*` 那一段）加三个族块：

```c
    {
        size_t count = nvn_texture_family_count();
        for (i = 0; i < count; i++) {
            const nvn_texture_family_entry *entry = nvn_texture_family_entry_at(i);
            if (!entry || !entry->name) continue;
            if (!strcmp(name, entry->name)) {
                s_device.implemented_resolved++;
                fprintf(stderr, "[nvn] nvnDeviceGetProcAddress: \"%s\" -> implemented\n", name);
                return entry->fn;
            }
        }
    }
    {
        size_t count = nvn_texture_view_family_count();
        for (i = 0; i < count; i++) {
            const nvn_texture_view_family_entry *entry = nvn_texture_view_family_entry_at(i);
            ...
        }
    }
    {
        size_t count = nvn_device_handle_family_count();
        for (i = 0; i < count; i++) {
            const nvn_device_handle_family_entry *entry = nvn_device_handle_family_entry_at(i);
            ...
        }
    }
```

> 这三个块**已经**写在两个变体生成脚本里（`scripts/runtime/make_nvn_texture_variants.py`
> 和 `scripts/runtime/make_nvn_texture_view_variants.py`），生成的副本在
> `.local/selfproof/nvn_device_with_texture_family.c` 和
> `.local/selfproof/nvn_device_with_texture_view_family.c`，可以直接抄。

三个 device 句柄函数也挂在这三个族块里（`nvn_device_handle_family_*`），
**不要**另外在 `nvn_device.c` 里再实现一遍。

### 7.2 `scripts/runtime/build_guest_process_contract.sh`（总控写权限）

`core_sources` 列表里加两个源（`nvn_sampler.c` 已经在里面，是 8131f76 加的）：

```python
nvn_device/'nvn_texture.c', nvn_device/'nvn_texture_view.c',
```

**族契约的 glob 和开关是自动的**，不用改：`tests/native/nvn_*_contract.c` 会被自动收进去，
开关由文件名推导 —— `nvn_texture_contract.c` → `--nvn-texture`，
`nvn_texture_view_contract.c` → `--nvn-texture-view`。

> **注意**：契约链接需要 `nvn_texture.c` / `nvn_texture_view.c` 在 host 源列表里，
> 否则链接会缺符号；契约**运行**需要 7.1 的接线，否则名字解析不到（会解析到 trap）。
> 这两件事做完，契约才会绿。

### 7.3 `src/android/native/CMakeLists.txt`（总控写权限）

NVN 源列表加 `nvn_texture.c` / `nvn_texture_view.c`。

### 7.4 池编号 base

**不需要**在本族里做什么。`nvnDeviceGetTextureHandle` 返回的是**池内相对号**
（`tex | samp << 20`），base 是在**读取 handle** 的地方加的
（`nvn_texture_pool_base()` / `nvn_sampler_pool_base()`，83a64d5 已经落地）。
本族没有碰那两个访问器。

---

## 8. 证据路径

| 证据 | 路径 |
|---|---|
| 纹理族自证日志 | `.local/logs/p-mm-26-texture-contract-selfproof.log` |
| view 族自证日志 | `.local/logs/p-mm-26-texture-view-contract-selfproof.log` |
| Android 交叉编译日志 | `.local/logs/p-mm-26-android-crosscompile.log` |
| 8 个变体的 stdout/stderr 副本 | `.local/selfproof/nvn_texture_*.{stdout,stderr}`、`.local/selfproof/nvn_texture_view_*.{stdout,stderr}` |
| 接线后的 `nvn_device.c` 副本 | `.local/selfproof/nvn_device_with_texture_family.c`、`.local/selfproof/nvn_device_with_texture_view_family.c` |
| 三个变体的源 | `.local/selfproof/nvn_texture_broken_fixed.c`、`nvn_texture_broken_prefix.c`、`nvn_texture_stub_no_snapshot.c`、`nvn_texture_view_broken_fixed.c`、`nvn_texture_view_broken_prefix.c`、`nvn_texture_view_stub_no_levels.c` |
| Android 目标文件 | 工作树 `.local/build-nvn-texture-android/*.android.o`（sha256 见 5.7） |
| guest 解析记录（名字来源） | `.local/logs/p-mm-29-guest-transcript.log` |

主检出目录的 `.local/logs/` 与 `.local/selfproof/` 里已放了同一批副本。

---

## 9. 提交

- 分支：`task/P-MM-26-texture-object`
- 提交里**只有**第 1 节列出的文件
- **没有** push

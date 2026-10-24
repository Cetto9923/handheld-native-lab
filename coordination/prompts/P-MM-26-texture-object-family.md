# 工单 P-MM-26｜MiniMax Code｜NVN 纹理对象族 + 纹理 view 族 + 三个 device 句柄函数（48 名）

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `c527ca6`（含 P-MM-25 sampler 对象族，已集成）
> 真机证据：`.local/logs/p-mm-29-guest-transcript.log`（guest 已越过 sampler builder，第一致命是 `nvnSamplerInitialize`）
> 前置：P-MM-25 已交付并集成（`c15cb3b` / `c527ca6`）；帧数仍为 0；未达标题/可玩

---

## 0. 目标（一句话）

让 guest 在 `nvnSamplerInitialize` 之后**不再撞陷坑**，能一路走到「建纹理 → 建 view → 注册进池 → 取 handle → 绑定 → draw」。

上一轮真机（`p-mm-29`）guest 的第一致命是 `nvnSamplerInitialize`（trap slot 71）。P-MM-25 已补上 sampler 对象族。**下一个必然撞上的是 `nvnTextureInitialize`（trap slot 40）**，它现在还是陷坑。纹理对象族 + view 族 + 三个 device 句柄函数就是挡在这条路上的门。

---

## 1. 仓库与环境（必须）

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- **自己开独立 worktree**（`.worktrees/p-mm-26`），分支 `task/P-MM-26-texture-object`；主检出目录里有别人的 WIP，不要动
- 设备：**不需要真机**，你不要碰设备、不要 adb、不要 boot lock
- 先读：`AGENTS.md` → 本工单 → `src/graphics/nvn_device/nvn_sampler.{h,c}`（**形态模板**：同款游戏的 sampler 对象族，31+14 名的同构写法）→ `src/graphics/nvn_device/nvn_texture_builder.{h,c}`（**字段模板**：纹理 builder 已经存在，纹理对象是它的快照）
- 成功可 `git commit`；**不 push**；不 reset --hard / clean / stash
- `.local/romfs/**` 只读；`task/T04-nvn-firstframe` / `.claude/worktrees/*` 勿改

---

## 2. 名字清单（**guest 自己解析出来的**，不要自己猜）

来源：`.local/logs/p-mm-29-guest-transcript.log` 里 `nvnDeviceGetProcAddress` 解析记录。

### 2.1 纹理对象族（31 名）

```
nvnTextureInitialize          nvnTextureFinalize            nvnTextureSetDebugLabel
nvnTextureGetDebugID          nvnTextureGetFlags             nvnTextureGetTarget
nvnTextureGetWidth            nvnTextureGetHeight            nvnTextureGetDepth
nvnTextureGetLevels           nvnTextureGetSamples           nvnTextureGetFormat
nvnTextureGetStride           nvnTextureGetMemoryPool        nvnTextureGetMemoryOffset
nvnTextureGetTextureAddress   nvnTextureGetViewOffset        nvnTextureGetStorageClass
nvnTextureGetRawStorageClass  nvnTextureGetStorageSize       nvnTextureGetZCullStorageSize
nvnTextureGetSwizzle          nvnTextureGetDepthStencilMode nvnTextureGetSparseTileLayout
nvnTextureCompare             nvnTextureReadTexels           nvnTextureReadTexelsStrided
nvnTextureWriteTexels         nvnTextureWriteTexelsStrided   nvnTextureFlushTexels
nvnTextureInvalidateTexels
```

### 2.2 纹理 view 族（14 名）

```
nvnTextureViewSetDefaults     nvnTextureViewSetLevels        nvnTextureViewSetLayers
nvnTextureViewSetFormat       nvnTextureViewSetTarget        nvnTextureViewSetSwizzle
nvnTextureViewSetDepthStencilMode
nvnTextureViewGetLevels       nvnTextureViewGetLayers        nvnTextureViewGetFormat
nvnTextureViewGetTarget       nvnTextureViewGetSwizzle       nvnTextureViewGetDepthStencilMode
nvnTextureViewCompare
```

### 2.3 device 句柄函数（3 名，现在完全没有实现）

```
nvnDeviceGetTextureHandle          (device, tex, samp) -> tex | samp << 20
nvnDeviceGetImageHandle            (device, id)       -> id
nvnDeviceGetSeparateTextureHandle  (device, id)       -> id
```

**handle 的含义（重要，别搞错）**：handle 不是指针，是**池内编号**。低 20 位是纹理号，第 20 位以上是采样器号。
参考树 `hle_nvn.c:2059`：`return (NVNtextureHandle)((uint64_t)(uint32_t)tex | (uint64_t)(uint32_t)samp << 20);`
**池的编号基址是在读取 handle 时才加上去的**（`dpool_base`），不在 handle 里。所以 `GetTextureHandle` 返回的是**池内相对号**，不要在这里加 base。

---

## 3. 语义来源（按优先级）

1. **签名**：`src/graphics/nvn/include/nvn/nvn.h`（vendored，PFNNVN*PROC 形式，约 L1130/L1132/L1151 是三个句柄函数）。
   ⚠️ 这个头是**别的游戏（Darksiders II）的 NVN 53.105 重建件**，签名**没有对本作证实**。凡是头里没有签名的名字，**逐参数记录、不解释、不臆造返回值**（照 `nvn_texture_builder.c` 里 raw_proc 的做法）。
2. **语义**：`.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`（同款游戏的参考宿主，MIT）：
   - `nvnTextureInitialize` 在 **L437**，`nvnTextureFinalize` L465，各 getter L466–L492
   - `nvnTextureCompare` L492
   - `nvnTextureView*` L499–L507
   - `nvnTextureReadTexels` L523 / `ReadTexelsStrided` L526 / `InvalidateTexels` L529
   - `nvnTextureGetSwizzle` L1954、`GetStorageClass` L1958、`GetZCullStorageSize` L1959
   - ⚠️ **参考树没有的**：`nvnTextureGetRawStorageClass`、`nvnTextureGetSparseTileLayout`、`nvnTextureViewGetSwizzle`/`SetSwizzle`、`nvnTextureViewGetDepthStencilMode`/`SetDepthStencilMode`、`nvnTextureWriteTexels*`、`nvnTextureFlushTexels`、`nvnTextureGetDebugID`、`nvnTextureSetDebugLabel`、`nvnTextureViewCompare`。这些**没有参考对照**，必须在报告里点名，不得声称"与参考一致"。
3. **纹理 builder 的字段**：本项目 `nvn_texture_builder.{h,c}` 已经把 guest 填的每个字段都存下来了。**纹理对象 = 初始化时刻对 builder 的一次快照**（builder 之后被改，texture 不变）。这一点和 P-MM-25 的 sampler 快照同构。

---

## 4. 状态放在宿主（**铁律**）

- 纹理对象和 view 的状态**全在宿主**，按 guest 地址建键。
- **绝不写 guest 对象内存**：契约里用 **0xAA 哨兵**证明纹理对象 / view / builder 的字节一个都没被改。
- 不要去猜 NVN 纹理对象的内存布局（那是 Nintendo SDK 的私有布局，本项目没有）。

---

## 5. 交付物

### 5.1 你写的文件（**只有这些**，别人正在写别的文件）

```
src/graphics/nvn_device/nvn_texture.h
src/graphics/nvn_device/nvn_texture.c
src/graphics/nvn_device/nvn_texture_view.h
src/graphics/nvn_device/nvn_texture_view.c
tests/native/nvn_texture_contract.c
tests/native/nvn_texture_view_contract.c
scripts/runtime/selfproof_nvn_texture.sh
scripts/runtime/selfproof_nvn_texture_view.sh
scripts/runtime/make_nvn_texture_variants.py
scripts/runtime/make_nvn_texture_view_variants.py
coordination/reports/T-TEXTURE-object-2026-10-25.md
```

### 5.2 你**不许**改的文件（总控保留写权限）

`src/graphics/nvn_device/nvn_device.c`、`src/android/native/CMakeLists.txt`、
`scripts/runtime/build_guest_process_contract.sh`、`tests/native/guest_process_contract.c`、
`src/graphics/nvn_device/nvn_command_buffer.{h,c}`、`src/graphics/nvn_device/nvn_command_buffer.generated.c`、
`src/graphics/nvn_device/nvn_texture_pool.{h,c}`、`src/graphics/nvn_device/nvn_sampler_pool.{h,c}`、
`src/graphics/nvn_device/nvn_backend_vulkan.c`、`src/runtime/hle/guest_process.c`、`src/runtime/nso/*`

### 5.3 族契约的写法（**照抄这个约定**）

- 每个契约 TU 自带 `main` 和**自己的开关**，开关名 = 文件名去掉 `_contract` 再把 `_` 换成 `-`：
  - `tests/native/nvn_texture_contract.c` → 开关 `--nvn-texture`
  - `tests/native/nvn_texture_view_contract.c` → 开关 `--nvn-texture-view`
  - 开关不对就 `fprintf(stderr,"usage: ...")` + `return 2`（**不要**用 `return 0`）
- 契约最后一行的 stdout 是一行 JSON，里面有一个 `<name>_errors`（例如 `nvn_texture_errors` / `nvn_texture_view_errors`），宿主门读它
- 自己带 `CHECK` / `expect_value` / `expect_condition`，**期望值必须独立于实现常量写死**
- **禁止恒真断言**；**禁止把错误数硬编码成 0**；**禁止删测试改绿**

### 5.4 自证脚本（**三变体，必须 A红 / B假绿 / C红**）

照 `scripts/runtime/selfproof_nvn_sampler.sh` 的形状写：

| 变体 | 做法 | 期望 |
|---|---|---|
| baseline | 不改 | exit 0，stdout `errors:0` |
| **A** | 把某个期望值改坏 | **exit 1**，stderr 有真 `[check-fail]` 行 |
| **B** | 同处改坏 **+** 去掉最终 CHECK **+** 把上报错误数**硬编码 0** | **exit 0**，而 stdout 仍谎报 `errors:0`，stderr 留着真 `[check-fail]` ← **这就是假绿，必须被量出来** |
| **C** | 契约一个字不改，只把桩（族实现）改坏 | **exit 1** |

脚本**自己判四个退出码**，对就 exit 0，不对 exit 非 0。
日志写到 `.local/logs/p-mm-26-<name>-selfproof.log`（**每次运行独立目录**）。

---

## 6. 验收（每一条都要有**实测数字**，不是「应该」）

1. **名字齐全**：31 + 14 + 3 = **48 个名字**都能通过 guest 自己的 `nvnDeviceGetProcAddress` 解析到，且**每次解析到同一入口**；族表里每个名字**只出现一次**。
2. **真实对象走一遍 lookup**：填 builder → `nvnTextureInitialize` → 每个 getter 读回 builder 填的值 → 绿。
3. **0xAA 哨兵**：纹理对象 / view / builder 全部未被写 → 绿。
4. **快照语义**：Initialize 之后再改 builder，texture 的 getter 仍返回旧值 → 绿。
5. **handle**：三个 device 句柄函数返回 `tex | samp<<20` / `id` / `id`，且**没有**把池 base 加进去 → 绿。
6. **三变体自证**：A exit 1 / B exit 0 且 stdout 谎报 errors:0 / C exit 1，脚本 exit 0。
7. **Android 交叉编译**：用 NDK clang `--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` 编译过。
8. **报告**：写 `coordination/reports/T-TEXTURE-object-2026-10-25.md`，里面必须有：
   - 每个名字的签名来源（vendored 头行号 / 参考树行号 / 无来源）
   - 参考树没有的名字**点名**
   - 你**没有**证明的东西（帧、像素、draw 消费、真机行为——都**没有**）

---

## 7. 总控之后要接的线（**你不用做，写在报告里**）

1. `src/graphics/nvn_device/nvn_device.c`：加 `nvn_texture_family_count()` / `nvn_texture_family_entry_at()` 和 `nvn_texture_view_family_*` 两个族块（形状照 `nvn_sampler_family_*` 那一段）
2. `scripts/runtime/build_guest_process_contract.sh`：源文件列表加 `nvn_texture.c` / `nvn_texture_view.c`；**族契约的 glob 和开关是自动的**，不用改
3. `src/android/native/CMakeLists.txt`：NVN 源列表加 `nvn_texture.c` / `nvn_texture_view.c`
4. 三个 device 句柄函数也挂在 `nvn_device.c` 的 device 族里

---

## 8. 禁止事项（写清楚，别踩）

- ❌ 不碰真机、不 adb、不 boot lock、不装 APK
- ❌ 不改 5.2 里的任何文件
- ❌ 不 push
- ❌ 不写 guest 对象内存（0xAA 哨兵会抓）
- ❌ 不为通过测试硬编码结果、伪造数据、删断言
- ❌ 不声称标题画面 / 可玩 / 有帧 / 帧率
- ❌ 不把「参考树没有的名字」写成「与参考一致」
- ❌ 不在报告里写「应该」「预计」「理论上」这类没有实测支撑的话

---

## 9. 完成后的动作

1. `git commit`（分支 `task/P-MM-26-texture-object`，**不 push**），提交里只放 5.1 列的文件
2. 把自证日志和 stdout/stderr 副本拷到主检出的 `.local/logs/` 和 `.local/selfproof/`
3. 写完报告就**停下来**，在最终回复里给出：分支、commit、每个验收项的实测结果（含四个退出码）、证据路径、以及**没有**证明的东西

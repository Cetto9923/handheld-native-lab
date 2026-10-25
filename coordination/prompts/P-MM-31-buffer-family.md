# 工单 P-MM-31｜MiniMax Code｜NVN 缓冲族（18 名）

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `971df74`（含 P-MM-26 纹理族、P-MM-27 管线崩溃根因）
> 依据：设备重跑（2026-10-25 08:49）的 transcript 里，guest 解析了但**尚未调用**的陷坑名字，本族是其中最大的一族。

---

## 0. 目标（一句话）

实现 **NVN 缓冲族 18 名**，让 guest 在建完纹理之后**能建顶点/索引缓冲**。

**这一轮不产出画面。**

---

## 1. 名字清单（**guest 自己解析出来的**，共 18 名）

```
nvnBufferBuilderSetDefaults   nvnBufferBuilderSetDevice    nvnBufferBuilderSetStorage
nvnBufferBuilderGetDevice     nvnBufferBuilderGetSize      nvnBufferBuilderGetMemoryOffset
nvnBufferBuilderGetMemoryPool
nvnBufferInitialize           nvnBufferFinalize            nvnBufferSetDebugLabel
nvnBufferGetDebugID           nvnBufferGetAddress          nvnBufferMap
nvnBufferGetSize              nvnBufferGetMemoryPool       nvnBufferGetMemoryOffset
nvnBufferFlushMappedRange     nvnBufferInvalidateMappedRange
```

来源：设备 transcript `.local/logs/p-mm-33-guest-transcript.log` 里 `nvnDeviceGetProcAddress` 的解析记录。

---

## 2. 语义来源

1. **签名**：`src/graphics/nvn/include/nvn/nvn.h`（vendored，别的游戏的重建件，**签名未对本作证实**）。头里没有签名的**逐参数记录、不臆造返回值**（照 `nvn_texture_builder.c` 里 raw_proc 的做法）。
2. **语义**：`.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`（同款游戏的参考宿主，MIT）：
   - builder：`SetDefaults` **L1766**（**保留 device，其余清零**）、`SetDevice` L1767、`SetStorage(b, pool, off, n)` L1768
   - `nvnBufferInitialize` **L1769**、`Finalize` L1777、`SetDebugLabel` L1778
   - `GetAddress` **L1779**、`Map` **L1780**、`GetSize` L1781、`GetMemoryPool` L1782、`GetMemoryOffset` L1783
   - `FlushMappedRange` L1784 / `InvalidateMappedRange` L1785 —— 参考树把它们**记成空操作**，并写明原因：*unified memory: nothing to flush*
   - `nvnBufferBuilderGetSize` L2022、`GetMemoryOffset` L2023、`GetMemoryPool` / `GetDevice` 同段
3. **形状模板**：`src/graphics/nvn_device/nvn_texture.{h,c}`（P-MM-26 刚交付的纹理对象族，**同一个形状**：状态全在宿主、按 guest 地址建键、**绝不写 guest 对象内存**、0xAA 哨兵）。
4. **缓冲的存储来自内存池**：本项目已有 `nvn_memory_pool.{h,c}`（`nvnMemoryPoolMap` / `nvnMemoryPoolGetBufferAddress` / `nvn_memory_pool_resolve_address`）。**`nvnBufferGetAddress` / `nvnBufferMap` 返回的必须是 guest 自己的内存地址**（参考树是 `pool->mem + off`），这样命令缓冲里记的地址才能被 replay 解析回宿主内存。

---

## 3. 状态放在宿主（**铁律**）

- 缓冲与 builder 的状态**全在宿主**，按 guest 地址建键。
- **绝不写 guest 对象内存**：契约用 **0xAA 哨兵**证明 buffer / builder 的字节一个都没被改。
- 不要猜 NVN 缓冲对象的内存布局（那是 Nintendo SDK 私有布局）。

---

## 4. 写范围（**只有这些**）

```
src/graphics/nvn_device/nvn_buffer.h
src/graphics/nvn_device/nvn_buffer.c
tests/native/nvn_buffer_contract.c
scripts/runtime/selfproof_nvn_buffer.sh
scripts/runtime/make_nvn_buffer_variants.py
coordination/reports/T-BUFFER-family-2026-10-25.md
```

## 5. 不许改（总控保留写权限）

`src/graphics/nvn_device/nvn_device.c`、`nvn_memory_pool.*`、`nvn_texture.*`、`nvn_sampler*`、
`nvn_command_buffer.*`、`nvn_command_replay.*`、`src/android/native/CMakeLists.txt`、
`scripts/runtime/build_guest_process_contract.sh`、`src/runtime/**`、`tests/native/guest_process_contract.c`

---

## 6. 族契约与自证（照 P-MM-26 的形状）

- 契约 TU 自带 `main` 和**自己的开关**，开关名 = 文件名去掉 `_contract` 再把 `_` 换成 `-` ⇒ **`--nvn-buffer`**。
- 开关不对就 `fprintf(stderr,"usage: ...")` + `return 2`（**不要** `return 0`）。
- 末行 stdout 是一行 JSON，里面有 **`nvn_buffer_errors`**；自带 `CHECK` / `expect_value` / `expect_condition`。
- **期望值必须独立于实现常量写死**；**禁止恒真断言**；**禁止把错误数硬编码成 0**；**禁止删测试改绿**。

**三变体自证**（脚本自己判四个退出码）：

| 变体 | 做法 | 期望 |
|---|---|---|
| baseline | 不改 | exit 0，stdout `errors:0` |
| **A** | 改坏一个期望值 | **exit 1**，stderr 有真 `[check-fail]` |
| **B** | 同处改坏 **+ 去掉最终 CHECK **+ 上报错误数**硬编码 0** | **exit 0**，而 stdout 仍谎报 `errors:0`、stderr 留真 `[check-fail]` ← **假绿要量出来** |
| **C** | 契约一字不改，只把桩（族实现）改坏 | **exit 1** |

日志：`.local/logs/p-mm-31-buffer-contract-selfproof.log`（**每次运行独立目录**）。

---

## 7. 验收（每条都要**实测数字**）

1. **18 个名字**都能通过 guest 自己的 `nvnDeviceGetProcAddress` 解析到，**每次解析到同一入口**，族表里**每个名字只出现一次**。
2. **真实对象走一遍 lookup**：填 builder → `nvnBufferInitialize` → 每个 getter 读回 builder 填的值 → 绿。
3. **0xAA 哨兵**：buffer / builder 全程未被写 → 绿。
4. **快照语义**：Initialize 之后再改 builder，buffer 的 getter 仍返回旧值 → 绿。
5. **地址是真的**：`GetAddress` / `Map` 返回的地址**落在本项目的内存池里**（用 `nvn_memory_pool_resolve_address` 验），不是 0、不是编的。
6. **三变体自证**：A exit 1 / B exit 0（谎报）/ C exit 1，脚本 exit 0。
7. **NDK 交叉编译**：`--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` 过。
8. **报告** `coordination/reports/T-BUFFER-family-2026-10-25.md`：每个名字的签名来源（vendored 头行号 / 参考树行号 / 无来源）、参考树没有的名字**点名**、**没有**证明的东西。

---

## 8. 纪律

1. **第一条工具调用就是写文件**，不是搜索、不是推演。
2. **先写最小可编译版本，立刻 `git commit`**，再一项一项补。**宁可有五个能编译的提交，也不要一个想清楚了但没写出来的提交。**
3. 预算过半还没提交 ⇒ **立刻提交**当前能编译的状态。
4. 每次实验一个独立目录，原始日志不覆盖。

---

## 9. 禁止事项

- ❌ push；reset --hard / clean / stash
- ❌ 改第 5 节里的文件
- ❌ 写 guest 对象内存（0xAA 哨兵会抓）
- ❌ 为通过测试硬编码结果 / 伪造数据 / 删断言
- ❌ 声称标题画面 / 可玩 / 有帧 / 帧率
- ❌ 把「参考树没有的名字」写成「与参考一致」
- ❌ 碰真机（**本卡不需要设备**，设备是协调者的）

---

## 10. 完成后的动作

1. `git commit`（分支 `task/P-MM-31-buffer-family`，**不 push**），只放第 4 节的文件
2. 自证日志与 stdout/stderr 副本拷到主检出的 `.local/logs/` 与 `.local/selfproof/`
3. 报告写完就**停下来**，最终回复给我：分支、commit、每个验收项的实测结果（含四个退出码）、证据路径、**没有**证明的东西

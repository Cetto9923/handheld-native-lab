# T-BUFFER-family-2026-10-25 ｜ NVN 缓冲族（18 名）｜ 工单 P-MM-31

> 基线：`work/rg477m-native` @ `971df74`
> 分支：`task/P-MM-31-buffer-family`
> 宿主：macOS darwin arm64（`Apple clang 17.0.2`），NDK r26b（26.1.10909125）

---

## 0. 一句话

交付 **18 个名字**：7 个 buffer builder + 11 个 buffer 对象。状态全在宿主，按 guest 地址建键，
**没有写 guest 对象内存一个字节**（契约用 0xAA 哨兵证明）。buffer 是 builder 在
`nvnBufferInitialize` 时刻的一次快照。**地址是 guest 自己的内存**（池内存 + 偏移），用
`nvn_memory_pool_resolve_address` 验得过。三变体自证 A 红 / B 假绿 / C 红，脚本自己判四个
退出码。NDK `aarch64-linux-android34` + `-Wall -Wextra -Werror` 交叉编译通过。

**这一轮不产出画面。** 没有帧、没有像素、没有 draw 消费 —— 见第 6 节。

---

## 1. 交付物

| 文件 | 作用 |
|---|---|
| `src/graphics/nvn_device/nvn_buffer.h` | 族的表与统计 |
| `src/graphics/nvn_device/nvn_buffer.c` | 18 个名字的实现 |
| `tests/native/nvn_buffer_contract.c` | 族契约 TU（自带 `main`，开关 `--nvn-buffer`） |
| `scripts/runtime/make_nvn_buffer_variants.py` | 三个变体 + 接线副本 |
| `scripts/runtime/selfproof_nvn_buffer.sh` | 自证脚本（自己判四个退出码） |
| `coordination/reports/T-BUFFER-family-2026-10-25.md` | 本报告 |

**没有碰**工单第 5 节里总控保留写权限的任何文件：`nvn_device.c`、`nvn_memory_pool.*`、
`nvn_texture.*`、`nvn_sampler*`、`nvn_command_buffer.*`、`nvn_command_replay.*`、
`src/android/native/CMakeLists.txt`、`scripts/runtime/build_guest_process_contract.sh`、
`src/runtime/**`、`tests/native/guest_process_contract.c`。

---

## 2. 名字清单与**逐个**的来源

18 个名字来自设备 transcript `.local/logs/p-mm-33-guest-transcript.log` 的
`nvnDeviceGetProcAddress` 解析记录，**不是猜的**（用 `grep -o 'nvnBuffer[A-Za-z]*'` 从那份
transcript 里提取，`sort -u` 恰好 18 个，与工单第 1 节一致）。

「vendored 头」= `src/graphics/nvn/include/nvn/nvn.h`（别的游戏 Darksiders II 的 NVN 53.105
重建件，**签名未对本作证实**）。「参考树」= `.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`
（同款游戏，MIT）。

| # | 名字 | vendored 头行号 | 参考树实现行号 | 参考树 protos 行号 |
|---|---|---|---|---|
| 1 | `nvnBufferBuilderSetDefaults` | 1235 | **1766** | 110 |
| 2 | `nvnBufferBuilderSetDevice` | 1234 | **1767** | 109 |
| 3 | `nvnBufferBuilderSetStorage` | 1236-1237 | **1768** | 111 |
| 4 | `nvnBufferBuilderGetDevice` | **无** | **无** | **无** |
| 5 | `nvnBufferBuilderGetSize` | 1240 | **2022** | 114 |
| 6 | `nvnBufferBuilderGetMemoryOffset` | 1239 | **2023** | 113 |
| 7 | `nvnBufferBuilderGetMemoryPool` | 1238 | **无** | 112 |
| 8 | `nvnBufferInitialize` | 1241 | **1769** | 115 |
| 9 | `nvnBufferFinalize` | 1243 | **1777** | 117 |
| 10 | `nvnBufferSetDebugLabel` | 1242 | **1778** | 116 |
| 11 | `nvnBufferGetDebugID` | 1251 | **无** | 125 |
| 12 | `nvnBufferGetAddress` | 1245 | **1779** | 119 |
| 13 | `nvnBufferMap` | 1244 | **1780** | 118 |
| 14 | `nvnBufferGetSize` | 1250 | **1781** | 124 |
| 15 | `nvnBufferGetMemoryPool` | 1248 | **1782** | 122 |
| 16 | `nvnBufferGetMemoryOffset` | 1249 | **1783** | 123 |
| 17 | `nvnBufferFlushMappedRange` | 1246 | **1784** | 120 |
| 18 | `nvnBufferInvalidateMappedRange` | 1247 | **1785** | 121 |

### 2.1 参考树**没有**的名字（3 个，点名）

这 3 个名字在参考树的 `names.txt`（第 130、131、144 行）和 `nvn_thunks.S`（Ln129、Ln130、
Ln143）里，也就是说**游戏自己要它们**，但参考树**没有实现**。它们的值**不声称与参考一致**：

1. **`nvnBufferBuilderGetDevice`** —— vendored 头里**完全没有**这个签名（`grep
   'PFNNVNBUFFER'` 查不到），参考树把它列在「Names without a prototype in the header」
   （`nvn_protos.h:506`）。**本运行时照 `nvn_texture_builder.c` 里 `nvnTextureBuilderGetDevice`
   的 raw_proc 做法**：8 个字参数原样记录并打印，只解引用第一个字，返回值用
   `NVN_SET_RESULT` 放进 x0。返回的是 guest 自己 `SetDevice` 进去的 device，不是零。
2. **`nvnBufferBuilderGetMemoryPool`** —— vendored 头 1238 写的是**按值返回** `NVNmemoryPool`
   （一个 256 字节的不透明 struct，`nvn.h:153-155`），参考树也只有原型（112 行）、没有实现。
   按值返回 256 字节不透明 struct 给 guest 是没法用的，而它的兄弟
   `nvnBufferGetMemoryPool`（头 1248 / 参考树 1782）返回的是**指针**。**本运行时返回指针**，
   这是一处**明确记录在案的偏离**。
3. **`nvnBufferGetDebugID`** —— vendored 头 1251 有签名（`uint64_t`），参考树有原型（125 行）
   但**没有实现**，所以参考树给不出值。**本运行时**给的是这个对象自己的 debug id，取自设备
   的 debug domain id 生成器（`nvnDeviceGenerateDebugDomainId`），跟这个运行时里其它每一个
   对象同源。

### 2.2 其余 15 个名字的语义来源

- **默认值**（`SetDefaults` 保留 device、其余清零）：参考树 **1766**。
- **描述内容**（device + memory pool + offset + size）：参考树 1761 的 `buf_builder_t`。
- **`Initialize` 拒绝没有存储的 builder**：参考树 **1771**（`!bb->pool || !bb->pool->mem` → 返回 0）。
- **`Initialize` 越过池尾仍要建、并记一条**：参考树 **1772**。
- **`GetAddress` / `Map`** = 池内存 + 偏移：参考树 **1779 / 1780**。
- **`GetSize` / `GetMemoryPool` / `GetMemoryOffset`**：参考树 **1781 / 1782 / 1783**。
- **`Flush` / `Invalidate` 是空操作**：参考树 **1784 / 1785**，参考树自己写明原因
  *unified memory: nothing to flush*。本运行时给的是同一语义、同一条理由（存储是 guest 自己
  写的宿主内存，本来就是一致的）。
- **builder 的 `GetSize` / `GetMemoryOffset`**：参考树 **2022 / 2023**。
- **`Finalize` / `SetDebugLabel`**：参考树 **1777 / 1778**。

---

## 3. 状态放在宿主（铁律）

- builder 与 buffer 的状态**全在宿主**，按 guest 地址建键（`nvn_buffer.c` 的
  `builder_find` / `buffer_find`）。
- **绝不写 guest 对象内存**。契约用 **0xAA 哨兵**覆盖 guest 自己的 builder / buffer 字节：
  填完 builder 之后查一次，全部流程跑完再查一次，两次都必须是 0xAA（契约第 5 节、第 12 节）。
- 没有猜 NVN 缓冲对象的内存布局。

---

## 4. **地址是真的**（工单契约第 5 条）

工单写死了一条：*「`nvnBufferGetAddress` / `nvnBufferMap` 返回的必须是 guest 自己的内存地址
（参考树是 `pool->mem + off`），这样命令缓冲里记的地址才能被 replay 解析回宿主内存」*。

本运行时照办：

- `GetAddress` / `Map` 的值 = **内存池自己的内存地址 + buffer 在池里的偏移**，取自内存池族
  （`nvnMemoryPoolMap`），不是本族自己编的数。
- 契约用 `nvn_memory_pool_resolve_address` 验这条地址**能解析回宿主内存**，且解析回来的指针
  **就是**同一个地址、`available` ≥ buffer 大小。
- 契约另外直接比对了**数值**：`address == (uint64_t)(uintptr_t)pool_storage + 256`，
  即地址确实是「池内存 + 偏移」。

实测（baseline，host）：地址等于 `pool_storage + 256`（数值直接比对通过；地址本身是栈上
`pool_storage` 的地址，随每次运行变化，所以这里只给等式而不给一个会过期的数字），
`resolve_address` 解析成功、解析回来同一个指针、`available` 足够。

---

## 5. 族契约与三变体自证

### 5.1 契约

`tests/native/nvn_buffer_contract.c`，自带 `main` 和自己的开关 **`--nvn-buffer`**（构建脚本从
文件名推导，**没有改构建脚本**）。开关不对 → `fprintf(stderr,"usage: ...")` + `return 2`
（**不是** `return 0`）。末行 stdout 是一行 JSON，里面有 **`nvn_buffer_errors`**。

契约**通过 guest 自己的 lookup** 驱动这一族（bootstrap → `nvnDeviceGetProcAddress` →
每个名字），**不直接调实现函数** —— 这样「设备没交出这个名字」是契约的失败，而不是静默通过。

期望值**独立于实现常量写死**（`TEST_BUFFER_SIZE 4096`、`TEST_MEMORY_OFFSET 256`、
`TEST_POOL_SIZE 65536` 等都是契约自己写的字面量，不是实现的常量）。
**没有恒真断言**：所有断言都带非平凡的比较（`!= 0`、`== pool_object`、地址数值相等、
`available >= size`、0xAA 逐字节、debug id 互不相同、统计计数 > 0 等）。
**错误数没有被硬编码成 0**：末行 JSON 打印的是 `errors` 这个变量本身。

### 5.2 三变体（脚本自己判四个退出码）

`scripts/runtime/selfproof_nvn_buffer.sh`（日志 `.local/logs/p-mm-31-buffer-contract-selfproof.log`）。
每次运行一个**独立目录**（`.local/selfproof/run-<时间戳>-<pid>/`），原始 stdout/stderr 不覆盖。

| 变体 | 做法 | 期望 | **实测** |
|---|---|---|---|
| baseline | 不改 | exit 0，stdout `errors:0` | **exit 0**，`{"nvn_buffer_errors":0,"names":18,"initialized":2,"finalized":1,"gets":26,"maps":2,"flushes":1,"invalidates":1,"refused":7,"live_buffers":1,"live_builders":2}` |
| **A** | 改坏一个期望值（size 期望写成 `0x1234`） | exit 1，stderr 真 `[check-fail]` | **exit 1**，`[check-fail] nvn-buffer-size-round-trips actual=0x1000 expected=0x1234` |
| **B** | 同处改坏 **+ 去掉最终 CHECK + 上报错误数硬编码 0** | exit 0 而 stdout 仍谎报 `errors:0` | **exit 0**，stdout 仍 `{"nvn_buffer_errors":0,…}`，stderr 仍有真 `[check-fail] nvn-buffer-size-round-trips actual=0x1000 expected=0x1234` —— **假绿量出来了** |
| **C** | 契约一字不改，只把桩改坏（族不再把 builder 的描述拷进 buffer） | exit 1 | **exit 1**，8 条 `[check-fail]`：size / memory-offset / memory-pool 三个 round-trip、`address-is-not-zero`、`address-is-the-pool-memory-plus-the-offset`、`address-resolves-in-the-memory-pool`、`address-has-room-for-the-buffer`、`snapshot-keeps-the-size` |

**脚本自身 exit 0**（四个退出码全部符合预期）。B 变体是**故意**把假绿量出来的：它证明
「去掉最终闸门 + 把错误数写死成 0」这个形状会骗过只看退出码和 stdout 的人。

变体 C 的桩是从 `nvn_buffer.c` 派生的一份**副本**（`.local/selfproof/run-*/`），
把 `nvnBufferInitialize` 里拷贝描述的那几行改成不拷；契约文件本身**一个字没动**。

族在 `src/graphics/nvn_device/nvn_device.c` 里的接线属于总控，所以自证脚本**自己造了一份接线
副本**（`.local/selfproof/run-*/nvn_device_with_buffer_family.c`）来跑这四次；
**树里的 `nvn_device.c` 没有被碰**。

---

## 6. 验收项逐条实测

| # | 验收项 | 实测结果 |
|---|---|---|
| 1 | 18 个名字都能通过 guest 自己的 `nvnDeviceGetProcAddress` 解析到，每次解析到同一入口，族表里每个名字只出现一次 | **绿**。契约第 1 节对族表 18 项逐个查：`resolved != NULL`、`resolved == lookup(...)`（同一入口）、`resolved == entry->fn`（设备交出的就是族表这一项）、两两比较确认**无重名**。`names:18` |
| 2 | 真实对象走一遍 lookup：填 builder → `nvnBufferInitialize` → 每个 getter 读回 builder 填的值 | **绿**。size=4096、memory offset=256、memory pool 指针相等、builder 侧 size/offset/pool/device 全部读回 |
| 3 | 0xAA 哨兵：buffer / builder 全程未被写 | **绿**。契约第 5 节（流程中）与第 12 节（全部跑完后）各查一次，builder/buffer 的字节全是 0xAA |
| 4 | 快照语义：Initialize 之后再改 builder，buffer 的 getter 仍返回旧值 | **绿**。改 builder 后 buffer 仍报 size=4096 / offset=256 / 同一个地址；builder 自己则报新值 8192 / 1024（证明确实改到了 builder） |
| 5 | 地址是真的：`GetAddress`/`Map` 落在本项目的内存池里（用 `nvn_memory_pool_resolve_address` 验），不是 0、不是编的 | **绿**。地址 = 池内存 + 256（数值直接比对），`resolve_address` 解析成功、解析回来是同一个指针、`available` ≥ buffer 大小；`Map` 与 `GetAddress` 同址 |
| 6 | 三变体自证：A exit 1 / B exit 0（谎报）/ C exit 1，脚本 exit 0 | **绿**。四个退出码实测 **0 / 1 / 0 / 1**，脚本 **exit 0**（见 5.2） |
| 7 | NDK 交叉编译 `--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` 过 | **绿**。NDK 26.1.10909125，两个 TU **都 exit 0**：`nvn_buffer.android.o`、`nvn_buffer_contract.android.o`，均 ELF 64-bit LSB relocatable, ARM aarch64。日志 `.local/logs/p-mm-31-android-crosscompile.log` |
| 8 | 报告 | 本文件 |

契约 TU 与族源文件另外用 `build_guest_process_contract.sh` 的严格档
（`-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -DNSO_WITH_CXX_GUARDS=1`）
单独编译，**都 exit 0**。

---

## 7. **没有**证明的东西（明确点名）

- **没有真机证据。** 这一族**从未在 RG477M 上跑过**。设备是总控的，本卡没有碰设备、没有 adb、
  没有碰 boot lock。促成这张卡的 transcript 是**改动之前**guest 自己解析出来的。
- **没有帧、没有像素、没有 draw 消费、没有标题画面、没有可玩、没有帧率。** 这一轮不产出画面。
  buffer 是状态对象：目前**没有任何东西消费** buffer 的参数 —— 没有 descriptor、没有 pipeline、
  没有 replay 读 buffer 的大小/偏移/池。
- **没有证明命令缓冲里的地址真能被 replay 解析回宿主内存。** 本卡只证明
  `nvn_memory_pool_resolve_address` 能解析这一族交出的地址（契约第 4 节）。**replay 那条路
  没有跑过** —— `nvn_command_replay.*` 不在本卡的写范围里。
- **签名未对本作证实。** vendored 头是**别的游戏**（Darksiders II）的 NVN 53.105 重建件。
  三个名字（`nvnBufferBuilderGetDevice`、`nvnBufferBuilderGetMemoryPool`、`nvnBufferGetDebugID`）
  **参考树根本没有实现**，它们的值**不声称与参考一致**（详见 2.1）。
- **`nvnBufferBuilderGetMemoryPool` 是一处明确偏离**：vendored 头 1238 写的是按值返回 256 字节
  的不透明 struct，本运行时返回**指针**。理由与影响见 2.1。
- **`nvnBufferGetDebugID` 的值是本运行时自己的 id**，不是真 NVN 会返回的值。
- **契约里的 device 是契约自己装的后端。** 内存池需要一个**真的存在的设备**（宿主构建没有
  Vulkan 实现 → 没有设备 → 没有池 → buffer 没有地址），所以契约装了一个只报告「有设备」的
  后端（`nvn_device_set_backend` 是运行时给的接缝）。**被测的是池、buffer 和地址算术**，
  **不是图形设备**。Android 上装的是真 Vulkan 后端，契约里的替身根本不编译。
- **`nvnBufferInitialize` 拒绝没有存储的 builder** 并答 false。参考树在同样情况下也答 0
  （`hle_nvn.c:1771`）。guest 的真实调用传的是它刚填好的 builder，差别是真的，日志里写着。
- **没有写的 buffer 的 getter 答 0 / NULL 并在日志里说「这个 buffer 不存在」**，参考树没有这种
  情况（它的 buffer 总是初始化过的）。
- **`Flush` / `Invalidate` 是空操作**，理由是存储是 guest 自己写的宿主内存，本来就一致。
  参考树同样把它们记成空操作。**没有做真实的 cache 维护**，因为这里没有需要维护的 cache。
- **没有证明 guest 的真实调用点形状。** 本卡只用了 transcript 里的**名字**，
  没有反汇编 guest 对这 18 个名字的**调用点**（参数怎么传、返回值怎么用）。
- **没有跑过仓库的 host gate。** `build_guest_process_contract.sh` 会自动 glob
  `nvn_*_contract.c`，所以这个契约 TU **会被**下一次集成捡起来；**但它需要总控先接线**
  （见第 8 节），否则链接会缺 `nvn_buffer.c` 的符号。

---

## 8. 总控需要接线的三处（同 P-MM-25 的形状）

1. **`src/graphics/nvn_device/nvn_device.c`**：一个族块，形状和其它族一样
   （`nvn_buffer_family_count()` / `nvn_buffer_family_entry_at()`）。**自证脚本用的那份接线
   副本就是这个块**（`.local/selfproof/run-*/nvn_device_with_buffer_family.c`）。
2. **`scripts/runtime/build_guest_process_contract.sh`**：`nvn_buffer.c` 进 source 列表。
   契约 TU **不用改**：构建脚本已经从文件名推导出开关 `--nvn-buffer`。
3. **`src/android/native/CMakeLists.txt`**：`"${FFRS_GUEST_NVN_DIR}/nvn_buffer.c"` 进 NVN
   source 列表，让 APK 带上这一族。

---

## 9. 证据路径

| 证据 | 路径 |
|---|---|
| 自证日志（四个退出码） | `.local/logs/p-mm-31-buffer-contract-selfproof.log` |
| 每次运行的独立目录（原始 stdout/stderr、变体源码、接线副本） | `.local/selfproof/run-<时间戳>-<pid>/` |
| NDK 交叉编译日志 | `.local/logs/p-mm-31-android-crosscompile.log` |
| NDK 产物 | `.local/nvn_buffer.android.o`、`.local/nvn_buffer_contract.android.o` |
| 名字来源（guest transcript） | `.local/logs/p-mm-33-guest-transcript.log` |
| 参考树 | `.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`、同目录 `nvn_protos.h`、`names.txt` |

---

## 10. Git

分支 `task/P-MM-31-buffer-family`，**未 push**。

| commit | 内容 |
|---|---|
| `9e6a707` | feat(graphics): add the NVN buffer object family (18 names) |
| `4af28da` | test(graphics): the contract of the NVN buffer object family |
| `bfb608c` | test(graphics): the self-proof of the NVN buffer contract |
| `199f357` | fix(graphics): keep the contract's stand-in backend off the device build |

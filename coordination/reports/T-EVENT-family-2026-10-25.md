# T-EVENT-family-2026-10-25 ｜ NVN event 族（11 名）｜ 工单 P-MM-46

> 基线：`work/rg477m-native` @ `3517404`
> 分支：`task/P-MM-46-event-family`，worktree `.worktrees/p-mm-46`
> 这一轮**不产出画面**：event 是 GPU 与 CPU 之间的信号，不是绘制。

---

## 0. 一句话

NVN event 族的 **11 个名字**已进运行时：状态全在宿主、按 guest 地址建键、用共享的
`nvn_object_table.h`（随对象数增长）、**guest 对象与 pool 内存一个字节没被写**（0xAA / 0xCC 哨兵成立）；
`Signal` 之后 `GetValue` 读得到已信号（`0x5a5a5a5a` → 加 7 → `0x5a5a5a61` → 换模式写 `0x11111111`）；
三变体自证四个退出码 **0 / 1 / 0 / 1**，其中 B 的假绿被**量出来**（stdout 谎报 0 错误，stderr 留真 `[check-fail]`）；
NDK `--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` 两个 TU 都过。

**协调者还要接线两处**（本卡的写范围之外）：`nvn_event.c` 进 `core_sources`，以及 `nvn_device.c` 里的族块。
**未接线时契约会失败**，这是实测，不是声称（见 §8）。

---

## 1. 交付物

| 文件 | 状态 | 行数 |
|---|---|---|
| `src/graphics/nvn_device/nvn_event.h` | 新建 | 92 |
| `src/graphics/nvn_device/nvn_event.c` | 新建 | 637 |
| `tests/native/nvn_event_contract.c` | 新建 | 490 |
| `scripts/runtime/make_nvn_event_variants.py` | 新建 | 125 |
| `scripts/runtime/selfproof_nvn_event.sh` | 新建 | 221 |
| `coordination/reports/T-EVENT-family-2026-10-25.md` | 新建 | 本文件 |

写范围严格受限：**没有碰** `nvn_device.c`、`nvn_queue.c`、`nvn_sync.*`、
`scripts/runtime/build_guest_process_contract.sh`、`src/android/native/CMakeLists.txt`、
`tests/native/guest_process_contract.c`。自证要用的"接线版"设备层是
`.local/selfproof/nvn_device_with_event_family.c` —— **副本**，树里的文件没动。

---

## 2. 名字清单与**逐个**的来源

这 11 个名字是 guest 自己解析出来的，顺序是设备 transcript 里的解析顺序
（`.local/logs/p-mm-44-guest-transcript.log:59003-59013`，trap slot 29–39），
与参考树自己的名字表 `.local/refs/nxnative/dm/gfx/nvn/names.txt:503-513` 一致：

```
nvnEventBuilderSetDefaults    nvnEventBuilderSetStorage     nvnEventBuilderGetStorage
nvnEventBuilderGetMemoryPool  nvnEventBuilderGetMemoryOffset
nvnEventInitialize            nvnEventFinalize              nvnEventGetValue
nvnEventSignal                nvnEventGetMemoryPool         nvnEventGetMemoryOffset
```

guest 侧的另外两份证据（只读）：
`evidence/nvn/guest_nvn_code_references.tsv:249-258`（guest 代码里每个名字的引用地址）、
`evidence/nvn/main_nvn_ids.txt:249-258`。

### 2.1 vendored 头**没有**的名字（5 个，点名）

vendored 头 `src/graphics/nvn/include/nvn/nvn.h` **只有 6 个** event 名字
（`PFNNVNEVENT*` 在 1747-1752）。另外 5 个名字：

| 名字 | vendored 头 | 参考树实现 | 参考树"无原型"名单 | 本卡签名出处 |
|---|---|---|---|---|
| `nvnEventBuilderGetStorage` | **无** | **无** | `nvn_protos.h:517` | 推导（见下） |
| `nvnEventBuilderGetMemoryPool` | **无** | **无** | `nvn_protos.h:518` | 推导（见下） |
| `nvnEventBuilderGetMemoryOffset` | **无** | **无** | `nvn_protos.h:519` | 推导（见下） |
| `nvnEventGetMemoryPool` | **无** | **无** | `nvn_protos.h:520` | 推导（见下） |
| `nvnEventGetMemoryOffset` | **无** | **无** | `nvn_protos.h:521` | 推导（见下） |

**这 5 个名字的 ABI 在整个工作区里没有任何权威出处**：vendored 头没有、参考树的
`nvn_protos.h` 只把它们列在"头里没有原型的名字"注释里（517-521 行）、
`hle_nvn.c` 没有实现（参考树对它们只会打 `[RED] NVN unimplemented function called` 并返回 0）、
guest 反汇编不在工作区里。因此签名**是推导的，不是被证实的**，理由逐条写明：

- **`nvnEventBuilderGetMemoryPool` → `void *`（指针）**
  同门同族：buffer 族的 `nvnBufferBuilderGetMemoryPool` 在 vendored 头里写的是"按值返回 pool"
  （`nvn.h:1238`），但 pool 是 256 字节不透明结构（`nvn.h:153-155`），按值没法交给 guest；
  而兄弟 getter `nvnBufferGetMemoryPool`（`nvn.h:1248`）返回的是**指针**。
  本 runtime 的 buffer 族就是按指针实现的，理由写在 `nvn_buffer.c:47-51`。这里跟它一致。
- **`nvnEventBuilderGetMemoryOffset` → `ptrdiff_t`**
  同上：`nvnBufferBuilderGetMemoryOffset`（`nvn.h:1239`）→ `ptrdiff_t`；
  参考树对 buffer 的实现 `hle_nvn.c:2023` 也返回 `ptrdiff_t`。
- **`nvnEventGetMemoryPool` → `void *`（指针）**
  `nvnBufferGetMemoryPool`（`nvn.h:1248`）→ `NVNmemoryPool*`；参考树 `hle_nvn.c:1782` 同。
- **`nvnEventGetMemoryOffset` → `ptrdiff_t`**
  `nvnBufferGetMemoryOffset`（`nvn.h:1249`）→ `ptrdiff_t`；参考树 `hle_nvn.c:1783` 同。
- **`nvnEventBuilderGetStorage` → `void *`（返回 pool，单寄存器）**
  这是唯一一个**没有同门 precedent** 的名字。event builder 的"storage"就是
  `SetStorage(pool, offset)` 写下的那一对（`nvn.h:1748`）。两个半边已经由上面两个 getter 给出，
  能塞进**一个返回寄存器**的那一半是 pool。**用 out-parameter 写回那一对，等于往 guest 内存里写**，
  而这个 runtime 的铁律就是不写 guest 内存；单寄存器返回是唯一不越界的形状。
  这是推导，不是被证实的 —— 报告点名，Coordinator 若有别的证据可以推翻它。

### 2.2 vendored 头**有**的 6 个名字：签名出处 + 参考树行号 + 实测行为

vendored 头 = **别的游戏（Darksiders II）的 DWARF 重建件**，NVN 53.105
（`nvn.h:25-28` 自己写着）。**签名未对本作证实。**

（附带发现：**本项目的 vendored 副本与参考树的 `dm/gfx/nvn/include/nvn.h` 有一处真正的差异**，
其余 540 行 `diff` 全是缩进空白。本项目那份第 1356 行是

```c
typedef NVNmemoryPool (*PFNNVNTEXTUREGETMEMORYPOOLPROC)(const NVNtexture*);   /* 按值 */
```

参考树那份是

```c
typedef NVNmemoryPool* (*PFNNVNTEXTUREGETMEMORYPOOLPROC)(const NVNtexture*);  /* 指针 */
```

**这一处不在 event 族里**（是 texture 族的 `nvnTextureGetMemoryPool`），本卡的 11 个名字不受影响；
但它正好说明这个头里的"按值返回 pool"是重建件的旧写法 —— 也正是 buffer 族把
`GetMemoryPool` 改成指针的理由（`nvn_buffer.c:47-51`）。event 族跟着走指针。）

| 名字 | 签名出处（vendored `nvn.h`） | 参考树实现行号 | 参考树语义 | 本卡实测行为 |
|---|---|---|---|---|
| `nvnEventBuilderSetDefaults` | `:1747` `(NVNeventBuilder*)` | `hle_nvn.c:644` | `memset(b,0,sizeof(ev_builder_t))` | 清掉宿主侧 builder 的 pool（→NULL）与 offset（→0）；guest 的 builder 字节不变 |
| `nvnEventBuilderSetStorage` | `:1748` `(NVNeventBuilder*, const NVNmemoryPool*, int64_t)` | `hle_nvn.c:645` | 记下 `(pool, off)` | 宿主侧记下 pool 与 int64 offset；guest 的 builder 字节不变 |
| `nvnEventInitialize` | `:1749` `(NVNevent*, const NVNeventBuilder*) -> NVNboolean` | `hle_nvn.c:646-650` | pool 无内存则 `return 0` 并打 `[RED] EventInitialize has no storage`；否则把 builder 的描述抄进 event | 无 pool / pool 无内存 / builder 未准备 → **返回 0**；否则**返回 1**并把 `(pool, offset)` 快照进 event |
| `nvnEventFinalize` | `:1750` `(NVNevent*)` | `hle_nvn.c:651` | `magic = 0` | 释放宿主侧记录（槽位回收）；**不写 guest 的 event 对象**；之后 getter 答 0 / NULL |
| `nvnEventGetValue` | `:1751` `(const NVNevent*) -> uint32_t` | `hle_nvn.c:652` | 读 guest pool 内存里的那个值 | 读宿主侧保存的值：写信号后 `0x5a5a5a5a`，加信号后 `0x5a5a5a61`，换模式后 `0x11111111` |
| `nvnEventSignal` | `:1752` `(NVNevent*, NVNeventSignalMode, uint32_t)` | `hle_nvn.c:653-654` | mode == `ADD` 则 `+= v`，**其它模式一律 `= v`** | 同上：mode 0 写、mode 1 加、mode 3 也写 |

模式常量来自 vendored 头 `nvn.h:661-665`：`WRITE=0x0`、`ADD=0x1`、`LARGE=0x7FFFFFFF`。

**与参考树的一处刻意不同（点名）**：参考树把 event 的**值放在 guest 的 pool 内存里**
（`hle_nvn.c:649`：`volatile uint32_t *` 指向 `pool->mem + off`），本 runtime **不写 guest 内存**，
所以值存在宿主侧。代价与后果写在 §3。

---

## 3. 状态放在宿主（铁律）

- 状态在宿主，键是 guest 给的地址；**不写** guest 的 event 对象、builder 对象、也不写 guest 的 pool 内存。
- 表用共享的 `src/graphics/nvn_device/nvn_object_table.h`（`e8518f7` 引入），**随对象数增长**，
  没有另写定长小表。上限 `NVN_EVENT_MAX_EVENTS 65536` / `NVN_EVENT_MAX_BUILDERS 4096` 是**上限不是表长**。
- 契约用三组哨兵证明：guest 的 event 对象 `0xAA`、guest 的 builder 对象 `0xAA`、
  guest 的 pool 内存 `0xCC`（信号前后都不变）。

**与参考树的差异，代价说清楚**：参考树把值写在 guest 的 pool 内存（`hle_nvn.c:649`），
所以 guest 若自己读那块内存能读到信号值；本 runtime 读不到 —— 只能通过 `nvnEventGetValue` 读。
GPU 侧写值的那条路（`nvnCommandBufferSignalEvent`，参考树 `hle_nvn.c:819-824`，
它把"值的指针"塞进命令缓冲让 replay 去写）在参考树里已经存在，在本 runtime 里**还没有**。
等命令缓冲那条路落地时，值必须搬回 pool 内存 —— 那是后一张卡的事，**本卡不假装它已经在了**。

---

## 4. event 与 sync 的差别（参考树自己查的结果）

两者的载体、方向、等待方式都不同。以下全部来自参考树（MIT，同款游戏）：

| | **sync** | **event** |
|---|---|---|
| 参考树结构 | `sync_t { uint32_t magic, fenced; uint64_t fence; }`（`nvn_obj.h:56`） | `ev_t { uint32_t magic; volatile uint32_t *val; }`（`nvn_obj.h:58`） |
| 装的是什么 | 一个 **fence 值** + 一个"是否已 fence"标志 | 一个 **32 位值的地址** |
| 谁来填 | `nvnQueueFenceSync`（`hle_nvn.c:631`）记下最近一次 submit 的 fence 值 | `nvnEventSignal`（`hle_nvn.c:653-654`）写值或加值 |
| 等待方式 | `nvnSyncWait`（`hle_nvn.c:638-643`）**阻塞等 GPU** 走到那个 fence 值，返回 `ALREADY_SIGNALED` / `CONDITION_SATISFIED` / `TIMEOUT_EXPIRED` | 参考树**没有对 event 的等待**；`nvnCommandBufferWaitEvent` 在参考树的 no-op 名单里（`hle_nvn.c:1168`） |
| 命令缓冲侧 | `nvnCommandBufferFenceSync`（`hle_nvn.c:826-829`）把 fence 挂到提交上 | `nvnCommandBufferSignalEvent`（`hle_nvn.c:819-824`）把"值的指针+模式+值"塞进命令缓冲 |

一句话：**sync 是 fence/wait 的载体（"GPU 干到哪一步了"），event 是 signal/value 的载体
（"某个计数器现在是多少"）**。sync 会被等待，event 只会被写和被读。
所以 P-MM-45 的 sync 族与本卡的 event 族是两件事，本卡没有复用 sync 的任何东西。

---

## 5. 族契约与三变体自证

### 5.1 契约

`tests/native/nvn_event_contract.c`，自带 `main` 与开关 `--nvn-event`
（构建脚本按文件名推导：`nvn_event_contract.c` → `--nvn-event`，`build_guest_process_contract.sh:111-120`）。

契约**通过 guest 自己的 `nvnDeviceGetProcAddress` 解析 11 个名字**，不直接调实现函数：
解析不到、解析到不同入口、名字重名，都会失败。

期望值**写死在契约里**（`TEST_VALUE_AFTER_WRITE 0x5a5a5a5a`、
`TEST_VALUE_AFTER_ADD 0x5a5a5a61`、`TEST_VALUE_AFTER_OTHER 0x11111111`、
`TEST_MEMORY_OFFSET 256`、`TEST_OTHER_OFFSET 1024`），**不从实现的常量派生**。
没有恒真断言，没有硬编码 0 错误数（JSON 里报的是真的 `errors`）。

> 契约在写的时候**抓到了我自己写错的算术**：`0x5a5a5a5a + 7` 是 `0x5a5a5a61`，我先写成了 `0x5a5a5a5b`，
> 契约第一次跑就红了。这条记在这里，因为它正是"期望写死"的价值。

### 5.2 三变体（脚本自己判四个退出码）

`scripts/runtime/selfproof_nvn_event.sh` + `scripts/runtime/make_nvn_event_variants.py`。
**四个退出码由脚本自己判**，实测结果：

| 变体 | 改了什么 | 期望退出码 | **实测** |
|---|---|---|---|
| baseline | 契约与实现都不动 | 0 | **0** ✅ |
| A | 改坏一处**期望**（`0x5a5a5a5a` → `0x12345678`），实现不动、闸门留着 | 1 | **1** ✅ stderr 有真 `[check-fail] nvn-event-value-after-a-write-signal actual=0x5a5a5a5a expected=0x12345678` |
| B | 同一处期望改坏 **+ 去掉最终闸门 + 硬编码错误数为 0** | 0 | **0** ✅ **stdout 仍谎报 0**（`{"nvn_event_errors":0,...}`），**stderr 留真 `[check-fail]`** |
| C | 契约不动，**只改桩**（桩不记录信号的值） | 1 | **1** ✅ 6 条真 `[check-fail]` |

**B 的假绿是被量出来的，不只是被跑出来的**：脚本额外检查三件事并写进日志 ——
进程退出码是 0、stdout 里仍然出现 `"nvn_event_errors":0`、stderr 里仍然有 `[check-fail]`；
三条里少一条，脚本自己失败（退出 1）。日志实测：

```
stdout_claims_zero_errors=1
stderr_holds_the_real_failure=1
the false green is measured: exit 0 with a lie on stdout and the truth on stderr
```

baseline 的 stdout（真的绿）：

```json
{"nvn_event_errors":0,"names":11,"initialized":2,"finalized":1,"signals":5,"gets":27,"refused":9,"live_events":1,"live_builders":2}
```

---

## 6. 验收项逐条实测

| # | 验收项 | 实测 |
|---|---|---|
| 1 | 11 个名字经 guest 自己的 `nvnDeviceGetProcAddress` 解析到、每次同一入口、互不重名 | ✅ 契约第 1 段：`nvn-event-family-has-eleven-names`=11、`name-resolves` / `name-is-stable` / `name-matches-the-table` / `family-holds-each-name-once` 全过 |
| 2 | Initialize 之后 builder 的值是**快照** | ✅ 改 builder 后 `snapshot-keeps-the-memory-offset`=256、`snapshot-keeps-the-memory-pool`=pool、`snapshot-keeps-the-value` 不变，而 builder 自己变成 1024 |
| 2 | guest 对象**一个字节没被改**（0xAA 哨兵） | ✅ event/builder 对象全 `0xAA`（前后各查一次），pool 内存 `0xCC` 也不变 |
| 2 | **Signal 之后 GetValue 必须读得到已信号**（具体期望值） | ✅ `0x5a5a5a5a` → 加 7 → `0x5a5a5a61` → 换模式写 → `0x11111111`，全是写死的数 |
| 3 | 族契约报错数为 0 | ✅ 见 §5.2 baseline |
| 3 | 构建脚本自动发现并推导开关 | ✅ `nvn_*_contract.c` glob 命中 `nvn_event_contract.c` → `--nvn-event`，与契约 `main` 的开关一致 |
| 3 | 若契约因缺链接而失败 | ⚠️ **会失败，因为协调者还没接线** —— 见 §8，实测输出在 `.local/logs/p-mm-46-event-not-wired.log` |
| 4 | 三变体自证四个退出码 | ✅ 0 / 1 / 0 / 1，见 §5.2；B 的假绿被量出来 |
| 5 | NDK `--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` | ✅ `nvn_event.c` 与 `nvn_event_contract.c` 两个 TU 都 exit 0，产物是 `ELF 64-bit LSB relocatable, ARM aarch64` |
| 6 | 报告逐项列出签名出处 + 参考树行号 + 实测行为 | ✅ §2.2 表；event/sync 的差别见 §4 |

---

## 7. **没有**证明的东西（明确点名）

1. **5 个名字的 ABI 没有被证实**。`nvnEventBuilderGetStorage`、`nvnEventBuilderGetMemoryPool`、
   `nvnEventBuilderGetMemoryOffset`、`nvnEventGetMemoryPool`、`nvnEventGetMemoryOffset`
   在整个工作区里没有原型、没有参考树实现、没有 guest 反汇编。签名是**推导**的（理由见 §2.1）。
   真实设备上跑一次 guest 才能定。
2. **没有证明 guest 的调用形状**。本卡的契约用自己写的函数指针类型调这 11 个名字；
   guest 真实调用时的寄存器/栈布局（尤其 `nvnEventBuilderGetStorage` 到底返回一个值还是两个 out 参数）
   **没有被观测**。
3. **没有证明真机行为**。本卡不需要真机，设备由协调者持锁，**没有碰设备**。
   没有在 RG477M 上跑过一行 guest 代码。
4. **没有证明画面**。这一轮**不产出画面**，event 不画任何东西；replay 仍只解码、不发 Vulkan 命令。
5. **没有证明 GPU 侧写值的那条路**。`nvnCommandBufferSignalEvent` / `nvnCommandBufferWaitEvent`
   不在本卡范围（不在 11 个名字里），参考树里 `WaitEvent` 还是 no-op。本卡的值只在 CPU 侧。
6. **没有证明 event 的值在 guest 的 pool 内存里**。参考树是放在那里的（`hle_nvn.c:649`），
   本 runtime **故意不放**（§3）。这条差异会等到 GPU 路径落地时才算清。
7. **契约没有证明有真实图形设备**。契约装的是自己那个"只报告 device、没有图形硬件"的 backend
   （沿用 buffer 契约的做法）。它证明的是 event 族的逻辑，不是硬件。
8. **没有跑协调者的完整构建脚本**。`scripts/runtime/build_guest_process_contract.sh` 没跑全量
   （它需要协调者接线后才通得过本族）；本卡跑的是自证脚本，它自己建全部所需对象。

---

## 8. 总控需要接线的两处（本卡的写范围之外）

1. **`scripts/runtime/build_guest_process_contract.sh`**：把 `nvn_device/'nvn_event.c'` 加进 `core_sources`
   （第 23 行那一长串里）。
   不加 → 契约链接失败，实测缺三个符号：
   ```
   Undefined symbols for architecture arm64:
     "_nvn_event_family_count", referenced from:
     "_nvn_event_family_entry_at", referenced from:
     "_nvn_event_get_stats", referenced from:
   ```
2. **`src/graphics/nvn_device/nvn_device.c`**：加 event 族块（形状与其他族一样），
   并 `#include "nvn_event.h"`。
   不加 → 11 个名字解析到 **trap slot**，契约 11 条 `name-matches-the-table` 全红，
   运行在第一个 trap 调用上以 **exit 97** 停下（运行时拒绝假装 trap 调用成功）。

两处的实测输出：`.local/logs/p-mm-46-event-not-wired.log`。
接线用的族块原文可以直接从 `.local/selfproof/nvn_device_with_event_family.c` 取。

---

## 9. 证据路径

| 证据 | 路径 |
|---|---|
| 自证总日志（含四个退出码与假绿的量法） | `.local/logs/p-mm-46-event-contract-selfproof.log` |
| 自证本次运行的原始 stdout/stderr | `.local/selfproof/run-20261025-094007-36196/`（四个变体各一份 `.stdout` / `.stderr`） |
| 未接线的两种失败 | `.local/logs/p-mm-46-event-not-wired.log` |
| NDK 产物 | `.local/build-nvn-event-ndk/nvn_event.ndk.o`、`nvn_event_contract.ndk.o`（ARM aarch64 ELF） |
| 三个变体与接线版设备层 | `.local/selfproof/nvn_event_broken_fixed.c`、`nvn_event_broken_prefix.c`、`nvn_event_stub_no_signal_value.c`、`nvn_device_with_event_family.c` |
| guest 解析顺序 | `.local/logs/p-mm-44-guest-transcript.log:59003-59013` |
| 参考树 | `.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c:644-654`、`:819-824`、`:1168`；`nvn_obj.h:56-58`；`nvn_protos.h:517-521`；`names.txt:503-513`；`nvn_thunks.S:6959-6969` |
| vendored 头 | `src/graphics/nvn/include/nvn/nvn.h:1747-1752`（事件类型 `:202-208`、模式 `:658-665`、`:153-155`） |

`.local/` 不进 Git（按 AGENTS.md）。

---

## 10. Git

- 分支：`task/P-MM-46-event-family`
- worktree：`.worktrees/p-mm-46`
- 基线：`3517404`
- commit：
  - `1439cb5` feat(nvn): the event family of eleven names, on the host
  - `ba2cd23` test(nvn): the contract of the event family, and the three variants
  - `9b26461` docs(coordination): the report of the event family
  - `（本条）docs(coordination): the evidence paths of the report`
- 自证跑在**干净的 HEAD `9b26461`** 上（自证日志里 `dirty:` 为空）。
- **没有 push。**

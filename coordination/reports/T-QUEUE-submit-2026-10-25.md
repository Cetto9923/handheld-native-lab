# T-QUEUE-submit｜队列提交/呈现族（5 名）—— replay 的接缝

> 分支：`task/P-MM-32-queue-submit`（基于 `work/rg477m-native` @ `6d6cbb1`）
> 工单：`coordination/prompts/P-MM-32-queue-submit.md`
> 日期：2026-10-25
> 设备：**本卡不需要真机**。没有 adb，没有 boot lock，没有碰设备。

---

## 0. 一句话

五个名字都进了族表，**`nvnQueueSubmitCommands` 把 guest 交上来的 handle 接到 replay 上**：
取命令包 → 调 `nvn_command_replay_run()` → 记下包数/记录数/拒绝数。
**replay 目前只解码、不发 Vulkan 命令**，日志写的是「包已读，命令未提交」。
呈现路径没接上，present 只记录请求，**不谎报成功**。

---

## 1. 名字清单与签名来源

| 名字 | 本实现的签名 | 签名来源 | 参考树 |
|---|---|---|---|
| `nvnQueueSubmitCommands` | `void (void *queue, int count, const uint64_t *handles)` | vendored `nvn.h:1171` `PFNNVNQUEUESUBMITCOMMANDSPROC` = `void (*)(NVNqueue*, int, const NVNcommandHandle*)`；`NVNcommandHandle` = `uint64_t`（`nvn.h:40`） | **有**，L1132 |
| `nvnQueuePresentTexture` | `void (void *queue, void *window, int index)` | vendored `nvn.h:1174` `PFNNVNQUEUEPRESENTTEXTUREPROC` = `void (*)(NVNqueue*, NVNwindow*, int)` | **有**，L1630 |
| `nvnQueueFenceSync` | `void (void *queue, void *sync, int condition, int flags)` | vendored `nvn.h:1744` `PFNNVNQUEUEFENCESYNCPROC` = `void (*)(NVNqueue*, NVNsync*, NVNsyncCondition, int)` | **有**，L631 |
| `nvnQueueWaitSync` | `int (void *queue, const void *sync)` | vendored `nvn.h:1746` `PFNNVNQUEUEWAITSYNCPROC` = `NVNboolean (*)(NVNqueue*, const NVNsync*)` | **有**，L634 |
| `nvnQueueBuilderGetMemory` | `void *(const void *builder)` | **本卡没有可确认的签名来源 —— 见下** | **没有** |

### 1.1 点名：`nvnQueueBuilderGetMemory` 在参考树和 vendored 头里都没有

工单第 2 节让「参考树里自己找」。实际结果是：

- 参考树 `.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`（MIT，revision `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3`）：
  `grep -n "nvnQueueBuilderGetMemory"` → **零命中**。参考树里有的是 `nvnQueueBuilderGetQueueMemorySize`（L669），
  以及 `nvnBufferBuilderGetMemoryOffset`（L2023，跟这个不是一回事）。
- vendored `src/graphics/nvn/include/nvn/nvn.h`：`PFNNVNQUEUEBUILDERGETQUEUEMEMORYSIZEPROC`（L1165）有，
  `nvnQueueBuilderGetMemory` **没有**。

**guest 确实要这个名字**：设备 transcript `.local/logs/p-mm-33-guest-transcript.log:57654`
记着 `nvnDeviceGetProcAddress: "nvnQueueBuilderGetMemory" -> trap slot 2 (not implemented yet)`，
静态证据 `evidence/nvn/guest_nvn_code_references.tsv:324` 也记着它。

**本卡的处理**：按**本运行时的既有命名惯例**回答。本运行时已经把这族里
`nvnQueueBuilderGetMemorySize` 和 `nvnQueueBuilderGetQueueMemorySize` 当成同一个问题答了两遍，
所以 `nvnQueueBuilderGetMemory` 按 queue memory 的取法回答：返回
`nvnQueueBuilderSetQueueMemory` 交进来的那块内存。

**这是推断，不是证实。** 参考树里同类的 `nvnMemoryPoolBuilderGetMemory`
（vendored `nvn.h:1200`，`void (*)(const NVNmemoryPoolBuilder*)`）也是返回内存指针而不是大小，
这跟本卡的取法一致，但**同族同名不等于同一函数**，签名仍未对本作证实。

---

## 2. 接缝怎么接的

`nvnQueueSubmitCommands(queue, count, handles)`，逐个 handle：

1. **`nvn_command_buffer_get_package(handle, &package)`** 取那个不可变的命令包。
   - **取不到 ⇒ 点名拒绝并计数**（`packages_refused++`，stderr 打出是哪个 handle）。
     **不静默跳过，不假装提交过。**
   - 取到了 ⇒ **`nvn_command_replay_run(handle)`**，并把**返回的记录数 / 拒绝数**记进日志。
2. **本卡不在这里解释任何命令字节。** 字节的解读是 replay 的活；
   本函数只看 `package.record_count`（包里有几条记录）这一个计数，不碰 opcode。
3. **不向 Vulkan 提交任何东西。** replay 目前只解码。日志明写「包已读，命令未提交」。
4. 计数：`packages_handed_to_replay` / `packages_refused` / `records_read` / `replay_refusals`。

**参考树对照**：`.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c` L1132 处，
参考树是**执行**命令包 —— `mtl_exec((const uint8_t *)h[i])`，**`h[i]` 就是命令缓冲交出来的 handle**。
本运行时的 replay **读**那个包而不是执行它。这正是接缝所在。

### 2.1 提交锁（本卡加的，理由记在这里）

参考树在 `nvnQueueSubmitCommands` 上有一把 `g_sub_mu`（L1129），注释写明为什么：

> *One submission at a time … Fire Emblem: Three Houses initializes two queues and submits on both
> from two threads; the second submission arrived while the first was replaying and the host
> stopped with "submit re-entered" at the ninth frame.*

**本项目有同一个隐患**：replay 每次 run 开头会 `memset(&s_state, 0, ...)`，
`s_state` 是**全局的**「当前命令流的状态」（绑过的 pool、program、render target）。
两个提交重叠时，一个会清掉另一个正在跑的状态。

所以本卡加了 `s_submit_lock`，**锁住 replay 的调用本身**（不只是计数器）。
NVN 只通过 sync 给两个队列排序，所以一个接一个地 replay 是合法的顺序之一。

**没有**把 `s_queue_lock` 套在外面：`s_queue_lock` 护的是本族自己的计数器，
`s_submit_lock` 护的是 replay 的调用；两者不嵌套，不会有锁序问题。

**这个锁不是猜的，是量出来的。** 契约里 §5.5b 用两个线程提交两个**绑不同 texture pool** 的包：
两个 run 重叠时，一个会清掉另一个的 pool 绑定，另一个的 texture 就被记成
`refused_foreign_pool`（replay 里的名字是 *the handle is of a pool that is not bound*）。

实测（宿主，同一个契约二进制）：

| 构建 | 结果 |
|---|---|
| **有**提交锁 | **0 / 30** 次运行出错 |
| **没有**提交锁 | **12 / 12** 次运行出错（错误数 2–17） |

这个检查在加锁之前是**没有牙齿的**：原来只数包，去掉锁照样过（12 次全绿）。
现在它测的是「两个线程有没有互相清掉对方的 pool 绑定」，
去掉锁就红，加锁就绿。

**没有写 guest 内存**：handle 只过 recorder 的注册表，从不解引用；
队列对象、命令缓冲对象、handle 数组全部保持 0xAA 哨兵。

---

## 3. sync 族和 event 族还不存在 ⇒ sync 状态先放本族自己的表

`nvnQueueFenceSync` / `nvnQueueWaitSync` **不依赖 sync 族**（那还没写）。
sync 对象的状态放在 **本族自己的表** `s_syncs[NVN_QUEUE_MAX_SYNCS]`，**按 guest 地址建键**
（`sync_find(void *sync, int create)`，与本族其它表同一形态）。

**点名：这张表将来要和 sync 族合并。** 表的注释里写明了这一点：
`nvn_queue.h` 的 `nvn_queue_fence_sync` 声明上写着
*"The sync family does not exist yet … That table is to be merged into the sync family when that family arrives."*

参考树行为对照（`hle_nvn.c` L631 / L634）：
- `nvnQueueFenceSync` 把 sync 标记为已 fence，并记下当时最后一个 fence。
  本实现记下的是**本运行时的提交计数器**（`packages_handed_to_replay`），
  **不是 GPU fence** —— 宿主还没有 fence 可发，因为 replay 不提交任何命令。日志明说了这一点。
- `nvnQueueWaitSync` **永远返回 1**；若该 sync 还没有 fence，**只记一次**日志
  （*the sync object has no fence yet, the host does not wait*）。
  本实现的「只记一次」是**进程级**的静态标志（`s_wait_without_fence_logged`），跟参考树的 `static int once` 一样。

---

## 4. 本卡不真的 present

`nvnQueuePresentTexture`：
- **记录请求**（`presents_requested++`，日志打出是哪个窗口、哪个 index）；
- **点名**说呈现路径还没接上（*the present path is not connected yet and nothing was presented*）；
- **不谎报成功**：`presents_performed` 这个计数器**恒为 0**，且契约专门断言它。

呈现路径在 P-MM-28 手里。**本卡没有 present 任何东西。**

---

## 5. 验收项逐条实测

### 5.1 五个名字解析、稳定、不重名 ✅

契约用 **guest 自己的** `nvnDeviceGetProcAddress` 解析（经 `nvnBootstrapLoader` 拿到），
每个名字：
- 解析到非 NULL；
- **再解析一次得到同一个入口**（`lookup(NULL,name) == lookup(NULL,name)`）；
- **族表里每个名字只出现一次**（走 `nvn_queue_family_count()` / `nvn_queue_family_entry_at()` 数命中数）；
- 解析到的入口**就是族表里那一条**（dispatch 与族表不能漂移）。

### 5.2 接缝接通并被契约验到 ✅ —— **用 replay 的包数/记录数，不是桩数**

契约走的是真链路：真 `nvnCommandBufferInitialize` → `AddCommandMemory` → `BeginRecording` →
`ClearColor` + `BindVertexBuffer` → `EndRecording` 拿到**真 handle** →
`nvnQueueSubmitCommands` → 之后查 **`nvn_command_replay_get_stats()`**：

| 断言 | 实测 |
|---|---|
| `after.packages - before.packages == 1` | ✅ |
| `after.records > before.records` | ✅（3 条记录） |
| `after.clears >= 1` | ✅ |
| `queue_after.packages_handed_to_replay` +1 | ✅ |
| `queue_after.records_read` 增长 | ✅（+3） |

**这个断言被自证变体 C 反证过**：桩把 replay 调用去掉后，replay 读到 **0 个包**，
契约立刻红（4 条 `[check-fail]`）。所以它不是恒真断言。

### 5.3 未知 handle 点名拒绝并计数 ✅

`0x5eed5eed5eed5eed`：
- `packages_refused` **+1**（stderr 点名是哪个 handle）；
- `packages_handed_to_replay` **+0**（**不算作提交过**）；
- replay 的 `packages` **+0**、`records` **+0**（**没让 replay 去读一个不存在的包**）。

### 5.4 0xAA 哨兵成立 ✅

契约用 `memset(0xaa)` 造了这些 guest 对象，事后逐字节检查**没有一个字节被改**：
队列对象、命令缓冲对象、**handle 数组**（逐字节 `memcmp` 前后快照）、
sync 对象、窗口对象、builder 对象。

### 5.5 `nvnQueueWaitSync` 永远返回 1，未 fence 时只记一次 ✅

- 没 fence 的 sync → 返回 **1**；
- 第一次这样的等待 → `waits_without_fence_logged` **+1**（日志出现一次）；
- **第二次**等待同一个 sync、**以及**等待另一个没 fence 的 sync → `waits_without_fence_logged` **+0**
  （**不再记日志**，跟参考树的 `static int once` 一样）；
- 已 fence 的 sync → 返回 **1**，且**不计入** `waits_without_fence`。

### 5.5b 两个线程同时提交（**本卡加的**，因为参考树记了这个故障）✅

契约让**两个线程**各提交一个包，每个包**先绑自己的 texture pool、再绑自己的 texture**。
两个 run 重叠时，一个会清掉另一个的 pool 绑定，于是另一个的 texture 被记成
`refused_foreign_pool`。

断言：**`refused_foreign_pool == 0`**，重复 200 轮。

实测（同一个契约二进制，只换掉锁）：

| 构建 | 结果 |
|---|---|
| **有**提交锁 | **0 / 30** 次运行出错（30 次全是 `errors:0`） |
| **没有**提交锁 | **12 / 12** 次运行出错（错误数 2–17） |

**所以这个断言不是恒真的**：去掉锁它就红。

（这个检查在第一版里只数包数，去掉锁照样绿，所以那时它**不算检查**。
改成查 `refused_foreign_pool` 之后才有牙齿。）

### 5.6 `nvnQueuePresentTexture` 不谎报成功 ✅

`presents_requested` +1、`presents_performed` **恒为 0**、日志明写没接上。

### 5.7 三变体自证 A红 / B假绿 / C红，脚本 exit 0 ✅

脚本 `scripts/runtime/selfproof_nvn_queue_submit.sh` **自己判四个退出码**，不靠人读。

| 变体 | 做法 | exit | stdout | stderr | 判定 |
|---|---|---|---|---|---|
| baseline | 不改 | **0** | `errors:0` | 静默 | ✅ as expected |
| **A** | 改坏一个期望值 | **1** | `errors:1` | 1 条真 `[check-fail]` | ✅ as expected |
| **B** | 同处改坏 + 去最终 CHECK + 错误数硬编码 0 | **0** | **`errors:0`（谎报）** | **1 条真 `[check-fail]`** | ✅ as expected（**假绿量出来了**） |
| **C** | 契约一字不改，只把桩改坏（不调 replay） | **1** | `errors:4` | 4 条真 `[check-fail]` | ✅ as expected |

脚本最终 `failures: 0`，**自身 exit 0**。

- **B 的假绿证据**（stdout 与 stderr 同时看）：
  stdout = `{"nvn_queue_submit_errors":0,...}`，
  stderr = `[check-fail] queue-submit-the-replay-read-one-more-package actual=0x1 expected=0x2`。
  **退出码 0、报告说没错、stderr 里躺着真的失败** —— 这就是假绿，已量出来。
- **C 的证据**：桩不调 replay ⇒ replay 读到 **0 个包**，`records_read` 从 3 掉到 0，
  4 条 `[check-fail]` 指向接缝本身。

### 5.8 NDK 交叉编译 `--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` ✅

用 `.local/minimal-ndk/toolchain`（官方 NDK r27d 的最小切片，
`validated-members.json` 校验来源与成员 CRC32）：
- `src/graphics/nvn_device/nvn_queue.c` → **rc=0**
- `tests/native/nvn_queue_submit_contract.c` → **rc=0**
- 产物：`ELF 64-bit LSB relocatable, ARM aarch64`，`file` 确认。

**另外跑了规范构建脚本** `scripts/runtime/build_guest_process_contract.sh`：
它按文件名自动发现 `tests/native/nvn_*_contract.c`，**自动推导出开关 `--nvn-queue-submit`**
（**没有改构建脚本**），全流程 **BUILD EXIT=0**，
manifest 里 `family_contracts` 含 `nvn_queue_submit_contract`，
报告为 `{"nvn_queue_submit_errors": 0, ...}`，`dirty: ''`（构建时树是干净的）。

### 5.9 报告 ✅

本文档。

---

## 6. 证据路径

| 内容 | 路径 |
|---|---|
| 分支 | `task/P-MM-32-queue-submit`（worktree `.worktrees/p-mm-32`） |
| 实现 | `src/graphics/nvn_device/nvn_queue.{h,c}` |
| 契约 | `tests/native/nvn_queue_submit_contract.c` |
| 自证脚本 | `scripts/runtime/selfproof_nvn_queue_submit.sh` |
| 变体生成 | `scripts/runtime/make_nvn_queue_submit_variants.py` |
| 自证日志 | **主检出** `.local/logs/p-mm-32-queue-submit-selfproof.log` |
| 变体 stdout/stderr | **主检出** `.local/selfproof/p-mm-32-<时间戳>/`（**每次运行独立目录**） |
| 规范构建 manifest | **主检出** `.local/selfproof/p-mm-32-build-manifest.json` |
| 规范构建产物 | `.worktrees/p-mm-32/.local/build-p-mm-43/` |
| 并发竞态探针（探索用，不在交付里） | `.worktrees/p-mm-32/.local/probe/queue_submit_race_probe.c` |

---

## 7. **没有**证明的东西（点名）

1. **没有证明任何一个命令到达了 GPU。** replay 只解码，**不发 Vulkan 命令**。
   本卡产出的是「包已读」，不是「画出来了」。
2. **没有证明 present 有效。** 呈现路径在 P-MM-28 手里；本卡只记录请求，
   `presents_performed` 恒为 0。
3. **没有标题画面、没有可玩、没有帧、没有帧率。** 本卡没有画面输出。
4. **没有在真机上跑过。** 本卡不需要设备；**没有 adb、没有 boot lock、没有碰设备**。
5. **没有证明 guest 的 sync 对象布局。** sync 状态在宿主，按 guest 地址建键，
   **没有写 guest 的 sync 对象**（0xAA 哨兵抓到了）。sync 族合并之后这块要再验一次。
6. **`nvnQueueBuilderGetMemory` 的签名未证实**（见 §1.1）：参考树和 vendored 头里都没有这个函数，
   本实现是**按本运行时惯例推断**的。
7. **没有证明 `nvnQueueFenceSync` 记的 fence 值有 GPU 意义** —— 记的是提交计数器，
   因为宿主还没有 fence 可发。
8. **没有证明「并发提交」在真机上不出问题。** 本卡在宿主上加了提交锁
   （`s_submit_lock`，理由见 §2.1），宿主上量过（§5.5b），
   但**没有在真机上用两个线程两个队列跑过**。参考树记录过这个故障模式
   （*submit re-entered*，L1129）。
9. **没有证明契约在别的构建配置下也过** —— 只在宿主（`-O2`）和 android34（`-O2`）两个配置下验过。
10. **没有碰 guest 对象内存** —— 这一条是**证明了的**（0xAA 哨兵），列在这里是为了区分。

---

## 8. 写范围核对

**只改了工单第 5 节的六个文件**：

```
src/graphics/nvn_device/nvn_queue.h          ✓
src/graphics/nvn_device/nvn_queue.c          ✓
tests/native/nvn_queue_submit_contract.c     ✓
scripts/runtime/selfproof_nvn_queue_submit.sh ✓
scripts/runtime/make_nvn_queue_submit_variants.py ✓
coordination/reports/T-QUEUE-submit-2026-10-25.md ✓
```

**没有改**第 6 节保留的文件（`nvn_device.c`、`nvn_command_buffer.*`、`nvn_command_replay.*`、
`nvn_present.*`、`nvn_backend_vulkan.c`、`nvn_window.*`、`nvn_texture.*`、`nvn_sampler*`、
`src/android/native/CMakeLists.txt`、`scripts/runtime/build_guest_process_contract.sh`、
`src/runtime/**`、`tests/native/guest_process_contract.c`）。

**没有** push、reset --hard、clean、stash。**没有**碰真机。

# P-MM-2-DRAFT — `movie::SetAllocator`（import index 74）绑定

> 状态：**草稿，待总控派工**。本单只做侦察与下单提词，不含任何实现。
> 侦察者：只读侦察 Agent B｜基线：`work/rg477m-native` @ `5423dac`（Agent A 已合入快照线程策略）
> 证据基线日志：`.local/logs/rg477m-boot-v52-snapshot-thread.log`（exit 133）
> 参照日志：`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`（exit 134）

---

## 1. 标题与任务一句话

**任务**：在 HLE 层为 `main` NSO 的导入 `movie::SetAllocator`（import index 74，mangled `_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_`）补一个**如实记录并转发 allocator 注册**的宿主桩，消除 v52 真机 boot 的新第一致命 SIGTRAP，使 boot 越过 `movie::SetAllocator` 并暴露下一个真实阻塞点。

**一句话**：v52 已越过内存快照策略（`[snapshot-refused]` / `[va-resource-invalid]` / `Aborted` 全部消失），新第一致命是 **`movie::SetAllocator` 未绑定 → 命中 BRK import sentinel → `signal=5` SIGTRAP，exit 133**；本单给这一个符号补宿主桩。

**唯一首选符号**：`movie::SetAllocator`（import index 74）。本单不夹带其它符号。

---

## 2. 阻塞证据

### 2.1 真机日志（v52，第一致命）

`.local/logs/rg477m-boot-v52-snapshot-thread.log`（共 51953 行）：

| 行号 | 内容 | 含义 |
|---|---|---|
| `51932` | `[snapshot-thread-policy] caller=bound-worker registered_threads=18 quota=1073741824 charged=748945408 stack_bytes=2097152; same primary-owned ledger read under the process mutex` | **快照策略已越过**（上一单目标达成） |
| `51933` | `[va-resource-policy] total=16777216 raw_metadata_used=3295232 blocks=12872 reported_used=3819520 SDK_margin=0x80000; ...` | 工作线程成功取到只读资源快照 |
| `51939` | `[fault] signal=5 pc=0x7f5df5a250 fa=0x7f5df5a250` | **SIGTRAP**，`pc==fa` = 跳到 sentinel 页上的 BRK |
| `51940` | `[unresolved-import] index=74 name=_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_ caller_offset=0x14990` | 精确符号 + 精确调用点 |
| `.exitcode` | `EXIT=133` | 128+5 = SIGTRAP |

对照 v51（`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`，exit 134）：`51930 [snapshot-refused] reason=primary-thread` / `51931 [va-resource-invalid] status=1 native=22` / `51932 Aborted` —— **v52 中这三条全部消失**，确证阻塞点已前移。

`signal=5` 且 `pc == fa`，是本仓库 import sentinel 的**代码导入 BRK 陷阱**（`src/runtime/nso/nso_loader.c:250` 写入 `brk`=0xd4200000，`nso_loader.c:529` 打印 `[unresolved-import]`），不是访存越界、不是 abort。

### 2.2 导入身份

- **import index 74**，`evidence/coordinator/nso_imports_main.tsv` 第 75 行（该文件第 1 列是 `<index> <mangled>` 空格分隔、第 2 列是 demangled）：
  ```
  74 _ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_	movie::SetAllocator(void* (*)(unsigned long, unsigned long, void*), void (*)(void*, void*), void* (*)(void*, unsigned long, void*), void*)
  ```
- guest 侧模块 = **`main` NSO**，`build_id 736837afd390442ed7c74a7de730e71b`（与 v52 日志 `51932` 行的 `build=` 一致；运行时实际加载的是 `.local/uncompressed/main.bin`，sha256 `6e6d1f3c…`，见 `.local/scratch/abi-main/inspection.json`）。

### 2.3 ABI（已用 clang 往返实测复现，非推测）

用 `clang++` 反向构造签名并比对 `nm` 输出，**逐字节复现**了同一个 mangled 名：

```
produced: _ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_
expected: _ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_
```

对应 C 声明：
```c
namespace movie {
void SetAllocator(void* (*)(unsigned long, unsigned long, void*),  // x0  alloc
                  void  (*)(void*, void*),                          // x1  free
                  void* (*)(void*, unsigned long, void*),           // x2  realloc
                  void*);                                          // x3  userdata
}
```

**口径修正（重要）**：任务书里写的「8 个函数指针 + size」**不成立**。真实签名是 **3 个函数指针 + 1 个 `void*` userdata，共 4 个参数，返回 void**。以本条为准。

### 2.4 两处调用点（全部调用点，已穷举）

`python3 scripts/runtime/resolve_import_callers.py .local/uncompressed/main.bin --name '_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_'`：

```
import 74 _ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_
  plt 0x5e67f60 callers=2 0x1498c 0x46aab8c
```

- `0x1498c` = **本次崩溃点**。v52 日志 `51940` 行的 `caller_offset=0x14990` 是 **BL 指令地址 + 4**（返回地址），与 `0x1498c` 自洽。独立解 `0x1498c` 处 BL 指令的 imm26，得目标 `0x5e67f60`，与 PLT stub 完全一致。
- `0x46aab8c` = 第二处调用点（**尚未触达**）。

两处实参布局**一致**，均为 guest 自己的三个分配器回调 + NULL userdata：

| 寄存器 | 调用点 A `0x1498c` | 调用点 B `0x46aab8c` | 语义 |
|---|---|---|---|
| x0 | module_offset `0x1c4b0` | module_offset `0x1c4b0` | alloc 回调 |
| x1 | module_offset `0x1c4c0` | module_offset `0x1c4c0` | free 回调 |
| x2 | module_offset `0x13340` | module_offset `0x13340` | realloc 回调 |
| x3 | **NULL** | **NULL** | userdata |

（`0x1498c` 处 `adr` 指令解出的目标 = `0x1c4b0/0x1c4c0/0x13340`；`0x46aab8c` 处 `adrp+add` 解出同样三个 module offset，且 `mov x3, xzr` ⇒ x3=NULL。两处独立解算结果一致。）
（v52 日志 `51940` 之后的寄存器转储 `x0=0x7cb993b4b0 x1=0x7cb993b4c0 x2=0x7cb9932340 x3=0x0`，以 `base=0x7CB991F000` 折算同样是 `0x1c4b0/0x1c4c0/0x13340/NULL`，与静态解算吻合。）

### 2.5 这个符号的真实宿主语义（不是 nn:: SDK API）

**`movie::` 是游戏/SDK 自带的影片播放库命名空间，不是 `nn::` 命名空间。** 已核实：

- 该符号**定义在 `subsdk0` NSO**（Nintendo SDK **movie** 模块，`build_id ab62fe8edd07a6a293f885d54359e559`），定义点 `subsdk0` module_offset `0x129630`，size 4：
  ```
  0x129630: 0x14087ce4   B → 0x3489c0      (tail-call 到 subsdk0 自己的 PLT 转发桩)
  ```
  `0x3489c0` 处是标准 PLT 转发桩（`adrp x16 / ldr x17,[x16,#0x5a8] / add / br x17`），即 subsdk0 把 `movie::SetAllocator` **转发给另一个模块的导入**（subsdk0 侧同族导入含 `multimedia_GetAllocator`、`NvOsHeapAllocatorSetCallbacks`、`NvOsGetCurrentAllocator`）。
- **本运行时根本没加载 `subsdk0`**：`rg -rn 'subsdk0' src/` 无任何命中，`src/` 内没有任何 NSO 加载 subsdk0 的代码路径。`main` 只加载 `main` 自身。
- `main` 里的 `movie::` 家族共 **29 个导入**（见 §3.2），其中**只有 2 个已绑定**（`movie::GetThreadPriority` idx 391 / `movie::SetThreadPriority` idx 572），其余 27 个全部未绑定。

结论：`movie::SetAllocator` 是**「把 guest 自己的 malloc/free/realloc 注册进影片库」**的 setter，不是 Nintendo SDK 的平台 API，**没有任何宿主平台 API 可以直接转发**。仓库里**已有同语义先例**可照抄：`nn::fs::SetAllocator` → `nso_host_fs_set_allocator`（`src/runtime/hle/nso_host.c:588`）——存下 alloc/free 两个函数指针 + 打一条 `[hle]` 日志，并提供 getter。

---

## 3. 口径修正与后续阻塞序列

### 3.1 口径修正：「unresolved-import 计数 = 0」≠「全部导入已绑定」

v52 日志第 5 行（profile）：

```
[profile] explicit diagnostic port-policy quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0 build=736837afd390442ed7c74a7de730e71b imports=651 unresolved=390; CRT/TLS not initialized
```

**651 个导入里仍有 390 个未绑定。** 之前 P-CC-7 闭环说的「unresolved-import 计数 = 0」，指的是**真机 boot 过程中没有因未解析导入而产生 fault**（即没有 `[unresolved-import]` 行、没有 `[fault] signal=5`），**不等于全部导入都已绑定**。未绑定导入拿到的是 BRK sentinel，只有**被 guest 真正调用到**时才会 SIGTRAP。

已用离线工具复核（`scripts/runtime/resolve_import_callers.py` + `nso_imports_main.tsv` 名字比对）：
- `main` 共 651 导入 / 627 个 PLT stub / 120862 个 BL→stub 调用点。
- **未绑定且有调用点的导入共 341 个**（未绑定总数 389，其中 48 个在本二进制里零调用点）。
- 全局调用点密度最高的是 `_Unwind_Resume`（idx 254，2648 处）、`__assert_fail`（idx 563，63 处）、`__dynamic_cast`（idx 528，62 处）。

**这一条对排单的意义**：未绑定面很大（341 个），逐个修会非常慢。真正的做法是**按 guest 实际执行顺序一个一个解**——每个修完跑一次真机，看下一个撞哪个。

### 3.2 `movie::` 命名空间在 `main` NSO 的完整导入清单（29 个）

已绑定 2 个（`src/runtime/hle/nso_host.c:2952-2955`）：
- idx **391** `_ZN5movie17GetThreadPriorityEv` → `nso_host_movie_get_thread_priority`
- idx **572** `_ZN5movie17SetThreadPriorityEi` → `nso_host_movie_set_thread_priority`

**未绑定 27 个**，括号内为 guest 中 BL 调用点数（0 = 本二进制零调用点）：

| idx | mangled | 调用点 |
|---|---|---|
| **74** | `_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_` | **2（本次崩溃）** |
| 390 | `_ZN5movie9ExtractorC1ENS_13ContainerTypeE` | 2 |
| 392 | `_ZN5movie7DecoderC1ENS_11DecoderTypeENS_19DecoderOutputFormatENS_11DecoderModeEi` | 3 |
| 393 | `_ZN5movie9MediaDataC1Ev` | 10 |
| 394 | `_ZN5movie9MediaDataD1Ev` | 11 |
| 395 | `_ZN5movie9MediaData5ClearEv` | 2 |
| 396 | `_ZNK5movie9MediaData10FindStringEPKcPS2_` | 1 |
| 398 | `_ZN5movie9MediaDataaSERKS0_` | 2 |
| 399 | `_ZNK5movie9MediaData9FindInt64EPKcPl` | 3 |
| 400 | `_ZNK5movie9MediaData9FindInt32EPKcPi` | 27 |
| 401 | `_ZNK5movie9MediaData8FindRectEPKcPiS3_S3_S3_` | 3 |
| 402 | `_ZN5movie9MediaDataC1ERKS0_` | 2 |
| 403 | `_ZN5movie6BufferC1Ev` | 8 |
| 404 | `_ZN5movie6Buffer8SetRangeEii` | 4 |
| 405 | `_ZNK5movie6Buffer4SizeEv` | 4 |
| 406 | `_ZNK5movie6Buffer6OffsetEv` | 1 |
| 407 | `_ZN5movie6BufferD1Ev` | 8 |
| 418 | `_ZN5movie6Buffer9SetNvnDevEPv` | 1 |
| 419 | `_ZN5movie6Buffer10SetNvnSyncEPv` | 1 |
| 420 | `_ZN5movie6Buffer18SetDataAndCapacityEPvm` | 4 |
| 570 | `_ZNK5movie6Buffer4BaseEv` | 2 |
| 571 | `_ZN5movie11SetCoreMaskEm` | 1 |
| 573 | `_ZNK5movie6Buffer8CapacityEv` | 6 |
| 574 | `_ZN5movie37GetH264NalUnitStartPositionAndNalSizeEPPhPmS1_S2_` | 1 |
| 575 | `_ZN5movie34CreateH264DecoderConfigurationDataEPKhmS1_mPNS_9MediaDataE` | 1 |
| 576 | `_ZN5movie33CreateVp9DecoderConfigurationDataEiiiPNS_9MediaDataE` | 1 |
| 577 | `_ZN5movie9MediaData8SetInt32EPKcEi` | 1 |

**族内打包边界建议**：

- **本单只做 idx 74 一个符号**。理由：它是**唯一一个已经被真机 boot 证实会撞上的** `movie::` 符号；其余 26 个调用点集中在 `0x590d64c`–`0x5912034` 与 `0x5e143a8`–`0x5e17834` 两段区间，属于影片播放路径，**是否会在 boot 阶段被触达尚无任何观测证据**。在没有证据前一次性打包 27 个符号，等于凭空猜 26 个 API 的语义（`MediaData::FindInt32` 该返回什么、`Buffer::SetNvnDev` 该接什么 NVN 句柄、`Decoder::Decoder` 的四个枚举怎么映射），代价高且大概率返工。
- **但要预留扩展点**：桩的形态（存指针 + 转发 + 日志）应能直接复用到 idx 571 `movie::SetCoreMask`、572 `movie::SetThreadPriority` 这一类同族 setter，且与既有 `nn::fs::SetAllocator` 保持同一套命名/日志约定，避免下一单再开新范式。
- **一个族整体打包的合理时机**：当某一单的日志证明 `movie::` 播放路径整体被触达（例如出现 idx 393/400/403 的 fault），那时按「MediaData 值对象簇」「Buffer 句柄簇」「Decoder/Extractor 生命周期簇」三簇打包，而不是 27 个一把梭。

### 3.3 guest 已经实际调用过的未绑定符号（真正的后续阻塞序列）

在 main NSO 二进制里**已被 BL 调用到**、且当前未绑定的符号（按调用点密度排序，节选）：

| 优先级 | idx | 符号 | 调用点 | 备注 |
|---|---|---|---|---|
| P0 | **74** | `movie::SetAllocator` | 2 | **v52 已实测撞上** |
| P1 | 254 | `_Unwind_Resume` | 2648 | 异常展开；只在抛异常路径上，boot 期不一定命中 |
| P2 | 563 | `__assert_fail` | 63 | 断言失败路径 |
| P3 | 528 | `__dynamic_cast` | 62 | RTTI |
| P4 | 569 | `nn::diag::detail::OnAssertionFailure` | 56 | |
| P5 | 342 | `__errno_location` | 45 | |
| P6 | 400 | `movie::MediaData::FindInt32` | 27 | `movie::` 簇最大头 |
| P7 | 329 / 250 / 409 / 412 / 413 / 411 / 579 / 296 / 44 | `time` / `sscanf` / `nn::os::InitializeMultiWaitHolder` / `UnlinkMultiWaitHolder` / `FinalizeMultiWaitHolder` / `LinkMultiWaitHolder` / `nnsocketGetLastErrno` / `nn::socket::SetSockOpt` / `nn::fs::DeleteFile` | 19/18/18/18/16/16/14/13 | |

**顺序不可预测**：调用点密度 ≠ 执行顺序。真机 boot 只会按实际执行流撞上其中一个，所以**唯一可靠的推进方式是「修一个 → 跑一次真机 → 看下一个」**。

---

## 4. 根因定位（指向源码文件与函数）

**根因：`movie::SetAllocator` 在两个 resolve 表里都没有登记，因此 loader 判定它 unresolved，导入槽被指向 BRK sentinel，guest 一调用就 SIGTRAP。**

绑定链是三层，缺一不可：

1. **`src/runtime/nso/nso_loader.c:263-270`** — loader 调 `resolve(i, name)`；**返回 0 ⇒ `m->unresolved_count++`**，导入槽改指 `m->import_traps + i*8`（该页每 8 字节写入 `brk`=0xd4200000，`nso_loader.c:250`）。这就是 v52 的 `signal=5`。
2. **`src/runtime/hle/guest_process.c:999-1240` — `guest_process_resolve()`**
   - `:1001` 只特判了 `nn::os::GetVirtualAddressMemoryResourceUsage`；
   - `:1113` 的 `static const char *const typed[]` 白名单里**没有** `_ZN5movie12SetAllocator…`（`movie::` 只列了 idx 391/572，见 `:1197-1198`）；
   - `:1240` `for(size_t i=0;typed[i];i++) if(!strcmp(name,typed[i])) return nso_host_resolve(index,name);` —— 白名单没命中就 `return 0` ⇒ unresolved。
   - ⇒ **`guest_process.c:1113` 的 `typed[]` 表是本符号的第一处必改点。**
3. **`src/runtime/hle/nso_host.c:2753-3046` — `nso_host_resolve()`**
   - `:2952-2955` 只有 `movie::GetThreadPriority` / `movie::SetThreadPriority` 两条 `movie::` 绑定；
   - ⇒ **`nso_host.c` 的 resolve 表是第二处必改点**（桩实现在 `:577-586` `nso_host_movie_*` 旁）。
4. **签名门禁（第三处，容易漏）**：`src/runtime/hle/guest_process.c:1244` `import_signature()` 与 `:1488` 的 `found:` 标签构造 `nb_signature`。**返回 0 ⇒ `prepare_boundary_locked()` 在 `:1519` 打印 `[boundary-unsupported] index=… name=…` 并 `return status(NSO_UNSUPPORTED,ENOTSUP,0)`，整个 boundary 不建立 ⇒ 不会 boot。**
   - `:1300` 那个 `one[]` 表（`arguments=0,returns=1`）里也**没有**本符号。
   - ⇒ **`guest_process.c:1244` 起的 `import_signature()` 是第三处必改点**。本符号 `arguments=4, returns=0`（4 个通用寄存器参数、无返回值）。
   - 对照 `guest_process.c:1448` 的既有写法 `_ZN2nn2os32SetMemoryAllocatorForThreadLocal… {arguments=2;returns=0;}` —— 同样是「注册分配器」语义、同样的 `arguments/returns` 声明方式，**可直接照抄该模式**。

---

## 5. 候选改动文件表

| 路径 | 改什么 | 为什么 | 必改 / 可能需碰 |
|---|---|---|---|
| `src/runtime/hle/nso_host.c` | ① 在 `:577-586` `nso_host_movie_get/set_thread_priority` 旁新增 `nso_host_movie_set_allocator(void*(*)(unsigned long,unsigned long,void*), void(*)(void*,void*), void*(*)(void*,unsigned long,void*), void*)`；② 在 `:2952-2955` 附近加 `strcmp(name,"_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_")` 绑定 | 桩实现 + host resolve 表登记。**注意 `nso_host.c` 当前未被 Agent A 改动**（`git status` 只有 `guest_process.c/.h`、`tests/native/guest_process_contract.c`、`scripts/runtime/build_guest_process_contract.sh`、`coordination/HANDOFF.md` 被改），所以本文件是**无冲突**的 | **必改** |
| `src/runtime/hle/guest_process.c` | ① `:1113` `typed[]` 白名单加 `_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_`；② `:1244` `import_signature()` 增加 `arguments=4; returns=0;` 分支 | ① 不过白名单 ⇒ `guest_process_resolve()` 返回 0 ⇒ 仍然 unresolved ⇒ 仍然 SIGTRAP；② 不过签名 ⇒ `[boundary-unsupported]` ⇒ boundary 不建立 ⇒ 根本不会 boot。**⚠️ Agent A 已于 `5423dac` 提交并交还该文件（当前工作树干净）——派工前仍须按 AGENTS.md 核对 `active_writers`，确认无他人占用** | **必改（确认 A 已交还后即可动）** |
| `src/runtime/hle/guest_process.h` | 若桩需要导出声明（供契约测试引用）则加声明 | 与既有 `nso_host_*` 导出风格一致 | **可能需碰** |
| `tests/native/guest_process_contract.c` | 加一条契约：resolve 本符号返回非 0，且 `import_signature` 认可（4 参数 / 0 返回） | 防止「只加了白名单忘了签名」这类半改 | **可能需碰** |
| `scripts/runtime/build_guest_process_contract.sh` | 仅在契约测试新增用例需要改构建开关时才动 | 一般无需改 | **可能需碰** |
| `coordination/state.json` / `coordination/HANDOFF.md` | 由**总控**更新状态与交接 | AGENTS.md 规定这两个文件由总控唯一写 | **总控写** |

---

## 6. 建议实现方向（含明确结论）

### 6.1 结论：**采用「如实记录并转发 allocator 注册」**，不是「可跳过」

**明确结论**：桩必须是**如实记录并转发**——把 guest 传入的三个分配器回调 + userdata 原样存进宿主状态、打一条 `[hle]` 日志、并保留可被后续 `movie::` 桩复用的读取入口。**不是空实现、不是直接 `return`。**

**依据（区分观测与推断）**：

**观测（硬证据）**
1. 两处调用点 `0x1498c` / `0x46aab8c` 传入的是**guest 自己模块内的三个函数指针**（module_offset `0x1c4b0` / `0x1c4c0` / `0x13340`，均在 `main` 的 .text 内），userdata 为 NULL。语义就是「guest 把自己的 malloc/free/realloc 注册给影片库」。
2. `movie::SetAllocator` 的真实实现**定义在 `subsdk0` NSO**（`0x129630`，size 4，`B → 0x3489c0` 转发到 subsdk0 自己的 PLT 槽），而 **`subsdk0` 在本运行时根本没被加载**（`rg 'subsdk0' src/` 无命中）。所以**没有真实影片库会来读回这个注册**。
3. `main` 的 29 个 `movie::` 导入里没有 `movie::GetAllocator` 之类的读取接口 —— **guest 无法从 `movie::` 侧读回自己注册的东西**（除非影片库内部用注册的 alloc 去分配，而影片库不存在）。
4. 仓库已有**同语义先例**：`nn::fs::SetAllocator` → `nso_host_fs_set_allocator`（`nso_host.c:588`）就是「存下 alloc/free 函数指针 + 打 `[hle]` 日志 + 提供 getter」，且 `guest_process.c:1448` 给它登记了 `arguments=2;returns=0;`。

**推断（标注为推断）**
5. 因此本桩的唯一可观测作用是「不要 SIGTRAP、不要破坏 boot」；**记录下来是为了忠实反映 guest 的意图、并为后续 `movie::` 簇（尤其 idx 393/400/403 等会真的分配内存的桩）保留正确的分配器**。若后续要实现 `movie::Buffer::SetDataAndCapacity`（idx 420）或 `MediaData` 相关桩，它们应当走 guest 注册进来的分配器，而不是宿主 malloc —— 记录这一步是那条链的前提。
6. 选「如实记录并转发」而非「可跳过」的**决定性理由**：跳过 = 悄悄丢弃 guest 的一次真实状态变更，将来 `movie::` 簇实现时会出现「分配器来源不一致」的隐性错误，且没有任何日志能提示；记录 = 多一次 `[hle]` 日志 + 4 个指针，代价极低，且完全可回退。**取舍理由：记录的成本是一次日志 + 4 个指针，收益是保留 guest 意图与后续可追溯性；跳过的成本是将来排障时无法区分「guest 没注册」与「我们丢了注册」。**

### 6.2 实现要点（方向，不含完整实现）

1. **桩形态照抄 `nso_host_fs_set_allocator`**：新增两个（或三个）静态指针 + 一个 userdata 静态量；桩体内 `fprintf(stderr,"[hle] movie::SetAllocator: alloc=%p free=%p realloc=%p userdata=%p\n",…)`；日志格式与既有 `[hle] nn::fs::SetAllocator: alloc=%p free=%p` 保持同一风格，便于 grep 对比。
2. **签名必须严格按 §2.3 的 4 参 / void 返回**：`(alloc, free, realloc, userdata)`，顺序不可换。桩体内**只记录、不调用**这些 guest 函数指针 —— 调用 guest 回调需要正确的 guest TLS/栈上下文，在 host 桩里直接调用会破坏 guest 线程状态。
3. **resolve 表登记**：在 `nso_host.c` 的 `movie::` 段照 `movie::SetThreadPriority` 的写法加一条 `strcmp` 分支；同时在 `guest_process.c` 的 `typed[]` 白名单加同名条目（**白名单是硬门禁，漏了等于没改**）。
4. **签名门禁登记**：在 `import_signature()` 加 `arguments=4; returns=0;`。注意本符号**无返回值**，`returns=0`（对照 `guest_process.c:1448` 的 allocator setter 同款写法）。
5. **契约测试**：至少两条断言 —— (a) `guest_process_resolve(74, mangled)` 返回非 0；(b) `import_signature(mangled, …)` 返回 1 且 `gp_arguments==4 && gp_returns==0`。第 (b) 条是本单最容易被漏掉的点，务必覆盖。

---

## 7. 验收命令

### 7.1 宿主契约（不碰设备）

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
# 契约测试构建 + 运行（与既有流程一致）
bash scripts/runtime/build_guest_process_contract.sh
# 期望：guest_process_contract 全绿，且新用例覆盖 resolve 非 0 与 gp_arguments=4/gp_returns=0
```

### 7.2 宿主离线取证（复核用，不改任何文件）

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
# 确认符号身份与调用点（只读本地字节）
python3 scripts/runtime/resolve_import_callers.py .local/uncompressed/main.bin \
  --name '_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_'
# 期望：import 74 / plt 0x5e67f60 / callers=2 0x1498c 0x46aab8c

# 复核 ABI（clang 往返）
cat > /tmp/abi_check.cpp <<'EOF'
namespace movie {
void SetAllocator(void* (*)(unsigned long, unsigned long, void*), void (*)(void*, void*), void* (*)(void*, unsigned long, void*), void*);
}
EOF
clang++ -c /tmp/abi_check.cpp -o /tmp/abi_check.o && xcrun nm /tmp/abi_check.o | grep -o '_ZN5movie12SetAllocator[A-Za-z0-9_]*'
# 期望：_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_
```

### 7.3 真机 boot（参数沿用 v51/v52 四元组）

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
# 构建 + 推 + boot；日志名建议 .local/logs/rg477m-boot-v53-movie-setallocator.log
adb -s RG477M01445122 push <产物> /data/local/tmp/
adb -s RG477M01445122 shell 'cd /data/local/tmp && ./<产物> \
  --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 \
  --boot > /sdcard/rg477m-boot-v53-movie-setallocator.log 2>&1; echo "EXIT=$?" >> /sdcard/rg477m-boot-v53-movie-setallocator.log'
adb -s RG477M01445122 pull /sdcard/rg477m-boot-v53-movie-setallocator.log \
  .local/logs/rg477m-boot-v53-movie-setallocator.log
cat .local/logs/rg477m-boot-v53-movie-setallocator.log.exitcode   # 期望不再是 133
```

**真机验收判据（全部满足才算本单 PASS）**
1. 日志中**不再出现** `[unresolved-import] index=74` 与 `caller_offset=0x14990`；
2. 日志中**不再出现** `[fault] signal=5 pc=0x7f5df5a250`；
3. 新增一行 `[hle] movie::SetAllocator: alloc=… free=… realloc=… userdata=…`，其中 `alloc/free/realloc` 三个指针**非 NULL**，`userdata` 为 NULL（与 §2.4 实测一致）；
4. boot **继续前进**（出现新的 `[hle] …` 活动或新的 fault），即本单**只证明越过了 idx 74**，不证明任何画面/可玩；
5. exit code **不再是 133**（新值如实记录，不预设）。

**若 boot 前进后撞上新的未绑定导入**（预期极可能发生，见 §3.3），按本单同格式产出下一张 `P-MM-2-DRAFT-<符号>.md`，**不要在本单顺手修**。

### 7.4 自证

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
git status --short        # 派工者：只应出现本单授权范围内的改动
git diff --stat
```

---

## 8. 禁止清单

1. **禁止**修改 `src/**`、`tests/**`、`scripts/**` 之外的任何文件；**禁止**改 `coordination/state.json`、`coordination/HANDOFF.md`、`coordination/STATUS.md`（总控唯一写）。
2. **禁止**在 `guest_process.c` 上与他人并发写。该文件已由 Agent A 于 `5423dac` 交还，但**派工前必须按 AGENTS.md 核对 `coordination/state.json` 的 `active_writers`**，确认无他人占用；**同一文件单写者**。
3. **禁止**伪造页表、伪造 RSS、伪造完整 Horizon VM、伪造资源池容量。资源口径必须保持现有 `logical node pool, not Horizon page tables/RSS` 语义。
4. **禁止**在桩里**直接调用** guest 传入的三个分配器回调（会破坏 guest TLS/栈上下文）；**只记录**。
5. **禁止**顺手实现 `movie::` 簇的其它 26 个符号，或任何 §3.3 表里的其它符号 —— 本单唯一首选符号是 idx 74。
6. **禁止**声称标题画面 / 可玩 / 已出画面。本单只是解除一个 import 阻塞。
7. **禁止**改游戏包、原交付、存档、模拟器或其它项目；`.local/romfs/**`、`.local/exefs/**`、`.local/uncompressed/**` **只读**。
8. **禁止** `git reset --hard` / `force push` / `git checkout` / `git clean` / `git stash` 覆盖他人未提交变更；**禁止** `git add .` 混入他人 WIP；**禁止** push 远端。
9. **禁止**使用 `agy`（Antigravity）。
10. **禁止**在 `import_signature()` 里给本符号编造与实际 ABI 不符的 `arguments/returns`（必须是 4 / 0）。
11. **禁止**改 `.local/` 下的日志、退出码与证据产物（只追加新版本文件，不覆盖既有 v51/v52）。

---

## 9. 待 A 的 v52 确认

**已确认，无需等待。** 本单已基于 `.local/logs/rg477m-boot-v52-snapshot-thread.log`（51953 行，`.exitcode` = `EXIT=133`）完成取证，v52 的关键行已逐条摘录于 §2.1。

以下**仍需下一轮真机 boot（v53）确认**，属于本单交付后的观察项，不影响本单派工：

1. **越过 idx 74 后的下一个阻塞符号是什么** —— v53 日志会出现新的 `[unresolved-import] index=… name=… caller_offset=…` 或新的 `[fault] signal=…`。本单**不预测**该符号（调用点密度 ≠ 执行顺序，见 §3.3），但已把 `movie::` 族 27 个未绑定符号与其调用点数完整列出，便于 v53 出结果后直接对表。
2. **guest 是否会触达 `movie::` 播放路径**（idx 393/400/403 等调用点集中在 `0x590d64c`–`0x5912034` 与 `0x5e143a8`–`0x5e17834`）。目前**无任何观测证据**表明 boot 阶段会走这两段区间。若 v53 证明会触达，则 §3.2 的「按三簇打包」建议生效，需要另开一单。
3. **`movie::SetAllocator` 注册的分配器是否被真正使用** —— 只有当某个 `movie::` 分配型桩（如 idx 420 `Buffer::SetDataAndCapacity`）落地后才可验证，届时应确认它是否走 guest 注册的分配器而非宿主 malloc。
4. **Agent A 对 `guest_process.c` 的改动是否已合入主线** —— 本单的必改点 ① 与 ② 都在该文件。**已确认：A 的改动已于 `5423dac` 合入**（`fix(runtime): allow memory/resource snapshots from guest worker threads`），本单引用的行号 `typed[]:1113` / `import_signature():1244` / allocator setter  precedent `:1448` 均已在新 HEAD 上复核一致。派工时仍应在落刀前重新核一次行号。

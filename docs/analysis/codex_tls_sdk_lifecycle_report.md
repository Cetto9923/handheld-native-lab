> 更正：可空hook是GLOBAL OBJECT而非ELF STB_WEAK；普通新建线程返回的OS槽→状态通知→hook→ELF drain顺序已闭合。详见[codex_tls_exit_hook_report.md](codex_tls_exit_hook_report.md)，其限定结论取代下文旧未闭合表述。

# SDK TLS 槽与 main 回调：限定静态结论

普通 LLVM 反汇编及本地 NSO 元数据，非 REA Evidence。没有运行目标代码、修改 TP/SDK/repo 或操作设备。全部派生产物在本目录 `.local/`。机器表：`.local/sdk-lifecycle-review-v1/tls_sdk_lifecycle.json`；逐 PC：`.local/sdk-lifecycle-normalized-v3/`；命令、编译退出码及原始切片：`.local/sdk-lifecycle-v1/run.json` 与各子目录。复用了 runtime 匹配输入的既有小切片，没有重新 setup。

原输入前后 SHA256 相同：

| 输入 | SHA256 |
|---|---|
| main | `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5` |
| sdk | `9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9` |

原始路径为 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/uncompressed/{main,sdk}.bin`；runtime 同名输入是指向它们的符号链接。所有下述 PC 都是模块相对地址。

## API、槽范围和存储

| SDK 函数 | PC | 观察到的 AArch64 ABI |
|---|---:|---|
| `nn::os::AllocateTlsSlot` | `143b60` | x0=输出 u32 指针，x1=析构回调；w0=0 或 **0x1203** |
| `nn::os::SdkAllocateTlsSlot` | `143bb0` | 同参数；传 SDK 标志 1；SDK 搜索耗尽转 AbortImpl |
| `nn::os::FreeTlsSlot` | `143bf0` | w0=槽，零扩展后转 manager；void |
| `nn::os::GetTlsValue` | `143c10` | w0=槽；x0=u64 值 |
| `nn::os::SetTlsValue` | `143c30` | w0=槽，x1=u64 值；void |
| `nn::os::GetUsedTlsSlotCount` | `143c50` | w0=应用槽已用数，未计 SDK 槽 |

这些均有实际导出符号，详见 `api.tsv`。C 导出 `nnosAllocateTlsSlot` (`1448c0`) 实际调用 **SdkAllocateTlsSlot**，再将结果与 `0x3fffff`；不能据名称把它等同应用侧分配器。C Free/Get/Set 在 `1448e0/1448f0/144900` 转相应公开函数。

`TlsManager` ctor (`144c84`) 清应用计数和 **32 个 callback 指针**：`this+8+slot*8`，范围 `[this+8,this+108)`。`SearchUnusedTlsSlotUnsafe` (`144a08`) 以表项为零表示空闲，未见独立位图：

- 应用标志 0：按 0→15 找空项，manager `144f04` 另检查应用计数 `<16`；满额返回失败，输出槽不写。
- SDK 标志 1：按 **31→17** 找空项；全部占用则 `144c80 → nn::diag::detail::AbortImpl`。
- **槽 16 不在两个搜索分支中**。ctor 会清其表项，析构循环会扫描它，但具体专用者或保留用途仍未知，不能擅自开放分配。逐加载/返回 PC 在 `search_slots.tsv`。

`144f24` 的 ADR 指向 `144fa0: RET`；空 destructor 参数替换为该非零哨兵，`144f38` 存入 callback 表。因此“已分配但无用户 destructor”仍占槽。分配/释放先锁当前 `ThreadType+1a8`、再锁 manager `+108`。成功分配释放这两锁后，`144f78` 调 `ThreadManager::SetZeroToAllThreadsTlsSafe` (`1420d0`)：在其 `+1c8` 锁下沿已登记线程链清该槽，再于 `144f7c` 写输出槽。

Free manager (`144fa4`) 于 `144fec` 清 callback，若槽 `<16` 则减应用计数；**不调用 destructor，不清线程值**。下一次成功分配通过上述线程链清值。空 callback 标记走 `14501c` AbortImpl；Get/Set 可见函数没有范围或分配检查。本任务没有执行无效输入。

Get/Set 的精确地址式分别由 `143c10..143c20` / `143c30..143c40` 给出：

```text
thread = mem64(TPIDRRO_EL0 + 0x1f8)
value  = mem64(thread + 0x80 + 8*u32(slot))
```

32 槽值范围为 `ThreadType+[80,180)`。线程入口结构片段 `141c9c` 将传入 ThreadType 指针写入 `TPIDRRO+1f8`。这与 main 使用 `TPIDR_EL0` 的 ELF TLS 分开；SDK `__get_tp` (`98a00`) 读 `TPIDRRO+1f0`，`__get_tls_data` (`98a10`) 再减 `18`。本单元不证明这些指针与 main TP 的完整链接或宿主 pthread 兼容性。

## OS 槽 destructor 的真实执行顺序

导出 `InvokeTlsDestructors` `[144cf0,144eb8)` 扫描 0→31，回调 ABI 是 x0=当前 u64 值，现场读取 callback 表，**回调返回后才清当前槽值**。最多 **5 轮**，不是按 PoC 或 POSIX 常量推定：

| 轮 | BLR PC | 条件与清理 |
|---|---|---|
| 1 | `144d3c` | callback 非零即调用，值为 0 也调用；每槽均清值 |
| 2 | `144d88` | 非零值才查 callback；清当前值；整轮无非零值则返回 |
| 3 | `144de8` | 同上 |
| 4 | `144e48` | 同上 |
| 5 | `144eb0` | 非零值才查 callback；清当前值；没有第六轮 |

同槽在 callback 内重设的值会被随后的清零覆盖；较早槽被重设可在下一轮遇到；较晚槽可能在本轮遇到。最终轮 callback 重新写已访问槽时，函数没有再扫一次，不能断言返回时所有值为零。该函数本体不释放 callback 标记，也没有锁。直接调用点为 `ThreadManager::CleanupThread+20` (`141990`) 及未命名线程入口片段 `141d04`，均在设置结束状态/广播等待者之前。没有证明所有线程退出路径、并发 Free/Allocate 或异常 callback 的完整语义。

## ELF/C++ thread_atexit 是另一套生命周期

SDK `__cxa_thread_atexit` (`63e800`) 正常路径于 `63e808` 调未命名 `98a20`，参数 x0=callback、x1=argument、x2=DSO token；可见正常返回 w0=0。`98a44` 调 `nnosAllocateMemoryForThreadLocal` 分配 **48 字节节点**；wrapper `144640` 设 x1=16 后间接跳至已安装 allocator。分配后直接写节点，未见返回 null 检查；OOM 合约不能靠默认成功代替。

节点可确认字段为 callback/argument/DSO/module index/next/prev，偏移 `0/8/10/18/20/28`。线程表头在 `mem64(TPIDRRO+1f0)` 指向对象的首字；在 SdkMutex 下压入表头，LIFO。未命名 drain `98b60` 先摘头，再解锁于 `98bc4`、`98bcc: BLR x8` 调 callback，之后重锁并重读头。callback 新注册的节点可在同一次循环处理，**没有 OS 槽的固定 5 轮上限**。`98c10` 在可见 module-table entry 非空分支释放节点，参数 node/48；空 entry 分支 `98bf8` 跳回解锁/重读，没有该次可见 free，不能概括所有节点必释放。DSO 过滤入口 `98c40` 同样先摘链，再于 `98cd4` 调 callback。

`9ae68` 的直接调用确实到 drain；其所在 `[9ae40,9af54)` 之后会释放 TLS 分配块。尚未闭合 BSS weak `nnmuslThreadPointerDestroyer` (`b88c40`) 如何绑定该入口，以及 ELF drain 与 OS 槽 destructor 在所有线程上的最终先后顺序。保留此缺口。

## main +303e0 回调与 heap 边界

前一单元确认 `30318` 注册 `303e0`，argument=`TP+268`。现闭合 `[303e0,304dc)` 的普通控制流：

1. `303e4` 取 p=`*argument`；零则返回。`303f8` 写 `[p+40]=p+10000`。
2. `30400/30404` 取反 u32 `[p+48]`；`30410..3041c` 以 LDAXR/STLXR 原子更新 `u32[p] -= ~u32[p+48]`（32 位回绕）。`30420` 仅在成功更新结果为零时继续。
3. `30438` 取 main TP；读取 `TP+260` 位 0。首次于 `30494` 注册第二个 callback=`303cc`、argument=`TP+258`、DSO=`B+771ba00`，不检查返回值，并设置标志。
4. 将 p 留存在 `TP+258`，如果那里原值非零，则在 `3046c` 尾调 `b70f8` 释放原值。`303cc` 以后也取 `*(TP+258)` 并尾调该入口。两个 callback 都没有清其原 argument 指针字段。

`303e0` 本体无 BLR；间接调用发生在 `b70f8`。main 导出 `free@132e0` 的 `132e4` 也尾调同一入口，证明这是本游戏 free 路由。非空参数经 x1 传入 allocator 对象的虚表 `+50`，`b7118/b7150/b71a4` 为间接 BR；对象未初始化路径经 guard 调 `be270`，其体未在本单元追踪。因此需要 guest allocator 对象、虚表、初始化和 guest 可调用目标；不能直接用 host free。

这可精确称为“块释放/保留回调”，且具有 64KiB 边界算术；**具体 C++ 类型、原始分配大小/来源、引用/计数不变量仍未证明**。没有把相邻 `304e0` 另一个函数的虚调用混入本回调。

## 最小实现边界与余项

独立 GuestThreadContext 可依据这些证据分别建模 32 个 OS u64 值、进程 callback 表、两种分配搜索与应用计数、清线程链、精确五轮策略，以及独立的 ELF LIFO destructor 节点和分配 hook。它仍需 guest callback ABI、真实线程生命周期及 guest heap，不能把 16 槽 PoC 直接绑定 SDK。

剩余最小缺口是槽 16 的实际所有者、weak thread-pointer hook 安装/退出路径和顺序、main allocator 对象及分配不变量。没有新增全程序 TP header 不访问证明，也没有 SDK/HLE 绑定或动态兼容性验收。本轮没有被拒绝、pending 或 uncertain 的动作。

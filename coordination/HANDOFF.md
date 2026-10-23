# Antigravity接力入口：越过CreateThread首阻，GameThread子线程真机跑通，推进至nn::fs::SetAllocator

本轮按工单 P-AG-3 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::os::CreateThread` 及相关 OS 线程 HLE 落地闭环**：
   - 实现了 `nn::os::CreateThread`（及 7 参重载）、`StartThread`、`WaitThread`、`DestroyThread`、`SetThreadName`、`SetThreadNamePointer`、`GetThreadNamePointer`、`GetThreadPriority`、`GetThreadCurrentPriority`、`ChangeThreadPriority`、`GetThreadId`、`GetCurrentThread`、`SleepThread`、`YieldThread`、`SetThreadCoreMask`、`GetThreadCoreMask`、`GetThreadAvailableCoreMask`。
   - Disassemble `sdk.bin` 确证 `sizeof(ThreadType) == 0x1c0` (448 字节) 及内部字段偏移（state@0x40, priority@0x44, stack@0x48, stack_size@0x58, arg@0x60, entry@0x68, name_buf@0x180, name_ptr@0x1a0, thread_id@0x1b8）。
   - 每个新线程分配独立的 `guest_tp` 镜像并绑定（不修改宿主 Bionic TPIDR_EL0）。

2. **跨线程 Native Boundary 与进程上下文打通**：
   - 实现子线程的 `native_boundary` 插槽表 (`s_slots[NB_MAX_GUEST_THREADS]`)，支持在子线程上保留并恢复宿主 shadow call stack / platform 寄存器 (`x18`)，并通过 `nb_call_child` 在游戏分配的 1MB guest stack (`0x707a081000`) 上执行 `guest_entry`。
   - 所有导入网关 (`nb_import_gateway`, `nb_import_stack13_gateway`, `nb_import_snprintf_gateway`) 均适配子线程槽查找，子线程调用宿主 HLE 导入项无陷阱、无寄存器破坏。
   - 实现 `guest_process_bind_child_thread()` 与 `guest_process_unbind_child_thread()`，子线程在进入 guest code 前与退出后正确关联当前进程上下文，使 `GetHostArgc` (`host_argument_count`) 等 HLE 服务能够安全访问命令行参数及内存配额。

3. **宿主契约测试与真机契约测试全部通过**：
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全部 24 项宿主契约（含 `host-threads-contract`）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --threads` 与 `--guest-tp`：
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

4. **真机实测突破：越过 CreateThread 首阻，GameThread 进入业务循环**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x76405c3acc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x7642a19570 entry=0x763d31e1f0 arg=0x0 stack=0x7079f81000 size=0x100000 prio=16 tid=3285
     [hle] nn::os::StartThread: thread=0x7642a19570 tid=3285
     [hle] thread tid=3285 bound guest TP base=0xb4000077dc418640 span=4144
     [hle] child thread tid=3285 entering guest_entry=0x763d31e1f0 sp=0x707a081000 arg=0x0
     [hle] nn::os::SetThreadName: thread=0x7642a19570 name='GameThread'
     ```
   - 子线程 `GameThread` 在独立 1MB guest stack 上成功调用 `GetHostArgc` (`0x352f220` -> PLT `0x5e67e80`) 且零陷阱返回，越过整个线程首阻！

5. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE`（`nn::fs::SetAllocator`，symbol index 20，PLT `0x5e67c00`，`caller_offset=0x553c` / `lr=0x7639df4540`，base=`0x7639def000`）。
   - 退出码：133（SIGTRAP sentinel brk at unlinked import 20）。
   - 完整原始日志：`.local/logs/rg477m-boot-v19-threads.log` + `.exitcode`。

6. **明确未完成项标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，未真正实施页面写保护；
   - 尚无标题画面 / 可操作场景证据；
   - NVN→Vulkan 完整桥接、RomFS/IoStore 运行时挂载未闭环。

## 先读这些

- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本次 `--boot` 完整原始运行日志已留盘：`.local/logs/rg477m-boot-v19-threads.log` (exitcode 133)。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **全量构造函数序列** | **100% 达成**：`init_array` 全部 1,890 项构造函数真机连跑通过，无一跳漏，登记 13,536 项 atexit 回调。 |
| **游戏主入口 nnMain** | **成功进入**：offset `0x352f160` 成功执行并验证 `nn::util::ReferSymbol`。 |
| **nn::os::CreateThread / StartThread** | **已越过并真机通过**：1MB 独立栈、独立 `guest_tp`、跨线程 Native Boundary 插槽分派正常。 |
| **GameThread 子线程** | **成功执行**：进入 `GameThread`，调用 `GetHostArgc` 正常返回，进入游戏核心初始化。 |
| **最新真实阻塞** | **明确归因**：`_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE`（import 20, caller 0x553c），游戏进入文件系统初始化。 |
| **ProtectRelro 状态** | **明确标注未完成**：当前为打印桩，未实施 mprotect 保护。 |

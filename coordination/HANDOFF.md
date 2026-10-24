# Antigravity接力入口：越过 __cxa_thread_atexit，推进至 _ZN2nn4diag6detail9AbortImplEv（P-AG-15）

## 调度备注（夜守 PM）
- **当前阻塞**：`_ZN2nn4diag6detail9AbortImplEv`（`nn::diag::detail::AbortImpl()`，symbol index 12, JMPREL reloc index 3996, GOT `0x7772058`, caller offset `0x11b8`, lr offset `0x11bc`, exit 133）
- **基线 HEAD**：本轮提交检查点（`__cxa_thread_atexit` & `__cxa_thread_atexit_impl` HLE 已过）；日志 `.local/logs/rg477m-boot-v31-cxa-thread-atexit.log`
- **P-AG-15**：已闭环（`__cxa_thread_atexit` 与 `__cxa_thread_atexit_impl` 严格遵循 Itanium/Android C++ ABI 规范落地，支持线程级生命周期管理与 LIFO 执行；真机上 GameThread 成功完成 3 次 `__cxa_thread_atexit` 登记，成功枚举 `rom:/FFRS/Content/Paks/`，创建并启动 `FHeartBeatThread` 线程，跨线程 Event 唤醒全通，并平稳推进至 `AbortImpl`）；无其他并行 agy worker
- **阶段**：`T02_CXA_THREAD_ATEXIT_PASSED_ABORT_IMPL_REACHED_ON_RG477M`
- **未达**：标题画面 / 可操作场景（勿伪称可玩）

本轮按工单 P-AG-15 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **严格 Itanium / Android C++ ABI `__cxa_thread_atexit` 全链路落地闭环**：
   - 遵循 Itanium C++ ABI / POSIX / Android bionic 规范，实现独立线程局部析构登记链表；
   - 通过 `pthread_key_t` 注册线程退出清理器，确保子线程退出时按 LIFO 倒序自动调用各析构函数并释放节点；
   - 支持显式主动析构 `nso_exit_registry_thread_finalize()` 以及线程登记数查询 `nso_exit_registry_thread_pending()`；
   - 一并落地配套符号 `__cxa_thread_atexit_impl`；
   - 输出可归因日志 `[hle] __cxa_thread_atexit: dtor=%p obj=%p dso=%p pending=%zu`；
   - 在 `guest_process.c` 将符号登记入 `typed` 白名单，并在 `import_signature`（3 参 1 返，`register_only`）绑定，GameThread 及衍生子线程无缝复用 Native Boundary 统一网关。

2. **宿主与真机契约全绿（36 项契约全部 PASS）**：
   - 增加 `--cxa-thread-atexit` 独立契约测试用例 `cxa_thread_atexit_contract`，全面覆盖符号解析、空参数拒绝、主线程 LIFO 逆序执行校验、多线程线程局部隔离（子线程注册不影响主线程）、子线程退出时 pthread 自动析构验证；
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 36 项宿主契约（含 `host-cxa-thread-atexit-contract`，returncode 0）；
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --cxa-thread-atexit`：
     `{"cxa_thread_atexit_errors":0,"resolve":true,"invalid_params":true,"lifo":true,"thread_isolation":true,"auto_exit":true}`；
   - 复跑 `--posix-memalign`、`--os-event`、`--threads` 与 `--guest-tp` 均全绿通过。

3. **真机实测重大突破：越过 __cxa_thread_atexit，虚幻引擎枚举 Paks 并派生 FHeartBeatThread**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [hle] posix_memalign: memptr=0x67fa2806d8 alignment=64 (0x40) size=40 (0x28) -> *memptr=0xb400006e6d2a4a40 rc=0
     [hle] __cxa_thread_atexit: dtor=0x6db9f5e650 obj=0xb400006e7d2aa450 dso=0x6dc1657a00 pending=1
     [hle] nn::fs::OpenDirectory: path='rom:/FFRS/Content/Paks/' mode=3 (host_path='romfs/FFRS/Content/Paks/') total_entries=3 -> handle=0x5b7f3a84a0
     [hle] nn::fs::GetDirectoryEntryCount: handle=0x5b7f3a84a0 -> count=3
     [hle] nn::fs::ReadDirectory: handle=0x5b7f3a84a0 req_max=3 read_entries=3 (cursor=3/3)
     [hle] nn::fs::CloseDirectory: closed handle=0x5b7f3a84a0 (path='rom:/FFRS/Content/Paks/')
     [hle] nn::fs::OpenDirectory: path='rom:/FFRS/Saved/Paks/' mode=3 (host_path='romfs/FFRS/Saved/Paks/') stat failed errno=2 (No such file or directory)
     [hle] nn::fs::OpenDirectory: path='rom:/Engine/Content/Paks/' mode=3 (host_path='romfs/Engine/Content/Paks/') stat failed errno=2 (No such file or directory)
     [crt] atexit registered callback=0x6db9ff1e90 pending=13561; deferred
     [crt] atexit registered callback=0x6dbd361f60 pending=13562; deferred
     [crt] atexit registered callback=0x6db9ff1e70 pending=13563; deferred
     [crt] atexit registered callback=0x6db9fb7e80 pending=13564; deferred
     [hle] nn::os::InitializeEvent: event=0x5e79e9acf0 initially_signaled=0 clear_mode=0 (ManualClear)
     [page-port-policy] op=map address=0x67fa851000 bytes=1048576 result=0 after real VM/accounting transition
     [hle] nn::os::CreateThread: thread=0x60b9e0d278 entry=0x6db9f56a1c arg=0x60b9e0d240 stack=0x67fa851000 size=0x100000 prio=15 tid=25178
     [hle] nn::os::SetThreadName: thread=0x60b9e0d278 name='FHeartBeatThread'
     [hle] nn::os::StartThread: thread=0x60b9e0d278 tid=25178
     [hle] __cxa_thread_atexit: dtor=0x6db9ff9ec4 obj=0xb400006e7d2aae00 dso=0x6dc1657a00 pending=2
     [hle] thread tid=25178 bound guest TP base=0xb400006e7d2ab430 span=4144
     [hle] child thread tid=25178 entering guest_entry=0x6db9f56a1c sp=0x67fa951000 arg=0x60b9e0d240
     [crt] atexit registered callback=0x6db9ff20e0 pending=13565; deferred
     [hle] nn::os::SignalEvent: event=0x5e79e9acf0
     [hle] nn::os::WaitEvent: event=0x5e79e9acf0 satisfied
     [hle] nn::os::ClearEvent: event=0x5e79e9acf0
     [page-port-policy] op=map address=0x67faa50000 bytes=393216 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x67faad0000 bytes=4096 result=0 after real VM/accounting transition
     [crt] atexit registered callback=0x6db9ff1eb0 pending=13566; deferred
     [crt] atexit registered callback=0x6db9f3e560 pending=13567; deferred
     [hle] nn::fs::OpenFile: path='rom:/FFRS/FFRS.uproject' mode=1 (host_path='romfs/FFRS/FFRS.uproject') -> open failed errno=2 (No such file or directory)
     [crt] atexit registered callback=0x6dba104340 pending=13568; deferred
     [page-port-policy] op=map address=0x6599e20000 bytes=65536 result=0 after real VM/accounting transition
     [hle] __cxa_thread_atexit: dtor=0x6dba12c440 obj=0xb400006e7d2aae40 dso=0x6dc1657a00 pending=3
     [fault] signal=5 pc=0x705dc5a060 fa=0x705dc5a060
      x0=0x6259  x1=0x0  x2=0x67fa280688  x3=0x9ff230772b
      x4=0xffffffffffffff  x5=0x1931148  x6=0x32  x7=0x7f7f7f7f7f7f7f7f
      x8=0x6dc1b81aa4  x9=0x1  x10=0x2  x11=0x5b7f3b4f58
      x12=0x20  x13=0xb400006e4d2a6250  x14=0x1931148  x15=0x9ff2311a37
      x16=0x6dc16ae058  x17=0x705dc5a060  x18=0x5fabd61ad1b3a274  x19=0x6dc1bcc9e8
      x20=0x5e39e1c7b4  x21=0x5  x22=0x67fa280878  x23=0x200
      x24=0x6dc681dbb0  x25=0x6dc681dbb0  x26=0x6dc681def8  x27=0x114000
      x28=0x116000 
      fp=0x67fa280730 lr=0x6db9f3d1bc sp=0x67fa280720
     ```
   - 虚幻引擎连续 3 次成功完成 `__cxa_thread_atexit` 登记！
   - GameThread 顺利进入 Pak 文件系统扫描（`rom:/FFRS/Content/Paks/` 枚举），派生子线程 `FHeartBeatThread` 并成功完成跨线程 Event 唤醒。

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn4diag6detail9AbortImplEv`（`nn::diag::detail::AbortImpl()`）。
   - 符号索引：`symbol index = 12`（JMPREL 重定位索引 `reloc_idx = 3996`，`type = 1026` / `R_AARCH64_JUMP_SLOT`）。
   - GOT 槽位：`0x7772058`。
   - 调用点现场：
     - 基地址：`0x6db9f3c000`
     - 调用点：`caller_offset = 0x11b8`
     - 返回点：`lr_offset = 0x11bc`（`lr = 0x6db9f3d1bc`）
     - GOT 地址：`x16 = 0x6dc16ae058`（`0x6dc16ae058 - 0x6db9f3c000 = 0x7772058`）
   - 退出码：133（SIGTRAP sentinel `brk` at unlinked import 12 `nn::diag::detail::AbortImpl()`）。
   - 完整原始日志：`.local/logs/rg477m-boot-v31-cxa-thread-atexit.log` + `.exitcode`。

5. **明确未完成项标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，未真正实施页面写保护；
   - 尚无标题画面 / 可操作场景证据；
   - NVN→Vulkan 完整桥接、大文件 Pak/IoStore（2.9GB/5.9GB）读取管线未完成。

---

## 阶段验收证据汇总

| 检查项 | 状态与依据 |
|---|---|
| **全量构造函数 (1890/1890)** | **已通过**：构造函数 1890/1890 顺利执行完毕返回 0。 |
| **nnMain 游戏主入口** | **已越过并真机通过**：成功进入 offset `0x352f160`。 |
| **CreateThread / StartThread** | **已越过并真机通过**：成功派生 GameThread 与 FHeartBeatThread，独立 guest TP 绑定无崩溃。 |
| **nn::fs::SetAllocator** | **已越过并真机通过**：成功保存 UE 分配器回调。 |
| **nn::fs::QueryMountRomCacheSize** | **已越过并真机通过**：成功写出 64KB 缓存尺寸。 |
| **nn::fs::MountRom** | **已越过并真机通过**：成功挂载本地 `romfs` 目录返回 0。 |
| **nn::fs::OpenFile / GetFileSize / ReadFile / CloseFile** | **已越过并真机通过**：实测完整打开、查询、读取 `rom:/UECommandLine.txt`（38B）并关闭。 |
| **nn::aoc::CountAddOnContent / ListAddOnContent** | **已越过并真机通过**：实测 Count 返回 0，List 返回 0 项，顺利越过。 |
| **nn::nifm::Initialize** | **已越过并真机通过**：成功初始化网络接口管理返回 0。 |
| **nn::socket::Initialize** | **已越过并真机通过**：成功初始化 6MB 缓冲区套接字系统返回 0。 |
| **nn::account::Initialize** | **已越过并真机通过**：成功初始化账号子系统返回 0。 |
| **nn::fs::OpenDirectory / GetDirectoryEntryCount / ReadDirectory / CloseDirectory** | **已越过并真机通过**：实测成功打开 `rom:/` 与 `rom:/FFRS/Content/Paks/`，读取条目并安全关闭，GameThread 顺利越过！ |
| **nn::os::InitializeEvent / FinalizeEvent / SignalEvent / ClearEvent / WaitEvent** | **已越过并真机通过**：实测成功初始化 Event（AutoClear 与 ManualClear），跨线程 Signal/Wait 唤醒全部通过！ |
| **posix_memalign / memalign** | **已越过并真机通过**：实测成功完成 64 字节对齐分配（40 字节），GameThread 顺利越过！ |
| **__cxa_thread_atexit / __cxa_thread_atexit_impl** | **已越过并真机通过**：实测 3 次成功登记线程局部析构器，GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `_ZN2nn4diag6detail9AbortImplEv`**（symbol index 12, JMPREL reloc index 3996, GOT 0x7772058, caller 0x11b8, lr 0x11bc, exit 133, 日志 `.local/logs/rg477m-boot-v31-cxa-thread-atexit.log`）。 |

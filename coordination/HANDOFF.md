# Antigravity接力入口：越过 nn::os::InitializeEvent，推进至 posix_memalign（P-AG-13）

## 调度备注（夜守 PM）
- **当前阻塞**：`posix_memalign`（symbol index 190, JMPREL reloc index 186, GOT `0x77725d0`, caller offset `0xadbc8`, lr offset `0xadbcc`, exit 133）
- **基线 HEAD**：本轮提交检查点（InitializeEvent & Event synchronization primitives HLE 已过）；日志 `.local/logs/rg477m-boot-v29-initializeevent.log`
- **P-AG-13**：已闭环（`nn::os::InitializeEvent`、`FinalizeEvent`、`SignalEvent`、`ClearEvent`、`WaitEvent`、`TryWaitEvent`、`TimedWaitEvent` 成功落地，GameThread 成功初始化事件、映射内存页面并推进 atexit 至 13560 项）；无其他并行 agy worker
- **阶段**：`T02_OS_EVENT_PASSED_POSIX_MEMALIGN_REACHED_ON_RG477M`
- **未达**：标题画面 / 可操作场景（勿伪称可玩）

本轮按工单 P-AG-13 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::os::InitializeEvent` 及完整 Event 同步原语全链路落地闭环**：
   - 依据 Nintendo SDK 官方反编译及 Horizon ABI，精准定义 `EventType` 内存镜像结构（`prev`/`next` 循环自引用链表头、`signaled` 标志、`initially_signaled`、`clear_mode` [0=ManualClear, 1=AutoClear]、`state` [1=Initialized]、`counter`、`internal_cs`、`internal_cv`），总长 40 字节（`0x28`），并在 guest 内存写入真实布局。
   - 宿主侧实现线程安全的事件注册表池（`s_event_registry`，搭配 `pthread_mutex_t` 与 `pthread_cond_t`），完整实现 `InitializeEvent`、`FinalizeEvent`、`SignalEvent`、`ClearEvent`、`WaitEvent`、`TryWaitEvent` 与 `TimedWaitEvent`。
   - 严格支持 AutoClear（唤醒或 try_wait 成功时自动重置为 unsignaled）与 ManualClear（广播唤醒且保持 signaled 状态直至显式 ClearEvent），支持带纳秒超时的 `TimedWaitEvent`。
   - 在 `guest_process.c` 中将相关符号与别名登记入 `typed` 白名单与 `import_signature`，子线程 `GameThread` 无缝复用 Native Boundary 网关分派。

2. **宿主与真机契约全绿（34 项契约全部 PASS）**：
   - 增加 `--os-event` 独立契约测试用例 `os_event_contract`，全面覆盖符号与别名解析、空指针/无效参数保护、AutoClear 自动清除、ManualClear 手动清除、纳秒级 TimedWait 超时与命中、跨子线程异步 Signal/Wait 唤醒通知及 Finalize 生命周期管理。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 34 项宿主契约（含 `host-os-event-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --os-event`：
     `{"os_event_errors":0,"init":true,"auto_clear":true,"manual_clear":true,"timed_wait":true,"cross_thread":true,"finalize":true}`。
   - 复跑 `--fs-open-dir`、`--account`、`--socket`、`--fs-open-file`、`--threads` 与 `--guest-tp` 均全绿通过。

3. **真机实测重大突破：越过 InitializeEvent，虚幻引擎顺利完成事件创建、内存映射推进至 posix_memalign**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [hle] nn::fs::CloseDirectory: closed handle=0x5cac08f7e0 (path='rom:/')
     [crt] atexit registered callback=0x78920c1500 pending=13550; deferred
     [crt] atexit registered callback=0x78923e3570 pending=13551; deferred
     [crt] atexit registered callback=0x78923e3580 pending=13552; deferred
     [crt] atexit registered callback=0x7891f7da80 pending=13553; deferred
     [hle] nn::os::InitializeEvent: event=0x6951e9a330 initially_signaled=0 clear_mode=1 (AutoClear)
     [page-port-policy] op=map address=0x7111e00000 bytes=86016 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x72d2381000 bytes=4096 result=0 after real VM/accounting transition
     [crt] atexit registered callback=0x7892047530 pending=13554; deferred
     [crt] atexit registered callback=0x7892045520 pending=13555; deferred
     [page-port-policy] op=map address=0x6951e9b000 bytes=4096 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x7271e60000 bytes=131072 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x6951e9c000 bytes=4096 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x6951e9d000 bytes=4096 result=0 after real VM/accounting transition
     [crt] atexit registered callback=0x78920c1830 pending=13556; deferred
     [crt] atexit registered callback=0x78920c13e0 pending=13557; deferred
     [crt] atexit registered callback=0x7892045990 pending=13558; deferred
     [crt] atexit registered callback=0x7892045950 pending=13559; deferred
     [crt] atexit registered callback=0x789208ec10 pending=13560; deferred
     [page-port-policy] op=map address=0x6971eae000 bytes=4096 result=0 after real VM/accounting transition
     [fault] signal=5 pc=0x7b3893d5f0 fa=0x7b3893d5f0
      x0=0x72d22806d8  x1=0x40  x2=0x28  x3=0x6971eae8f8
      ...
      x16=0x78996395d0  x17=0x7b3893d5f0  x18=0x65  x19=0xb4000079b44ca240
      ...
      fp=0x72d22806e0 lr=0x7891f74bcc sp=0x72d22806d0
     ```
   - 子线程 `GameThread` 成功调用并完全越过 `nn::os::InitializeEvent`（`event=0x6951e9a330`, `AutoClear`）！
   - 虚幻引擎顺利完成事件同步原语绑定，连续进行 86KB、4KB、131KB 等多次页面映射，注册 7 项 atexit 回调至 13560 项，最终平稳触达未绑定导入 `posix_memalign`。

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`posix_memalign`（`int posix_memalign(void **memptr, size_t alignment, size_t size)`）。
   - 符号索引：`symbol index = 190`（JMPREL 重定位索引 `reloc_idx = 186`）。
   - GOT / JMPREL 槽位：`0x77725d0`。
   - 调用点现场：
     - 基地址：`0x7891ec7000`
     - 调用点：`caller_offset = 0xadbc8`
     - 返回点：`lr_offset = 0xadbcc`（`lr = 0x7891f74bcc`）
     - GOT 地址：`x16 = 0x78996395d0`（`0x78996395d0 - 0x7891ec7000 = 0x77725d0`）
     - 实参分析：
       - `x0 = 0x72d22806d8`：`void **memptr` 出参指针
       - `x1 = 0x40`：`size_t alignment = 64`
       - `x2 = 0x28`：`size_t size = 40`
   - 退出码：133（SIGTRAP sentinel `brk` at unlinked import 190 `posix_memalign`）。
   - 完整原始日志：`.local/logs/rg477m-boot-v29-initializeevent.log` + `.exitcode`。

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
| **CreateThread / StartThread** | **已越过并真机通过**：成功派生 GameThread，独立 guest TP 绑定无崩溃。 |
| **nn::fs::SetAllocator** | **已越过并真机通过**：成功保存 UE 分配器回调。 |
| **nn::fs::QueryMountRomCacheSize** | **已越过并真机通过**：成功写出 64KB 缓存尺寸。 |
| **nn::fs::MountRom** | **已越过并真机通过**：成功挂载本地 `romfs` 目录返回 0。 |
| **nn::fs::OpenFile / GetFileSize / ReadFile / CloseFile** | **已越过并真机通过**：实测完整打开、查询、读取 `rom:/UECommandLine.txt`（38B）并关闭。 |
| **nn::aoc::CountAddOnContent / ListAddOnContent** | **已越过并真机通过**：实测 Count 返回 0，List 返回 0 项，顺利越过。 |
| **nn::nifm::Initialize** | **已越过并真机通过**：成功初始化网络接口管理返回 0。 |
| **nn::socket::Initialize** | **已越过并真机通过**：成功初始化 6MB 缓冲区套接字系统返回 0。 |
| **nn::account::Initialize** | **已越过并真机通过**：成功初始化账号子系统返回 0。 |
| **nn::fs::OpenDirectory / GetDirectoryEntryCount / ReadDirectory / CloseDirectory** | **已越过并真机通过**：实测成功打开 `rom:/`，获取 7 个条目，读取 7 个条目并安全关闭，GameThread 顺利越过！ |
| **nn::os::InitializeEvent / FinalizeEvent / SignalEvent / ClearEvent / WaitEvent** | **已越过并真机通过**：实测成功初始化 Event（AutoClear），GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `posix_memalign`**（symbol index 190, JMPREL reloc index 186, GOT 0x77725d0, caller 0xadbc8, lr 0xadbcc, exit 133, 日志 `.local/logs/rg477m-boot-v29-initializeevent.log`）。 |

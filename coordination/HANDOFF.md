# Antigravity接力入口：越过 posix_memalign，推进至 __cxa_thread_atexit（P-AG-14）

## 调度备注（夜守 PM）
- **当前阻塞**：`__cxa_thread_atexit`（symbol index 130, JMPREL reloc index 129, GOT `0x7772408`, caller offset `0x221e8`, lr offset `0x221ec`, exit 133）
- **基线 HEAD**：本轮提交检查点（posix_memalign & aligned allocation HLE 已过）；日志 `.local/logs/rg477m-boot-v30-posixmemalign.log`
- **P-AG-14**：已闭环（`posix_memalign`、`memalign`、`aligned_alloc`、`valloc`、`free` 成功落地，GameThread 成功完成 64 字节对齐分配 `ptr=0xb4000074efb8ed40` 并平稳推进至 `__cxa_thread_atexit`）；无其他并行 agy worker
- **阶段**：`T02_POSIX_MEMALIGN_PASSED_CXA_THREAD_ATEXIT_REACHED_ON_RG477M`
- **未达**：标题画面 / 可操作场景（勿伪称可玩）

本轮按工单 P-AG-14 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `posix_memalign` 及对齐分配全链路落地闭环**：
   - 严格遵循 POSIX/musl ABI 规范实现 `posix_memalign`，校验 `memptr` 非空、`alignment` 为 2 的幂且不小于 `sizeof(void *)`，安全处理 `size == 0` 分配最小保护空间；
   - 内存分配成功后执行 `memset(ptr, 0, alloc_size)` 清零，杜绝脏内存导致未定义行为，安全写入 `*memptr` 并返回 0；
   - 紧随落地配套对齐分配与释放原语 `memalign`、`aligned_alloc`、`valloc` 与 `free`；
   - 在 `guest_process.c` 中将相关符号登记入 `typed` 白名单，并在 `import_signature` 中精确绑定寄存器签名（`posix_memalign` 3 参 1 返，`memalign`/`aligned_alloc` 2 参 1 返，`valloc` 1 参 1 返，`free` 1 参 0 返），GameThread 子线程无缝复用 Native Boundary 统一网关。

2. **宿主与真机契约全绿（35 项契约全部 PASS）**：
   - 增加 `--posix-memalign` 独立契约测试用例 `posix_memalign_contract`，全面覆盖符号解析、空指针/无效对齐保护、UE4 真实 64 字节对齐参数分配、4KB 页面对齐分配、0 尺寸分配及 `memalign` 直接分配；
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 35 项宿主契约（含 `host-posix-memalign-contract`，returncode 0）；
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --posix-memalign`：
     `{"posix_memalign_errors":0,"resolve":true,"invalid_params":true,"alloc_64":true,"alloc_page":true,"zero_size":true,"memalign":true}`；
   - 复跑 `--os-event`、`--fs-open-dir`、`--account`、`--socket`、`--fs-open-file`、`--threads` 与 `--guest-tp` 均全绿通过。

3. **真机实测重大突破：越过 posix_memalign，虚幻引擎顺利完成内存对齐分配并推进至 __cxa_thread_atexit**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [hle] nn::os::InitializeEvent: event=0x645d49a330 initially_signaled=0 clear_mode=1 (AutoClear)
     [page-port-policy] op=map address=0x6c1d400000 bytes=86016 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x6ddd981000 bytes=4096 result=0 after real VM/accounting transition
     [crt] atexit registered callback=0x739d687530 pending=13554; deferred
     [crt] atexit registered callback=0x739d685520 pending=13555; deferred
     [page-port-policy] op=map address=0x645d49b000 bytes=4096 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x6d7d460000 bytes=131072 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x645d49c000 bytes=4096 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x645d49d000 bytes=4096 result=0 after real VM/accounting transition
     [crt] atexit registered callback=0x739d701830 pending=13556; deferred
     [crt] atexit registered callback=0x739d7013e0 pending=13557; deferred
     [crt] atexit registered callback=0x739d685990 pending=13558; deferred
     [crt] atexit registered callback=0x739d685950 pending=13559; deferred
     [crt] atexit registered callback=0x739d6cec10 pending=13560; deferred
     [page-port-policy] op=map address=0x647d4ae000 bytes=4096 result=0 after real VM/accounting transition
     [hle] posix_memalign: memptr=0x6ddd8806d8 alignment=64 (0x40) size=40 (0x28) -> *memptr=0xb4000074efb8ed40 rc=0
     [fault] signal=5 pc=0x7641b50410 fa=0x7641b50410
      x0=0x739d529650  x1=0xb40000749fb92240  x2=0x73a4c22a00  x3=0xffffffffffffffff
      x4=0xffffffffffffffff  x5=0x6ddd880090  x6=0x3d637220  x7=0x3d7274706d656d2a
      x8=0x1  x9=0x0  x10=0x0  x11=0x627d298958
      x12=0x17d  x13=0x6ddd87f674  x14=0x1  x15=0x1ffff
      x16=0x73a4c79408  x17=0x7641b50410  x18=0x65  x19=0xb40000749fb92240
      x20=0xb40000749fb92010  x21=0x6ddd8807ec  x22=0x6ddd8807e8  x23=0x0
      x24=0x1  x25=0xb40000749fb92260  x26=0x0  x27=0x6ddd8808b8
      x28=0x73a4d13300 
      fp=0x6ddd880730 lr=0x739d5291ec sp=0x6ddd880730
     ```
   - 子线程 `GameThread` 成功调用并完全越过 `posix_memalign`（`memptr=0x6ddd8806d8`, `alignment=64`, `size=40` -> `*memptr=0xb4000074efb8ed40`, `rc=0`）！
   - 虚幻引擎顺利获得对齐内存并继续执行，平稳触达未绑定导入 `__cxa_thread_atexit`。

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`__cxa_thread_atexit`（`int __cxa_thread_atexit(void (*dtor)(void *), void *obj, void *dso_symbol)`）。
   - 符号索引：`symbol index = 130`（JMPREL 重定位索引 `reloc_idx = 129`）。
   - GOT / JMPREL 槽位：`0x7772408`。
   - 调用点现场：
     - 基地址：`0x739d507000`
     - 调用点：`caller_offset = 0x221e8`
     - 返回点：`lr_offset = 0x221ec`（`lr = 0x739d5291ec`）
     - GOT 地址：`x16 = 0x73a4c79408`（`0x73a4c79408 - 0x739d507000 = 0x7772408`）
     - 实参分析：
       - `x0 = 0x739d529650`：`void (*dtor)(void *)` 析构函数指针（offset `0x22650`）
       - `x1 = 0xb40000749fb92240`：`void *obj` 线程局部变量对象指针
       - `x2 = 0x73a4c22a00`：`void *dso_symbol` 模块 DSO 符号句柄
   - 退出码：133（SIGTRAP sentinel `brk` at unlinked import 130 `__cxa_thread_atexit`）。
   - 完整原始日志：`.local/logs/rg477m-boot-v30-posixmemalign.log` + `.exitcode`。

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
| **posix_memalign / memalign** | **已越过并真机通过**：实测成功完成 64 字节对齐分配（40 字节），GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `__cxa_thread_atexit`**（symbol index 130, JMPREL reloc index 129, GOT 0x7772408, caller 0x221e8, lr 0x221ec, exit 133, 日志 `.local/logs/rg477m-boot-v30-posixmemalign.log`）。 |

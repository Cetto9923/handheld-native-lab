# Antigravity接力入口：越过 nn::fs::OpenDirectory，推进至 nn::os::InitializeEvent（P-AG-12）

## 调度备注（夜守 PM）
- **当前阻塞**：`nn::os::InitializeEvent`（`_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE`，import 132，GOT `0x7772418`，lr offset `0x25bd0`，caller offset `0x25bcc`，exit 133）
- **基线 HEAD**：本轮提交检查点（OpenDirectory & directory enumeration HLE 已过）；日志 `.local/logs/rg477m-boot-v28-opendirectory.log`
- **P-AG-12**：已闭环（OpenDirectory、GetDirectoryEntryCount、ReadDirectory、CloseDirectory 成功落地，已完成虚幻引擎 7 项 romfs 核心目录枚举）；无其他并行 agy worker
- **阶段**：`T02_FS_OPENDIRECTORY_PASSED_OS_INITIALIZEEVENT_REACHED_ON_RG477M`
- **未达**：标题画面 / 可操作场景（勿伪称可玩）

本轮按工单 P-AG-12 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::fs::OpenDirectory` 及完整目录枚举 HLE 全链路落地闭环**：
   - 实现了 `nn::fs::OpenDirectory`、`nn::fs::GetDirectoryEntryCount`、`nn::fs::ReadDirectory`、`nn::fs::CloseDirectory` 及别名和 `CreateDirectory`。
   - 严格依据 Horizon / Nintendo SDK / SwitchBrew ABI 定义了 `nn_fs_directory_entry`（尺寸精确为 784 字节 / `0x310`，`name[0x301]`、`type` 0=Dir/1=File、`file_size` 64-bit），并通过 `_Static_assert` 约束对齐与偏移。
   - 实现目录句柄表池管理（`s_fs_open_dirs`），按 `mode`（1=Dir, 2=File, 3=All）在已挂载 `rom`（宿主 `romfs`）上执行扫描快照，提供无文件描述符泄露的平滑流式读取、精确计数及资源安全释放。
   - 在 `guest_process.c` 中将相关符号与别名登记入 `typed` 白名单与 `import_signature`，子线程 `GameThread` 无缝复用 Native Boundary 网关分派。

2. **宿主与真机契约全绿（33 项契约全部 PASS）**：
   - 增加 `--fs-open-dir` 独立契约测试用例 `fs_open_dir_contract`，全面覆盖符号与别名解析、空指针/无效路径校验、目录过滤模式（Dir/File/All）、分块与尽头读取、关闭句柄失效性检查及真实 `rom:/` 目录探测。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 33 项宿主契约（含 `host-fs-open-dir-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --fs-open-dir`：
     `{"fs_open_dir_errors":0,"open":true,"count":true,"read":true,"close":true,"validation":true}`。
   - 复跑 `--account`、`--socket`、`--fs-open-file`、`--threads` 与 `--guest-tp` 均全绿通过。

3. **真机实测重大突破：越过 OpenDirectory，虚幻引擎成功枚举 7 项 romfs 目录并推进至 nn::os::InitializeEvent**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [hle] nn::fs::GetEntryType: path='rom:/FFRS/FFRS.uproject' (host_path='romfs/FFRS/FFRS.uproject') stat failed errno=2
     [hle] nn::fs::OpenDirectory: path='rom:/' mode=3 (host_path='romfs') total_entries=7 -> handle=0x63e35aa1a0
     [hle] nn::fs::GetDirectoryEntryCount: handle=0x63e35aa1a0 -> count=7
     [page-port-policy] op=map address=0x70a6400000 bytes=28672 result=0 after real VM/accounting transition
     [page-port-policy] op=map address=0x7586982000 bytes=4096 result=0 after real VM/accounting transition
     [hle] nn::fs::ReadDirectory: handle=0x63e35aa1a0 req_max=7 read_entries=7 (cursor=7/7)
     [hle] nn::fs::CloseDirectory: closed handle=0x63e35aa1a0 (path='rom:/')
     [crt] atexit registered callback=0x7b4667d500 pending=13550; deferred
     [crt] atexit registered callback=0x7b4699f570 pending=13551; deferred
     [crt] atexit registered callback=0x7b4699f580 pending=13552; deferred
     [crt] atexit registered callback=0x7b46539a80 pending=13553; deferred
     [fault] signal=5 pc=0x7dea6cb420 fa=0x7dea6cb420
      x0=0x6c0649a330  x1=0x0  x2=0x1  x3=0x7586880858
      ...
      x16=0x7b4dbf5418  x17=0x7dea6cb420  x18=0x65  x19=0x0
      ...
      fp=0x75868808c0 lr=0x7b464a8bd0 sp=0x75868808c0
     ```
   - 子线程 `GameThread` 成功调用并完全越过 `nn::fs::OpenDirectory`、`GetDirectoryEntryCount`、`ReadDirectory`、`CloseDirectory`！
   - 虚幻引擎顺利完成目录内容解析，并在获取 7 个目录项后触发 28KB + 4KB 页面映射，继续注册 4 项 atexit 回调（推进至 13553 项），最终平稳触达未绑定导入 132 `_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE`。

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE`（`nn::os::InitializeEvent(nn::os::EventType*, bool, nn::os::EventClearMode)`）。
   - 符号索引：`symbol index = 132`。
   - GOT / JMPREL 槽位：`0x7772418`。
   - 调用点现场：
     - 基地址：`0x7b46483000`
     - 调用点：`caller_offset = 0x25bcc`
     - 返回点：`lr_offset = 0x25bd0`（`lr = 0x7b464a8bd0`）
     - GOT 地址：`x16 = 0x7b4dbf5418`（`0x7b4dbf5418 - 0x7b46483000 = 0x7772418`）
     - 实参分析：
       - `x0 = 0x6c0649a330`：`EventType *event` 事件对象指针
       - `x1 = 0x0`：`bool initially_signaled = false`
       - `x2 = 0x1`：`EventClearMode = EventClearMode_AutoClear (1)`
   - 退出码：133（SIGTRAP sentinel `brk` at unlinked import 132）。
   - 完整原始日志：`.local/logs/rg477m-boot-v28-opendirectory.log` + `.exitcode`。

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
| **当前阻塞前沿** | **确证为 `nn::os::InitializeEvent`**（`_ZN2nn2os15InitializeEventEPNS0_9EventTypeEbNS0_14EventClearModeE`，import index 132，GOT 0x7772418，caller 0x25bcc，lr 0x25bd0，exit 133，日志 `.local/logs/rg477m-boot-v28-opendirectory.log`）。 |

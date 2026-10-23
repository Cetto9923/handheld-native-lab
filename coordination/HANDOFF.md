# Antigravity接力入口：越过 nn::account::Initialize，推进至 nn::fs::OpenDirectory（P-AG-11）

## 调度备注（夜守 PM）
- **当前阻塞**：`nn::fs::OpenDirectory`（`_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci`，import 37，GOT `0x7772120`，lr offset `0x11c44`，caller offset `0x11c40`，exit 133）
- **基线 HEAD**：本轮提交检查点（account HLE 已过）；日志 `.local/logs/rg477m-boot-v27-accountinit.log`
- **P-AG-11**：已闭环（account::Initialize 成功，已推进至虚幻引擎目录枚举探测 `OpenDirectory`）；无其他并行 agy worker
- **阶段**：`T02_ACCOUNT_INIT_PASSED_FS_OPENDIRECTORY_REACHED_ON_RG477M`
- **未达**：标题画面 / 可操作场景（勿伪称可玩）

本轮按工单 P-AG-11 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::account::Initialize` HLE 全链路落地闭环**：
   - 实现了 `nn::account::Initialize`（`_ZN2nn7account10InitializeEv` 及别名 `nnaccountInitialize`），按 Horizon / Nintendo SDK ABI 实现无参账号子系统初始化，记录并返回 0 (`ResultSuccess`)，保留清晰可归因日志 `[hle] nn::account::Initialize: initialized account system -> returning 0`。
   - 在 `guest_process.c` 中将 `_ZN2nn7account10InitializeEv` 与 `nnaccountInitialize` 登记入 `typed` 白名单与 `import_signature`（0 参数，1 返回值），子线程 `GameThread` 无缝复用 Native Boundary 网关分派。

2. **宿主与真机契约全绿（32 项契约全部 PASS）**：
   - 增加 `--account` 独立契约测试用例 `account_contract`，全面覆盖符号解析、别名调用、初始化状态记录及基线返回 0 验证。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 32 项宿主契约（含 `host-account-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --account`：
     `{"account_contract_errors":0,"initialize":true,"baseline_zero":true}`。
   - 复跑 `--socket`、`--nifm`、`--aoc`、`--fs-open-file`、`--fs-mount-rom`、`--threads` 与 `--guest-tp` 均验证通过：
     `{"socket_contract_errors":0,"initialize":true,"baseline_zero":true}`
     `{"nifm_contract_errors":0,"initialize":true,"baseline_zero":true}`
     `{"aoc_contract_errors":0,"count":true,"list":true,"query_cache":true,"mount":true,"baseline_zero":true}`
     `{"fs_open_file_errors":0,"open":true,"read":true,"size":true,"close":true,"entry_type":true,"validation":true}`
     `{"fs_mount_rom_errors":0,"mount":true,"unmount":true,"validation":true}`
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测重大突破：越过 nn::account::Initialize，激活 UE 核心探测，推进至 nn::fs::OpenDirectory**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [hle] nn::socket::Initialize: buffer=0x6fa6fcb000 size=6291456 (0x600000) alloc_size=131072 (0x20000) concurrency=14 -> returning 0
     [hle] nn::account::Initialize: initialized account system -> returning 0
     [crt] atexit registered callback=0x6f9f4896d0 pending=13539; deferred
     [crt] atexit registered callback=0x6f9f489710 pending=13540; deferred
     [crt] atexit registered callback=0x6f9f489750 pending=13541; deferred
     [crt] atexit registered callback=0x6f9f489790 pending=13542; deferred
     [crt] atexit registered callback=0x6f9f4d1fb0 pending=13543; deferred
     [page-port-policy] op=map address=0x69df783000 bytes=4096 result=0 after real VM/accounting transition
     [crt] atexit registered callback=0x6f9f3b7970 pending=13544; deferred
     [crt] atexit registered callback=0x6f9f3b7980 pending=13545; deferred
     [crt] atexit registered callback=0x6f9f576090 pending=13546; deferred
     [crt] atexit registered callback=0x6f9f504890 pending=13547; deferred
     [crt] atexit registered callback=0x6f9f3b7750 pending=13548; deferred
     [crt] atexit registered callback=0x6f9f30ce50 pending=13549; deferred
     [hle] nn::fs::GetEntryType: path='rom:/FFRS/FFRS.uproject' (host_path='romfs/FFRS/FFRS.uproject') stat failed errno=2
     [fault] signal=5 pc=0x72491d4128 fa=0x72491d4128
      x0=0x69df680758  x1=0x69df680630  x2=0x3  x3=0x0
      x4=0x601f21c71a  x5=0x601f21c73a  x6=0x0  x7=0x3a006d006f0072
      x8=0x69df680636  x9=0x6  x10=0xdc00  x11=0xfca02400
      x12=0xffffffff  x13=0xfffe  x14=0xffffffff  x15=0x0
      x16=0x6fa6a7c120  x17=0x72491d4128  x18=0x0  x19=0x69df680820
      x20=0x603f26ca80  x21=0x6fa6f0e5a0  x22=0x601f21c720  x23=0x0
      x24=0x6  x25=0x69df6806b0  x26=0x6  x27=0x6fa7f344c0
      x28=0x116000 
      fp=0x69df6807b0 lr=0x6f9f31bc44 sp=0x69df680600
     ```
   - 子线程 `GameThread` 成功调用并完全越过 `nn::account::Initialize`（返回 0）！
   - 虚幻引擎顺利完成互斥锁群初始化与 atexit 注册，触发 4KB 页面分配，并激活引擎启动核心文件探测（已成功由宿主处理 `GetEntryType: rom:/FFRS/FFRS.uproject`），随后推进至目录打开 `nn::fs::OpenDirectory`！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci`（`nn::fs::OpenDirectory(nn::fs::DirectoryHandle*, const char*, int)`）。
   - 符号索引：`symbol index = 37`（JMPREL 重定位索引 `reloc_idx = 36`）。
   - GOT / JMPREL 槽位：`0x7772120`。
   - 调用点现场：
     - 基地址：`0x6f9f30a000`
     - 调用点：`caller_offset = 0x11c40`
     - 返回点：`lr_offset = 0x11c44`（`lr = 0x6f9f31bc44`）
     - GOT 地址：`x16 = 0x6fa6a7c120`（`0x6fa6a7c120 - 0x6f9f30a000 = 0x7772120`）
     - 实参分析：
       - `x0 = 0x69df680758`：`out_handle` 输出句柄指针（栈内存）
       - `x1 = 0x69df680630`：目录路径指针
       - `x2 = 0x3`：目录打开模式（`OpenDirectoryMode_All = 3`）
   - 退出码：133（SIGTRAP sentinel `brk` at unlinked import 37）。
   - 完整原始日志：`.local/logs/rg477m-boot-v27-accountinit.log` + `.exitcode`。

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
| **nn::account::Initialize** | **已越过并真机通过**：成功初始化账号子系统返回 0，GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `nn::fs::OpenDirectory`**（`_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci`，import index 37，GOT 0x7772120，caller 0x11c40，lr 0x11c44，exit 133，日志 `.local/logs/rg477m-boot-v27-accountinit.log`）。 |

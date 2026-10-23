# Antigravity接力入口：越过 nn::socket::Initialize，推进至 nn::account::Initialize（P-AG-10）


## 调度备注（夜守 PM）
- **当前阻塞**：`nn::account::Initialize`（`_ZN2nn7account10InitializeEv`，import 364，GOT `0x7772ae8`，lr offset `0x352f53c`，caller offset `0x352f538`，exit 133）
- **下一动作**：调度下一工单针对 `nn::account::Initialize` 推进
- **基线 HEAD**：本检查点提交（socket HLE 已过）；日志 `.local/logs/rg477m-boot-v26-socketinit.log`
- **P-AG-10**：已闭环；无其他 agy agent 在跑

本轮按工单 P-AG-10 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::socket::Initialize` HLE 全链路落地闭环**：
   - 实现了 `nn::socket::Initialize`（`_ZN2nn6socket10InitializeEPvmmi` 及别名 `nnsocketInitialize`），按 Horizon / Nintendo SDK ABI 正确接收工作缓冲区 `work_buffer`、尺寸 `work_buffer_size`、分配器大小 `allocator_size` 及并发度 `concurrency`，记录并返回 0 (`ResultSuccess`)，保留清晰可归因日志 `[hle] nn::socket::Initialize: buffer=... size=... alloc_size=... concurrency=... -> returning 0`。
   - 在 `guest_process.c` 中将 `_ZN2nn6socket10InitializeEPvmmi` 与 `nnsocketInitialize` 登记入 `typed` 白名单与 `import_signature`（4 参数，1 返回值），子线程 `GameThread` 无缝复用 Native Boundary 网关分派。

2. **宿主与真机契约全绿（31 项契约全部 PASS）**：
   - 增加 `--socket` 独立契约测试用例 `socket_contract`，全面覆盖符号解析、别名调用、初始化状态记录及基线返回 0 验证。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 31 项宿主契约（含 `host-socket-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --socket`：
     `{"socket_contract_errors":0,"initialize":true,"baseline_zero":true}`。
   - 复跑 `--nifm`、`--aoc`、`--fs-open-file`、`--fs-mount-rom`、`--threads` 与 `--guest-tp` 均验证通过：
     `{"nifm_contract_errors":0,"initialize":true,"baseline_zero":true}`
     `{"aoc_contract_errors":0,"count":true,"list":true,"query_cache":true,"mount":true,"baseline_zero":true}`
     `{"fs_open_file_errors":0,"open":true,"read":true,"size":true,"close":true,"entry_type":true,"validation":true}`
     `{"fs_mount_rom_errors":0,"mount":true,"unmount":true,"validation":true}`
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测突破：越过 nn::socket::Initialize，推进至 nn::account::Initialize**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x6edbfbfacc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x6ede415570 entry=0x6ed8d1a1f0 arg=0x0 stack=0x6915981000 size=0x100000 prio=16 tid=21008
     [hle] nn::os::StartThread: thread=0x6ede415570 tid=21008
     [hle] thread tid=21008 bound guest TP base=0xb400007037e57220 span=4144
     [hle] child thread tid=21008 entering guest_entry=0x6ed8d1a1f0 sp=0x6915a81000 arg=0x0
     [hle] nn::os::SetThreadName: thread=0x6ede415570 name='GameThread'
     [hle] nn::fs::SetAllocator: alloc=0x6ed57fe2d0 free=0x6ed58074c0
     [hle] nn::fs::QueryMountRomCacheSize: out_size=0x6915a80de8 -> 65536 (0x10000)
     [page-port-policy] op=map address=0x66b5610000 bytes=65536 result=0 after real VM/accounting transition
     [hle] nn::fs::MountRom: mounted 'rom' buffer=0x66b5610000 size=65536 (0x10000) host_path='romfs'
     [hle] nn::fs::OpenFile: path='rom:/UECommandLine.txt' mode=1 (host_path='romfs/UECommandLine.txt') size=38 -> handle=0x5a838fb8f8 (fd=3)
     [hle] nn::fs::GetFileSize: handle=0x5a838fb8f8 size=38
     [hle] nn::fs::ReadFile: handle=0x5a838fb8f8 offset=0 req_size=38 read_bytes=38
     [hle] nn::fs::CloseFile: closed handle=0x5a838fb8f8 (path='rom:/UECommandLine.txt')
     [crt] atexit registered callback=0x6ed57ed4f0 pending=13537; deferred
     [hle] nn::aoc::CountAddOnContent: returning 0 (no add-on content installed)
     [hle] nn::aoc::ListAddOnContent: out_indices=0x6915a808d8 offset=0 count=256 -> returning 0
     [crt] atexit registered callback=0x6ed596b0d0 pending=13538; deferred
     [hle] nn::nifm::Initialize: initialized network interface manager -> returning 0
     [hle] nn::socket::Initialize: buffer=0x6edd4ac000 size=6291456 (0x600000) alloc_size=131072 (0x20000) concurrency=14 -> returning 0
     [fault] signal=5 pc=0x717b66fb60 fa=0x717b66fb60
      x0=0x3c5  x1=0x5a838ba774  x2=0x6c  x3=0x1
      x4=0x6ed589da00  x5=0x6915a80804  x6=0x31  x7=0x3d79636e65727275
      x8=0x6edbfc4e08  x9=0x6ed8d25f80  x10=0x3c5  x11=0x0
      x12=0x20  x13=0xb400007127e54e00  x14=0x1  x15=0x0
      x16=0x6edcf5dae8  x17=0x717b66fb60  x18=0x0  x19=0x5f7566ce80
      x20=0x6915a80e60  x21=0x7177f22f38  x22=0x520f  x23=0x520d
      x24=0x6ee58e9bb0  x25=0x6ee58e9bb0  x26=0x6ee58e9ef8  x27=0x114000
      x28=0x116000 
      fp=0x6915a80fa0 lr=0x6ed8d1a53c sp=0x6915a80e00
     ```
   - 子线程 `GameThread` 成功调用并完全越过 `nn::socket::Initialize`（返回 0）！
   - 虚幻引擎网络/套接字初始化完成后，主初始化路径立即推进至账号系统初始化 `nn::account::Initialize`！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn7account10InitializeEv`（`nn::account::Initialize()`）。
   - 符号索引：`symbol index = 364`（JMPREL 重定位索引 `reloc_idx = 349`）。
   - GOT / JMPREL 槽位：`0x7772ae8`。
   - 调用点现场：
     - `lr = 0x6ed8d1a53c`（基址 `0x6ed57eb000`，调用点 `caller_offset = 0x352f538`，返回点 `lr_offset = 0x352f53c`）
     - `x16 = 0x6edcf5dae8`（GOT 偏移 `0x7772ae8`）
     - `x17 = pc = 0x717b66fb60`（sentinel brk in unlinked stub）
   - 退出码：133（SIGTRAP sentinel `brk` at unlinked import 364）。
   - 完整原始日志：`.local/logs/rg477m-boot-v26-socketinit.log` + `.exitcode`。

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
| **nn::socket::Initialize** | **已越过并真机通过**：成功初始化 6MB 缓冲区套接字系统返回 0，GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `nn::account::Initialize`**（`_ZN2nn7account10InitializeEv`，import index 364，GOT 0x7772ae8，caller 0x352f538，lr 0x352f53c，exit 133，日志 `.local/logs/rg477m-boot-v26-socketinit.log`）。 |

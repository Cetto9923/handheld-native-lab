# Antigravity接力入口：越过 nn::nifm::Initialize，推进至 nn::socket::Initialize

本轮按工单 P-AG-9 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::nifm::Initialize` HLE 全链路落地闭环**：
   - 实现了 `nn::nifm::Initialize`（`_ZN2nn4nifm10InitializeEv` 及别名 `nnnifmInitialize`），按 Horizon / Nintendo SDK ABI 正确完成网络接口管理器状态记录并返回 0 (`ResultSuccess`)，保留清晰可归因日志 `[hle] nn::nifm::Initialize: initialized network interface manager -> returning 0`。
   - 在 `guest_process.c` 中将 `_ZN2nn4nifm10InitializeEv` 与 `nnnifmInitialize` 登记入 `typed` 白名单与 `import_signature`（0 参数，1 返回值），子线程 `GameThread` 无缝复用 Native Boundary 网关分派。

2. **宿主与真机契约全绿（30 项契约全部 PASS）**：
   - 增加 `--nifm` 独立契约测试用例 `nifm_contract`，全面覆盖符号解析、别名调用及基线返回 0 验证。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 30 项宿主契约（含 `host-nifm-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --nifm`：
     `{"nifm_contract_errors":0,"initialize":true,"baseline_zero":true}`。
   - 复跑 `--aoc`、`--fs-open-file`、`--fs-mount-rom`、`--threads` 与 `--guest-tp` 均验证通过：
     `{"aoc_contract_errors":0,"count":true,"list":true,"query_cache":true,"mount":true,"baseline_zero":true}`
     `{"fs_open_file_errors":0,"open":true,"read":true,"size":true,"close":true,"entry_type":true,"validation":true}`
     `{"fs_mount_rom_errors":0,"mount":true,"unmount":true,"validation":true}`
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测突破：越过 nn::nifm::Initialize，推进至 nn::socket::Initialize**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x79d12c6acc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x79d371c570 entry=0x79ce0211f0 arg=0x0 stack=0x740ad81000 size=0x100000 prio=16 tid=11730
     [hle] nn::os::StartThread: thread=0x79d371c570 tid=11730
     [hle] thread tid=11730 bound guest TP base=0xb400007b1dc4a010 span=4144
     [hle] child thread tid=11730 entering guest_entry=0x79ce0211f0 sp=0x740ae81000 arg=0x0
     [hle] nn::fs::SetAllocator: alloc=0x79cab052d0 free=0x79cab0e4c0
     [hle] nn::fs::QueryMountRomCacheSize: out_size=0x740ae80de8 -> 65536 (0x10000)
     [hle] nn::os::SetThreadName: thread=0x79d371c570 name='GameThread'
     [page-port-policy] op=map address=0x71aaa10000 bytes=65536 result=0 after real VM/accounting transition
     [hle] nn::fs::MountRom: mounted 'rom' buffer=0x71aaa10000 size=65536 (0x10000) host_path='romfs'
     [hle] nn::fs::OpenFile: path='rom:/UECommandLine.txt' mode=1 (host_path='romfs/UECommandLine.txt') size=38 -> handle=0x5e253336f8 (fd=3)
     [hle] nn::fs::GetFileSize: handle=0x5e253336f8 size=38
     [hle] nn::fs::ReadFile: handle=0x5e253336f8 offset=0 req_size=38 read_bytes=38
     [hle] nn::fs::CloseFile: closed handle=0x5e253336f8 (path='rom:/UECommandLine.txt')
     [crt] atexit registered callback=0x79caaf44f0 pending=13537; deferred
     [hle] nn::aoc::CountAddOnContent: returning 0 (no add-on content installed)
     [hle] nn::aoc::ListAddOnContent: out_indices=0x740ae808d8 offset=0 count=256 -> returning 0
     [crt] atexit registered callback=0x79cac720d0 pending=13538; deferred
     [hle] nn::nifm::Initialize: initialized network interface manager -> returning 0
     [fault] signal=5 pc=0x7c6ef23888 fa=0x7c6ef23888
      x0=0x79d27b3000  x1=0x600000  x2=0x20000  x3=0xe
      x4=0xffffffffffffffff  x5=0x740ae80726  x6=0x727265666564203b  x7=0xa64657272656665
      x8=0xae51d750b9343b9  x9=0x1  x10=0x2dd2  x11=0x5e2534c558
      x12=0x20  x13=0xb400007aedc49890  x14=0xffffffff  x15=0x0
      x16=0x79d2264858  x17=0x7c6ef23888  x18=0x0  x19=0x5e2532fc38
      x20=0x740ae80e60  x21=0x7c6dd1af38  x22=0x2dd1  x23=0x2dce
      x24=0x79d85a3bb0  x25=0x79d85a3bb0  x26=0x79d85a3ef8  x27=0x114000
      x28=0x116000 
      fp=0x740ae80fa0 lr=0x79ce0214f8 sp=0x740ae80e00
     ```
   - 子线程 `GameThread` 成功调用并完全越过 `nn::nifm::Initialize`（返回 0）！
   - 虚幻引擎网络系统在 `nifm` 初始化完成后，立即推进至 Socket 子系统初始化 `nn::socket::Initialize`！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn6socket10InitializeEPvmmi`（`nn::socket::Initialize(void *work_buffer, size_t work_buffer_size, size_t allocator_size, int concurrency)`）。
   - 符号索引：`symbol index = 273`（JMPREL 重定位索引 `reloc_idx = 267`）。
   - GOT / JMPREL 槽位：`0x7772858`（紧邻 `0x7772850` 下一槽位）。
   - 调用点现场：
     - `x0 = 0x79d27b3000`（work_buffer 工作内存指针）
     - `x1 = 0x600000`（6MB 工作内存尺寸）
     - `x2 = 0x20000`（128KB 分配器尺寸）
     - `x3 = 0xe`（14 并发度）
     - `x4 = 0xffffffffffffffff` (-1)
     - `lr = 0x79ce0214f8`（调用返回点偏移 `lr_offset = 0x352f4f8`）
   - 退出码：133（SIGTRAP sentinel `brk` at unlinked import 273）。
   - 完整原始日志：`.local/logs/rg477m-boot-v25-nifminit.log` + `.exitcode`。

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
| **nn::nifm::Initialize** | **已越过并真机通过**：成功初始化返回 0，GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `nn::socket::Initialize`**（`_ZN2nn6socket10InitializeEPvmmi`，import index 273，GOT 0x7772858，caller 0x352f4f8，exit 133，日志 `.local/logs/rg477m-boot-v25-nifminit.log`）。 |

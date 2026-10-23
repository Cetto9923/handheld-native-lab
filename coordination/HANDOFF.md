# Antigravity接力入口：越过MountRom，推进至文件打开OpenFile

本轮按工单 P-AG-6 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::fs::MountRom` HLE 落地闭环**：
   - 实现了 `nn::fs::MountRom`（及别名 `nnfsMountRom`），按签名 `nn::Result MountRom(char const* name, void* buffer, size_t size)` 校验非空名称与缓冲区指针及 64KB 最小缓存尺寸要求，动态解析合法本地 RomFS/素材路径，返回 0（`nn::ResultSuccess`）。
   - 一并补全 `nn::fs::Unmount`（及别名 `nnfsUnmount`）HLE，保障文件系统挂载生命周期完备。
   - 在 `guest_process.c` 中将 `_ZN2nn2fs8MountRomEPKcPvm`（3 个 GP 参数，1 个返回值）与 `_ZN2nn2fs7UnmountEPKc`（1 个 GP 参数，1 个返回值）加入 `typed` 白名单与 `import_signature`，走已有 Native Boundary 分派网关。

2. **宿主与真机契约测试全部通过**：
   - 增加 `--fs-mount-rom` 独立契约测试用例 `fs_mount_rom_contract`，验证空名称、空缓冲区、小尺寸拒绝以及有效挂载与卸载行为。
   - `scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 27 项宿主契约（含 `host-fs-mount-rom-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --fs-mount-rom`：
     `{"fs_mount_rom_errors":0,"mount":true,"unmount":true,"validation":true}`
   - 复跑 `--fs-query-cache`、`--fs-allocator`、`--threads` 与 `--guest-tp` 均验证通过：
     `{"fs_query_cache_errors":0,"query":true,"size_valid":true}`
     `{"fs_allocator_errors":0,"set":true,"callbacks_saved":true}`
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测突破：越过 MountRom，推进至 OpenFile**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x7217548acc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x721999e570 entry=0x72142a31f0 arg=0x0 stack=0x6c50f81000 size=0x100000 prio=16 tid=30513
     [hle] nn::os::StartThread: thread=0x721999e570 tid=30513
     [hle] thread tid=30513 bound guest TP base=0xb4000072e4857640 span=4144
     [hle] child thread tid=30513 entering guest_entry=0x72142a31f0 sp=0x6c51081000 arg=0x0
     [hle] nn::os::SetThreadName: thread=0x721999e570 name='GameThread'
     [hle] nn::fs::SetAllocator: alloc=0x7210d872d0 free=0x7210d904c0
     [hle] nn::fs::QueryMountRomCacheSize: out_size=0x6c51080de8 -> 65536 (0x10000)
     [page-port-policy] op=map address=0x69f0c10000 bytes=65536 result=0 after real VM/accounting transition
     [hle] nn::fs::MountRom: mounted 'rom' buffer=0x69f0c10000 size=65536 (0x10000) host_path='.'
     [fault] signal=5 pc=0x74b7e54040 fa=0x74b7e54040
      x0=0x6c51080d48  x1=0x6c51080c20  x2=0x1  x3=0x0
      x4=0x7216c77588  x5=0x62d0c9a9ec  x6=0x0  x7=0x3a006d006f0072
      ...
      fp=0x6c51080d50 lr=0x7210d79868 sp=0x6c51080c10
     ```
   - 子线程 `GameThread` 成功执行并越过 `nn::fs::MountRom`（实参 `name="rom"`，`buffer=0x69f0c10000`，`size=65536`，返回 0），RomFS 挂载完成。
   - 随后 `GameThread` 顺利推进到文件打开逻辑，调用 `nn::fs::OpenFile(&handle, path, OpenMode_Read)`！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci`（`nn::fs::OpenFile`，symbol index 10，PLT `0x5e67b40`，调用点 `caller_offset=0x5864` / `lr=0x7210d79868`，基址 `base=0x7210d74000`，调用偏移 `offset=0x5868`；实参 `x0=&handle (0x6c51080d48)`，`x1=path (0x6c51080c20)`，`x2=1 (OpenMode_Read)`，`x7="rom:"`）。
   - 退出码：133（SIGTRAP sentinel brk at unlinked import 10）。
   - 完整原始日志：`.local/logs/rg477m-boot-v22-mountrom.log` + `.exitcode`。

5. **明确未完成项标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，未真正实施页面写保护；
   - 尚无标题画面 / 可操作场景证据；
   - NVN→Vulkan 完整桥接、RomFS/IoStore 真实文件读取与句柄管理未闭环。

## 先读这些

- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本次 `--boot` 完整原始运行日志已留盘：`.local/logs/rg477m-boot-v22-mountrom.log` (exitcode 133)。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **全量构造函数序列** | **100% 达成**：`init_array` 全部 1,890 项构造函数真机连跑通过，无一跳漏，登记 13,536 项 atexit 回调。 |
| **游戏主入口 nnMain** | **成功进入**：offset `0x352f160` 成功执行并验证 `nn::util::ReferSymbol`。 |
| **nn::os::CreateThread / StartThread** | **已越过并真机通过**：1MB 独立栈、独立 `guest_tp`、跨线程 Native Boundary 插槽分派正常。 |
| **GameThread 子线程** | **成功执行**：进入 `GameThread`，调用 `GetHostArgc` 正常返回，进入文件系统初始化。 |
| **nn::fs::SetAllocator** | **已越过并真机通过**：成功保存回调指针，越过文件系统初始化首阻。 |
| **nn::fs::QueryMountRomCacheSize** | **已越过并真机通过**：成功写出 64KB 缓存尺寸，UE 成功调用分配器完成 64KB 页面映射。 |
| **nn::fs::MountRom** | **已越过并真机通过**：参数校验通过，合法挂载本地素材路径，成功返回 0。 |
| **最新真实阻塞** | **明确归因**：`_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci`（import 10, caller 0x5864 / lr 0x5868, handle=0x6c51080d48, mode=1, path="rom:..."), 游戏进入 RomFS 文件读取。 |
| **ProtectRelro 状态** | **明确标注未完成**：当前为打印桩，未实施 mprotect 保护。 |

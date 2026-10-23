# Antigravity接力入口：越过QueryMountRomCacheSize，推进至RomFS挂载MountRom

本轮按工单 P-AG-5 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::fs::QueryMountRomCacheSize` HLE 落地闭环**：
   - 实现了 `nn::fs::QueryMountRomCacheSize`（及别名 `nnfsQueryMountRomCacheSize`），按签名 `nn::Result QueryMountRomCacheSize(size_t *out_size)` 准确校验非空指针，写出标准 RomFS 挂载缓存尺寸 64 KB（`0x10000`），返回 0（`nn::ResultSuccess`）。
   - 在 `guest_process.c` 中将 `_ZN2nn2fs22QueryMountRomCacheSizeEPm` 加入 `typed` 白名单与 `import_signature`（1 个 GP 参数，1 个返回值），走已有 Native Boundary 分派网关。

2. **宿主与真机契约测试全部通过**：
   - 增加 `--fs-query-cache` 独立契约测试用例 `fs_query_cache_contract`，验证空指针拒绝与缓存尺寸正常写入。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 26 项宿主契约（含 `host-fs-query-cache-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --fs-query-cache`：
     `{"fs_query_cache_errors":0,"query":true,"size_valid":true}`
   - 复跑 `--fs-allocator`、`--threads` 与 `--guest-tp` 均验证通过：
     `{"fs_allocator_errors":0,"set":true,"callbacks_saved":true}`
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测突破：越过 QueryMountRomCacheSize，动态分配 64KB 缓存，推进至 MountRom**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x7475354acc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x74777aa570 entry=0x74720af1f0 arg=0x0 stack=0x6eaed81000 size=0x100000 prio=16 tid=10211
     [hle] nn::os::StartThread: thread=0x74777aa570 tid=10211
     [hle] nn::os::SetThreadName: thread=0x74777aa570 name='GameThread'
     [hle] thread tid=10211 bound guest TP base=0xb400007654637010 span=4144
     [hle] child thread tid=10211 entering guest_entry=0x74720af1f0 sp=0x6eaee81000 arg=0x0
     [hle] nn::fs::SetAllocator: alloc=0x746eb932d0 free=0x746eb9c4c0
     [hle] nn::fs::QueryMountRomCacheSize: out_size=0x6eaee80de8 -> 65536 (0x10000)
     [page-port-policy] op=map address=0x6c4ea10000 bytes=65536 result=0 after real VM/accounting transition
     [fault] signal=5 pc=0x77171210b0 fa=0x77171210b0
      x0=0x7474f50adb  x1=0x6c4ea10000  x2=0x10000  x3=0x1
      x4=0xffffffffffffffff  x5=0x6eaee80438  x6=0x33353536  x7=0x3030303031616534
      ...
      fp=0x6eaee80dd0 lr=0x746eb85594 sp=0x6eaee80da0
     ```
   - 子线程 `GameThread` 成功执行并越过 `nn::fs::QueryMountRomCacheSize`（入参 `x0=0x6eaee80de8`，写出 `65536`），随后调用 UE 内存分配器成功分配 64KB 缓存缓冲区（`0x6c4ea10000`），并顺利进入 `nn::fs::MountRom("rom", 0x6c4ea10000, 0x10000)`！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn2fs8MountRomEPKcPvm`（`nn::fs::MountRom`，symbol index 22，PLT `0x5e62700`，调用点 `caller_offset=0x5590` / `lr=0x746eb85594`，基址 `base=0x746eb80000`，实参 `name="rom"`，`buffer=0x6c4ea10000`，`size=0x10000`）。
   - 游戏成功完成 RomFS 缓存查询并动态分配后，正规进入 RomFS 挂载调用。
   - 退出码：133（SIGTRAP sentinel brk at unlinked import 22）。
   - 完整原始日志：`.local/logs/rg477m-boot-v21-querymountrom.log` + `.exitcode`。

5. **明确未完成项标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，未真正实施页面写保护；
   - 尚无标题画面 / 可操作场景证据；
   - NVN→Vulkan 完整桥接、RomFS/IoStore 运行时挂载未闭环。

## 先读这些

- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本次 `--boot` 完整原始运行日志已留盘：`.local/logs/rg477m-boot-v21-querymountrom.log` (exitcode 133)。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **全量构造函数序列** | **100% 达成**：`init_array` 全部 1,890 项构造函数真机连跑通过，无一跳漏，登记 13,536 项 atexit 回调。 |
| **游戏主入口 nnMain** | **成功进入**：offset `0x352f160` 成功执行并验证 `nn::util::ReferSymbol`。 |
| **nn::os::CreateThread / StartThread** | **已越过并真机通过**：1MB 独立栈、独立 `guest_tp`、跨线程 Native Boundary 插槽分派正常。 |
| **GameThread 子线程** | **成功执行**：进入 `GameThread`，调用 `GetHostArgc` 正常返回，进入文件系统初始化。 |
| **nn::fs::SetAllocator** | **已越过并真机通过**：成功保存回调指针，越过文件系统初始化首阻。 |
| **nn::fs::QueryMountRomCacheSize** | **已越过并真机通过**：成功写出 64KB 缓存尺寸，UE 成功调用分配器完成 64KB 页面映射。 |
| **最新真实阻塞** | **明确归因**：`_ZN2nn2fs8MountRomEPKcPvm`（import 22, caller 0x5590 / lr 0x5594, name="rom", buf=0x6c4ea10000, size=0x10000），游戏进入 RomFS 正式挂载。 |
| **ProtectRelro 状态** | **明确标注未完成**：当前为打印桩，未实施 mprotect 保护。 |

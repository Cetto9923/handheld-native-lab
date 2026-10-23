# Antigravity接力入口：越过SetAllocator，推进至RomFS挂载QueryMountRomCacheSize

本轮按工单 P-AG-4 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::fs::SetAllocator` HLE 落地闭环**：
   - 实现了 `nn::fs::SetAllocator`（及别名 `nnfsSetAllocator`），按签名 `SetAllocator(void* (*alloc)(size_t), void (*free)(void*, size_t))` 准确接收并保存回调指针到宿主运行态，并导出获取接口。
   - 在 `guest_process.c` 中将 `_ZN2nn2fs12SetAllocatorEPFPvmEPFvS1_mE` 加入 `typed` 白名单与 `import_signature`（2 个 GP 参数，0 个返回值，即 void），走已有 Native Boundary 分派网关。

2. **宿主与真机契约测试全部通过**：
   - 增加 `--fs-allocator` 独立契约测试用例 `fs_allocator_contract`，验证符号解析与回调指针存储。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 25 项宿主契约（含 `host-fs-allocator-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --fs-allocator`：
     `{"fs_allocator_errors":0,"set":true,"callbacks_saved":true}`
   - 复跑 `--threads` 与 `--guest-tp` 均验证通过：
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测突破：越过 SetAllocator，推进至 RomFS 初始化**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x7c5d0ffacc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x7c5f555570 entry=0x7c59e5a1f0 arg=0x0 stack=0x7696b81000 size=0x100000 prio=16 tid=24244
     [hle] nn::os::StartThread: thread=0x7c5f555570 tid=24244
     [hle] thread tid=24244 bound guest TP base=0xb400007ed9df9010 span=4144
     [hle] child thread tid=24244 entering guest_entry=0x7c59e5a1f0 sp=0x7696c81000 arg=0x0
     [hle] nn::fs::SetAllocator: alloc=0x7c5693e2d0 free=0x7c569474c0
     [fault] signal=5 pc=0x7efb1d40a8 fa=0x7efb1d40a8
      x0=0x7696c80de8  x1=0x0  x2=0x0  x3=0x0
     ...
      fp=0x7696c80dd0 lr=0x7c5693056c sp=0x7696c80da0
     [hle] nn::os::SetThreadName: thread=0x7c5f555570 name='GameThread'
     ```
   - 子线程 `GameThread` 成功执行并越过 `nn::fs::SetAllocator`（调用点 `0x553c`，实参 `alloc=0x7c5693e2d0 free=0x7c569474c0`），并顺利执行后续指令！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn2fs22QueryMountRomCacheSizeEPm`（`nn::fs::QueryMountRomCacheSize`，symbol index 21，PLT `0x5e67c10`，调用点 `caller_offset=0x5568` / `lr=0x7c5693056c`，基址 `base=0x7c5692b000`）。
   - 游戏在设置文件系统分配器后，正规进入 RomFS 挂载序列（第一步查询 RomFS 挂载缓存尺寸）。
   - 退出码：133（SIGTRAP sentinel brk at unlinked import 21）。
   - 完整原始日志：`.local/logs/rg477m-boot-v20-setallocator.log` + `.exitcode`。

5. **明确未完成项标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，未真正实施页面写保护；
   - 尚无标题画面 / 可操作场景证据；
   - NVN→Vulkan 完整桥接、RomFS/IoStore 运行时挂载未闭环。

## 先读这些

- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本次 `--boot` 完整原始运行日志已留盘：`.local/logs/rg477m-boot-v20-setallocator.log` (exitcode 133)。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **全量构造函数序列** | **100% 达成**：`init_array` 全部 1,890 项构造函数真机连跑通过，无一跳漏，登记 13,536 项 atexit 回调。 |
| **游戏主入口 nnMain** | **成功进入**：offset `0x352f160` 成功执行并验证 `nn::util::ReferSymbol`。 |
| **nn::os::CreateThread / StartThread** | **已越过并真机通过**：1MB 独立栈、独立 `guest_tp`、跨线程 Native Boundary 插槽分派正常。 |
| **GameThread 子线程** | **成功执行**：进入 `GameThread`，调用 `GetHostArgc` 正常返回，进入文件系统初始化。 |
| **nn::fs::SetAllocator** | **已越过并真机通过**：成功保存回调指针，越过文件系统初始化首阻。 |
| **最新真实阻塞** | **明确归因**：`_ZN2nn2fs22QueryMountRomCacheSizeEPm`（import 21, caller 0x5568 / lr 0x556c），游戏进入 RomFS 挂载准备。 |
| **ProtectRelro 状态** | **明确标注未完成**：当前为打印桩，未实施 mprotect 保护。 |

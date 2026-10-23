# Antigravity接力入口：越过 nn::aoc::CountAddOnContent，推进至 nn::nifm::Initialize

本轮按工单 P-AG-8 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::aoc` / DLC 集群 HLE 全链路落地闭环**：
   - 实现了 `nn::aoc::CountAddOnContent`（`_ZN2nn3aoc17CountAddOnContentEv` 及别名 `nnaocCountAddOnContent`），基线无 DLC 情况下安全返回 0 并留存 `[hle]` 归因日志。
   - 实现了 `nn::aoc::ListAddOnContent`（`_ZN2nn3aoc16ListAddOnContentEPiii` 及别名 `nnaocListAddOnContent`），校验出参指针及数量，无 DLC 时返回 0。
   - 实现了 `nn::fs::QueryMountAddOnContentCacheSize`（`_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi` 及别名 `nnfsQueryMountAddOnContentCacheSize`），输出 0 缓存大小并返回 0。
   - 实现了 `nn::fs::MountAddOnContent`（`_ZN2nn2fs17MountAddOnContentEPKciPvm` 及别名 `nnfsMountAddOnContent`），对未安装 DLC 严格返回 `0x202`（`ResultPathNotFound`）。
   - 在 `guest_process.c` 中将上述 AOC 符号加入 `typed` 白名单与 `import_signature`，子线程 `GameThread` 无缝复用 Native Boundary 分派网关。

2. **宿主与真机契约全绿**：
   - 增加 `--aoc` 独立契约测试用例 `aoc_contract`，全面覆盖符号解析、别名调用、基线 0 计数、空列表列出、缓存查询与挂载错误拒绝。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 29 项宿主契约（含 `host-aoc-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --aoc`：
     `{"aoc_contract_errors":0,"count":true,"list":true,"query_cache":true,"mount":true,"baseline_zero":true}`
   - 复跑 `--fs-open-file`、`--fs-mount-rom`、`--threads` 与 `--guest-tp` 均验证通过：
     `{"fs_open_file_errors":0,"open":true,"read":true,"size":true,"close":true,"entry_type":true,"validation":true}`
     `{"fs_mount_rom_errors":0,"mount":true,"unmount":true,"validation":true}`
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测突破：越过 CountAddOnContent 及 ListAddOnContent，推进至 nn::nifm::Initialize**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x6d9626bacc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x6d986c1570 entry=0x6d92fc61f0 arg=0x0 stack=0x5e33d81000 size=0x100000 prio=16 tid=29109
     [hle] nn::os::StartThread: thread=0x6d986c1570 tid=29109
     [hle] thread tid=29109 bound guest TP base=0xb400007022049220 span=4144
     [hle] child thread tid=29109 entering guest_entry=0x6d92fc61f0 sp=0x5e33e81000 arg=0x0
     [hle] nn::fs::SetAllocator: alloc=0x6d8faaa2d0 free=0x6d8fab34c0
     [hle] nn::fs::QueryMountRomCacheSize: out_size=0x5e33e80de8 -> 65536 (0x10000)
     [hle] nn::os::SetThreadName: thread=0x6d986c1570 name='GameThread'
     [page-port-policy] op=map address=0x5bd3a10000 bytes=65536 result=0 after real VM/accounting transition
     [hle] nn::fs::MountRom: mounted 'rom' buffer=0x5bd3a10000 size=65536 (0x10000) host_path='romfs'
     [hle] nn::fs::OpenFile: path='rom:/UECommandLine.txt' mode=1 (host_path='romfs/UECommandLine.txt') size=38 -> handle=0x63f3a87530 (fd=3)
     [hle] nn::fs::GetFileSize: handle=0x63f3a87530 size=38
     [hle] nn::fs::ReadFile: handle=0x63f3a87530 offset=0 req_size=38 read_bytes=38
     [hle] nn::fs::CloseFile: closed handle=0x63f3a87530 (path='rom:/UECommandLine.txt')
     [crt] atexit registered callback=0x6d8fa994f0 pending=13537; deferred
     [hle] nn::aoc::CountAddOnContent: returning 0 (no add-on content installed)
     [hle] nn::aoc::ListAddOnContent: out_indices=0x5e33e808d8 offset=0 count=256 -> returning 0
     [crt] atexit registered callback=0x6d8fc170d0 pending=13538; deferred
     [fault] signal=5 pc=0x7033d31880 fa=0x7033d31880
      x0=0x0  x1=0x0  x2=0x5e33e80a28  x3=0x0
      x4=0x50  x5=0x5e33e80726  x6=0x727265666564203b  x7=0xa64657272656665
      x8=0x6d97757000  x9=0x1  x10=0xdc00  x11=0xfca02400
      x12=0xffffffff  x13=0xfffe  x14=0xffffffff  x15=0x0
      x16=0x6d97209850  x17=0x7033d31880  x18=0x0  x19=0x63f3a83a78
      x20=0x5e33e80e60  x21=0x703211bf38  x22=0x71b2  x23=0x71ac
      x24=0x6d9fb95bb0  x25=0x6d9fb95bb0  x26=0x6d9fb95ef8  x27=0x114000
      x28=0x116000 
      fp=0x5e33e80df0 lr=0x6d903d9d24 sp=0x5e33e80df0
     ```
   - 子线程 `GameThread` 成功执行并越过 `nn::aoc::CountAddOnContent`（返回 0）以及 `nn::aoc::ListAddOnContent`（count=256，返回 0）！
   - GameThread 随后注册新 deferred atexit 回调（`0x6d8fc170d0 pending=13538`），并推进至下一步网络模块初始化！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn4nifm10InitializeEv`（`nn::nifm::Initialize()`，symbol index 272，reloc index 266，JMPREL 槽位 `0x7772850`，PLT 跳转目标，调用点 `caller_offset=0x942d24` / `lr=0x6d903d9d24`，基址 `base=0x6d8fa97000`；实参 `x0=0`）。
   - 紧随相关的网络接口接口集群包括：
     - `_ZN2nn4nifm10InitializeEv`（symbol 272）
     - `_ZN2nn4nifm18IsNetworkAvailableEv`（symbol 89）
     - `_ZN2nn4nifm27SubmitNetworkRequestAndWaitEv`（symbol 90）
     - `_ZN2nn4nifm31HandleNetworkRequestErrorResultEv`（symbol 91）
     - `_ZN2nn4nifm26GetCurrentPrimaryIpAddressEPNS_6socket6InAddrE`（symbol 276）
   - 退出码：133（SIGTRAP sentinel brk at unlinked import 272）。
   - 完整原始日志：`.local/logs/rg477m-boot-v24-countaddon.log` + `.exitcode`。

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
| **nn::aoc::CountAddOnContent / ListAddOnContent** | **已越过并真机通过**：实测 Count 返回 0，List 返回 0 项，GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `nn::nifm::Initialize`**（import index 272，caller 0x942d24，exit 133，日志 `.local/logs/rg477m-boot-v24-countaddon.log`）。 |

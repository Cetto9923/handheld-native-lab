# Antigravity接力入口：越过 OpenFile / RomFS 读取，推进至 nn::aoc::CountAddOnContent

本轮按工单 P-AG-7 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **最小可用 `nn::fs::OpenFile` 及 RomFS 文件读路径 HLE 全链路落地闭环**：
   - 实现了 `nn::fs::OpenFile`（及别名 `nnfsOpenFile`），按签名 `nn::Result OpenFile(FileHandle* out_handle, const char* path, int mode)` 严格校验参数与路径有效性。
   - 实现 RomFS 路径解析器 `resolve_fs_path`，支持 `rom:/...` 挂载路径及纯相对路径转换，自动对齐真实本地 RomFS 资源根目录。
   - 实现了 `GetFileSize`（按值传递 `FileHandle`，读取并返回真实文件尺寸）、`ReadFile`（5 参及 4 参变体，基于 `pread` 安全实现无状态偏移读取）、`CloseFile`（安全释放宿主文件描述符并清理 handle 槽）、`GetEntryType`（正确识别文件与目录）以及 `FlushFile`、`SetFileSize`、`WriteFile`、`DisableGlobalFileDataCache`、`EnableGlobalFileDataCache`。
   - 在 `guest_process.c` 中将所有文件系统读路径符号加入 `typed` 白名单与 `import_signature`，无缝复用 Native Boundary 子线程分派网关。

2. **本地 RomFS 素材无缝对接与宿主/真机契约全绿**：
   - 将流式 RomFS 中启动必需轻量配置文件与目录（31MB，含 `UECommandLine.txt` 38 字节、`StagedBuild_FFRS.ini`、清单及 IoStore utoc）提取并部署至真机 `/data/local/tmp/.../romfs`。
   - 增加 `--fs-open-file` 独立契约测试用例 `fs_open_file_contract`，全面覆盖参数校验、文件生命周期（open->size->read->close）、关闭后句柄拒绝、目录类型判断及真实 RomFS 文件读取。
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 28 项宿主契约（含 `host-fs-open-file-contract`，returncode 0）。
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --fs-open-file`：
     `{"fs_open_file_errors":0,"open":true,"read":true,"size":true,"close":true,"entry_type":true,"validation":true}`
   - 复跑 `--fs-mount-rom`、`--fs-query-cache`、`--fs-allocator`、`--threads` 与 `--guest-tp` 均验证通过：
     `{"fs_mount_rom_errors":0,"mount":true,"unmount":true,"validation":true}`
     `{"fs_query_cache_errors":0,"query":true,"size_valid":true}`
     `{"fs_allocator_errors":0,"set":true,"callbacks_saved":true}`
     `{"thread_errors":0,"create":true,"start":true,"wait":true,"destroy":true,"name":true,"priority":true,"tid":true,"tp_mirror":true}`
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。

3. **真机实测突破：越过 OpenFile / ReadFile，成功读取 UE 命令行，推进至 CountAddOnContent**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证：
     ```text
     [boot-step4] calling nnMain at offset 0x352f160...
     [hle] nn::util::ReferSymbol(0x7a53cffacc): verified SDK link anchor
     [hle] nn::os::CreateThread: thread=0x7a56155570 entry=0x7a50a5a1f0 arg=0x0 stack=0x748d781000 size=0x100000 prio=16 tid=17527
     [hle] nn::os::StartThread: thread=0x7a56155570 tid=17527
     [hle] thread tid=17527 bound guest TP base=0xb400007cdfbc5430 span=4144
     [hle] child thread tid=17527 entering guest_entry=0x7a50a5a1f0 sp=0x748d881000 arg=0x0
     [hle] nn::os::SetThreadName: thread=0x7a56155570 name='GameThread'
     [hle] nn::fs::SetAllocator: alloc=0x7a4d53e2d0 free=0x7a4d5474c0
     [hle] nn::fs::QueryMountRomCacheSize: out_size=0x748d880de8 -> 65536 (0x10000)
     [page-port-policy] op=map address=0x722d410000 bytes=65536 result=0 after real VM/accounting transition
     [hle] nn::fs::MountRom: mounted 'rom' buffer=0x722d410000 size=65536 (0x10000) host_path='romfs'
     [hle] nn::fs::OpenFile: path='rom:/UECommandLine.txt' mode=1 (host_path='romfs/UECommandLine.txt') size=38 -> handle=0x613502adb0 (fd=3)
     [hle] nn::fs::GetFileSize: handle=0x613502adb0 size=38
     [hle] nn::fs::ReadFile: handle=0x613502adb0 offset=0 req_size=38 read_bytes=38
     [hle] nn::fs::CloseFile: closed handle=0x613502adb0 (path='rom:/UECommandLine.txt')
     [crt] atexit registered callback=0x7a4d52d4f0 pending=13537; deferred
     [fault] signal=5 pc=0x7cf3782198 fa=0x7cf3782198
      x0=0x0  x1=0x0  x2=0x748d867bc4  x3=0x0
      x4=0x6acd41c91a  x5=0x6acd41c8fa  x6=0x0  x7=0x3a006d006f0072
      ...
      x16=0x7a54c9d190  x17=0x7cf3782198  x18=0x0  x19=0x7a5512fc30
      fp=0x748d880cf0 lr=0x7a4d5343dc sp=0x748d867fe0
     ```
   - 子线程 `GameThread` 成功执行并越过 `OpenFile`、`GetFileSize`、`ReadFile`、`CloseFile`，完整读取了 `rom:/UECommandLine.txt`（内容 `-project="../../../FFRS/FFRS.uproject"`，38 字节）！
   - GameThread 随后注册 deferred atexit 回调，并推进至下一步游戏初始化逻辑！

4. **最新真实阻塞前沿确认**：
   - 确证新阻塞符号：`_ZN2nn3aoc17CountAddOnContentEv`（`nn::aoc::CountAddOnContent()`，symbol index 51，JMPREL 槽位 `0x7772190`，PLT 跳转目标，调用点 `caller_offset=0x93dc` / `lr=0x7a4d5343dc`，基址 `base=0x7a4d52b000`；实参 `x0=0`）。
   - 紧随相关的 AOC / DLC 接口集群包括：
     - `_ZN2nn3aoc17CountAddOnContentEv`（symbol 51）
     - `_ZN2nn3aoc16ListAddOnContentEPiii`（symbol 52）
     - `_ZN2nn2fs31QueryMountAddOnContentCacheSizeEPmi`（symbol 53）
     - `_ZN2nn2fs17MountAddOnContentEPKciPvm`（symbol 54）
   - 退出码：133（SIGTRAP sentinel brk at unlinked import 51）。
   - 完整原始日志：`.local/logs/rg477m-boot-v23-openfile.log` + `.exitcode`。

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
| **nn::fs::OpenFile / GetFileSize / ReadFile / CloseFile** | **已越过并真机通过**：实测完整打开、查询、读取 `rom:/UECommandLine.txt`（38B）并关闭，GameThread 顺利越过！ |
| **当前阻塞前沿** | **确证为 `nn::aoc::CountAddOnContent`**（import index 51，caller 0x93dc，exit 133，日志 `.local/logs/rg477m-boot-v23-openfile.log`）。 |

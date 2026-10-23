# Antigravity接力入口：RWLock/Time/Tick/Wctype闭环与487项构造函数真机连跑实证

本轮按用户指令推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。
最新进展：
1. **ReaderWriterLock (RWLock) HLE 全族接线完成并在真机验证通过**：
   - 完整实现 `InitializeReaderWriterLock`, `FinalizeReaderWriterLock`, `AcquireReadLock`, `TryAcquireReadLock`, `ReleaseReadLock`, `AcquireWriteLock`, `TryAcquireWriteLock`, `ReleaseWriteLock`, `IsWriteLockHeldByCurrentThread`, `IsReadLockHeld`, `IsReaderWriterLockOwnerThread`。
   - 镜像同步 `ReaderWriterLockType` 内部字段（`+0x04` readers, `+0x0c` write_depth, `+0x10` init_flag, `+0x18` write_owner_token）。
   - 补充专用回归测试 `--rwlock`，在 RG477M 真机上通过测试：
     `{"rwlock_errors":0,"initialized":true,"write_exclusive":true,"read_shared":true,"owner_tracked":true,"finalized":true}`。
2. **`nn::time`、SystemTick 与 C99/wctype 系列导入项补齐并验证**：
   - 补齐 `ToCalendarTime`、`ToCalendarTimeInUtc`、`GetDeviceLocationName`、`LoadTimeZoneRule`、`GetDayOfWeek`，真机 `--time` PASS：
     `{"time_errors":0,"initialized":true,"current_time_valid":true,"location_name":true,"day_of_week":true,"to_calendar_time":true,"to_calendar_time_utc":true}`。
   - 补齐 `GetSystemTickFrequency`（Switch 硬件标准 19.2MHz / `19200000ULL`）与 `GetSystemTick`（基于 `CLOCK_MONOTONIC` 精确缩放），真机 `--tick` PASS：
     `{"tick_errors":0,"frequency_19_2mhz":true,"ticks_monotonic":true}`。
   - 补齐 `iswxdigit`, `iswdigit`, `iswspace`, `iswalnum`, `iswalpha`, `iswlower`, `iswupper`, `isspace`, `isalpha`, `tolower`，真机 `--ctype` PASS：
     `{"ctype_errors":0,"iswxdigit":true,"iswdigit":true,"isspace":true,"tolower":true}`。
3. **逐模块、逐构造函数执行记账重大突破：从 idx=8 推进至 idx=487**：
   - 真机 `--boot` 实测证明：**构造函数 0 至 486（共 487 项构造函数）按真实顺序完整执行完毕，无跳漏、无重复执行！**
   - 累积登记 2,366 个 `atexit` 回调；完成多轮大块虚拟内存映射与分配器注册；
   - 构造函数 487 (`idx=487/1890 off=0x1478c60`) 启动并执行至 caller `0x1484494`。
4. **最新真实阻塞点确定**：
   - 未绑定导入项 `index=150 name=_ZTHN13LowLevelTasks5FTask10ActiveTaskE`（caller_offset=0x1484494），退出码 133。
   - 反汇编实证：`0x148448c cbz x8, 0x1484494` 检测 weak 导入 `_ZTH` 是否为空，紧接着在 `0x14844a4` 执行 `mrs x8, tpidr_el0` 并读取 `[x8, #0x238]`（UE5 LowLevelTasks 线程局部变量）。
   - 此阻塞点恰好精准接合工单 `P-CC-1`（原生 TP 隔离 / 逐线程 thread_local 读取适配）。
5. **未完成项明确标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，**未真正实施页面写保护，仍列未完成**；
   - 逐线程 TP 隔离与 guest `TPIDR_EL0` 适配仍列未完成，为下一阶段主入口。

## 先读这些

- [可直接复制的完整接力提词](ANTIGRAVITY_PROMPT.md)；[当前证据索引](../docs/analysis/codex_handoff_evidence_index.md)；[所有工作树绝对路径/分支/SHA/WIP快照](WORKTREES.json)。先读本仓库 AGENTS.md，实核 Git 与 writer 后再接手。
- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本地检查点基线 `1bcf655`。当前 Antigravity 为唯一根总控与独占写入者。
- 严禁打印、记录或提交任何游戏密钥与加密素材，所有大型原始二进制留在 `.local/`。
- 本次 `--boot` 完整原始运行日志已留盘：`.local/logs/rg477m-boot-v16-ctor487.log` (237KB, exitcode 133)。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **RWLock HLE 全族** | **已实现并真机通过**：`Initialize/Finalize/Acquire/Release/Try/Is*` 完整闭环，多线程排他/共享锁验证通过。真机 contracts `--rwlock` 输出 0 错误全部 PASS。构造函数 8 中的真实 RWLock 调用顺利通过。 |
| **逐构造函数记账** | **重大进展**：已实测实证 idx 0~486 共 487 项构造函数连续顺序执行完成，累积登记 2366 项 atexit 回调。 |
| **时间与计时辅助** | **已实现并真机通过**：`nn::time` 全族（ToCalendarTime/InUtc 等）、19.2MHz `GetSystemTickFrequency` / `GetSystemTick`、`wctype/ctype` 辅助全部真机 PASS。 |
| **最新真实阻塞** | **明确归因**：构造函数 487 调用的 `_ZTHN13LowLevelTasks5FTask10ActiveTaskE`（import 150, caller 0x1484494），紧跟 `MRS TPIDR_EL0` + offset `0x238`。 |
| **ProtectRelro 状态** | **明确标注未完成**：当前为打印桩，未实施 mprotect 保护，不计入闭环语义。 |
| **逐线程 TP 状态** | **明确标注未完成**：主线程分配了静态镜像，但未实施线程上下文与 TP 切换隔离（工单 P-CC-1 对口领域）。 |
| 106/110 与原 startup | 已编译并在 RG 验证：`nninitStartup` 空/带参均正常返回，真实 OS slot 0 析构完成。 |
| 原游戏分配器 | `--startup-allocator`：startup 后 48/64B alloc/free 双对测试在 RG477M 通过。全局分配器单例 `0x77c5210` 现已由 `nninitStartup` 构造就绪。 |
| 图形/设备 | LAS MIT 命令记录/typed Vulkan transfer 实际 Mali copy/clear 零差异；待推进 NVN 接口层与着色器转化。 |

## 下一步关键路径

1. **构造器序列与五参 SDK Start 联动 (`_ZN2nn4init5StartEmmPFvvES2_S2_`)**：
   - 现 `nninitStartup` 已完成执行，全局分配器单例 `0x77c5210` 构造闭合。
   - 接下来需要按官方 SDK 启动序列执行 `x3()`（`.init_array` 1,890 项构造函数，包括 UE5 `FMallocBinned3` 初始化等）。
   - 构造器执行完毕后，执行 `nnMain` 入口。
2. **逐线程 TLS / TP 适配**：
   - 依据 `codex_tls_caller_report.md`，main 中存在 1489 处 `MRS TPIDR_EL0` 读取。
   - 确保当前主线程和后续子线程在访问 `TP + 0x10..0x1028` 范围时，得到正确的线程局部数据指针，不写宿主 Bionic TP，保持宿主线程安全性。
3. **图形与 NVN 接入**：
   - 将已验证的 Vulkan buffer / transfer 与 NVN 兼容层对接，推进至游戏首帧与标题渲染。

## 环境、复用与否定经验

- ADB `/Users/yuyan9923/Library/Android/sdk/platform-tools/adb`，RG序列 `RG477M01445122`；每次显式 `-s` 并先核设备身份，不清数据。原始CLI/命令见索引，不自动重放已完成stage。RG Android14/ARM64/nativebridge0/4KiB/Mali G615；AVD与荣耀平板结果分别记录。平板未连，型号/SoC按后续实测，等比例/必要留边，不拉伸。
- 官方partial NDK在 `.local/minimal-ndk/toolchain`，AppleClang+RustLLD链实际可产Android ELF；精确argv复用manifest，不假定完整NDK/CMake安装完成。
- REA技能 `/Users/yuyan9923/.codex/skills/reverse-engineer-anything/SKILL.md` 已实际读取；LLVM静态切片单独归因；不重复同一故障或全局修复。
- 已采用LASRecomp Beta4 MIT、revision `fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93` 的命令记录/typed transfer；未采用CPU memmove假GPU、GetInfo16/17常量0、未知NVN布局；GLSL shaderc不等于Maxwell translator。
- 保留否定路线：mmap无MAP_FIXED不能推出必须映射0；tail0曾掩盖139；最长RELA候选不证明完整性；pre-init malloc139是未构造对象访问，不能手填vptr，poststartup成功不能抹去失败；leaf返回0或musl早return2不是游戏；94MB必OOM猜测已撤回。
- 用户授权本地开发/提交/并行、已许可原分支安全ff及设备诊断；不push/发布/购买/root/刷机/清数据、不下载新游戏或密钥、不停止其他工具。

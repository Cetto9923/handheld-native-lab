# Antigravity接力入口：全量1890项构造函数真机跑通，突破至游戏主入口nnMain实证

本轮按工单 P-AG-2 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。
最新重大突破：
1. **合并 Claude Code 侧 guest TP 骨架并真机验证**：
   - 合并 `src/runtime/hle/guest_tp.c/.h`、`tests/native/guest_tp_contract.c`、`tests/native/guest_tp_device.c`、`scripts/runtime/inspect_guest_tp_reads.py`、`scripts/runtime/build_guest_tp_contract.sh`。
   - 修复 Android Bionic 对 ELF `PT_TLS` 段强制要求 `p_align >= 64` 的限制（为 `guest_tp_thread_mirror` 增加 `_Alignas(64)` 内存对齐），避免 Bionic linker abort (exit 134)。
   - 在 RG477M 上跑通独立契约测试 `guest_tp_device.android` 与集成契约测试 `guest_process_contract.android --guest-tp`：
     `{"guest_tp_errors":0,"mirror_model":true,"patch_model":true,"trap_decision":true,"isolation":true,"zth_resolved":true,"live_mirrors":0}`。
2. **打通动态重定向符号类型校验与弱 TLS 符号**：
   - 深入归因了 `_ZTHN13LowLevelTasks5FTask10ActiveTaskE` 等 weak TLS 初始化例程在 ELF dynamic symbol table 中的符号类型为 `STT_NOTYPE` (`type=0`)；原 `nso_loader.c` 的 `nso_retarget_imports` 仅允许 `(sym_info & 15) == 2` (`STT_FUNC`)，导致重定向被误判为 unsupported (`status=5 native=95`)。
   - 放行 `STT_NOTYPE` 导入项重定向，并接入 `nso_host_tls_init_noop`，彻底越过 ctor 487。
3. **补齐 C99 libm 数学函数库与标准库工具**：
   - 依序接入并打通 `pow`, `powf`, `atan2`, `atan2f`, `fmod`, `fmodf`, `hypot`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`, `log2`, `log10`, `sqrt`, `sinhf`, `tanhf`, `ceil`, `floor`, `round`, `trunc`, `fabs`, `ldexp`, `modf`, `logbf`, `exp2`, `fmaf`, `nextafterf`, `lrint`, `lrintf` 等浮点运算。
   - 接入 `rand`, `srand`, `isupper`, `atoi`, `atof`, `atoll`, `strtoll`, `strtoull`, `strtoul`, `strtod`, `strcspn`, `strspn`, `strncat`, `strcat`, `strncasecmp`, `bsearch`, `qsort`。
4. **接入 `nn::hid::VibrationConverter` 构造/析构 HLE**：
   - 实现 `_ZN2nn3hid18VibrationConverterC1Ei` 与 `_ZN2nn3hid18VibrationConverterD1Ev` 桩桥接。
5. **历史性里程碑：全量 1,890 项构造函数真机连跑 100% 全部通过！**：
   - 调整 `--guest-resource-quota 16777216` 容纳 >13,500 项页面与 atexit 元数据记录。
   - **真机实测证明：全部 1,890 项构造函数 (`idx=0..1889/1890`) 连续无跳漏顺序执行完成！**
   - 登记并安全暂存 13,536 项 `atexit` 析构回调。
   - 日志实证输出：`[boot-step3] constructors completed successfully! total finished=1890 return=0x0`！
6. **成功推进至游戏实际主入口 `nnMain`**：
   - `[boot-step4] calling nnMain at offset 0x352f160...`
   - `[hle] nn::util::ReferSymbol(0x7887eceacc): verified SDK link anchor`
7. **最新真实阻塞点确定**：
   - 未绑定导入项 `index=126 name=_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi`（`nn::os::CreateThread`，caller_offset=`0x352f1b8`），退出码 133。
   - 游戏在 `nnMain` 启动阶段开始创建其底层子线程（任务调度线程/音频线程/渲染准备线程）。
8. **未完成项明确标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，**未真正实施页面写保护，仍列未完成**；
   - 子线程创建与真机逐线程 TLS/TP 调度将在下一阶段 `CreateThread` 推进中闭环。

## 先读这些

- [可直接复制的完整接力提词](ANTIGRAVITY_PROMPT.md)；[当前证据索引](../docs/analysis/codex_handoff_evidence_index.md)；[所有工作树绝对路径/分支/SHA/WIP快照](WORKTREES.json)。先读本仓库 AGENTS.md，实核 Git 与 writer 后再接手。
- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本地检查点基线 `79ddbb8`。当前 Antigravity 为唯一根总控与独占写入者。
- 严禁打印、记录或提交任何游戏密钥与加密素材，所有大型原始二进制留在 `.local/`。
- 本次 `--boot` 完整原始运行日志已留盘：`.local/logs/rg477m-boot-v17-tp.log` (1.2MB, 18,745 行, exitcode 133)。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **全量构造函数序列** | **100% 达成**：`init_array` 全部 1,890 项构造函数真机连跑通过，无一跳漏，登记 13,536 项 atexit 回调。 |
| **游戏主入口 nnMain** | **成功进入**：offset `0x352f160` 成功执行并验证 `nn::util::ReferSymbol`。 |
| **Guest TP / ActiveTask** | **已实现并真机通过**：1,489 处 `MRS TPIDR_EL0` 动态适配，弱 TLS 符号 `_ZTH*` 绑定通过，`TP+0x238` 访问正常。 |
| **C99 libm / stdlib** | **全族闭环**：`pow`, `atan2`, `sin`, `cos`, `logbf`, `exp2`, `fmaf`, `strtoll` 等全族真机通过。 |
| **最新真实阻塞** | **明确归因**：进入 `nnMain` 后调用的 `_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi`（import 126, caller 0x352f1b8）。 |
| **ProtectRelro 状态** | **明确标注未完成**：当前为打印桩，未实施 mprotect 保护。 |
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

# Antigravity接力入口：接口515 (__nnmusl_init_dso) 与原startup在RG477M成功通过

本轮按用户指令推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。
最新进展：**接口 515 (`__nnmusl_init_dso`)、DSO 登记、主线程 TLS 模板及 `ProtectRelro` 已全部实现并在真机通过。**
真机完整运行 `main.nso` 经 `DT_INIT`（调用 515 返回 1）与 `nninitStartup`（返回 `0x92`，分配器与堆初始化完成，OS TLS 槽 0 析构完成）全流程返回 `EXIT=0`。

## 先读这些

- [可直接复制的完整接力提词](ANTIGRAVITY_PROMPT.md)；[当前证据索引](../docs/analysis/codex_handoff_evidence_index.md)；[所有工作树绝对路径/分支/SHA/WIP快照](WORKTREES.json)。先读本仓库 AGENTS.md，实核 Git 与 writer 后再接手。
- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本地检查点基线 `b137209bc3a2747cf41f41de1ad9d26d1ca8a49d`。当前 Antigravity 为唯一根总控与独占写入者。
- 严禁打印、记录或提交任何游戏密钥与加密素材，所有大型原始二进制留在 `.local/`。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **接口 515 (`__nnmusl_init_dso`)** | **已实现并真机通过**：`guest_process_nnmusl_init_dso` 经 GP21 拆包 21 个机器字（base、text、tls_start、tls_init 544B、tls_total 4121B、align 8、relro/jmprel/rela）。结合 `module_tls` 分配主线程 TLS 镜像。幂等返回 1（通知 DT_INIT DSO 登记完成）。宿主 contracts 全部 0 错误；真机 `--dso-registration` 输出 `{"dso_registration_errors":0,"dso_registered":true,"tls_init_size":544,"tls_total_size":4121,"tls_align":8,"repeat_idempotent":true}`，`EXIT=0`。 |
| **接口 516 (`ProtectRelro`)** | **已实现**：`guest_protect_relro` 接收 5 个指针参数，在 `guest_process_resolve` 中绑定。 |
| **真机完整 DT_INIT + 原 startup** | **真机实测 PASS (`EXIT=0`)**：在 RG477M (`RG477M01445122`) 上运行 `dso-frontier-v4-nso_owned_startup.android-5aa4b786 main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 1048576 --init-wrapper --symbol nninitStartup`：<br>1. `DT_INIT 0x5920950` 执行，调用接口 515 登记 DSO 并返回 1；<br>2. `nninitStartup` (+0x352f150) 执行：申请 OS TLS slot 0，查询内存，预留 62 GB 虚拟空间 (`0xf80000000`)，注册 3 个 `atexit`，完成堆映射，注册 TLS allocator 回调 (`allocate=0x737a7a0830, free=0x737a7a0870`)，返回 `0x92`；<br>3. 真实 OS TLS slot 0 析构函数顺利调用（2轮/1调用/remaining 0），虚拟空间安全释放，**无超时、无崩溃，`EXIT=0`**。 |
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

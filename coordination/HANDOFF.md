# Antigravity接力入口：变参 snprintf 桥接与逐构造函数执行记账验证

本轮按用户指令推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。
最新进展：
1. **AAPCS64 真实变参 `snprintf` 桥接已实现并在真机验证通过**：
   - 彻底避免将 `snprintf` 当作标量六参或直接转换 `va_list` 指针。
   - 实现专用网关 `nb_import_snprintf_gateway`：捕获调用方真实寄存器 `x3-x7`（GP）、`q0-q7`（FP）及调用方原始栈顶 `SP_guest`，组装合规的 32 字节 AAPCS64 `va_list` 结构体，由宿主 Bionic `vsnprintf` 执行格式化。
   - 补充专用回归测试 `--snprintf`，在 RG477M 真机上通过测试：
     `{"snprintf_errors":0,"integers":true,"pointers":true,"strings":true,"floats":true,"length_modifiers":true,"truncation":true,"stack_arguments":true,"gateway_tested":true}`。
2. **逐模块、逐构造函数执行记账已实施**：
   - 在 `nso_call.c` 中为 1,890 项构造函数布置透明追踪跳板（trampoline table），逐项输出 `[ctor-start] idx=...` 与 `[ctor-done] idx=...`。
   - 真机 `--boot` 实测证明：构造函数 0 至 7 按真实顺序完整执行，无跳漏、无重复执行！
     - 构造函数 0 (`+0x25d60`) 执行完成；
     - 构造函数 1 (`+0x70`) 执行完成（原真实调用 `strtol` 与 `snprintf` 均成功返回，`FMallocBinned3` 堆映射完成）；
     - 构造函数 2 至 5 全部顺利执行完毕；
     - 构造函数 6 (`+0x38620`) 经新增 `finite` 绑定顺利执行完成；
     - 构造函数 7 (`+0x6bdf0`) 顺利执行完成，累积登记 72 个 `atexit` 回调；
     - 构造函数 8 (`+0x5c4d0`) 启动并登记第 73 个回调，遇到首个真实阻塞。
3. **首个真实阻塞点确定**：
   - 未绑定导入项 `index=163 name=_ZN2nn2os26InitializeReaderWriterLockEPNS0_20ReaderWriterLockTypeE` (`caller_offset=0x119e74`)，退出码 133。
4. **未完成项明确标注**：
   - `ProtectRelro` 当前仍为桩函数日志记录，**未真正实施页面写保护，仍列未完成**；
   - 逐线程 TP 隔离仍列未完成（仅主线程分配了模板镜像，`TPIDR_EL0` 仍由宿主维护）。

## 先读这些

- [可直接复制的完整接力提词](ANTIGRAVITY_PROMPT.md)；[当前证据索引](../docs/analysis/codex_handoff_evidence_index.md)；[所有工作树绝对路径/分支/SHA/WIP快照](WORKTREES.json)。先读本仓库 AGENTS.md，实核 Git 与 writer 后再接手。
- 根交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，当前主分支 `work/rg477m-native`。
- 本地检查点基线 `b827fc5`。当前 Antigravity 为唯一根总控与独占写入者。
- 严禁打印、记录或提交任何游戏密钥与加密素材，所有大型原始二进制留在 `.local/`。

## 最新真实结果

| 范围 | 证据与边界 |
|---|---|
| **变参 `snprintf` 桥接** | **已实现并真机通过**：`nb_import_snprintf_gateway` 解析 `x3-x7`、`q0-q7` 与栈参数，组装 AAPCS64 `va_list` 传给 Bionic `vsnprintf`。真机 contracts `--snprintf` 输出 0 错误全部 PASS。构造函数 1 中的真实 `snprintf` 调用正常通过。 |
| **逐构造函数记账** | **已实现并在 RG477M 实测实证**：1890 项跳板追踪就绪。已实测证明 idx 0~7 共 8 个构造函数顺序执行完成，无漏执行、无重复执行。 |
| **浮点辅助 `finite`** | **已实现并真机通过**：`NB_SIG_REGISTER_ONLY`（1 FP arg `d0`，1 GP return `w0`），解除了构造函数 6 的阻塞。 |
| **首个真实阻塞** | **明确归因**：构造函数 8 调用的 `_ZN2nn2os26InitializeReaderWriterLockEPNS0_20ReaderWriterLockTypeE`（import 163, caller 0x119e74）。 |
| **ProtectRelro 状态** | **明确标注未完成**：当前为打印桩，未实施 mprotect 保护，不计入闭环语义。 |
| **逐线程 TP 状态** | **明确标注未完成**：主线程分配了静态镜像，但未实施线程上下文与 TP 切换隔离。 |
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

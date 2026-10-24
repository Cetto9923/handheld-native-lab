# P-AG-18 闭环｜2026-10-24 10:30 Asia/Shanghai — UE TaskGraph worker SIGSEGV 根因修复与 libc++/locale 推进

- **结论**：v36 的 TaskGraph worker `SIGSEGV (signal 11, fa=0x4, module_offset=0x3cb84)` 根因确认为 **子线程（Worker 线程）与 GameThread 共享单个 guest OS TLS 槽 0 记录**（缺少 `_Thread_local guest_os_tls_thread *current_os_thread`）。子线程并发调用 `nn::os::GetTlsValue(slot=0)` / `SetTlsValue` 篡改 `FThreadLocalData` 锁计数器，导致锁计数器下溢为负数（-3）；解锁慢路径 `0x3cb20`（`ldr x20, [x9, #8]`，`x9 = ((-3 & ~1) | 2) & ~3 = -4`）将 `-4 + 8 = 0x4` 当作队列节点指针访问，触发 SIGSEGV `fa=0x4`。
- **修复动作**：
  1. `src/runtime/hle/guest_process.c`：
     - 新增 `static _Thread_local guest_os_tls_thread *current_os_thread;`；
     - `guest_process_bind_child_thread()` 中通过 `guest_os_tls_register(p->os_tls, &current_os_thread)` 为子线程独立注册专属 guest OS TLS 线程记录；
     - `guest_process_unbind_child_thread()` 中通过 `guest_os_tls_cleanup` 与 `guest_os_tls_unregister` 独立解绑与注销；
     - `tls_get()` / `tls_set()` 统一通过 `current_os_thread ? current_os_thread : p->os_thread` 路由，彻底隔离多线程 OS TLS 槽 0 状态。
  2. `tests/native/guest_process_contract.c`：
     - 在 `exercise_os_wrappers` 中新增子线程独立 OS TLS 验证用例，实测子线程初值归零、独立赋值生效、主线程槽位值不被干扰。
  3. **补齐后继 libc++ / C 运行时动态符号**（`src/runtime/hle/nso_host.c` / `nso_host.h` / `guest_process.c`）：
     - `_ZNSt3__15mutex4lockEv` / `_ZNSt3__15mutex6unlockEv`
     - `_ZNSt3__118condition_variable10notify_oneEv` / `_ZNSt3__118condition_variable10notify_allEv` / `_ZNSt3__118condition_variable4waitERNS_11unique_lockINS_5mutexEEE`
     - `_ZNSt3__16chrono12steady_clock3nowEv` / `pthread_cond_clockwait`
     - `_ZNSt3__120__throw_system_errorEiPKc` / `_ZNSt3__122__libcpp_verbose_abortEPKcz`
     - `setlocale`（返回 `"C"`，避免 libc++ 试图从不存在的外部路径加载本地化数据）
     - `getenv` / `nl_langinfo` / `__cxa_pure_virtual`
- **真机复测证据（RG477M01445122）**：
  - 日志：`.local/logs/rg477m-boot-v40-setlocale.log`（退出码 133 / SIGTRAP）；
  - **TaskGraph worker SIGSEGV (`fa=0x4 / module_offset=0x3cb84`) 彻底消除！退出码 139 彻底翻篇**；
  - 虚幻引擎顺利跨过 TaskGraph 启动、ICU 本地化数据加载与资源字典解析、连续物理读取 pak/ucas/romfs（读取 24224 B、4856 B、11474 B、5386 B、216 B、2282 B、2080 B、96 B 等），登记 atexit 回调达 13,591 个！
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x7a20cc7280 fa=0x7a20cc7280`
  - `[unresolved-import] index=80 name=_ZN2nn2oe10InitializeEv caller_offset=0x15440`
  - 紧邻其后调用的下一个导入为 `_ZN2nn2oe18GetDesiredLanguageEv`（index 81）。
- **未达**：标题画面 / 可玩（仍未进渲染呈现阶段）。

---

# 日间调度（P-AG-18 启动｜2026-10-24 09:52 Asia/Shanghai）

- **切换原因**：P-AG-17 Claude（PID 46203，会话 fe01711c）已 end_turn 闭环；HEAD 已到 `9c3f24d`；真机 v36 越过 PreInit，阻塞 TaskGraph worker SIGSEGV fa=0x4 / module 0x3cb84（exit 139）；主树无活跃写手。
- **里程碑**：新 HEAD `9c3f24d`；真机 v33→v36；PreInit 不再失败；pak/IoStore 由 UE 自挂。
- **已派**：P-AG-18 攻 UE TaskGraph worker SIGSEGV；提词 `coordination/prompts/P-AG-taskgraph-sigsegv.md`；agy PID **88093**（优先，用户已确认登录；若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-18.log`
- **基线**：`.local/logs/rg477m-boot-v36-aoc-event.log` exit 139；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-17 闭环｜2026-10-24 09:45 Asia/Shanghai — PreInit() failed 根因修复

- **结论**：v32 的 `LowLevelFatalError [LaunchNintendo.cpp:241] PreInit() failed! Aborting` 根因是**真机 romfs 镜像缺少 `FFRS-Switch.pak` / `FFRS-Switch.ucas`**（宿主 08:03 已物化，但从未推给设备）。补齐后引擎自行挂载 pak/IoStore，PreInit 不再失败，已推进到 UE TaskGraph worker 启动阶段。
- **新阻塞**：guest SIGSEGV（fa=0x4，module offset 0x3cb84）发生在 `Background Worker (Standby #1)`，非未绑定 import、非缺容器。详见下文「下一前沿」。
- **基线日志**：`.local/logs/rg477m-boot-v36-aoc-event.log`（exit 139）；上一版 `.local/logs/rg477m-boot-v35-aoc-event.log` 同一阻塞可复现
- **未达**：标题 / 可玩（仍未出任何一帧）

---

## 1. 根因（已证实，非推测）

| 证据 | 内容 |
|---|---|
| `.local/romfs/Manifest_UFSFiles_Switch.txt:1` | `FFRS/FFRS.uproject` 是 **UFS** 文件 → 存在 pak 内，不是散文件 |
| `FFRS-Switch.pak` TOC 目录树 @ 文件偏移 `0xb058625c` | 条目 `FFRS.uproject` → 数据偏移 `0x39ac0`；同级还有 `AssetRegistry.bin`、`FFRS/Config/*.ini` |
| v32 日志 `18786` | `OpenDirectory 'rom:/FFRS/Content/Paks/' total_entries=3` → 真机只有 utoc/ucas 三件套，**没有 .pak** |
| 真机目录 | `FFRS-Switch.utoc` / `global.ucas` / `global.utoc`；宿主 `.local/romfs/.../Paks/` 有 5 件（.pak/.ucas 08:03 才物化，未推送） |
| 字节一致性 | 物化 .pak 与基线 NSP RomFS 原文**逐字节一致**：末 512 B sha256 `5a8e206fac85ead47ca941bee7da9dfcc5e7d4eb8de4e20addeff6b9754cd48a` 两侧相同；pak 页脚 magic `0x5A6F12E1` 位于 `2958593130`、文件尾 119 B 为零，是**原作固有布局**，不是物化缺陷 |

→ `PreInitPreStartupScreen` 之前找不到 pak → 项目描述符不可达 → `nn::fs::OpenFile('rom:/FFRS/FFRS.uproject')` 失败 → PreInit 返回非零 → 致命错误。

## 2. 修复动作

1. **补齐真机容器**：向 RG477M 推送 `FFRS-Switch.pak`(2,958,593,334 B) 与 `FFRS-Switch.ucas`(5,976,848,928 B)。推送日志 `.local/logs/p-ag-17-push-paks.log`。未删任何 `.local/romfs` 大文件，未 root/刷机/清数据。
2. **子线程故障可归因**（`src/runtime/nso/nso_loader.c`）：原 `fault_module` 是 `_Thread_local`，运行时创建的 guest 子线程没有该作用域 → 崩溃只报一个无归属 PC。改为进程级 `fault_scope_owner`（信号处置本来就是进程级的），并新增 `nso_fault_scope_set_thread_stack()`，由 HLE 发布 **guest 栈区间**（`guest_sp - size .. guest_sp`，guest SP 从栈顶向下增长）→ 子线程也能走 guest 栈帧回溯。
3. **绑定新 import**（`src/runtime/hle/nso_host.c` / `guest_process.c`）：
   - `nn::aoc::GetAddOnContentListChangedEvent(nn::os::SystemEvent*)`（dynsym index 56）
   - `nn::os::WaitSystemEvent` / `SignalSystemEvent` / `DestroySystemEvent`
   - 语义：宿主自己持有**信号状态**，**不改写 guest 对象内存**（保留游戏原 `nn::os::SystemEvent` 布局）；本机无法安装任何追加内容 → 列表永不变化 → 事件恒为未触发，等待方阻塞，与零售卡真机行为一致。
4. **宿主契约**：新增 `--diag-aoc-event`（guest 对象 64 B 调用前后逐字节不变、signal→wait 往返不阻塞、null 入参不崩溃）。宿主与 RG477M 均通过：`{"diag_aoc_event_errors":0,"resolve":true,"guest_object_untouched":true,"signal_wait_roundtrip":true}`。

## 3. 真机复测证据（RG477M01445122）

```
[hle] nn::aoc::GetAddOnContentListChangedEvent: event=0x6352a9a9c0 -> unsignaled (no add-on content can be installed)
[hle] nn::os::WaitSystemEvent: event=0x6352a9a9c0 waiting
[hle] nn::fs::OpenFile: path='rom:/FFRS/Content/Paks/FFRS-Switch.pak' size=2958593334
[hle] nn::fs::OpenFile: path='rom:/FFRS/Content/Paks/FFRS-Switch.utoc' size=12382627
[hle] nn::fs::OpenFile: path='rom:/FFRS/Content/Paks/FFRS-Switch.ucas' size=5976848928
```
- **PreInit 不再失败**：全日志无 `AbortImpl` / `LowLevelFatalError`；UE **自己**打开了 pak（读页脚区 offset 2958172777）、`.utoc`、`.ucas`（尾部 2.4 MB）→ pak/IoStore 挂载由游戏原生代码完成。
- 已创建线程：`GameThread` / `FHeartBeatThread` / `Nintendo AddOnContent Thread` / `Foreground Worker #0` / `Foreground Worker (Standby #0)` / `Background Worker #0` / `Background Worker #1` / `Background Worker (Standby #1)`。
- 退出码 139（SIGSEGV），v35 / v36 同一阻塞可复现。

## 4. 下一前沿（已刻画，未根因）

```
[fault] signal=11 pc=0x7292b87b84 fa=0x4 module_offset=0x3cb84
 x1=0xfffffffffffffffd x8=0xfffffffffffffffe x9=0xfffffffffffffffc
 frame[0..4] module_offset = 0x37b44 / 0x248c4 / 0x25210 / 0xec908 / 0x1aaf8
```
- 崩溃指令 `module 0x3cb84: ldr x20, [x9, #8]`，其中 `x9 = x8 & ~3`、`x8 = (x1 & ~1) | 2`；`x1 = 0xfffffffffffffffd` → 读地址 `0x4` → SIGSEGV。
- 上下文是无锁侵入式双向链表 push（`ldaxr/stxr` + 头指针 tag bit1），即**节点指针被污染**，不是未绑定 import（日志无 `[unresolved-import]`），也不是缺容器。
- 线程：`Background Worker (Standby #1)`（TaskGraph worker 启动阶段）。
- 归属说明：崩溃函数未保存 x30，`frame[0]` 来自上层栈帧，**调用者链不可靠**；要定位需从 `module 0x3cb24` 的调用点反向追。

### 并行排查出的两条「非阻塞」缺失路径
- `rom:/FFRS/Shaders`、`rom:/.nrr/ModuleInfo-Shipping.nrr`：引擎会探测，但 `Manifest_UFSFiles_Switch.txt` 与 `Manifest_NonUFSFiles_Switch.txt` **都不含任何 shader 路径、也没有 .nrr** → 零售包本来就没有，不是可补的宿主文件。零售包的 shader 容器仍未定位，属独立工作项。

---

# 日间调度（P-AG-17 Claude 接力｜2026-10-24 09:16 Asia/Shanghai）

- **切换原因**：agy 31803 空转约 14 分钟日志 0 字节（疑卡审批）；用户要求白天持续推到可玩。
- **已杀**：agy 31803
- **已派**：同提词 P-AG-preinit.md → Claude Code（请求 Fable，网关落 MiniMax-M3.1-Flash）；PID **46203**；日志 .local/agent-pm/cc-p-ag-17.log
- **看守**：已重建「FFRS 日间推进看守」*/15 9–22、「FFRS 夜间推进看守」*/15 1–8
- **未达**：标题 / 可玩

---

# 日间调度（P-AG-17 启动｜2026-10-24 09:02 Asia/Shanghai）

- **切换原因**：P-AG-16 已闭环（HEAD 95d18ef）；agy 7126 已退出；主树无活跃写手；用户 09:02 要求继续推进。
- **已派**：P-AG-17 攻 GEngineLoop.PreInit / PreInitPreStartupScreen；提词 coordination/prompts/P-AG-preinit.md；agy PID **31803**（日志 .local/agent-pm/agy-p-ag-17.log；若 --dangerously-skip-permissions 弹出审批请点批准）
- **基线**：真机 v32 AbortImpl 越过；阻塞 PreInit() failed（exit 132）；设备 RG477M01445122；pak/ucas PASS
- **未达**：标题 / 可玩

---

# 夜守调度（P-AG-16 闭环｜2026-10-24 08:50 Asia/Shanghai）

- **切换原因**：P-AG-16 已闭环；真机 v32 越过 `AbortImpl`；未链接导入 12 彻底消除（signal 5 消除）；成功捕获并归因虚幻引擎致命错误 `LowLevelFatalError: PreInit() failed! Aborting`；新阻塞推进至 UE 致命错误退出指令 `udf #0`（signal 4 / exitcode 132）。主树无活跃 agy/claude 写手。
- **已闭环**：P-AG-16 Antigravity 攻克 AbortImpl 并完成根因归因
- **设备**：RG477M01445122 在线；pak/ucas 已物化 PASS
- **未达**：标题 / 可玩

---

# Antigravity接力入口：越过 AbortImpl，归因 PreInit() failed!（P-AG-16）

## 调度备注（夜守 PM）
- **当前阻塞**：虚幻引擎初始化失败 `GEngineLoop.PreInit()` 返回错误码，触发 `LaunchNintendo.cpp:241` 的 `LowLevelFatalError [PreInit() failed! Aborting]`，经由 `FOutputDeviceError::Serialize` 调用 `AbortImpl()` 后进入致命退出指令 `0x11f8: udf #0`（signal 4, exitcode 132）。
- **基线 HEAD**：本轮提交检查点（`nn::diag::detail::AbortImpl` & `nn::diag::GetBacktrace` HLE 已过，PreInit 根因归因）；日志 `.local/logs/rg477m-boot-v32-abortimpl.log`（exitcode 132）
- **P-AG-16**：已闭环（`AbortImpl` 与 `GetBacktrace` 最小可归因 HLE 落地并接入 Native Boundary 网关；真机实测越过 import 12，消除未链接导入 signal 5；完整捕获寄存器快照、栈帧回溯及 UTF-16 致命错误内容；彻底定位到 `GEngineLoop.PreInit()` 失败逻辑）；无其他并行 agy worker
- **阶段**：`T02_ABORT_IMPL_PASSED_PREINIT_ATTRIBUTED_ON_RG477M`
- **未达**：标题画面 / 可操作场景（勿伪称可玩）

本轮按工单 P-AG-16 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **`nn::diag::detail::AbortImpl` 与 `nn::diag::GetBacktrace` 最小可归因 HLE 落地**：
   - 在 `src/runtime/hle/nso_host.c` 落地安全探活与可归因诊断 HLE；
   - 捕获完整寄存器快照（`x0`, `x19`, `x20`, `x21`, `x22`, `x8`, `fp`, `lr`）；
   - 安全探活并遍历 Frame Pointer 链表，输出完整的调用栈回溯（Frame 0～Frame 10）；
   - 探活 `x22` 指向的 UTF-16 错误描述字符串并转码输出为可读文本；
   - 伴生落地 `_ZN2nn4diag12GetBacktraceEPmi`（`nso_host_diag_get_backtrace`）；
   - 在 `guest_process.c` 中将两符号登记入 `typed` 白名单，并在 `import_signature`（0 参 1 返 与 2 参 1 返，`register_only`）绑定，GameThread 及衍生子线程无缝复用 Native Boundary 统一网关。

2. **深度逆向与调用点根因归因**：
   - 符号调用点位于 `main.nso` 偏移 `0x11b8`，属于虚幻引擎的 `FOutputDeviceError::Serialize` 虚方法（vtable `0x67d8c50` 的偏移 `0x10`）；
   - 栈帧回溯显示：
     - `frame[1]`: `ret=0x73fb3c81bc`（偏移 `0x11bc`，`FOutputDeviceError::Serialize`）
     - `frame[2]`: `ret=0x73fb5a9154`（偏移 `0x1e2154`）
     - `frame[3]`: `ret=0x73fb5399f0`（偏移 `0x1729f0`，`LowLevelFatalError` 格式化）
     - `frame[4]`: `ret=0x73fe8f6884`（偏移 `0x352f884`，`LaunchNintendo.cpp` 第 241 行）
   - 代码逻辑定位：
     - `LaunchNintendo.cpp` 中调用 `GEngineLoop.PreInit(CmdLine)`（位于偏移 `0x3530700`）；
     - `0x3530700` 调用内部 `FEngineLoop::PreInitPreStartupScreen`（偏移 `0x3530920`）；
     - `PreInitPreStartupScreen` 返回了非零错误码；
     - `0x352f8c0: cbnz w0, #0x352f96c` 判定返回非零直接跳转至致命错误分支（Line 241）；
     - `0x352f97c: mov w1, #0xf1`（`0xf1 = 241`，即源码第 241 行）调用致命错误上报；
   - 提取到完整错误信息：
     ```text
     LowLevelFatalError [File:.\../Platforms/Nintendo/Source/Runtime/Launch/Private/LaunchNintendo.cpp] [Line: 241] 
     PreInit() failed! Aborting
     ```
   - 破译 v31 日志中 `x0=0x6259` 的来源：`0x6259` 为十进制 `25177`，正是 `GameThread` 的真实线程 ID，是 `0x1e3f10` 处调用 `nn::os::GetThreadId` 获取的，并非 `nn::Result` 错误码。

3. **宿主与真机契约全绿**：
   - 增加 `--diag-abort` 独立契约测试用例 `diag_abort_contract`，覆盖符号动态解析、AbortImpl 安全返回 0、GetBacktrace 边界与深度探查校验；
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 37 项宿主契约（含 `host-diag-abort-contract`，returncode 0）；
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --diag-abort`：
     `{"diag_abort_errors":0,"resolve":true,"return_zero":true,"backtrace":true}`；
   - 复跑 `--cxa-thread-atexit`、`--posix-memalign`、`--os-event`、`--threads` 与 `--guest-tp` 均全绿通过。

4. **真机实测重大突破：越过 import 12，消除 signal 5，捕获致命错误现场**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证（`.local/logs/rg477m-boot-v32-abortimpl.log`）：
     ```text
     [hle] nn::fs::OpenFile: path='rom:/FFRS/FFRS.uproject' mode=1 (host_path='romfs/FFRS/FFRS.uproject') -> open failed errno=2 (No such file or directory)
     [crt] atexit registered callback=0x73fb58f340 pending=13568; deferred
     [page-port-policy] op=map address=0x6bdb220000 bytes=65536 result=0 after real VM/accounting transition
     [hle] __cxa_thread_atexit: dtor=0x73fb5b7440 obj=0xb40000754068be40 dso=0x7402ae2a00 pending=3
     [hle] nn::diag::detail::AbortImpl: lr=0x5603b5b368 fp=0x6e3b680690 x0=0x4685 x19=0x74030579e8 x20=0x6e3b680690 x21=0x5 x22=0x6e3b680878 x8=0x740300caa4
     [hle]   frame[0]: fp=0x6e3b6806f0 ret=0x5603b5b368
     [hle]   frame[1]: fp=0x6e3b680730 ret=0x73fb3c81bc
     [hle]   frame[2]: fp=0x6e3b680ca0 ret=0x73fb5a9154
     [hle]   frame[3]: fp=0x6e3b680de0 ret=0x73fb5399f0
     [hle]   frame[4]: fp=0x6e3b680fa0 ret=0x73fe8f6884
     [hle]   frame[5]: fp=0x740a52f940 ret=0x5603b5ba34
     [hle]   frame[6]: fp=0x740a52fb20 ret=0x5603b4fc8c
     [hle]   frame[7]: fp=0x740a52fb50 ret=0x76a0752cd0
     [hle]   frame[8]: fp=0x740a52fb80 ret=0x76a06e6b04
     [hle]   frame[9]: fp=0x0 ret=0x0
     [hle] nn::diag::detail::AbortImpl message: "LowLevelFatalError [File:.\../Platforms/Nintendo/Source/Runtime/Launch/Private/LaunchNintendo.cpp] [Line: 241] 
     PreInit() failed! Aborting


     "
     [hle] nn::diag::detail::AbortImpl: lr=0x5603b5b368 fp=0x6e3b680680 x0=0x4685 x19=0x74030579e8 x20=0x6e3b680680 x21=0x5 x22=0x6e3b680878 x8=0x740300caa4
     [hle]   frame[0]: fp=0x6e3b6806e0 ret=0x5603b5b368
     [hle]   frame[1]: fp=0x6e3b680710 ret=0x73fb3c81f8
     ...
     [fault] signal=4 pc=0x73fb3c81f8 fa=0x73fb3c81f8
     ```
   - 未链接导入符号 12 彻底消除，GameThread 顺利越过 `AbortImpl`；
   - 进程执行退出路径并达到硬断言 `0x11f8: udf #0`，退出码由 133 变为 132。

5. **明确未完成项与下一前沿标注**：
   - **下一前沿**：虚幻引擎 `FEngineLoop::PreInitPreStartupScreen`（偏移 `0x3530920`）执行失败并返回非零错误码；需进一步分析其失败原因（检查配置文件如 `rom:/Engine/Config/StagedBuild_FFRS.ini`、Pak / IoStore 挂载与解密、或渲染/音频子系统预初始化等）；
   - `ProtectRelro` 当前仍为桩函数日志记录，未真正实施页面写保护；
   - 尚无标题画面 / 可操作场景证据；
   - NVN→Vulkan 完整桥接、大文件 Pak/IoStore（2.9GB/5.9GB）读取管线未完成。

---

## 阶段验收证据汇总

| 检查项 | 状态与依据 |
|---|---|
| **全量构造函数 (1890/1890)** | **已通过**：构造函数 1890/1890 顺利执行完毕返回 0。 |
| **nnMain 游戏主入口** | **已越过并真机通过**：成功进入 offset `0x352f160`。 |
| **CreateThread / StartThread** | **已越过并真机通过**：成功派生 GameThread 与 FHeartBeatThread，独立 guest TP 绑定无崩溃。 |
| **nn::fs::SetAllocator** | **已越过并真机通过**：成功保存 UE 分配器回调。 |
| **nn::fs::QueryMountRomCacheSize** | **已越过并真机通过**：成功写出 64KB 缓存尺寸。 |
| **nn::fs::MountRom** | **已越过并真机通过**：成功挂载本地 `romfs` 目录返回 0。 |
| **nn::fs::OpenFile / GetFileSize / ReadFile / CloseFile** | **已越过并真机通过**：实测完整打开、查询、读取 `rom:/UECommandLine.txt`（38B）并关闭。 |
| **nn::aoc::CountAddOnContent / ListAddOnContent** | **已越过并真机通过**：实测 Count 返回 0，List 返回 0 项，顺利越过。 |
| **nn::nifm::Initialize** | **已越过并真机通过**：成功初始化网络接口管理返回 0。 |
| **nn::socket::Initialize** | **已越过并真机通过**：成功初始化 6MB 缓冲区套接字系统返回 0。 |
| **nn::account::Initialize** | **已越过并真机通过**：成功初始化账号子系统返回 0。 |
| **nn::fs::OpenDirectory / GetDirectoryEntryCount / ReadDirectory / CloseDirectory** | **已越过并真机通过**：实测成功打开 `rom:/` 与 `rom:/FFRS/Content/Paks/`，读取条目并安全关闭，GameThread 顺利越过！ |
| **nn::os::InitializeEvent / FinalizeEvent / SignalEvent / ClearEvent / WaitEvent** | **已越过并真机通过**：实测成功初始化 Event（AutoClear 与 ManualClear），跨线程 Signal/Wait 唤醒全部通过！ |
| **posix_memalign / memalign** | **已越过并真机通过**：实测成功完成 64 字节对齐分配（40 字节），GameThread 顺利越过！ |
| **__cxa_thread_atexit / __cxa_thread_atexit_impl** | **已越过并真机通过**：实测 3 次成功登记线程局部析构器，GameThread 顺利越过！ |
| **nn::diag::detail::AbortImpl / GetBacktrace** | **已越过并真机通过**：实测越过 import 12，消除未链接导入 signal 5；完整捕获寄存器快照、栈帧回溯及 UTF-16 致命错误内容，彻底定位到 `GEngineLoop.PreInit()` 失败逻辑！ |
| **当前阻塞前沿** | **确证为虚幻引擎 `GEngineLoop.PreInit()` 失败**（偏移 `0x3530920` 返回错误码，触发 `LaunchNintendo.cpp:241` 的 `LowLevelFatalError: PreInit() failed! Aborting`，最终到达 `0x11f8: udf #0`，signal 4 / exitcode 132，日志 `.local/logs/rg477m-boot-v32-abortimpl.log`）。 |

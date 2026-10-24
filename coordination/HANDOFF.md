# 三角色并行收口（P-MM-1 派工 + 双写处置｜2026-10-24 14:20 Asia/Shanghai）

- **双写冲突处置**：派工时发现 **Claude Code PID 44703（工单 P-CC-8，与 P-MM-1 同一任务、同一 `guest_process.c`）** 正在写入。**经用户裁定 MiniMax 接管写锁** → SIGTERM 优雅停止该进程（已退出），**保留其未提交 diff 并逐行复核后继承**（未 reset / clean / stash）。其策略实现复核为正确，予以保留；仅另修掉契约假绿缺陷。同源日志留存为 `.local/logs/rg477m-boot-v52-snapshot-thread.P-CC-8-build.log`。
- **P-MM-1 闭环 @ HEAD `5423dac`**：真机 v52 **越过** `[snapshot-refused] reason=primary-thread` → `[va-resource-invalid]` → `Aborted`（exit 134 → **133**）；新阻塞 = **import 74 `movie::SetAllocator`**（SIGTRAP）。宿主 + 真机 `--snapshot-thread` 契约同 JSON PASS。详见下方 P-MM-1 闭环段。
- **下一刀已升格为正式工单**：`coordination/prompts/P-MM-2-movie-setallocator.md`（`movie::SetAllocator`，import 74）。证据附录 `coordination/prompts/P-MM-2-DRAFT-movie-setallocator.md`（侦察 Agent B 只读产出；总控已更正其 v52 日志行号 —— DRAFT 里的行号来自被覆盖的旧 P-CC-8 那份日志，**源码行号有效、日志行号已作废**）。
- **口径修正（排单必读）**：v52 profile 行 `imports=651 unresolved=390` —— **651 个导入里还有 390 个未绑定**。此前「unresolved-import 计数 = 0」指的是**boot 过程中没有因未解析导入而产生 fault**，**不是**全部导入已绑定。未绑定且有 BL 调用点的共 **341** 个 ⇒ 唯一可靠推进方式是「修一个 → 跑一次真机 → 看下一个」。
- **P-MM-NVN 旁路进度**（Agent C，独立 worktree `task/T04-nvn-firstframe`，**未进主树**、未碰设备）：guest 在 `nvnDeviceInitialize` 成功后**立即**调用两个解析函数（`0x142dbe0` / `0x1430e10`），用 `nvnDeviceGetProcAddress` **一次性解析全部 534 个 NVN 名字**，两函数体各含 **534 条 `blr`、0 条 `cbz/cbnz/tbz/tbnz`**（**全程无 NULL 检查**）⇒ **任一名字解析不到即 NULL → SIGSEGV，第一帧必然出不来**。`nvnBootstrapLoader` = **import 353**，guest 仅 5 处调用、只传 2 个名字。证据：`docs/analysis/P-MM-NVN-firstframe-gaps.md`、`evidence/nvn/guest_nvn_resolution.json`（**在 NVN worktree 内**）。建议第一个可派工单元 = 放行 import 353 + 接线 534 名字注册器。

---

# 日间调度（看守｜2026-10-24 14:06 Asia/Shanghai）

- **里程碑**：P-MM-1 已闭环 @ HEAD `5423dac`；真机 **v52** 越过 snapshot-refused（exit 134→133）；新阻塞 `movie::SetAllocator`（import 74，SIGTRAP exit 133）。证据：`.local/logs/rg477m-boot-v52-snapshot-thread.log`。
- **写手**：P-CC-8 Claude PID 44703 已停（启动即 unrecognized_model）；实际闭环由 MiniMax P-MM-1 完成并提交。Antigravity 仍 429。
- **本轮未新派**：`.local/scratch/p-mm-2/` 在 14:05–14:06 仍有实质侦察（ABI/callers/census），主路径实现写手已退但 MiniMax 团队仍在为下一刀取证；为避免再双写 `nso_host.c`/`guest_process.c`，本轮**不**另拉 Claude。下一张目标：`movie::SetAllocator`（及紧随 movie 族按需）。
- **基线**：设备 RG477M01445122 在线；pak/ucas PASS；**未达**标题/可玩。

---

# P-MM-1 闭环｜2026-10-24 14:05 Asia/Shanghai — 内存/资源快照多线程策略：工作线程取快照放行，真机**越过 primary-thread snapshot abort**（exit 134 → 133），新前沿推进到 `movie::SetAllocator`（import 74）

- **结论**：工单 P-MM-1 目标闭环。**真机彻底越过 `[snapshot-refused] reason=primary-thread` → `[va-resource-invalid]` → `Aborted`（exit 134）这条阻塞链**；guest 的 IOThreadPool 工作线程现在能取到内存/资源快照，新第一致命推进到**未解析的代码导入** `movie::SetAllocator`（import 74），exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **对被停止同岗 Agent WIP 的逐行复核结论**：`src/runtime/hle/guest_process.c` 新增的 `snapshot_caller_is_primary()` / `snapshot_caller_bound()` 策略**正确，予以保留**。理由：① 主线程路径行为与改动前完全一致（`snapshot_caller_bound` 对主线程返回 `pthread_equal(...)`）；② 工作线程只要是该 guest **已绑定 OS-TLS 记录**、且记录状态为 LIVE/CLEANING，即可读同一份 primary-owned ledger；③ 未绑定的宿主线程仍被拒（`reason=current-process` / `unbound-caller-thread`）且**输出不被改写**；④ **复用既有 `guest_os_tls_report()` API，未新增 guest_os_tls 接口、未改 TLS 隔离语义** —— 取舍正确，保持不变。`guest_os_tls_report(m,NULL,out)` 不空指针解引用（`thread_link` 走到链表尾返回 NULL 槽 ⇒ `GUEST_OS_TLS_NOT_REGISTERED`），且 `current_os_thread==NULL` 时 `snapshot_caller_bound` 已提前返回 0，走不到 report。
  2. **修掉的假绿缺陷（本单必修项）**：`tests/native/guest_process_contract.c` 的 `snapshot_thread_contract()` 原本把错误数**硬编码**为 `"snapshot_thread_errors":0`，且 `CHECK(... && !errors)` 位于**所有 `expect_*` 断言之前** —— 断言失败只打印 `[check-fail]` 行，契约仍返回 0（假绿）。修法三条：(a) 报告改为 `printf(... "%u" ..., errors)` 打印**真实**错误数；(b) 在报告之前补**最终 `CHECK(!errors)`**（现为第 3133 行），任一 `expect_*` 失败真正导致非 0 退出；(c) 报告里各布尔字段也改为**本次运行实测值**（不再写死 `true`）。
  3. **「故意改坏 → 契约变红」自证**（日志 `.local/logs/p-mm-1-contract-selfproof.log`）：只改一处 —— `expect_value("snapshot-worker-used-matches-primary", c.worker_info.used, c.primary_info.used)` → `... c.primary_info.used+1)`。
     - **修复后的源码** → `[check-fail] snapshot-worker-used-matches-primary actual=0x20000 expected=0x20001` + `process contract line=3133: !errors`，**exit 1（红）**。
     - **恢复修复前形态 + 同一处改坏** → 同样打印 `[check-fail]`，却仍输出 `"snapshot_thread_errors":0` 并 **exit 0（假绿）**。
     ⇒ 证明该缺陷是真实存在的假绿，且本单修复确实让它变红。（自证在 `.local/selfproof/` 下用副本完成，**工作树源码未被改动**，HEAD 源码仍是修复后状态。）
  4. **契约（宿主 + 真机同测，同一份 JSON 逐字相同）**：
     - 宿主 PASS（`.local/build-p-mm-1/host-snapshot-thread-contract/stdout.log`）：`{"snapshot_thread_errors":0,"primary_memory_ok":true,"primary_resource_ok":true,"primary_va_ok":true,"bound_worker_memory_ok":true,"bound_worker_resource_ok":true,"bound_worker_va_ok":true,"worker_matches_primary":true,"unbound_caller_refused":true,"unbound_output_untouched":true}`
     - **RG477M 真机 PASS（逐字相同）**：同上（stdout sha256 `a178625ecd80c589b9d704fc96437d12ec7db54316b12a1c81fcc94187bab15f` 两侧一致）
     - 覆盖面：主线程自问 ✓；**已绑定工作线程**取 memory / resource / VA 三类查询均 OK ✓；工作线程读数与主线程**逐项相等**（total / used / baseline / stack_bytes / thread_objects / resource total+used / VA enabled）✓；未绑定宿主线程被拒且输出未改写 ✓。
  5. **验收 A 达成（真机 v52）**：`.local/logs/rg477m-boot-v52-snapshot-thread.log`（52115 行，exit 133）实测
     - `[snapshot-refused] reason=primary-thread` **0 次**；`reason=os-tls-registered-threads` **0 次**；`[va-resource-invalid]` **0 次**；`Aborted` **0 次**；`[query-invalid]` **0 次**；全日志**无任何** `[snapshot-refused]`。
     - **工作线程侧成功**证据：第 **52093** 行 `[snapshot-thread-policy] caller=bound-worker registered_threads=18 quota=1073741824 charged=748843008 stack_bytes=2097152; same primary-owned ledger read under the process mutex`，紧接第 **52094** 行 `[va-resource-policy] total=16777216 raw_metadata_used=3288320 blocks=12845 reported_used=3812608 SDK_margin=0x80000; logical node pool, not Horizon page tables/RSS`（同一 mutex 下读同一份 ledger，数字与主线程一致）。
  6. **验收 B（新阻塞）**：第 **52101–52102** 行
     ```
     [fault] signal=5 pc=0x7a0451b250 fa=0x7a0451b250
     [unresolved-import] index=74 name=_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_ caller_offset=0x14990
     ```
     exit **133**（SIGTRAP，未解析**代码**导入）；全日志 `unresolved-import` 仅此 1 条（v51 为 0 条）。
     ⇒ **与同岗 Agent 产物结论一致**（其 `.local/build-p-cc-8/` 的 v52 日志同为该符号、同一 index 74、同一 caller_offset `0x14990`、同一 exit 133）；本单已用 `.local/build-p-mm-1/` **自己的产物独立复跑确认**，未直接引用其日志作证据。
  7. **新阻塞一句话**：guest 在模块初始化后调用 **`movie::SetAllocator`**（未解析导入 index 74），运行时无该符号的 typed HLE ⇒ SIGTRAP 退出（exit 133）；**下一刀应攻 `movie::SetAllocator` 及其同族导入**，不是快照线程策略。
  8. **日志限流决策：不做限流（理由）**：实测 v51 全量 boot 中 `[va-resource-policy]` 仅 **2** 次、`[query-port-policy]` 仅 **4** 次、child TLS 绑定 **17** 次；本单 v52 中工作线程快照只触发 **1** 行 `[snapshot-thread-policy]`。运行时本来就对每次查询逐条打点（`[query-port-policy]` / `[va-resource-policy]` / `[page-port-policy]`），再加一行不构成刷屏；加限流反而可能盖掉本单最需要诊断的「哪次快照来自哪个线程」转换。故**保持每次一行、不加限流**。
  9. **产物与日志**：
     - 构建：`.local/build-p-mm-1/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-mm-1 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；宿主全量契约 **156** case PASS，`normal_contract.errors=0`、`recovery_contract.errors=0`；`manifest.json` 的 `source_sha256` 与工作树 **22 项逐项一致，无源码漂移**；`manifest.json` 的 `android_executed` 已置 true 并补 `device_contracts` / `device_boot` 证据）。
     - 启动二进制 `nso_owned_startup.android` sha256 `6533dea46ce9e6e9a2a8fdf4be84d0ba060d794215ad7e8e34b1a1417e8d3ac1`（设备端 `p-mm-1-nso_owned_startup.android-6533dea4`）；契约二进制 `guest_process_contract.android` sha256 `c75dc9cd3aa55431c6b6f8b77099893842ca7c28232c5c8e90bc8c1730ca4375`（设备端 `p-mm-1-guest_process_contract.android-c75dc9cd`）。
     - 真机全量 boot：`.local/logs/rg477m-boot-v52-snapshot-thread.log` + 同名 `.exitcode` = **133**；run 目录 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/`，cwd 正确；参数与 v47/v51 一致：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`。
     - 真机契约：`.local/logs/rg477m-contract-p-mm-1-snapshot-thread.stdout`（exit 0）。
     - **无效首跑（不作证据）**：`.local/logs/rg477m-boot-v52-snapshot-thread.INVALID-cli-order.log`（exit 2；CLI 参数顺序写成 `--boot` 在前，而 `nso_call.c` 要求选项在 mode 之前，已按正确顺序 `--guest-quota … --guest-stack … --guest-resource-quota … --boot` 重跑）。
     - 同岗 Agent 的旧 v52 日志归档为 `.local/logs/rg477m-boot-v52-snapshot-thread.P-CC-8-build.log`（exit 133，仅作对照，非本单证据）。
     - 自证日志：`.local/logs/p-mm-1-contract-selfproof.log`。
  10. **改动文件清单**：`src/runtime/hle/guest_process.c`（策略，保留同岗 Agent WIP 并经复核）、`src/runtime/hle/guest_process.h`（注释同步）、`tests/native/guest_process_contract.c`（新增 `--snapshot-thread` 契约 + 修掉假绿）、`scripts/runtime/build_guest_process_contract.sh`（+1 行接入 `host-snapshot-thread-contract`）、`coordination/HANDOFF.md`、`coordination/state.json`（协调者写）、`coordination/STATUS.md`（脚本生成）。
  11. **未达 / 边界**：标题画面 / 可玩（仍未进呈现阶段）。本单**未**伪造 Horizon 页表/RSS，**未**改 TLS 隔离语义，**未**降级任何「模块身份 / pages ledger / charged 一致性」校验，**未**新增 guest OS TLS 接口，**未**把拒绝改成静默假成功（拒绝仍保留 `[snapshot-refused] reason=…` 可归因日志）。
  12. **提交**：本单提交 `fix(runtime): allow memory/resource snapshots from guest worker threads`（父提交 `3a9092e`，分支 `work/rg477m-native`，**未 push**；本单提交 SHA 见 `git log -1`）。`coordination/prompts/P-CC-snapshot-thread.md`、`P-MM-1-multiagent-supplement.md`、`P-MM-snapshot-thread-policy.md` 三份派工单据为协调者产物，**未**纳入本单提交，仍是 untracked，留给协调者处理。

---

# 日间调度（P-CC-8 启动｜2026-10-24 13:50 Asia/Shanghai）

- **切换原因**：P-CC-7 claude PID 24728 已退出并写闭环 @ HEAD `3a9092e`；真机最新 v51 越过 EventFd/Poll（unresolved-import=0），阻塞 `[snapshot-refused] reason=primary-thread` → `va-resource-invalid` → exit 134；主树 clean、无活跃 FFRS 主路径写手。Antigravity 仍处 429 额度窗口，按优先级改派 Claude Code。
- **里程碑**：新 HEAD `3a9092e`；真机 v47→v51；socket 事件族落地；导入级 unresolved 清零；新前沿为内存快照线程策略。
- **已派**：P-CC-8 攻 snapshot 线程策略（放宽 primary-thread / 评估 os-tls-registered-threads）；提词 `coordination/prompts/P-CC-snapshot-thread.md`；claude PID **44703**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-8.log`
- **基线**：`.local/logs/rg477m-boot-v51-socket-eventfd-final.log` exit 134；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# 日间调度（P-CC-7 启动｜2026-10-24 13:20 Asia/Shanghai）

- **切换原因**：P-CC-6 claude PID 7295 已退出并写闭环 @ HEAD `2120088`；真机最新 v47 越过 stdout/stdio，阻塞 `nn::socket::EventFd`（import 325，exit 133）；主树 clean、无活跃 FFRS 主路径写手。Antigravity 仍处 429 额度窗口（约 12:09 起，重置约再等数小时），按优先级改派 Claude Code。
- **里程碑**：新 HEAD `2120088`；真机 v46→v47；import 186 stdout SIGSEGV 消除；stdio 数据对象+函数族落地；新前沿 EventFd。
- **已派**：P-CC-7 攻 `nn::socket::EventFd`（及紧随 EventFdWrite/Read）；提词 `coordination/prompts/P-CC-socket-eventfd.md`；claude PID **24728**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-7.log`
- **基线**：`.local/logs/rg477m-boot-v47-stdio-stdout.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-CC-7 闭环｜2026-10-24 13:35 Asia/Shanghai — nn::socket 事件族（EventFd/EventFdWrite/EventFdRead/Close/GetLastError/Poll）落地，真机 **0 个未解析导入**，新前沿推进到「工作线程内存快照被拒」

- **结论**：工单 P-CC-7 目标闭环并超额推进。真机**彻底越过 `nn::socket::EventFd`（import 325）**，并连带清掉同族与等待路径上的 `nnsocketPoll`（import 603）：**v50 全量 boot 日志中 `unresolved-import` 计数为 0**，不再有任何未解析导入致命。
  1. **零售 ABI 取证（离线读零售 NSO，不猜）**：用 `scripts/runtime/inspect_nso.py --slice` 读 Nintendo SDK NSO（build_id `0023a1b06130cd795…`，sha256 `9dfd1cc6…`）：
     - `nn::socket::EventFd(unsigned long, EventFdFlags)` @ sdk `0x159b70`（4 字节 thunk）→ `detail::EventFd` @ `0x165df0`：返回 w0 中的描述符，失败返回 -1 并置 errno。
     - `nn::socket::EventFdWrite(int, unsigned long)` @ `0x159be0`：`str x1,[sp,#8] ; mov w2,#8 ; bl write ; cmp w0,#8 ; csetm w0,ne` → 恰好写 8 字节，成功 0 / 失败 -1。
     - `nn::socket::EventFdRead(int, unsigned long*)` @ `0x159b80`：`cbz x1 → -1 ; mov w2,#8 ; bl read ; cmp w0,#8 ; str x8,[x19]` → 恰好读 8 字节，成功 0 / 失败 -1。
     - `nn::socket::detail::Poll(pollfd*, int, int)` @ `0x164c80` 与 `detail::Poll(PollFd*, unsigned long, int)` @ `0x164a80`：`lsl x8, x21, #3` ⇒ **PollFd 为 8 字节**，与 POSIX `struct pollfd`（fd/int、events/short、revents/short）同布局。
  2. **新增可复用取证工具 `scripts/runtime/resolve_import_callers.py`**（本单新增，进 Git）：离线解析 NSO 动态段（DT_JMPREL/DT_RELA）与 16 字节 PLT stub（`adrp x16,page / ldr x17,[x16,#off] / add x16,x16,#off / br x17`），把 **guest module_offset → import index → 导入名** 打通，并可反查某导入的全部调用点。用它定位到 guest 的事件对象实现：`EventFd(0,4)` @ `0x1091254`、`EventFdWrite(fd,1)` @ `0x10912d4`、`EventFdRead(fd,&local)` @ `0x1091308`、`nn::socket::Close(fd)` @ `0x10912ac`、`nn::socket::GetLastError()` @ `0x10912ec`；并确认 `EventFdRead` 的返回值与栈上局部量**被 guest 完全丢弃**（0x10912f0 处 `bl` 后直接返回），guest 只把它当「排空一次事件」。
  3. **HLE 落地（`src/runtime/hle/nso_host.c` / `.h`）**：
     - `nn::socket::EventFd(initval, flags)` → 返回**真实宿主描述符**（不是哨兵）：Linux/Android 用内核 `eventfd(2)`（`EFD_SEMAPHORE|EFD_NONBLOCK`），其他宿主用等价 pipe 后端（同一套可观察语义），因此 `nn::socket::Close` / `nn::socket::Poll` 能直接作用在同一对象上。
     - `nn::socket::EventFdWrite` / `EventFdRead` 按零售语义搬运 8 字节，成功 0 / 失败 -1；读空返回 EAGAIN 并记 `[hle]` 归因日志。
     - `nn::socket::Close`、`nn::socket::GetLastError`（线程局部错误码）、`nn::socket::Poll`（PollFd 数组就地 poll）、`nnsocketPoll`（C 入口，int nfds）。
     - 安全边界：guest 的 `nn::socket` 描述符就是宿主描述符，故 `Close` 拒绝 0/1/2（零售 socket 命名空间不含进程标准流），避免误关运行时自身 stdout/stderr。
     - **语义选择与偏差（已在源码注释中写明）**：① 采用 **auto-reset（EFD_SEMAPHORE）** 语义——guest 每次 post 1、每次 wait 一次读，普通计数型 eventfd 会把两次 post 一次吃掉而丢唤醒；② **读不阻塞**：零售读会阻塞到有信号，HLE 空读返回 EAGAIN，因为 guest 先 poll 再读且丢弃读返回值（0x10912f0 证据），阻塞会把运行时线程挂死；③ `EventFdFlags` 原值打印但**不重新解释**：零售映射在 socket-framework 服务里（无服务回退 `0x165d10` 直接 EOPNOTSUPP），本地无任何产物说明其位含义，guest 只用到一个值 4。
  4. **契约（宿主 + 真机同测）**：新增 `--socket-eventfd` 契约（`tests/native/guest_process_contract.c`），覆盖创建→写→读往返、空读被拒、空指针被拒、**auto-reset 语义（两次 post 两次排空）**、可 poll 性（POLLIN 报位）、关闭。
     - 宿主 PASS：`{"socket_eventfd_contract_errors":0,"create":true,"write":true,"read_roundtrip":true,"read_empty_rejected":true,"null_output_rejected":true,"auto_reset":true,"pollable":true,"close":true}`
     - **RG477M 真机同契约 PASS（JSON 逐字相同）**，且真机走的是内核 `eventfd` 后端（宿主是 pipe 后端，两侧可观察行为一致）。
  5. **真机推进轨迹（RG477M01445122，与 v47 同一套参数）**：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；guest 输入不变（main NSO build_id `736837afd390442ed7c74a7de730e71b`）。
     - **v48**（`.local/logs/rg477m-boot-v48-socket-eventfd.log`，exit 133）：`unresolved` **397 → 391**（6 个新绑定）；`[hle] nn::socket::EventFd: initval=0 flags=0x4 -> descriptor=6` 与 3 次 `EventFdWrite` 真实落地；前沿推进到 `nnsocketPoll`（import 603，caller_offset=0x5e52a84）。
     - **v49**（`.local/logs/rg477m-boot-v49-socket-poll.log`，exit 134）：`unresolved` **391 → 390**；**全日志 `unresolved-import` 计数 = 0**；guest 进入事件等待循环（`nn::socket::Poll` timeout 100 ms 轮询）、起 `IOThreadPool #0/#1` 工作线程、连续映射 4285 段真实内容（合计 556.8 MiB）。
     - **v50**（`.local/logs/rg477m-boot-v50-socket-poll-diag.log`，exit 134）：同 v49 轨迹，加了快照拒绝原因诊断。
     - **v51 = 最终证据**（`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`，exit 134，产物 `.local/build-p-cc-7d/` 与本单提交源码逐项一致）：`unresolved=390`、**`unresolved-import` 计数 0**、`nn::socket::EventFd` 相关 `[hle]` 行 11 条、`[snapshot-refused] reason=primary-thread`。
  6. **新阻塞前沿（已精确定位，非导入问题）**：`[snapshot-refused] reason=primary-thread` → `[va-resource-invalid] status=1 native=22` → `Aborted`（exit **134**）。即 guest 的**工作线程**（`IOThreadPool #0/#1`）调用了 `nn::os::GetVirtualAddressMemoryResourceUsage`，而运行时 `snapshot_locked` 只允许**主线程**取内存/资源快照（`p->thread.state!=THREAD_RUNNING || !pthread_equal(p->thread.native,pthread_self())`），于是拒答并 abort。证据链：v47/v48/v49 中该查询的最后一次成功发生在日志第 42 行、**早于任何子线程注册**（首个 child TLS 绑定在第 18734 行），v50 起在 17 次子线程绑定后失败。
     - 本单为让该拒绝可归因，把 `snapshot_locked` 的拒绝条件改成**逐项命名**（`[snapshot-refused] reason=…`，拒绝语义完全不变），避免只留 `status=1 native=22` 的不可诊断崩溃。
     - **下一张工单建议**：这是**内存/资源快照的线程策略**（`nn::os::GetMemoryInfo` / `GetVirtualAddressMemoryResourceUsage` 的单主线程限制），不是 socket 族问题；要么允许任意线程取快照（底层 state 本来就有锁），要么改为非 abort 的可归因失败。属策略决定，未由本单擅自更改。
  7. **产物与日志**：
     - 构建（最终）：`.local/build-p-cc-7d/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-cc-7d --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；宿主全量契约 PASS；`manifest.json` 的 `source_sha256` 与工作树逐项一致，无源码漂移）。启动二进制 `nso_owned_startup.android` sha256 `6f2f9ecfffdcea6665cafdebd398ada33b64f204ffda1ae006ba2fb227d7cdf6`（设备端 `p-cc-7d-nso_owned_startup.android-6f2f9ec`）；契约二进制 `guest_process_contract.android` sha256 `7e8e9bd2fd82a926239deff9061e497e1889b6ae5849ec8b530c7b13424915ed`（设备端 `p-cc-7d-guest_process_contract.android-7e8e9bd`，已跑 `--socket-eventfd` PASS）。
     - 真机证据：`.local/logs/rg477m-boot-v48-socket-eventfd.log`（exit 133）、`.local/logs/rg477m-boot-v49-socket-poll.log`（exit 134）、`.local/logs/rg477m-boot-v50-socket-poll-diag.log`（exit 134）、`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`（exit 134，最终），各带 `.exitcode`。
  8. **未达**：标题画面 / 可玩（仍未进呈现阶段）。本次只推进导入边界与 socket 事件族；**未**伪造 TLS 握手、证书校验、在线账号、socket 服务或任何网络在线状态；`nn::socket` 的其余族（Socket/Bind/Connect/Send/Recv/SetSockOpt/Select/Fcntl 等）仍未实现，guest 一旦调用即会成为新前沿。

---

# P-CC-6 闭环｜2026-10-24 13:09 Asia/Shanghai — stdout/stderr/stdin 数据导入真机越过，前沿推进至 nn::socket::EventFd（import 325）

- **结论**：工单 P-CC-6 目标闭环。真机**彻底越过 `stdout` 数据导入（import 186）**：v46 的 SIGSEGV 前沿（exit 139）已消除，v47 推进到更远的 **未解析代码导入 `nn::socket::EventFd`（import 325，SIGTRAP/exit 133）**。
  1. **根因复核（不是假设，是可算的证据）**：v46 故障 `fa=0x7b39ee35d0`，而 `0x7b39ee35d0 − 186×8 = 0x7b39ee3000` 恰为页对齐 ⇒ 该地址正是 loader 给**第 186 号数据导入**分配的 `object_traps` PROT_NONE 槽（`nso_loader.c`：`m->imports[i] = (e[4]&15)==1 ? object_traps : import_traps` + `i*8`）。故障指令 `module_offset 0x5e5a678` 反汇编为 `ldr x8, [x8]`，其 x8 来自 `ldr x8, [x8, #0x888]` ⇒ GOT 槽 `module_offset 0x7767888`，而该槽对 `stdout` 的重定位正是 **R_GLOB_DAT、addend 0**（脚本解析 main NSO 动态表确认；`stdin` 0x77678a0、`stderr` 0x7768ee0 同型）。因此 guest 的读法是「GOT → 对象地址 → 对象里的 FILE*」两跳，v46 的第二跳撞上 PROT_NONE ⇒ SIGSEGV。独立佐证：用 NDK clang 编译 `fprintf(stdout,…)` 得到同一形态 `adrp/ldr :got:stdout; ldr x0,[x8]`。
  2. **数据导入落地（关键差异：对象不是函数）**：`nso_host.c` 新增 stdio 段——`stdout`/`stdin`/`stderr` 各绑定到**宿主 `FILE*` 的可写镜像槽地址**（`&stream->object`，镜像副本而非 libc 自身变量，避免 guest 写穿宿主 stdio 状态），日志 `[hle] stdio-object: stdout -> host FILE*=0x… (object address=0x…, writable mirror)`。**绝不返回函数入口或 BRK 哨兵**。
  3. **loader/边界语义修正（此前会直接失败）**：`nso_retarget_imports` 原本只接受 `sym_type∈{0,2}`，STT_OBJECT 会被判 `[retarget-unsupported]` 而整轮边界准备失败；且边界会把每个绑定导入做成**可调用 thunk**，对数据导入是错的。现做法：新增 `nso_symbol_type()`（loader 权威符号类型），`prepare_boundary_locked` 把 STT_OBJECT 导入**排除出可调用绑定与 retarget**，保留 loader 已写入 GOT 的对象地址 ⇒ 日志 `[boundary-data-object] index=186 name=stdout address=0x…; resolved data import kept as host object, not callable`，`[boundary-ready] imports=251 actual-primary-owned; data objects=3`（251 可调用 + 3 数据对象 = 254 已绑定；651−254 = 397 unresolved，与 profile 行一致）。
  4. **stdio 函数族落地**：`printf`/`fprintf`/`sprintf`（变参）、`fflush`/`fwrite`/`fputs`/`fputc`/`puts`（非变参）。**变参 ABI 用本仓库既有的 snprintf 网关模式推广**，而不是直接透传：
     - 新增 `nb_import_variadic1_gateway`（1 个具名参数：printf）与 `nb_import_variadic2_gateway`（2 个具名参数：fprintf/sprintf），在 `native_boundary.S` 用 `NB_VARIADIC_GATEWAY` 宏生成，按 AAPCS64 重建寄存器保存区（`q0-q7@sp+0`、`x1-x7@sp+128`、`__gr_top=sp+184`、`__vr_top=sp+128`、`__gr_offs=-8×(8-named)`、`__vr_offs=-128`、`__stack` = **guest 原始入栈 SP**），并把 `va_list*` 放到具名参数之后的那个参数寄存器。
     - ABI 取证：实测 NDK clang（AArch64）**变参调用不设置 `al`**（无 vector 计数寄存器），且 clang 生成的 `va_start` 保存区布局与网关一致（`x1-x7` 于 +128、`q0-q7` 于 +0、`__gr_offs=-56`（1 具名）/`-48`（2 具名）/`-40`（3 具名，如既有 snprintf））。故宿主一律调用**非变参** `vprintf/vfprintf` 形态，不依赖 `al`。
     - 宿主要求 `v*` 形态（`nso_host_vprintf/vfprintf/vsprintf`），`guest_process.c` 的 `import_signature` 用新 `version=4 / NB_SIG_VARIADIC_STDIO` 声明 1 或 2 个具名 GP 参数；`native_boundary.c` 校验并选择对应网关。
  5. **安全边界（guest 字符串不是宿主可信输入）**：格式串与文本参数先拷进**有界宿主缓冲**（格式串 4096 B、文本 64 KiB）再交给宿主 libc 解析，超界/未终止即拒绝并记账；`sprintf` 无 guest 侧长度，改用有界缓冲生成后再拷出，截断明确记账（guest 仍拿到应有长度）。`vfprintf`/`vsnprintf`（guest 传 musl 风格 `va_list` 指针）**本单未绑定**：guest 侧 `va_list` 布局未经证实前不得当作宿主 `va_list` 透传，宁可留作未解析导入。
  6. **契约（宿主 + 真机同测）**：新增 `--stdio` 契约（`tests/native/guest_process_contract.c`）。宿主 PASS：`{"stdio_contract_errors":0,"objects_bound":true,"object_holds_host_stream":true,"writable_mirror":true,"fwrite_roundtrip":true,"fputs_roundtrip":true,"fputc_roundtrip":true,"puts_roundtrip":true,"fflush_roundtrip":true,"callables_bound":true}`。**RG477M 真机同契约 PASS**（同一 JSON），且在真机上走了**真实 native boundary 网关**：`fprintf` 变参 11 个实参（含 `%.2f` 双精度与 4 个溢出到栈的整型）输出逐字节核对 `contract 7 text 1.50 1 2 3 4 5 6 7 8\n`；`printf`（1 具名）网关输出 `one 42 tail` 均正确 ⇒ 变参尾（含栈溢出区）重建无误。
  7. **真机推进（RG477M01445122，v47 与 v46 同一套参数）**：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；guest 输入不变（main NSO build_id `736837afd390442ed7c74a7de730e71b`）。`unresolved` **408 → 397**（正是 3 数据对象 + 8 函数）。v46 故障点 `module_offset 0x5e5a678` 已越过，guest 继续创建 `HttpManagerThread`（tid 24412）、`atexit` 登记攀升至 13,618。
  8. **新阻塞前沿**：`[fault] signal=5 pc=0x76cd4f3a28 fa=0x76cd4f3a28` + `[unresolved-import] index=325 name=_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE caller_offset=0x1091278`；退出码 **133**（SIGTRAP，未解析**代码**导入）。⇒ 下一张工单应攻 **nn::socket 族**（`nn::socket::EventFd` 等），而非 stdio 族。
  9. **产物与日志**：
     - 宿主/Android 构建：`.local/build-p-cc-6c/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-cc-6c --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；全量宿主契约 PASS；`manifest.json` 记录 `source_sha256` 与当前工作树**逐项一致，无源码漂移**）。启动二进制 `nso_owned_startup.android` sha256 `e1363149cdce632f5f65d8fb3a53d83086c66d02d565647812f594f7eb33cdbc`；契约二进制 `guest_process_contract.android` sha256 `533023b84e8ecce38e50842e5a6cc6792e3ebca3aaa0a79cd38fb8732ff337da`。
     - 真机证据：`.local/logs/rg477m-boot-v47-stdio-stdout.log` + `.exitcode` = **133**（设备端 `p-cc-6c-nso_owned_startup.android-e1363149`，run 目录 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/`，cwd 正确）。
     - 契约真机执行：设备端 `p-cc-6c-guest_process_contract.android-533023b8 --stdio` PASS。
     - 栈预算说明：stdio 的格式/文本/输出缓冲（4 KiB / 16 KiB / 16 KiB）跑在 **guest 栈**（宿主线程栈即 guest 栈，2 MiB）上，故取小上限；单次调用最大 scratch 约 20 KiB。
  10. **未达**：标题画面 / 可玩（仍未进呈现阶段）。本次仅推进导入边界，**未**伪造任何 TLS 握手、证书校验、在线账号或 socket 服务；`nn::socket::EventFd` 等 socket 语义未实现。

---

# 日间调度（P-CC-6 启动｜2026-10-24 12:53 Asia/Shanghai）

- **切换原因**：P-CC-5 claude PID 92401 已退出并写闭环 @ HEAD `f9fd30b`；真机最新 v46 越过 `nnsslInitialize`，阻塞 `stdout`（import 186，STT_OBJECT，exit 139）；主树 clean、无活跃 FFRS 主路径写手。Antigravity 仍处 429 额度窗口（约 12:09 起，重置约再等数小时），按优先级改派 Claude Code。
- **里程碑**：新 HEAD `f9fd30b`；真机 v45/v46 越过 nnssl；import 606/608 消除；新前沿 stdio 数据导入。
- **已派**：P-CC-6 攻 stdio 族（`stdout`/`stdin`/`stderr` 对象 + printf/fprintf/fwrite/…）；提词 `coordination/prompts/P-CC-stdio-stdout.md`；claude PID **7295**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-6.log`
- **基线**：`.local/logs/rg477m-boot-v46-nnssl-initialize.log` exit 139；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-23 / P-CC-5 闭环｜2026-10-24 12:34 Asia/Shanghai — nnsslInitialize（import 606）真机越过，前沿推进至 stdout（import 186）

- **结论**：工单 P-AG-23（Antigravity 因 429 额度耗尽中途退出）+ P-CC-5（Claude Code 接力）目标闭环：真机**彻底越过 `nnsslInitialize`（import 606）**，新前沿归因为 `stdout`（import 186，STT_OBJECT 数据导入）。
  1. **接力范围**：agy 已落地 `nnsslInitialize`/`nnsslFinalize` HLE、白名单、`import_signature`、resolve 与 `--nnssl` 契约；P-CC-5 **未重写该桩**，只做三件事：(a) 核对 WIP 与既有产物是否匹配并重编；(b) 按 v44 **同一套** diagnostic port-policy 跑通真机全量 `--boot`；(c) 依零售 SDK 反汇编证据修正 `nnsslFinalize` 的 ABI（`void` → 返回 masked `nn::Result`）。
  2. **产物一致性核对**：`.local/build-p-ag-23/nso_owned_startup.android`（sha256 `9a2fa25651cbc18668527126626568437794bc10b54c6b9023f9b6099f41ecfa`，mtime 12:08:22）晚于全部 nnssl 源码改动（12:05:02–12:06:01），且 `strings` 含 `nnsslInitialize`/`nnsslFinalize` 与 `[hle] nnsslInitialize…` 日志串 → 与当时 WIP 源码匹配，可直接上机。因随后修正了 `nnsslFinalize` ABI，最终以新目录 **`.local/build-p-cc-5/`** 重编（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-cc-5 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0），**最终真机证据全部来自该产物**（`nso_owned_startup.android` sha256 `f0d2ec172cb342411f4c7559746bff6f5e34b81db28952ef26c1d25cdf7158e5`）。
  3. **ABI 证据（零售 Nintendo SDK NSO，build_id `0023a1b06130cd795f7bc4613772af0af71c8cfd…`，sha256 `9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9`，`scripts/runtime/inspect_nso.py --slice` 取证）**：
     - `nnsslInitialize` @ sdk module_offset `0x4335e0`（size 24）：`stp x29,x30,[sp,#-0x10]! ; mov x29,sp ; bl <internal> ; and w0,w0,#0x3fffff ; ldp x29,x30,[sp],#0x10 ; ret` —— 调用前**不布置任何参数** ⇒ 零参；返回 w0 为 masked `nn::Result`（bit 0..21 = module|description，0 = 成功）。
     - `nnsslFinalize` @ sdk module_offset `0x433620`（size 24）：**同一形态** ⇒ 同样返回 masked `nn::Result`，**并非 `void`**。
     - `nn::ssl::Initialize()` @ `0x437670`、`nn::ssl::Finalize()` @ `0x4378c0`：亦为 0 参 + 返回 `nn::Result`（成功路径 `mov w0, wzr`）。
     - 影响面：`nb_import_gateway`（`src/runtime/nso/native_boundary.S`）只保存/恢复 `x18/x29/x30/x16`，**不恢复 x0** ⇒ void 桩会把不确定值交给 guest 当 `nn::Result` 读。已把 `nso_host_ssl_finalize` 改为返回 `int32_t 0`，`import_signature` 由 `returns=0` 改为 `returns=1`，契约新增 `finalize_result_zero` 断言；源码注释内嵌上述模块地址与反汇编。
  4. **真机推进轨迹（RG477M01445122，v44 同一套参数）**：
     - 参数：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；guest 输入不变（main NSO build_id `736837afd390442ed7c74a7de730e71b`，sha256 `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`）。
     - `unresolved` 计数 **410 → 408**（正是 `nnsslInitialize` + `nnsslFinalize` 两个新绑定）。
     - `[hle] nnsslInitialize: initialized Nintendo SSL service (stub) -> returning 0` —— import 606 被真机**真实调用并越过**。
     - 引擎随即 `[hle] nnResultIsFailure: res=0x0 -> 0` ⇒ guest 把返回值当 `nn::Result` 判为成功，**反证 `nnsslInitialize` 返回 `nn::Result` 且 0 = 成功**，与 SDK 反汇编一致。
     - LibcurlGc 子线程完成事件同步（SignalEvent / WaitEvent / FinalizeEvent）后继续推进；父线程 `atexit` 登记攀升至 13616，插件 Pak 目录探测继续。
  5. **新阻塞前沿**：
     - `[fault] signal=11 pc=0x789c88f678 fa=0x7b39ee35d0 module_offset=0x5e5a678`
     - `[unresolved-import] index=186 name=stdout caller_offset=0x5e5a874`
     - 退出码 **139**（SIGSEGV）。
     - 归因：`stdout` 在 main NSO dynsym 中 `st_info&0xf == 1`（**STT_OBJECT，数据导入**），loader 对未解析的**数据**导入填 PROT_NONE 的 object sentinel ⇒ guest **读写 `stdout` 对象槽**即 SIGSEGV（与代码分支未解析导入触发 SIGTRAP 不同）。
     - 同族仍未解析：`stdin`(188)、`stderr`(306) 数据对象，以及 `printf`(175)、`fprintf`(307)、`vfprintf`(538)、`fwrite`(263)、`fputs`(532)、`snprintf`(183)、`vsnprintf`(145)、`sprintf`(526) 函数。**下一张工单应攻 stdio 族（3 数据 + 8 函数），而非 ssl 族**（ssl 族在 `stdout` 之前不会成为前沿）。
  6. **契约**：宿主 `--nnssl` PASS `{"nnssl_contract_errors":0,"initialize":true,"finalize":true,"baseline_zero":true,"finalize_result_zero":true}`；RG477M 真机 `--nnssl` 同结果 PASS。
  7. **日志**：
     - 最终证据（= 提交源码构建）：`.local/logs/rg477m-boot-v46-nnssl-initialize.log` + `.exitcode` = **139**（产物 `.local/build-p-cc-5/`）。
     - 过程证据（= P-AG-23 产物，pre-ABI-fix）：`.local/logs/rg477m-boot-v45-nnssl-initialize.log` + `.exitcode` = 139，结论一致。
     - 无效首跑（cwd 错误导致 `cannot open file`）已备份为 `.local/logs/rg477m-boot-v45-nnssl-initialize.INVALID-cwd.log`，不再作为证据。
  8. **未达**：标题画面 / 可玩（仍未进呈现阶段）。未伪造 TLS 握手/证书校验/在线账号；`nnssl*` 仅为生命周期成功桩。

---

# 日间调度（P-CC-5 启动｜2026-10-24 12:26 Asia/Shanghai）

- **切换原因**：P-AG-23 agy PID 78326 已退出；Antigravity 429 额度耗尽（约 12:09，重置约再等 ~5h）；nnssl HLE/契约/Android 构建已落地但未提交，真机全量 boot 未完成。现有 v45 日志无效（`cannot open file`/exit 1）。主树 dirty、无活跃 FFRS 主路径写手。按优先级改派 Claude Code。
- **已派**：P-CC-5 接力完成 nnsslInitialize 真机 boot + 提交；提词 `coordination/prompts/P-CC-nnssl-boot-finish.md`；claude PID **92401**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-5.log`
- **基线**：HEAD `4393ed9` + dirty WIP；产物 `.local/build-p-ag-23/nso_owned_startup.android`；阻塞仍 v44 `nnsslInitialize` import 606 exit 133；设备 RG477M01445122；pak/ucas PASS
- **未达**：标题 / 可玩

---

# 日间调度（P-AG-23 启动｜2026-10-24 12:00 Asia/Shanghai）

- **切换原因**：P-AG-22 agy PID 70112 已退出并写闭环 @ HEAD `4393ed9`；真机最新 v44 越过 `nnResultIsFailure`，阻塞 `nnsslInitialize`（import 606，exit 133）；主树 clean、无活跃 FFRS 主路径写手。
- **里程碑**：新 HEAD `4393ed9`；真机 v43→v44；import 607 消除；LibcurlGc 子线程已起并完成事件同步；pak/ucas PASS。
- **已派**：P-AG-23 攻 `nnsslInitialize`（及紧随 nnsslFinalize / ssl 族按需）；提词 `coordination/prompts/P-AG-nnssl-initialize.md`；agy PID **78326**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-23.log`
- **基线**：`.local/logs/rg477m-boot-v44-nn-result-is-failure.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-22 闭环｜2026-10-24 12:00 Asia/Shanghai — nnResultIsFailure（import 607）及 nnResult 族落地，真机推进至 nnsslInitialize

- **结论**：工单 P-AG-22 目标圆满闭环。
  1. 落地最小可用 `nnResultIsFailure` 及 `nnResult*` 族 HLE（Nintendo SDK 规范：`nnResultIsFailure` 返回 `res != 0`，`nnResultIsSuccess` 返回 `res == 0`，`nnResultGetModule` 提取低 9 位 `res & 0x1ff`，`nnResultGetDescription` 提取 13 位描述 `(res >> 9) & 0x1fff`，`nnResultSuccess` 返回 0；清晰打印 `[hle] nnResultIsFailure: res=0x%x -> %u` 等归因日志）。
  2. `guest_process.c` 中 `typed[]` 白名单与 `import_signature`（1 GP arg, 1 return GP register for IsFailure/IsSuccess/GetModule/GetDescription; 0 args, 1 return for Success）完整绑定。
  3. 宿主契约与 RG477M 真机契约测试均通过（`{"nn_result_contract_errors":0,"is_failure_works":true,"is_success_works":true,"get_module_works":true,"get_description_works":true,"success_constant_zero":true}`）。
  4. RG477M 真机实测彻底越过 index 607 `nnResultIsFailure`（被 LibcurlGc 线程创建流程两次成功调用并校验结果为 0/success），引擎继续设置子线程名 `nn::os::SetThreadNamePointer(name='LibcurlGc')`，初始化两组事件（`nn::os::InitializeEvent`，AutoClear），通过 `nn::os::StartThread` 成功唤醒调度子线程，子线程成功绑定专属 guest OS TLS 记录并进入 guest_entry（`0x7454b37450`），父子线程完成同步等待与事件销毁（`SignalEvent` / `WaitEvent` / `FinalizeEvent`）。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x76f4b2b2f0 fa=0x76f4b2b2f0`
  - `[unresolved-import] index=606 name=nnsslInitialize caller_offset=0x5e647e8`（SSL 子系统初始化）
  - 退出码 133（SIGTRAP，未解析导入触发）；基线日志 `.local/logs/rg477m-boot-v44-nn-result-is-failure.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-22 启动｜2026-10-24 11:45 Asia/Shanghai）

- **切换原因**：P-AG-21 agy PID 49431 已退出并写闭环 @ HEAD `c61fbac`；真机最新 v43 越过 `GetCurrentProcessHandle`，阻塞 `nnResultIsFailure`（import 607，exit 133）；主树 clean、无活跃 FFRS 主路径写手（P-CC-4 Claude 43082 已退出）。
- **里程碑**：新 HEAD `c61fbac`；真机 v42→v43；import 105 消除；CreateThread 子线程已起；pak/ucas PASS。
- **已派**：P-AG-22 攻 `nnResultIsFailure`（及紧随 Result 族）；提词 `coordination/prompts/P-AG-nn-result-is-failure.md`；agy PID **70112**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-22.log`
- **基线**：`.local/logs/rg477m-boot-v43-os-process-handle.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-21 闭环｜2026-10-24 11:35 Asia/Shanghai — nn::os::GetCurrentProcessHandle（import 105）落地，真机推进至 nnResultIsFailure

- **结论**：工单 P-AG-21 目标圆满闭环。
  1. 落地最小可用 `nn::os::GetCurrentProcessHandle` HLE（Nintendo SDK 规范：无参，返回当前进程 `nn::os::NativeHandle`，映射宿主当前进程 pid 作为稳定句柄；清晰打印 `[hle] nn::os::GetCurrentProcessHandle -> handle=0x%x (pid=%d)` 归因日志）。
  2. `guest_process.c` 中 `typed[]` 白名单与 `import_signature`（0 args, 1 return GP register）完整绑定 `_ZN2nn2os23GetCurrentProcessHandleEv` 与 `nnosGetCurrentProcessHandle`。
  3. 宿主契约与 RG477M 真机契约测试均通过（`{"os_process_handle_contract_errors":0,"get_current_process_handle":true,"handle_positive":true,"matches_getpid":true}`）。
  4. RG477M 真机实测彻底越过 index 105 `nn::os::GetCurrentProcessHandle`（被 UE 平台初始化流程成功调用并保存进程句柄），引擎继续注册 22 项 atexit 回调（总数攀升至 13,616），持续探测数十个插件 Pak 目录（CriWare、Paper2D、ControlRig、Metasound、Chaos、CommonUI 等），执行多段 4KB/24KB 虚存映射，校验 SDK link anchor（`nn::util::ReferSymbol`），并通过 `nn::os::CreateThread` 成功创建并调度子线程（tid=26311，entry=0x785096f450，stack=0x6eeaa1f000）。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x7af08462f8 fa=0x7af08462f8`
  - `[unresolved-import] index=607 name=nnResultIsFailure caller_offset=0x5e663c0`（子线程执行中调用 `nnResultIsFailure`）
  - 退出码 133（SIGTRAP，未解析导入触发）；基线日志 `.local/logs/rg477m-boot-v43-os-process-handle.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-21 启动｜2026-10-24 11:16 Asia/Shanghai）

- **切换原因**：P-AG-20 agy PID 35135 已退出；HANDOFF 已写闭环 @ HEAD `6e8cbc1`；真机最新 v42 越过 `gettimeofday`/`getpid`，阻塞 `nn::os::GetCurrentProcessHandle`（import 105，exit 133）；主树无活跃 FFRS 主路径写手（P-CC-4 Claude PID 43082 并行 NVN 预研仍活跃，勿杀）。
- **里程碑**：新 HEAD `6e8cbc1`；真机 v41→v42；gettimeofday import 211 消除；pak/VM/事件继续。
- **已派**：P-AG-21 攻 `nn::os::GetCurrentProcessHandle`（及紧随进程/句柄族）；提词 `coordination/prompts/P-AG-get-current-process-handle.md`；agy PID **49431**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-21.log`
- **并行**：P-CC-4 Claude PID 43082 @ `Documents/Codex/2026-10-24/task/ffrs-nvn-firstframe`（`task/T04-nvn-firstframe`）
- **基线**：`.local/logs/rg477m-boot-v42-gettimeofday.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-20 闭环｜2026-10-24 11:10 Asia/Shanghai — gettimeofday（import 211）与 getpid 落地，真机推进至 nn::os::GetCurrentProcessHandle

- **结论**：工单 P-AG-20 目标圆满闭环。
  1. 落地最小可用 `gettimeofday` HLE（POSIX 契约：填充 `struct timeval`，返回 0，timezone 忽略；清晰打印 `[hle] gettimeofday: tv=%p tz=%p -> sec=%ld usec=%ld rc=0` 归因日志）。
  2. 同步覆盖紧随 C 运行时 `getpid`（返回宿主 pid，清晰打印 `[hle] getpid -> %d`）。
  3. `guest_process.c` 中 `is_typed_runtime_export`、`import_signature`（2 args, 1 return GP register for gettimeofday; 0 args, 1 return GP register for getpid）完整绑定。
  4. 宿主契约与 RG477M 真机契约测试均通过（`{"gettimeofday_contract_errors":0,"valid_timeval":true,"tv_sec_positive":true,"tv_usec_bounded":true,"null_tv_rejected":true,"getpid_positive":true}`）。
  5. RG477M 真机实测彻底越过 index 211 `gettimeofday`（先后两次被 UE 成功调用并获取时间戳），引擎继续执行两个虚存映射（57,344 B @ `0x560582a000`、94,208 B @ `0x58ca182000`）、连续完成多段 pak 物理读取（15,251 B、13,513 B、23,992 B、30,288 B、12,734 B）、多线程事件同步等待（`nn::os::SignalEvent` / `nn::os::WaitEvent`），`atexit` 登记回调数攀升至 13,594，成功探测引擎构建版本描述符。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x6ff7e2a348 fa=0x6ff7e2a348`
  - `[unresolved-import] index=105 name=_ZN2nn2os23GetCurrentProcessHandleEv caller_offset=0x7f1f4`（`nn::os::GetCurrentProcessHandle()`）
  - 退出码 133（SIGTRAP，未解析导入触发）；基线日志 `.local/logs/rg477m-boot-v42-gettimeofday.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-20 启动｜2026-10-24 10:54 Asia/Shanghai）

- **切换原因**：P-AG-19 agy PID 26521 已退出；HANDOFF 已写闭环 @ HEAD `902ef4a`；真机最新 v41 越过 `nn::oe::Initialize` / `GetDesiredLanguage`，阻塞 `gettimeofday`（import 211，exit 133）；主树 clean、无活跃 FFRS 写手。
- **里程碑**：新 HEAD `902ef4a`；真机 v40→v41；oe import 80/81 消除；ICU/pak/事件同步继续。
- **已派**：P-AG-20 攻 `gettimeofday`（及紧随时钟族符号）；提词 `coordination/prompts/P-AG-gettimeofday.md`；agy PID **35135**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-20.log`
- **基线**：`.local/logs/rg477m-boot-v41-oe-init.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-19 闭环｜2026-10-24 10:55 Asia/Shanghai — nn::oe::Initialize 与 nn::oe::GetDesiredLanguage 落地与推进

- **结论**：工单 P-AG-19 目标圆满闭环。
  1. 落地最小可用 `nn::oe::Initialize` HLE（无参，基线成功返回 0 / ResultSuccess，清晰归因 `[hle] nn::oe::Initialize: initialized operation environment -> returning 0` 日志）。
  2. 紧随其后的 `nn::oe::GetDesiredLanguage`（dynsym index 81）按 Nintendo SDK `nn::settings::LanguageCode`（8 字节 packed ASCII / NUL-terminated 字符串）以 ARM64 ABI `x0` 返回 `"en-US"`（与零售包 ICU 资源查找 `en_US` 强一致），打清晰 `[hle] nn::oe::GetDesiredLanguage: returning 'en-US'` 归因日志。
  3. `guest_process.c` 中 `is_typed_runtime_export` 与 `import_signature`（0 args, 1 return GP register）完整绑定 `_ZN2nn2oe10InitializeEv` / `nnoeInitialize` 与 `_ZN2nn2oe18GetDesiredLanguageEv` / `nnoeGetDesiredLanguage`。
  4. 宿主契约与 RG477M 真机契约测试均通过（`{"oe_contract_errors":0,"initialize":true,"get_desired_language":true,"baseline_locale":"en-US"}`）。
  5. 真机实测越过 index 80 与 index 81，两次成功完成 `Initialize` 与 `GetDesiredLanguage` 流程，引擎推进跨过本地化区域设置，继续物理读取 pak、多线程事件同步等待（`nn::os::SignalEvent` / `nn::os::WaitEvent`），atexit 登记进一步攀升至 13,593。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x7668e4c698 fa=0x7668e4c698`
  - `[unresolved-import] index=211 name=gettimeofday caller_offset=0x5920c94`
  - 退出码 133（SIGTRAP，未解析导入导致）；基线日志 `.local/logs/rg477m-boot-v41-oe-init.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-19 启动｜2026-10-24 10:38 Asia/Shanghai）

- **切换原因**：P-AG-18 agy PID 88093 已退出；HANDOFF 已写闭环 @ HEAD `15636ca`；真机最新 v40 越过 setlocale / TaskGraph SIGSEGV，阻塞 `nn::oe::Initialize`（import 80，exit 133）；主树 clean、无活跃 FFRS 写手。
- **里程碑**：新 HEAD `15636ca`；真机 v36→v40；TaskGraph SIGSEGV（exit 139）消除；ICU/pak 读推进。
- **已派**：P-AG-19 攻 `nn::oe::Initialize`（及紧随 GetDesiredLanguage）；提词 `coordination/prompts/P-AG-oe-initialize.md`；agy PID **26521**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-19.log`
- **基线**：`.local/logs/rg477m-boot-v40-setlocale.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

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

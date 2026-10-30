# 工单 P-MM-2｜MiniMax Code｜主路径：绑定 `movie::SetAllocator`（import 74）越过 v52 新 SIGTRAP

> **状态**：正式派工（已由总控从侦察草稿 `coordination/prompts/P-MM-2-DRAFT-movie-setallocator.md` 升格并**更正证据行号**）。
> 侦察者：只读侦察 Agent B｜升格：总控（MiniMax Code）｜时间：2026-10-24 14:2X Asia/Shanghai
> 基线：`work/rg477m-native` @ **`5423dac`**（"fix(runtime): allow memory/resource snapshots from guest worker threads"）
> 证据日志：`.local/logs/rg477m-boot-v52-snapshot-thread.log`（exit **133**，产物 `.local/build-p-mm-1/`）
> **完整证据附录**：`coordination/prompts/P-MM-2-DRAFT-movie-setallocator.md`（29 个 `movie::` 导入全表、调用点普查、ABI 往返实测、边界取证）

---

## 0. 仓库与环境（必须）

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**（HEAD `5423dac`，工作树 clean）
- 设备：`adb -s RG477M01445122`（先 `adb devices`）；不 root / 不刷机 / 不清数据；**不 push 远端**
- 先读（按序）：`AGENTS.md` → `coordination/HANDOFF.md` 顶部 **P-MM-1 闭环**段 → 本工单 → 证据附录 DRAFT
- 产物目录：`.local/build-p-mm-2/`；新 boot 日志：`.local/logs/rg477m-boot-v53-movie-setallocator.log` + `.exitcode`
- `.local/romfs/**` 只读，勿删；`task/T04-nvn-firstframe` / `.claude/worktrees/*` **勿改**
- **不要调用 agy**（Antigravity 仍处 429 额度窗口）
- 成功后可 `git commit` 检查点；**不 push**；密钥 / `.pak` / `.ucas` 不进 git
- **真机 boot 锁**：开始 boot 前 `touch .local/agent-pm/BOOT_LOCK_MM_A`，**结束后必须删掉**
- 验收不降级；**禁止声称标题/可玩**；做完停下写 HANDOFF，不要 ask_user

---

## 1. 问题证据（已由总控复核，行号已更正为 **`5423dac` + `.local/build-p-mm-1/` 产物**）

`.local/logs/rg477m-boot-v52-snapshot-thread.log`（共 **52115** 行）：

| 行号 | 内容 | 含义 |
|---|---|---|
| `5` | `[profile] ... build=736837afd390442ed7c74a7de730e71b imports=651 unresolved=390; CRT/TLS not initialized` | 加载期口径，见 §2 |
| `52093` | `[snapshot-thread-policy] caller=bound-worker registered_threads=18 quota=1073741824 charged=748843008 stack_bytes=2097152; same primary-owned ledger read under the process mutex` | **上一单已越过**（工作线程成功取只读快照） |
| `52094` | `[va-resource-policy] total=16777216 raw_metadata_used=3288320 blocks=12845 reported_used=3812608 SDK_margin=0x80000; ...` | 工作线程侧资源查询成功 |
| `52101` | `[fault] signal=5 pc=0x7a0451b250 fa=0x7a0451b250` | **SIGTRAP**，`pc == fa` = 跳到 sentinel 页上的 BRK |
| `52102` | `[unresolved-import] index=74 name=_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_ caller_offset=0x14990` | **精确符号 + 精确调用点** |
| `.exitcode` | `EXIT=133` | 128 + 5 = SIGTRAP |

对照 v51（exit 134）的 `51930 [snapshot-refused] reason=primary-thread` / `51931 [va-resource-invalid]` / `51932 Aborted` —— **v52 中这三条全部消失**，阻塞点已前移。

> ⚠️ **DRAFT 附录里的 v52 行号（51932/51933/51939/51940、总 51953 行、`pc=0x7f5df5a250`）是旧 P-CC-8 产物那份日志的**，已被本单 `.local/build-p-mm-1/` 的复跑覆盖（该旧日志现留存为 `.local/logs/rg477m-boot-v52-snapshot-thread.P-CC-8-build.log`）。**以本工单 §1 的行号为准。** DRAFT 里的**源码行号**（`guest_process.c:1113/1240/1244/1448/1487/1520`、`nso_host.c:577/588/2952`、`nso_loader.c:260/529`）已按 `5423dac` 复核，**有效**。

**符号身份**（`evidence/coordinator/nso_imports_main.tsv`）：
```
74 _ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_	movie::SetAllocator(void* (*)(unsigned long, unsigned long, void*), void (*)(void*, void*), void* (*)(void*, unsigned long, void*), void*)
```
guest 侧模块 = **`main` NSO**，`build_id 736837afd390442ed7c74a7de730e71b`。

**ABI（clang 往返实测复现，非推测）**：4 个通用寄存器参数、**无返回值**。
```c
namespace movie {
void SetAllocator(void* (*)(unsigned long, unsigned long, void*),  // x0 alloc
                  void  (*)(void*, void*),                          // x1 free
                  void* (*)(void*, unsigned long, void*),           // x2 realloc
                  void*);                                          // x3 userdata
}
```
`clang++` 反向构造并用 `nm` 比对，**逐字节复现**同一 mangled 名。

**调用点（已穷举，2 处）**：`0x1498c`（本次崩溃点，LR = `0x14990` = BL 地址 + 4，自洽）与 `0x46aab8c`。两处传入**完全相同**的三个 guest 自有函数指针（module_offset `0x1c4b0` / `0x1c4c0` / `0x13340`，均在 `main` 的 .text 内）+ `x3 = NULL`。

---

## 2. 口径修正（排单必读，务必理解）

**「unresolved-import 计数 = 0」≠「全部导入已绑定」。**

v52 第 5 行显示 `imports=651 unresolved=390` —— **651 个导入里还有 390 个未绑定**。P-CC-7 闭环说的「unresolved-import 计数 = 0」指的是**真机 boot 过程中没有因未解析导入而产生 fault**（没有 `[unresolved-import]` 行、没有 `[fault] signal=5`），**不是**全部导入都已绑定。

机制（`src/runtime/nso/nso_loader.c`）：
- `:263-270` loader 调 `resolve(i, name)`；**返回 0 ⇒ `m->unresolved_count++`**，导入槽改指 `m->import_traps + i*8`；该页每 8 字节写入 `brk` = `0xd4200000`（`:260-261`）⇒ guest 一调用就 SIGTRAP；`:529` 打印 `[unresolved-import]`。
- 未绑定导入总数 390，其中**有 BL 调用点的 341 个**（48 个在本二进制零调用点）。

**排单含义**：未绑定面很大，逐个修会非常慢。唯一可靠的推进方式是 **「修一个 → 跑一次真机 → 看下一个」**。调用点密度 ≠ 执行顺序。

`movie::` 命名空间在 `main` NSO 共 **29 个导入**（已绑定 2：`idx 391 movie::GetThreadPriority` / `idx 572 movie::SetThreadPriority`），**未绑定 27 个** —— 全表见 DRAFT 附录 §3.2。

---

## 3. 根因定位（绑定链三层，缺一不可）

1. **`src/runtime/nso/nso_loader.c:263-270`** —— resolve 返回 0 ⇒ unresolved ⇒ BRK sentinel。
2. **`src/runtime/hle/guest_process.c` `guest_process_resolve()`**
   - `:1113` `static const char *const typed[]` 白名单里**没有**本符号（`movie::` 只列了 idx 391/572，见 `:1197-1198`）
   - `:1240` `for(size_t i=0;typed[i];i++)if(!strcmp(name,typed[i]))return nso_host_resolve(index,name);` —— 白名单不命中就 `return 0`
   - ⇒ **第一处必改点**
3. **`src/runtime/hle/nso_host.c` `nso_host_resolve()`**
   - `:2952-2955` 只有 `movie::GetThreadPriority` / `movie::SetThreadPriority` 两条 `movie::` 绑定
   - ⇒ **第二处必改点**（桩实现在 `:577-586` `nso_host_movie_*` 旁）
4. **签名门禁（第三处，最容易漏）**：`guest_process.c:1244` `import_signature()` → `:1487` `found:` 标签构造 `nb_signature`。**返回 0 ⇒ `prepare_boundary_locked()` 在 `:1520` 打印 `[boundary-unsupported] index=… name=…` 并返回 `NSO_UNSUPPORTED`，整个 boundary 不建立 ⇒ 根本不会 boot。**
   - `:1300` 的 `one[]` 表（`arguments=0,returns=1`）里也没有本符号
   - ⇒ **第三处必改点**。本符号 **`arguments=4; returns=0;`**
   - 对照既有写法 `guest_process.c:1448` `_ZN2nn2os32SetMemoryAllocatorForThreadLocal… {arguments=2;returns=0;}` —— 同为「注册分配器」语义，**可直接照抄该模式**

---

## 4. 候选改动文件表

| 路径 | 改什么 | 必改 / 可能需碰 |
|---|---|---|
| `src/runtime/hle/nso_host.c` | ① 在 `:577-586` `nso_host_movie_get/set_thread_priority` 旁新增 `nso_host_movie_set_allocator(alloc, free, realloc, userdata)`；② 在 `:2952-2955` 附近加 `strcmp` 绑定分支 | **必改**（本文件当前未被 P-MM-1 改动，无冲突） |
| `src/runtime/hle/guest_process.c` | ① `:1113` `typed[]` 加本符号 mangled 名；② `:1244` `import_signature()` 加 `arguments=4; returns=0;` 分支 | **必改**（P-MM-1 已闭环交还，可直接改） |
| `src/runtime/hle/guest_process.h` | 若桩需导出声明（供契约引用）则加 | 可能需碰 |
| `tests/native/guest_process_contract.c` | 加契约：resolve 本符号返回非 0；`import_signature` 返回 1 且 `gp_arguments==4 && gp_returns==0` | **必改**（防止「只加白名单忘签名」） |
| `scripts/runtime/build_guest_process_contract.sh` | 接入新契约用例 | 可能需碰 |
| `coordination/HANDOFF.md` / `STATUS.md` / `state.json` | **总控写**（AGENTS.md 规定唯一写者） | 总控写 |

> **写锁状态**：`guest_process.c` / `guest_process_contract.c` / `build_guest_process_contract.sh` 的占用已由 P-MM-1（Agent A，commit `5423dac`）**交还**。本单是这些文件的**唯一写者**，可自由修改。

---

## 5. 实现方向（已拍板，勿再空转讨论）

### 5.1 结论：**采用「如实记录并转发 allocator 注册」**，不是「可跳过」

桩必须把 guest 传入的三个分配器回调 + userdata **原样存进宿主状态**、打一条 `[hle]` 日志、并保留可被后续 `movie::` 桩复用的读取入口。**不是空实现、不是直接 return、不是静默丢弃。**

**观测（硬证据）**
1. 两处调用点传入的是 guest 自己模块内的三个函数指针 + `x3 = NULL` —— 语义即「guest 把自己的 malloc/free/realloc 注册给影片库」。
2. 真实实现**定义在 `subsdk0` NSO**（`0x129630`，size 4，`B → 0x3489c0`），而 **`subsdk0` 在本运行时根本没被加载**（`rg 'subsdk0' src/` 无命中）⇒ **没有真实影片库会来读回这个注册**。
3. `main` 的 29 个 `movie::` 导入里**没有** getter ⇒ guest 无法从 `movie::` 侧读回自己注册的东西。
4. 仓库已有**同语义先例**：`nn::fs::SetAllocator` → `nso_host_fs_set_allocator`（`nso_host.c:588`）＝「存下 alloc/free 指针 + 打 `[hle]` 日志 + 提供 getter」，且 `guest_process.c:1448` 给它登记了 `arguments=2;returns=0;`。

**推断（标注为推断）**
5. 本桩唯一可观测作用是「不要 SIGTRAP、不要破坏 boot」；**记录是为了忠实反映 guest 意图，并为后续 `movie::` 簇（尤其会真的分配内存的 idx 420 `Buffer::SetDataAndCapacity`、idx 393/400/403 等）保留正确的分配器**。
6. **决定性理由**：跳过 = 悄悄丢弃 guest 的一次真实状态变更，将来 `movie::` 簇实现时会出现「分配器来源不一致」的隐性错误且无日志可提示；记录 = 一次日志 + 4 个指针，代价极低、完全可回退。**取舍理由：记录成本 = 一条日志 + 4 个指针，收益 = 保留 guest 意图与后续可追溯性；跳过成本 = 将来排障无法区分「guest 没注册」与「我们丢了注册」。**

### 5.2 实现要点

1. **照抄 `nso_host_fs_set_allocator` 的形态**：静态指针 + userdata 静态量；桩体内
   `fprintf(stderr,"[hle] movie::SetAllocator: alloc=%p free=%p realloc=%p userdata=%p\n",…)`；
   日志风格与既有 `[hle] nn::fs::SetAllocator: alloc=%p free=%p` 保持一致，便于 grep 对比。
2. **签名严格按 §1 的 4 参 / void 返回，顺序不可换**。桩体内**只记录、不调用**这些 guest 函数指针 —— 调用 guest 回调需要正确的 guest TLS/栈上下文，在 host 桩里直接调用会破坏 guest 线程状态。
3. **resolve 表登记**：`nso_host.c` 的 `movie::` 段照 `movie::SetThreadPriority` 加一条 `strcmp`；**同时**在 `guest_process.c` 的 `typed[]` 白名单加同名条目（**白名单是硬门禁，漏了等于没改**）。
4. **签名门禁登记**：`import_signature()` 加 `arguments=4; returns=0;`（无返回值 ⇒ `returns=0`）。
5. **契约测试至少两条断言**：(a) `guest_process_resolve(74, mangled)` 返回非 0；(b) `import_signature(mangled,…)` 返回 1 且 `gp_arguments==4 && gp_returns==0`。**(b) 是本单最易漏点，务必覆盖。**
6. **不要夹带其它符号**：本单只做 idx 74。其余 26 个未绑定 `movie::` 符号是否在 boot 期被触达**尚无任何观测证据**（其调用点集中在 `0x590d64c`–`0x5912034` 与 `0x5e143a8`–`0x5e17834` 两段影片播放路径区间）⇒ 无证据前一次性打包 27 个等于凭空猜 26 个 API 语义，代价高且大概率返工。**但桩形态要留扩展点**，能直接复用到 idx 571 `movie::SetCoreMask`、572 `movie::SetThreadPriority` 这一类同族 setter。

---

## 6. 验收

### 6.1 宿主契约（不碰设备）
```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-mm-2 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216
```
期望：exit 0；宿主全量契约 PASS；新用例覆盖 resolve 非 0 与 `gp_arguments=4 / gp_returns=0`；`manifest.json` 的 `source_sha256` 与工作树逐项一致（**无源码漂移**）。

### 6.2 离线取证复核（只读，可重跑）
```bash
python3 scripts/runtime/resolve_import_callers.py .local/uncompressed/main.bin --index 74
python3 scripts/runtime/inspect_nso.py .local/uncompressed/main.bin --out .local/scratch/p-mm-2/main --slice 0x14940:0xc0 --slice 0x46aab40:0xa0
```
期望：import 74 / plt `0x5e67f60` / callers = 2（`0x1498c`、`0x46aab8c`）。

### 6.3 真机 boot（参数沿用 v47/v51/v52 四元组）
```
quota=1073741824  stack=2097152  image=162869248  resource_quota=16777216
```
- 设备 `RG477M01445122`；日志 `.local/logs/rg477m-boot-v53-movie-setallocator.log` + `.exitcode`
- CLI 形如：`nso_owned_startup.android <main.nso> --guest-quota N --guest-stack N --guest-resource-quota N --boot`
  ⚠️ **选项必须在 `--boot` 之前**（`nso_call.c` 的解析顺序；P-MM-1 踩过一次，首跑无效已归档为 `.INVALID-cli-order.log`）
- 设备端二进制命名与推送方式照 P-CC-7 / P-MM-1 的做法；**cwd 必须正确**；无效首跑要改名归档，**不得当证据**
- 产物必须来自 `.local/build-p-mm-2/`

**验收 A（本单必过）**
- 日志中**不再**出现 `[unresolved-import] index=74` / `[fault] signal=5`（同一 pc）作为第一致命
- 出现 `[hle] movie::SetAllocator: alloc=… free=… realloc=… userdata=…` 的成功调用日志
- `unresolved` 计数从 390 降为 **389**（profile 行可自证：绑定 1 个导入 ⇒ unresolved -1）

**验收 B（能推则推）**
- 记录**新的**第一致命（下一个未绑定导入 / 新 signal / 新 abort）并写入 HANDOFF
- 用 `rg` 自证：
  ```bash
  rg -n 'unresolved-import|\[fault\]|Aborted|movie::SetAllocator|\[profile\]' .local/logs/rg477m-boot-v53-movie-setallocator.log | tail -30
  ```

### 6.4 自证
- 新契约**故意改坏一次**必须让契约变红（打印真实错误数 + 最终 `!errors` 断言，参考 P-MM-1 的做法：`.local/logs/p-mm-1-contract-selfproof.log` 两变体对照）。**禁止恒真断言 / 硬编码 0 错误数。**

---

## 7. 收尾

1. `coordination/HANDOFF.md` **顶部**写 **P-MM-2 闭环**段：HEAD、是否越过 import 74、新阻塞符号 + exitcode、日志路径、契约 JSON（宿主 + 真机）、产物路径与 sha256、改动文件清单
2. `python3 scripts/project_status.py --write`
3. `git add` 仅本单相关文件；**不要 `git add .`**；commit 建议：`feat(runtime): bind movie::SetAllocator; advance boot past import 74`
4. **停下**回报：HEAD / 是否越过 import 74 / 新阻塞一句话 + 日志路径 + exitcode / 改动文件清单

---

## 8. 禁止

不声称标题/可玩/已出画面；不空返回掩盖失败；不伪造影片库/解码器/页表；不删 `.local/romfs` 大文件；不 push、不 force、不动 `main`；不调用 agy；**不为过编译而注释掉关键校验**；不夹带 26 个未证实被触达的 `movie::` 符号；不碰 NVN worktree。

做完停下汇报，等项目经理下一派工。

# 工单 P-MM-1｜MiniMax Code｜主路径：内存/资源快照多线程策略（过 snapshot-refused → 继续 boot）

你是实现 Agent。上一单 **P-CC-7** 已闭环：HEAD **`3a9092e`**（分支 `work/rg477m-native`）。真机 **0 个 unresolved-import**；已起 GameThread / FHeartBeatThread / HttpManagerThread / IOThreadPool #0/#1 等。

**当前致命阻塞不是缺 import**，而是运行时故意拒绝工作线程取内存快照后 `abort()`。

## 0. 仓库与环境（必须）
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`
- 分支：`work/rg477m-native`（只动这个分支；**禁止**动 `main`、禁止 force-push、禁止 reset/clean/stash 覆盖别人 WIP）
- 设备：`adb -s RG477M01445122`（先 `adb devices`）；不 root / 不刷机 / 不清数据；不 push 远端 git
- 先读（按序）：
  1. `AGENTS.md`
  2. `coordination/HANDOFF.md`（顶部 **P-CC-7 闭环** 段）
  3. `coordination/STATUS.md` / `coordination/state.json`（phase 字段）
  4. 基线日志：`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`（及同名 `.exitcode`=**134**）
- 产物目录建议：`.local/build-p-mm-1/`
- 新 boot 日志建议：`.local/logs/rg477m-boot-v52-snapshot-thread.log` + `.exitcode`
- 大文件：`.local/romfs/FFRS/Content/Paks/` 已有 pak/ucas（约 8GiB+）——**可读，勿删，勿无故重推**
- 若存在 worktree `task/T04-nvn-firstframe` / `Documents/Codex/2026-10-24/task/ffrs-nvn-firstframe` / `.claude/worktrees/*`：**勿改**
- **不要调用 agy**（Antigravity 仍可能 429）；本单由 MiniMax Code 独立完成
- 成功后可 `git commit` 检查点；**不 push**；密钥 / `.pak` / `.ucas` 不进 git
- 验收不降级；**禁止声称标题/可玩**；做完停下写 HANDOFF，不要 ask_user

## 1. 问题证据（必须用日志复核，勿凭记忆）
在 v51 日志尾部应能看到类似链：
```
[snapshot-refused] reason=primary-thread; memory/resource answer withheld
[va-resource-invalid] status=1 native=22; no valid resource return
Aborted   # exit 134
```
根因定位（已在 HANDOFF 写明，请打开源码核对）：
- Guest **工作线程**（IOThreadPool 等）调用 `nn::os::GetVirtualAddressMemoryResourceUsage`
- `src/runtime/hle/guest_process.c` 内 `snapshot_locked()` 要求：
  1. `pthread_equal(p->thread.native, pthread_self())` —— 只许**主线程**（失败 reason=`primary-thread`）
  2. 随后还有 `tls.registered_threads!=1` —— 多线程注册后也会拒绝（reason=`os-tls-registered-threads`）
- 失败路径：`virtual_resource_usage()` / `query_memory_info()` 等对 `NSO_OK` 以外 **直接 abort()**

## 2. 策略决定（本单已拍板，按此实现，勿再空转讨论）
**目标语义**：在已持有 `p->mutex` 的前提下，**任意已绑定 guest OS TLS 的运行中线程**均可取只读内存/资源快照；底层 pages/tls/context 本就有锁或在 mutex 下读。

**明确不做**：
- 不伪造完整 Horizon 页表/RSS
- 不降低「模块身份 / pages ledger / charged 一致性」等数据完整性校验（那些 reason 仍应拒绝）
- 不把拒绝改成静默假成功；若仍拒绝必须保留 `[snapshot-refused] reason=…` 可归因日志
- 对 **本应成功** 的多线程查询：禁止再走到 `abort()`

推荐实现顺序（最小 diff）：
1. **放宽线程身份**：把 `primary-thread` 条件从「必须等于主 pthread」改为「当前线程是合法 guest 工作线程」——例如：
   - `p->thread.state==THREAD_RUNNING` 且（`pthread_equal(主线程)` **或** 当前线程在 `guest_os_tls` 已注册且未 quarantine）
   - 若现有 API 不够，在 `guest_os_tls_*` 增加「查询 self 是否已注册」的只读 helper（见下方文件清单）
2. **放宽 `registered_threads==1`**：多线程 boot 后该断言过时。改为：`registered_threads>=1` 且当前调用线程已注册；或按「快照只读聚合、不依赖唯一线程」调整校验，并保留日志字段 `registered_threads=`
3. **abort 策略**：`GetVirtualAddressMemoryResourceUsage` / 相关查询在「策略已允许多线程」后若仍 `NSO_INVALID`，优先修校验；仅对真正不可恢复保留 abort。可选：对明确的策略拒绝返回 SDK 可吞的失败码而不是 abort——但**优先让快照成功**，因为 UE 工作线程会反复查 VA 资源
4. 保持现有 `[snapshot-refused] reason=…` 诊断日志（P-CC-7 刚加的，别删）
5. 宿主契约：扩展/新增用例——**在非主线程**调用 resource/memory 查询应返回 OK（见 tests）

## 3. 主要改动文件 / 目录（按优先级）

### 必改
| 路径 | 做什么 |
|------|--------|
| `src/runtime/hle/guest_process.c` | 改 `snapshot_locked()` 线程与 `registered_threads` 策略；核对 `guest_process_memory_info` / `guest_process_resource_info` / `guest_process_va_enabled` / `virtual_resource_usage` / `query_memory_info`；必要时让工作线程路径可返回有效 `__uint128` 资源对而不 abort |
| `src/runtime/hle/guest_os_tls.c`（或同目录实际文件名，以仓库为准） | 若需「当前 pthread 是否已注册」查询，增加只读 API；**勿破坏**现有 TLS 隔离 |
| `src/runtime/hle/guest_os_tls.h` / `guest_process.h` | 声明新 helper（若有） |
| `tests/native/guest_process_contract.c` | 增加**非主线程**调用 `GetVirtualAddressMemoryResourceUsage` / `GetMemoryInfo`（或经 resolve 的符号）应成功的契约；Android/RG477M 同测若脚本已支持 |
| `scripts/runtime/build_guest_process_contract.sh` | 确保新契约编进 `.local/build-p-mm-1/`；宿主 + 真机 contract 都要跑 |

### 可能需碰（仅当链接/签名要求）
| 路径 | 何时 |
|------|------|
| `src/runtime/hle/nso_host.c` | 仅当 resolve/绑定表要补测试入口；本单一般**不必**新 import |
| `src/runtime/hle/*pages*` / `guest_pages*.c` | 仅当放宽线程后 ledger 断言误伤；优先读、少改 |
| `coordination/HANDOFF.md` | 写 **P-MM-1 闭环** 段 |
| `coordination/STATUS.md` / `coordination/state.json` | `python3 scripts/project_status.py --write` |

### 不要动
- `.local/romfs/**` 大资源
- `task/T04-*` / NVN 预研 worktree
- ProtectRelro 桩（除非硬阻塞且 HANDOFF 点名）
- 其他无关 HLE（socket/stdio/nnssl 已闭环，勿回滚）

## 4. 构建与真机验证（命令级）

### 4.1 契约
```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
# 按仓库既有脚本（与 P-CC-7 相同入口）产出到 .local/build-p-mm-1
bash scripts/runtime/build_guest_process_contract.sh
# 确认宿主 contract PASS；若脚本支持 device/RG477M contract，一并跑到 PASS
```

### 4.2 全量 boot（参数必须与 v47/v51 一致）
```text
quota=1073741824
stack=2097152
image=162869248
resource_quota=16777216
```
- 设备：`RG477M01445122`
- 日志：`.local/logs/rg477m-boot-v52-snapshot-thread.log` + `.exitcode`
- **验收 A（本单必过）**：
  - 日志中**不再**以 `[snapshot-refused] reason=primary-thread` 后立刻 `[va-resource-invalid]` + `Aborted` 作为第一致命
  - 允许仍有 `[snapshot-refused]`，但不得是「仅因工作线程身份」导致的 abort
  - 出现工作线程侧成功的资源/memory 查询日志更佳（例如既有 `[va-resource-policy]` / `[query-port-policy]`）
- **验收 B（能推则推）**：记录**新的**第一致命（下一 unresolved-import / 新 signal / 新 abort reason）；exitcode 写入 `.exitcode`
- 用 `rg` 自证：
  ```bash
  rg -n 'snapshot-refused|va-resource-invalid|unresolved-import|Aborted|fault' .local/logs/rg477m-boot-v52-snapshot-thread.log | tail -40
  ```

## 5. 提交与回报
1. 更新 `coordination/HANDOFF.md` 顶部：P-MM-1 闭环（HEAD、是否越过 snapshot primary-thread、新阻塞、日志路径、exitcode）
2. `python3 scripts/project_status.py --write`
3. `git add` 仅本单相关源码/测试/coordination；`git commit` 信息建议：
   `fix(runtime): allow memory/resource snapshots from guest worker threads`
4. **停下**，回报给项目经理：
   - HEAD
   - 是否越过 primary-thread snapshot abort
   - 新阻塞一句话 + 日志路径 + exitcode
   - 改动文件列表

## 6. 若本单提前完成且仍有额度：下一刀预告（可选，同一会话可继续，但须先提交 P-MM-1）
过快照后常见下一类（**先以真机日志为准，勿猜**）：
- 新的 `unresolved-import`（再写 typed HLE，模式同 P-CC-7）
- 图形/NVN 相关符号 → **不要在本主树大改 NVN**；开独立 worktree 或等项目经理派 `P-CC-nvn-*`
- 新的 abort/SIGSEGV → 先归因再改，保持 `[hle]`/`[snapshot-refused]` 风格可诊断日志

## 7. 禁止清单（再强调）
- 不声称标题/可玩/已出画面
- 不空返回掩盖失败
- 不下载未授权本体
- 不删 `.local/romfs` 大文件
- 不 push、不 force、不动 main
- 不调用 agy
- 不做「为过编译而注释掉关键校验」的烂补丁

做完停下汇报，等项目经理下一派工。

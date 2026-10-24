> ## ⛔ 本提词已作废 —— 请勿执行（SUPERSEDED / NEVER EXECUTED）
>
> - **状态**：**作废**。同一目标（`movie::SetAllocator`，import 74）已由 **MiniMax 写手**在正式工单 **P-MM-2** 下完成并提交：**commit `1c8545f`**（HEAD）。
> - **派工经过**：2026-10-24 14:36 Asia/Shanghai 日间调度以「MiniMax 空转 >15 分钟、`lsof` 0 句柄」为由改派本单给 Claude Code（PID **94924**）。该判定基于**陈旧观测**：MiniMax 当时正在 `.local/build-p-mm-2/` 与 `.local/selfproof/` 写盘（不在 `src/**`），故 `lsof` 未见主树句柄，但主树改动一直存在。
> - **实际结果**：PID 94924 **启动即退出**（`.local/agent-pm/cc-p-cc-9.log` 仅 `unrecognized_model` 一行），**未写入任何仓库文件** ⇒ **未发生实际双写**，P-MM-2 改动完整保留。
> - **归档原因**：本文件目标与已完成的 P-MM-2 重合，若留在 `coordination/prompts/` 中不带标记，后续 Agent 极易把它误认作「下一张待派工单」而重复改 `nso_host.c` / `guest_process.c`（本项目已因此发生 2 次写锁冲突）。
> - **权威证据**：`coordination/prompts/P-MM-2-movie-setallocator.md`、`P-MM-2-DRAFT-movie-setallocator.md`、`coordination/HANDOFF.md` 顶部 P-MM-2 闭环段、`.local/logs/rg477m-boot-v53-movie-setallocator.log`（exit 133）。
> - **当前下一刀**：`nn::oe::SetCpuBoostMode(nn::oe::CpuBoostMode)`（import 75），已在 `coordination/state.json` 登记为 `P-MM-3-NN-OE-SET-CPU-BOOST-MODE` / `PENDING_DISPATCH`。
> - **下方正文原样保留**，仅作历史留档。

---

# 工单 P-CC-9｜Claude Code｜主路径：绑定 `movie::SetAllocator`（import 74）越过 v52 SIGTRAP

你是实现 Agent。MiniMax 主推的 P-MM-2 工单已就绪，但 `.local/scratch/p-mm-2/` 自 14:10 后无实质写盘、MiniMax 未打开主树文件，日守按 >15 分钟空转规则改派本单。**完整证据与 ABI 以** `coordination/prompts/P-MM-2-movie-setallocator.md` **及** `P-MM-2-DRAFT-movie-setallocator.md` **为准；本提词是执行版摘要。**

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- 当前 HEAD：见启动时 `git rev-parse --short HEAD`（应为 `3ac7fc0` 或更新的 docs/调度提交；代码基线同 `5423dac` 的 snapshot 策略）
- 先读（按序）：`AGENTS.md` → `coordination/HANDOFF.md` 顶部三角色收口 + P-MM-1 闭环 → `coordination/prompts/P-MM-2-movie-setallocator.md` → 本提词
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；**不 push 远端**
- 产物：`.local/build-p-cc-9/`；新 boot 日志：`.local/logs/rg477m-boot-v53-movie-setallocator.log` + `.exitcode`
- `.local/romfs/**` 只读，勿删；`task/T04-nvn-firstframe` / `.claude/worktrees/*` **勿改**
- **不要调用 agy**（Antigravity 仍 429）
- 成功后可 `git commit` 检查点；密钥 / `.pak` / `.ucas` 不进 git
- **真机 boot 锁**：开始 boot 前 `touch .local/agent-pm/BOOT_LOCK_CC_9`，**结束后必须删掉**；若已有其他 `BOOT_LOCK_*` 先等其释放
- 验收不降级；**禁止声称标题/可玩**；做完停下写 HANDOFF，不要 ask_user
- 若发现 MiniMax 或其他写手正在改 `nso_host.c`/`guest_process.c`，**立即停下**并在 HANDOFF 注明冲突，勿双写

## 当前阻塞（证据）
- 基线日志：`.local/logs/rg477m-boot-v52-snapshot-thread.log`（exit **133**）
- 已越过：snapshot-refused / va-resource-invalid / Aborted（P-MM-1 @ `5423dac`）
- 致命：
  ```
  [fault] signal=5 pc=… fa=…
  [unresolved-import] index=74 name=_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_ caller_offset=0x14990
  ```
- 口径：`imports=651 unresolved=390` 仍成立；「unresolved-import 计数=0」≠全部导入已绑定。推进方式：**修一个 → 跑一次真机 → 看下一个**。

## ABI（已实测，勿猜）
```c
namespace movie {
void SetAllocator(void* (*)(unsigned long, unsigned long, void*),  // x0 alloc
                  void  (*)(void*, void*),                          // x1 free
                  void* (*)(void*, unsigned long, void*),           // x2 realloc
                  void*);                                          // x3 userdata
}
```
`arguments=4; returns=0;`。调用点 2 处（`0x1498c` 本次崩溃、`0x46aab8c`），传入相同三个 guest 函数指针 + userdata NULL。

## 必改三处（缺一不可）
1. **`src/runtime/hle/guest_process.c` `typed[]` 白名单**：加入本 mangled 名（现仅有 Get/SetThreadPriority）。
2. **`src/runtime/hle/nso_host.c`**：在 `nso_host_movie_*` 旁实现 `nso_host_movie_set_allocator(...)`，并在 `nso_host_resolve()` 绑定；语义参考 `nn::fs::SetAllocator`（保存回调，勿空实现导致后续 movie 立刻崩且不可归因）。
3. **`guest_process.c` `import_signature()`**：为本符号补 `{arguments=4; returns=0;}`（可照抄 `_ZN2nn2os32SetMemoryAllocatorForThreadLocal…` 模式）。**漏此项 → `[boundary-unsupported]`，根本不 boot。**

细节、对照行号、movie 族 29 导入全表见 P-MM-2 正式工单与 DRAFT 附录。

## 目标（按序）
1. 实现上述三处绑定 + 最小可归因日志（注册成功应可在 boot 日志看到，勿静默）。
2. 宿主契约：新增或扩展最小 `--movie-setallocator`（或等价）契约；宿主 PASS；有条件时 RG477M 同测。
3. 按 v52 **同一套** diagnostic 参数跑全量 `--boot`（`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216`）。新日志：`.local/logs/rg477m-boot-v53-movie-setallocator.log` + `.exitcode`。
4. 验收：日志不再以 import 74 `movie::SetAllocator` 的 `[unresolved-import]`/SIGTRAP 致命；记录**新的**可归因阻塞符号/阶段与 exitcode。
5. 更新 `coordination/HANDOFF.md` 写 P-CC-9 闭环段；`python3 scripts/project_status.py --write`（若可用）。
6. **提交检查点**（仅本工单相关文件）；回报：HEAD / 是否越过 SetAllocator / 新阻塞 / 日志路径 / exitcode。
7. 做完停下。

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push 远端；不伪造完整网络在线/付费账号；不因额度改去调用 agy；不回退 snapshot/EventFd/stdio/nnssl 已落地行为；不扩大改动到 NVN worktree。

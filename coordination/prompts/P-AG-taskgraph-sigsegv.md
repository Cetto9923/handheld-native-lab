# 工单 P-AG-18｜Antigravity｜主路径：越过 UE TaskGraph worker SIGSEGV（fa=0x4 / module 0x3cb84）

你是实现 Agent。上一单 P-AG-17 已闭环：HEAD `9c3f24d`（分支 `work/rg477m-native`）。真机已越过 PreInit：pak/ucas 已推送，UE 自行挂载 IoStore，AOC SystemEvent HLE 已绑定；全日志无 AbortImpl/LowLevelFatalError。新阻塞在 TaskGraph worker 启动后的 guest SIGSEGV。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`（P-AG-17 闭环段与「下一前沿」）、`.local/logs/rg477m-boot-v36-aoc-event.log`（exit **139**）
- 对照已落地：FS/Event/memalign/cxa_thread_atexit/AbortImpl/AOC SystemEvent、子线程 fault_scope、pak/ucas 真机 5 件齐
- 真机：`adb -s RG477M01445122`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- `.local/romfs/FFRS/Content/Paks/` 已有 FFRS-Switch.pak/ucas/utoc + global.*（约 8.32GiB）——可读，勿删大文件；真机容器已推过，勿无故重推除非校验缺失
- 验收不降级；勿动 ProtectRelro 桩除非硬需要
- `rom:/FFRS/Shaders`、`rom:/.nrr/ModuleInfo-Shipping.nrr` 在两份 manifest 中均不存在（零售包本无）——**不是本单优先补文件路径**

## 当前阻塞（已刻画，未根因）
- `[fault] signal=11 pc=… fa=0x4 module_offset=0x3cb84`（v35/v36 同址复现）
- 线程：`Background Worker (Standby #1)`（亦见 Foreground/Background Worker 族刚创建之后）
- 指令：`module 0x3cb84: ldr x20, [x9, #8]`；`x9 = x8 & ~3`、`x8 = (x1 & ~1) | 2`；`x1=0xfffffffffffffffd` → 读 `0x4` → SIGSEGV
- 上下文：无锁侵入式双向链表 push（`ldaxr/stxr` + 头指针 tag bit1）——**节点指针被污染**，非未绑定 import（无 `[unresolved-import]`），非缺容器
- 崩溃函数未存 x30，`frame[0]` 来自上层帧，调用者链不可靠；需从 `module 0x3cb24` 调用点反向追
- 阶段：`T02_PREINIT_UNBLOCKED_UE_TASKGRAPH_SIGSEGV_ON_RG477M`
- 基线日志：`.local/logs/rg477m-boot-v36-aoc-event.log`（及 v35）

## 目标
1. 逆向 `module 0x3cb24..0x3cb84` 所属函数与调用点：确认是 UE TaskGraph/无锁队列哪条路径；定位节点指针污染来源（TLS/堆对齐/错误释放/错误 HLE 写坏 guest 对象/竞态等）
2. 优先修根因（修正 HLE 语义、对象布局、线程 TP/栈、内存分配对齐、或补必要符号）；使 boot 越过该 SIGSEGV 或推进到下一明确阻塞（新 import / 新 fatal / 渲染初始化等）
3. 勿空吞 SIGSEGV；若必须 stub，打清晰 `[hle]`/`[fault]` 日志并保留可归因现场
4. native_boundary / guest_process 如需新符号则绑定；宿主契约；RG477M `--boot` 同前参数复测
5. 新日志建议 `rg477m-boot-v37-*.log` + `.exitcode`；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
6. 提交检查点；回报 HEAD / 新阻塞 / 日志路径；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空过 SIGSEGV；不下载未授权本体；不删 `.local/romfs` 大文件；不 push

做完停下汇报，等项目经理下一派工。

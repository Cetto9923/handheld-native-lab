# 工单 P-CC-6｜Claude Code｜主路径：stdio 族（stdout 数据导入 + printf 族）推进

你是实现 Agent。上一单 P-CC-5 已闭环：HEAD `f9fd30b`（分支 `work/rg477m-native`，工作区 clean）。真机已越过 `nnsslInitialize`/`nnsslFinalize`（import 606/608）；新阻塞在 **数据导入** `stdout`（STT_OBJECT）读写 PROT_NONE object sentinel → SIGSEGV。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md` 顶部 P-CC-5 闭环段、`.local/logs/rg477m-boot-v46-nnssl-initialize.log`（及 `.exitcode`=139）
- 对照已落地 HLE：`nnssl*` / `nnResult*` / `nn::os::*` —— typed 白名单 + `import_signature` + `[hle]` 可归因日志 + 宿主契约
- **关键差异**：`stdout`/`stdin`/`stderr` 是 **STT_OBJECT（数据）**，不是函数。loader（`nso_loader.c`）对未解析对象导入填 `object_traps` PROT_NONE 槽；resolve 必须返回**可读可写对象地址**（通常宿主 `FILE*`），不能返回函数入口或 BRK 哨兵
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 禁止 reset/clean/stash 覆盖 WIP；成功后可提交检查点；密钥/.pak/.ucas 不进 git；勿删 `.local/romfs` 大文件
- 验收不降级；勿伪称标题/可玩；做完停下汇报，不要 ask_user
- 若存在 worktree `task/T04-nvn-firstframe` / `.claude/worktrees/t04-nvn`，勿改其内容

## 当前阻塞（证据）
- 基线：`.local/logs/rg477m-boot-v46-nnssl-initialize.log` exit **139**
- `[hle] nnsslInitialize: initialized Nintendo SSL service (stub) -> returning 0` 已越过
- `[fault] signal=11 pc=… fa=… module_offset=0x5e5a678`
- `[unresolved-import] index=186 name=stdout caller_offset=0x5e5a874`
- 归因：dynsym `st_info&0xf == 1`（STT_OBJECT）；guest 读写 `stdout` 对象槽即 SIGSEGV（与代码导入 SIGTRAP/133 不同）
- 阶段建议标记：`T02_NNSSL_UNBLOCKED_STDIO_ON_RG477M`

## 同族未解析（`evidence/coordinator/nso_imports_main.tsv`）
**数据对象（优先，否则继续 SIGSEGV）：**
- 186 `stdout`、188 `stdin`、306 `stderr`

**函数（越过对象后很可能紧随，能推则推）：**
- 175 `printf`、307 `fprintf`、538 `vfprintf`、263 `fwrite`、532 `fputs`、533 `fputc`、308 `puts`
- 183 `snprintf`、145 `vsnprintf`、526 `sprintf`、187 `fflush`

**勿先攻 ssl 族**——ssl 在 stdout 之前不会成为前沿。

## 目标（按序）
1. 落地最小可用 stdio **数据**导入：`stdout`/`stdin`/`stderr` 绑定到宿主 `FILE*`（或等价可写对象），使 guest 解引用不再撞 PROT_NONE；打清晰 `[hle]`/`[resolve]` 归因日志（对象绑定一次即可）。
2. 按真机下一阻塞优先补紧随 stdio **函数**（至少覆盖 guest 实际调用的 printf/fprintf/fwrite/fputs/fflush 等）；可用薄转发到宿主 libc，返回值/ABI 按 AArch64 核对；格式串来自 guest 时注意安全（勿当本机可信输入执行）。
3. `guest_process.c` typed 白名单 + `import_signature`（对象：视作已解析地址；函数：按各符号 argc/returns）；`nso_host_resolve` 绑定；宿主契约覆盖对象指针非空 + 至少 1–2 个常用函数往返；RG477M 契约同测。
4. 按 v46 **同一套** diagnostic 参数跑全量 `--boot`（`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216`）。新日志建议：`.local/logs/rg477m-boot-v47-stdio-stdout.log` + `.exitcode`。产物可放 `.local/build-p-cc-6/`。
5. 验收：日志不再以 `unresolved-import … name=stdout` 致命；须出现对象/函数绑定证据；记录新阻塞符号/阶段与 exitcode。
6. 更新 `coordination/HANDOFF.md` 写 P-CC-6 闭环段；`python3 scripts/project_status.py --write`（若可用）。
7. **提交检查点**（仅本工单相关文件）；回报：HEAD / 是否越过 stdout / 新阻塞 / 日志路径 / exitcode。
8. 做完停下。

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push 远端；不伪造完整 TLS/在线账号；不把对象导入当成函数 BRK 哨兵「假绑定」。

做完停下汇报，等项目经理下一派工。

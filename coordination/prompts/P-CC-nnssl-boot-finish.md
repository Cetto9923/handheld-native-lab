# 工单 P-CC-5｜Claude Code｜接力完成 nnsslInitialize 真机 boot + 提交

你是实现 Agent。P-AG-23（agy）已写出 `nnsslInitialize`/`nnsslFinalize` HLE 并完成宿主+真机契约，但因 Antigravity 额度耗尽（429 RESOURCE_EXHAUSTED）在推送/全量 boot 前退出，未提交。请在现有 dirty WIP 上收尾，不要重写已有桩。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- HEAD 仍为 `4393ed9`；工作区已有未提交改动（保留，禁止 reset/clean/stash 丢弃）
- 先读：`AGENTS.md`、`coordination/HANDOFF.md` 顶部 P-AG-23 调度与 P-AG-22 闭环、`coordination/prompts/P-AG-nnssl-initialize.md`、`.local/agent-pm/agy-p-ag-23.log`
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 密钥与 `.pak`/`.ucas` 不进 git；勿删 `.local/romfs` 大文件
- 验收不降级；勿伪称标题/可玩；做完停下汇报，不要 ask_user
- 若存在 worktree `task/T04-nvn-firstframe`，勿改其内容

## 已完成（勿重复造轮子）
- 源码已改：`nso_host.c`/`nso_host.h`/`guest_process.c`/`tests/native/guest_process_contract.c`（nnsslInitialize/Finalize 白名单+签名+resolve+契约）
- Android 产物已编：`.local/build-p-ag-23/nso_owned_startup.android`（及同目录 contract 等），mtime ~12:08
- 宿主 `--nnssl` 契约 PASS；RG477M 契约 PASS（见 agy-p-ag-23.log）
- 现有 `.local/logs/rg477m-boot-v45-nnssl-initialize.log` **无效**：仅 `[profile-error] ... cannot open file`、exitcode=1，**不算**越过 nnssl；请覆盖或改用新文件名重跑真机全量 boot

## 当前真实阻塞（基线仍是 v44）
- 日志：`.local/logs/rg477m-boot-v44-nn-result-is-failure.log` exit **133**
- `[unresolved-import] index=606 name=nnsslInitialize caller_offset=0x5e647e8`
- 阶段：`T02_NN_RESULT_UNBLOCKED_NNSSL_INIT_ON_RG477M`

## 目标（按序）
1. 核对 WIP 与 `.local/build-p-ag-23/nso_owned_startup.android` 是否仍匹配当前源码；若源码又有改动则按既有 `scripts/runtime/build_guest_process_contract.sh` / P-AG-22–23 同款 toolchain 重编到 `.local/build-p-ag-23/`（或新建 `.local/build-p-cc-5/`，二选一写清）。
2. 把新 `nso_owned_startup.android` 推到设备，按 v44 **同一套** diagnostic port-policy / quota 参数跑全量 `--boot`（对照 v44 日志头：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216`）。写出有效新日志，建议：
   - `.local/logs/rg477m-boot-v45-nnssl-initialize.log` + `.exitcode`
   - 若需保留无效旧 v45，可先备份再覆盖，或改用 `v46-*.log` 并在汇报写明。
3. 验收：日志须出现 `[hle] nnsslInitialize...`；`unresolved-import index=606 name=nnsslInitialize` 不再是致命阻塞；记录新阻塞符号/阶段与 exitcode。能顺手推 `nnsslFinalize`/紧邻 ssl 族则推，但以越过 606 并归因下一阻塞为准。
4. 更新 `coordination/HANDOFF.md` 写 P-AG-23/P-CC-5 闭环段；`python3 scripts/project_status.py --write`（若脚本可用）。
5. **提交检查点**（仅本工单相关文件）；回报：HEAD / 是否越过 nnsslInitialize / 新阻塞 / 日志路径 / exitcode。
6. 做完停下。

## 禁止
不声称标题/可玩；不空返回掩盖未归因失败；不下载未授权本体；不删 `.local/romfs` 大文件；不 push 远端；不伪造完整 TLS/在线账号；不因额度问题改去调用 agy。

做完停下汇报，等项目经理下一派工。

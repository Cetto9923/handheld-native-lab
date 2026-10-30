# 工单 P-CC-1｜Claude Code｜并行：原生 TP / 逐线程隔离骨架（不碰主树 dirty）

你是并行实现 Agent。主路径 RWLock/boot 由 Antigravity 独占主工作区；你**不得**修改主检出上已 dirty 的：
`src/runtime/hle/mutex.*`、`nso_host.*`、`guest_process.*`、`tests/native/guest_process_contract.c`、`scripts/runtime/build_guest_process_contract.sh`。

## 工作区
优先使用独立 worktree（若存在）：
`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-native-tp-read`
分支 `task/T02-native-tp-read`。若不存在或过期：从主仓 `work/rg477m-native` 新建 `task/T02-native-tp-read-cc` worktree 到 Documents 下，基线取主仓当前 HEAD。
先只读核验主仓 HANDOFF 与 `docs/analysis/codex_tls_caller_report.md`（若有）。

## 目标
1. 在**不写 Bionic 宿主 TP**的前提下，设计并实现最小可用的「guest TP 镜像 + 透明 MRS TPIDR_EL0 读取适配」骨架（可参考仓库已有 `native_boundary` / module_tls 模式）
2. 覆盖范围写清：仅主线程模板 vs 逐线程；哪些 offsets（如 TP+0x10..）已证明需要
3. 宿主侧契约测试可先跑；Android/RG 真机测试**不要抢 adb**——若需真机，把命令与二进制路径留给总控/Antigravity，标注 NOT_RUN_DEVICE
4. 产出报告：`docs/analysis/codex_native_tp_read_report.md`（或更新已有同名），本地提交本分支检查点

## 约束
- 读 `AGENTS.md`；不 push；不打印密钥；不 reset 他人 WIP
- 固定字面量 primary / 写宿主 TPIDR 的路线已否定，不要重开
- 做完停下，列出可合并到主路径的文件清单与风险

模型偏好：按本机 Claude Code 默认/Fable 配置即可。

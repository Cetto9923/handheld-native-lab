# 工单 P-AG-2｜Antigravity｜主路径：guest TP / ActiveTask TLS + 续跑 ctor 487+

你是实现 Agent。上一单 P-AG-1 已闭环：HEAD `79ddbb8`，真机构造函数 0–486 连跑，卡在 ctor 487。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓若 clean 可正常提交检查点
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩

## 当前阻塞（证据）
- ctor **487** / 1890，import **150** `_ZTHN13LowLevelTasks5FTask10ActiveTaskE`，caller `0x1484494`
- 随后 `MRS TPIDR_EL0` + load `[x8,#0x238]`，exit **133**
- 日志：`.local/logs/rg477m-boot-v16-ctor487.log` + `.exitcode`

## 并行参考（只读/可 cherry-pick，勿踩对方未提交）
Claude Code 在 worktree：
`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-native-tp-read`（分支 `task/T02-native-tp-read`）
已有未提交骨架：`src/runtime/hle/guest_tp.c/.h`、`tests/native/guest_tp_contract.c`、`scripts/runtime/inspect_guest_tp_reads.py`
- 先只读评估能否合入主树；合入时注明来源与风险
- **不要写宿主 Bionic TPIDR**；固定字面量 primary 路线已否定
- 真机 adb 由你独占；Claude 侧标注 NOT_RUN_DEVICE 的命令由你复跑

## 目标（按序）
1. 落地最小可用 guest TP 镜像 + 透明 MRS TPIDR_EL0 读取适配（主线程至少覆盖 `TP+0x238` / ActiveTask 路径；能扩到模板 `0x10..0x1019` 更好）
2. 处理 `_ZTHN13LowLevelTasks5FTask10ActiveTaskE`（弱符号/TLS init）：绑定或等价 HLE，使 ctor 487 能越过
3. 宿主契约测试通过；在 RG477M 复跑 `--boot`，确认 ctor 最大 idx **>486**；能推则继续推下一未绑定 import
4. 更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点；回报：HEAD、越过的 ctor 最大 idx、新阻塞、日志路径、从 CC worktree 合入了哪些文件

## 禁止
- 不抢停 Claude 进程；不改其 worktree 未提交文件（复制到主树再改）
- 不声称全部 1890 ctor 已过，除非日志证明
- ProtectRelro / 全量逐线程闭环本单不强制，但若阻塞启动须记录

做完停下汇报，等项目经理下一派工。

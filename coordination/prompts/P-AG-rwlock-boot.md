# 工单 P-AG-1｜Antigravity｜主路径：完成 RWLock 并续跑真机构造函数

你是实现 Agent，不是总控文档编辑器以外的唯一写者时可更新 HANDOFF/state。用户已换号恢复额度；上一轮在 ReaderWriterLock 处中断。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`
- 当前 HEAD 约 `1bcf655`；工作区已有未提交 WIP（mutex/nso_host/guest_process/contract）——**保护并完成它**，禁止 reset/clean/stash 丢掉
- 真机：`adb -s RG477M01445122`；每次先 `adb devices` 核身份；不 root/刷机/清数据；不 push 远端
- 密钥/大型二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩

## 目标（按序）
1. 完成 `InitializeReaderWriterLock` 及相关 RWLock HLE 接线（import 163 及同族 Acquire/Release/Try/Is*），把现有 dirty 收成可构建检查点
2. 宿主 + Android 最小契约编译通过；在 RG477M 上复跑 `--boot`（或项目现用 boot 命令），确认构造函数能越过 idx=8
3. 若撞上下一未绑定 import（例如 `LoadTimeZoneRule` 等）：最小证据绑定或明确阻塞名/caller/exit，继续能推则推，不能则停并写清下一步
4. 更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点（勿 `git add .` 混入无关大文件）；回报：HEAD、越过的 ctor 最大 idx、新阻塞、日志路径

## 禁止
- 不要改无关 worktree；不要停止其他工具；不要声称全部 1890 ctor 已过，除非日志证明
- ProtectRelro / 全量逐线程 TP 本单不强制闭环，但若阻塞启动须记录

做完停下汇报，等项目经理下一派工。

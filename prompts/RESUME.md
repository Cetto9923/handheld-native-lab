# 新任务 / 新模型接力提词

接手项目 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，继续《最终幻想：共鸣》在 RG477M 上的 Android ARM64 原生运行、分辨率适配与稳定帧率目标。

先完整读根 AGENTS.md，再读 docs/PROJECT.md、coordination/state.json、coordination/HANDOFF.md 和当前任务关联报告，运行 `python3 scripts/project_status.py`。不要从头重装工具、重做已证明的研究或继承无证据的PASS。

核实当前分支/HEAD、worktree、dirty/untracked、存活进程与日志及写入占用；保护上一位WIP，先由总控转移占用。若你是新总控，登记身份；若是工作Agent，仅领具体任务与独占写入范围。多个窗口不要同时改全局状态或切同一工作区分支。

用真实证据恢复当前阶段，优先执行 HANDOFF 中下一步命令，继续最前置可做任务。设备未连接只阻塞真机验收，不能伪造帧率或把项目改成交付模拟器。每次关键结果落盘，收口一个小检查点后再接下一项；不等额度耗尽才交接。

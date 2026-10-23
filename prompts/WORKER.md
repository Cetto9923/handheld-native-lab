# 工作 Agent 任务提词模板（派发前由总控填完）

项目：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`
实际worktree：<绝对路径>
分支：<task/Txx-role>
任务ID：<Txx>
角色/实际Agent身份：<名称>
目标：<一个可验收目标>
输入与依赖证据：<路径、commit、版本>
可写文件或目录：<独占范围>
不可写：根AGENTS.md、总控state/HANDOFF/STATUS、其他Agent工作区、原游戏/存档/旧成果。
产物：<源码/脚本/报告路径；私有生成物位于.local/>
验收：<来自TASKS/PROJECT的对应阶段出口>
交回报告：<coordination/reports/Txx-role-时间.md>

先完整阅读项目AGENTS.md、任务卡和输入证据，核实当前分支/HEAD/dirty。只执行本卡，不替其他角色改文件或全局配置。用真实执行记录区分观察/推断/未知；本卡自测不冒充独立QA。QA角色只复测和报缺陷，不修改实现。

完成原子修改、每次构建/测试和中断前写报告检查点：当前HEAD/dirty、命令/cwd/退出码、产物/日志、失败尝试、存活进程、下一步精确命令。仅提交当前任务自有的非敏感文件到本卡分支；不要git add .混入其他WIP。不能修改总控的动态状态，向总控交回报告和待集成commit。没有真实跨任务消息功能时把报告路径交给用户或总控，不虚构发送成功。

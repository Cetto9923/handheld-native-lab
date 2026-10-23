# 当前接力入口

## 最后完成

仅创建独立协作项目、阶段任务和规格。没有执行本游戏反编译、提取、重编译、APK 构建或真机测试。没有真实工作 Agent 在后台运行。

本地 Git 根交付分支：`work/rg477m-native`。当前 HEAD 与工作区以 `python3 scripts/project_status.py` 为准，不把文档里的旧 SHA 当当前 HEAD。

## 下一位先做

1. 完整读 AGENTS.md，然后读 docs/PROJECT.md、TASKS.md、coordination/state.json 和本文件。
2. 运行 `python3 scripts/project_status.py`，核对分支、HEAD、未提交内容及写入占用。
3. 阅读 evidence/bootstrap-preflight.json；它是初始 PATH 与外层包清单，既不证明引擎，也不是完整包哈希。需要时运行 `python3 scripts/preflight.py` 刷新，保留新旧区别。
4. 总控登记真实身份，领取 T00；若可用多 Agent，先让 QA 只读准备验收，而非冒充已执行测试。T00 出口后并行 T01 与 T03A；T03B 为独立真机门禁，缺设备时不阻塞可做的 T04 实现。
5. T00 明确首个真实分析命令与后续样例，不只停在方案文档；按实际证据继续推进。

## 本机既有输入

目标、路径、版本线索和规格都在 docs/PROJECT.md；分析工具和参考在 docs/TOOLCHAIN.md。游戏输入保持原样。旧 Mac移植交付 是模拟器方案，不能复用其 PASS 当原生证据。

## 阻塞与未验证

设备未连接，真机硬件能力、画面和性能未验证。原生路线尚未验证，所有实现阶段未开始。是否有合法可分析输入、可复用提取目录及当前 SDK/NDK，T00 再核实；不得从旧报告推断它们已满足。

## 后续每次接力替换本文件的主体

记录：完成的任务及证据；失败尝试和根因置信度；精确 worktree/分支/HEAD/dirty；启动中的命令、PID、日志和恢复方式；下一位的前三条精确命令；待转移写入占用；哪些不能改、哪些尚未验证。历史记录保存到 coordination/reports/，不要只保留聊天摘要。

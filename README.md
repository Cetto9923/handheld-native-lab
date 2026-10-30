# Handheld Native Lab

Android ARM64 掌机原生运行时、图形/音频兼容与性能研究。当前实机研究对象为 RG477M；游戏名称仅用于说明兼容研究对象。本项目独立开发，不是游戏发行商或设备厂商的官方产品。

**2026-10-30研究基线：已在指定设备进入实际场景并恢复两名法师的身体护罩；一段移动约29.97Hz，剧情转场仍有长帧。音频、战斗、人物闪烁和完整流程验收尚未全部通过，不声称稳定可玩或普遍适配。** 精确范围、APK/着色器摘要与未通过门见 [基线说明](docs/BASELINE-20261030.md)。

GitHub 私有仓库只保存代码和清理后的研究记录，不提供游戏、固件、密钥、存档、生成的游戏代码或游戏美术资源；私有可见性不授予这些材料的分发权。第三方源码保留各自许可，见 [NOTICE](NOTICE.md)。多机型一目录安装方案属于[下一阶段](docs/PACKAGING-NEXT.md)，当前不是此类成品。

## Antigravity 开始

1. 在 Antigravity 打开本文件所在项目目录。
2. 新建任务，粘贴 [完整启动提词](prompts/START_ANTIGRAVITY.md)。
3. 更换模型、客户端或任务时使用 [接力提词](prompts/RESUME.md)。
4. 多窗口人工分派时使用 [工作 Agent 提词模板](prompts/WORKER.md)。

## 恢复入口

- [AGENTS.md](AGENTS.md)：本项目唯一工程规范。
- [PROJECT.md](docs/PROJECT.md)：目标、分辨率、帧率与阶段验收。
- [当前状态](coordination/STATUS.md)：从 state.json 生成。
- [交接记录](coordination/HANDOFF.md)：最后做到哪、实际证据与下一步。
- [阶段验收](docs/analysis/codex_stage_status.md)：按最终可玩目标列出已验证范围和缺口。
- [任务规格](TASKS.md)：T00–T07（含 T03A/T03B） 的输入、依赖及阶段出口。
- [工具与参考](docs/TOOLCHAIN.md)：REA、上游与本地环境入口。

```sh
python3 scripts/project_status.py
python3 scripts/preflight.py
```

preflight 只检查 PATH、标准 SDK 目录和指定游戏包的 PFS0 外层清单，不解密、不执行游戏、不读取密钥内容。外层清单指纹不是完整游戏包 SHA-256。

## Token 和接力

不要把全部状态留在一个聊天里。总控维护小而明确的 state.json 和 HANDOFF.md，每个工作 Agent 有独立任务、写入范围和回报文件。频繁保留实际构建、失败与下一条命令，换模型时继续未完成任务，不重新从头调研。

自有源码和记录本地提交；私有输入与大型产物保存在 `.local/`，不能因 Git 忽略就视为已经备份。若换电脑，需另行保全这些产物或使用记录的命令重新生成。

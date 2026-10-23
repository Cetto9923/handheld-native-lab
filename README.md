# FINAL FANTASY RESONANCE → Anbernic RG477M

目标：从游戏本体建立 Android ARM64 原生运行路线，适配 RG477M 分辨率与实体控制器，以稳定帧率和持续运行表现验收。

**当前只有协作项目、任务与探针，尚无本游戏原生实现、APK、真机运行或帧率达标结果。** 原生可行性和性能需要实证；反编译技能与芯片理论性能不保证结果。

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

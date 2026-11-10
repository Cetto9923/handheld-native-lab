# Handheld Native Lab

Android ARM64 原生运行时、图形/音频兼容层与多设备移植工程实践。本项目独立开发，旨在探索主机/掌机原生 AArch64 代码在通用 Android 设备上的原生执行与 Vulkan 图形重构。

**最新成果：v1.0.2 通用版已发布**
- **全机型屏幕旋转与比例自适应**：解耦原生纵向屏幕限制，针对各品牌手机/平板（包括一加、荣耀、小米等）统一采用系统级逻辑横屏（Identity Transform + Opaque Composition），杜绝反向拉伸与画面黑边条。
- **高通骁龙（Adreno 全系 / 骁龙 8 至尊版）着色器崩溃修复**：全面放开子群着色器（Subgroup Helper Inlining）动态注入与基数同步保护，彻底解决 Adreno 830 / 750 / 735 系列驱动编译崩溃导致的瞬间闪退。
- **免特权多介质数据包自动发现**：支持外置 TF/SD 卡、次级存储卷及 Android 11+ 作用域存储保底路径（`getExternalFilesDirs`），实现 APK 覆盖更新免重拷数据包。
- **[Switch 到 Android 原生移植避坑与工程总结指南](docs/SWITCH_TO_ANDROID_PORTING_PITFALLS_GUIDE.md)**：包含动态链接、内存布局、Vulkan/NVN 桥接、着色器转译、存储权限与生产构建的全部工程踩坑实录。

## 快速安装与运行

1. 从 [GitHub Releases](https://github.com/Cetto9923/handheld-native-lab/releases) 下载最新发布的 APK 安装包（`FFRS-Universal-Fix.apk`）。
2. 将已持有的合法数据包目录放置在设备主存储根目录或应用私有存储中（如 `/sdcard/FFRS/`）。
3. 安装 APK 并启动，应用将自动识别存储卷中的游戏本体数据，无需重新迁移或更换数据包。

> [!NOTE]
> 本项目为开源原生运行时工程与兼容层代码，**不包含任何专有游戏数据包、商业 ROMFS 资源、固件或解密密钥**。第三方依赖代码保留其原始开源许可证，详见 [NOTICE](NOTICE.md)。

## 💬 游戏数据包获取与交流群

本项目遵从开源协议，仅分发代码与轻量加载器 APK，不直接在 GitHub 托管游戏商业资产。

- **数据包获取**：打开抖音搜索 **`Cetto`**（昵称/马甲：**`樱木`**），进入主页加入 **抖音交流群**。
- **群公告已置顶**：群内已置顶分享夸克网盘一键下载链接（内含已配齐的 17GB 通用数据包 `FFRS_Data_Universal.zip` 及详细安装说明）。
- **交流与反馈**：欢迎群内交流各种安卓设备（一加、荣耀、红米、小米等）的运行表现、按键手柄映射与后续移植体验！

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

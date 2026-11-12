# Handheld Native Lab: Final Fantasy Resonance (最终幻想：共鸣) Android Native Port

[![Release](https://img.shields.io/github/v/release/Cetto9923/handheld-native-lab)](https://github.com/Cetto9923/handheld-native-lab/releases)
[![Platform](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/Cetto9923/handheld-native-lab)
[![Graphics](https://img.shields.io/badge/Graphics-Vulkan%201.1+-orange.svg)](https://github.com/Cetto9923/handheld-native-lab)
[![License](https://img.shields.io/badge/License-MIT%20%2F%20Custom-blue.svg)](NOTICE.md)

An independent, open-source Android ARM64 native runtime, dynamic HLE compatibility layer, and Vulkan graphics renderer for **Final Fantasy: Resonance (最终幻想：共鸣)**. Supports Anbernic RG477M, Snapdragon 8 Elite / 8 Gen 3 / 8 Gen 2 / 7+ Gen 3, Dimensity devices, and standard Android smartphones & tablets.

> **中文简介**：本项目为《最终幻想：共鸣》独立研发的 Android 原生运行时与 Vulkan 兼容层。采用原生 AArch64 代码执行（非模拟器架构），具备全机型屏幕比例与方向自适应、高通骁龙 Adreno 着色器动态内联修复、以及多存储卷免权限数据包自动识别能力。

**最新成果：v1.0.6 稳定性与帧率优化版已发布**
- **全机型屏幕旋转与比例自适应**：解耦原生纵向屏幕限制，针对各品牌手机/平板（包括一加、荣耀、小米等）统一采用系统级逻辑横屏（Identity Transform + Opaque Composition），杜绝反向拉伸与画面黑边条。
- **高通骁龙（Adreno 全系 / 骁龙 8 至尊版）着色器崩溃修复**：全面放开子群着色器（Subgroup Helper Inlining）动态注入与基数同步保护，彻底解决 Adreno 830 / 750 / 735 系列驱动编译崩溃导致的瞬间闪退。
- **免特权多介质数据包自动发现**：支持外置 TF/SD 卡、次级存储卷及 Android 11+ 作用域存储保底路径（`getExternalFilesDirs`），实现 APK 覆盖更新免重拷数据包。
- **[Switch 到 Android 原生移植避坑与工程总结指南](docs/SWITCH_TO_ANDROID_PORTING_PITFALLS_GUIDE.md)**：包含动态链接、内存布局、Vulkan/NVN 桥接、着色器转译、存储权限与生产构建的全部工程踩坑实录。

## 快速安装与运行

1. 从 [GitHub Releases](https://github.com/Cetto9923/handheld-native-lab/releases) 下载最新发布的 APK 安装包（`FFRS-v1.0.6.apk`）。
2. 将已持有的合法数据包目录放置在设备主存储根目录或应用私有存储中（如 `/sdcard/FFRS/`）。
3. 安装 APK 并启动，应用将自动识别存储卷中的游戏本体数据，无需重新迁移或更换数据包。

> [!NOTE]
> 本项目为开源原生运行时工程与兼容层代码，**不包含任何专有游戏数据包、商业 ROMFS 资源、固件或解密密钥**。第三方依赖代码保留其原始开源许可证，详见 [NOTICE](NOTICE.md)。

## 💬 游戏数据包获取与交流群

本项目遵从开源协议，仅分发代码与轻量加载器 APK，不直接在 GitHub 托管游戏商业资产。

- **数据包获取**：打开抖音搜索 **`Cetto`**（昵称/马甲：**`樱木`**），进入主页加入 **抖音交流群**。
- **群公告已置顶**：群内已置顶分享夸克网盘一键下载链接（内含已配齐的 17GB 通用数据包 `FFRS_Data_Universal.zip` 及详细安装说明）。
- **交流与反馈**：欢迎群内交流各种安卓设备（一加、荣耀、红米、小米等）的运行表现、按键手柄映射与后续移植体验！

## ⚖️ 免责声明 (Legal Disclaimer)

1. **学术与技术研究用途**：本项目代码及相关文档仅供计算机软硬件体系结构兼容性、ARM64 原生运行时执行与 Vulkan 图形 API 桥接的技术研究、教学与个人学习使用，严禁用于任何商业目的。
2. **版权归属**：游戏本体《FINAL FANTASY RESONANCE》、涉及的游戏代码、美术资源、音乐音效、商标及一切知识产权均归原版权方（包括 Square Enix、Nintendo 等）所有。本项目与原厂商无任何商业合作、赞助或官方隶属关系。
3. **使用者义务与正版倡议**：使用者应自行通过正规合法渠道购买正版游戏软件。群内提供之兼容性测试整合资源仅供研究与尝鲜评测，测试完成后请于 24 小时内自行删除。
4. **禁止牟利**：任何个人或组织严禁将本项目源码、打包 APK、群聊资格或相关测试资源用于任何形式的收费、倒卖、捆绑销售或广告商业牟利，违者须自行承担全部法律后果。

## 📂 项目目录架构说明

```
handheld-native-lab/
├── src/                          # 核心源码
│   ├── android/                  # Android NativeActivity、JNI 桥接与机型画像适配（DeviceProfile）
│   ├── graphics/                 # NVN ↔ Vulkan 图形翻译层、SPIR-V 动态子群助手重写器、着色器管道
│   └── runtime/                  # AArch64 HLE 运行时、POSIX/TLS 线程环境模拟层、NSO 加载器
├── scripts/                      # 工具与构建脚本
│   ├── android/                  # APK 自动化编译与机型签名脚本
│   ├── graphics/                 # Vulkan 着色器提取与帧率/管线分析工具
│   └── distribution/             # 数据包契约验证与自动化部署导出工具
├── tests/                        # 自动化测试套件
│   ├── device/                   # 多机型屏幕方向、Adreno 驱动画像合同单元测试
│   └── native/                   # 核心运行时 ABI 与图形契约测试
├── docs/                         # 项目技术文档
│   ├── PROJECT.md                # 架构设计、渲染分辨率与性能验收门禁
│   └── SWITCH_TO_ANDROID_PORTING_PITFALLS_GUIDE.md # Switch 移植 Android 完整避坑指南
├── NOTICE.md                     # 第三方依赖与开源许可证声明
└── AGENTS.md                     # 项目工程总控与代码协作规范
```

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

# 工具与参考入口

## 本机已有入口

- 分析环境说明：`/Users/yuyan9923/.local/share/rea-toolchain/README.md`
- 共享环境变量：`/Users/yuyan9923/.local/share/rea-toolchain/env.sh`
- Antigravity REA 技能：`/Users/yuyan9923/.gemini/config/skills/reverse-engineer-anything/SKILL.md`
- 共享技能：`/Users/yuyan9923/.agents/skills/reverse-engineer-anything/SKILL.md`
- CLI：`/Users/yuyan9923/.local/bin/rea`、`hactool`、`ghidra-headless`。
- 此前安装 Ghidra 12.1.4、Switch Loader、Java 及 REA 6.5.0。T00 应验证当前实际入口与版本，不继承历史安装结论作为本轮运行结果。

用实际加载的 REA MCP schema；如果当前会话已经可用，直接针对目标分析，不例行重复 doctor/setup。需要 NSO 分析时按 Ghidra Switch Loader 的当前可用路径处理，避免误把不支持格式交给 REA 原生入口。

构建 Android 的 NDK、SDK、CMake、Ninja、ADB 等先在本机检查，缺什么再按选择的路线安装什么。preflight 只报告 PATH/常用 SDK 路径，未找到不证明整机不存在。正式选用版本写进自有构建清单；不要盲装所有参考项目的依赖。

## 候选研究资料（不是已确认适用的依赖）

- REA：https://github.com/morluto/rea
- Ghidra：https://github.com/NationalSecurityAgency/ghidra
- Switch Loader：https://github.com/Adubbz/Ghidra-Switch-Loader
- SuperMarioWonderRecomp：https://github.com/BelmanteGu/SuperMarioWonderRecomp
- Android 研究文档：https://raw.githubusercontent.com/BelmanteGu/SuperMarioWonderRecomp/main/docs/nvn/ANDROID-PORT.md
- AC-Recomp：https://github.com/swaymun/AC-Recomp

2026-10-23 查询时 Wonder 仓库主页有 404，而部分原始文档仍可读取；它的可获取性、revision 和依赖必须重新验证。Android 研究文档含估算、规划与不同时点记录，不能当 RG477M 实测。AC-Recomp 是另一款游戏的专用实验，不是通用 NSP 转换器。

所有引用先验证来源和不可变 revision，检查源码、许可证和实际输入约束，再决定复用。不得为了套用现有项目而绕过 TitleID/版本检查。不默认支持本游戏，不因页面 404 就宣称整个原生路线不可能。

## 初始未知

本作引擎、NSO 模块、控制流/指令覆盖、所需系统服务、纹理/着色器清单、Mali GPU 特性、实际 ROM 可分析程度与完整原生性能，均待验证。当前没有选定或安装新的重编译运行时。

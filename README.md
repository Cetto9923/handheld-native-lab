# Handheld Native Lab: Final Fantasy Resonance (最终幻想：共鸣) Android Native Port

[![Release](https://img.shields.io/github/v/release/Cetto9923/handheld-native-lab?label=Release&color=brightgreen)](https://github.com/Cetto9923/handheld-native-lab/releases)
[![Platform](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/Cetto9923/handheld-native-lab)
[![Graphics](https://img.shields.io/badge/Graphics-Vulkan%201.1+-orange.svg)](https://github.com/Cetto9923/handheld-native-lab)
[![License](https://img.shields.io/badge/License-MIT%20%2F%20Custom-blue.svg)](NOTICE.md)

An independent, open-source Android ARM64 native runtime, HLE compatibility layer, and Vulkan graphics renderer for **Final Fantasy: Resonance (最终幻想：共鸣)**. Features aspect-ratio adaptation, graphics pipeline warmup, in-game cheat & save management, and multi-storage volume detection.

> **中文简介**：本项目为《最终幻想：共鸣》独立研发的 Android 原生运行时与 Vulkan 兼容层，采用原生 AArch64 执行（非模拟器架构）。具备屏幕比例自适应、内置金手指与存档备份管理、Vulkan 渲染着色器优化及多存储卷数据包自动识别。

## 更新历史

**v1.0.30-preview（2026-11-15）** — 内置金手指与存档流程优化 + 画面修复
- **内置金手指编辑**：新增游戏内中文金手指面板，支持常用满状态一键应用及自定义数值修改；修复缺少本地名称文件时角色名与数值标签无法正常显示中文的问题。
- **存档一键备份与恢复**：菜单整合为「备份存档」与「恢复备份」，自动记录并显示备份时间点与最近写盘进度，支持无缝还原历史存档。
- **后台导出优化**：修复应用切至后台时重复创建备份文件夹的问题，现统一由菜单手动触发导出。
- **角色光照与模型还原**：修复计算着色器光照缓冲的多分量写入截断缺陷，彻底解决角色在部分光照场景下沦为纯黑剪影的问题，完整恢复模型动态漫反射与皮肤材质。
- **粒子几何伪影修复**：修复序章火焰等场景中粒子系统因数据类型回退导致的实例索引塌陷，消除角色跑动时伴随的透明方形网格与色差方块。
- **地表与视角闪烁修复**：修正水都等章节地面覆盖层的深度测试与偏移逻辑，消除视角移动时的逐帧高频 Z-Fighting 闪烁。
- **内存与显存占用优化**：重构渲染缓冲闲置回收机制，大地图场景常驻内存占用由约 6.5GB 降至约 4.2GB，显著降低长时运行的崩溃风险，提升帧率平稳度。
- **菜单与按键解耦**：功能菜单与虚拟控制键独立分层，点击菜单直接展开功能列表，支持通过 PAD 按键独立开关虚拟按键显示。

**v1.0.23-preview（2026-11-15）** — 渲染管线预热 + 地形与精度优化
- 预热清单扩充至 1162 条渲染管线，启动时自动与本地记录合并，减少技能与转场首次释放时的着色器编译卡顿。
- 补齐着色器预热发行工具支持（`ib1`、`stb1`、`vin1` 及组合着色器类型）。
- 修正地形整数顶点接口与角色位置运算精度，优化清屏覆盖逻辑。

**v1.0.9（2026-11-13）** — 图形写入适配 + 场景稳定性
- 存储纹理缓冲（Typed Storage）格式匹配重写，修复格式化写入缺失导致的图形驱动 FATAL 崩溃。
- 场景切换崩溃修复：事件生命周期竞态由 abort 改为 retire，消除多等待者进程终止。
- 蓝牙 A2DP 音频延迟消除，快速加载流程与交互体验优化。
- 已验证的管线配方作为 seed 随数据包分发，支持增量追加，减少冷启动着色器编译卡顿。
- 显示分辨率分层与 4:3 比例填充优化，稳定 30 FPS 帧率节奏。

**v1.0.8（2026-11-12）** — 间接绘制修复 + 实用功能
- 恢复间接绘制 GPU 同步，修复月晕与特效闪烁。
- 解除间接绘制管线阻塞，野外帧率稳定锁定 30.00 FPS。
- 虚拟按键新增快速存档 / 快速读档。
- 新增角色名显示与属性查看（作弊调试工具）。

**v1.0.7（2026-11-11）** — 管线缓存 + 崩溃修复
- 计算着色器热配方学习与缓存，复用优化后的着色器变体，改善场景切换卡顿。
- 修复多等待者事件过早释放导致的崩溃。
- 音频流互斥锁修复，存档 fsync 移至锁外执行。

**v1.0.6（2026-11-12）** — 帧率与稳定性优化
- 独立 PCM 传输队列 + per-target GPU fence，改善标题与野外帧节奏。
- SPIR-V 间接实例重写器与整型纹理读取修正，修复部分粒子/特效缺失。
- 音频桥写入节奏修正，减少场景切换时的 PCM 空白与音频断续。

**v1.0.4（2026-11-11）** — 掌机操作模式适配 + 存储发现
- 完整掌机操作配置文件，自动切换掌机操作模式与布局。
- 存储 Intent 路由与数据包发现逻辑升级，支持 APK 覆盖更新免重拷数据。

**v1.0.3（2026-11-11）** — 土之神殿崩溃修复
- 修复进入土之神殿（Earth Shrine）场景时的渲染阻塞与假死。

**v1.0.2（2026-10-30）** — 初始公开基线
- 全屏旋转与比例自适应（系统级逻辑横屏，消除反向拉伸与黑边）。
- 着色器子组辅助内联（Subgroup Helper Inlining）动态注入，解决驱动编译崩溃闪退。
- 外置 TF/SD 卡及 Android 11+ 作用域存储路径自动识别。

## 快速安装

1. 从 [GitHub Releases](https://github.com/Cetto9923/handheld-native-lab/releases) 下载最新 APK（如 `FFRS-1.0.30-preview-20261115.apk`）。
2. 将已持有的合法数据包目录放置在主存储根目录或应用私有存储中（如 `/sdcard/FFRS/`）。
3. 安装 APK 后直接启动，应用自动识别存储卷中的游戏数据，无需手动迁移。

> [!NOTE]
> 本项目仅包含运行时与兼容层代码，**不含任何游戏数据包、商业 ROMFS、固件或密钥**。第三方依赖保留其原始开源许可证，详见 [NOTICE](NOTICE.md)。

## 💬 数据包获取与交流群

- 抖音搜索 **`Cetto`**（马甲：**`樱木`**），进入主页加入抖音交流群。
- 群公告已置顶夸克网盘一键下载链接（17GB 通用数据包 `FFRS_Data_Universal.zip` + 安装说明）。
- 欢迎反馈实际运行情况、按键映射体验与各类场景表现。

## ⚖️ 免责声明

1. **学术与技术研究**：本项目代码仅供 ARM64 原生运行时与 Vulkan 图形 API 桥接的技术研究与个人学习，严禁商业用途。
2. **零资产原则**：本项目仓库及发布的安装包仅包含运行时程序与系统兼容层代码，**严禁且不包含任何原版游戏的数据包、商业 ROMFS、音频、模型、固件或密钥**。
3. **版权归属**：《FINAL FANTASY》（最终幻想）及游戏中所有角色、美术、音乐等知识产权均归属于 SQUARE ENIX Co., Ltd. 及原版权所有者。本项目与原厂商无任何商业关联或授权关系。
4. **正版倡议**：请通过正规渠道购买正版游戏支持开发者。运行本程序需要使用者自行合法提取并提供对应版本的完整正版游戏数据。
5. **免责保证**：本程序按“现状”（AS-IS）提供，不包含任何明示或暗示的保证。请妥善备份个人存档，作者不对因使用本程序可能导致的存档损坏、数据丢失或其他任何异常承担责任。

## 📂 目录结构

```
handheld-native-lab/
├── src/
│   ├── android/        # NativeActivity、JNI 桥接、DeviceProfile 适配
│   ├── graphics/       # NVN ↔ Vulkan 翻译层、SPIR-V 重写器、着色器管道
│   └── runtime/        # AArch64 HLE 运行时、POSIX/TLS 线程层、NSO 加载器
├── scripts/
│   ├── android/        # APK 自动化构建与机型签名
│   ├── graphics/       # 着色器提取与帧率分析工具
│   └── distribution/   # 数据包验证与部署导出
├── tests/
│   ├── device/         # 屏幕方向与图形驱动契约单元测试
│   └── native/         # 核心运行时 ABI 与图形契约测试
├── docs/
│   ├── PROJECT.md      # 架构设计、渲染分辨率与性能验收规格
│   └── SWITCH_TO_ANDROID_PORTING_PITFALLS_GUIDE.md
├── NOTICE.md
└── AGENTS.md
```

## 参考文档

- **[Switch → Android 移植踩坑指南](docs/SWITCH_TO_ANDROID_PORTING_PITFALLS_GUIDE.md)**：动态链接、内存布局、NVN/Vulkan 桥接、着色器转译、存储权限与构建完整实录

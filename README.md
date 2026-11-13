# Handheld Native Lab: Final Fantasy Resonance (最终幻想：共鸣) Android Native Port

[![Release](https://img.shields.io/github/v/release/Cetto9923/handheld-native-lab?label=Release&color=brightgreen)](https://github.com/Cetto9923/handheld-native-lab/releases)
[![Platform](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/Cetto9923/handheld-native-lab)
[![Graphics](https://img.shields.io/badge/Graphics-Vulkan%201.1+-orange.svg)](https://github.com/Cetto9923/handheld-native-lab)
[![License](https://img.shields.io/badge/License-MIT%20%2F%20Custom-blue.svg)](NOTICE.md)

An independent, open-source Android ARM64 native runtime, HLE compatibility layer, and Vulkan graphics renderer for **Final Fantasy: Resonance (最终幻想：共鸣)**. Supports Anbernic RG477M, Dimensity 8300 / Snapdragon 8 Elite / 8 Gen 3 / 8 Gen 2 / 7+ Gen 3, and standard Android phones & tablets.

> **中文简介**：本项目为《最终幻想：共鸣》独立研发的 Android 原生运行时与 Vulkan 兼容层，采用原生 AArch64 执行（非模拟器架构）。支持安柏尼克 RG477M 及主流骁龙/天玑机型，具备屏幕比例自适应、Adreno 着色器崩溃修复和多存储卷数据包自动识别。

## 更新历史

**v1.0.9（2026-11-13）** — Mali 图形修复 + 场景稳定性
- Mali-G615（天玑/RG477M）typed storage 格式匹配重写，修复 formatless write 导致的驱动 FATAL 崩溃
- 场景切换崩溃修复：事件生命周期竞态由 abort 改为 retire，消除多等待者进程终止
- 蓝牙 A2DP 音频延迟消除，快速加载 UX 优化
- 已验证的管线配方作为 seed 随数据包分发，支持增量追加，减少冷启动着色器编译卡顿
- RG477M 分辨率分层 + 4:3 pillarbox，30 FPS 声明

**v1.0.8（2026-11-12）** — 间接绘制修复 + 实用功能
- 恢复间接绘制 GPU 同步，修复月晕与特效闪烁
- 解除间接绘制管线阻塞，野外帧率稳定锁定 30.00 FPS
- 虚拟 Pad 新增快速存档 / 快速读档
- 新增角色名显示与属性查看（作弊调试工具）

**v1.0.7（2026-11-11）** — 管线缓存 + 崩溃修复
- 计算着色器热配方学习与缓存，复用优化后的着色器变体，改善场景切换卡顿
- 修复多等待者事件过早释放导致的崩溃
- 音频流互斥锁修复，存档 fsync 移至锁外执行

**v1.0.6（2026-11-12）** — 帧率与稳定性优化
- 独立 PCM 传输队列 + per-target GPU fence，改善标题与野外帧节奏
- SPIR-V 间接实例重写器与整型纹理读取修正，修复部分粒子/特效缺失
- 音频桥写入节奏修正，减少场景切换时的 PCM 空白与音频断续

**v1.0.4（2026-11-11）** — 天玑 8300 & RG477M 掌机适配
- RG477M（天玑 8300 / Mali-G615）完整 DeviceProfile，自动切 Handheld 操作模式
- 多机型存储 Intent 路由与数据包发现逻辑升级，支持 APK 覆盖更新免重拷数据

**v1.0.3（2026-11-11）** — 地球神殿崩溃修复
- 修复进入 Earth Shrine 场景时的渲染阻塞与 ANR

**v1.0.2（2026-10-30）** — 初始公开基线
- 全机型屏幕旋转与比例自适应（系统级逻辑横屏，消除反向拉伸与黑边）
- Adreno 全系 Subgroup Helper Inlining 动态注入，解决骁龙 830/750/735 驱动崩溃闪退
- 外置 TF/SD 卡及 Android 11+ 作用域存储路径自动识别

## 快速安装

1. 从 [GitHub Releases](https://github.com/Cetto9923/handheld-native-lab/releases) 下载最新 APK（`FFRS-v1.0.9.apk`）
2. 将已持有的合法数据包目录放置在主存储根目录或应用私有存储中（如 `/sdcard/FFRS/`）
3. 安装 APK 后直接启动，应用自动识别存储卷中的游戏数据，无需手动迁移

> [!NOTE]
> 本项目仅包含运行时与兼容层代码，**不含任何游戏数据包、商业 ROMFS、固件或密钥**。第三方依赖保留其原始开源许可证，详见 [NOTICE](NOTICE.md)。

## 💬 数据包获取与交流群

- 抖音搜索 **`Cetto`**（马甲：**`樱木`**），进入主页加入抖音交流群
- 群公告已置顶夸克网盘一键下载链接（17GB 通用数据包 `FFRS_Data_Universal.zip` + 安装说明）
- 欢迎反馈各机型（一加、荣耀、红米、小米等）的运行情况与手柄映射体验

## ⚖️ 免责声明

1. **学术与技术研究**：本项目代码仅供 ARM64 原生运行时与 Vulkan 图形 API 桥接的技术研究与个人学习，严禁商业用途。
2. **版权归属**：《FINAL FANTASY RESONANCE》的一切知识产权归原版权方（Square Enix、Nintendo 等）所有，本项目与原厂商无任何商业关联。
3. **正版倡议**：请通过正规渠道购买正版游戏。群内兼容性测试资源仅供研究，测试完成后请于 24 小时内自行删除。
4. **禁止牟利**：严禁将本项目代码、APK、群资格或测试资源用于任何收费、倒卖或商业销售，违者自行承担全部法律后果。

## 📂 目录结构

```
handheld-native-lab/
├── src/
│   ├── android/        # NativeActivity、JNI 桥接、DeviceProfile 机型适配
│   ├── graphics/       # NVN ↔ Vulkan 翻译层、SPIR-V 重写器、着色器管道
│   └── runtime/        # AArch64 HLE 运行时、POSIX/TLS 线程层、NSO 加载器
├── scripts/
│   ├── android/        # APK 自动化构建与机型签名
│   ├── graphics/       # 着色器提取与帧率分析工具
│   └── distribution/   # 数据包验证与部署导出
├── tests/
│   ├── device/         # 多机型屏幕方向与 Adreno 驱动契约单元测试
│   └── native/         # 核心运行时 ABI 与图形契约测试
├── docs/
│   ├── PROJECT.md      # 架构设计、渲染分辨率与性能验收规格
│   └── SWITCH_TO_ANDROID_PORTING_PITFALLS_GUIDE.md
├── NOTICE.md
└── AGENTS.md
```

## 参考文档

- **[Switch → Android 移植踩坑指南](docs/SWITCH_TO_ANDROID_PORTING_PITFALLS_GUIDE.md)**：动态链接、内存布局、NVN/Vulkan 桥接、着色器转译、存储权限与构建完整实录

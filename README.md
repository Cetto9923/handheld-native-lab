# Handheld Native Lab

面向 Android ARM64 设备的原生运行时与 Vulkan 图形/音频兼容层，支持高通骁龙（Adreno）与联发科天玑（Mali / RG477M 等）平台运行《最终幻想：共鸣 / FINAL FANTASY RESONANCE》。本项目为独立技术研究与兼容实现，非游戏发行商或设备厂商官方产品。

## 核心特性

- **原生 ARM64 运行时与 Vulkan 渲染器**：支持原生 720p/1080p 输出切换、BC 纹理原生采样、精确深度/顶点输入兼容与多场景描述符/同步热路径优化（`PushMemo`、`PushMemoTexel`、`PushMemoShaderState`、窄屏障与单趟写回检查）。
- **画质与帧率设置**：内置 `30 / 40 / 60 FPS` 实时帧率上限调节与 `720p / 1080p` 输出档位切换。
- **内置功能菜单**：支持可自定义位置的对称紧凑虚拟手柄、全屏与旋转控制、实时 FPS 显示、存档备份/恢复及内置金手指。

## 构建说明

```sh
# 环境预检
python3 scripts/preflight.py

# 构建 Android ARM64 APK
bash scripts/android/build_game_window_apk.sh
```

> **使用说明**：安装 APK 后，需在设备存储根目录 `/sdcard/FFRS` 放置合法导出的游戏数据。

## 声明与许可

- 本仓库仅包含兼容层与工具源码，**不提供**任何游戏本体、固件、密钥、存档或美术音频资源。
- 第三方依赖许可保留各自声明，详见 [NOTICE.md](NOTICE.md)。

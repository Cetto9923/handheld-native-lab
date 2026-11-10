# Android 移植发行最佳实践（SSOT）｜Universal APK + External Game Data + Runtime Device Profile

> **日期**：Asia/Shanghai **2026-10-31**  
> **状态**：本项目选定模型写死；后续装包 / 换机 / 档位 / 提词必须以本文为准。  
> **相关**：`coordination/prompts/_STANDING-audio-acceptance.md`（音频常设硬门，必须引用）  
> **落地刀序**：P-DIST-1 → P-DIST-2 → P-OPT-HONOR（mid）→ 双机回归 → 40Hz → 手柄  

---

## 1. 业界常见做法（简述对照）

| 模型 | 安装物 | 数据 | 换机方式 | 备注 |
|---|---|---|---|---|
| 模拟器发行 | APK（模拟器本体） | 用户自备 ROM / dump | 换机仍自备 ROM | **非本项目模型** |
| 商业移植 / 官方手游 | APK（常含 ABI splits） | Play Asset Delivery / OBB / 首包内置 + 热更 CDN | 商店按设备下分资源 | 需要商店基建与签名链路 |
| 社区 / 掌机移植常见 | **通用 APK** | **外置数据包目录**（拷 SD / Download） | 启动自动发现 | **本项目采用此模型**（用户已定硬门） |

本仓库用户硬门原文要点：**「拷数据到卡上 + 只装 APK → 自动扫描发现并启动；换机同一套可顺利安装启动。」**

---

## 2. 本项目选定模型（写死）

### 标题：**Universal APK + External Game Data + Runtime Device Profile**

一句话：一个安装包、一份机型无关数据包；机型差异全部落在运行时 Profile，不拆包。

### 2.1 安装包（唯一）

- **一个** `arm64-v8a` APK，包名 `org.ffrs.gamewindow`（隔离实验可用 `org.ffrs.perfisolated` 等，不得冒充主交付）。
- **不含**整包游戏资源；可含极小默认 UI / 字体 / 引导文案。
- 签名与版本：`versionCode` **单调递增**；每次交付 `RESULT.md` 必须记 **APK SHA-256**（当场 `sha256sum`）。
- 禁止：按机型打不同正式 APK；禁止「荣耀包 / RG477M 包」双轨正式交付。

### 2.2 数据包（唯一，机型无关）

固定布局（至少）：

```
<game_root>/
  main.nso
  romfs/
    FFRS/Content/Paks/   # 关键容器所在
```

- 用户拷到内置或外置常见路径即可；**换机不换数据包**。
- App **必须**自动扫描发现；Intent extra `game_dir` **仅调试**，不得当验收 PASS。
- 关键容器常见文件（缺则点名，勿假装齐全）：`FFRS-Switch.pak` / `.ucas` / `.utoc`、`global.ucas` / `global.utoc`。

### 2.3 机型差异 = 运行时档位 Profile，不拆包

| profile | 检测线索（示例） | 目标分辨率 | 目标 Hz（游戏模式） | 备注 |
|---|---|---|---|---|
| **low** | 天玑 / 中低 Adreno / 小屏掌机如 **RG477M** | **720P** | 先 **30** 再冲 **40** | 保底可玩（当前基线） |
| **mid** | 8s Gen3 / **Adreno735** 等如 **荣耀 GT Pro** | **1080P** | 先 **30** 再冲 **40** | 大屏验收 |
| **high** | 8 Elite / Adreno830 等如 **奥丁3** | **1080P+** | **40** 优先，**60** 后置 | 余量大 |

**检测顺序（写死）：**

1. 显式 override（调试 / 用户设置；须可关闭且默认不挡自动）  
2. SOC / GPU 字符串表（`ro.soc.model`、`GL_RENDERER` 等）  
3. 屏幕短边 / 内存启发式  
4. 默认 **low**

**禁止**：为每台机打不同 APK；为每台机打不同数据包；把狂暴模式数字冒充游戏模式验收。

### 2.4 可选缓存（非必装）

- shader / PSO / 驱动相关缓存按 `gpu_renderer + driver_version` **分目录**存放。
- 换机可重建；**不进**必装数据包；丢缓存不得导致「找不到游戏数据」。

### 2.5 输入

- 触摸兜底 + 标准手柄（后续 PS5 / NS）。
- 按键映射与 Device Profile **解耦**（映射表可共享；灵敏度可按屏尺寸微调，但不拆包）。

### 2.6 音频硬门

- 一律引用：**验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`**。
- 抬帧 / 冲 40 / 去主机限帧 **每步** 复测蓝牙 A2DP；不得为冲帧永久砍薄缓冲；不得默认 `LOW_LATENCY`；`fence_spin_us` 默认 0。

### 2.7 验收矩阵（换机）

同一 **APK SHA** + 同一 **数据包** 在机 A、机 B 均须：

| 项 | 标准 |
|---|---|
| 自动发现 | 冷启、无手填路径、内置或外置至少一种真机 PASS；理想两种 |
| 进 3D | setup → guest → 可见 3D 画面 |
| 档位正确 | 日志可归因 profile=low/mid/high；分辨率 / `gfx-target-fps` 与表一致 |
| 音频常设 | `_STANDING-audio-acceptance.md` 全勾 |
| 视觉不回退 | 无新全黑 / UI 遮盖 / 异常拉伸；不复发已知 FATAL |

当前基线锚点：**RG477M ≈ 游戏模式稳 30Hz 可玩（战斗略卡）= low 档保底**；下一步 **荣耀 GT Pro = mid 档 1080P 先 30 再 40**。

### 2.8 明确不做

- 按机型分包正式 APK / 不同数据包  
- 手填绝对路径冒充安装成功  
- 狂暴模式数字冒充游戏模式  
- 为冲帧永久砍音频缓冲 / 默认 `LOW_LATENCY` / 无 A/B 的 `fence_spin>0`  
- 未写 `OSS-NOTES.md`（含 URL/commit）的无出处大改  
- 完整 NSP 自动解包流水线、无界整盘深扫拖死启动  

---

## 3. 工程落点（给写手导航）

| 能力 | 主要落点 | 备注 |
|---|---|---|
| 傻瓜安装 / 自动发现 | `GameActivity.java`：`hasGame` / `candidateGameDirs` / `resolveGameDir` / `refreshSetup` / 存储权限 | 扩扫描；Intent `game_dir` 仅调试 |
| Device Profile | 新建小模块 + 与 `gfx-target-fps` / 分辨率 marker 挂钩 | 日志必须打印所选 profile 与依据 |
| 音频 | `audio_bridge.c/h` | 只守救回点；抬帧刀不得回退 |
| 限帧 / present | `xbg_game.cpp` 等 gfx 路径 | 游戏模式验收；禁直接冲 60（honor 刀） |

---

## 4. 落地刀序（PM 派工）

1. **P-DIST-1** 傻瓜安装自动发现（内置+外置；机型无关扫描）  
2. **P-DIST-2** Device Profile 骨架（low/mid/high + 日志可归因 + 挂钩限帧/分辨率）  
3. **P-OPT-HONOR** mid 档：游戏模式 1080P 稳 ~30，再同窗冲 40；禁 60  
4. **双机回归**：同一 APK SHA + 同一数据包，RG477M(low) + 荣耀(mid)  
5. **40Hz 抬档**（每步复测音频）  
6. **手柄**（PS5/NS；与 profile 解耦）

并行规则：RG477M 基线目录 **只测/只留**；荣耀 **新会话 + 独立工作目录**；`adb -s` 锁序列号；禁 `adb kill-server`。

---

## 5. 引用句（复制到提词）

- 发行模型：对齐 `coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md`（Universal APK + External Game Data + Runtime Device Profile）。  
- 音频：验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。  

# 2026-11-14 分支归并及回归修复记录

用户要求本项目收敛到 `work/rg477m-native` 交付主干和 `task/T-codex-recovery-20261114` 开发分支，保留历史 APK。用户授权本轮使用 RG477M01445122，并已停止两个外部写者。

## 输入与恢复点

- Git bundle、各工作树 index/worktree 差分及未跟踪文件备份在 Honor 树 `.local/codex-recovery-20261114/branch-archive/`。
- 完整分支、提交和 patch 等价审查索引为该目录的 `branch-audit.json`。
- 用户确认角色正常的 MiniMax APK SHA256 为 `9083174963c40ce1c7d1726671fa2ffd4e00f573efad516d80def5b3a10cf881`。已从设备回读备份，构建源码对应 `ef33490` 加随后提交到 `ddeadec` 的源代码改动。
- Antigravity APK SHA256 为 `6eec4e94d3ff9cc7544c16b2fc6c793494103aab5986cdfdc2e4a9d05753f40f`，已单独备份。
- 两次采集之间 MiniMax 安装了新包，原 `perf-sample` 开关 A/B 作废，后续同包重测。

## 归并规则

历史提交全部保存在归并历史和 bundle。当前代码逐项选择有效实现，重复 patch 保留当前实现，过时协调记录作为历史输入留存。

| 来源 | 当前实现处置 |
|---|---|
| stability、sync、shared-texel-split、texelfix | 已有等价 patch，保留当前主干实现 |
| transit、rescue | 定向 event/readback 退休、预热节流和配方落盘已等价合入 |
| crash 分支 `92d672a` | 吸收非对齐 Event 字段 memcpy、8 字节对齐契约和可检索诊断 |
| `e4fe8ff` 粒子修复 | 吸收五组粒子 uint fetch 事实，补正常入口、其它 float binding 保持和真实 SPIR-V 验证 |
| `e4fe8ff` 全局 RGBA32UI 回退 | 保留原生 R16_UINT/R16ui，原始绑定与地址步长有直接证据 |
| `ddeadec` 深度修复 | 保留游戏原深度，撤回固定 depth bias。其新自动判定器存在 mixed-store、CompositeExtract 和 specialization 问题，本轮正常路径完整保留游戏深度 |
| `b6e487b` 多设备修补 | 采用主干已有严格事件同步和已确认音频行为，设备归因继续等待目标机日志 |
| `d19a037` 格式支持 | 按当前接口补 Rg32/Rg16 扩展格式能力门禁及重复类型处理，保留原生位宽和 shared split |
| 旧 moon/draw/report 专用诊断 | 现有 probe/ubtrace 已覆盖主要用途，完整代码保存在历史 |
| package 分支旧协调 WIP | 保存差分及报告，旧 state/HANDOFF 保留为历史资料 |

`3b6302a58b86a34a` 的原始绑定为 R16_UINT。两次 OpImageWrite 的 vec4 载体均为同值复制，不能由载体分量数推导为 16 字节 RGBA32UI texel。

## 当前验收状态

- 粒子宿主正常入口、ASan/UBSan、十份原始或重写模块 spirv-val 已执行通过。
- 角色正常依据用户对 MiniMax 包的反馈，本轮整合包待独立复测。
- 闪烁、卡顿、召唤及跨设备闪退继续定位和验收。
- 验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。

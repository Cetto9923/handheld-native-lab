# 常设验收｜已知音频坑（后续一切优化/装包提词必须引用）

> **日期**：Asia/Shanghai **2026-10-29**  
> **用户确认**：「这版本声音好了」（蓝牙 **AirPods Pro 3** / A2DP）  
> **状态**：常设硬门。后续任何优化、抬帧、装包、隔离实验提词 **必须引用本文件**；清单任一项未勾 = **整刀 FAIL**。  
> **引用句**：「验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`」

---

## 救回点（2026-10-29，勿回退）

- AAudio 默认 **`PERFORMANCE_MODE_NONE`**（禁默认 `LOW_LATENCY`；旧日志 `performance=10` 即踩坑）
- 按 burst 设缓冲：`setBufferSizeInFrames`（约 `burst*2`，扬声器侧可夹 ~1920–3840；蓝牙大 burst 须吃得下 A2DP 抖动）
- `AAudioStream_write` 超时约 **100ms**；`TIMEOUT` **重试**，禁止当致命失败停流
- `DISCONNECTED (-899)` / 设备变更 → **自动重开流**（`device_changed` + `AudioDeviceCallback`）；禁永久停 worker
- **`fence_spin_us` / `gfx-fence-spin-us` 默认 0**（忙等会饿死蓝牙 AAC/AAudio，扬声器往往仍「看起来还行」）

旁证 APK（非正式 RESULT）：`.local/rg477m-perf-20261028/perfisolated.apk` SHA-256 `5683f7e725978d0750899a2b6d3c392a607fdf756275ac1e308dd06fca4612d9`（以当场 `sha256sum` 为准）。

---

## 进度备注

- **音频门**：用户主观 PASS（蓝牙）。  
- **40Hz 未完成**（同窗曾见 ~21 fps present）；后续抬帧 / 冲 40 / 去主机限帧提词 **每一步** 必须完整过本清单后再谈帧率数字。  
- 更早「能出声」基线（40 字节 AudioOutBuffer ABI、Started/Stopped 极性）仍是硬前提，但不替代本清单。

---

## 已知音频坑（常设验收；任一不过 = 整刀 FAIL）

- [ ] 默认 AAudio 为 `PERFORMANCE_MODE_NONE`（禁默认 `LOW_LATENCY`）；guest 日志 `performance=0`
- [ ] 蓝牙耳机（A2DP）野外/战斗各 **≥2 分钟** 主观连续无断续；**仅扬声器顺滑不算过**
- [ ] 运行中连接/断开蓝牙：须自动重开流，禁因 `DISCONNECTED(-899)` 永久停音频或拖死 UE tick
- [ ] `fence_spin_us` / `gfx-fence-spin-us` 默认 **0**；若非 0，须蓝牙 A/B 且音频门不过则回滚
- [ ] AAudio write：`TIMEOUT` 可重试；禁「write failed → 停 worker / 拒收满队列」导致整局卡
- [ ] 缓冲不得为冲帧永久砍薄；抬 `gfx-target-fps` / 去主机限帧 **每步** 必复测蓝牙音频
- [ ] `xruns=0` 不能单独当 PASS；人耳断续即 FAIL（记「调度/缓冲滞后」）
- [ ] 记录包名 + APK SHA；确认桌面图标不是旧包
- [ ] 音频一坏：立刻回 30 限帧或已知顺滑 APK，复测 PASS 前禁止继续抬帧/冲 40/60

---

## 如何写入新提词

1. 开场或验收节写死：**验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`**。  
2. 写手开工先完整阅读本文件；抬帧/装包后按上表逐项勾选并写入 `RESULT.md`。  
3. 本文件条目与具体工单 §音频门 **叠加**（不互相替代）；冲突时以更严者为准。  
4. 勿删减本清单；若发现新坑，另开短刀追加条目并同步 worktree + 主树两份副本。

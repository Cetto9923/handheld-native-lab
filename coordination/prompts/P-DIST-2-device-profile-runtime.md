# 工单 P-DIST-2｜Device Profile 运行时档位（low / mid / high）

> **状态**：正式派工（新开短线程；禁止肥会话续聊）  
> **写手**：**Codex**  
> **对齐 SSOT**：`coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md` §2.3  
> **依赖**：可与 **P-DIST-1** 并行（文件所有权不重叠：本刀勿大改扫描逻辑；扫描归 P-DIST-1）。若自动发现尚未 PASS，装包验收前须先有可用发现路径（P-DIST-1 或既有候选）。  
> **设备**：荣耀 `adb -s A42BBB5109000333` 验证 **mid**；日志须能演示检测链。RG477M 映射为 **low**（本刀可用单元/日志论证；真机写入禁干扰基线测试）。  
> **工作树**：优先 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`；否则待 PM 指定 / `.worktrees/honor-dist-2`。  
> **并行隔离**：新会话；`adb -s` 仅荣耀；禁碰 RG477M；禁 `adb kill-server`。  
> **音频**：验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。  
> **硬禁止**：不改数据包布局；不为每机拆 APK；不 push；不 `reset --hard`；抬档须可回滚。

---

## 0. 目标

落地 **Device Profile** 自动选择 + **日志可归因** + 与 **`gfx-target-fps` / 分辨率** 挂钩。

默认表（结构固定；数字可在 RESULT 注明微调，但不得拆包）：

| profile | 线索 | 分辨率 | 目标 Hz（游戏模式） |
|---|---|---|---|
| low | 天玑/中低 Adreno/小屏如 RG477M | 720P | 先 30 再 40 |
| mid | 8s Gen3 / Adreno735 如荣耀 GT Pro | 1080P | 先 30 再 40 |
| high | 8 Elite / Adreno830 如奥丁3 | 1080P+ | 40 优先，60 后置 |

检测顺序：**显式 override → SOC/GPU 字符串表 → 屏幕短边/内存启发式 → 默认 low**。

---

## 0.1 开工前

1. 读本文件 + SSOT + `_STANDING-audio-acceptance.md` +（若存在）P-DIST-1 RESULT。  
2. 摸清现有限帧 / 分辨率入口：`xbg_refresh_target_fps`、`gfx-target-fps` marker、相关 Java/native 设置。  
3. 查公开案例（手游质量档、Unreal scalability、模拟器 per-device preset）→ `OSS-NOTES.md`。  
4. 所有权：不与 P-DIST-1 同时改 `GameActivity` 扫描函数；本刀新建 profile 模块或窄接口优先。

---

## 1. 允许

- 新建 Device Profile 模块（Java 与/或 native）；启动时选择并打日志：`profile=`、`reason=`、`soc=`、`gpu=`、`short_edge=`。  
- 将所选 profile 映射到分辨率与 `gfx-target-fps`（mid 默认锁 30 直至 P-OPT-HONOR 抬档）。  
- 调试 override（设置项或 marker）；默认走自动。  
- 文档化字符串表（可配置表文件）。

## 2. 禁止

- 改 `main.nso` / `romfs` 布局或内容。  
- 按 profile 产出不同正式 APK / 不同数据包。  
- 本刀直接冲 60 或把 high 的 60 当荣耀验收。  
- 无 OSS 出处的大范围渲染重写。  
- 回退音频救回点。

---

## 3. 验收清单

- [ ] 荣耀真机日志：`profile=mid`（或等价）+ 选择依据可读  
- [ ] override 可强制 low/mid/high 并在日志体现  
- [ ] 默认分辨率 / target-fps 与表一致（mid→1080P + 30 起步）  
- [ ] 数据包布局未改；自动发现未被破坏  
- [ ] `OSS-NOTES.md` 有 URL/commit  
- [ ] 常设音频：源码未回退；进游戏则全勾  
- [ ] RESULT 含 APK SHA（若产出 APK）  
- [ ] 未碰 RG477M；未 push

---

## 4. 交付

1. `.local/dist-2-device-profile/OSS-NOTES.md`  
2. `.local/dist-2-device-profile/RESULT.md`  
3. HANDOFF 短摘要  
4. 停；交 P-OPT-HONOR 用 mid 档冲稳 30

---

## 5. SESSION_OPENER（可粘贴给 Codex）

```
刀号 P-DIST-2-device-profile-runtime｜写手=Codex。
对齐 SSOT：coordination/docs/ANDROID-PORT-DISTRIBUTION-BEST-PRACTICE.md §2.3 Device Profile。

工作树：优先 /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro；否则待 PM 指定或 .worktrees/honor-dist-2。可与 P-DIST-1 并行但文件所有权不重叠（本刀勿大改扫描）。

设备：荣耀 adb -s A42BBB5109000333 验证 mid。禁碰 RG477M；禁 adb kill-server。

目标：落地 low/mid/high 自动选择 + 日志可归因（profile/reason/soc/gpu）+ 挂钩 gfx-target-fps 与分辨率。检测顺序：override → SOC/GPU 表 → 短边/内存启发式 → 默认 low。禁改数据包布局；禁按机拆 APK。

默认表：low=RG477M类 720P/先30；mid=荣耀Adreno735 1080P/先30；high=奥丁类 1080P+/40优先60后置。mid 起步锁 30，冲帧交给 P-OPT-HONOR。

验收必须完整包含 coordination/prompts/_STANDING-audio-acceptance.md。写 OSS-NOTES（URL/commit）；无出处大改=FAIL。不 push。完整提词：coordination/prompts/P-DIST-2-device-profile-runtime.md
```

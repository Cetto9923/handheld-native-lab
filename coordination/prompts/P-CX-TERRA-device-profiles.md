# 工单 P-CX-TERRA-device-profiles｜Codex Terra：运行时 device profile 档位引擎

> **状态**：正式可派（复杂刀）  
> **写手**：**Codex**  
> **模型**：**GPT-6.1 Terra（高）**  
> **产品真源**：`coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md`（必读 §0.1 演进原则 + §3 档位）  
> **音频硬门**：验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`（新树若无则从基线复制）  
> **依赖**：建议 P-AGY-INSTALL-autodiscover 的发现路径至少可用（或明确沿用现候选）；本刀不重做安装器  
> **工作树**：优先 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`；未就绪则独立 worktree/分支，**禁止**与 RG477M 正在测的脏树双写  
> **设备**：实现可机型无关；真机验证至少覆盖档位 `handheld_mid`（对照文档/日志）与 `tablet_sd8s`（荣耀 `A42BBB5109000333`）。操作荣耀时 **only** `-s A42BBB5109000333`；禁 kill-server；禁抢 RG477M 前台  
> **日期**：Asia/Shanghai **2026-10-31**

---

## 0. 演进原则（硬｜用户 2026-10-31）

1. **站在 RG477M 可玩基线上演进**；承认现码多 BUG；基于现码/现 APK/数据模型扩展。  
2. **禁止闭门造车 / 为干净架构推倒重来**；禁重写无关子系统。  
3. 遇事先查业界/开源/社区，写 `OSS-NOTES.md`（URL/commit）；**无出处大改 = FAIL**。  
4. **复用钩子**：`gfx-target-fps` / `FFRS_TARGET_FPS` / `xbg_refresh_target_fps`；音频救回点不动退；安装发现沿用 `resolveGameDir` 族。  
5. **先对照基线行为再改荣耀**：profile 选出的 720P/30 不得无故破坏 RG477M 已可玩路径。

---

## 1. 目标

实现 **device profile** 选择：

1. **自动探测** SoC / GPU / 分辨率（短边）/ 刷新率（能读则读）。  
2. **Preset 表**（最小）：
   - `handheld_mid` ≈ RG477M → 720P 口径 / 目标 **30**（后置 40）  
   - `tablet_sd8s` ≈ 荣耀 GT Pro → 1080P 口径 / 目标 **30**（后置 40）  
   - `odin_elite` 预留槽（可先占位 JSON，不必真机过）  
3. **用户可覆写**（设置或 marker）；覆写写入日志/RESULT。  
4. 把目标分辨率/fps **接到现有** `gfx-target-fps` 等；**禁默认 60**；音频不回退。  
5. 默认验收假设 **游戏模式**（非狂暴）；狂暴档数字作废。

---

## 2. 不做

- 不重做 NVN 全栈；不重做音频栈。  
- 不把本刀做成「每机型一个 APK」。  
- 不在本刀冲 60；不同窗 30 PASS 前不把 40 当成功。  
- 不双写基线 RG477M 测树；不 push；不改 main。

---

## 3. 验收清单

- [ ] 读过最佳实践 §0.1/§3 + standing audio  
- [ ] 自动选择：在荣耀上落到 `tablet_sd8s`（或等价），`gfx-target-fps`（或日志）为 **30** 而非 60  
- [ ] 文档/测试说明 `handheld_mid` 映射与 RG477M 对照一致（可不在本刀打断 RG477M 测；可用日志/单元/桌面说明）  
- [ ] 用户覆写路径存在且可还原  
- [ ] 音频救回点未回退（NONE / fence_spin=0 等 standing 勾选）  
- [ ] OSS-NOTES 有出处（若引入新探测 API / 新配置格式）  
- [ ] RESULT + HANDOFF；停

---

## 8. SESSION_OPENER（可粘贴给 Codex Terra）

```
刀号 P-CX-TERRA-device-profiles｜写手=Codex｜模型=GPT-6.1 Terra（高）｜产品真源 coordination/product/FFRS-ANDROID-PORT-BEST-PRACTICE.md §0.1+§3。

演进硬约束：站在 RG477M 可玩基线上扩现码；禁闭门造车/推倒重来；遇事先查 OSS 写 OSS-NOTES（URL/commit）；无出处大改=FAIL。复用 gfx-target-fps/FFRS_TARGET_FPS/xbg_refresh_target_fps；音频救回点不回退；安装发现不重做（依赖/沿用 resolveGameDir）。

工作树优先 /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro（或独立分支）；禁止与 RG477M 正在测的脏树双写。

实现：自动探测+preset（handheld_mid≈RG477M 720P/30、tablet_sd8s≈荣耀 1080P/30、odin_elite 预留）+用户覆写；接到现有限帧/分辨率钩子。禁默认 60；默认游戏模式非狂暴。

验收必须完整包含 coordination/prompts/_STANDING-audio-acceptance.md。真机操作荣耀时 adb -s A42BBB5109000333 only；禁 kill-server；禁抢 RG477M 前台。完整提词：coordination/prompts/P-CX-TERRA-device-profiles.md
```

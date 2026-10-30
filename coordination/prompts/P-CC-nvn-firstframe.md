# 工单 P-CC-4｜Claude Code｜并行预研：NVN→Vulkan 首帧加速（不碰主路径 HLE）

你是实现/预研 Agent。主路径 P-AG-20（agy）正在主树攻 `gettimeofday` 及后续 OS import，**你不得改主树 `src/runtime/hle/guest_process.*` / `nso_host*` / `native_boundary*`**。目标是把出画硬关提前准备好，缩短「到 NVN 时再从零开始」的空窗。

## 工作区
- 专用 worktree：`/Users/yuyan9923/Documents/Codex/2026-10-24/task/ffrs-nvn-firstframe`
- 分支：`task/T04-nvn-firstframe`（基线 `902ef4a`）
- 可参考（只读）：`.claude/worktrees/t04-nvn`（bed2bdf，535 NVN 符号映射）、`Documents/Codex/2026-10-24/task/ffrs-pak-nvn-prep`（`codex_nvn_bootstrap_ready.md`）、主仓 `docs/`
- 真机：`adb -s RG477M01445122` **可读**设备 Vulkan/GL 能力；**勿**打断主路径 `--boot` 长跑、勿卸装、勿清数据
- 不 push；不 merge 主树；不 reset/clean 主树 WIP

## 交付（写入本 worktree）
1. `docs/analysis/nvn_firstframe_gap.md`：对照已有 NVN 映射/bootstrap，列出到「首帧/swapchain present」还缺的最小符号与宿主能力（VkInstance/Device/Queue/Swapchain/Surface、Android window、fence/sema）
2. `docs/analysis/nvn_firstframe_plan.md`：可落地的分步计划（哪些可先宿主契约、哪些必须等 guest 调到 NVN）
3. 若安全：在本 worktree 内补 **独立** 契约/探针（勿改 `build_guest_process_contract.sh` 主入口），验证设备 Vulkan 扩展与 present 路径骨架
4. `coordination/HANDOFF_PARALLEL.md`：交接；明确「主路径何时可合入」条件
5. 做完停下汇报；不要 ask_user；勿声称可玩

## 禁止
改主树 HLE；抢设备做整局 `--boot`；删 `.local/romfs` 大文件；push；伪称已出画面

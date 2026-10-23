# 工单 P-CC-2｜Claude Code｜并行预研：UE Pak/IoStore 读路径 + NVN bootstrap 集成清单

你是实现/分析 Agent。主路径由 Antigravity **P-AG-13** 在主树 `work/rg477m-native` 攻 `nn::os::InitializeEvent`；你做**不重叠**的并行预研，给后续首帧/标题加速。

## 工作树与约束
- **只在本 worktree 工作**：`/Users/yuyan9923/Documents/Codex/2026-10-24/task/ffrs-pak-nvn-prep`，分支 `task/T04-pak-nvn-prep`（基线 `79721fc`）
- **禁止**改主树 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` 的未提交 WIP；禁止碰 `src/runtime/hle/guest_process.*`、`native_boundary.*`、`nso_host*`（主路径专属）
- **禁止** adb/真机操作（设备由 P-AG-13 独占）
- 不 reset/clean/stash 覆盖；不 push；不清数据；不 root
- 密钥/大二进制只留 `.local/`；验收不降级：不声称标题/可玩
- 模型：按项目约定用 Fable（若 CLI `--model` 可用则指定）

## 先读
- `AGENTS.md`、`coordination/HANDOFF.md`（只读了解前沿）
- `docs/analysis/asset_inventory.md`、`docs/analysis/nvn_surface.md`
- `.claude/worktrees/t04-nvn` 内已有 NVN 分析与 `src/graphics/nvn/`（可只读对照，**不要**改那个 worktree；需要代码时在本 worktree 复制/新增）
- 真机日志样例（只读）：主仓 `.local/logs/rg477m-boot-v28-opendirectory.log` 尾部（了解 FS 已过到哪）

## 目标（按序，可交付文档+可选骨架，不做主路径 HLE）
1. **Pak/IoStore 读路径预研报告**（写到本 worktree `docs/analysis/codex_pak_iostore_prep.md`）：
   - 从 UE5 Switch/NVN 启动链推断：越过 OS Event / 更多 FS 后，预计会打开哪些 `rom:/` 大包（Pak/IoStore/ucas/utoc）
   - 对照 `asset_inventory` 与宿主 romfs 目录，列出「已有 / 缺失」文件表（缺失只记录，不下载未授权本体）
   - 给出最小 HLE/宿主映射建议：`OpenFile` 大文件、顺序 Read、可选内存映射策略；与现有 `nn::fs::*` HLE 如何衔接（接口清单，不直接改主树）
2. **NVN→Vulkan 集成就绪清单**（写到 `docs/analysis/codex_nvn_bootstrap_ready.md`）：
   - 基于已有 T04-nvn 结论，列出主路径要接到 `nvnBootstrapLoader` 时的最小步骤与依赖（Display/Vulkan 实例是否已在 Android 侧就绪）
   - 在本 worktree 可选：补最小 `nvnBootstrapLoader` 宿主契约测试骨架（编译能过即可），**不要**声称真机出帧
3. 更新本 worktree 内简短 `coordination/HANDOFF_PARALLEL.md`：完成项、未完成项、给主路径的「何时合入」建议
4. 本地提交检查点到 `task/T04-pak-nvn-prep`；**不 merge、不 push**；回报：HEAD、报告路径、是否缺素材

## 禁止
- 不抢主树、不抢设备、不改 guest_process/native_boundary
- 不伪造游戏包/密钥；不把大二进制提交进 git
- 不声称标题/可玩

做完停下汇报，等项目经理；不要 ask_user。

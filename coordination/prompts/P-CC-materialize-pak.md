# 工单 P-CC-3｜Claude Code｜物化缺失 Pak/IoStore 容器到宿主 romfs

你是实现 Agent。主路径 P-AG-14 在攻 `posix_memalign`（主树）；你只做素材物化，不碰 HLE。

## 事实
- 用户自购 NSP 在：`/Users/yuyan9923/Downloads/SwitchGame/[最终幻想：共鸣][FINAL FANTASY RESONANCE]/[本体v1.0.0][0100B8A026F14000][22.1.0][中文][HK].nsp`（约 8.4GiB）
- 密钥：仓库 `.local/keys.txt`；tik/hdr：`.local/nca/base.tik`、`.local/nca/program_hdr.bin`
- 宿主 romfs：`.local/romfs/`（已有 `FFRS-Switch.utoc` / `global.ucas` / `global.utoc` 等）
- **缺失硬需求**：
  - `.local/romfs/FFRS/Content/Paks/FFRS-Switch.pak` ≈ 2,958,593,334 B
  - `.local/romfs/FFRS/Content/Paks/FFRS-Switch.ucas` ≈ 5,976,848,928 B
- 已有流式读取器：`scripts/extract/romfs_stream.py`（`RomFSStream.read_file_data`），但目前 CLI **不会**把大文件落到磁盘
- 磁盘可用约 31GiB，够写 8.32GiB；**禁止全量解包整个 RomFS**
- 参考：`docs/analysis/asset_inventory.md` 与并行报告（worktree）`/Users/yuyan9923/Documents/Codex/2026-10-24/task/ffrs-pak-nvn-prep/docs/analysis/codex_pak_iostore_prep.md`

## 工作约束
- 仓库根：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`
- **允许**：在 `scripts/extract/` 增加小脚本（如 `materialize_paks.py`）调用现有 `RomFSStream`，分块写出上述两个文件到 `.local/romfs/...`；写证据到 `.local/`（size + SHA-256）
- **禁止**：改 `src/runtime/hle/*`、`native_boundary*`、抢真机 adb；不 push；不把 `.pak/.ucas/keys` 提交进 git；不全量解包；不声称可玩
- 模型：Fable（若可用）

## 目标
1. 实现并运行定向物化：只抽 `FFRS-Switch.pak` + `FFRS-Switch.ucas` 到正确 romfs 路径
2. 写出 `.local/logs/materialize-pak-<ts>.log`：每文件 size、SHA-256、耗时；与预期 size 比对
3. 可选：跑已有 `scripts/runtime/build_pak_read_path_probe.sh`（若存在）确认 OpenFile/GetFileSize/定位读从 PathNotFound 变为成功
4. 简短回报路径与校验结果；**不要** merge 到主路径 HLE；脚本可本地提交（不含大文件）

做完停下汇报；不要 ask_user。

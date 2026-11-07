# FFRS · 荣耀平板（GT Pro）移植工作区

本目录是 **与 RG477M 基线隔离** 的荣耀移植项目，由基线仓库的 git worktree 创建。

## 基线关系

- **基线仓库（勿在此乱删数据）：** `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`
- **账本：** `BASELINE-2026-10-31.md` / `BASELINE.lock`
- **分支：** `work/honor-gtpro-30hz`（自可玩线提交 `032e5f6` / `task/T02-codex-black-screen`）
- **游戏数据：** `.local/romfs` → **符号链接** 到基线  
  `.../ff-resonance-rg477m-native/.local/release-pack-20261028/FFRS/romfs`  
  **禁止删除基线 romfs**；本目录删除链接不会删数据，但删基线会断链。
- **APK 快照：** `.local/apk-baseline/` + `APK-MANIFEST.md`

## 设备铁律

- ONLY：`adb -s A42BBB5109000333 …`
- 禁止：`RG477M01445122` / 任何 RG477M 序列号
- 禁止：`adb kill-server`（会打断并行的 RG477M 验收）
- 禁止：卸载/覆盖 RG477M 上已装可玩包

## 建议开场

新开 **独立 Codex 会话**，cwd 指到本目录，粘贴  
`coordination/prompts/P-OPT-HONOR-GTPRO-30hz-game-mode.md`  
并遵守 `coordination/prompts/_STANDING-audio-acceptance.md`。

不要复用 RG477M 线程 `01a186a6-8745-7220-b158-0730044d3f2f`。

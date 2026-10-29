# 给 Codex 的任务 v2：遮挡查询 A/B —— 改用「驻留场景两次独立运行」法

> 接上一版 `P-CODEX-occ-ab-20261026.md`。上一版用**过场**做进程内交替 A/B **失败了**：水晶旁白只有十几秒、按挂钟推进，12 秒的交替周期套不住稳定的 on/off 两相位，off 相位一个合格样本都没采到。前因后果见上一版文件开头的「今天发生了什么」，这里不重复。
>
> **教训（务必记住）**：过场永远不适合做 A/B——它随挂钟漂移，不同次运行停在不同剧情。本版改用**驻留场景 + 两次独立冷启动**，彻底绕开对齐难题。

---

## 一、方法变更

- 不再用 `gfx-ab-toggle` 交替框架。
- 改用引擎的**独立永久开关 `gfx-no-occlusion`**（`xbg_game.cpp:654`）：写 `'1'` 则整程关闭遮挡查询，不交替。
- 在**同一个「驻留」场景**（不随挂钟推进、draws 高、角色静止）里跑**两次独立冷启动**对比：
  - run1：无 `gfx-no-occlusion` 标记（基线，遮挡查询开）
  - run2：`gfx-no-occlusion=1`（遮挡查询关）
- 两次走**同一导航路径、停在同一点、静止不动**，各采 PERF 60 秒，按 draws 桶取稳定段比较。

**为什么驻留场景行**：它不随挂钟推进，两次运行能停在同一状态，draws 稳定，没有过场漂移。

---

## 二、驻留场景的选择

进游戏后**第一个可站立的 3D 野外**即可——**即使它仍是黑屏也行**，因为：
- 黑屏期间 draws 照样高（账本记录：野外基线 draws≈337-359，fps≈4.0），PERF 照记；
- 角色站着不自动推进，是稳定驻留；
- 注意：该野外约 6 分钟后会因 `-5` 崩溃（无片元着色器 draw），所以**每次只采 60 秒，安全**。

如果第一个野外进不去或立刻崩，退而求其次用任何一个「角色能站定、draws≥300、不自动播片」的场景，但 run1/run2 必须是**同一个**场景同一个点。

---

## 三、环境

- 设备 adb serial：`RG477M01445122`（已连，USB）
- 包名：`org.ffrs.gamewindow`，标记目录 `/sdcard/Android/data/org.ffrs.gamewindow/files/`
- 当前设备上的 APK = 改动 A+B（勿重装、勿编译）
- 按键：97=confirm, 96=cancel, 109=Select/skip, 19/20=上/下，方向/移动用 `adb shell input keyevent` 或 `input swipe`（若需移动角色）
- 产物目录：`.worktrees/codex-black-screen/.local/kiro-runs/ab/41-noocc-tworun/`

---

## 四、步骤

**run1（基线，遮挡开）：**
1. 确认标记不存在：`adb shell ls .../files/gfx-no-occlusion` 应 No such file（若在则 rm）。
2. 冷启动：`am force-stop` 后 `am start`。
3. 导航到驻留野外：New Game → 用 109/97 跳过过场 → 进入可站立野外，**停下不动**。
4. 截 1 张图确认是野外（非过场、非标题）。
5. 原地静止，`adb logcat` 抓 `NVN PERF` 行 **60 秒**。日志存 `run1-perf.log`，截图存 `run1/`。

**run2（遮挡关）：**
6. 写标记并读回验证：`printf '1' > f; adb push f .../files/gfx-no-occlusion; adb shell cat .../files/gfx-no-occlusion` 必须回显 `1`。
7. 冷启动，**走与 run1 完全相同的导航路径、停在同一点**。
8. 截 1 张图确认同一野外。
9. 原地静止抓 `NVN PERF` **60 秒**，存 `run2-perf.log`，截图存 `run2/`。

**解析与还原：**
10. 每行 PERF 带 `draws / fps / gpu_ms / passes / op5(ReportCounter) / op27(Dispatch) / op28(Barrier)`。两 run 各取 draws 落在**同一桶**（例如都取 draws 300-400 的稳定段；以 run1 实际 draws 为准，run2 取最接近的同桶）。
11. 输出对比表：run1 vs run2 的平均 `fps / gpu_ms / passes / op5 / op27 / op28`，各注样本数与所用 draws 桶。
12. 还原：`adb shell rm .../files/gfx-no-occlusion`，`ls` 确认 ABSENT。

---

## 五、严禁

- 改 `xbg_game.cpp` 或任何源码、重编译、`git add/commit/push`。
- **伪造或内插数据**；两 run 停在不同场景/不同点就如实报「场景没对上」并重试，不要硬凑。
- 拿**过场**或不同停留点的数据混比——这正是上一版失败的原因。
- 用等待时长冒充场景对齐。

---

## 六、交回

- 对比表 + run1/run2 原始 PERF 日志路径 + 两 run 各一张野外截图路径。
- 结论：**遮挡关闭后 op5 pass_ends 是否显著下降、fps 是否上升**（判定遮挡查询是否为 FFRS 切 pass 主因）。
- 若有效：建议下一步 = FFRS 负载下给遮挡查询加批处理/合并（上游 `occlusion_batch_enabled` 机制可借；或在特定 pass 跳过）。
- 若无效或 draws 对不上：如实说明，Kiro 会转测 `gfx-query-per-draw`（关遮挡批处理）、`timestamps`、`wbskip`、`noload`——**都用本版的「驻留场景两次独立运行」法，不再用过场交替**。

---

## 七、边界（沿用 AGENTS.md 与今日约定）

- 只在 `.worktrees/codex-black-screen`（task/T02-codex-black-screen）工作；不 push、不碰 main、不动主树脏改动。
- 产物进 `.local/kiro-runs/ab/41-noocc-tworun/`，不进 git。
- 一切以真机实测为准；外部资料非授权，不改游戏身份校验。

# P-CODEX-black-markers-v4 (2026-10-26) 黑屏：用现成标记做单变量实验

只用设备标记，不改源码、不编译、不碰 git、不开子任务。读 xbg_game.cpp 只用 grep -n + sed -n 'A,Bp'，每次 <=150 行。

## 为什么做这个（你记忆里没有的今天结论）
- v3b 静态读码已结束：GBuffer 的 VkFormat / ImageView / layout 在已查范围内一致，没有不匹配（NOTES.md 43）。静态路线不再继续。
- 引擎是 d2r-mobile 的 NVN->Vulkan 渲染器。它有一批"Android 宿主对 D2R 默认置 1"的标记，FFRS 没有置，设备上现在都不存在，等于全关：
  - gfx-nan-minmax（xbg_game.cpp 约 1993-1999 行注释）：Maxwell 的 min/max/saturate 遇 NaN 取另一个操作数；翻译器出的 FMin/FMax/FClamp 遇 NaN 在 Vulkan 里未定义，驱动可能把 NaN 传下去，写进 UNORM 目标就是黑。UE 延迟渲染的 GBuffer/光照里 NaN 很常见。**这条与"3D 全黑、UI 正常"吻合，优先级最高。**
  - gfx-quad-derivatives（约 1953 行）：把 quad shuffle 换成细粒度导数。UE 后处理/光照用到时，关掉会算错。
  - gfx-warp32（约 1970 行）：宽 subgroup 上模拟 32 线程 warp。
  - gfx-forward-temps、gfx-ldc-fold：翻译器输出清理，次要。
- 另有 gfx-bright（默认开，约 3239 行起）：D2R 专用的色调映射 LUT 改写，只在 compute shader 552f45c348a61b86 之后触发。FFRS 的 shader 哈希不同，理论上不命中；需要确认 `bright applied` 计数为 0，排除它在 FFRS 上误写。

## 实验（每次只改一个变量，各自冷启动，走你 run41/42 用过的同一路径到同一"战场出生点"，静止 30 秒）
- E0 基线：当前设备标记不动。记录启动日志里这几行的值：`NaN-ignoring min / max`、`quad shuffles to fine derivatives`、`32-lane warps`、`translator output clean-up`、以及任何 `bright` 行。
- E1：只加 gfx-nan-minmax = 1。
- E2：只加 gfx-quad-derivatives = 1。
- E3：只加 gfx-warp32 = 1。
- E4：只加 gfx-bright = 0（A 项：确认 bright 是否影响；若 E0 里 bright applied=0 可跳过并写明）。
- E5：E1+E2+E3 同时开。

标记写法：echo -n 1 | adb push 到 /sdcard/Android/data/org.ffrs.gamewindow/files/<名字>，cat 读回字节确认；每次实验后删除并确认 ABSENT。
每次实验必做：
1. 确认启动日志里对应行显示 =1（标记真被读到）。若翻译结果有缓存、改标记后行为没变，先查缓存键是否包含该标志，如不含，说明并清该缓存（只清缓存目录，不动存档/游戏包），再重测。
2. 到出生点后截图，用同一方式算画面平均亮度（上次黑屏≈0）和"非 UI 区域是否有非黑像素"。把截图路径与亮度数字一起记录。
3. 记 draws 桶、fps、是否有 FATAL / -5。

## 纪律
- 没有可见画面变化就如实写"无变化"，不要把亮度噪声当成功；有变化就保留截图原图。
- 每 10 分钟追加 NOTES.md（.local/kiro-runs/44-black-markers/NOTES.md）：实验编号、标记值、日志确认行、亮度。
- 40 分钟内给出结果，没做完的实验标 NOT_RUN。
- 不扩大范围到性能、遮挡、屏障。

## 交回
一张表：实验 | 标记 | 日志确认 | 出生点亮度 | 是否出现 3D 画面 | 截图路径。
结论：哪个标记（或组合）让 3D 世界出现了画面；若全部无变化，列出 E0 日志里这些开关的实际值。

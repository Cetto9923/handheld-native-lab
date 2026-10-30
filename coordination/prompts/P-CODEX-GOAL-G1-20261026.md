# P-CODEX-GOAL-G1：目标模式（长任务，自主推进）2026-10-26

你在目标模式下自主推进，直到达成目标、被阻塞、或到达阶段上限。不需要逐步向我确认。遇到下面“必须停下问用户”的情形才停。

## 1. 目标 G1（用户已批准，按顺序）
A 画面正常：New Game → 开场 → 第一个 3D 野外，3D 世界可见（对照小地图教学页：燃烧废墟 + 石板路 + 角色），不再黑屏；标题/菜单/过场不退化。
B 性能：固定 30fps（docs/PROJECT.md 首个可玩版本口径：真实移动场景连续 5 分钟，平均 >=29，P95<=35ms，P99<=50ms，>100ms 长帧每 5 分钟 <=1 次；冷/暖缓存分开测；指标来自原始帧时间戳）。60fps 是之后加分项，不承诺。
C 能玩下去：能自由移动、对话、进入战斗，连续试玩不崩溃。
约束：保持游戏逻辑/动画/音频速度，不靠加速或跳帧达标；不静默降画质（分辨率适配必须记录实际 viewport/render target；清晰度优先取能达标的最高档 1280x720 > 960x540 > 854x480）；不伪造 signal，不伪造完成。
**验收权在用户**：画面是否正常由用户本人目视最终确认。你只能写“证据显示 X，待用户目视确认”，不能写 PASS。

## 2. 前因后果（你记忆里没有的今天内容）
- 设备 RG477M01445122 已 USB 连接，已授权全程占用。包名 org.ffrs.gamewindow。标记目录 /sdcard/Android/data/org.ffrs.gamewindow/files/。
- 真正的渲染引擎是本工作树里**未被 git 跟踪**的 src/graphics/nvn_vulkan/engine/（d2r-mobile 的 NVN→Vulkan HLE 渲染器，约 4448 行 xbg_game.cpp，515KB）。T04 旧分支“replay 只解码不执行”的判断已作废。
- 黑屏现场：右上小地图与左下“小地图/系统菜单”UI 正常，3D 世界亮度≈0。标题、Option 菜单、水晶旁白、月夜废墟过场画面是真实的。
- 你自己已查清（NOTES 42/43）：无 FS 的 draw 全是合法纯深度管线；六 MRT 颜色管线 0% 缺 FS；-5 是每 draw 即时的合法性检查；GBuffer 的 VkFormat/ImageView/layout 在已查范围内一致。静态读码路线已到头，**下一步必须靠运行时证据**。
- 我（Kiro）今天对引擎源码做了两处改动，当前设备上装的就是 A+B 的 APK（c24a0153a8f60b2c）：
  - 改动 A：xbg_game.cpp 启动读标记处，read_preflight 默认 true→false（标记/环境变量仍可开回）。效果：开场过场 fps 2.9→7.5，GPU 122→62ms。
  - 改动 B：Commands 构造函数在“自上次非 draw finish() 屏障后没有任何命令被记录”时跳过构造屏障（perf.ops==last_finish_ops；finish() 里设置 last_finish_ops，submit_slots 里与 barrier_seq=0 一起重置）。效果：过场 fps 7.50→7.79，很小。
  - **回滚备份**：.local/backup-20261026-kiro/xbg_game.cpp.afterAB（sha256 e644683353c68eae…，与当前源码逐字节相同）。注意：我早先的 src-backup/.orig、afterA、build 日志、marker-backup、ab_to_field.py、minimap_detect.py 等**已不在 .local/kiro-runs 里（.local 在 18:18 左右被清过，原因未知）**。不要依赖它们存在；也不要删除任何不是你自己创建的 .local 内容。
- 性能事实（改动 A+B，水晶旁白过场 draws≈580-590）：fps≈7.8，GPU≈58ms，每帧约 15 个 pass，ReportCounter(op5) 是切 pass 第一号原因，遮挡查询≈100/帧，compute dispatch≈134/帧且每个都结束 pass+全屏障。标题 fps≈16。距 30fps 还差 2~4 倍。
- 在黑屏场景测 fps 意义有限（3D 主体没上屏）。**先修黑屏，再做性能。**
- 引擎自带的开关（都是标记文件，写法：adb push 后 cat 读回确认字节，并核对启动日志里对应行的值）。D2R 宿主默认置 1、FFRS 宿主没置的有：gfx-nan-minmax（UNORM 目标遇 NaN 变黑，**与本黑屏症状高度吻合，最优先**）、gfx-quad-derivatives、gfx-warp32、gfx-forward-temps、gfx-ldc-fold。gfx-bright（默认开）是 D2R 专用色调映射 LUT 改写，需确认它在 FFRS 上 applied=0。
- 引擎内置 A/B 框架：files/gfx-ab-toggle=<周期秒数>，files/gfx-ab-target=<开关名>，进程内交替；开关名如 occ/query/timestamps/wbskip/noload。**不要在过场上做 A/B**（过场按挂钟走，已两次失败）；要用驻留场景、两次独立冷启动、按 draws 桶对齐。
- 引擎有 capture 设施（xbg_game.cpp 约 161-180 行：capture_begin/capture_data 把数据写 .bin，64MiB 预算），可作为运行时转储的现成手段。
- 另一个已知问题：野外约 6 分钟后 FATAL “Invalid graphics stage/depth-only state: -5”，原因未定，本轮未复现。
- 外部先例：LAS/BOTW/Super Mario Wonder 的 Recomp Android 项目与本项目同路线，仅供思路参考，**不要再开网络调研、不要 clone 它们**。

## 3. 授权与边界
- 用户授权：修改引擎源码、重新编译 APK、安装到 RG477M，风险可接受。
- 只在本工作树 /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/codex-black-screen（分支 task/T02-codex-black-screen）改动。不 push，不碰 main/master，不动主树脏改动，不 git add .（只提交你自己改的、非敏感文件；engine 目录未被跟踪，用备份而不是 git 回滚）。
- 不改游戏包、存档、其他项目；不读无关凭据；不下载新的游戏/固件/密钥。
- 不编辑 coordination/state.json、coordination/HANDOFF.md、STATUS.md（总控的唯一写者是 Kiro/用户）。你只写：.local/kiro-runs/50-goal/ 下的 LEDGER.md 与证据，以及代码。
- 构建：在工作树根目录 `CMAKE_BUILD_PARALLEL_LEVEL=1 bash scripts/android/build_game_window_apk.sh`（上次所有步骤 exit 0）。主机内存紧张，**不并行编译，不开子任务**。
- 每次改源码前：cp 到 .local/backup-<时间>-codex/ 并记 sha256；改完 cmp/diff 留档；安装 APK 前后记录新旧 APK sha256 和 native 库 sha256；用 `adb shell dumpsys package` 或库 sha 证明设备跑的是你这次构建。

## 4. 防卡死纪律（今天 Codex 一次空转 51 分钟，我也在大文件上卡过）
- xbg_game.cpp 共 4448 行，**禁止整文件读取**；只 `grep -n` 定位，再 `sed -n 'A,Bp'`，每次 <=150 行。
- 每 10 分钟向 .local/kiro-runs/50-goal/LEDGER.md 追加一条：时间、阶段、做了什么、命令、得到的事实、下一步精确命令。**10 分钟没有新条目就是卡住了：立刻停下，写明卡在哪，换一个更小的子问题。**
- 每个单项实验设上限（冷启动→到场景→采样 <=15 分钟）。超时就记 NOT_RUN/BLOCKED 并继续下一项，不要死等。
- 截图与读取截图不要放在同一批并行调用里（读取时文件还不存在）。写设备标记必须读回字节。判断游戏阶段只按内容（draws 桶、截图特征），不按等待时长；任何画面检测器先用已知正/负样本离线验证再上设备。
- 不信旧报告的结论和数字，核实后再用（尤其“真实帧数=0”“已 PASS”之类）。

## 5. 路线图（按阶段推进，每阶段有出口条件）

### P0 现场核对（<=15 分钟）
git status、分支、HEAD；确认源码 sha256 == e644683353c68eae…；设备在线；列出设备上 gfx-* 标记及内容（预期只有 gfx-codex-audio-dump=1、frame-metrics=1、gpu-step=0、movie-dump=0、legacy-waitall=1、readback-cached=0）；记录已装 APK 与 native 库 sha256。找到并复用你 run41/42 用过的“冷启动→New Game→跳过→战场出生点”导航与采样脚本；脚本写进 .local/kiro-runs/50-goal/ 并先离线验证检测逻辑。

### P1 修黑屏（目标 A）。上限 3 小时
步骤 1：按 coordination/prompts/P-CODEX-black-markers-v4-20261026.md 做单变量标记实验（E0 基线、E1 nan-minmax、E2 quad-derivatives、E3 warp32、E4 bright=0、E5 E1+E2+E3）。每次：日志确认行、出生点截图平均亮度、是否出现 3D 画面。**一旦某项让 3D 世界出现画面，立刻转 P1-验证，不要继续跑完剩余实验。**
步骤 2（标记全无变化时）：用运行时转储**二分黑色首次出现的位置**。用 capture 设施或加一个由新标记 gfx-codex-probe 控制的诊断开关（默认关，不改默认行为），在战场出生点的一帧里按顺序读回并统计非零像素/均值：
  (a) 六 MRT GBuffer 的几何 pass 之后（albedo/normal 等，是否有非零内容）；
  (b) 光照/合成之后的 HDR 场景色；
  (c) 色调映射之后；
  (d) present 前的最终图。
  黑色首次出现在 (a)→(b) 之间：看光照 pass 的输入采样（descriptor 指向的是否是 (a) 的同一图像、格式、mip/layer、sampler）；(b)→(c)：色调映射 LUT（32^3 表，FFRS 的对应 compute shader 哈希需要自己找，不是 D2R 的 552f45c3…）；(c)→(d)：呈现/blit 路径。
  同时在 (a) 之前检查：GBuffer 的几何 pass 是否真的有像素通过深度测试（遮挡查询返回 samples=0 会让游戏**不提交**世界 draw：关遮挡后 draws 610→111 的现象值得解释，注意 gfx-no-occlusion 返回 0 样本）。
步骤 3：按证据做**最小**修复（每次一处，改前备份），重编译、安装、同一出生点复测。
出口条件：连续 3 次冷启动，出生点截图都显示 3D 世界（非 UI 区域平均亮度显著 >0 且有结构），标题/Option 菜单/水晶过场截图无退化，**并把截图原图交给用户目视**。未经用户目视不得宣布 A 完成，但可以带着“待用户确认”继续 P3。

### P2 -5 崩溃（只在 P1 完成后；上限 1 小时）
在野外驻留 10 分钟，若复现 -5，记录触发 draw 的完整状态（stage mask、颜色附件数、写掩码、m[4] 是否为空、前后各 5 条命令）。给出最小修复或如实报告原因未定。

### P3 性能（目标 B；只在世界可见后做，在真实可见的野外驻留场景测）
先建立基线：野外驻留 60 秒，记录 draws 桶、fps、GPU ms、passes/帧、op5/op27/op28 pass_ends、submits/帧；冷/暖缓存分开。
然后一次只动一个杠杆，两次独立冷启动、同路径、同停留点、同 draws 桶对比，只保留有净收益且画面不退化的：
 1. 改动 C：dispatch() 尾部的 COMPUTE_SHADER→HOST 屏障改为 `if(d.needs_writeback()||capture.active)`，与 draw 路径（约 3232 行）的既有守卫一致。
 2. 遮挡查询（ReportCounter 切 pass）：在驻留场景下评估 gfx-no-occlusion / 批处理（注意 draws 会变，必须先弄清遮挡结果对 draw 提交的影响，不能当成纯性能开关）。
 3. 其余按 pass-profile（标记 gfx-pass-profile 内容必须是明确参数 '1200 1 120'）的数据挑最大头，包括：BC 纹理 CPU 软解（Mali 不支持 BC）→ ASTC 转码的可行性评估、视频帧逐帧 1080p 上传、分辨率档位（须记录实际 render target）。
达标判定用原始帧时间戳，不用 PERF 平均值冒充 P95/P99。到不了 30 就如实写差多少、卡在哪个杠杆。

### P4 能玩（目标 C）
在 A 成立后：移动、对话、进战斗各至少一次；连续试玩 >=15 分钟记录崩溃/卡死。不具备的步骤标 NOT_RUN。

## 6. 必须停下问用户的情形
- 需要降低验收指标、改变游戏行为/速度、降低内部渲染分辨率到 960x540 以下。
- 需要改游戏包、存档、固件，或需要新的游戏文件/密钥。
- 需要动主树脏改动、push、碰 main。
- 出现了设备重启/刷机类风险操作。
其余决定自己做，在 LEDGER 里写一行“选了什么/为什么”。

## 7. 交回格式
- 每个阶段结束时在 LEDGER 写阶段小结：结论（证据显示什么）、证据路径（日志/截图/sha256）、构建信息（commit/dirty 状态、APK sha256、native 库 sha256）、未验证项、回滚命令。
- 全程结束或被阻塞时，写 .local/kiro-runs/50-goal/FINAL.md：目标 A/B/C 各自状态（EVIDENCE_SHOWS / NOT_RUN / BLOCKED，不使用 PASS），下一步精确命令。
- 不要把实现者自测写成独立验收。

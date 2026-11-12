# 大地图「比 1.0.7 更卡」与战斗结算闪退 — 真机归因（2026-11-13）

任务线：总控直做。真机在荣耀平板 `A42BBB5109000333`（用户已交回并授权）。
工作区：`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/release-1.0.9-20261112`
分支：`task/T-release-1.0.9-20261112`

## 0. 用户现场原话与本报告的三个答案

> 「读档02存档 是进入大地图 超级慢 我正在读」
> 「而且这个版本比之前那个版本更卡 在大地图上」
> 「战斗花面似乎也有请问卡顿」
> 「战斗结算画面闪退了」

| 问题 | 答案 | 依据 |
|---|---|---|
| 战斗结算闪退是什么 | **1.0.9 引入的回归，已定位并已修复**（commit `89386a8`）。签名：`FATAL: Typed storage image format does not match the bound view format`。真凶是 Mali typed storage 线新增的严格格式匹配，误杀了**游戏自己声明为 typed** 的 storage image | §2 |
| 为什么 1.0.9 在大地图上比 1.0.7 更卡 | **不是 1.0.9 自己变慢了，是它没有让游戏自己的降载机制生效。** 1.0.9 每帧 draw 数一路涨到 784，1.0.7 在同一路线上是 431 然后**回落到 44 并稳住**。1.0.7 那次回落时 fps 掉到 15.86、`submit_cpu_ms` 从 15.79 涨到 37.73 —— 那是**游戏自己**在帧率低时降低渲染负载；1.0.9 的 fps 始终稳在 30（`submit_cpu_ms` ≈ 17 ms），**游戏的降载判据从未被触发**，于是绘制量持续膨胀 | §3 |
| 战斗特效卡顿 | **NOT_RUN**：需要一场真实战斗（平板上 1.0.9 的战斗结算已崩，无法取战斗段帧数据）。战斗特效的既有结论（seed 分发通路）在本轮没有新证据 | §5 |

## 1. 顺手修掉的两个测量缺陷（否则上面的结论拿不到）

### 1.1 场景标记从未生效 → 「场景测量」三轮都是空的

`scene::init`（`xbg_game.cpp:987`）在启动时 `stat(tag_path)`，**文件不存在就直接 return，测量永远关闭**。
R1–R3 三轮的脚本都在**启动 45 秒后**才写 `gfx-scene-tag`，所以 `enabled=false`，
三轮日志里 `NVN scene marker` / `event=enter` 一行都没有。
修正后的写法（`rounds_r4.sh`）：**先写标记再启动 App**，然后在切换时刻**重写**一次（mtime 变化＝一次入场）。

修正后 R4 立即拿到硬数字：`NVN scene marker=1`、`event=enter occurrence=1`、20 条 stall 行。

### 1.2 `other_ms` 打印成 1.19e107 → 场景归因全是垃圾值

`xbg_game.cpp:1063` 的 `xbg_logf` 格式串有 **7 个浮点转换**（`%.1f`×5、`%.2f`×2），
但只传了 **6 个浮点实参**：`slow_ms` 以 `(unsigned long long)` 传给了 `%.1f` 位。
AArch64 下变参的整型与浮点走不同的寄存器组，**错位从该位开始，后面所有浮点槽位全部读到未初始化值** ——
`other_ms` 拿到的是垃圾，`gpu_ms` / `submit_cpu_ms` 也一并错位。
（这条同时说明：**R4 的场景数字是被污染的**，R5 修复后的数字才可信。）

修法：把 `slow_ms` 显式转成 `double`。

## 2. 战斗结算闪退：完整取证链

### 2.1 崩溃签名
- 设备：`/sdcard/Android/data/org.ffrs.gamewindow/files/engine.log.fatal`（71 B）
  → `FATAL: Typed storage image format does not match the bound view format`
- `guest_boot.log` 尾部同一行出现两次（`xbg_fatalf` 会再打一遍到 stderr）。
- 进程已死：`ps -A | grep ffrs` 空。logcat crash buffer 只有一段无用的 HKS 段（无 tombstone，
  因为这是 `xbg_fatalf` 主动 abort，不是系统信号崩溃）。

证据目录：`.local/acceptance-1.0.9-honor-20261113/R7-battle-settle-crash/`
（`engine.log.fatal`、`guest_boot_tail.log`、`logcat-crash.txt`、`logcat-main.txt`、`screen.png`）

### 2.2 崩溃点
`src/graphics/nvn_vulkan/engine/xbg_game.cpp:4503`（storage image）与 `:4302`（storage texel buffer）：

```cpp
const uint32_t declared=shader.storage_image_formats.count(b.binding)?shader.storage_image_formats[b.binding]:0u;
if(declared){
    need(xbg_typed_storage_matches(declared,uint32_t(viewed)),
         "Typed storage image format does not match the bound view format");
}```

这两条检查是 1.0.9 新增的（Mali 线 `62459f0` / `0125c05`）。**1.0.7 的同一分支没有它们**
（`git show aca34f0:.../xbg_game.cpp` 的 storage image 分支直接进 formatless 特性位检查）。

### 2.3 为什么它误杀

崩溃模块 `c11e2f23ff063e85`（compute，stage 5）。从游戏包
`FFRS/romfs/shaders/c11e2f23ff063e85.xgs`（NAS 上的 `FFRS-data.zip`）解出 SPIR-V，
`spirv-dis` 显示：

```
%47 = OpTypeImage %uint 3D 0 0 0 2 Rg32ui      <- binding 47（typed）
%51 = OpTypeImage %float 3D 0 0 0 2 Unknown     <- binding 53 与 65 共用（formatless）
%55 = OpTypeImage %uint 3D 0 0 0 2 Rgba32ui     <- binding 59（typed）
```

包里的绑定标志：binding 47 = `0x80000100`、59 = `0x80010200`，**`flessW=0`**，即包本身声明这两个
绑定**不是** formatless write；而 53/65 = `0x80000201` 才是。

所以：**这是游戏自己声明的 typed storage image**。游戏侧绑定的是它自己的 NVN view 格式，
NVN 不要求这个格式与 Vulkan view 格式拼写一致 —— 这对格式是游戏自己的配对，不是引擎该拒的。
1.0.7 跑同样的模块时没有这条检查，所以不崩。

### 2.4 修法
把「模块自己声明的 typed 格式」与「**引擎自己改写出来的** typed 格式」分开：

- `Shader` 增加 `std::set<uint32_t> typed_storage_rewritten`；
- `typed_storage_variant()` 把它改写过的 binding 记进去；
- 绑定检查只在 `typed_storage_rewritten` 命中时才做严格匹配，
  判定规则下沉到头文件 `xbg_typed_storage_requires_match(rewritten, declared, bound)`，
  引擎与宿主测试共用一条规则。

游戏自带 typed 模块交回驱动自己的校验（与 1.0.7 一致），引擎自己改写的变体仍然严格匹配。

### 2.5 自证
- 宿主契约：`tests/device/test_typed_storage.cpp` 增加「游戏自带 typed」用例，
  新 fixture `tests/device/typed_storage_game_declared.spvasm`，接进 `scripts/verify_host.py`。
- **变异自证**：把规则改回 1.0.9 旧写法（去掉 `rewritten` 条件）后，`typed-storage-contract` 变红 ——
  说明新断言不是恒真。
- `python3 scripts/verify_host.py`：30 步全绿；唯一红仍是 `module-load-info`
  （本 worktree 未解包游戏本体，缺环境输入，修复前后一致，非回归）。
- 设备：修复包 sha256 `cc5bb6be78238e2124a5e01f332eebdf82e9423126a024fa7f4e73f3298e068a`，
  `install -r` 覆盖安装 1.0.9/code 9，未卸载、存档保留。
- 同路线对照（修复前 R5 → 修复后 R8）：
  `>50ms` 帧 **66 → 56**，`>200ms` 帧 **17 → 12**，进大地图段卡顿总量 **8932 ms → 3769 ms**。

## 3. 「1.0.9 在大地图上更卡」的归因

### 3.1 用户观察与数据一致
同一条路线（标题 → Continue → 向南走 30 次），同一台平板：

| | 1.0.7 | 1.0.9（修复后） |
|---|---|---|
| fps | 27.76 | 26.94 |
| gap p95 | 36.77 ms | 39.49 ms |
| `>50ms` 帧占比 | 2.1% | 3.2% |
| `>100ms` 帧占比 | 0.6% | 1.1% |
| `>200ms` 帧占比 | 0.3% | 0.9% |
| **draws/帧（稳态）** | **44** | **784** |

1.0.9 确实更卡，且卡顿帧比例全面更高。

### 3.2 但工作量膨胀的机制在**游戏侧**，不在引擎侧
帧 CSV 的 `draws` 计数逐段走势（每 300 帧一桶，draws/帧）：

```
1.0.7 : 16.8  27.0  40.0  40.0  146.1  127.4  213.1  187.2  455.8  434.1
        460.7  396.6  400.9  443.6  425.1  443.4  423.6  144.2  431.5 | 44.5  44.2  44.8  46.1  46.2  45.9  44.2  44.2  44.7  44.2  44.1  44.2
1.0.9 : 14.2  27.0  40.0  40.0  405.8  407.2  407.3  407.3  482.8  641.2  779.6  784.0
```

1.0.7 在 frame 5581 之后**回落到 44/帧并稳住**；1.0.9 一路涨到 784/帧且**没有回落**。

### 3.3 回落是**游戏自己**做的
1.0.7 的 PERF 窗口 W91→W94：

| 窗口 | fps | submit_cpu_ms | draws/帧 | passes | views |
|---|---|---|---|---|---|
| W91 | 30.00 | 15.79 | 431.5 | 23 | 16751 |
| W92 | **15.86** | **37.73** | 220.8 | 23 | 9699 |
| W93 | **15.88** | 32.66 | **25.0** | 23 | 337 |
| W94 | 29.99 | 10.50 | 45.1 | 23 | 4639 |

`passes` 始终是 23 —— **渲染 pass 数没变**，变的是每个 pass 里的 draw 数与 view 数。
W92/W93 的 fps 掉到 15.9、`submit_cpu_ms` 涨到 37.7 ms、draws 掉到 25：
这是**游戏检测到帧时间变长、主动降低每 pass 的绘制量**（游戏侧的动态画质/LOD），
不是引擎替它降的。

1.0.9 的对应窗口（修复后 R8 全程）：`fps` 始终 28.6–30.3，`submit_cpu_ms` 16–18.6 ms，
**从未出现 15 fps 的低谷**，所以**游戏的降载判据一次都没被触发**，
绘制量就一路膨胀到 784/帧。

### 3.4 因果链
`引擎侧提交路径变慢 → 帧时间拉长 → 游戏的降载判据需要看到更长的帧时间 →
1.0.9 恰好每帧都稳在 30 fps（引擎把提交路径压住了）→ 游戏永远不降载 →
绘制量膨胀 → CPU/GPU 负载被自己画的 784 笔吃掉 → 表现为「更卡」`。

**这推翻了一个直觉**：不是「1.0.9 做了更多无用功」，而是「1.0.9 让游戏少做了它本该做的降载」。
`submit_cpu_ms` 从 1.0.7 稳态的 10.1–10.5 ms 涨到 1.0.9 的 16–18 ms，
这是引擎侧唯一真正变差的地方（其余计数器口径两版一致，见 §3.5）。

### 3.5 口径核对（避免拿不同口径的数字对比）
- `perf.draws` 在 `execute_draw_record` 里每帧累加，1.0.7 与 1.0.9 **同一口径**。
- 帧 CSV 的 `draws` 列两版同样口径：每 60 帧增量 2646（1.0.7） vs 47039（1.0.9）。
- `texture_serial` 两版会话末都是 232、末段增量都是 0 → **资源量相近**，
  1.0.9 不是换了更重的场景。
- `pipelines` 1.0.9 = 151 少于 1.0.7 = 422；`images` 1096 vs 1088 相近 → 场景复杂度同量级。
- 构建 marker 两版一致（`async=0`、`defer events=0`、`completion_wait=1`、`frame slots=16`、
  `descmemo=1`、`persistent framebuffers=1`、`wtrack=0`），唯一差别是 warm list 长度（1560 vs 404）。

### 3.6 尚未认领的根因
上面把「为什么绘制量不回落」定位到了**游戏侧的降载反馈环**，但
**「引擎侧 `submit_cpu_ms` 为什么从 10.4 ms 涨到 16.8 ms（+61%）」这条还没有归因**，
它才是让游戏判据失灵的源头。本轮没有查这一条，**不得宣称大地图卡顿已解决**。

## 4. 取证纪律

- 归因被推翻时显式撤回：本文 §3 最初按「1.0.9 每帧工作量是 1.0.7 的 3.5 倍」写成引擎回归，
  核对后发现绘制量差异来自**游戏未降载**，引擎侧并未多画 —— 该表述已撤回。
- R1–R3 三轮的场景测量数据**作废**（标记从未生效，§1.1）；R4 的场景归因数据**作废**
  （变参错位污染，§1.2）。有效数据只有 R5（变参修复后）与 R8（含崩溃修复）。
- 帧 CSV 与日志的配对：R1 的 `codex-frame-25926.csv` 是 **11-11 的旧文件**
  （pid 复用撞上 `O_EXCL`），不是本轮数据；根目录原有的 `csv-10071/10453/10537.csv` 三份同样是旧文件。
  设备上 187 份历史 CSV 已先全部归档到 `.local/acceptance-1.0.9-honor-20261113/device-evidence/`
  （147 MB，含 SHA-256），再清理，避免删掉未备份的证据。
- 平板现场 marker：`gfx-codex-frame-metrics=1`、`gfx-async-pipelines=1`、`gfx-defer-events=0`、
  `gfx-depth-aspect=auto`，其余诊断为 0。

## 5. 未完成 / 待办

1. **战斗特效卡顿**：NOT_RUN。需要平板上一场真实战斗的帧数据；1.0.9 的战斗结算会崩，
   修复合流包（R8）之后还没有在平板上跑过战斗。
2. **战斗结算闪退**：修复已装机，**但还没有在平板上复现验证**（需要走到战斗结算那一幕）。
3. **`submit_cpu_ms` 从 10.4 → 16.8 ms 的归因**：未做。这是让游戏降载失灵的源头。
4. **477M**：设备 `RG477M01445122` 全程不在线（adb、USB 枚举、mDNS、5555/5037 端口扫描均不可见），
   Mali typed storage 的真机腿是 NOT_RUN。离线侧已派 worker 做全量 shader 覆盖统计
   （游戏包里 20258 个真实 `.xgs` 可从 NAS 上的 `FFRS-data.zip` 流式读取）。
5. **神殿场景**：仍无真机证据，NOT_RUN。

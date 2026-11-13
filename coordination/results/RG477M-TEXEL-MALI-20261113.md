# RG477M：Mali Texel formatless-write 与战斗崩溃 — 分支通过报告（task/T-rg477m-texel-mali）

- 日期：2026-11-13
- 设备 ONLY：RG477M（`RG477M01445122`，Mali-G615，Android ARM64，1280×960）。荣耀平板 `A42BBB5109000333` 不在本分支范围，本次未做任何操作。
- 仓库：`ff-resonance-rg477m-native`；分支：`task/T-rg477m-texel-mali`；worktree：`.worktrees/rg477m-texel-class-20261113`
- 基线：`task/T-rg477m-shared-texel-split-20261113`（e9e5890，含 formatless-write 救援与共享 image type 拆分）
- 本分支提交：`ec88013`（视图数值类对齐）、`e9d5153`（能力门控 + 契约 + 日志卫生）、`6f4dc1b`（探测顺序无关）、以及本次的运行时开关与本报告
- 边界：本分支只改 `xbg_spirv_typed_storage.h` / `xbg_game.cpp` 的 typed-storage 路径与 `nvn_engine.c` 的日志封顶，未触碰 GameActivity、audio_bridge、Fence 同步逻辑。

## 1. 根因（两次 Mali 崩溃）

1. 第一次（模块 `3b6302a58b86a34a`）：guest 的 storage texel buffer 无 SPIR-V 可声明的格式，模块保持 formatless；Mali 该格式无 `VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT`，引擎护栏 abort：`FATAL: Texel format lacks formatless-write support`。
2. 第二次（模块 `d201be268f66c813`，stage 5 compute）：binding 35 与 41 共用一个 formatless storage image type，模块 sampled type 为 `%float`；guest 给 binding 41 绑的是 `R32_SINT`（VkFormat 99）。引擎保留 guest 视图格式，改写试图在 float-sampled image 上声明 `R32i`，违反 **VUID StandaloneSpirv-Image-04965**，改写被拒（`NVN typed storage UNSUPPORTED ... binding=41 format=99 reason=rewrite-refused shared=0`），模块保持 formatless，随后命中同一条护栏 abort。

**旧构建在真机上走进战斗必崩**：2026-11-13 12:57，基线包（e9e5890）在同一条路线上加载战斗计算模块 `d201be268f66c813` 后 FATAL，进程退出、前台回到桌面。证据：设备 `engine.log.fatal.oldbuild-crash-1257`、`engine.log.oldbuild-1257`（改名归档，未删除）。

结论：guest 的元数据与 guest 自己的 shader 对该缓冲的数值类不一致。参考平台按 shader 解释 formatless 访问，所以视图应取模块的数值类、保留 guest 的分量数（分量数决定 texel 大小与布局，写入字节不变）。

## 2. 改动与能力分流（Adreno 不受影响）

类别对齐被写成一条**纯规则**，设备的 Vulkan 能力是它的输入：

```c++
XbgTypedStorageViewDecision xbg_typed_storage_view_decision(uint32_t guest_format,
                                                            const XbgStorageBindingInfo *info,
                                                            bool device_formatless_write_ok);
```

- 设备能做该格式的 formatless 写（`storage_texel_formatless_write_ok` 为真）→ **保留 guest 自己的视图与 guest 自己的模块**，本补丁对该设备零改动。
- 设备不能做 → 视图取模块的数值类，分量数保持 guest 的，引擎在模块里声明该格式。

RG477M 真机实测（Mali-G615）：

```
NVN format-feature-flags2=1 (formatless storage checks per format)
NVN typed-storage capability: formatless-write texel sint=0 uint=0 float=0 image=1 feature=1
NVN texel format=99 bufferFeatures=0x78 formatFlags2=1
```

即：设备报告了 `shaderStorageImageWriteWithoutFormat` 特性，但 R32 各格式的 buffer 特性并不含 `VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT`，buffer 上的 formatless 写实际不可用 → 走替换路径；storage image 仍走 guest 原格式。逐绑定决策日志带上能力值：

```
NVN typed storage cand shader=70a97cd190a8738f b35=type98->98 flags=2147550465 count=1 caps=0
NVN typed storage variant shader=876dd0820a7ad8fa identity=876dd0820a7ad8fa:stb1:47_100,53_99
```

`caps=0` 即该格式的探测为否，替换被允许；`identity` 里的 `47_100` 就是模块 float 类别落到 `R32_SFLOAT` 的结果。

顺带修掉两处会让这台机器出问题的隐患：

- 格式特征缓存会**被查询顺序污染**（在 `VK_KHR_format_feature_flags2` 解析前查到的走旧路径，之后永不刷新），同一个格式会因查询时机给出不同答案，能力门控会跟着漂。已改为旧路径答过的格式在扩展可用后重查（`6f4dc1b`）。
- 渲染路径每个纹理释放都 `getenv("FFRS_PROFILE_TABLET")` 并逐条打日志；已去掉 getenv，打印在所有设备上一律限频（前 32 条 + 每 1024 条）。这是 477M 上 `engine.log` 两小时写满 64MB 的原因。

本次追加一个**运行时开关**（只读一次 marker，不是热路径）：`gfx-typed-storage-class-align` 写 `0` 即关闭类别对齐，init 时打印 `NVN typed-storage class alignment=0/1`。它让真机 A/B 不用重新构建。

## 3. 宿主验证（`scripts/verify_host.py`）

- 全部用例通过，无 red（43 项，含 `typed-storage-class-align-*`、`typed-storage-capability-gate-*`、`typed-storage-split-*`、`engine-log-rotation`）。
- 新增契约 `typed-storage-capability-gate`（`tests/device/test_typed_storage_capability_gate.cpp`）：能力为真时 guest 的 `R32_SINT` 视图原样绑定；能力为假时取模块类别且分量数不变。
- 新增契约 `typed-storage-class-align`（`tests/device/test_typed_storage_class_align.cpp`）：用真实战斗模块 `d201be268f66c813` 断言 binding 35/41 共享的 formatless image type 能声明同一格式、且声明后描述符路径解析出同一个视图。
- **变异自证**：把门控改成忽略能力（`if(false)return decision;`）后契约变红（`Assertion failed: (!capable.substituted)`，退出码 134），恢复后绿。
- 重写后的真实模块仍通过 `spirv-val --target-env vulkan1.1`。
- 真实模块契约仍 SKIPPED 的 3 项（`c11e2f23ff063e85` / `3b6302a58b86a34a` / `9bee7f09d6c655c3`）需要从游戏包提取模块，仓库里目前只有 `d201be268f66c813` 与 `ba0d8842fa53e521`。

## 4. 真机验证（RG477M）

- 构建：`scripts/android/build_game_window_apk.sh`（versionCode 9；签名证书 SHA-256 `d11967a3…61f36e` 与已装包一致，`install -r` 成功）。
- 路线：标题 → 读档 → 3D 场景 → 用**手柄方向键**（DPAD_UP/LEFT）驱动角色行走（此前用触摸拖拽虚拟摇杆，实测角色并未移动，路线无效）。
- 结果：越过旧崩溃点后继续行走数十步，`engine.log.fatal` 不存在，进程存活，**无 FATAL**。
- 帧率：`NVN PERF` 实测 fps=22.5（submit_cpu 16.3ms，gpu 43.6ms，736 draws/帧，171 passes/帧，135 dispatches/帧）。
- 证据目录：`.local/acceptance-477m-20261113/`（run1..run5、diag25、seq40、live1、dpad_old、r_mine、r_old、视频与逐帧日志）；设备 `engine.log` 每轮前改名归档，未删除。

## 5. 画面闪烁：间歇性，未定位到根因（如实记录）

**现象**：画面上半部（天空、月亮、远处废墟，约 20~23% 像素）整块出现或消失；底部角色与 UI 几乎无差；形态是**突发**（相邻帧差 22~23%），不是平滑渐变。

**多轮采样结果**（同一构建、同一手柄序列）：

| 会话 | 构建 | 采样 | >15% 突变帧 | 帧差均值 |
|---|---|---|---|---|
| live1 | 本分支 | 14 帧 | 8 | 16.1% |
| r_mine/r1 | 本分支 | 14 帧 | 0 | 5.3% |
| r_mine/r2 | 本分支 | 14 帧 | 0 | 5.2% |
| seq40 | 本分支 | 40 帧 | 0 | 约 5% |
| video1 | 本分支 | 90s / 899 帧 | 0 | 约 5.5% |
| dpad_old | 基线 e9e5890 | 14 帧 | 0 | 约 5.0% |
| r_old/r1 | 基线 e9e5890 | 14 帧 | 0 | 5.0% |
| r_old/r2 | 基线 e9e5890 | 14 帧 | 0 | 7.1% |

**结论**：同一构建、同一手柄序列下闪烁**并非稳定复现**（本分支 3 轮里 1 轮闪、2 轮不闪；基线 3 轮均未复现），属**间歇性**现象。因此早先基于单轮对照得出的「本分支引入回归」判定**不成立，已撤回**；但同样不能据此断言它与本分支无关。

**更关键的是：这条路线本身不可复现机位。** 同一构建、同一手柄序列的两轮，首帧画面差异已达 32.6%（本分支 r1 vs r2）、54.6%（基线 r1 vs r2）；跨构建同一序列差异 9.8%~64%。也就是说「走到同一个位置」这一前提在自动路线下并不成立，任何以「同一位置」为前提的对照都无效。这也是本报告不再给出回归结论的第二个原因。

**已排除的差异**（本分支 vs 基线，同一手柄序列下实测）：

- typed storage 变体集合**逐字相同**（12 个变体，identity 一致）→ 绑定格式一致。
- 渲染工作量一致：ops 2216 vs 2232、passes 171.3 vs 171.3、draws 736.4 vs 735.4、dispatches 135.0 vs 135.0、pipelines/layouts/images 均相同 → 不是少画或多画。
- 条件渲染未跳过任何绘制（`render_cond … draws_skipped=0.00`）。
- 真机逐绑定实证（临时诊断，取数后已移除）：21 个 storage texel 绑定里只有 2 处的绑定格式与 guest 不同，且都是既有确定性规则（guest 的 scalar 标志把 `R32_SINT` 归一为 `R32_UINT`；16-bit 格式无 SPIR-V image format 时换 32-bit 同分量数）；**类别对齐在主线场景一次都未触发**。

**未排除、待查的方向**：

1. 帧间资源同步/读回竞争（`snap_hit`/`snap_miss` 路径），导致远景 pass 的输入纹理隔帧为空。
2. Adreno 专属的 `honor_radix_sync` 路径（`histogram_sync_rewrite` / `boss_interface_rewrite`）在 Mali 上未启用；该路径的注释直指「depth pass 复用会让月亮光晕闪烁」。若判定与本分支无关，这条属于 477M 侧的独立修复。
3. 渲染 pass 合并决策依赖逐帧操作计数，逐帧 CPU 时间变化会改变合并结果。

## 6. 未完成与风险（据实记录）

- **画面闪烁未解决（NOT_RESOLVED）**：见第 5 节。已定位现象形态与排除项，未定位根因。
- **战斗结算画面未验证（NOT_RUN）**：自动行走没有进入战斗。**战斗是剧情触发的，不是随机遇敌**（用户 2026-11-13 澄清；先前「随机遭遇」的推测已撤回）。旧构建在同一条拖拽路线上第 25 步加载了战斗计算模块 `d201be268f66c813` 并崩溃，说明剧情战斗会在读档后按进度自动触发；而触摸拖拽虚拟摇杆实测不驱动角色，自动路线既没有推动剧情，停留时长也不足以跨过触发点。因此 `d201be268f66c813` 的真机 typed 路径仍未被覆盖，需要按剧情推进进入战斗。已验证的是「原崩溃点不再崩溃、进程稳定存活」，不是「打完到结算」。
- **未合入 `work/rg477m-native`**：477M 主 worktree 有他人未提交的 WIP（荣耀线 30Hz 的 coordination/、scripts/、src/ 改动），按规范不 reset/clean/stash、不 `git add .`，因此本分支保持独立，合入由总控串行执行。
- 平板线（Adreno）不在本分支；能力门控在 Adreno 上应保持 guest 原格式与 guest 原模块，但**未在平板上验证**（按用户指令不得触碰平板）。
- 平板的 `hybrid_wait_fence failed: 2` 超时（C4）属于平板线，未在本分支处理。

## 7. 复现命令

```bash
cd .worktrees/rg477m-texel-class-20261113
FFRS_REAL_CLASS_MODULE=../../.local/modules/d201be268f66c813.spv \
FFRS_REAL_SPLIT_MODULE=../../.local/modules/ba0d8842fa53e521.spv \
  python3 scripts/verify_host.py
bash scripts/android/build_game_window_apk.sh
adb -s RG477M01445122 install -r .local/android-apk/ffrs-game-window-debug.apk
# 手柄方向键驱动角色（触摸拖拽虚拟摇杆无效）
adb -s RG477M01445122 shell input gamepad keyevent --duration 3000 KEYCODE_DPAD_UP
```

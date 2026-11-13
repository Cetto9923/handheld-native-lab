# RG477M：Mali Texel formatless-write 与战斗崩溃 — 分支通过报告（task/T-rg477m-texel-mali）

- 日期：2026-11-13
- 设备 ONLY：RG477M（`RG477M01445122`，Mali-G615，Android ARM64，1280×960）。荣耀平板 `A42BBB5109000333` 不在本分支范围，本次未做任何操作。
- 仓库：`ff-resonance-rg477m-native`；分支：`task/T-rg477m-texel-mali`；worktree：`.worktrees/rg477m-texel-class-20261113`
- 基线：`task/T-rg477m-shared-texel-split-20261113`（e9e5890，含 formatless-write 救援与共享 image type 拆分）
- 本分支提交：`ec88013`（视图数值类对齐）、`e9d5153`（能力门控 + 契约 + 日志卫生）、`6f4dc1b`（探测顺序无关）、`3cfb588`（类别对齐运行时开关）、`b035af4`（报告措辞）、`8f0771f`（报告措辞）、**`81b0cdb`（Mali 上传序言策略）**、**`e505b07`（遮挡报告归零按可见处理 —— 闪烁根因）**
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

- 全部用例通过，无 red（46 项，含 `typed-storage-class-align-*`、`typed-storage-capability-gate-*`、`typed-storage-split-*`、`upload-prologue-policy-*`、`engine-log-rotation`）。
- 新增契约 `upload-prologue-policy`（`tests/device/test_upload_prologue_policy.cpp`）：两条设备规则都写成纯函数（Mali 判定、上传序言默认、遮挡归零默认），宿主测试同时陈述 Mali 与 Adreno 两侧。
- **变异自证**（两条）：把遮挡规则改成「所有设备都强制可见」→ 契约变红（退出码 134，命中 `!xbg_occlusion_force_visible_default(0x5143u,"Adreno (TM) 735")`）；把 Mali 判定改成恒假 → 契约变红（退出码 134，命中 `xbg_device_is_mali(0x13b5u,"Mali-G615 MC6")`）。恢复后绿。
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
- **战斗闭环（本轮补齐）**：用户以手柄方向键推进剧情，**战斗是剧情触发**。战斗计算模块 `d201be268f66c813` 在本分支真机上加载 **6 次**，**无 FATAL**、进程存活。旧构建（e9e5890）在同一条路线上加载该模块后必崩。
- **战斗段帧率**：fps=29.99~30.05、submit_cpu 11.5~11.6ms、gpu 29.95~30.38ms、418 draws / 111 passes / 95 dispatches、493 pipelines。
- **地图段帧率**：fps=30.0、gpu 28.4~28.9ms、39 draws / 29 passes。
- **特效冷加载卡顿**：全程 116 次 slow-load（每次 13~24ms，stage 0/4 特效 shader 编译），`d201be` 自身仅 12.89ms。

## 5. 画面闪烁：根因已定位并修复（遮挡查询报告归零被误判为不可见）

**现象**：画面上半部（天空、月亮、远处废墟，约 20~23% 像素）整块出现或消失；底部角色与 UI 几乎无差；形态是**突发**（相邻帧差 22~23%），不是平滑渐变。

### 5.1 根因

引擎把 samples-passed 报告写给游戏时，取的是**写报告那一刻已知的那部分遮挡查询结果之和**（`xbg_game.cpp` 的 `write_report`）：

```cpp
uint64_t samples=0;auto e=epochs.find(r.epoch);
if(e!=epochs.end()){e->second.advance_known();samples=e->second.prefix(r.count);}
```

GPU 还没返回的查询结果在这个和里算作 0，而**「还没返回」与「确实被遮挡」在这个值上无法区分**。游戏收到 0，就把该查询所属的那一块场景整块剔除；下一帧结果到了，又整块画回来 —— 这就是观测到的整块闪。

Mali-G615 上查询结果的回收比其他 GPU 晚，跨 epoch 的已知性经常落后一帧，于是这个误判频繁发生。

### 5.2 修复

在 Mali 上，**报 0 按可见处理**（`e505b07`）：查询照跑、有样本照剔，只把「报告为 0」这一种情况改判为可见。Adreno 保持原样（其查询结果及时到达，且实测平板 profile 是绿的）。marker `gfx-occlusion-visible` 可覆盖设备默认。

采用「归零按可见」而不是直接关闭遮挡查询，是因为后者会把剔除功能整个拿掉，丢掉它带来的性能收益；实测遮挡查询仍以 **134 次/帧** 正常运行，剔除照常工作。

### 5.3 真机验证

| 阶段 | 条件 | 闪烁 | 依据 |
|---|---|---|---|
| 基线 | 无 | 有 | — |
| A/B 隔离 | `gfx-no-occlusion=1`（查询全关） | **无** | 隔离出遮挡剔除这条线 |
| A/B 排除 | `gfx-depth-aspect=0`（关深度采样复用） | **有** | 排除深度路径 |
| A/B 排除 | `gfx-upload-prologue=1`（上传走序言） | **有** | 排除上传路径 |
| 修复后 | `occlusion_visible=1 (device default=1)`，`no_occlusion=0` | **无** | 保留查询、只改归零判定 |

修复后同一进程实测：`forced visible` 累计触发 **10,800+ 次**（即被误判为不可见、原本会消失的远景块数量），遮挡查询仍以 **133.9 次/帧** 运行，战斗段 fps=29.99~30.05、gpu 30.0ms、418 draws / 111 passes，**0 FATAL**，战斗模块 `d201be268f66c813` 加载 6 次无崩溃。

### 5.4 已撤回的错误归因（据实记录）

本轮排查中，以下两条归因**被实测推翻，已撤回**，不得继续引用：

1. ❌ **「上传打断渲染通道 → 分块 GPU 回写附件 → 远景消失」**：`gfx-upload-prologue=1` 生效后，`NVN PERF uploads window` 的 `broke_pass` 从每窗口 9~15 降到 0~3，**闪烁依旧**。该改动（`81b0cdb`）本身有效（减少通道打断与 GPU 往返搬运），但不是闪烁的根因。
2. ❌ **「深度附件采样复用导致月亮光晕闪烁」**：这条线索来自引擎里 Adreno 735 的既有注释（「reusing the open depth/stencil pass flickers the moon halo」），但 `gfx-depth-aspect=0` 关掉后 `depth-aspect sample kept` 归零，**地图照样闪**。

更早在本分支报告里出现过的另外两条也已撤回：「是火焰动画」（用户澄清）与「本分支引入回归」（单轮对照机位不可复现）。

### 5.5 起决定作用的用户观察

用户报告「**地图移动到某个位置也闪烁**」这一条，把范围从「战斗特效专属」拉回到全场景渲染路径，是把搜索方向从特效资源切换到遮挡剔除的关键转折点。

### 5.6 顺带修复：Mali 上的整份上传走序言命令缓冲

`81b0cdb`：本次提交里尚未被使用的图像，其整份上传改由序言命令缓冲在提交之前完成，不再在帧中间结束打开的渲染通道加两道全屏障（每结束一次，分块 GPU 就要把该通道附件写出再读回）。Mali 默认开、Adreno 保持关、marker `gfx-upload-prologue` 可覆盖。规则写成纯函数（`xbg_upload_policy.h`），宿主测试同时陈述两侧。

## 6. 未完成与风险（据实记录）

- **画面闪烁已解决（FIXED）**：根因是遮挡查询报告归零被误判为不可见，见第 5 节。修复 `e505b07`，真机复测无闪烁。
- **战斗结算画面仍未验证（NOT_RUN）**：本轮已进入战斗并释放 LIMIT 大招，`d201be268f66c813` 加载 6 次无 FATAL，但**没有走到战斗结算画面**。已验证的是「战斗场景可进入、战斗计算模块不再崩溃、进程稳定存活」，**不是**「打完到结算」。
- **特效冷加载偶有卡帧**：116 次 slow-load（13~24ms/次）来自特效 shader 的冷编译。`gfx-shader-reuse=2`（改写并逐字比对）已装到设备，但本轮日志里没有取到 mismatch 计数行，该项**结论未出**（NOT_RUN）。若 mismatch 为 0，可切 `=1` 省掉重复改写。
- **未合入 `work/rg477m-native`**：477M 主 worktree 有他人未提交的 WIP（荣耀线 30Hz 的 coordination/、scripts/、src/ 改动），按规范不 reset/clean/stash、不 `git add .`，因此本分支保持独立，合入由总控串行执行。
- 平板线（Adreno）不在本分支；能力门控在 Adreno 上应保持 guest 原格式与 guest 原模块，但**未在平板上验证**（按用户指令不得触碰平板）。
- 平板的 `hybrid_wait_fence failed: 2` 超时（C4）属于平板线，未在本分支处理。

## 7. 交付包

- 路径：`.local/dist/ffrs-game-window-1.0.9-天玑.apk`（主仓 `.local/`，不入 Git）
- sha256：`d27c97770d4ec46b035109bafd55638ba0667a6b9c1e5b0452efa8f2e31e7007`
- 与设备在装包逐字节一致（md5 `4889555e81b4bbb1c58bb8ed619408eb`）
- versionCode 9 / versionName 1.0.9；签名证书 SHA-256 `d11967a31fcef92f3d6b44adf9d9678c8d2b07bf8f3246d51a791617ed61f36e`
- 外发合规：存档密钥 `assets/ffrs_save_key.bin` = **0 字节**；无 png/pak/nsp/spv/xgs 条目

## 8. 复现命令

```bash
cd .worktrees/rg477m-texel-class-20261113
FFRS_REAL_CLASS_MODULE=../../.local/modules/d201be268f66c813.spv \
FFRS_REAL_SPLIT_MODULE=../../.local/modules/ba0d8842fa53e521.spv \
  python3 scripts/verify_host.py
bash scripts/android/build_game_window_apk.sh
adb -s RG477M01445122 install -r .local/android-apk/ffrs-game-window-debug.apk
# 设备端 A/B（写在 /sdcard/Android/data/org.ffrs.gamewindow/files/，重启进程生效）
adb -s RG477M01445122 shell 'echo 1 > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-upload-prologue'
adb -s RG477M01445122 shell 'echo 1 > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-no-occlusion'   # 隔离遮挡剔除用
# 手柄方向键驱动角色（触摸拖拽虚拟摇杆无效）
adb -s RG477M01445122 shell input gamepad keyevent --duration 3000 KEYCODE_DPAD_UP
```

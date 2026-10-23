# Codex 原生移植检查点：项目未完成

目标是 RG477M 上实际 Android ARM64 原生游戏启动和可玩流程，再验证实体输入、音频、存读档、分辨率、帧呈现及持续运行。当前仅有部分运行时、单函数行为对照与诊断 APK，不能称游戏移植成功。

## 所有权与模型

Antigravity 在 d26d3f6 交接时工作树干净、active_writers=[]；只读进程核查未发现其游戏构建或代码写入进程。其原工作目录和旧 worktree 未删除。Codex 集成目录是 `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`，分支 `task/codex-integration`；交付目标仍是 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` 的 `work/rg477m-native`，只由总控串行快进。

实际启动的 runtime、Android、独立 QA 三个子代理均指定 `gpt-6.1-sol / xhigh`。环境没有 CreateGoal / UpdateGoal 入口，没有开启持久目标引擎或后台自动调度；本回合按完整目标持续推进。

- Runtime：`../ffrs-runtime`，`task/T02-codex-runtime`，独占 src/runtime、tests/native、scripts/runtime、自己的报告。
- Android：`../ffrs-android`，`task/T03A-codex-android`，独占 src/android、scripts/device、tests/device、自己的报告。
- 总控：集成、NVN 分发、普通主机验证与 coordination。
- 独立 QA：`../ffrs-qa`，平台安全检查中止；停止重试，无完整独立验收。其普通基线与未提交文件保留，未混入集成。

各工作树 `.local` 独立；仅 uncompressed 与 exefs 链接到原私有输入。没有 reset/clean/stash 他人成果，没有停止其他代理，没有推送或发布。不要提交游戏、生成游戏代码、APK、密钥或大型日志。

## 已集成与已复跑

| 集成提交 | 成果 |
|---|---|
| 9a51034 / 7a360a8 / 37d90c8 | 修复 NVN 无法编译和假成功桩；有类型注册、缺失明确失败、主机复跑脚本和静态库构建。 |
| 72aeca5 | 严格 resolver / MOD0 与映射边界 / 真退出码；原始 leaf 16,875 样本。 |
| 68a90d0 / 41a7971 | 真实 ARM64 ELF、CLI、JNI 诊断 APK；手柄/生命周期/私有文件诊断，源码指纹守卫。 |
| e860f99 | typed ReferSymbol、独立 guest-TLS PoC、实际 Android leaf ELF 生成。 |
| 2cf53b7 | SDK 证实的地址区域 PROT_NONE 真实预留和释放。 |
| addc495 | SDK 证实的 Mutex 字段/函数 ABI 与真实 POSIX 同步。 |

当前输入 main.bin SHA256 `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`，主模块解析 651 个导入、4,612 RELA、1,370,974 RELR。未知导入严格停止并报告名字，不返回假成功。

`.local/runtime/integration-mutex/`：真实 nnMain 越过地址预留及 Mutex 初始化，当前停止于 atexit（sym6，caller +0x13ed4，exit133）。startup 仍是 AllocateTlsSlot，malloc 是 QueryMemoryInfo，构造器是 __nnmusl_init_dso，均 exit133。完整 CRT/TLS/线程仍未兼容；不能从入口更深入推定行为正确。

`.local/host-verification/checkpoint-addc495/summary.json`：普通功能套件通过。含 8线程80,000次互斥增量/overlap0、递归深度3、争用 trylock、100次同址重建；4组 PROT_NONE 预留/保护/释放（最大62GiB，只预留不触碰实体页）；guest-TLS 128,000次隔离值检查与宿主控制组；原始游戏 +0x694bd0 的 16,875 组真实 Mac ARM64 执行与数学 oracle 零差异。输入前后哈希一致。未做 Switch/可信参考环境对照或设备执行，正式 T02 仍未通过。

`.local/runtime/leaf-e860f99/`：实际 Android ELF `libffrs_leaf.so`，2,176B，SHA256 `8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8`；原始44B函数与60B参考函数，无 DT_NEEDED，Android 尚未执行。

`.local/artifacts/android/build-evidence.json`：41a7971 源码静态构建通过，指纹编译前/编译后/打包后相同。现有 `ffrs-diagnostic-debug.apk` 414,228B，SHA256 `af68f4172999787af7fa933e14517e986e3e74e6e522bbf7c815946f27643f40`。这是诊断宿主，不是游戏 APK。AArch64/DEX/JNI/Manifest/签名/16KiB对齐通过；未安装、未执行游戏。

`.local/artifacts/nvn/coverage.json`：535 名字、2 查询入口可解析、533缺失；真实 exit3，proves_rendering=false。Bundled header 有59名字缺 PFN 类型，没有实际 Vulkan 后端、着色器/命令/显示翻译。

## 构建条件

SDK build-tools34 / android34 与 Java23 已实际可用。完整 NDK 包下载校验未完成，当前使用 TLS 官方 Google r27d ZIP 中逐项长度/CRC校验的最小 sysroot、AppleClang21 与 Rust LLD。不要称完整官方 NDK 安装。3330文件缓存已复制到原仓库新建 `.local/minimal-ndk`，没有覆盖任何原产物。完整839006233B ZIP 的官方 SHA1 尚未验证。

实际 adb devices 返回空列表，T03B BLOCKED_DEVICE。诊断 UI 里的输入/文件检查代码没有真机执行证据，临时文件往返不等于游戏存档；没有音频或真实画面。

## 当前继续工作与恢复

Runtime 正在核验并实现真正的退出回调注册语义，成功注册不提前执行回调，CRT上下文缺失继续明确记录。Android 正在添加从 app-private files/libffrs_leaf.so 执行原函数/参考函数的可选验证入口；缺文件 NOT_RUN，无设备不得写 PASS。领取状态以 state.json 的 active_writers 为准，接手先核实代理与工作树，不能按时间抢写。

```sh
python3 scripts/project_status.py
python3 scripts/verify_host.py --runtime-contracts --game-leaf --output .local/host-verification/recovery-CHECKPOINT
python3 scripts/runtime/run_experiments.py --host-hle --out .local/runtime/recovery-CHECKPOINT
FFRS_USE_MINIMAL_TOOLCHAIN=1 scripts/device/build_diagnostic_apk.sh
python3 scripts/runtime/build_leaf_library.py --out .local/runtime/leaf-CHECKPOINT
```

使用新的 CHECKPOINT 目录，不覆盖历史日志。先在隔离分支集成并复跑，确认原交付目录仍干净且未被其他代理改动，再快进 work/rg477m-native。编译期间冻结输入；源码指纹变化的 APK 必须拒绝。各命令保留真实退出码，不用 tail 的0代替游戏退出码。

后续关键路径：CRT/TLS 与真实入口 → Android 实际执行 → NVN/Vulkan 资源/着色器/呈现与资源挂载 → 真标题 → 可玩场景 → 输入音频保存 → 分辨率/30FPS/45分钟稳定性与独立复核。未实现这些前，不能把功能桩、空窗口或打包成功计为游戏成功。

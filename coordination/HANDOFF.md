# Codex 原生移植检查点：项目未完成

目标是 RG477M 上实际 Android ARM64 原生游戏启动和可玩流程，再验证实体输入、音频、存读档、分辨率、帧呈现及持续运行。当前仅有部分运行时、单函数行为对照与诊断 APK，不能称游戏移植成功。

## 最新进展（2026-10-23 08:35 UTC）

交付目录 `work/rg477m-native` 已干净快进至 c280e2a；隔离集成分支当前 444115b，包含额外的明确初始化策略、真实CRT gate诊断、Android HLE CLI及模块TLS镜像。尚待总控后续串行复跑/快进，不用reset覆盖原目录。

- Android14 / ARM64 AVD pvz2 已真实安装及冷启动诊断APK；native bridge=0，4KiB页。原始游戏44字节leaf在Android实际执行16,875输入，原始/库参考/本地数学三方0差异，见 `.local/emulator/pvz2-arm64/leaf-result.json`、`leaf-pass.png`。完整游戏启动和渲染仍NOT_RUN。
- 已安装APK来自交付目录c280e2a，422420B，SHA256 `8ec9d704ae331f77074e67cc63ee56b398ff89d6f3bf148f8de1b3f9637bb07d`，无额外uses-permission。版本4显示修复正在独立分支收口；当前APK路径未来重建会改变摘要，安装前必须读最新build-evidence。
- 同一AVD实际运行严格Android HLE CLI：加载exit0；构造器->__nnmusl_init_dso133，单独nnMain/malloc->__cxa_guard_acquire133，startup->AllocateTlsSlot133。见 `runtime-execution.json`，不能沿用Mac不同的首个缺失接口。
- 构造器修复已集成87217d0：CLI显式 `--init-wrapper` / `--init-elf`，避免main包装器自己遍历数组后再次运行；原含糊`--init`拒绝。CRT 21参数原生捕获与已RELR GOT一致，原SDK gate实际0/init返回2，诊断exit86。后续SDK helper会写TPIDRRO区域/TPIDR_EL0，未执行该路径。
- 模块TLS镜像444115b已有真实分配/复制/清零/对齐、8线程正常测试与独立纯C审查。main模板0x220/总长0x1019/align8/prefix16来自证据；TCB/header未建模，未绑定HLE。副本的字节存储独立不代表其中指针所指对象可在模块卸载后使用。
- AVD实际Vulkan为Apple M5/MoltenVK API1.3，只是虚拟GPU证据。探针把未知屏幕填1280x960/60的错误已实测发现（实际1080x2340/dpi440），正在用真实Activity窗口、insets、密度和Display mode修复，未测音频不再填默认48000/192。
- 原QA安全/异常NSO流程仍中止；新独立普通功能审查已真实通过，见 codex_functional_review.md 和 codex_module_tls_review.md，不冒充完整QA。
- 运行时继续修实际Android C++guard缺口，优先复用官方编译器ABI；TLS分析继续49处非相邻TP数据流；Android继续显示修复。三个真实工作代理，写入scope见state.json。
- 两次本机执行环境短暂中断。首次AVD在断连后adb/ps均确认不存在，未推定原因；第二次只读冷启动PID63060，日志在session2目录。不要把旧工具session号当存活证明；恢复后先核实进程/文件，不重复不确定补丁。
- 新目标荣耀平板GT Pro，按官方产品规格3000x1920/第三代骁龙8s/面板144Hz暂记，实际型号与能力待ADB。输出/渲染保留游戏画幅、必要留边，不拉伸、不裁切UI；面板Hz不等于游戏FPS。

下文保留前阶段检查点和恢复约束；首个阻塞及Android执行状态以上述新进展为准。

## 所有权与模型

Antigravity 在 d26d3f6 交接时工作树干净、active_writers=[]；只读进程核查未发现其游戏构建或代码写入进程。其原工作目录和旧 worktree 未删除。Codex 集成目录是 `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`，分支 `task/codex-integration`；交付目标仍是 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` 的 `work/rg477m-native`，只由总控串行快进。

实际启动的 runtime、Android、独立 QA 三个子代理均指定 `gpt-6.1-sol / xhigh`。环境没有 CreateGoal / UpdateGoal 入口，没有开启持久目标引擎或后台自动调度；本回合按完整目标持续推进。

- Runtime：`../ffrs-runtime`，`task/T02-codex-runtime`，独占 src/runtime、tests/native、scripts/runtime、自己的报告。
- Android：`../ffrs-android`，`task/T03A-codex-android`，独占 src/android、scripts/device、tests/device、自己的报告。
- 总控：集成、NVN 分发、普通主机验证与 coordination。
- 独立 QA：`../ffrs-qa`，平台安全检查中止；停止重试，无完整独立验收。其普通基线与未提交文件保留，未混入集成。

各工作树 `.local` 独立；仅 uncompressed 与 exefs 链接到原私有输入。没有 reset/clean/stash 他人成果，没有停止其他代理，没有推送或发布。不要提交游戏、生成游戏代码、APK、密钥或大型日志。

旧交接文本里的密钥内容已从当前版本移除，历史提交未重写。本任务仅本地开发；以后若获准上传仓库，须先另行审查历史敏感内容，不能直接推送现有历史。

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

`.local/runtime/integration-mutex/`：隔离直接调用真实 nnMain 越过地址预留及 Mutex 初始化，当前停止于 atexit（sym6，caller +0x13ed4，exit133）。startup 仍是 AllocateTlsSlot，malloc 是 QueryMemoryInfo，构造器是 __nnmusl_init_dso，均 exit133。这些实验分别调用各入口定位边界，未完成平台启动/构造器序列；完整 CRT/TLS/线程仍未兼容，不能从入口更深入推定行为正确。

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

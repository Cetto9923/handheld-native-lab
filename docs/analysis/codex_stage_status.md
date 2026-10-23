# 原生移植阶段验收：尚未进入可玩阶段

验收目标为 RG477M 上实际游戏启动、标题、可操作场景、输入、音频、存读档及持续帧率；单函数测试与诊断 APK 只证明其中的基础设施。

| 阶段 | 当前真实证据 | 尚缺的验收 |
|---|---|---|
| 加载器及真实入口 | 主模块可映射，解析 651 个导入、4,612 条 RELA、1,370,974 条 RELR；原始 44 字节函数在 Mac ARM64 的 16,875 组输入与数学参考零差异。隔离直接调用 `nnMain` 已越过地址预留和互斥量初始化，已实际注册并延迟保存 `atexit` 回调，随后在未实现 `QueryMemoryInfo`（caller +0x27a50）严格停止，退出 133。 | 完整 CRT/TLS、线程和平台接口；完整启动；Switch/可信参考环境对照及RG477M实测；Android ARM64 AVD的独立leaf已实际执行16,875组零差异，但正式T02尚未通过。 |
| 资源 | 既有清单、流式提取工具和本地素材保留。 | 游戏实际文件系统挂载、资源解压与运行时使用；不能把文件存在视为加载成功。 |
| Android ARM64 / APK | 真实 AArch64 ELF 与已签名 `org.ffrs.diagnostic` APK，DEX/JNI、Manifest、签名、16 KiB LOAD 对齐静态检查通过。 | 已在Android14 ARM64 AVD安装/冷启动，JNI及leaf比较成功；严格CLI加载exit0，完整CRT入口仍exit133。尚未在RG477M/荣耀平板安装执行，当前仍是诊断宿主。部分官方工具链可重建，完整 NDK 包校验未完成。 |
| 图形 / 标题 | NVN 有类型分发与缺失报告可用；535 个名字中 2 个查询入口可解析、533 个未实现。 | 实际 Vulkan 后端、资源/队列/着色器/呈现及真实标题。两个入口不构成渲染支持。 |
| 可玩场景 | NOT_RUN。 | 标题之后真实场景、操作流程和正常退出。 |
| 输入 / 音频 / 保存 | Android AVD已实测诊断UI点击/生命周期及app-private临时文件往返；无实体手柄或音频/游戏存档证据。 | 游戏输入映射、真实音频、游戏存读档；临时文件测试不能替代存档。 |
| 真机 / 性能 / 稳定性 | RG477M已真实安装、JNI加载、leaf、compiler guard与部分物理输入通过；荣耀平板未连接。 | 已查Mali-G615 MC6 Vulkan1.3.247和实际显示；GPU操作/完整手柄音频休眠仍待验收。30FPS真实场景与45分钟运行NOT_RUN。 |

这些入口实验分别调用 `nnMain`、startup、malloc、构造器定位阻塞，并没有完成平台启动或构造器序列。单独让 `nnMain` 更深入不能证明完整初始化后行为正确。

此前“总体约 10%～15%”只能是按最终可玩目标给出的粗略工程判断，不是代码量、任务卡或工时统计。这次补齐了真实打包和运行时基础设施，但最高风险的完整启动、图形与可玩性仍未验证，目前没有足够证据明显提高该估计。

测试原始输出与真实退出码在 `.local/runtime/`、`.local/host-verification/` 和 `.local/logs/android/`。当前检查点与精确恢复命令见 [HANDOFF](../../coordination/HANDOFF.md)。独立 QA 被平台安全检查中止，未取得完整独立验收，不记为通过。

Android实证：`.local/emulator/pvz2-arm64/leaf-result.json` / `leaf-pass.png` / `runtime-execution.json`。AVD为API34、arm64-v8a、native bridge=0、4KiB页，图形探针为Apple M5 / MoltenVK Vulkan1.3，不能替代RG477M或荣耀平板GPU验证。版本4已修复旧显示误报，并在第二个临时AVD会话实测窗口1080×2340、内容1080×2138、440dpi、60.000004Hz；API28/29的旧安全区getter语义被独立审查发现P2，待修。

最新实际Android guard结果：`14ed86a` 的CLI已越过缺失guard，到QueryMemoryInfo（main+0x27a50/malloc+0x1398c）严格exit133；但独立正常多线程/异常重试contract在Android实际exit134（uncaught int），正检查EH展开链接，不能记guard验收通过。证据 `.local/emulator/pvz2-arm64-session3/guard-execution.json`。

异常修复实证闭环：`a22d0dc` 的`.eh_frame_hdr`/PT_GNU_EH_FRAME配置补齐后，同一真实ARM64 AVD的完整未删减guard契约exit0，8线程80000调用/初始化1/abortwinner1/exceptionattempts2/errors0；info0，main/malloc仍QueryMemoryInfo133。旧134失败证据保留在session3，当前结果见`guard-eh-execution.json`。这只验收宿主compiler-runtime异常，不证明原始guest NSO的unwind/构造器。

真机当前结果见[codex_device_report.md](codex_device_report.md)。用户实际连接并操作后，RG477M leaf16875/0、C++guard8线程80000/errors0、JNI模块加载成功；nnMain/malloc仍QueryMemoryInfo133。不得再写RG477M未连接，也不得把诊断成功改记游戏成功。

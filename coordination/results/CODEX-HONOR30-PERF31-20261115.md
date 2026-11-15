# 荣耀平板安装与帧率优化检查点

## 当前结论

荣耀平板已安装已发布的1.0.30-preview，APK回读一致，存档和历史备份在覆盖安装前后逐字一致。用户反馈这轮测试仍有掉帧，目前测试流程没有再闪退。稳定30FPS仍待同场景实机对照。

本轮已实现整数纹理候选分析的模块解析复用。源码检查点为 `5cd18bf1dc5c0d68fb9943882b8a4584a3ec34a6`。全部73495份真实模块候选输出与冻结旧实现相同。108项宿主检查、整数组件ASan/UBSan与SPIR-V验证、Android构建通过。主机581模块基准中，暖轮中位累计耗时由381.326ms降到353.762ms，降低7.23%。六个实际慢加载顶点模块的分析耗时降低约6%至10%。该数值限定候选分析组件，整场战斗收益尚未验证。

## 安装与设备状态

- 设备为A42BBB5109000333，显式指定串行ADB，应用org.ffrs.gamewindow。
- 安装包为 `.local/releases/1.0.30/FFRS-1.0.30-preview-20261115.apk`，3703949字节，SHA256 `89cbb81c758138ae5e81b4be2f5ca28ddd5c2fb2591fc5de026a0d63c3c7cb78`。
- 证据在 `.local/honor30-perf31-20261115/honor-install/`。`before-saves-backups.tar` 与 `after-saves-backups.tar` 包含codex-local-save及ffrs-quicksave，归档字节完全一致。保留旧APK及日志。
- 遗留gfx-async-pipelines值1会在编译期间跳过绘制。本轮归档为gfx-async-pipelines.used-honor30-20261115，普通入口恢复创建值0。其余启动标记保持采集时值，包括gfx-shader-reuse=0。
- 1358为安装后启动，3953为关闭异步跳绘制后启动。后续PID5199由用户操作，采样前后pidof一致。3953退出记录为USER_REQUESTED，历史crash buffer中的org.snk.the1st错误属于其他应用。
- 单帧黑屏已由用户确认发生在读档或切场景。后续截图为土之神殿中的LOAD DATA菜单。混合运行CSV保留加载、菜单、转场等窗口，不作为稳定场景FPS验收。

## 最小代码修改

原候选分析每检查一个绑定会深复制整个Analysis，其中包含指令、定义表及控制流图。严格分析失败后再次复制，尝试castback路径。大型技能顶点着色器即使预热输出复用命中，前置候选分析仍执行。

新代码保留只读解析结构，各绑定及fallback开始前清空六组数据流集合、三个指令位置与descriptor/float类型字段。parse产生的uint_type、控制流和指令表保持复用。新增绑定装饰逆序回归，覆盖前一绑定失败、strict成功和castback成功后的状态隔离。

冻结旧头SHA256为 `f4e33a29d28b076227c5164f3675d4b548bbf7d8faa760ff3e8d50490d3965a6`，新头为 `42ae87f1487ebf7b397047ab728b2c2dd891a9e80c8f1d58bfd18371074adb40`。全量对照覆盖73495模块、7934个候选非空模块和6027253252字节SPIR-V，逐模块候选列表完全相同，结果索引SHA256为 `d8bd4b04feaa256c373e914939a2cfc67179bc61b5f282c7334e2368f04c6d90`。

## 构建与恢复

本地候选APK在 `.local/honor30-perf31-20261115/candidate-build/FFRS-1.0.30-preview-cpu-analysis-candidate.apk`，3703949字节，SHA256 `b0467af818d23de88d39ba29aff077e39b0b95050f1001b91c45b087c4689051`。本地候选保持30版本元数据，区别由文件名、摘要和构建证据标识。与已发布30相比，未签名ZIP成员仅lib/arm64-v8a/libffrs_game_window.so发生改变，其SHA256为 `ea3e442c9e29251aa96b8d599acdc3545c2b70ad0ef2394318f60b644d82c862`。DEX、资源、清单及APK数据资产逐字相同。平板目前继续运行已发布30，候选尚未安装或发布。

构建输入57项摘要前后一致，并与源码检查点逐项核对。5个协作者诊断WIP文件保留，候选继续使用上一版本显式资源及Java列表。host summary中android_build字段属于宿主脚本范围，该阶段标NOT_RUN，后续Android脚本单独exit0，见build plan/Android日志。

精确复跑入口如下。

```sh
python3 .local/honor30-perf31-20261115/analyzer/compare.py
python3 .local/honor30-perf31-20261115/analyzer/full_compare.py
python3 .local/honor30-perf31-20261115/candidate-build/run.py
```

前两条需读取本地既有6GB着色器归档并编译小组件，第三条按plan中的环境构建，维持4jobs。所有输入、命令、日志、原始/候选头摘要及产物位于本任务.local目录。运行完成，无长构建进程。

## 最新实机证据与下一处热点

PID5199日志与帧CSV在honor-passive目录，元数据记录采集时刻与进程。曾记录16617到16618帧间隔1459.10ms，同帧18条slow-load合计1270.64ms，说明该次窗口存在显著同步着色器加载开销。该窗口的具体游戏场景未逐帧标注，不能外推到全部战斗卡顿。

同一进程的现成CPU三帧诊断覆盖27062至27064，1194draw、159dispatch、24submission，shader0，pipeline cache hit159/miss0。descriptor独占范围计时合计25.796ms，约8.60ms/帧。snapshot合计6.462ms，约2.15ms/帧。计时使用elapsed wall clock，三帧覆盖与具体技能未绑定，后续截图为读档菜单。持续描述符构建、快照维护与提交等待应继续按真实战斗定位，不能只处理首次编译。

simpleperf尝试cpu-cycles及cpu-clock均被设备内核拒绝为unsupported，两个退出码均1，未获取采样数据。随后采用仓库现成gfx-cpu-profile一次性三帧计时，自动消费标记，取得上述数据。无UI点按或游戏逻辑修改。

## 成熟方案对照与后续执行

既有DroidPlay复核基线a72a85c59cac54f3428827cfb72e1adf63b1521a、MIT，以及BOTW基线65c3d4783acbb843c1a6a011030cfc06d991b091继续沿用，来源索引见CODEX-UPSTREAM-PORTS-20261114.md。本轮新增修改来自本项目实际分析开销。

- 继续沿完整管线身份与真实状态预热，核对当前设备学习表新增项及编译尖峰。Khronos说明运行中创建管线会引入尖峰，预热依赖完整创建状态与资源。 [Khronos管线缓存样例](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html)
- GPU方向按实际重通道和资源依赖逐项验证。目前代码有ALL_COMMANDS全局屏障、频繁render pass边界和LOAD/STORE保守路径。Arm对Mali CSF的说明可用于检查流水线依赖，但具体修改需先证明读写依赖及附件生存期。 [Arm Vulkan同步说明](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/vulkan-synchronization-how-pipeline-barriers-map-to-mali-gpu-hardware)
- 分别统计稳定场景CPU/GPU帧耗时和呈现间隔尾部。Android帧节奏方案可用于核对呈现排队，但一次GPU耗时超过33.33ms仍需压缩该帧工作量。 [Android Frame Pacing](https://developer.android.com/games/sdk/frame-pacing)

下一次交还设备测试窗口后，候选与已发布30做同一场景暖缓存对照，记录开场、走动与同一技能的FPS、P95/P99、50/100/250ms长帧、音频和显示回归。实际技能GPU通道继续用既有gfx-pass-live采样，并保留采样扰动条件。位置精度、FragDepth、RG16滤波与完整shader identity回归沿EXPERIENCE.md已有检查执行。

独立QA未执行，候选真机战斗收益、所有场景稳定30FPS及其他手机的兼容性均未验收。用户这轮无闪退反馈限定其已测流程。

# 当前普通交付与捕获闭合补记（2026-11-08）

普通交付已推进至 **1.0.2-honor / versionCode3**，运行时代码 `6a90ed1ed30bedc2c368cde64eed18d6a2e32106`，本地检查点 `honor-graphics-20261108-view-facts`。APK `4a91b71cf13689029b13156ab831b9387890ff8443cf171657f6a9bb42cc1002`，3,749,266B；数据ZIP仍为下文同一 `01a338c5…10411af`，16,918,705,179B。安装手册已指向1.0.2，旧包/旧tag保留，不移动。

修后暗魔法9836与Limit19659分别完成同draw完整原生GPU输入/实际shader/附件捕获及未改API单目标重放：Dark174/174独立读回、Limit126/126独立读回（137项含11别名）全部一致。**CAPTURE_COMPLETE=YES**仅限指定首个pre/color pair；Limit同帧另外6个matching实例保留在API、不冒称native覆盖。R32explicit range实际创建审计Dark29696/Limit39263均0非法、0submitted坏引用；VIEW_VALIDITY=FIXED限已观测类型/该VUID，非全VUID或全游戏。清理后索引 `coordination/experiments/honor-capture-20261108/postfix-closure.json`。

`febedab`仅修opt-in诊断的保护映射读回写文件：现有fence/invalidate后先CPU memcpy到普通heap再fwrite，避免page_guard下内核I/O的EFAULT；普通marker0渲染/声音路径不变，交付APK无需替换为诊断537c。既有VS Position/CS归约黑效根因保持独立，不把view合规或捕获I/O当原黑块根因。

普通1.0.2的Limit/Boss/防御/爆炎/召唤有限视觉自测PASS、Dark等价runtime自测PASS；音频未测试，用户已明确免除本次音频验收；400Pro/独立QA/护盾魔法未验。60未达，最后暖野外32.66594个distinct新帧/s；没有修改游戏速度或用重复呈现帧充60。整体仍未验收通过。下文为2026-11-07初始冻结记录，旧身份/旧缺项只作历史，当前状态以上述补记和RESULT/HANDOFF顶部为准。

# Honor 图形修复基线（2026-11-07）

范围为本 Honor 工作树现有 dirty 代码及本线程接续修复，父提交 `032e5f68172c101cb2b6e44ee06f02974dab2da8`，分支 `work/honor-gtpro-30hz`。源基线由本地标签 `honor-graphics-20261107` 标识；不推送、不修改 RG 工作树/main。任务唯一 writer 为当前线程 Codex /root。

这是图形流程验证基线，**不是完整游戏、稳定60帧或音频验收基线**。

- 暗魔法：实际 GPU VS Position / 读值对照定位到旧 bindless 翻译丢读；正式原 Maxwell 重译恢复蓝白核和紫红外圈，保留 EQUAL / FS / 预通道。
- Limit：冰晶、火焰等分别定向核对；火尾三对 pre/color VS 都需重译，仅类型合规或只修 color 不足。真实 draw1816568 的终端 GPU x/y/w 对照已记录；不以空 FS、ALWAYS、删预通道或裁黑块代替。
- BOSS 出场：精确 compaction CS 的64-lane共享归约重复计数（7+0得到14，岩石132而非100），按两个32-lane warp 分存并保留屏障后求和；不截断粒子数。另正式重译碎石 VS。
- 数据包含11个 natural XGS 修复文件，索引为 `coordination/results/HONOR-EFFECT-BUNDLES-20261107.json`。它们是必需的私有生成产物，不提交 Git；仅安装 APK 不能替代数据修复。
- 原有 Honor 的输入/显示、类型、subgroup helper、histogram、同步对象引用生命周期等 dirty 依赖一并认领保存，不能把本提交全部归因于最后一对 shader。
- 荣耀400Pro：SM8650配置目标60，精确Adreno750沿用已有helper inline及精确模块histogram/interface守卫；735的depth-reuse特殊门没有外推。手机实测 NOT_RUN；1080字段不代表内部渲染尺寸已实测。

普通候选 APK（versionCode2，1.0.1-honor）：`2a4c811d88eb0271fd1c5039bc9a98eaac2c5d95f95499f8f22787b1a1108201`，3,732,882字节，包名/签名兼容，安装-r保留数据。

私有交付目录：`.local/honor-delivery-20261107/release/`。数据ZIP 16,918,705,179字节，SHA-256 `01a338c52b79869c04f486eb1d42ce926fa6935d28d19371ca0a7c6cd10411af`，40,547文件；全成品CRC/逐文件SHA回读与40,536原始设备文件摘要均核对，另11修复文件摘要匹配。游戏/NSO/完整资源/存档/密钥/APK/大证据没有进入Git。

视觉证据覆盖教程完整暗魔法/Limit和Boss出场；候选身份与逐阶段自测结果以既有 RESULT / HANDOFF 顶部为准。独立QA、其他手机和全游戏流程未验收。蓝牙音频依用户要求 NOT_RUN；NONE、缓冲、重开/超时重试、fence spin0保持。NDK NONE数值为10，不能将该日志误标为LOW_LATENCY。

60目标仍未达到：普通1f90暖野外44.224秒实测35.27487个独立新帧/s，0重复/失败/跳过。defer2对照34.966fps且实际事件数0，已否定并恢复默认。不得将标题60fps、配置值、屏幕144Hz或录像采样率写作实场60验收。

源码构建：`FFRS_SPIRV_TOOLS_SOURCE=<本Honor树内已固定的SPIRV-Tools源码> FFRS_BUILD_JOBS=2 bash scripts/android/build_game_window_apk.sh`。实际命令、编译器/NDK、源文件摘要与日志在私有 `limit-red-tail-20261107-2130/build-phone-guards-plan.json`、`build-phone-guards-logs/`、交付目录 `source-build-manifest.json`；不依赖读取被禁止的RG目录。

数据包重建：`python3 scripts/distribution/package_honor_portable.py --data .local/honor-delivery-20261107/staging/FFRS --out <新的本树.local输出目录>`。必须保留源文件，使用新输出目录；脚本不会上传或写设备。

安装步骤见 `coordination/docs/HONOR-400PRO-INSTALL.md`。捕获完整性仍为NO、全view合规仍NOT_FIXED，具体原运行输出/资源世代等缺项保留在既有RESULT，不能把范围内视觉通过冒充完整捕获或全游戏验收。

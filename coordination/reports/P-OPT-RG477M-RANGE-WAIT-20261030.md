# RG477M：GPU 等待实验未采纳，场景录音已补齐

本轮没有采纳新的性能路径。所有生产源码中的 COW／范围等待实验改动已逐段撤下，与实验前保留的脏改逐字核对一致。两项实现作为 **未应用的研究补丁** 保存在 `coordination/experiments/`，不是默认配置，也不代表性能或完整正确性通过。原音频提交 a7eb0ed 保留；未 push、未清存档、未改系统游戏档，仅操作 RG477M01445122。

## 同包运动对照

隔离包 `org.ffrs.perfisolated`，SHA256 `6204d4c05a50ce589d4493b5a9b639f7a9b1c2bf02f4534234bf50ba31e2f8b2`。同一 APK、暖缓存、游戏模式 cpu_mode=2、target30、fence0、paced audio1、legacy waitall0。截图分别确认标题和真实燃烧台阶场景后才开始上／下来回移动 120 秒。原始慢帧不删除。

| 路径 | CSV 时间 | 平均 Hz | p95 ms | 最大 ms | >50 / >100ms |
|---|---:|---:|---:|---:|---:|
| 原路径 mode0 | 124.140s | 29.9662 | 33.6557 | 173.048 | 2 / 1 |
| 范围等待 mode1 | 124.073s | 29.9824 | 33.8731 | 106.518 | 1 / 1 |

p95 没有改善，两轮均有 >100ms 停顿，不能凭单轮最大值差异认定更稳。因此不采纳。独立 mode2 曾先退役实际旧 producer，再执行原来的完整排空，对 CPU 清零范围进行至少 120 次比较，观察到零差异；这只是范围化等待的窄自测，不证明完整生命周期、所有场景或性能通过。实现保留真实 fence、CPU 回写和 query/event 发布，对当前 recording producer、owner copy、图像别名等回退原路径。

COW 实验未启用 fast mode：初版有至少 120 次 GPU 字节比较相同，但不能证明 fast 路径的 CPU 事件顺序和 arena 寿命。增加事件保护后的版本没有比较样本，具体 eligibility 差异未定位；日志实际 defer_events=0，不能把零样本归因为待发布事件。两者均作为未采纳实验保留，不能复述先前未经证实的事件归因。

## 实际场景输出录音

录制 APK 同上，mode1 仅为本轮试验条件；已在录制后停止该包并换回不含实验路径的新诊断包。录制前逐张确认真实场景，执行约两分钟来回走动。scrcpy `--audio-source=output --no-video --no-control --no-playback --audio-codec=flac --time-limit=135`，退出码 0。它会改变输出路由，因此不是 A2DP 耳听验收，也不是无干扰的 FPS 采样。

原始 FLAC：6,795,345 bytes，SHA256 `ab0445026ac9c5fea3ae47916e90ae0cf3136a1a2fdf15c87a0f8999538600a9`，实际 134.912 秒／48kHz／双声道。完整记录中仅检测到两段 >=20ms 全声道空白：0–64ms、85.333–213.333ms；214ms 之后没有同类空白。peak374，无削顶。只证明这段输出未再出现原先周期性零段，不能证明内容保真、主观连续、蓝牙或物理 A/V 同步。

原生诊断 PCM 只录前 180 秒。之前一轮手动确认场景较久，field 起点按 timing/CSV 时钟映射已在 PCM180秒之后，该轮场景音频覆盖为零，明确 NOT_RUN。其他 raw42..180 分析含加载静音，不可当场景音频卡顿；原始数据与范围说明全部保留。可选波形图因两套 Python 均缺 matplotlib 未生成；统计核心已独立执行，未安装全局依赖。

## 出处与范围

实现前已读 [Khronos Mali WaitIdle/Fences 案例](https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html)；仅文档参考，不复制源码。引擎参考 [d2r-mobile xbg_game.cpp](https://github.com/d2r-mobile/d2r-mobile/blob/a8782caa28d7b49d1b341e34de488d3168acb86c/dm/gfx/vulkan/engine/xbg_game.cpp)，revision `a8782caa28d7b49d1b341e34de488d3168acb86c`，已读取 MIT 许可证；参考已有 pending-view／wait_fence／retire_slot 结构，没有从其他游戏推导本游戏兼容。COW 的 GPU fill 语义还参考 nxnative `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3` 的 queued blit fill，MIT，无源码复制。

本地证据目录：`.local/rg477m-opt-30hz-game-mode/audio-paced-transport-20261030/range-wait/`，包含 plans、命令、原始日志、frames、screenshots、paired-result、output-field-135s 与分析脚本；COW 在相邻 clear-clone/。构建／安装／验证／A/B／录制 session 均已终止，真实退出码与运行范围见各目录。

全单未通过：A2DP 野外／战斗各 >=2分钟耳听、真断连重开、物理 A/V 同步、战斗稳30、身体护罩及全视觉、缺 Paks 的 setup 负例仍未过。完整常设音频原文和其 NONE 枚举冲突说明保留在根 RESULT；NONE 实际10、6150帧缓冲、100ms写、TIMEOUT重试、DISCONNECTED重开、fence0均不变。无独立设备 QA，不声称可玩。

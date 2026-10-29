# C2 透明绘制参数已捕获，护罩仍未修复

仅将实际捕获中新增的一对 shader：VS `6018ff2b44b763d7` / FS `c2ca757a9f517e96`，加入既有 probe-only uniform/support 快照白名单。保留每 binding64KiB、额外缓冲8MiB总预算，仍排除 GPU storage buffer 的 CPU 读取；未启用 probe 时不改变渲染。没有修改 shader、材质、混合、游戏时钟或音频。

设备 RG477M01445122，隔离包 org.ffrs.perfisolated SHA256 `43ea0c2157c29d5dc0b5e1babeb64699319c17f49bd4bbc4c408ae659106e00c`，原568 manifest/resources/classes，新 native。target30/paced1/fence0/legacy0/async0/depth1。两项未采纳的性能实验生产源码已全部撤下；其他既有脏改保留，未 push。构建90896、安装1650均exit0。

固定时刻24秒、17秒两次抓帧均完成，但目标C2绘制未出现，明确 NOT_CAPTURED，不当作参数验收。24秒记录还证明 C2首次加载在捕获frame4931之后：最近的GPU驻留日志frame4945，因此不能把这次缺失解释为shader未加载或素材缺失。改用已有“首次实际C2加载”日志作为触发依据，仅轮询本应用日志，下一完整帧开始抓取。

最终 onload session5076 exit0，PID29847，首次发现约25.718秒、marker写入25.784秒。捕获frame6897、154 groups、44,143,512bytes，END完整；目标group112包含1次C2绘制，片元7份／顶点9份 uniform/support 快照。实际C3曝光系数1.934，与邻近旧shader的0.6不同，证明不能借用其他draw常量推断。原始位字段同时保留；把整段缓冲按float查看时有NaN/极小值，不代表这些字被当float读取或已损坏，必须按SPIR-V类型与访问逐项判断。

原始SPIR-V另做 `spirv-opt -O` + `spirv-dis` 的离线可读性副本，未部署、未设置override。片元最终alpha由纹理/顶点因子与深度淡出相乘；这是下一步数据核对线索，不是护罩身份或根因结论。

出处：已读取 [Ryubing issue476](https://github.com/Ryubing/Issues/issues/476) 的Apple NaN纹理坐标案例与 [Mali缺效果 issue45](https://github.com/leegao/bcn_layer/issues/45)，都是具体报告，后者未解决，均不能直接证明本游戏问题。当前C2已有OpIsNan/OpSelect坐标处理，不重复盲改。既有引擎参考 nxnative `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3` / d2r-mobile `a8782caa28d7b49d1b341e34de488d3168acb86c`，MIT已读取，无外部源码复制。完整URL记录在OSS-NOTES。

本地证据：`.local/rg477m-opt-30hz-game-mode/audio-paced-transport-20261030/{fx-c2-uniforms,shield-c2-t24,shield-c2-t17,shield-c2-onload}/`。图像、shader及原始uniform仅在.local。最后一轮自动停止隔离包、关闭dump、移动probe标记为disabled，保持async0/depth1。GPU抓帧的卡顿与视频编码负载不算性能或音频验收；护罩、战斗30、A2DP全门仍未通过，不声称可玩。

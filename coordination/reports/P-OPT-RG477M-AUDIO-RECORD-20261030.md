# RG477M 系统音频记录复查（未替代常设音频验收）

设备 RG477M01445122，org.ffrs.perfisolated SHA256 `d227a982cae71ce115bbc64b35e9e19efd9d4296fd14aa96285642861185304f`。用户自行将媒体音量从静音打开到2/15，脚本没有修改音量或系统性能档。保持 target30、fence_spin0、NONE(实际NDK枚举10)、原6150-frame容量、100ms write、paced transport1。未用 LOW_LATENCY、未缩缓冲、未抬40/60。

scrcpy output/FLAC 记录 exit0：137.984秒，48kHz双声道，7,059,502 bytes，SHA256 `25daa8aa6a1a01442cd75c3af8a5cff5c6a5a838548dbcd18806683bb0d732fa`。峰值681，削波0；相邻非零10ms块完全重复0。全部≥20ms全零段保留：0–106.667ms、128–234.667ms、132.928167–137.984s。中间0.234667–132.928167s未检测到≥20ms全零断点。现场截图覆盖真实移动场景和双法师受袭剧情。

末尾5.055833s静音与脚本主动force-stop相邻：按host PLAN文件创建估算，force-stop在133.856s；捕获启动与packet尚未做精确时钟配对。因此保留静音及限制，不直接称整段无卡音。零断点、零重复或xruns0均不证明人耳连续、内容正确或实际蓝牙A2DP链路正常。

两份失败/范围不足记录未丢弃：静音条件 output135s仅得到6.997s全零，不能用作连续性证据；备用playback录音的宿主scrcpy3.3.1客户端exit-11且无产物，游戏未因此崩溃。原生PCM诊断180s容量与时序行数在本轮较晚进入场景前已耗尽，原生录音只覆盖标题；不得拿来声称剧情PCM与系统录音同窗对照完成。Host/device单调时钟不同，未做校准时禁止直接相减配对。

依据 [scrcpy官方音频文档](https://github.com/Genymobile/scrcpy/blob/f01231dff8294fe2c99045a4f9a14b233a71bb86/doc/audio.md)；output会转移设备播放，不能当真实A2DP耳听、物理AV同步或无采集性能门。常设音频完整清单仍保留在总RESULT，真实蓝牙野外/战斗各≥2min、实际断连重开等仍未全部通过。未push，未声称可玩。

原件和脚本仅.local/rg477m-opt-30hz-game-mode/audio-paced-transport-20261030/{audio-output-user-volume135,audio-story-135s,audio-playback-smoke,audio-native-story}/；正常状态下的移动帧率另行采样，不能用此录音轮数字替代。

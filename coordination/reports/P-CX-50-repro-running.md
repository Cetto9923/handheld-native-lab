# P-CX-50 同APK59复现进行中检查点

ONLY task/T02-codex-black-screen @0444c81+既有脏改。源代码未改，无reset/跨树/push/RG16重做/FPS/readback调优。

本地APK59 SHA cf72f13754a596dce629c7f40a3c51003072b06bf6bc1e370aa5c6763ee0ceb7 核验正确。设备原安装SHA4f05136b…不同，已保存installed-before.apk后按用户许可install -r原APK59，exit0。原日志已归档在run59；本次新目录 .local/codex-runs/P-CX-50-repro，不覆盖旧证据。游戏存档目录codex-local-save在启动前pull返回0 files/exit0；本次会创建OptionSaveData等测试数据，不覆盖既有文件。

启动UTC 2026-10-25T14:11:18.264568+00:00（Asia/Shanghai +8），一次COLD，PID18966，default original game_dir。真实输入key97进入NewGame，key97确认普通难度，key19选是，key97开始；随后普通key97推进开场。每次ACTION_DOWN/UP被本PID GameWindow日志记录，inputs.txt可复核。没有跳过键或改时间。

截图序列01-title→02普通难度→03确认框→04转场→05/06水晶叙事→09下一段灾难叙事→11世界秩序段→13月夜废墟。证明从菜单进入正式开场内容且有输入及持续画面变化，不证明角色自由控制/后续章节/长期稳定。旧标题3D背景截图不再被误当新游戏角色场景。显示异常留证，不以单帧宣称渲染正确。

当前最后PID观察 {'time': '2026-10-25T14:22:47.965315+00:00', 'pid': '18966', 'exit': 0}。当前快照大小177340416 bytes。关键计数 {'registry of the command packages is full': 0, 'the handle 0x0 names no command package': 0, '[guest-abort]': 0}。本次尚未复现原TIMEOUT，不能证实或排除历史run59 A/B/C/D；run49 B→A仅为对照。

采集长进程：exec session80623；命令 python3 .local/codex-runs/P-CX-50-repro/monitor.py，cwd本工作树，普通日志monitor.log，manifest.json记录PID/start。timeline.jsonl每2秒记录进程，guest-latest.log每30秒更新（并非完整历史快照链）；logcat-pid.txt仅此PID。APP_GONE/PID变化立即pull guest-end.log并结束；30分钟上限保存guest-limit.log，监测结束但不终止游戏。终态由manifest.outcome判定；monitor.log在结束时写最终manifest。不要重复启动或安装来找尾部。

下一精确恢复动作：先cat该manifest和timeline最后行；若guest-end.log出现，按同PID匹配并截TIMEOUT前80后20行分类A–E。若仅guest-latest且仍alive，继续观察当前运行或先看当前剧情界面；不得仅因上限前无崩溃就标修复。无需重新build。本轮尚无源码修复/修复回归PASS。

# P-CX-50 同APK59 30分钟受控复现收口

## 事实
工作树ONLY task/T02-codex-black-screen @0444c81+原WIP，源代码未改，无push/reset/跨树/FPS专项/readback调优/RG16重做。

用户授权同APK59复现。本地SHA cf72f13754a596dce629c7f40a3c51003072b06bf6bc1e370aa5c6763ee0ceb7正确；设备原装4f05136b…不同，已备份installed-before.apk后install -r原APK59成功。未装不同替代构建，未build。存档codex-local-save启动前pull为空/exit0，随后游戏创建OptionSaveData；未覆盖已有用户存档。

一次COLD启动UTC 2026-10-25T14:11:18.264568+00:00；guest PID18966，original default game_dir。时间线首项{'time': '2026-10-25T14:11:18.626971+00:00', 'pid': '18966', 'exit': 0}，末项{'time': '2026-10-25T14:41:09.174046+00:00', 'pid': '18966', 'exit': 0}。监测session80623在30分钟上限正常exit0，manifest.outcome=OBSERVATION_LIMIT_ALIVE；未强停游戏，采集/logcat子进程已结束。此后没有自动监测承诺。

完整末段快照guest-limit.log共511928603字节/3932282行，SHA256 bcaad4c0905c80e8ffeee3dcbb49e83e3e7b7ad961a089d50a42d9b6a2aa6b97。ADB pull exit0，约12.7秒。它是上限时仍运行应用的完整文件快照，不是一次已结束运行的guest-end，不包含观察窗口后的行为。相关APK/run/PID/输入ACTION_DOWN/UP/UTC时间线/截图/同PIDlogcat均有frozen-evidence-index.json哈希索引。

统计：{'guest_abort': 0, 'literal_TIMEOUT_EXPIRED': 0, 'sync_timeout_zero_poll': 1620, 'sync_timeout_nonzero': 0, 'registry_full': 0, 'null_handle_refused': 0, 'rg16_old_fatal': 0}。1620次SyncWait TIMEOUT全timeout=0轮询，非致命；没有非零等待TIMEOUT、literal TIMEOUT_EXPIRED、registry full、handle0拒提或guest-abort。

实际输入由标题NewGame→普通→确认是进入正式开场。截图06水晶叙事中文字幕、09下一段灾难叙事、11世界秩序、13月夜废墟、15/16龙与角色过场，图17观察末段转场；inputs.txt与同PID日志记录按键。确有故事内容及持续画面变化，不是探针/硬编码画面；但未到已验证的角色自由控制、战斗或长期稳定。严重卡顿及显示异常存在，不能称顺利运行。截图与输入事件的时间相邻不等于每次按键均能证明推动文本，也不排除自动过场推进。

## 定性
本轮结果 NOT_REPRODUCED / NOT_FIXED。历史run59仍主类E/INCONCLUSIVE（原同PID尾部缺失）。A/B在本30分钟快照未观察到；不能排除窗口外或不同路线发生。run49的B(registry满)→A(handle0拒提)完整链仅属于run49。C真实非零wait超时/D工作未retire也未由本轮证实。不归因随机Mali超时，不把timeout0正常轮询当致命。

## 卡顿的有限因果证据
用户实机“比幻灯片还低”记为NOT_SMOOTH。只使用已有engine日志，不新做FPS测试。既有一个60次引擎呈现窗口约27.5秒，CPU submit约363ms/呈现、submit/wait约197次/呈现，分项fence累计约271ms/呈现；作用域和窗口不同，CPU含等待，不可把重叠总时长相加。这些内部frame/FPS字段未验为实际独立游戏帧，不能当精确游戏FPS。

日志确认“NVN CPU reads resolved before each operation=1”。只读源码：xbg_game.cpp:3101 draw、3328 dispatch、3465 copy_texture_buffer及多个操作入口调用preflight_cpu_reads；1605在read_preflight真时调用resolve_pending；1582..1584仅有pending_writebacks/reports且非reentrant时调用gpu_sync；1009 gpu_sync调用submit_slots(true)，后者真实提交并等待。cpu_access:1606按CPU访问与待写回/报告范围重叠也会调用resolve_pending。故保守读预检/结果写回可使操作粒度串行化，是与高gpu_sync提交计数吻合的具体候选。不是每次上传必然等待：无pending时直接返回；也不能仅由聚合计数把197次逐一分配到某入口。

未证：卡顿唯一根因、GPU工作/CPU翻译各自独占占比、与历史fatal同因、删除某等待的安全性。没有关闭预检、延迟fence、伪造signal、跳帧或降低指标。

## 可安全继续的下一刀
不重复启动/继续无证据30分钟循环。最小下一刀是先为保守preflight调用点/原因与pending范围建立可审计计数及资源可见性契约：区分查询报告、CPU写回和真正重叠读，验证写回先于CPU读取、fence先于同步信号、slot/pool生命周期；只在无依赖被严格证明时减少整队列等待。不要直接把gfx-read-preflight设0或从其它游戏抄“why=a0”。

范围建议：不需要开放FPS目标/分辨率/readback/RG16/通用benchmark；若要实际改变预检和提交批次，应由父任务明确把禁令收窄为允许“本游戏同步正确性与串行等待成本”的小范围改进。当前仅完成只读因果定位，无优化diff与性能PASS。原TIMEOUT继续保留阻塞，不假装已修复；角色自由控制与顺畅正式剧情仍需验证。

证据路径：.local/codex-runs/P-CX-50-repro/manifest.json、frozen-evidence-index.json、final-log-summary.json、guest-limit.log、logcat-pid.txt、timeline.jsonl、inputs.txt、final-existing-timing-excerpt.txt及01..17截图。原run59全部证据保留。

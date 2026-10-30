# P-CX-50 严重卡顿检查点（不另开FPS专项）

用户实机反馈“画面帧率比幻灯片还低”，记为不能称顺利运行的明确事实。既有截图/开场文字/进程存活均不满足流畅/稳定可玩验收。

同APK59复现仍是PID18966，当前时间线末项 {'time': '2026-10-25T14:27:23.207247+00:00', 'pid': '18966', 'exit': 0}。没有新启动、安装、性能探针、readback、RG16改动或同步绕过。

仅分析本次已有日志：stall-log-summary.json快照中654条nvnSyncWait TIMEOUT均timeout=0轮询；非零timeout0条，guest-abort/registry满/handle0拒提计数均0。这不是原致命TIMEOUT的复现，且运行未结束，不能排除之后发生。guest-latest.log是周期pull目标，传输中可能短暂不完整，最终结论需guest-end/limit日志和采集manifest终态。

原样摘录existing-engine-timing-excerpt.txt来自guest自带既有日志，非新增benchmark：60次引擎呈现wall27471.2ms、内部fps字段2.18（不把它认作经验证的实际游戏FPS），submit_cpu363.33ms/引擎呈现、约197.5次提交和等待/引擎呈现。另一个既有分项window fence271.02ms/引擎呈现、187次，gpu_sync阻塞258ms/引擎呈现左右；GPU累计成本也非零。窗口与作用域不同，CPUsubmit含等待，不能把CPU/GPU/fence重叠时长相加。

只读源码对应xbg_game.cpp：submit_slots发vkQueueSubmit；gpu_sync调用submit_slots(true)，pending writeback/query路径会调用gpu_sync；legacy wait用真实vkWaitForFences。现有数据足以证明同步提交/等待在卡顿中成本显著，但不能据此断言唯一瓶颈、GPU空转比例或原fatal与卡顿同因。GPU真实工作/CPU编码也有成本，需要原致命证据后再决定最小改法。未删除等待/强制signal/跳帧/伪完成。

当前等待：同PID退出冻结完整guest-end.log，或30分钟观察上限guest-limit.log，采集session80623；终态看manifest.outcome。开场已由菜单进入水晶叙事、月夜废墟与后续动画，当前14-current-scene.png仍为剧情过场，角色自由控制/正式关卡连续运行未验证。下一动作先读同run当前manifest/timeline，不重启替代日志。历史run59定性仍E/INCONCLUSIVE，run49 B→A不能套用。

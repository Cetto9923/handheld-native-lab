# 荣耀开场火焰、人物和场景闪烁回归

2026-11-20 用户暂停 Antigravity 并移交排查。用户两段录屏的可复现场景：开局月夜火场，火堆和人物有单帧消失；第一章石道，地板几何一帧消失。不是全屏亮度闪烁，也不是此前第四章工厂跑分。录屏未标记 APK 指纹。

接管点 e127737 干净。候选 020ce8e 仅撤销 e127737 对 `nvnSyncWait` 的默认策略：恢复实际 GPU 完成等待。关等待时路径直接返回 ALREADY_SIGNALED；若仍有在用资源，客机可能提前覆盖数据。代码和历史记录支持优先验证，尚不把它写成两处均已确认的根因。保留 69a0730、3de9ca5 和 fe14bff。没有推送或发布。

`bash scripts/android/build_game_window_apk.sh` 的候选构建和 APK 签名检查退出 0，无本地存档密钥打包。APK：`.local/honor-opening-flicker-20261120/FFRS-Honor-opening-flicker-candidate.apk`，SHA-256 e3c31cc948f0be89014ef383ffa1d7c8253024789808f91aa7e1409355700531。荣耀 A42BBB5109000333 覆盖安装后回读一致，正向日志 `sync GPU wait on nvnSyncWait = 1`；没有卸载或清理数据。原 APK SHA-256 8d91177bc9f9bbb620da07fa87913665a420c79f88269e8f802f56d922a1c8be，保留于 display-settings 工作树的 `.local/honor-arsenal-writeback/original-apk.apk`。

正在进行本机隔离新档流程；火场和石道分别验收。若任一仍掉画面，同包继续隔离其他变化，优先对照 3de9ca5 中的 texel 复用/读回开关，不将它们和 GPU 等待一起回退后归因。性能另做无录屏的同场 A/B。

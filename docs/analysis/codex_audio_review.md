# T07-AUDIO-REVIEW 普通音频组件功能独立审查

冻结提交：`fb7d8702a2c13af5fb517717ca6c2789c441824c`。结论：**普通 host Java fake-clock/session 组件契约 PASS，未发现需修复的真实功能缺陷**。仓库只读，全部源码快照、补测、构建和日志仅在 `functional-review/audio/`。

## 源码审查

- `AudioProbeSession.java:36-85`：按实际 Track sample rate 生成 PCM16 mono；写入按已接受帧数推进 offset，0/partial/negative 返回值分别等待、继续或记录失败。真实 adapter `AndroidAudioProbe.java:85-88` 明确使用 `WRITE_NON_BLOCKING`。
- `AudioProbeSession.java:53-85`：构造耗时计入 session deadline；播放前与每轮轮询检查期限，单次等待不超过 10ms；跨过期限的调用不会被记成及时完成。`AndroidAudioProbe.java:145-146` 明确平台调用延迟不能被抢占，期限不等于对任意平台 API 的硬实时上限。
- `AudioProbeSession.java:86-112`：取消保留线程 interrupt 标志；已取得 Track 的路径最终尝试 release，stop 失败仍继续 release。总状态可为 `FAIL_CLEANUP`，同时保留独立的 `playback_status`、原始写错误与清理错误，未把清理失败隐去。
- `AndroidAudioProbe.java:28-62,124-130`：属性原值、配置 fallback、输出设备枚举与当前 track 的路由观察分别标注。44100 fallback 是命名的 probe configuration，不是设备采样率实测值。路由为空或查询失败保留 unavailable/error，没有把枚举出的设备当选中路由。
- `AndroidAudioProbe.java:113-117`：只称 explicit diagnostic tone，frame progress 不等于可听；听感固定为 `NOT_CONFIRMED_REQUIRES_HUMAN`，game audio 为 `NOT_RUN`。`DiagnosticActivity.java:122-153,298-302` 在工作线程执行，Activity pause 时只取消显式音频 session；成功情况下 UI 与文件使用同一份序列化内容。

## 本代理实际执行及真实退出码

OpenJDK 23.0.2；Java 编译使用 `--release 8 -Xlint:all,-options -Werror`（只排除 JDK 对旧 release 选项的提示，源码 lint 仍按错误处理）。

| 检查 | 编译码 | 执行码 |
|---|---:|---:|
| 原 `AudioProbeSessionTest` fake-clock/track 契约 | 0 | 0 |
| 独立 `AudioProbeAdditionalTest` 普通补测 | 0 | 0 |
| AndroidAudioProbe/Session/Result 对 API 34 SDK 编译 | 0 | NOT_RUN |

原契约覆盖 partial/zero/negative write、head stall/wrap、初始化/play/head 失败、取消、构造延迟、stop/release 失败与顺序、配置来源、低幅 PCM 时长和 ramps。

补测覆盖：播放前已带 interrupt 时不 play/write 但仍 stop/release；一次不可抢占的 write 跨过期限时记录 elapsed=2001、TIMEOUT（即使帧已写完且 head 已前进也不伪报及时成功）；route 查询失败时保留错误/未知路由与 frame progress，且 stop/release 各一次。均未改实现或放宽原测试断言。

命令 argv、cwd、输入源码/SDK 与 class 产物 SHA-256、stdout 和真实退出码见 `manifest.json`。

## 限制

这些测试没有创建真实 AudioTrack，也没有输出声音。API 34 仅是编译检查，不是 API 28-34 设备运行或硬件路由验证。Android 平台调用与清理耗时不可由 Java polling 强制抢占，报告已明确其 deadline scope。听感、真实扬声器/耳机/蓝牙路由、设备清理行为和音频端到端均由总控/用户另行确认；本轮未使用 ADB，未执行游戏、NSO、TP 或旧中止 QA/安全流程。当前授权组件范围无待修代码缺陷及剩余阻塞。

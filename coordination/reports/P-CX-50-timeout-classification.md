# P-CX-50 — run59进3D后TIMEOUT取证定性

事实：工作树 task/T02-codex-black-screen，HEAD0444c81ea36c92406cd10d7230b35d7620510aed，现有大量dirty/untracked保留；本轮无源码修复、构建、安装、重启、RG16重做、FPS/readback、push或跨树修改。

P-MM-48 CLOSED_HALF_SUCCESS：按run59 manifest，--gpu-proxy PASS exit0，binary SHA778ec5fe51771985686573ac09f6162fe79ec734b15a424924fae5338fe3dc47；APK cf72f13754a596dce629c7f40a3c51003072b06bf6bc1e370aa5c6763ee0ceb7，game_dir original。旧RG16 attachment FATAL在本地run59日志计数0。用户/派工现场报告UI路径进入3D飞艇甲板；查看07-scene-3d.png确有3D甲板与角色，但仍有标题/NewGame覆盖层，因此截图自身不证明可操作场景或稳定游玩。没有沿用旧主树0帧叙事，也无帧率验收。

P-MM-49 CLOSED_SUPERSEDED_INCOMPLETE：由P-CX-50接管其未完成取证，不记为修复通过。

P-CX-50 OPEN, 分类主因 E / INCONCLUSIVE（证据不完整）。现场报告pid5964在Asia/Shanghai约21:34:47存活、21:35:13 APP_GONE，致命NVNSubmission.cpp:315 GPU interrupt wait TIMEOUT_EXPIRED；本轮无法取得同一运行的abort前因窗口。

原始证据：本地guest.log 150538行/14811136字节，末行截断。abort=0、TIMEOUT=0、null handle拒提=0、registry full=0、submit refused非零=0、shadow missing47条（节流累计计数日志，不等于47个对象/事件总量）。这不排除缺失尾部发生A/B/C/D。

按manifest准确路径pull /sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log -> guest_boot-device-full.log，exit0；但内容4544205字节/62755行，包含192条nvn-package-audit decoded_only记录、无run59音频pid5964头，属于不同启动，排除用作run59尾部。当前设备文件有TIMEOUT但不是目标运行，绝不能混接因果。crash缓冲空；当前logcat2000行无ffrs/5964/TIMEOUT命中。故run59完整尾部NOT_AVAILABLE，非权限拒绝。

已冻结：P-CX-50-log-index.json（各日志大小/哈希/计数）、timeout-window.txt（明确NOT_AVAILABLE + run59最后80行）、non-run59-device-timeout-window.txt（隔离的异轮设备窗口，不作run59结论）、run49-comparison-timeout-window.txt（前80后最多20行）、crash.txt、logcat-current.txt/logcat-timeout.txt。原guest.log未覆盖。

对照事实（仅run49）：798968/798969 registry满，798970 handle0拒提；798978..798980重复，798999出现TIMEOUT，799003 guest-abort。这是run49的B→A链，不是run59的已证主因。shadow missing日志自己说明actual engine validation unchanged，不能据此判致命。

未证：run59是否A lost/null handle、B registry耗尽、C真实sync未signal、D工作未retire/flush；也未证Mali随机驱动超时。没有一行级修复证据，最小修SKIPPED。下一刀：获得用户允许的一次同APK59/原game_dir受控复现，在APP_GONE瞬间冻结同pid完整guest_boot（同时保留registry/submit/fence/retire前因），再按A–D定位；不预先扩大registry或伪造sync成功。本轮只提建议，不执行复现。

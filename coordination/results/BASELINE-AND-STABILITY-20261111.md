# 2026-11-11 Honor 基线冻结与稳定修改边界

记录的是这次核对的事实。没有在教程进程上重装，没有改 marker，没有操作 RG477M。

## 身份

- 本地标签 `baseline-honor-20261111` 指向 `df3e108`，只在本机，没有推送。
- 设备 `A42BBB5109000333` 前台仍是 PID `31943`。包名 `org.ffrs.gamewindow`，manifest 仍是 versionName `1.0.5` / versionCode `5`。
- 已安装包是 `FFRS-v1.1.1-stable.apk`。整包 SHA256 `f6cdd25c182e007fce022d5f6fbaa0dc2f1d0ceb26f9bafb685b92b0ad406ac9`。
- `lib/arm64-v8a/libffrs_game_window.so` SHA256 `b1f092ee15f0d630e5883416cf825fbf896a0570d2a3c510f1837b8b6b02d970`，与 `df3e108` 的原生源码对应。没有为了再对一次哈希去重编，因为重编不会得到同一个 so。
- `classes.dex` SHA256 `18a08b56401e3b9880a7cf5ba0521e8dbc612428ee40ae7ed670c8e31c1318c6`。它不包含 `37de946` 的 Java 改动。源码比正在跑的 dex 多这一笔。
- 回滚就是装回 `FFRS-v1.1.1-stable.apk`。教程还在前台时不要装。

## 外部采集

证据在 `.local/baseline-freeze-20261111/`，不进 Git。采集时没有注入输入，所以这不是土之神殿、大地图或战斗的验收。

- 画面是教程里的主菜单。
- PSS TOTAL `7553948` kB，Graphics `3331484` kB，Native Heap `1074428` kB，SWAP PSS `162730` kB。
- `/proc/31943/maps` 10009 行。
- `engine.log` 在 19:08 为 438542764 字节。末尾 200000 字节里没有 FATAL。
- `codex-frame-31943.csv` 35340 帧，跨度 1223.5 秒，整体 28.88 FPS，P50 33.32 ms，P95 33.92 ms，P99 37.98 ms，最大 6363.6 ms，超过 50 ms 的 150 帧，超过 100 ms 的 48 帧。
- 最后 1800 帧约 60.8 秒：29.59 FPS，P50 33.31 ms，P95 41.83 ms，P99 53.17 ms，最大 88.0 ms，超过 50 ms 的 23 帧，超过 100 ms 的 0 帧。
- 标题或菜单上的帧率不能当作场景帧率。

当时的 marker：

| 文件 | 内容 |
|---|---|
| gfx-async-pipelines | 0 |
| gfx-audio-paced-transport | 1 |
| gfx-codex-audio-dump | 0 |
| gfx-codex-frame-metrics | 1 |
| gfx-depth-aspect | 1 |
| gfx-fence-spin-us | 0 |
| gfx-legacy-waitall | 0 |
| gfx-readback-cached | 1 |
| gfx-target-fps | 30 |
| gfx-slot-count | 不存在，默认 16 |
| gfx-shader-reuse | 不存在 |
| gfx-honor-compute-prewarm | 不存在 |
| gfx-memory-profile | 不存在 |
| gfx-slot-count.codex-two-slot-backup | 2，保持原名，不要把它改成生效的 slot 文件 |

## 修改边界

冻结，单一写者登记之前不再加功能：

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp`
- `GameActivity.java`、`SaveCheater.java`、`GuestMemory.java`、`UnitNames.java`
- `scripts/android/build_game_window_apk.sh` 和 `AndroidManifest.xml`
- `coordination/state.json`、`HANDOFF.md`、`STATUS.md` 只由总控写

稳定修复在独立工作区 `.worktrees/stability-20261111`，分支 `task/T-stability-20261111`，HEAD `58901a8`。不要删这个 worktree。共享分支上还没有 cherry-pick。

三方都不做：把 guest 的无限等待改成超时返回；把致命退出改成丢掉 slot 继续跑；在 `nvn_dirty_since` 仍恒返回 0 时假装打开 vcache/wtrack；直接缩小 `keep_chunks`；没有设备对照时改 `slot_count` 默认值；再把限帧移出引擎锁。

## 主机上已经验证的修复

分支上的提交：

- `7558d87` 存档 fsync 移出全局文件锁，返回前数据仍已落盘
- `c600edc` 事件要等等待者和借用者都离开才释放
- `3b7858b` `engine.log` 超过 64 MB 只滚动一次，致命退出另写一个小文件
- `25cb34e` present crop 夹到源图内，`vkDeviceWaitIdle` 的失败会记日志
- `a0ef958` 音频设备变更只唤醒线程，打开 AAudio 时不持有输出锁
- `58901a8` macOS 上能编过 mutex 和 exit-registry，并把上面的契约接进主机套件

`python3 scripts/verify_host.py --runtime-contracts` 里能跑的检查都过了，包括事件契约的 ASan 和 TSan，以及主机上的存档契约。`module-load-info` 仍是 BLOCKED，因为本机没有解开的游戏模块。脚本的总 `passed` 因此是 false。没有去取游戏模块。

存档和音频的真机契约是 NOT_RUN。教程进程还在，没有装带这些修复的包。

## 阶段 C：marker 对照，NOT_RUN

PID 31943 还在教程里，所以没有改 marker，也没有重启。下面的命令留到这次会话结束、并且使用同一存档时再跑。一次只改一个量。冷启动和暖缓存分开记。判据是 PSS、map 行数、P95、P99、超过 100 毫秒的帧数。卡顿变多或者内存没有明显下降就回到缺省。

slot 对照，场景用土之神殿加载。缺省是 16。不要把备份文件改名成 `gfx-slot-count`，那个备份的内容是 2。

```
adb -s A42BBB5109000333 shell 'echo -n 10 > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-slot-count'
```

测完 10 再写成 8。回到缺省：

```
adb -s A42BBB5109000333 shell rm /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-slot-count
```

compute prewarm：

```
adb -s A42BBB5109000333 shell 'echo -n 1 > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-honor-compute-prewarm'
```

撤回就是删掉这个文件。

## 阶段 D：交给对应写者

这些文件本轮没有改。

出包。文件是 `scripts/android/build_game_window_apk.sh` 和 manifest。把 versionName / versionCode 调到和 APK 文件名一致，并把 `git rev-parse --short HEAD` 写进输出文件名和包内一个可读取的字符串。一个 APK 对应一个提交。教程还在时不要 `adb install`。会话结束后若要让设备上的 dex 跟上 `37de946`，从目标提交重新出包再 `install -r`，这会保留数据，但会结束当前进程。

`xbg_game.cpp`。等 B、C 有设备数据后再排：arena 滞回收缩、冷 shader 预载、真实脏页跟踪、按范围回读同步。`keep_chunks` 和默认 slot 数保持不动，直到对照数据出来。

`GameActivity.java` 的 `startGuest`。`GUEST_START.begin()` 之后，isolated regression save 的 catch 在约 2648 行直接 return，没有 `GUEST_START.finish(false)`。补上这一行。同一 catch 前面已经把 `guestStarted` 设成 true，写的人要一起看这个状态会不会卡住下一次启动。

RG477M 在本仓库是 BLOCKED。

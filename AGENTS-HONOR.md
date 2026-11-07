# Honor GT Pro agents 约束

1. 工作目录必须是 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`（本树）。
2. 设备 ONLY `adb -s A42BBB5109000333`。
3. 禁止操作序列号 `RG477M01445122`；禁止 `adb kill-server`；禁止卸载 RG477M 包。
4. `.local/romfs` 是指向基线 release-pack 的符号链接——只读使用，勿 `rm -rf` 基线数据。
5. 不要 push；不要改 `main`；RG477M 基线仓库并行测试时不要抢同一 APK 包名安装到 RG477M。
6. 常设音频验收：`coordination/prompts/_STANDING-audio-acceptance.md`。
7. 任务提词：`coordination/prompts/P-OPT-HONOR-GTPRO-30hz-game-mode.md`。

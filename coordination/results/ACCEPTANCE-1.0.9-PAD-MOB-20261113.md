# 1.0.9 真机流程验收（平板 + RG477M）— 2026-11-13

观察时刻：2026-11-13 01:30Z 起 / 00:30 CST 起
操作者：总控（MiniMax Code /root）
统一包：versionName 1.0.9 / versionCode 9
APK sha256：`3309cfc893d0e0e8bde020408b6d6cff3ebb6720c0b1d6a6cf269b1ff7164dfa`
release 分支 HEAD：`4156929`（`dc3bbb9` → `4156929`），工作区干净
设备：荣耀平板 `A42BBB5109000333`（Adreno，code 7）/ RG477M `RG477M01445122`（Mali，code 8）

## 已验证（PASS，有设备日志与截图支撑）

| 项 | 平板 | RG477M | 证据 |
| --- | --- | --- | --- |
| 标题画面出现 | PASS | PASS | `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/acceptance-1.0.9-honor-20261113/PAD-flow/`、`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/acceptance-1.0.9-honor-20261113/MOB/` |
| Continue → 存档读入 | PASS | PASS | 同上 |
| 进入 3D 燃烧废墟场景 | PASS | PASS | 同上 |
| 场景内实际走动（位移可见） | PASS | PASS | 小地图箭头位置随时间变化 |
| 进程存活 / `engine.log.fatal` | PASS（无） | PASS（无） | 现场 `ls engine.log.fatal` = NO_FATAL |
| 30 FPS 持续出帧 | PASS | — | `codex-frame-9906.csv` 帧号 26220→26400 / 6 s |

平板实测 pid 9906，RG477M 实测 pid 25064，两台均 `install -r`（存档保留，未卸载）。

## 本轮新查明的事实（可复用）

1. **虚拟手柄默认被隐藏 → 触摸推杆失效。**
   `GameActivity.discoverPad()` 只要检测到外接手柄（本平板接的是 **DualSense Wireless Controller**，device id 96），
   就把 `touch-pad` 标记视为「不需要虚拟手柄」，虚拟摇杆隐藏 →
   `publishPad()` 的 `touchOn=false` → `touchLx/touchLy` 永远为 0 →
   历史脚本用的 `input touchscreen motionevent` 拖左下角根本推不动角色。
   **解法**：点顶部 `PAD +` 手柄把手（或把 `files/touch-pad` 写成 `1`）把虚拟手柄显示出来，
   左下角 `LS` 摇杆即可用。本轮即因此从「走不动」变成「能走」。
2. **`input keyevent` 的按下时长只有约 3 ms，30 Hz 轮询的游戏收不到。**
   必须用 `input gamepad keyevent --duration <ms>`（历史记录里的 160 ms），
   或改用屏幕上的虚拟手柄按钮。日志 `game_input key=… nvn_bit=…` 可证明事件确实到达宿主。
3. **`KEYCODE_BUTTON_SELECT` 在荣耀平板被系统劫持**（会切出任务切换器/微信），不能用来开小地图；
   改为**直接点屏幕上的「−」按钮**（约 `(1260, 1770)`，即 `SELECT` 位）才是安全路径。
4. **`KEYCODE_BUTTON_START` = 系统菜单**；系统菜单里「读取」可进 LOAD DATA。
   LOAD DATA 列表里有游戏自己写的 **`战场`（战场）存档**：
   `Auto Save`（2026-11-12 14:38:05, Lv.1）与 `01`（2026-11-11 06:17:08, Lv.1）。
   载入 `Auto Save` 会回到第 1 章开头场景（Lv.1 单人），这是目前**最确定的进战斗入口**。

## 未能验证（NOT_RUN / 未闭合）

1. **平板战斗全流程（进战斗 → 打完整场 → 战斗结算画面不闪退）：仍未闭合。**
   已排除「输入没送达」「角色没动」「进程已死」三种可能：
   平板与 RG477M 各跑满 **400 步**（每步 3.4 s 持续推摇杆，约 43 min），
   小地图箭头位置持续变化 ⇒ 角色确实在走；`engine.log.fatal` 始终不存在 ⇒ 过程中无 FATAL。
   **判定：当前区域（燃烧废墟）随机遭遇不触发**，不是移植问题，是玩法路径问题。
   期间确认「向东走」会切场景（出现加载黑屏后进入新场景），说明区域之间可达，
   但遍历到的区域都没有遭遇表。
2. **RG477M 的战斗同样未闭合**，且更早就被设备侧卡住：
   **RG477M 于 2026-11-13 约 08:35 CST 从 adb 设备列表消失**（USB 掉线，非 `adb kill-server`；
   本项目禁止 kill-server）。之前已完成的标题/读档/进场景/走动证据仍然有效。
   ⇒ **需要用户重新插拔 477M 的 USB 后才能继续 477M 战斗验证。**
3. 战斗特效卡顿的真实帧数据：依赖战斗跑通，仍 NOT_RUN。
4. 大地图卡顿：仍只归因不改，等用户拍板。

## 明确不主张的内容

- 未证明「完整游戏兼容」，只证明上述流程段可玩且不闪退。
- 未把「装上了/标题出了/能走动」当成「进战斗实际玩」。
- 战斗特效卡顿没有新数据，不给任何性能结论。
- 平板与 477M 的两段证据来自同一个包，但**不是同一段流程**：平板走到战斗入口前，
  477M 因掉线停在场景内。

## 复现命令（honor 仓上下文，平板）

```bash
ADB=$HOME/Library/Android/sdk/platform-tools/adb; PAD=A42BBB5109000333
$ADB -s $PAD shell input tap 1500 40          # 显示虚拟手柄（外接手柄在场时默认隐藏）
$ADB -s $PAD shell input touchscreen motionevent DOWN 363 1477
$ADB -s $PAD shell input touchscreen motionevent MOVE 363 1287
sleep 3.4
$ADB -s $PAD shell input touchscreen motionevent UP 363 1287
$ADB -s $PAD shell input gamepad keyevent --duration 200 KEYCODE_BUTTON_START   # 系统菜单
$ADB -s $PAD shell input gamepad keyevent --duration 200 KEYCODE_BUTTON_A       # 读取
$ADB -s $PAD shell input gamepad keyevent --duration 180 KEYCODE_DPAD_UP       # 选 Auto Save
$ADB -s $PAD shell input gamepad keyevent --duration 200 KEYCODE_BUTTON_A       # 是
```

脚本：`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/acceptance-1.0.9-honor-20261113/PAD-flow/hunt7.sh`（坏）、`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/acceptance-1.0.9-honor-20261113/PAD-flow/hunt8.sh`（400 步巡游，含战斗探测）、
`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/acceptance-1.0.9-honor-20261113/PAD-flow/hunt9.sh`（载入 战场 存档后向北找教程战斗）、
`/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/acceptance-1.0.9-honor-20261113/MOB/hunt7.sh`（477M 同款巡游）。

# Honor 月亮光晕 depth-aspect 与金手指即时生效（2026-11-12）

分支 `task/T-rescue-1.0.7-20261112`（worktree `.worktrees/rescue-1.0.7-20261112`，基线 b484c9f）。
本报告只覆盖本卡的两个任务：任务 A（月亮光晕）与任务 B（金手指即时生效）。
真机未操作（设备 A42BBB5109000333 在用），所有结论来自源码与已有真机产物。

---

## 任务 A：月亮光晕 —— depth-aspect 复用被在 Honor 上误开

### 1. 根因（与我读到的代码一致）

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp:6441-6445`（改前）：引擎判定 Honor 的条件是
  `FFRS_PROFILE_TABLET=1` 且 `properties.vendorID==0x5143` 且 `properties.deviceName=="Adreno (TM) 735"`，
  注释写明"Adreno 735 复用 open depth/stencil pass 会让真实场景月亮光晕闪烁"。
- 改前的 marker 读取（同文件 6454-6457）：`depth_aspect_enabled=!honor;` 然后
  `if(m){depth_aspect_enabled=fgetc(m)!='0';}` —— **只要 marker 文件首字符不是 '0'，就强制打开**。
- `src/android/app/java/org/ffrs/diagnostic/GameActivity.java:1990`（改前）：
  `writeMarkerIfMissing(new File(dir, "gfx-depth-aspect"), "1\n");`
- `src/android/app/java/org/ffrs/diagnostic/DataBundleInstaller.java:258`（改前）：
  `marker(data, "gfx-depth-aspect", "1\n");`（这是**无条件覆盖**，不是仅在缺失时写）

结论与任务描述一致：App 把一条被标注为"在 Honor 上会闪烁"的路径在 Honor 上默认打开了。

补充（任务描述未提到、但影响修复落点）：引擎读 marker 的目录是 **shader 目录的父目录**，
而 App 写 marker 的目录有两处：

| 模式 | App 写 marker 的目录 | 引擎读的目录 |
|---|---|---|
| managed / 目录模式（Honor 平板走这条） | `getFilesDir()/runtime/<versionId>`（`DataBundleInstaller.prepare()` → `data = runtime` 之后才写 marker） | 同一目录 ✔ |
| 非 managed | `getExternalFilesDir(null)`（`GameActivity.ensureDefaultMarkers()`） | 同一目录 ✔ |

依据：`guest_boot_jni.cpp` 里 `shaders = <log 目录>/shaders`，`log_path = files + "/guest_boot.log"`，
`files = managed ? dataInstaller.runtimeRoot : getExternalFilesDir(null)`（`GameActivity.java:2626-2665`）。

### 2. 方案：判定留在引擎，App 只写中性的 "auto"

**为什么不把 Honor 判定搬到 Java**：
`DeviceProfile`（`src/android/app/java/org/ffrs/diagnostic/DeviceProfile.java`）只带
`id / targetFps / targetShortEdge`，**没有任何 GPU 信息**；`selectedDeviceProfile` 同样没有。
Java 侧唯一可靠知道 GPU 的是 Vulkan 物理设备属性，而那正是引擎已有的判定源。
在 Java 里再造一份 GPU 判定就是"凭空猜 GPU 型号"，与任务约束冲突。
因此：**决策留在引擎（唯一知道 GPU 的地方），App 写一个中性值，把决定权交回去。**

marker 文件语义（改后）：

| marker 内容 | 含义 |
|---|---|
| `1` | 强制打开 depth-aspect 复用（玩家显式覆写） |
| `0` | 强制关闭 |
| `auto`（默认）/ 其它值 / 文件缺失 | 用设备默认值：Honor 平板 = 关，其它平台 = 开 |

### 3. 改动（文件:行号）

**`src/graphics/nvn_vulkan/engine/xbg_game.cpp`**

- 新增 `xbg_resolve_depth_aspect()`：**6409-6429**（紧邻 `xbg_game_initialize` 之前）
  - `6410`：`static const char *depth_aspect_source="device default";`
  - `6411-6429`：解析 marker；默认 `depth_aspect_enabled=!honor`
  - `6417-6424`：**旧 App 默认值迁移**——文件内容恰好是 App 自己写的 `"1\n"`（2 字节）时，
    就地改写成 `"auto\n"` 并回到设备默认值；来源标记为 `legacy marker migrated`
  - `6426-6428`：`1` → 开，`0` → 关，其它 → 设备默认值
- 调用点：**6486-6487**
  - `xbg_resolve_depth_aspect(cut==std::string::npos?dir:dir.substr(0,cut),honor);`
  - 日志改为 `NVN depth-aspect sample reuse=%d (honor=%d; %s)`，第三段是来源
    （`device default` / `marker` / `legacy marker migrated` / `marker (device default)`）
  - Honor 判定本身（`6441-6445`）**未改动**，仍是 profile + vendorID + 设备名三重条件

**`src/android/app/java/org/ffrs/diagnostic/GameActivity.java:2069-2075`**
- `ensureDefaultMarkers()` 里的 `gfx-depth-aspect` 默认值由 `"1\n"` 改为 `"auto\n"`，
  并加注释说明原因。仍是 `writeMarkerIfMissing`：**玩家手动写过的值不会被覆盖**。

**`src/android/app/java/org/ffrs/diagnostic/DataBundleInstaller.java:257-261`**
- `prepare()` 里的 `gfx-depth-aspect` 由 `"1\n"` 改为 `"auto\n"`（这一处本来就是无条件覆盖，
  保持原有行为，只换默认值）。

### 4. 为什么旧设备上的文件会被纠正

Honor 平板上现存的 marker 是旧 App 写的 `"1\n"`（真机实测：文件存在、内容 "1"）。
两条路径都会把它纠正：

1. 引擎侧：读到恰好 `"1\n"` 就地迁移为 `"auto\n"` 并使用设备默认值（Honor = 关）。
2. App 侧：managed / 目录模式下 `DataBundleInstaller.prepare()` 每次启动都会把 marker 重写为 `"auto\n"`。

**取舍（需要总控知晓）**：`"1\n"` 这个精确字节序列无法区分"App 自己写的"和"人手敲的"。
本实现按"App 自己写的"处理（迁移），因此玩家若想在 Honor 上强制打开，必须写 `1`（不带换行）；
这是显式覆写，仍优先于设备默认值。

### 5. 如何自证（真机日志，尚未在设备上复跑）

Honor 平板启动后，`guest_boot.log` 里应出现：

- 首次启动（还是旧的 `"1\n"` 文件）：`NVN depth-aspect sample reuse=0 (honor=1; legacy marker migrated)`
- 之后启动：`NVN depth-aspect sample reuse=0 (honor=1; device default)`
- 非 Honor 平台：`NVN depth-aspect sample reuse=1 (honor=0; device default)`
- 若玩家显式写了 `0`/`1`：`... (honor=1; marker)`

不应再出现 `NVN depth-aspect sample kept serial=... count=N`（那是复用生效时的日志，见 `xbg_game.cpp:1805`）。

真机复跑 **NOT_RUN**（设备在用，按卡要求不碰设备）。

---

## 任务 B：金手指"即时生效"

### 1. 实时内存机制（查清结果）

| 环节 | 位置 | 结论 |
|---|---|---|
| 读写 guest 内存 | `src/android/app/java/org/ffrs/diagnostic/GuestMemory.java` | `regions()` 解析 `/proc/self/smaps`；`find()`/`peek()` 读；`walk()` 单次遍历；**`poke(long address, byte[] data)`（以 "rw" 打开 `/proc/self/mem`）已经具备写能力** |
| 为什么能读写 | 同上类注释 | guest 是本进程内的一个线程，它的页就在本进程地址空间里，所以 `/proc/self/mem` 就是 guest 内存 |
| "扫描实时数据"入口 | `GameActivity.java:1443 startGuestScan()` → `ScanTask` → `GuestMemory.walk(collector)` | **只读诊断**，产出 `guest-unit-scan.txt` / `guest-strings.txt` |
| 扫描到底定位了什么 | `GameActivity.java:1105 scanIds()` | **只定位 unit id**，并打印命中点前后 96 字节的 int/文本窗口；**没有定位 HP/MP/EXP/等级字段** |

结论：**工程里已有"读/写 guest 内存"的能力，但从未定位到运行期的单位数值字段。**
"扫描实时数据"是"用来认运行期布局"的取证工具，不是"已经认出来了"的实现。

### 2. 存档字段与运行期字段的关系：**不是同一套结构，偏移不一致**

- 存档明文体是 **UE 属性标签序列化**：每条 `IntProperty` 标签 = `name_len + name + type_len + "IntProperty" + arr_idx + size + 1 字节 GUID 标志 + int32 值`
  （解析见 `SaveCheater.java:194-243`）。
- 存档里每个单位是 `UnitId / Experience / HitPoint / MagicPoint` 四条标签；队伍上限是名册之后**另外两条**
  `HitPoint / MagicPoint`（`SaveCheater.java:284-320` 的 `readCaps()`，注释指到 `SaveData_UnitGuest`）。
- 运行期的单位数据是 guest 内存里的**运行期结构**（`FUnitParameter` / `SaveData_UnitGuest` 之类的反序列化产物），
  其字段偏移与存档标签偏移**没有任何对应关系**。

⇒ 任务描述里"存档文件里的字段布局与实时内存里的字段是同一套结构吗？偏移一致吗？"的答案是：
**不是同一套结构，偏移不一致**。"读实时内存定位地址 → 解密存档得到同样偏移 → 同时写两处"这条路**不成立**，
存档偏移无法推导运行期偏移。

### 3. 已有真机产物的复核（只读分析 `.local/cheat/scan/`）

- `guest-unit-scan.txt`（303 行）/ `guest-unit-scan2.txt`（451 行）：每个单位 12 条命中（受 `MAX_HITS=12` 截断）。
- 命中的 unit id 表在 `0x02804xxx` 一带，条目间距由条目内 `id-12` 处的 size 字段给出（216/328/112/248/280），
  **该结构里只有 id 和若干标志位，没有 HP/MP/EXP**。
- 关键否证：存档里的 HP（5418 等）在整个扫描产物中**只出现 1 次**，就是文件头那行 `file unit 1002 hp=5418/9999`；
  MP（489）出现 0 次。出现的 `9999` 属于另一张参数表（`1023 1013 1073 1027 9999 100 1061 1032 1001 ...`），
  与单位数值无关。
  ⇒ **运行期的 HP/MP/EXP 没有落在 unit id 命中点附近，扫描没有定位到它们。**
- `rosterblob` 命中（scan1 有 6 个、scan2 有 2 个）：扫描用的是"存档明文里 HP 字段前 16 字节起的 32 字节片段"，
  命中说明**序列化的名册片段在进程内存里也存在**。其中落在 App 堆（0x6d…）的那几个是扫描器自己解密出来的副本（自命中）；
  落在 guest 地址区（0x089a…、0x08a0…、0x0153…）的几个**性质未定**：
  可能是 guest 载入存档后留下的明文缓冲，也可能是别的拷贝。
  **它们是否就是游戏运行时读取的那份，NOT_CONFIRMED**；即使改写，也不等于改到运行期数值（那只是序列化缓冲，不是运行期结构）。

### 4. 采用的方案：**安全替代方案**（不改写运行期内存）

理由：运行期数值结构**尚未定位**（`2、`3）。在没有真机证据的情况下改写"看起来像"的位置，
一旦地址认错就是改坏 guest 内存（可能崩溃或污染无关状态），而存档是玩家唯一的进度。
按卡要求"不要硬做、不要伪造已即时生效"，本轮实现替代方案：

**a) 存档写入路径保持不变，但把"本局不生效 / 何时生效 / 为什么"讲清楚**

- `src/android/app/java/org/ffrs/diagnostic/SaveCheater.java:466-472`
  成功消息改为：
  `已修改 N 项（已备份原存档，可用菜单还原）` +
  `本局不生效：运行中的数值在 guest 内存里，存档只改到下次读档` +
  `回标题画面 Load Game 重新读档后生效`
- `src/android/app/java/org/ffrs/diagnostic/GameActivity.java:871-876`：金手指弹窗顶部固定说明
  "改动写入存档文件，本局不生效；回标题画面 Load Game 重新读档后生效。/ 原因：运行中的数值在 guest 内存的运行期结构里，该结构尚未定位，改写它有损坏本局的风险。"
- `GameActivity.java:826-836`（`openCheatDialog` 的类注释）同步记录该限制。

**b) 存档安全校验：加硬 + 用测试证明**

- `SaveCheater.java:627-635`（`put()`）：写之前校验偏移必须是**明文体内的完整 int32**
  （`offset < 0 || offset + 4 > plain.length` → 记为"偏移非法（已跳过）"，不写）。
  （偏移本身来自标签解析，解析时已做过边界检查，这里是第二道闸。）
- 原有校验全部保留：改前 SHA-1 覆盖 body 不匹配就拒绝写（`SaveCheater.java:394-397`）、
  改后长度必须一致（`SaveCheater.java:436-439`）、写完回读解密并与内存中的明文逐字节比对（`SaveCheater.java:449-462`）、
  首次修改前把原存档另存为 `<slot>.orig`（`ensureBackup()`）、写文件用临时文件 + rename 以免写坏（`install()`）。
- 加密往返自洽：AES/ECB/NoPadding 是确定性加密，"解密→再加密→再解密"必须回到原值，已写成断言（见 `5`）。

**c) 为"即时生效"补上缺失的那块证据（只读，不写 guest 内存）**

`GameActivity.java` 的扫描现在**额外定位名册数值在 guest 内存中的位置**：

- `1093-1103`：构造时把每个单位的 HP/MP/EXP（int32 与 float32 两种表示）登记进开放寻址表
- `1149-1159`：`registerLive()`；`1163-1193`：`scanLiveValues()`（**只读 4 字节对齐的字**，命中即打印地址 + 邻近 int）
- `1336-1341`：扫描结束写 "livevalue summary"，列出每个名册数值的命中数
  —— 命中数为 0 就直接说明"该数值在内存里不是这个数"（可能是本局已经变了，或它不是以数字存放）

命中地址若呈现"每个单位同一相对偏移 + 固定间距"，即可反推运行期结构，
下一轮才能在**严格校验**下写运行期内存（例如：8 个单位全部命中、偏移一致、唯一解，
再读回校验），并同时写存档，做到即时生效且退出后保留。**本轮没有实现这一步。**

### 5. 自测

**任务 B 的往返自洽（新写）** `tests/device/SaveCheaterRoundTrip.java` + `scripts/android/test_save_cheater.sh`
（宿主侧 Java，不需要设备、不碰真存档；用自己生成的 32 字节 key 和合成的 SVHD 存档）：

覆盖：
1. **解密→再加密→再解密 == 原值**（AES/ECB/NoPadding 恒等断言）
2. 编辑前 `inspect()` 读出的名册（id/HP/MP/EXP/上限）与预期一致
3. `apply()` 后回读文件：magic 正确、**SHA-1 仍覆盖 body**、解密后 HP/MP/EXP/上限/等级都是新值
   （等级不在 `inspect()` 的名册里，单独按标签解析校验 body）
4. 文件长度不变；`<slot>.orig` 备份与原文件逐字节相同
5. **body 被篡改（digest 不匹配）→ 拒绝写入，文件一个字节都没变**
6. 非 SVHD 文件 → 拒绝
7. 成功消息必须包含"本局不生效"

**任务 A 的 marker 语义（新写）** `tests/device/test_depth_aspect_marker.py`：
把 `xbg_game.cpp` 里**真实的** `xbg_resolve_depth_aspect()` 抠出来单独编译运行（沿用
`tests/device/test_target_fps_marker.py` 的做法），覆盖：无 marker / `auto` / `0` / `1`（无换行，手写）/
旧默认 `"1\n"`（并断言文件被迁移成 `auto\n`）× honor 真假；另加一条源码断言：App 两处都写 `auto\n`。

### 6. 自测命令与实际输出

```
$ python3 -m unittest tests.device.test_depth_aspect_marker -v
test_app_writes_the_neutral_default ... ok
test_marker_precedence_and_defaults ... ok
Ran 2 tests in 0.313s
OK

$ python3 -m unittest tests.device.test_profile_environment tests.device.test_target_fps_marker -v
Ran 4 tests in 1.793s
OK

$ bash scripts/android/test_save_cheater.sh
W/FFRS-SaveCheat: save backup written: .../GameSaveData0.orig
save round trip checks passed; the running session is not touched

$ javac -nowarn -d /tmp/ffrs-javac-check -cp ~/Library/Android/sdk/platforms/android-34/android.jar \
      -source 11 -target 11 src/android/app/java/org/ffrs/diagnostic/*.java
javac_exit=0     （仅一条 deprecation 提示）

$ bash scripts/android/build_game_window_apk.sh
exit_code[cmake-build-game-window]=0     （xbg_game.cpp 编译通过；4214/4864/5729 的 warning 是既有代码）
exit_code[d8]=0 / aapt2=0 / zipalign=0 / apksigner-sign=0 / apksigner-verify=0
APK created: .local/android-apk/ffrs-game-window-debug.apk
sha256: c80f9ce6a495e959b6b70a5006a48a609d3b2e6bbbddbd6671288f16c7b5c0ba
build_exit=0
```

### 7. 未完成 / 风险项

- **NOT_RUN（真机）**：Task A 的 Honor 平板复跑（设备在用，按卡要求未操作设备）。
  需要真机确认：Honor 启动日志出现 `depth-aspect sample reuse=0 (honor=1; ...)`，
  且不再出现 `depth-aspect sample kept`；月亮光晕不再闪烁。
- **NOT_RUN（真机）**：Task B 的 `livevalue` 定位需要在设备上跑一次"扫描实时数据"，
  用新报告里的命中地址判断运行期布局是否可反推。本轮只交付了定位能力，没有结论。
- **NOT_DONE**：金手指"本局即时生效"本身——运行期数值结构未定位，本轮按卡要求没有硬做，
  没有实现任何对 guest 内存的写入（`GuestMemory.poke` 未被本轮任何代码调用）。
- **NOT_CONFIRMED**：`.local/cheat/scan` 里 `rosterblob` 在 guest 地址区的命中，
  是 guest 留存的存档明文缓冲还是别的拷贝，未确认；即使确认，也不等于运行期数值结构。
- **风险**：`"1\n"` 旧默认值的迁移会把玩家手写的 `"1\n"` 也一并迁移（Honor 上被强制打开的路径）。
  规避方式：写 `1`（不带换行）。若总控认为该语义不可接受，需要另设 token 并接受"旧文件不自动纠正"。
- **风险**：`auto` 是本项目 marker 里的新 token。引擎对 `gfx-depth-aspect` 之外的所有 marker
  仍是"非 '0' 即真"的旧语义，未受影响；但若有外部脚本按 0/1 解析该 marker，需要同步（仓库内已确认无其他读取点）。

---

## 改动文件清单

| 文件 | 改动 |
|---|---|
| `src/graphics/nvn_vulkan/engine/xbg_game.cpp` | 新增 `xbg_resolve_depth_aspect()`（6409-6429），改调用点与日志（6486-6487） |
| `src/android/app/java/org/ffrs/diagnostic/GameActivity.java` | marker 默认值 `auto\n`（2075）；金手指弹窗生效时机说明（871-876）；`openCheatDialog` 注释（826-836）；扫描新增 `registerLive/scanLiveValues` 与 summary（1093-1193、1336-1341） |
| `src/android/app/java/org/ffrs/diagnostic/DataBundleInstaller.java` | marker 默认值 `auto\n`（261） |
| `src/android/app/java/org/ffrs/diagnostic/SaveCheater.java` | `put()` 偏移合法性校验（627-635）；生效时机消息（466-472） |
| `tests/device/test_depth_aspect_marker.py` | 新增：真实 marker 解析函数的宿主侧用例 |
| `tests/device/SaveCheaterRoundTrip.java` | 新增：存档解密→修改→加密→解密 往返自洽用例 |
| `tests/device/android/util/Log.java` | 新增：宿主侧 `android.util.Log` 桩（测试用） |
| `scripts/android/test_save_cheater.sh` | 新增：上面那条用例的运行脚本 |

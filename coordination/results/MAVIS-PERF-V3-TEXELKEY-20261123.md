# 帧率优化第 3 轮：texel 排除原因拆分 + 稳定 memo 键 + 引擎侧分臂累计

- **设备**：`A42BBB5109000333`（ROD2-W69 / Adreno 735 / vendorID 0x5143），无线 192.168.1.121:44689。
- **分支**：主交付分支 `task/T-mavis-perf-20261122` 仍停在已出货的第 1 版（`caa0cff` / `2d60e8b4…`）；
  本轮全部探针代码在侧分支 `task/T-mavis-perf-20261122-v3probe`（`a4d7ada` → `ddcb3b8`），**未合入主交付分支**。
- **目标**：在不影响任何特效与显示的前提下提升帧率，稳步推进到所有场景 40 Hz 以上。
- **本轮包**：`ffrs-v3-20261123.apk` SHA-256 `31dfe53f1b7f4aaac67bb9d7d8c012ad75d8d0a8692e2f47b0ec7ac44cda65e4`（已装机，仅探针用）。

> **后续更新（2026-11-23 凌晨，第 4 轮续）——先读 §10。**
> 场景终于进去了，并取到了两套独立方法一致的 V3 结论：**V3 全栈在兵工厂深处净负
> （fps -1.8~-3.5、CPU +1.6~2.4 ms/帧），按 L-029 止损，不出货第 2 版**（§10.10）。
> 过程中定位并修掉了 V3 探针自身的崩溃（`texel_key_census` 对 `end()` 迭代器解引用写入 = UB，§10.6）。
> 另外挖到一个比 descriptor 微观优化大一个量级的新杠杆：**主线程跨核迁移**导致帧率在
> 进入位置后约 20 秒内从 40+ 掉到 26~30 并保持（§10.11）。

## 0. 一句话结论

**本轮没有出货新版本，但把"descriptor 为什么重建"这个问题第一次问到底了**：
兵工厂场景每帧 445 次 descriptor 整表重建里，**421 次是被 texel 绑定挡掉的**，
而这 421 次里 **413 次是 uniform texel、只有 8 次是 storage texel**（新拆分的计数器实测）。
也就是说：**98% 的重建是 uniform texel 造成的**，而 uniform texel 恰好是**可以池化 + 可以给稳定键**的那一类。

按这个结论做的 V3（池化 + 稳定 memo 键 + 廉价刷新门）在同一进程交错采样里，
**descriptor 子域从 7.6 ms/帧 掉到 0.24 ms/帧，set-cache 未命中从 445 次/帧 掉到 3.3 次/帧**，
方向正确且量级足够大（距 40 Hz 需要的 5~6 ms/帧就在这个量级里）。
但**该窗口落在场景切换期，帧率总账被加载开销污染，场景内的干净 A/B 尚未取到**，因此按 L-029 不出货。

另外，本轮修正了两个**会导致"假结论"的工程问题**（探针字段数静默失效、日志尾部读到旧会话），
并把测量搬进引擎（分臂累计），使测量不再依赖"agent 能在前台抢到多久"。

## 1. 构建卡点（两个都是工程坑，不是算法问题）

### 1.1 `desc_memo_why_storagetexel` 未声明 → 4 个编译错误

新加的拆分量定义在 5186 行，而 `perf_frame()`（1880 行）就在它前面用。修法与既有 `desc_memo_why` 一致：
在文件顶部的 `extern` 前置声明块（952 行）里补声明，而不是把定义上移。

### 1.2 worktree 自带签名 key → `INSTALL_FAILED_UPDATE_INCOMPATIBLE`

`build_game_window_apk.sh` 用 `SIGNING_DIR="${FFRS_ROOT}/.local/signing"`，而 `FFRS_ROOT` 是当前 worktree 根，
于是**每个 worktree 各自 `keytool -genkeypair` 生成一把新 dev key**。
主树与既有 worktree 是 `D1:19:67:A3:…`，新 worktree 是 `57:6E:D3:B4:…`，自然装不上。

修法：构建前把主树那份 `gamewindow-debug.p12` 拷进新 worktree 的 `.local/signing/`，再构建。
已重签重建，装机成功。见 L-055。

## 2. 仪表修复：`gfx-cpu-profile` 的字段数

上一轮"探针装上但一个 `DESC_TIMING` 都没打印"的根因找到了：
引擎读标记用 `fscanf(m,"%llu %u %u",&s,&c,&detail)`，**需要 3 个字段**（start / count / detail）。
只写 1 个字段时 `fscanf` 返回 1，代码条件是 `>=2`，于是**整段探针被静默跳过**，不报错不打日志。

正确写法：

```bash
/tmp/adbw shell "printf '0 30 1' > $F/gfx-cpu-profile"   # start=0, count=30 帧, detail=1
```

修好后 descriptor 26 子域仪表首次打通（第 1 版包，兵工厂，30 帧窗口，coverage=OK）：
总 descriptor 12.6 ms/帧，子域 ms/帧：image_lookup **4.20**（122500 calls/30 帧 → 4083/帧，占 34%）
> set_finalize 2.03 > element_rest 1.11 > buffer_bind 1.05 > texel_view_create 0.65 > texel_pending 0.50
> texel_view_lookup 0.31 > memo_verify 0.35 > memo_key 0.39 > memo_snapshot 0.28 > setup 0.45
> texel_alias 0.13 > texel_readback 0.11 > readback_alloc 0.03 > readback_guard 0.03 > readback_copy 0.07
> image_view 0.016 > texel_cpu_access 0.005 > image_transition 0.003；未打标 remaining 0.88。

## 3. 关键发现：重建的主因是 uniform texel，不是 storage texel

热机兵工厂基线（`parts_ms_per_frame`）：

```
descriptor=9.44/1289.7   snapshot=5.94/6203   other=5.11/9.7   draw=3.36/682.7
cleanup=2.20/8.2        dispatch=0.80/165    pass=0.41/701.7  pipeline=0.33/190
present=0.44  hash=0.14/249.4  image=0.20/1797  program=0.25/583  cpuwrite=0.19/398.4
desc_ms: memo_key=0.22  tmpl_update=1.37  alloc=0.18  setkey=0.03  build=11.36  hit_total=1.40
setcache: hits=0.0  misses=443  memo_hits=404.8  memo_misses=1.0  memo_front=335.7
          why=texel421/simg14/dm6/dynbuf1   experiment_skips=293/帧  fresh_skips=150/帧
```

**每帧 443 次 set-cache 未命中 = 整表重建，其中 421 次直接被 texel 挡掉。**
本轮新加的拆分计数器给出答案：

```
NVN PERF texelkey stable=? ab=? used=? same=? changed=? storagetexel=8.0 uniformtexel=415.0
```

即 **uniform 415 + storage 8**。结论：**98% 的重建来自 uniform texel**。
而 uniform texel 正是"读多写少、范围固定、每帧内容可能变"的那一类——
它可以被池化成"每 slot 一份稳定目标缓冲"，于是 descriptor 里绑的不再是每帧变化的 guest handle，
memo 键里的 texel 分量也可以换成池内稳定 id。

**推翻了上一轮 L-052 结论的适用范围**：V2 探针只做"池化让 texel 进 memo"、没改键；
memokey 探针只改键、但其基线上 texel 仍被整体排除（`ok=false` 提前 break，根本没走到推键那行）。
两者**从未叠加测试过**，所以 memokey 才表现为"零变化"。V3 首次把两者叠加。

## 4. V3 设计

分支 `task/T-mavis-perf-20261122-v3probe`，两个 commit：

- `a4d7ada`：`texel_key_stable`（memo 键里 texel 绑定改用池内稳定 id `handle&0xfffff`）、
  `texel_pool_fast`（池化拷贝保留内容，只在 range 被失效时重刷）、
  `texel_pool_sorted`/`texel_pool_maxlen`（按 base 有序索引，`snapshot_invalidate` 精确标 dirty）、
  `desc_memo_why_storagetexel`/`desc_memo_why_uniformtexel`（拆分统计）、`texel_key_used/same/changed`（跨帧 id 稳定性普查）。
- `ddcb3b8`：给三个开关加 `ab_off` 门控（让引擎自带 `gfx-ab-toggle` 进程内交错 A/B 能切它们），
  并加**引擎侧分臂累计**（新增 `NVN AB arm=…` 汇总行）。

### 4.1 安全性：稳定键不会命中过期条目

memo verify 路径对 pooled texel 绑定**每次命中都复核**（`xbg_game.cpp` ~5540）：

```cpp
if(same)for(auto &tp:m.texel_pooled){const tex_t &tt=tp.record->texture;
    if(tp.record->revision!=tp.revision || uint64_t(uintptr_t(tt.mem))!=tp.base || uint64_t(tt.L.size)!=tp.bytes){same=false;++vwhy_tex;break;}
    if(!texel_pool_refresh(tp.base,tp.bytes,VkFormat(tp.format))){same=false;++vwhy_tex;break;}}
```

registration revision、guest 内存指针、range 大小三者任一变化即判失效；随后仍要刷新一次池化拷贝。
**稳定键只影响"能不能找到条目"，不影响"条目是否仍然有效"**。

### 4.2 L-032 红线：不弱化失效

`texel_pool_fast` 的快门条件是
`p->refreshed && !p->dirty && p->refreshed_slot==slot_serial && p->refreshed_epoch==snapshot_epoch`。
`slot_serial` **每提交递增**，所以快门只在**同一提交内**复用，
而原路径（`texel_copy_cache`）本来也是**每提交内缓存**（实测 copy=3.00/帧、hit=102~135/帧）。
⇒ 快门**没有延长任何拷贝的生命周期**，跨提交一律走真实刷新；`dirty` 标记只是额外的安全网。
guest 写 / GPU 结果落地都会走 `snapshot_invalidate`，按 base 有序索引精确标 dirty 并使池缓存失效。

## 5. 测量方法论（本轮工程量最大的一块）

用户这一轮在用平板（红果短剧 → 文件管理器），agent 每次能拿到的前台窗口只有 30~60 秒，
期间游戏被切到后台、日志停更、按键也发给了别人的 App。由此暴露并修掉了三类"会造出假结论"的问题：

| 问题 | 表现 | 修法 |
| --- | --- | --- |
| 探针字段数不对 | `fscanf` 返回 1 < 2，整段探针静默跳过 | 按引擎的格式串写 3 个字段（见 §2） |
| `engine.log` 是**累计**文件 | 场景校验读到**上一次会话**的行，误判已进场景，实际采到标题界面 | 用 `grep -c` 行数**增量**判定，或截图确认 |
| 按键发给**当前前台**的 App | 连按 A 十几次无效——按键全落在红果/文件管理器上 | 每次发键前确认前台；前台不是目标就不发 |

并把统计搬进引擎：按**生效开关组合**分臂累计**帧数**与各计数器，
每个窗口结束时累加进对应臂，最后打一行 `NVN AB arm=…`。
分臂标签用**生效值**（`enabled && !ab_off(...)`）而不是"以为设成了什么"。
这样窗口被抢只会少几帧，不会作废整个对比；且 `gfx-ab-toggle` 让两臂同温同序。

## 6. 目前的实机数据（诚实标注完成度）

### 6.1 场景内、已确认的基线（arm A，第 1 版行为，兵工厂，热机）

```
fps 38.3~43.0   submit_cpu 15.6~20.3   gpu 14.5~18.2   draws 687   passes 108
parts: desc 5.38~7.67  snap 2.34~3.93  draw 1.37~2.38  other 3.23~8.97
desc:  build 7.93~8.84  hit_total 0.47~1.07
setcache: memo_hits 342~407  setcache_miss 419~445  why_texel 374~423
          storage=8.0  uniform=413~415
```

### 6.2 V3 全量臂（pool+key+fast）——**唯一一条 in-scene 样本，落在场景切换期，不作结论**

```
draws=608（场景加载中）  fps=22.1  submit_cpu=31.8
parts: desc 0.24  other 12.36  draw 0.34  snap 0.25
desc:  build 0.57  hit_total 0.16
setcache: memo_hits 15.6  setcache_miss 3.3  why_texel 0.0
```

**可以确认的**：texel 排除归零（`why_texel 423 → 0`）、set-cache 未命中塌缩（445 → 3.3）、
descriptor 子域从 7.6 降到 0.24 ms/帧 —— **机制成立**。
**不能确认的**：帧率总账。该窗口的 `other=12.36` 与标题界面（`other` 8.5~14.4 ms/帧）
同量级，是加载期开销，**不能算进 V3 的账，也不能算作收益**。

### 6.3 池化单独一臂（arm B，pool on / key off）—— 1 个样本，与上一轮结论矛盾

```
fps 36.27  submit_cpu 18.82  desc 5.55  build 5.13
memo_hits 508.5  memo_misses 97.0  setcache_miss 344.0  why_texel 226.0
pool hits=1549.8  refresh=169.7  skips=88.1
```

比 arm A（fps 32.71 / cpu 20.31 / desc 6.54 / build 8.26 / setcache_miss 445）**看起来更好**，
但只有 1 个样本，且**与上一轮"pool 单独用净负 2.49 ms/帧"矛盾**。
两种可能：(a) 上一轮的对照口径不同；(b) 场景/热机状态不同。
**未复核前不下结论**，按 L-045（顺序测量会把漂移读成差异）与 L-029 处理。

## 7. 下一步（已就绪，只差一个设备窗口）

测量装置已经全部就位，剩下的事情只有一件：**在一个没人用平板的窗口里，跑一次 70 秒的交错采样**。

- 脚本：`/tmp/ffrs-validate/v3_final.py`（进场景 → 3 臂交错 6 秒/段 → 读 `NVN AB` 汇总 → 还回用户 App）。
- 需要的窗口：约 **70 秒**（启动到标题 30 秒 + 读档 6 秒 + 采集 35 秒）。
- 判据（已写入脚本与仪表）：
  - V3 臂 `NVN AB` 的 `desc_ms` / `build_ms` / `sc_miss` / `why_texel` vs 基线臂，
  - 以及 `cpu_ms` / `fps` 的净差；
  - `key_used / key_same / key_changed` 给出池内 id 的跨帧稳定性（上一轮没有取到，是设计前提的验证点）。
- 通过则：出第 2 个出货版本 + 显示回归（`/tmp/ffrs-validate/p0/texel_vis.py`，texel on/off 像素比对）；
  不通过则：按 L-029 止损并把结论写进 L-053+。

**合入 `work/rg477m-native` 的前提未变**：战斗特效场景复测、场景B（月夜火场）、RG477M 双机回归三项仍缺。

## 8. 附：本轮所有改动清单

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp`：
  - 新增 `texel_key_stable` / `texel_pool_fast` / `texel_pool_sorted` / `texel_pool_maxlen` /
    `texel_key_census` / `desc_memo_why_storagetexel` / `desc_memo_why_uniformtexel` /
    `texel_key_used|same|changed`；
  - `snapshot_invalidate` 按 base 有序标 dirty；`texel_pool_refresh` 加同提交内快门；
  - memo 键的 texel 分量可切换为池内稳定 id；
  - 三个开关加 `ab_off` 门控；
  - 新增 `NVN PERF texelkey` / `NVN PERF texelpool`（带生效值与 `ab=`）与 `NVN AB arm=…` 分臂汇总行；
  - live 开关新增 `texelkey` / `texelpoolfast`。
- 外部脚本（不入库）：`/tmp/ffrs-validate/v3_*.py`、`v3_final.py`、`parse_b.py`。

## 9. 进隔离存档的实测配方（连错七次才收敛）

这一节记的是"怎么把游戏开进第 4 章兵工厂"，因为它本身就是一个会造出假数据的坑。

| 尝试 | 做法 | 结果 | 判读 |
| --- | --- | --- | --- |
| 1~3 | 启动后轮询"日志尾部 draws>500"判场景 | 误判成功 | 日志尾部是**上一次会话**的行（L-054） |
| 4 | 读档中检测到前台被抢就 `am start` 拉回 | 读档被打断 | `am start` 重建 Activity = 重置读档 |
| 5 | 每 3 秒补按一次 A 直到进场景 | 在"读档→标题"之间循环 | **A 在读档界面是取消** |
| 6 | 只按一次 A，但读档中仍做 `am start` | 退回标题 | 同上 |
| 7 | 只按一次 A，之后 50 秒完全不碰进程 | **读档卡在读档界面不动** | 用户在按 A 后约 9 秒拿走了平板；**读档要前台持续在位** |

**收敛后的配方**：

```
启动（带 isolated_regression_save + isolated_regression_resume）
  → 等 30 秒让标题界面稳定
  → 按一次 A（选 Continue）
  → 不按任何键、不执行 am start，静等 30~50 秒
  → 用 grep -c 行数增量确认已进入场景（draws > 500）
  → 开始采样
```

**整段需要约 90 秒连续在前的平板**；采样本身只要 ~50 秒（引擎侧分臂累计，短窗口也成立）。

**本轮实际卡点**：用户这一轮持续在用平板（红果短剧 → 文件管理器 → 红果），
每次我拿到前台后 9~40 秒内就被拿回，读档因此无法完成。
按 L-057 的纪律，不该继续闷头自动试，**需要用户明确给出一个不被占用的窗口**。

## 10. 第 4 轮续（2026-11-23 凌晨）：场景终于进去了，基线取到了，但撞上 V3 崩溃

上一轮卡在"没有设备窗口"。这一轮平板空闲（红果的悬浮窗被隐藏），于是把整条进场景的路走通了，
取到了**迄今最干净的一条出货基线**；但同一轮也挖出两个必须先解决的问题：V3 的运行中热切臂会崩，
以及 **V3 全栈在读档加载阶段可复现崩溃**。因此**本轮仍然不出货**。

### 10.1 分屏/悬浮窗下 `keyevent` 全部落在别人身上

平板处于分屏/悬浮窗模式，红果短剧的下载浮窗盖在游戏前面：

```
mCurrentFocus=Window{... com.hongguo.pcplayer/com.hongguo.pcplayer.WebAppActivity}
mFocusedApp=ActivityRecord{... com.hongguo.pcplayer/.WebAppActivity t751}
```

`input keyevent 96` 连按十几次，游戏毫无反应——按键全被红果吃掉了（L-057 的续集）。
改用**坐标派发的触摸**点游戏自己的虚拟按键（触摸按坐标注入，不依赖窗口焦点），菜单立刻可操作：

| 虚拟键 | 坐标（3000×1920 实屏） |
| --- | --- |
| ▼ | (365, 833) |
| ▲ | (365, 578) |
| A | (2550, 1632) |

### 10.2 进第 4 章兵工厂的完整配方（Load Game 路线）

隔离存档 `shield-regression-20261122073707`（第 4 章，GameSaveData0/1 各 ~474KB，mtime 2026-11-18 09:28，完好）：

```
am force-stop org.ffrs.gamewindow
am start -W -n org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity \
  --ez isolated_regression_save true --es isolated_regression_resume shield-regression-20261122073707
  → 等 ~35 s，标题界面稳定（draws=39）
  → ▼ (365,833)    光标：Continue → Load Game
  → A  (2550,1632) 进 LOAD DATA（光标在 01，draws=82）
  → ▲ (365,578)    光标：01 → Auto Save
  → A  (2550,1632) 弹"是否读取?"（光标默认在"否"）
  → ▲ (365,578)    光标：否 → 是
  → A  (2550,1632) 确认 → 约 6 s 进场景（入口 draws=140）
```

**与上一轮（L-058）的配方不同**：上一轮是"标题只按一次 A 然后不碰"（Continue 路线）；
这一轮 Continue 路线**必崩**，必须走 Load Game 路线，且每一步都要等界面真的换掉再按下一步。
原因是 Continue 走的是另一条自动续档读路径，与手动选档不是同一条代码。

### 10.3 兵工厂有两个负载档，测量点选错就白测

| 位置 | draws | fps | uniform texel 排除/帧 | set-cache 未命中/帧 | 可否比 V3 |
| --- | --- | --- | --- | --- | --- |
| 入口暗区（读档落点） | 140 | 60 | **0** | 22 | **不可**（texel 路径根本不热） |
| 兵器工厂深处（往前走 4 步） | ~594 | 27~39 | **364** | 408 | 可 |

摇杆可行走：`input swipe 375 1485 375 950 2500`（摇杆中心 375,1485，向上推 = 前进），连点 4 步即到深处。

场景判据要分两段：进场景看 `draws>100`（**不是 >300**，入口才 140），走位后再看 `draws>450`。

### 10.4 arm 语义与"启动时定臂"

引擎侧标签 `arm = (pool?4) + (key?2) + (fast?1)`：

- **arm0 = 出货基线**：三个开关全关 ⇒ uniform texel 因 `!texel_pooled && !texel_memo_enabled`
  被整体排除出 memo ⇒ 与已出货版 `caa0cff` 行为一致（出货版无池化，`texel_memo_enabled` 默认 false）。
- **arm7 = V3 全栈**（池化 + 稳定键 + 同提交快门）；**arm4** = 只开池化（上一轮 V2 形态）。

`texel_ab[8]` 是**进程内**累计，进程重启即清零 ⇒ 启动时定臂的 A/B **每臂必须单独一个进程**。

### 10.5 热切臂崩溃：运行中关 texel 池 ⇒ 700 帧后 SIGSEGV

第一版 A/B 想在进程内用 `gfx-live-set` 交错切臂。02:36:21 写 `texelpool=0`，引擎在 present 22380 应用了
（`LIVE-SET texelpool=0 at present 22380`），**约 700 帧（30 秒）后 02:36:51 进程 native 崩溃**
（`Dying app ... isFromBinderDied:true`）。

之后那 3 分钟的"交错 A/B"其实一直在读**死进程**的旧日志——arm7 的计数一行没涨才发现。
⇒ **当时判定"V3 上不能运行中热切臂"**（这是探针自身的缺陷，出货版没有池化所以不受影响），
A/B 改成**启动时定臂**：每个臂冷启一次进程。

> **事后更正（§10.6 之后）**：这次崩溃的**根因不是"运行中关池子"，而是 §10.6 的 census UB**——
> arm7 从启动起 census 就在写坏堆，崩溃只是延后爆在了一个无关的写入上。
> 修掉 UB 之后，**同一进程内每 6 秒热切一次臂（arm7↔arm0）连续跑满 14 个块、进程全程存活**（§10.10 方法二）。
> 所以热切臂不是被禁掉了，而是**先得把 UB 修掉**；修复后热切反而是本轮最干净的测量方法。

顺带一条纪律：热切必须**每臂验证引擎真的应用了开关**（看 `LIVE-SET ... at present N`），
不能假设"我写了文件 = 引擎改了状态"。

### 10.6 V3 全栈在读档加载阶段可复现崩溃 → 根因已定位并修复

arm7（pool+key+fast 全开）走 §10.2 的 Load Game 路线，进"是否读取?"确认后约 3 秒进程崩溃。三次复现：

| 时刻 | 臂 | 路线 |
| --- | --- | --- |
| 02:25:53 | arm7（启动时默认） | Continue |
| 02:41~42（r1a） | arm7 | Load Game |
| 02:44~45（r2a） | arm7 | Load Game |

**分臂二分**（每个臂冷启一次进程，走同一条 Load Game 路线）把锅锁死在一个开关上：

| 臂 | pool | key | fast | 结果 |
| --- | --- | --- | --- | --- |
| arm0 | 关 | 关 | 关 | ✔ 进场景，深处 draws=597.4 fps=45.46 |
| arm4 | 开 | 关 | 关 | ✔ 进场景，深处 draws=596.1 fps=40.21 |
| arm5 | 开 | 关 | 开 | ✔ 进场景，深处 draws=597.7 fps=44.19 |
| **arm6** | 开 | **开** | 关 | ✘ DEAD_IN_LOAD t=3s |
| **arm7** | 开 | **开** | 开 | ✘ DEAD_IN_LOAD t=3s |

**⇒ 崩溃只由 `texelkey=1` 触发。** 代码复核随即命中一处未定义行为——V3 新增的
诊断计数器 `texel_key_census`（只在 `texel_key_stable` 下调用）：

```cpp
auto it=texel_key_sites.find(site);
if(it!=texel_key_sites.end()&&it->second.frame+1==fr){...}
it->second.id=id;it->second.frame=fr;   // ← site 不存在时 it == end()
```

最后两行**无条件执行**。站点第一次出现时 `find()` 返回 `end()`，对 `end()` 迭代器解引用写入
是未定义行为——写坏的是哈希表自己的内部结构，于是几秒后以 SIGSEGV 收场。
这与"只有 arm6/arm7 崩、而且都在场景开始有 texel 绑定之后崩"完全吻合。

**修复**（commit `f61515f`）：改用 `operator[]` 插入站点，`frame=0` 表示"没见过"不参与相邻帧比较。
这是**纯诊断计数器**的缺陷，不改变任何渲染行为；修复包 `ffrs-v3-fix-20261123.apk`
（sha256 `6328a69968c8dece8cd6838ab2b8f48569385e78f67cad8198265fe711ca2822`）已装机。

### 10.7 取证受限：设备不落 tombstone

`/data/tombstones` 最后一条是 11-20，本轮三次 native 崩溃**都没有新 tombstone**，`logcat -b crash` 也是空的；
logcat 只剩 `Dying app ... isFromBinderDied:true`，**没有 signal 行、没有 backtrace**。

叠了一层干扰：引擎自己装了 SIGSEGV handler（`xbg_game.cpp:2154~2174`，guest 内存脏页跟踪），
它会吞掉落在受跟踪页上的段错误并继续跑；只有落在跟踪页之外的才转发到默认处理 → 进程死。

⇒ 拿栈只能靠主动手段（挂 lldb / 加自己的 handler / 缩到最小复现），不能指望系统日志。

### 10.8 本轮取到的干净基线（arm0 = 出货基线，兵器工厂深处）

720 帧 / 26 秒 / present 22380~23080，采样期逐窗口 PERF 交叉验证（fps 25.2~29.1、draws 601.7~602.2）：

```
NVN AB arm=0 pool=0 key=0 fast=0 frames=720 fps=27.47 cpu_ms=30.19 gpu_ms=16.96
              desc_ms=0.32 build_ms=11.08 hit_ms=1.30
              memo_hits=354.3 memo_misses=8.1 sc_miss=408.6
              why_texel=379.5 store=8.0 unif=364.2
```

**读法**：
- **descriptor build 11.08 ms/帧 = CPU 的 36.7%**，是单一最大开销；
- set-cache 未命中 **408.6/帧**，其中 uniform texel 排除 **364.2/帧**（89%）；
- CPU 30.19 ms/帧、GPU 16.96 ms/帧 ⇒ **纯 CPU 瓶颈**；
- 距 40 Hz（25 ms/帧）差 **5~6 ms/帧**，而 V3 在场景切换样本上正好把 descriptor build
  从 7.6 压到 0.57 ms/帧（**-7 ms/帧**）——**量级刚好覆盖缺口**。

**也就是说：V3 是目前唯一有可能把兵工厂推过 40 Hz 的杠杆，因此它的崩溃必须查清。**

**还缺的一条**：arm7 在**兵工厂深处**的同样口径样本（arm7 现有数据只到标题/加载期与场景切换期）。

### 10.9 本轮的下一步（已执行完，结论见 §10.10 / §10.12）

1. **拿到 arm7 的崩溃栈** → 分臂二分 + 代码复核锁定 `texel_key_census` 的 UB，已修（`f61515f`）。
2. **按"启动时定臂"重跑 A/B** → arm0 / arm7 各 2 轮冷启（ABBA 顺序）已完成。
3. **判定出货** → arm7 未达 ≥35 fps（反而更慢），按 L-029 止损，**不出货**。
4. 合入 `work/rg477m-native` 的三个前提仍未变：战斗特效场景复测、场景B（月夜火场）、RG477M 双机回归。

### 10.10 V3 的最终实机结论：**净负，不出货**（两套独立方法一致）

崩溃修掉（`f61515f`）后，用两套**互相独立**的方法测了同一位置（兵器工厂深处，draws 恒为 601.9）的 V3 全栈（arm7）
与出货基线（arm0）。

**方法一：进程间定臂，ABBA 顺序（arm7 / arm0 / arm0 / arm7，每臂独立进程，取采样前 9 个稳定窗口）**

| 指标 | arm7（V3 全栈） | arm0（出货基线） | Δ |
| --- | --- | --- | --- |
| fps | 40.15 | 43.64 | **-3.49** |
| submit_cpu_ms/帧 | 18.14 | 15.75 | **+2.39** |
| gpu_ms/帧 | 15.95 | 15.66 | +0.29 |
| descriptor build | 5.00 | 6.12 | -1.12 |
| memo_key | 0.64 | 0.11 | +0.53 |
| hit 路径 | 1.62 | 0.70 | +0.92 |
| set-cache 未命中/帧 | 336.9 | 415.0 | -78.1 |
| uniform texel 排除/帧 | 131.0 | 385.0 | **-254.0** |
| memo_misses/帧 | 80.3 | 0.97 | **+79.3** |

**方法二：进程内时间交错（每 6 秒切一次臂，同进程、同温、同位置，14 个块全程存活）**

| 指标 | arm7 | arm0 | Δ |
| --- | --- | --- | --- |
| fps（交错期 18 / 23 窗口） | 28.58 | 30.37 | **-1.79** |
| submit_cpu_ms/帧 | 29.24 | 27.60 | **+1.64** |
| gpu_ms/帧 | 17.35 | 17.05 | +0.30 |
| descriptor views/帧 | 26274 | 29788 | -3514 |

**⇒ 两法同号同量级：V3 在兵工厂深处净负，按 L-029 止损，不出货第 2 版。**

**机理（机制有效，为什么总账是负的）**
1. V3 确实做到了它设计要做的事：uniform texel 排除 **385 → 131/帧**、set-cache 未命中 **415 → 337/帧**、
   descriptor build **6.12 → 5.00 ms/帧**（省 1.12），descriptor views/帧少建 3514 个。
2. 但省下的被三处吃掉：
   - **memo_misses 1.0 → 80.3/帧**。让 texel 绑定进 memo 只是给它"进池的资格"；
     键里**其它分量（UBO 地址等）每帧仍变** ⇒ 命中不了 ⇒ 只是从"被整体排除"变成"键不匹配后重建"。
   - **hit 路径 0.70 → 1.62 ms/帧**。命中要复核 pooled 条目、刷新池拷贝。
   - **memo_key 0.11 → 0.64 ms/帧**，其中大部分是本轮新增的**诊断 census** 的哈希查找（出货版不会有）。
3. 净结果：**CPU +1.6~2.4 ms/帧，fps -1.8~-3.5**。

**对上一轮结论的两条修正**

1. **"机制已确认"只在场景切换期成立。** 上一轮那条（texel 排除 423→0、set-cache 未命中 445→3.3、
   descriptor build 7.6→0.24 ms/帧）样本落在**场景切换窗口**，那时各 draw 的键恰好相同。
   兵工厂深处稳态下，收益只有它的三分之一左右。**过渡期样本不能代表稳态。**
2. **"热机 24.6~33.5 / 冷机 36.8~43"这组数，一半由设备状态解释。**
   本轮实测：进入位置后**约 20 秒内帧率从 40+ 掉到 26~30 并保持**，
   **两臂在同一个窗口序号同步发生，与臂无关**——是设备级事件，不是代码差异。

### 10.11 新杠杆：主线程跨核迁移（比 descriptor 微观优化大一个量级）

掉速时**所有子域同比例放大约 1.75×**：

| 子域 | 快时 | 慢时 | 比值 |
| --- | --- | --- | --- |
| descriptor | 5.21 ms | 9.13 ms | 1.75 |
| draw | 1.61 ms | 3.17 ms | 1.97 |
| dispatch | 1.09 ms | 2.15 ms | 1.97 |
| other（客体逻辑） | 3.88 ms | 5.12 ms | 1.32 |

**所有路径同比例变慢，是"CPU 整体变慢"的特征，不是某条代码路径变慢。** 佐证：

- `/proc/<pid>/task/<tid>/stat` 实测，游戏主线程 `ffrs.gamewindow` **在 cpu3~6 之间来回迁移**；
- 各核频率在跳：cpu0 ~1.0~1.7 GHz、cpu3 2.7 GHz、cpu7 0.63~3.0 GHz；
- `dumpsys thermalservice` 的 Thermal Status = 0（电池 21 °C），**不是热降频，是调度**。

代码侧现状：`xbg_game.cpp:163` 只有 `setpriority(PRIO_PROCESS,0,-10)`（提优先级），
**没有任何绑核**。

⇒ **下一步的杠杆应该换成调度，而不是继续抠 descriptor**：
把游戏主线程/渲染线程钉在大核（或向系统请求性能模式）。
这一步**不改变任何渲染语义**，不影响特效与显示，且按上面的 1.75× 量级，
收益上限远大于本轮所有 descriptor 优化的总和。

### 10.12 下一步（按优先级重排）

1. **调度层**：给渲染线程加绑核（大核）或性能模式请求，实测帧率是否稳在 40 Hz。
   这是目前唯一能看到"持续 40 Hz"希望的杠杆。
2. **V3 收尾**：探针保留在 `task/T-mavis-perf-20261122-v3probe`（含崩溃修复 `f61515f`），
   **不合入** `work/rg477m-native`；若以后重启 texel 方向，必须先解决
   "memo 键里其它分量每帧变"这一根因（否则池化收益永远被 miss 吃掉）。
3. **合入前提不变**：战斗特效场景复测、场景B（月夜火场）、RG477M 双机回归。
4. **测量方法固定为"进程内时间交错"**：进程间对比会被设备调度状态污染（±3.5 fps），
   交错测量把两臂放在同一进程同一时刻，可重复。
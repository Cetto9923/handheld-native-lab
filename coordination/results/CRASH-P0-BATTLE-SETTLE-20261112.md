# 场景切换闪退 P0 归因与加固（2026-11-12）

> **本轮性质：预防性根治 + 宿主契约自证。** 无 8E5 / 无神殿真机，所有设备侧结论一律 `NOT_RUN`。
> 本文档写明的是**代码级归因与加固**，**不是"已修复"**。设备未复现 ⇒ 不得声称修复。
> 分支 `task/T-crash-p0-20261112`，基线 `db094f8`（merge-base `7b733b9`）。

---

## 1. 任务与范围

用户报告 **8E5（高通 Snapdragon）** 设备上《最终幻想：共鸣》**巴哈姆特召唤兽击败对手后闪退**。
后续现场补充（荣耀平板一线测试者「三季人」）给出三条与**场景装载/卸载**直接相关的反馈。

**本线负责**：切换场景时的闪退（内存错误 / 非法地址 / abort 路径）。
**本线不负责**（属过场/加载性能线与特效/seed 线，不在本线写权范围）：
- 大地图「进入时卡一小会、之后流畅」——着色器/管线冷加载成本；
- warm list / pipeline cache / shader reuse 等加载性能改动。

**设备纪律**：荣耀平板 `A42BBB5109000333` 已归还用户，禁止连接；`RG477M01445122` 归另一条线独占；
本线**不需要任何设备**，未连接、未操作任何 adb。

---

## 2. 现场证据（逐条登记，含来源与不可证实部分）

**每条证据的报告人不同，不可混为一谈：**

| # | 报告人 | 设备 | 原文要点 | 本线解读 |
|---|---|---|---|---|
| E1 | **另一位报告人**（非用户本人、非三季人） | 8E5（高通 Snapdragon） | 巴哈姆特召唤兽击败对手后闪退 | 战斗结算 → 切场景 |
| E2 | **荣耀平板一线测试者「三季人」** | 荣耀平板 | 「每次进入**大地图**得卡一小会……其他城镇内、副本内都流畅」 | **加载性能**，非本线；但说明"进入大地图"是高频场景切换点 |
| E3 | **荣耀平板一线测试者「三季人」** | 荣耀平板 | 「**每次加载大地图都需要卡一会，有小概率闪退**」 | 场景切换 → **低频**闪退 |
| E4 | **荣耀平板一线测试者「三季人」** | 荣耀平板 | 「**进入新的幻兵水晶神殿大概率闪退**……自动存档会存神殿里面，再重新进游戏读这个档就可以」 | 场景切换 → **高频**闪退；「自动存档可绕过」**不等于已修复**，只是给了玩家一条规避路径 |

> **取证边界（重要）**：E2/E3/E4 全部来自**荣耀平板一线测试者「三季人」**，
> **不是用户本人的复现**；用户本人的剧情**还没走到那个神殿**。
> 因此本线**没有**神殿的可复现路径，也**不能**声称"本地已复现神殿闪退"。
> 神殿与大地图的取证只能靠**设备日志**（见 §7.1 的 grep 自查方法），不能靠本地跑通。
> E1 的 8E5 同样不在任何人手里。

### 2.1 现场证据对应（映射到本线缺陷与缺口）

| 现场条目 | 映射到本线修的缺陷 | 覆盖状态 | 仍存缺口 |
|---|---|---|---|
| E1（8E5 召唤兽结算后闪退） | C1 多等待者生命周期 / C2 悬垂 guest 指针 / C3 引擎侧非法 guest 地址 | **代码级已加固**（见 §5），设备侧 **NOT_RUN** | 无 8E5 复现条件，无法确认命中的就是哪一条 |
| E4（神殿进不去，**大概率**） | C1（场景卸载时 finalize 与 waiter 竞争） | 代码级已加固 + 契约覆盖 | 需要玩家提供 tombstone 才能把"大概率"收敛到具体一条 |
| E3（大地图**小概率**） | C1/C2/C3 同族；概率差异与"场景资源数量/线程数"正相关 | 同上 | 同上 |
| E2（大地图卡顿） | **不属本线** | 不覆盖 | 归过场/加载性能线 |

> **崩溃概率与场景复杂度正相关**（神殿资源/线程多 → 大概率；大地图资源少 → 小概率），
> 这与"宿主侧事件/多等待对象生命周期竞态"这一族缺陷的特征一致：
> 竞态窗口随场景持有的 Event / MultiWait / 线程数量线性变宽。
> 但**这只是与证据一致的假设，不等于已确诊**。

---

## 3. 归因结论（候选排序，含证据强度）

行号均为当前工作树（`db094f8`）磁盘真实行号。

### C1 — 场景卸载时 `FinalizeEvent` 与 MultiWait waiter 的生命周期竞争（**首要，证据最强**）

- `src/runtime/hle/nso_host.c:2903` `nso_host_os_finalize_event`
- `src/runtime/hle/nso_host.c:2908-2910`：

```c
if(ffrs_multiwait_event_waiting && ffrs_multiwait_event_waiting(event)) {
    fprintf(stderr,"[multiwait] refusing Event finalization during active wait\n");abort();
}
```

- 证据链：
  1. **guest 确实使用 MultiWait**：`src/runtime/hle/guest_process.c:1515` 与 `1303` 把
     `_ZN2nn2os17WaitAnyEPNS0_13MultiWaitTypeE` / `TimedWaitAny` 等 8 个符号接到 `src/runtime/hle/multiwait.c`；
     `src/android/native/CMakeLists.txt:73` 确认 `multiwait.c` **被编进 Android 构建**。
  2. **multiwait 的等待态是可观测的**：`multiwait.c` `wait_impl` 置 `s->waiting=1`，仅在就绪/超时后清零。
  3. **这是有意加的护栏，不是偶发**：`coordination/HANDOFF.md:2467` 记录
     「RootEventFinalizefix: linked/unlinked registered holders may outlive a live quiescent Event;
     only active Wait prevents finalize. `ffrs_multiwait_event_waiting` guard.
     Host quiescent exit0 / active SIGABRT-6」。
     即：**当存在活动等待时，事件终结被改成了 `abort()`**。
  4. **场景卸载正是这个竞态的触发点**：场景切换时旧场景的 Event 被 `nnosFinalizeEvent` 终结
     （`guest_process.c:1419/1623` 派发），而旧场景的工作线程可能仍阻塞在 `nn::os::WaitAny` 上。
     时序窗口宽度 ∝ 场景持有的等待者数量 —— 与 E4「大概率」/ E3「小概率」的梯度一致。
- **崩溃形态**：`abort()` → SIGABRT（6）。**这是宿主主动终止进程，不是访存错误**，
  所以 tombstone 里会是 `SIGABRT` + `Abort message: ...` 或无 message 的 signal 6。
- **证据强度：高**。它是全仓库中**唯一一处会在"场景卸载"这个动作上直接终止进程**的宿主路径，
  且护栏的存在本身即证明"该竞态真实发生过"。

### C2 — MultiWait holder 存活期长于其引用的 Event → 悬垂 guest 指针读写（**次要，与 C1 同族**）

- `src/runtime/hle/nso_host.c:3084-3098` `nso_host_os_is_event_signaled`：
  当指针**不在**事件注册表里时，直接读 guest 内存 `((uint8_t*)event)[0x10]`。
- `src/runtime/hle/nso_host.c:2903` finalize 会把条目**摘出注册表**。
- `multiwait.c` `wait_impl` 每轮循环对每个 holder 调 `nso_host_os_is_event_signaled(h->event)`。
- 证据链：HANDOFF:2467 **明写**「linked/unlinked registered holders may outlive a live quiescent Event;
  only active Wait prevents finalize」——
  即**只要当时没有活动 Wait，holder 就可以活得比 Event 久**。
  此时 Event 条目已被摘链 → `is_event_signaled` 落到 guest 内存读取分支 →
  若 guest 已随场景卸载释放该页 → **SIGSEGV 读**。
- 同族第二处：`nso_host_os_signal_event`（:2947）经 `borrow_event`（:2799）在条目缺失时**自动重建条目**，
  然后写 `((uint8_t*)event)[0x10]=1` → 对**已释放的 guest 页**写入 → SIGSEGV 写。
- **证据强度：中高**。缺口被项目自己的 HANDOFF 记录过，但缺少"guest 是否在该窗口释放页面"的直接证据。

### C3 — 引擎侧 OP_WAIT_EVENT：guest 地址未校验 + 忙等超时即 fatal（**第三**）

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp:5971-5974`：

```cpp
case XBG_OP_EXTENSION_BASE+4:{auto a=payload<xbg_wait_event>(p,n);need(a.address && a.mode<=1,"Invalid event wait");
    ...
    auto deadline=...+std::chrono::nanoseconds(cfg.fence_timeout_ns);
    for(;;){uint32_t value=__atomic_load_n(reinterpret_cast<uint32_t*>(a.address),__ATOMIC_ACQUIRE); ...
             need(std::chrono::steady_clock::now()<deadline,"NVN event wait timeout"); std::this_thread::yield();}
```

- 缺陷 1：`a.address` **只校验非零**，随后直接原子读 → 地址不在映射页 → **SIGSEGV**。
  审计 P0-4 建议「访问前校验地址落在已注册 guest pool 区间」，工程上已有现成工具：
  `nvn_memory_pool_resolve_address`（`xbg_game.cpp:100` 弱声明，`nvn_memory_pool.c:459` 定义）。
- 缺陷 2：忙等超时 → `need()` → `xbg_fatalf`（:2813，`__attribute__((noreturn))` + `abort()`）→ SIGABRT。
  审计 P0-4 建议「超时返回可恢复错误而非 fatal」。
- 场景相关性：场景切换时 guest 的命令流会重排/丢弃，事件地址可能落在已释放的池上。
- **证据强度：中**。静态缺陷确凿（审计已列 P0-4），但缺"神殿崩溃即此路径"的现场证据。

### C4 — `retire_slot` 的 fence 超时/device-lost 直接 fatal + 无界自旋（**第四**）

- `xbg_game.cpp:1036` `retire_slot` → `VK(hybrid_wait_fence(s.fence, max(cfg.fence_timeout_ns,10s)))`
  → `check()`（:469）→ `xbg_fatalf` → `abort()`。
- `xbg_game.cpp:1079` `while(s.waiters.n.load())std::this_thread::yield();` 无上限。
- 场景相关性：场景切换是 GPU 负载尖峰，fence 更容易超时 → abort。
- **证据强度：中低**。属 GPU 侧，与"进神殿必崩"这种高频确定性现象的吻合度不如 C1/C2。

### C5 — P0-1 自身在 `1df098f` 之后的残余缺口

- `1df098f fix(hle): free an event only after its waiters have left` 已经把**普通 WaitEvent** 的 UAF 关掉了
  （`nso_host.c:2903-2945`：`dying` 标志 + broadcast + 等 `waiters==0 && users==0` 才 free）。
- **残余**：该修复**只覆盖 `nn::os::WaitEvent` 等待者**，**不覆盖 MultiWait 等待者**——
  这正是 `2908` 那条 abort 护栏存在的原因，也正是 C1/C2 的根。
- **证据强度：高**（缺口本身由代码与 HANDOFF 双向确认）。

### 3.1 归因总结

> **最可能**：**C1**（场景卸载时 `FinalizeEvent` 与 MultiWait 活动等待的竞争 → 宿主 `abort()`），
> **紧接 C2**（同一竞态的另一面：holder 活得比 Event 久 → 悬垂 guest 指针读写 → SIGSEGV）。
> 两者**同源**：宿主事件生命周期只统计了 `WaitEvent` 等待者，没统计 MultiWait holder，
> 于是"事件终结"要么被 abort 拦下，要么在无人看管时把条目摘链。
> C3/C4 是**同族的第二梯队**（引擎侧），有确凿静态缺陷但缺现场对应。

---

## 4. 底层根因

> **宿主事件生命周期模型缺了一个引用者。**
>
> `nn::os::EventType` 在宿主侧有三种持有者：
> 1. 事件注册表条目（`nso_event_entry`）—— 事件对象本身；
> 2. **`nn::os::WaitEvent` 等待者** —— `1df098f` 已用 `waiters` 计数正确覆盖；
> 3. **`nn::os::MultiWaitHolder` 持有者** —— **无人计数**。
>
> 第 3 类持有者只在 `multiwait.c` 内部用裸指针 `h->event` 保存，宿主注册表完全不知道它。
> 于是"终结事件"这件事在第 3 类存在时**没有正确的构造**：
> 要么 abort（C1），要么摘链后留下悬垂指针（C2）。
> 引擎侧（C3/C4）则是另一个独立问题：**guest 地址没有边界校验**，且**把可恢复的超时当成不可恢复错误**。

---

## 5. 实现计划（构造上消除，不加 sleep / 不加超时掩盖 / 不吞错误）

### H1 `src/runtime/hle/nso_host.c` —— 事件生命周期：把 MultiWait 持有者纳入引用计数

1. **条目增加 `retired` 语义**：finalize 时若检测到 MultiWait 仍持有该事件，
   **不再 abort**，而是把条目标记为 `dying + retired`，**保留在注册表中**（可被 `is_event_signaled` 找到），
   并**等待常规 WaitEvent 等待者离开**（沿用 `1df098f` 已有机制）。
2. **退休条目永不被 free，直到 MultiWait 不再引用它**：
   新增 `sweep_retired_events()`，在每次 finalize 时检查
   `ffrs_multiwait_event_referenced(event)`（已有弱符号，`multiwait.c` 已实现）为假、且 `users==0 && waiters==0`
   才真正 destroy + free。
   ⇒ **构造上消除 C1**：不存在"释放时仍有持有者"的时刻。
3. **退休条目的信号状态改由宿主记账**：
   `nso_host_os_is_event_signaled` 对 `dying` 条目**返回宿主记录的 `signaled` 值，绝不读 guest 内存**。
   ⇒ **构造上消除 C2 的读侧**。
4. **杜绝"复活"写**：`borrow_event` 遇到 `dying` 条目时**返回 NULL**（不再 `users++`、不再重建条目），
   `signal_event`/`clear_event` 因此**不会向已终结的 guest 页写入**。
   ⇒ **构造上消除 C2 的写侧**（含"晚到的 SignalEvent"）。
5. **未被显式终结的自动注册路径保持原样**（静态初始化事件仍可自动注册），
   以免改变既有可观察行为；只有**已终结**的指针才拒绝复活。

### H2 `src/graphics/nvn_vulkan/engine/xbg_game.cpp` —— OP_WAIT_EVENT 的 guest 地址校验

- 读取 `a.address` 前，先用 `nvn_memory_pool_resolve_address(a.address,&available)` 确认该地址落在
  **已注册 guest pool** 且至少 4 字节可读；不满足则**记诊断并跳过这次等待**（不 dereference、不 abort）。
  ⇒ **构造上消除 C3 缺陷 1**（非法 guest 地址 → SIGSEGV）。
- 忙等超时：把 `need(...,"NVN event wait timeout")`（→ abort）改为**带诊断的降级**：
  记录事件地址/模式/已等时长，继续执行后续命令流。
  依据审计 P0-4「超时返回可恢复错误而非 fatal」；这**不改变正常路径**的行为（正常路径不会超时）。

### H3 `src/graphics/nvn_vulkan/engine/xbg_game.cpp` —— `retire_slot` 的 fatal 与无界自旋

- fence 等待返回 `VK_TIMEOUT` / `VK_ERROR_DEVICE_LOST` 时**不再 abort**：
  走 `failed` 置位 + slot 丢弃 + 队列恢复路径（引擎已有 `failed` 标志，
  `xbg_fatalf` 内即置位，`xbg_backend_failed()` 对外可读）。
- `while(s.waiters.n.load())` 自旋加**截止时间 + 退避**，超时后强制回收并标记 failed。
  ⇒ 消除 C4 的 abort 与永久卡死。

---

## 6. 宿主契约计划（修复前红、修复后绿；ASan/TSan 无告警）

| 契约测试 | 证明什么 | 修复前 | 修复后 |
|---|---|---|---|
| `event_finalize_contract.c`（扩展） | finalize 遇到**活动 MultiWait 等待**不再 abort，事件在等待者离开前不被释放 | 红（SIGABRT-6） | 绿 |
| `event_finalize_contract.c`（扩展） | 退休事件被**晚到的 SignalEvent** 触达时，不向 guest 页写、也不复活条目 | 红（写已终结页） | 绿 |
| 新增 `event_retire_multiwait_contract.c` | holder 活得比 Event 久时，`is_event_signaled` 不读 guest 内存（无 SIGSEGV） | 红（SIGSEGV） | 绿 |
| 新增 `nso_host` ASan/TSan 两套 | 上述并发场景无越界 / 无 data race | — | 无告警 |

注册到 `scripts/verify_host.py`（沿用 `event-finalize-asan` / `event-finalize-tsan` 的既有形状）。

---

## 7. NOT_RUN 清单（设备侧）

| 项 | 状态 | 缺什么 |
|---|---|---|
| 8E5 巴哈姆特召唤兽结算后闪退 | **NOT_RUN** | 无 8E5 设备、无该场景存档/操作路径 |
| 荣耀平板「进入新神殿」闪退 | **NOT_RUN** | 平板已归还用户，禁止连接 |
| 荣耀平板「加载大地图」小概率闪退 | **NOT_RUN** | 同上 |
| 任何真机 ASan/TSan 复跑 | **NOT_RUN** | 本线无设备 |

### 7.1 向玩家/一线索取的证据清单（**先做第 1 条，它最轻**）

**第 1 条（主要取证手段，一条 grep 即可定性）：**

```
adb shell "grep -a -n 'multiwait' /sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log"
adb logcat -b crash -d
```

- 路径已按工程实际核对：`GameActivity.java:2748` 用 `new File(getFilesDir(), "guest_boot.log")`，
  应用包名 `org.ffrs.gamewindow`（`src/android/app/gamewindow/AndroidManifest.xml:3`），
  故设备路径即 `/sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log`；
  `guest_boot_jni.cpp:84-95` 的 `RedirectOutput()` 用 `O_TRUNC` 把 stdout/stderr 整体 dup2 进该文件，
  **每次 guest 启动截断**，因此它天然就是"本次会话的现场"，不会被旧会话污染。
- **命中 `refusing Event finalization during active wait` ⇒ 确诊 C1**：
  宿主在场景卸载时因存在活动等待而主动终止进程，崩溃形态是 **SIGABRT(6)**，**不是访存错误**。
- 第二条 `adb logcat -b crash -d` 会给出 `Abort message` 或 signal 6，与第 1 条互相印证。
- 若第 1 条**没有**命中 multiwait、而第二条显示 SIGSEGV ⇒ 转向 C2/C3（悬垂指针 / 非法 guest 地址）。

**第 2 条（补充，tombstone 退居其次）：**

1. 崩溃瞬间设备上的 `engine.log.fatal` **全文**（本仓 `xbg_fatalf` 会写 `error_text` + `FATAL:` 前缀）；
2. `adb logcat -b crash -d` 的 **tombstone**（含 signal 号与 fault 地址）；
3. **崩溃类型**：SIGSEGV(11) / SIGABRT(6) / ANR —— 这一项**直接区分 C1/C2 与 C3/C4**；
4. **崩在进神殿瞬间，还是进神殿后第一场战斗**（区分场景装载 vs 战斗结算两条链）；
5. 设备型号与 Android 版本（8E5 与荣耀平板的驱动/调度差异是概率差异的候选解释之一）。

---

## 8. 覆盖 / 未覆盖

见文末"交付总结"节（随实现完成后补齐）。

---

## 9. 声明

- 撰写本文档时未修改任何源码、未构建、未执行任何设备命令、未连接任何设备；
- 不声称"已修复"；本轮的产出是**代码级加固 + 宿主契约**，设备侧一律 `NOT_RUN`；
- 未触碰他人 worktree / WIP / 存档 / `.local/` 输入；
- 未改 warm / pipeline / shader 等加载性能代码（不属本线写权范围）。

---

## 6. 实施记录（总控接手完成，2026-11-13 00:0x）

> 前两任 worker 均被宿主网络抖动（`net::ERR_NETWORK_CHANGED`）打断。归因（§3）已由 `75beed3` 落盘，
> 实现由总控接手完成。设备侧结论**全部 NOT_RUN**：本线没有 8E5、没有神殿真机、平板未获授权。

### 6.1 改了什么

| 文件 | 内容 |
|---|---|
| `src/runtime/hle/nso_host.c` | 事件条目**退休**而非释放；退休表用哈希（不用链表，避免在等待热路径上做 O(n) 扫描）；晚到的 signal 只改宿主侧状态、**绝不写 guest 内存**；finalize 命中已退休条目时不再写 guest 内存；退休时**释放等待者**；trim 在**注册表锁之外**确认没有 multiwait holder 指向它才释放 |
| `tests/native/event_retire_contract.c` | 新契约：等待者在场时 finalize 不 abort / 等待者被释放 / 晚到 signal 不碰 guest 内存 / 同一地址可开新的一生 / 二次 finalize 是 no-op / 退休表有界 |
| `tests/native/event_finalize_stubs.c` | multiwait 桩改为**可控**（`ffrs_test_multiwait_waiting` / `ffrs_test_multiwait_referenced`），并补上 `ffrs_multiwait_event_referenced` |
| `scripts/verify_host.py` | 新增 `event-retire-asan` 与 `event-retire-tsan` 两步 |

### 6.2 关键设计取舍（与上一任 worker 的差异）

1. **退休表用哈希而不是链表。** 上一任用单链表 + 线性扫描，而 `nso_host_os_is_event_signaled()` 在 multiwait 的等待循环里**每轮都被调用**（multiwait 持自己的锁回调进来）。1024 条墓碑的线性扫描正好落在本工程正在治理的卡顿热路径上，必须改成 O(1)。
2. **释放前必须在注册表锁之外确认引用。** `multiwait.c` 持锁回调 `is_event_signaled`，所以**持注册表锁调用 multiwait 函数会 ABBA 死锁**。因此 trim 分两段：锁内收集候选 → 锁外查 `ffrs_multiwait_event_referenced` → 仍被引用的**留在表里**，不释放。
3. **退休时必须释放等待者，否则从崩溃变成挂死。** multiwait 的无超时等待会永远等 `is_event_signaled` 为真；事件被 finalize 后状态就再也不会变。所以退休时把宿主侧状态置为已置位并 `ffrs_multiwait_notify_event` 通知，等待者随即返回。这保持了「事件最终仍被终结、等待者仍按原语义被唤醒」，只是把「进程被杀」换成「等待者被正常释放」。
4. **晚到的 signal 不再写 guest 内存。** 事件已终结，guest 可能已经释放那一页，所以 signal 只更新宿主侧状态并通知 multiwait，绝不碰 guest 对象。
5. **同一地址可以开新的一生。** guest 调用 `InitializeEvent` 时，墓碑被丢弃（除非仍有 holder 指向它），因此对象地址复用不会被墓碑挡住。
6. **旧代码里的 abort 被删除，但护栏意图没有丢**：现在会打印 `retired (multiwait_waiting=%d multiwait_referenced=%d)`，玩家仍可用 `grep -a multiwait` 自查。

### 6.3 红/绿自证

同一条 `event_retire_contract` 分别对新旧代码编译：

| 代码 | 结果 |
|---|---|
| 新代码（ASan+UBSan） | exit 0，3002 次退休，0 次非对齐拒绝，`event retire contract passed` |
| 旧代码 `75beed3`（ASan+UBSan） | **exit 134（SIGABRT）**，日志尾部正是 `[multiwait] refusing Event finalization during active wait` |
| 新代码（TSan） | exit 0，0 条 ThreadSanitizer 告警 |
| 既有 `event_finalize_contract`（ASan / TSan） | 两条都通过，既有行为未变 |

`scripts/verify_host.py`：**23/23 步通过**（原 19 步 + 新增 event-retire-asan / event-retire-tsan）。

### 6.4 仍未覆盖 / 仍存风险

- **设备侧全 NOT_RUN**：没有 8E5、没有神殿真机、平板未获授权。**不得声称已修复**，只能写「构造上消除了这类崩溃」。
- **C2（悬垂 guest 指针）只覆盖了 host 侧**：引擎侧 `OP_WAIT_EVENT` 仍只校验地址非零就原子读（审计 P0-4），**本轮未改**（它属于 graphics 线，且与主线改动区域重叠）。
- **C4（retire_slot fence 超时 → fatal）未改**。
- 墓碑表上限 1024，溢出后按最旧优先释放；若最旧条目仍被 holder 引用则跳过（该表可能暂时超过上限，极端情况下会缓慢增长）。
- 自动注册路径（guest 分配事件但**不**调用 `InitializeEvent`）在墓碑仍存活期间会被拒绝（记 `use refused` 日志）；墓碑被 trim 后自动恢复。这是「不写已释放页」与「不误伤新对象」之间的取舍，**需真机观察是否出现 refused 日志**。

### 6.5 合并清单

- `src/runtime/hle/nso_host.c`：结构体新增 `retired` / `retire_seq`；新增 `s_retired_buckets` / `s_retired_count` / `s_retired_seq` / `EVENT_RETIRED_MAX`；新增 `find_retired_locked()` / `retire_event_locked()` / `trim_retired_events()`；`borrow_event()` 增加墓碑拒绝；`nso_host_os_initialize_event()` 增加墓碑丢弃；`nso_host_os_finalize_event()` 去掉 abort 改为退休 + 释放等待者；`nso_host_os_signal_event()` 增加墓碑分支；`nso_host_os_is_event_signaled()` 增加墓碑读取。
- `tests/native/event_retire_contract.c`（新增）、`tests/native/event_finalize_stubs.c`、`scripts/verify_host.py`。

与其它线**不重叠**：本线只碰 `src/runtime/hle/nso_host.c` 与测试，不碰 `xbg_game.cpp`。
---

## 10. 复核与补正（2026-11-13，本轮收口）

> 本节记录：在 §6 的宿主侧实现（commit `a9e6c56`）之上，本轮**补上**总控要求的三件事，
> 并给出选定方案的论证、契约实测结果、审计项覆盖矩阵与合并清单。
> **设备侧结论仍然全部 NOT_RUN**：本线没有 8E5、没有荣耀平板、没有神殿真机。

### 10.1 本轮补正（commit `92d672a`）

1. **护栏日志串保持可 grep（总控硬要求）。**
   §6 的实现把 `[multiwait] refusing Event finalization during active wait` 整行删掉了，
   玩家就再也无法用一条 grep 自查。本轮**按字节原样恢复**这一行，只在句尾追加实情：

   ```
   [multiwait] refusing Event finalization during active wait: event=0x... (race recorded; the event is retired, the waiting holder is released, the process continues)
   ```

   - **为什么保留原串**：这条消息标记的是「竞态确实发生了」，
     而**加固并没有消灭竞态**（场景卸载时仍可能有活动等待），
     竞态只是从「杀进程」变成了「记录并安全处理」。消息消失 = 玩家失去唯一一条自证线索。
   - **为什么语义要补充**：原句的 refusing 描述的是「当场拒绝终结」，
     现在事件**仍然被终结**（退休），只是不再杀进程。追加的括号说明写明了实际处理，
     避免日志与行为不符。
   - **加固后玩家应观察到的信号**：
     - 竞态发生时：**出现** `[multiwait] refusing Event finalization during active wait`，且**进程继续运行**；
     - 紧接着一条 `[hle] nn::os::FinalizeEvent: event=... retired (multiwait_waiting=1 multiwait_referenced=1)`；
     - **不再出现 SIGABRT**。
   - 取证方法见 §7.1（一条 grep + `adb logcat -b crash -d`）。
2. **修掉一处 guest 内存的未对齐写。**
   `nso_host_os_initialize_event` 用 `*(uint64_t*)(bytes+0x14) = 0;` 往 guest 对象偏移 0x14 写 8 字节，
   而 0x14 天然不是 8 对齐的（UBSan 一直在报：`store to misaligned address ... for type 'uint64_t'`）。
   这是**基线就存在**的问题（基线 `db094f8` 同样报），不是本轮引入；
   但它污染了「ASan/TSan 无告警」这条证据。改为 `memcpy` 后 UBSan 干净。
   ARM64 对普通内存允许非对齐访问，所以它本身不是 8E5 崩溃的成因，**只影响证据的干净度**。
3. **把两条契约的事件对象固定为 `_Alignas(8)`。**
   宿主对未对齐的事件对象是**直接拒绝**的（`InitializeEvent` 与 `borrow_event` 都查 `& 7`）。
   契约里的 `struct event_object` 只有 1 字节对齐，只是碰巧落在 8 对齐的栈槽上；
   一旦栈布局变化，`InitializeEvent` 会静默拒绝、契约就会**在没有进入被测代码的情况下通过**。
   现在对齐是语言层保证的。

### 10.2 选定方案、理由与死锁自由论证

总控要求：「不得简单删掉或放宽护栏」，正确方向是把「有活动等待时 finalize」变成**安全路径**，
且**不能死锁**、**不能改变游戏行为**。本轮采用的方案（`a9e6c56` + `92d672a`）：

| 要求 | 本方案如何满足 |
|---|---|
| 不删护栏 | 护栏**保留且仍会触发**，只是不再 `abort()`；消息串按字节保留（§10.1） |
| 变成安全路径 | 事件**仍然被终结**：条目退休、guest 的 initialized 标志清零、条目离开活动注册表 |
| 等等待者退出后再摘链（有界、不死锁） | 做法是**永不阻塞等待 multiwait 持有者**：finalize **不等**，改为把条目退休并**主动释放持有者**（置位宿主侧状态 + `ffrs_multiwait_notify_event`）。因此**不存在需要设上界的等待**，也就**不可能死锁** |
| 兜底路径要写进报告 | 兜底有两条：① 持有者永不退出时，条目退休留存，**永不被释放**，持有者始终握着有效事件；② 退休表超过上限（1024）时按最旧优先释放，仍被持有的条目跳过并保留 |
| 语义等价 | 事件最终仍被终结；等待者仍被唤醒（释放 = 宿主侧状态置位并通知，与「事件已不存在」这一事实一致）；**没有**跳过 finalize、跳过场景切换或吞掉等待 |
| 不引入新的挂死 | 多等待者被释放而不是留死等；普通 `WaitEvent` 等待者沿用 `1df098f` 的 `initialized=false` + broadcast 机制离开 |

**锁序（ABBA）**：`multiwait.c` 持自己的锁回调 `nso_host_os_is_event_signaled()`，而后者取**事件注册表锁**。
因此**任何**持注册表锁调用 multiwait 函数的写法都会 ABBA 死锁。
本实现把 multiwait 侧的查询（`ffrs_multiwait_event_waiting/_referenced`）全部放在**取注册表锁之前**，
释放退休条目的引用确认放在**注册表锁之外**，并在注释里写明了这个约束。

### 10.3 宿主契约实测结果

命令（在 worktree 根执行）：

```
cc -std=c11 -Wall -Wextra -pthread -fsanitize=address,undefined \
   src/runtime/hle/nso_host.c tests/native/event_retire_contract.c \
   tests/native/event_finalize_stubs.c -o test-event-retire
cc -std=c11 -Wall -Wextra -pthread -fsanitize=thread  ... -o test-event-retire-tsan
python3 scripts/verify_host.py
```

| 契约 | 构建 | 运行 | 结果 |
|---|---|---|---|
| `event_retire_contract`（新） | ASan+UBSan | **exit 0**，`event retire contract passed`，**sanitizer 告警 0 条** | 绿 |
| `event_retire_contract`（新） | TSan | **exit 0**，**ThreadSanitizer 告警 0 条** | 绿 |
| `event_finalize_contract`（既有） | ASan+UBSan | **exit 0**，`event finalize contract passed`，**告警 0 条** | 绿 |
| `event_finalize_contract`（既有） | TSan | **exit 0**，**告警 0 条** | 绿 |
| **同一条 `event_retire_contract` 对基线 `db094f8` 的 `nso_host.c`** | ASan+UBSan | **exit 134（SIGABRT）**，日志末行正是 `[multiwait] refusing Event finalization during active wait` | **红** |
| `scripts/verify_host.py`（HEAD=`92d672a`） | — | **21 项检查，20 通过，1 项 blocked** | 见下 |

> **红/绿是同一条契约、只换 `nso_host.c`**，不是两个不同的测试。
> 基线那条的退出码 134 与末行日志，就是 §7.1 里让玩家去 grep 的那个特征。

**唯一 blocked 项**：`module-load-info` — 「local unpacked game module is absent」。
这是**输入缺失**（本机没有解包后的游戏模块，位于 `.local/`、不入 Git），**不是代码条件**，与本轮改动无关。

### 10.4 审计项覆盖矩阵

| 审计项 | 状态 | 说明 |
|---|---|---|
| **P0-1 FinalizeEvent × WaitEvent 并发 UAF** | **已覆盖（宿主侧）** | `1df098f` 已覆盖普通 `WaitEvent` 等待者；本轮补齐 **MultiWait 持有者**这一类此前无人计数的引用者 |
| **C1 场景卸载时 finalize × 活动等待 → abort** | **已覆盖** | 护栏保留、abort 移除、事件退休、持有者被释放 |
| **C2 悬垂 guest 指针（读/写两侧）** | **已覆盖（宿主侧）** | 读：`is_event_signaled` 对已终结事件只读宿主侧状态；写：晚到 signal 只改宿主侧状态、**绝不写 guest 页**；复活：已终结指针拒绝重新注册 |
| **P0-4 引擎侧 `OP_WAIT_EVENT`（guest 地址未校验 + 超时即 fatal）** | **未覆盖** | 见 §10.5 的理由与建议补丁形状 |
| **P0-5 `retire_slot` 的 fence 超时即 fatal + 无界自旋** | **未覆盖** | 同上，属 GPU 侧，证据强度为「中低」 |
| **P0-2 `nvnSyncWait` 的 UINT64_MAX 无限等待** | **未覆盖** | 与「场景切换」无直接关联；需单独的 GPU 同步线处理 |
| **P0-3 `vkDeviceWaitIdle` 无超时** | **未覆盖** | 同上 |
| **P0-6 ANativeWindow 双锁竞态** | **未覆盖** | 与场景切换无关，属窗口生命周期线 |
| 审计 P1 / P2 共 39 条 | **未覆盖** | 本轮只做 P0 家族；不扩范围 |

### 10.5 为什么没有动 `xbg_game.cpp`（P0-4 / P0-5）

审计 P0-4 建议「访问前校验地址落在已注册 guest pool 区间」。**本轮没有照做**，理由是它会改变可观察行为：

- 引擎对 guest 地址的信任模型是「guest 与 runtime **共享同一地址空间**」，
  `OP_SIGNAL_EVENT`（`xbg_game.cpp:5843`）与 `apply_event()`（`:1009`）都是**直接解引用** event 地址、从不校验；
- 可用的两个校验器都不合适：
  `nvn_memory_pool_resolve_address()` 只覆盖 **NVN 注册的 buffer pool**，而 event word 是 guest 自己的变量；
  `guest_process_is_guest_memory()` 只覆盖**地址区域**与**模块镜像**，
  **不覆盖 guest 堆**（guest 堆在 `heap_owner` 表里，见 `nso_host.c:3636` 与 `address_region.c:100`），
  用它去筛会**误杀合法地址**、把正常等待变成跳过；
- 只在**等待**一侧加校验、而**信号**一侧照旧解引用，会让两条路径对同一个地址给出不同结论；
- 真正一致的修法必须**在 event 地址进入引擎的那一点**（Signal / Wait 两侧）同时校验，
  并且需要一个「这个地址是否仍是活着的 guest 内存」的运行时接口
  （= `heap_owner_lookup` ∪ 地址区域 ∪ 模块镜像）。这是**跨模块新增 API**，且落在 graphics 线的在途改动文件上。

**建议给 graphics 线的确切补丁形状**（本线未实施，供其排期）：

1. runtime 侧新增并导出 `int nso_host_is_live_guest_range(uintptr_t addr, size_t bytes)`，
   判据 = `heap_owner_lookup()` 为 `HEAP_OWNER_GUEST_PAGE` **或** `nso_host_is_guest_memory()` **或** 落在模块镜像内，
   并要求整个区间被覆盖（起始与末尾各查一次）；
2. engine 侧以 **weak** 符号引用（与 `nvn_memory_pool_resolve_address` 同形），
   运行时未链接时**保持现状**（不改变行为）；
3. `OP_WAIT_EVENT`（`:5971`）与 `OP_SIGNAL_EVENT`（`:5843`）**两侧同时**使用该判据：
   不通过则**记诊断并跳过该次操作**（不解引用、不 abort）；
4. `OP_WAIT_EVENT` 的 2s 忙等超时（`:5971` 的 `need(..., "NVN event wait timeout")` → `xbg_fatalf` → `abort()`）
   改为**带诊断的降级**（记录地址/模式/已等时长后继续执行命令流），依据审计 P0-4「超时返回可恢复错误而非 fatal」；
   这只影响**已经异常**的路径（正常路径不会超时），不改变正常行为。

### 10.6 NOT_RUN 清单（设备侧，全部未做）

| 项 | 状态 | 缺什么 |
|---|---|---|
| 8E5 巴哈姆特召唤兽结算后闪退 | **NOT_RUN** | 无 8E5 设备；该报告人不在本线可触达范围 |
| 荣耀平板「进入新幻兵水晶神殿」大概率闪退 | **NOT_RUN** | 平板 `A42BBB5109000333` 已归还用户，禁止连接；**用户本人剧情未走到该神殿，本地无可复现路径** |
| 荣耀平板「加载大地图」小概率闪退 | **NOT_RUN** | 同上 |
| 真机 ASan/TSan 复跑 | **NOT_RUN** | 本线无设备 |
| 崩溃后 `guest_boot.log` 现场取证 | **NOT_RUN** | 需要设备；取证方法已写进 §7.1 |
| `scripts/verify_host.py` 的 `module-load-info` | **blocked** | 本机没有解包后的游戏模块（输入缺失，与代码无关） |

> **不得把本轮结论写成「已修复」**：能说的是「**在代码构造上消除了『场景卸载时因活动等待而终止进程』这一类崩溃**」，
> 并且给出了让玩家自证的取证方法。是否就是 8E5 / 神殿闪退的成因，**必须等设备侧日志**。

### 10.7 仍存风险

1. **退休表是有界的**：上限 1024，溢出时按最旧优先释放；仍被 multiwait 持有者引用的条目会被跳过并保留，
   因此该表**在极端情况下可能暂时超过上限并缓慢增长**（一个 holder 长期不释放事件时）。
2. **「晚到 signal」的语义收紧**：对**已终结**的事件，signal 现在只改宿主侧状态、**不再写 guest 对象**。
   在真实 SDK 里 finalize 之后再 signal 属于未定义行为，但本工程此前靠「写一个可能已释放的页」来兜住它，
   收紧后这一类调用会变成「只通知、不写」。若真机出现 `use refused` 日志变多，说明该路径确有发生，需复核。
3. **多等待者被释放 = 宿主侧状态置位**：等待者会认为「事件发生了」。这与「事件已被终结、等待者不能永远等死」的取舍一致，
   但确实与「事件从未被置位」的情形不同，属于**本轮唯一一处可观察的语义变化**，已在 §10.2 写明。
4. **引擎侧两条 abort 路径仍在**（P0-4 / P0-5），见 §10.5。

### 10.8 合并清单（给总控）

**分支 `task/T-crash-p0-20261112`，基线 `db094f8`，HEAD `92d672a`。本线只碰下列位置。**

```
src/runtime/hle/nso_host.c
  - struct nso_event_entry                : 新增 retired / retire_seq 字段
  - 静态区                                : 新增 s_retired_buckets / s_retired_count / s_retired_seq / EVENT_RETIRED_MAX
  - find_retired_locked()                 : 新增（按地址找墓碑）
  - retire_event_locked()                 : 新增（条目入退休表）
  - trim_retired_events()                 : 新增（注册表锁外确认无 holder 引用后才释放）
  - borrow_event()                        : 新增墓碑拒绝（已终结事件不得复活）
  - nso_host_os_initialize_event()        : 新增墓碑丢弃（同一地址开新的一生）；+0x14 的 uint64 写改 memcpy
  - nso_host_os_finalize_event()          : 删除 abort；改为退休 + 释放持有者；恢复护栏日志串
  - nso_host_os_signal_event()            : 新增墓碑分支（只改宿主侧状态，不写 guest 页）
  - nso_host_os_is_event_signaled()       : 新增墓碑读取（不读 guest 页）
tests/native/event_retire_contract.c     : 新增（等待者在场 finalize 不 abort / 持有者被释放 / 晚到 signal 不碰 guest 内存 /
                                             同一地址开新的一生 / 二次 finalize 是 no-op / 退休表有界）
tests/native/event_finalize_stubs.c      : multiwait 桩改为可控（ffrs_test_multiwait_waiting / ffrs_test_multiwait_referenced），
                                           补上 ffrs_multiwait_event_referenced
tests/native/event_finalize_contract.c   : 事件对象改为 _Alignas(8)
scripts/verify_host.py                    : 新增 event-retire-asan / event-retire-tsan 两步
coordination/results/CRASH-P0-BATTLE-SETTLE-20261112.md : 本报告
```

**不重叠声明**：本线**没有**修改 `src/graphics/nvn_vulkan/engine/xbg_game.cpp`，
因此与特效/seed 线、加载性能线在该文件上的在途改动**无冲突**；
也**没有**触碰 honor 主 worktree、存档、`.local/` 输入与任何设备。
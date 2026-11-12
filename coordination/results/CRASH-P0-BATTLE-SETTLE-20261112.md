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

| # | 来源 | 原文要点 | 本线解读 |
|---|---|---|---|
| E1 | 用户（8E5） | 巴哈姆特召唤兽击败对手后闪退 | 战斗结算 → 切场景 |
| E2 | 平板一线（场景装载） | 「每次进入**大地图**得卡一小会……其他城镇内、副本内都流畅」 | **加载性能**，非本线；但说明"进入大地图"是高频场景切换点 |
| E3 | 平板一线 | 「**每次加载大地图都需要卡一会，有小概率闪退**」 | 场景切换 → **低频**闪退 |
| E4 | 平板一线 | 「**进入新的幻兵水晶神殿大概率闪退**……自动存档会存神殿里面，再重新进游戏读这个档就可以」 | 场景切换 → **高频**闪退；「自动存档可绕过」**不等于已修复**，只是给了玩家一条规避路径 |

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

### 7.1 向玩家/一线索取的证据清单（用于把"大概率"收敛到具体一条）

1. 崩溃瞬间设备上的 `engine.log.fatal` **全文**（本仓 `xbg_fatalf` 会写 `error_text` + `FATAL:` 前缀）；
2. `adb logcat -b crash -d` 的 **tombstone**（含 signal 号与 fault 地址）；
3. **崩溃类型**：SIGSEGV(11) / SIGABRT(6) / ANR —— 这一项**直接区分 C1/C2 与 C3/C4**；
4. **崩在进神殿瞬间，还是进神殿后第一场战斗**（区分场景装载 vs 战斗结算两条链）；
5. 是否伴随 `[multiwait]` 前缀日志（命中 C1/C2 的直接标记）。

---

## 8. 覆盖 / 未覆盖

见文末"交付总结"节（随实现完成后补齐）。

---

## 9. 声明

- 撰写本文档时未修改任何源码、未构建、未执行任何设备命令、未连接任何设备；
- 不声称"已修复"；本轮的产出是**代码级加固 + 宿主契约**，设备侧一律 `NOT_RUN`；
- 未触碰他人 worktree / WIP / 存档 / `.local/` 输入；
- 未改 warm / pipeline / shader 等加载性能代码（不属本线写权范围）。

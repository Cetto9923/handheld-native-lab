# 全量代码闪退 / 卡顿风险审计（2026-11-11）

> **本轮性质：只读静态审计。** 未修改任何源码、未构建、未执行任何 `adb`/真机命令、未做 git 写操作；仅新增本报告文件。所有结论为**源码可达性分析**，不是真机复现、不是性能实测、不是验收结论。设备侧一律标记 `NOT_RUN`。
>
> 行号均按磁盘原始行（`split('\n')`）实测。**Java 文件检索工具显示的行号比磁盘真实行号小 26 行**，本报告统一采用磁盘真实行号，可直接用 `sed -n 'Np'` 复核。

---

## 0. 任务定位

用户要求：全量代码检查和扫描，找出**仍可能出现闪退或卡顿的场景的可能性**。

按 AGENTS.md / AGENTS-HONOR.md 约束执行：只读、不碰设备、不碰存档、不碰 WIP、不改 `main`、不推送。本报告只回答"哪些路径仍存在风险、触发条件是什么、建议怎么修"，**不实施修复**；修复需用户单独下指令。

## 1. 扫描范围与规模（实测）

| 模块 | 文件数 | 行数 | 覆盖 |
|---|---:|---:|---|
| `src/runtime/`（HLE + NSO） | 48 | 14,620 | 全量深读 + 危险模式扫描 |
| `src/graphics/`（NVN 设备层 / Vulkan 引擎 / probe） | 139 | 46,996 | 全量深读 + 危险模式扫描 |
| `src/android/native/`（JNI） | 28 | 3,518 | 全量深读 |
| `src/android/app/java/`（Activity / Installer / 输入 / 存档） | 17 | 5,158 | 全量深读 |
| **合计** | **232** | **70,351** | |

方法：4 路并行模块审计（runtime / NVN 设备层 / Vulkan 引擎 / Android-JNI-Java）＋ 全局危险模式检索（分配返回值、`abort()`、阻塞等待、`usleep/nanosleep`、自旋、Vulkan 返回值、每帧分配与拷贝）＋ 对全部 P0 与主要 P1 逐条**回读源码交叉验证**。

未逐行覆盖 `tests/`(154)、`scripts/`(125)、`coordination/`(170)、`docs/`(77)、`third_party/`：这些不参与运行时出图/输入/存档/生命周期路径，不改变本报告的闪退/卡顿结论；如需要构建脚本与 CI 侧的稳定性风险，可另做一轮。

全局计数（裸串检索，可靠）：`src/` 中 `abort()` 出现 **65 处**；`xbg_game.cpp` 中 `need(` / `xbg_fatalf(` / `VK(` 合计 **367 行**，且 `xbg_fatalf` 是无恢复的 `__attribute__((noreturn))` + `abort()`（xbg_game.cpp:2549）。这是"任何一处校验失败即进程终止"的结构性放大因素。

## 2. 结论摘要

- **P0（6 条）**：可导致进程崩溃或永久卡死，且路径可达。集中在两条主线：**host event / ANativeWindow 生命周期竞态** 与 **GPU 同步等待缺少兜底上限**。
- **P1（27 条）**：特定路径崩溃、明显卡顿或 ANR。集中在：**持锁限帧与冷 shader 编译**、**每帧大拷贝（vcache 未生效）**、**无条件 `abort()` 把可恢复错误升级为进程终止**、**UI 线程同步 IO**。
- **P2 / 条件性（12 条）**：理论竞态、低概率越界、诊断路径开销。
- **误报剔除 3 项**（见第 7 节），其中两处 weak-symbol `abort()` 因强定义存在而不成立。
- **已修复项核实**：`readback_cached`、pipeline warm list、`legacy_waitall=false`、`completion_wait`、`fence_spin_us=0`、async pool-reset 错误检查在源码开关层面按预期生效；但 `shader_reuse=0`、`vcache` 被 stub 门控关闭、`DM_BC_THREADS=0` 仍是当前默认下的性能缺口。

---

## 3. 主表：problem / suggestion / severity

### 3.1 闪退 / 卡死类

| problem（含证据位置） | suggestion | severity |
|---|---|---|
| `nso_host.c:2850-2874` FinalizeEvent 摘链后 `pthread_cond_destroy`/`mutex_destroy`/`free(e)`，不检查 `users`/`waiters`；`2923-2938` WaitEvent 仍阻塞在同一 cond 上 | 摘链后标记 dying，`users==0 && waiters==0` 才销毁；或引用计数延迟释放 | **P0** |
| `nvn_sync.c:415/461-467` guest 传 `-1`(UINT64_MAX) → `nvn_engine.c:252` → `xbg_game.cpp:6018/6025/6060/6091` → `vkWaitForFences(...,UINT64_MAX)`；`xbg_completion_wait.h:25-26` 明确保留 UINT64_MAX 无限语义 | 在 `nvnSyncWait` 入口钳位到有界超时并映射 `TIMEOUT_EXPIRED`；device-lost 单独降级 | **P0** |
| `nvn_backend_vulkan.c:990` `vkDeviceWaitIdle` 无超时，surface/swapchain 重建路径必走（`1172/1185/1355`） | 改按 fence 逐个超时回收，加 device-lost 分支 | **P0** |
| `xbg_game.cpp:5525-5527` OP_WAIT_EVENT 忙等 2s 后 `need()`→`xbg_fatalf`→`abort()`；地址仅校验非零即原子读 | 改条件变量超时等待、超时返回可恢复错误；校验地址属于已注册 pool | **P0** |
| `xbg_game.cpp:1014` `VK(hybrid_wait_fence(...,max(...,10s)))`，VK_TIMEOUT/DEVICE_LOST 经 `468/467/2549` 直接 abort；`1052` `while(s.waiters.n.load()) yield()` 无超时 | 超时/设备错误进入 slot 丢弃与队列恢复；waiters 自旋加 deadline 与退避 | **P0** |
| `game_window_jni.cpp:114-124/130-139` JNI `g_window_lock` 与 `native_window.c:15` `g_native_window_use_lock` 是两把锁，release 在锁外；`native_window.c:17-19` 消费方用无锁 `get()`（`nvn_backend_vulkan.c:1159`） | JNI 等 runtime 租约释放后再 release；渲染侧统一走 `nso_host_native_window_use()` | **P0** |
| `nso_host.c:4033/4405` `std::__throw_system_error` 导入绑定无条件 `abort()` | 映射为可抛/可返回 Result | P1 |
| `nso_host.c:647/3357/456/2018` page service / allocator interpose / 线程生命周期 / TLS cleanup 的非 OK 状态一律 abort | 返回 SDK Result，不终止进程 | P1 |
| `nso_host.c:3609/3652` `get_std_mutex`/`get_std_cv` 的 `calloc` 未判空即写 `e->guest_ptr` | 判空并返回错误 | P1 |
| `native_window.c:48-74` + `nso_host.c:3281-3289` 200ms×60s 忙轮询，超时写回 NULL（源码注释自承 "the guest will fault on it"） | 异步通知/快速失败返回错误码，禁止在 guest 调用线程轮询 | P1 |
| `nso_host.c:2258/2288` 外来 fd 阻塞 `read()` 无超时，且 2258 在 `s_socket_events_mutex` 内 | `poll`+超时或强制 `O_NONBLOCK`，锁外读 | P1 |
| `nso_host.c:1892/2928`、`multiwait.c:73-75` untimed `pthread_cond_wait` 无兜底；`nso_host.c:480` 附近 DestroyThread 等 `finished` 无超时 | 加长超时保护 / 关机广播唤醒 | P1 |
| `nso_host.c:3177/3221/3343` vsync worker `while(1)` 不 join、Display 销毁后仍每 16.7ms 解引用 guest 事件指针 | stop 标志 + join，销毁时清空事件指针 | P1 |
| `audio_bridge.c:567-569` `close_output` 无引用计数，并发 Close/append 后 UAF + 双 join | 引用计数 + 锁内摘槽置 closing | P1 |
| `nvn_backend_vulkan.c:1078/1353-1354` `VK_ERROR_DEVICE_LOST` 仅在 acquire 侧命名，present 路径不处理、继续用已 lost device | 置 `present_path_ready=0` 并触发完整重建 | P1 |
| `nvn_backend_vulkan.c:1230/1324/1490` `vkResetCommandBuffer`/`vkResetFences` 返回值被丢弃 | 检查返回值并先等对应 fence | P1 |
| `nvn_command_buffer.c:596/611` 录制热路径 `calloc` 失败直接 abort；`2719` readers 下溢 abort；`nvn_queue.c:624` lease 快照 abort | 返回错误码 + 原子计数 + 日志降级 | P1 |
| `nvn_command_buffer.c:1133-1150` `rec_set_scissors` 只校验 `count<0`，同类 `1109 set_viewports` 有 `first+count>16` 上界 | 统一钳位到 NVN 上限（16/32） | P1 |
| `xbg_game.cpp:2348-2355` 未知 `OpCapability`/`OpExtension` 直接 fatal；`count==1` 时 `w[i+1]` 可越界读 | 白名单降级 + `count>=2` 边界 | P1 |
| `xbg_game.cpp:5383/5966-5969` 命令计数 1000 万上限、CopyCommands 源链失败即 fatal | 上限返回可恢复错误，单次遍历增量记录 | P1 |
| `xbg_game.cpp:3326-3329/3369/3610` descriptor slot 16/128 魔数、`indirect` 参数非法时 `need()` 失败即 abort | 非法绑定降级为空/默认描述符并计数 | P1 |
| `xbg_game.cpp:125/800/2713/2726` worker 线程函数无 `try/catch`，异常逃逸即 `std::terminate`；warm/async 队列无界 | 每个 worker 包 `catch(...)` 标记 failed；队列有界丢弃 | P1 |
| `xbg_texture.c:381` 启用 BC 线程后无超时等待 `j.active`；`339-343` worker `for(;;)` 永不退出 | 加退出条件与超时广播 | P1 |
| `GameActivity.java:2115/2135-2136/2190` `GUEST_START.begin()` 成功后 catch 分支直接 `return`，不 `finish()` → 状态永久 STARTING、setupView 隐藏 | `finish(false)` 放 finally，覆盖所有早退分支 | P1 |
| `DataBundleInstaller.java:104/106/161/272` `stores` 跨 Job/UI 线程非同步集合，`pendingDirectoryRescan` 非 volatile | 同步集合 + volatile + 仅 UI 访问 | P1 |
| `GameActivity.java:1606` surfaceCreated 允许外部 Intent `run_probe=true` 与 guest 并发对同一 window 建 VkSurface（`game_vulkan_probe.cpp:338/395`） | 正式包剔除 probe 分支或强制与 guest 互斥 | P1 |
| `nvn_command_replay.c:399-421` 锁外遍历 `package.records`，`packages_collect` 可并发 `free` | 回放改走 `package_acquire/release` lease | P2 |
| `nvn_object_table.h:67-79` `realloc` 移动 slots 使跨解锁持有的族内 `*_state*` 悬空，索引不校验 generation | 稳定索引 + generation 句柄 / slab | P2 |
| `display_jni.cpp:8-9`、`guest_boot_jni.cpp:220` 失败早退未 `ExceptionClear()`，Java 侧带 pending 异常继续 | 早退路径统一清异常并置错误码 | P2 |
| `nvn_event.c:195`、`nvn_memory_pool.c:330` weak 兜底 abort | **当前产品构建有强定义，不成立**；仅保留"新增构建目标漏链"的启动期自检提醒 | 已剔除 |

### 3.2 卡顿 / ANR 类

| problem（含证据位置） | suggestion | severity |
|---|---|---|
| `xbg_game.cpp:6127-6172` 限帧 `nanosleep` + 最多约 1.2ms 忙等自旋，调用点 `6269` 位于 `std::lock_guard<std::recursive_mutex> lock(mutex)` 内；30 FPS 默认启用 | 限帧移到提交线程之外的独立时钟，忙等改绝对到期 `clock_nanosleep` | **P1** |
| `xbg_game.cpp:2501-2533` 冷 shader 在提交线程同步做磁盘读 + SPIR-V 重写 + `vkCreateShaderModule`；`789/5803` `shader_reuse=0`（HANDOFF 实测两个 compute 重写 76.62ms / 48.02ms） | 整条 load 移预热线程，渲染线程用占位/延迟管线 | **P1** |
| `xbg_game.cpp:49-52/1507-1535` 5 个 `nvn_dirty_*` 是 weak stub（`nvn_dirty_since` 恒 0），vcache 永不启用 → 每帧 vertex 6.9–7.6MB + index 1.27–1.34MB 全量 memcpy（simpleperf memmove 8.55%） | 实现真实 dirty-page tracker 或内容 hash 复用，再默认打开 vcache；**不得只改 marker 假启用** | **P1** |
| `xbg_game.cpp:5406` OP_COPY_B2B 未覆盖路径单条 ≤256MB 同步 `memmove` 在渲染线程 | 强制 GPU copy 或分片 + 栅栏排序 | P1 |
| `xbg_texture.c:347` `DM_BC_THREADS` 默认 0 = 串行软解 BC 贴图（源码注释：翻译线程约 80% 忙在 `xbg_decode_block`） | 默认给合理线程数、结果跨帧缓存 | P1 |
| `nso_host.c:1763-1775` 存档提交在全局 `s_fs_file_mutex` 内对最多 128 个 fd 逐个 `fsync`；`CloseFile/FlushFile/WriteFile` 同样持锁同步 IO | 细粒度每文件锁，同步 IO 移出全局锁 | P1 |
| `heap_owner.c:32/57/90` 每次 malloc/free 走全局锁内 O(n) `memmove` 保序表 | 哈希桶 / 分级锁 / 线程缓存 | P1 |
| `nvn_backend_vulkan.c:1165-1200` 每帧 `usleep(16000)` 丢帧循环 + OUT_OF_DATE 时反复 destroy/rebuild（含无超时 waitIdle）无退避 | 退避节流，SKIPPED 不上报 vsync | P1 |
| `nvn_sync.c:202-226` `sync_find` 持全局锁 O(n) 全表扫描，族内可达数千对象 | 地址→索引哈希表 | P1 |
| `GameActivity.java:1731/1678/836/1361` 主线程递归 `listFiles` 扫描、金手指整档读入+解密+写回、存档导出 | 全部移单一工作线程 | P1 |
| `xbg_game.cpp:1204-1217/2780-2781` pipeline cache 每 300 帧约 17.7MB 序列化写盘 + warm 并行编译争 CPU/IO | 增量写盘 + 按负载限流 | P2 |
| `xbg_game.cpp:3202-3260` per-draw `std::vector` key/ktag 构造、`std::sort`、hash 查表（profile descriptors 7.15% + build 5.28%） | 定长栈 key + 开放寻址缓存 | P2 |
| `GameActivity.java:2262` 退出轮询无超时、终态无重启入口 | 加超时提示与重启 UI | P2 |
| `GameActivity.java:198-207/589-626` 输入洪泛逐帧 `Log` 拼接 + `RectF`/数组分配 | 日志降级、对象预分配、按帧节流 | P2 |
| `DiagnosticActivity.java` 每个输入事件主线程追加写盘 + `fsync`（模块审计结论，未逐行回读复核） | 移工作线程 | P2 |

---

## 4. P0 逐条证据与"字面落地"修复清单

> 以下 6 条均已回读源码确认；**本轮不实施**，仅给出可逐条落地的具体动作。

**P0-1 FinalizeEvent × WaitEvent 并发 UAF**
- 证据：`nso_host.c:2753-2783`（`borrow_event` 只 `users++` 后解锁）、`2786-2791`（`release_event` 只 `users--`）、`2850-2874`（Finalize 摘链 → `2867` lock → `2869` broadcast → `2871/2872` destroy cond/mutex → `2873` free）、`2923-2938`（WaitEvent 在 `2928-2930` 阻塞于 `pthread_cond_wait` 并 `e->waiters++`）。
- 唯一防护 `2855 ffrs_multiwait_event_waiting` 只覆盖 multiwait 等待者，不覆盖普通 WaitEvent。
- 触发：线程 A 在 `WaitEvent` 阻塞，线程 B 对同一 event 调 `FinalizeEvent`（关机、场景卸载、模块退出常见）。
- 后果：等待线程在已销毁 mutex 上重锁、在已 free 内存上写 `waiters` → SIGSEGV / UB。
- 落地：`nso_host_os_finalize_event` 摘链后置 `dying=true` 并 broadcast；`release_event` 在 `users==0 && waiters==0 && dying` 时执行 destroy+free；`WaitEvent` 唤醒后先判 dying 再触碰 `e` 之外状态。

**P0-2 nvnSyncWait 的 UINT64_MAX 无限等待**
- 证据：`nvn_sync.c:40-42` 注释明确 guest 传 `-1`=wait forever；`415/461-467` 把 `timeout` 原样交给 `nvn_engine_wait`；`nvn_engine.c:252-253` 转发 `xbg_legacy_wait`；`xbg_game.cpp:6022/6025` `Deadline(completion_path?timeout:UINT64_MAX)`，`xbg_completion_wait.h:25-26` 对 `UINT64_MAX` **显式返回 UINT64_MAX（不钳位）**；`6060/6089/6091` `remaining` 取 `deadline.remaining_ns()` 或原 `timeout`；`1008` `vkWaitForFences(..., timeout_ns)`。另 `nvn_window.c:553` 直接传 `UINT64_MAX`。
- 触发：GPU hang、`VK_ERROR_DEVICE_LOST`、fence 永未 signal，且 guest 走 `nvnSyncWait(-1)` / window wait。
- 后果：渲染线程永久阻塞 → 无响应 / ANR / 看门狗杀进程。
- 落地：`nvnSyncWait` 入口把 `timeout==UINT64_MAX` 钳位为有界值（建议 2s，走现有 `fence_timeout_ns` 同源配置），超时返回 `TIMEOUT_EXPIRED`；`VK_ERROR_DEVICE_LOST` 单独识别并触发降级；保留 diagnostic marker 显式 opt-out。
- **交叉验证说明**：一度怀疑 `completion_path` 的 Deadline 会兜住无限超时；回读 `xbg_completion_wait.h:25-26` 确认其保留无限语义，**风险成立，不剔除**。

**P0-3 `vkDeviceWaitIdle` 无超时**
- 证据：`nvn_backend_vulkan.c:990`，位于 `nvn_vulkan_destroy_window_surface`；调用点 `1172/1185/1355`（OUT_OF_DATE / SURFACE_LOST / window generation 变化时的重建）。对比 `41-43` 的 acquire/present fence 等待有 1s/2s 超时，此处是全设备 idle，绕过超时保护。
- 触发：旋转/后台切回/分辨率变化叠加 GPU hang 或 device lost。
- 落地：改为按各 slot fence 逐个带超时等待，或加总超时 + device-lost 分支；重建路径不得无条件 waitIdle。

**P0-4 OP_WAIT_EVENT 忙等 + fatal + 未校验地址**
- 证据：`xbg_game.cpp:5525-5527`，`need(a.address && a.mode<=1)` 只校验非零；`reinterpret_cast<uint32_t*>(a.address)` 直接原子读；`deadline=now+fence_timeout_ns`（2s）；`need(now<deadline,"NVN event wait timeout")` 经 `468/467/2549` → abort。
- 触发：事件信号延迟提交、GPU 卡顿、`a.address` 指向已释放/未映射 guest 页。
- 后果：忙等占满渲染核，2s 后 abort；或立即 SIGSEGV。
- 落地：改带超时的条件变量等待；超时返回可恢复错误而非 fatal；访问前校验地址落在已注册 guest pool 区间。

**P0-5 `retire_slot` 的 fatal 与无界自旋**
- 证据：`xbg_game.cpp:1014` `VK(hybrid_wait_fence(s.fence, max(cfg.fence_timeout_ns, 10s)))`；`468` `#define VK(call) check(...)`；`467` `check` → `xbg_fatalf`；`2549` `xbg_fatalf` → `abort()`。`1008` 的 `vkWaitForFences` 返回 `VK_TIMEOUT`/`VK_ERROR_DEVICE_LOST` 都会走 fatal。`1052` `while(s.waiters.n.load()) std::this_thread::yield();` 无超时无退避。
- 触发：GPU hang / device lost / 某 waiter 线程被挂起或阻塞在长超时 wait。
- 后果：进程 abort，或所有提交/出图线程永久自旋（多数调用点持引擎 `recursive_mutex`）。
- 落地：超时/设备错误进入 slot 丢弃与队列恢复路径（`failed` 置位 + 资源回收），不 abort；waiters 自旋加 deadline + 退避，超时强制回收并标记 failed。

**P0-6 ANativeWindow 双锁生命周期竞态**
- 证据：`game_window_jni.cpp:114-124`（`g_window_lock` 内 set，`123` 在锁外 release 旧引用）、`130-139`（`g_window_lock` 内 `get()`+`clear()`，`138` 锁外 release）；`native_window.c:15` 定义的是**另一把** `g_native_window_use_lock`，`17-19 get()` 无锁，`35-42 use()` 持锁；消费方 `nvn_backend_vulkan.c:1159` 使用 `get()` 裸指针建/用 swapchain。
- 触发：旋转/切后台导致 `surfaceDestroyed` 与 guest present 并发；use 回调结束后 JNI 仍可能 release 同一引用。
- 后果：use-after-free native crash。
- 落地：渲染侧全部改走 `nso_host_native_window_use()` 并在回调内完成 window 用途；JNI `clearSurface` 先取得 runtime 侧 use_lock / 等待在途租约归零，再 `ANativeWindow_release`；两处 release 统一到单一租约机制。

---

## 5. P1 分级修复顺序（建议，未实施）

**第一梯队（崩溃/卡死，改动小、收益确定）**：P0 全 6 条 → `nso_host.c:4033/3609/3652/647/3357/456/2018` 的 abort 降级与判空 → `nvn_backend_vulkan.c:1230/1324/1490/1353` 的 VkResult 检查与 device-lost 处理 → `GameActivity.java:2115/2135` 的 `GUEST_START` finally 回滚。

**第二梯队（卡顿，需 A/B 与真机验证）**：`xbg_game.cpp:6127` 限帧移出锁 → `2501-2533` shader 冷加载移预热线程 → `49-52/1507-1535` 真实 dirty tracker（**严禁只改 marker 假启用**，HANDOFF:132-134 已明确 `nvn_dirty_since` 是恒 0 的 stub）→ `5406` 大拷贝分流 → `nso_host.c:1763` 文件 IO 锁粒度 → `xbg_texture.c:347` BC 线程默认值。

**第三梯队（UI/诊断路径）**：`GameActivity.java` 主线程扫描/金手指/导出移线程 → `DataBundleInstaller` 集合同步 → probe 与 guest 互斥 → JNI 失败路径 `ExceptionClear`。

## 6. P2 / 条件性风险

`nvn_command_replay.c:399-421` 锁外回放 UAF、`nvn_object_table.h:67-79` realloc 悬空指针与 generation 复用、`nvn_command_buffer.c:899/959/627/1113` 统计值锁外非原子写、`xbg_game.cpp:2348-2355` `count==1` 时 `w[i+1]` 越界读、`validate_capabilities` 对未知 capability 一律 fatal（新 shader bundle 出现即冷启动 abort）、`xbg_game.cpp:1204-1217` 大 pipeline cache 写盘、`GameActivity.java:2262` 退出轮询无超时、`guest_boot_jni.cpp:84-99` `dup2` 后未恢复原 stdout fd、`GameActivity.java:2021-2028` 每次 onStop 重复导出存档到同秒戳目录。

条件性提醒：`nvn_event.c:195` / `nvn_memory_pool.c:330` 的 weak 兜底 abort **仅在新增构建目标漏链强定义时可达**，建议加启动期自检而非运行期 abort。

## 7. 误报剔除与已核实防护

1. **剔除：weak 兜底 abort 两处。** `mtl_event_read` 有强定义 `src/graphics/nvn_vulkan/nvn_vk_adapter.c:200`；`nvn_command_buffer_retire_source_memory` 有强定义 `src/graphics/nvn_device/nvn_command_buffer.c:2455`。当前产品构建链接强定义，不走 weak 分支，不构成线上闪退路径。
2. **剔除：音频 FIFO 的 abort 群**（`audio_bridge.c:134/269/329/694`）。`:258` 单生产者 + `while(fifo_space<chunk) cond_wait`、`:320 peek(...,1024)` 上界与 `wrote<=ask`、`append_buffer:596` 的 `pending+released+busy<64` 记账已覆盖，属防御性断言。
3. **不剔除：nvnSyncWait 无限等待**（理由见 P0-2 交叉验证）。
4. **已修复项核实（源码开关层面，非本轮真机复测）**：`readback_cached`（`xbg_game.cpp:5827` + `guest_boot_jni.cpp:124` + `GameActivity.java:1319` 默认写标记）、pipeline warm list（`warm::enabled=true`）、`legacy_waitall` 无 marker 时 false、`completion_wait` Android/OHOS 缺省 true、`fence_spin_us=0`、async pool-reset 的 `r==VK_SUCCESS` 检查、`gfx_async` Android 硬编码 false。**仍默认关闭/失效**：`shader_reuse=0`（789/5803）、`vcache`（1507 `enabled=false` + weak stub）、`DM_BC_THREADS=0`（xbg_texture.c:347）、`sync_cache_save=false`。
5. **行号偏差修正**：Java 文件检索工具显示行号比磁盘真实行号小 26；本报告 Java 行号已按 `sed/awk` 实测修正（如 `startGuest` 在 2078 而非 2052，`GUEST_START.begin()` 在 2115，`finish` 在 2190）。

## 8. 验证缺口（只记录问题，不改代码 / 不加 Mock）

- 本轮**全部为静态只读分析**：无构建、无运行、无 `adb`、无真机、无 GPU capture。按 AGENTS-HONOR.md 未连接、未操作任何设备；设备侧验证一律 `NOT_RUN`。不得把本报告当作复现结论或验收 PASS。
- `tests/native/` 已有 `multiwait_contract.c`、`guest_process_contract.c`、`command_lifetime_contract.c`、`exit_registry_contract.c`、`audio_bridge_transport_contract.c`、`guest_pages_contract.c` 等契约测试；**未覆盖**：FinalizeEvent × WaitEvent 并发、fence 无限 timeout / device-lost、`retire_slot` fatal、`xbg_frame_pace` 持锁、ANativeWindow 双锁竞态、`GUEST_START` 异常回滚。
- 建议后续验证手段（本轮不实施）：两线程 Wait/Finalize 并发契约测试；device-lost 与 fence-timeout 故障注入；`gfx-*` marker 同场景 A/B 长稳真机录帧（冷/暖缓存分开、卡顿不删）；Java 状态机异常路径单测；窗口重建压测（反复旋转/切后台）。按项目规范，测试只记录问题，不得为通过测试而修改代码或 Mock。

## 9. 声明

- 未修改任何源码、未构建、未执行设备命令、未做 git 写操作、未触碰存档与 `.local/` 输入；仅新增本报告。
- 不声称"已修复"、不声称"稳定可玩"、不声称帧率达标；HANDOFF 已记录的 29.86 FPS 移动 / 神殿方块撤回结论均沿用，未被本报告改写。
- 本报告回答的是"可能性"：每条均给出可达路径与触发条件；是否在当前真机路径上实际触发，须由上述验证缺口中的实验确认。

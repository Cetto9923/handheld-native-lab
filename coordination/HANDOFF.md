# P-MM-17 闭环｜2026-10-24 23:30 Asia/Shanghai — **NVN 内存池家族（18 名）已落地，越过 `nvnMemoryPoolBuilderSetDefaults`**；guest 在真机上建成**第一个真实内存池**（`storage=0x5a5e960000 size=524288 flags=0xe2 -> true`）；新的第一致命前移到 **`nvnSamplerPoolInitialize`**（trap slot 23）

- **结论**：E3.3 达成，验收 A 与 B 都过。**guest 越过内存池家族**，第一次让 NVN 的存储路径真正跑起来：**新阻塞从「内存池不存在」换成「采样器池 / 纹理池 / 缓冲 / 命令缓冲这一整段不存在」**。**帧数仍为 0**（guest 还没记录过一条命令，**未达标题画面、不可玩**，不承诺第一章时间）。
  1. **实现形态（照 queue / window 两族的既有形态，不新造架构）**：新文件 `src/graphics/nvn_device/nvn_memory_pool.{h,c}`；**状态全在宿主**，按 guest 的 builder / pool 地址建键，**绝不写 guest 对象内存**（SDK 布局不可知）。18 个名字挂进 `nvn_device.c` 的 `nvnDeviceGetProcAddress`（紧挨 queue / window 注册），并在 `src/android/native/CMakeLists.txt` 与 `scripts/runtime/build_guest_process_contract.sh` 的同一批源文件里接入 ⇒ **契约与真机跑的是同一批代码**。
  2. **签名不是猜的，是取证来的（头文件 + guest 实参双向交叉）**：
     - 头文件 `src/graphics/nvn/include/nvn/nvn.h` L1196–1212 给出 15 个 `PFNNVNMEMORYPOOL*`；`nvnMemoryPoolBuilderGetDevice` 在 typedef 段**缺失**（名字在 `main_nvn_ids.txt`），按真实 NVN 约定补 `NVNdevice* nvnMemoryPoolBuilderGetDevice(const NVNmemoryPoolBuilder*)`。
     - `nvnMemoryPoolBuilderGetMemory` 在头文件里写成 **void 返回**（逆向头的笔误）⇒ 按 **`void*` 返回**实现（builder 存进去的 storage 指针原样返回），并在代码注释与本记录里标明这是**头文件笔误**。
     - **真机实参交叉验证**：`nvnMemoryPoolInitialize: pool=0x5219a26000 builder=0x5a39e80580 device=0x5639a03810 storage=0x5a5e960000 size=524288 flags=0xe2 -> true` ⇒ guest 确实以 **(ptr, size)** 两参设置 storage，`SetStorage(builder, void*, size_t)` 的签名被真机日志证实。
  3. **语义要点（禁止假成功，逐条落到代码）**：
     - `Initialize` 三种拒绝路径都**点名说明原因**：builder 未准备 / device 未初始化 / **没有 storage**（"this runtime does not allocate a pool behind the guest's back"）⇒ **不替 guest 偷偷 malloc 一份影子拷贝**（那会给 guest 一个与它写入地址不同的内存，比没有池更糟）。
     - `Map` 返回的就是 **guest 自己那块的 storage 基址**（guest 与宿主同地址空间），不是另一次映射。
     - `Flush/InvalidateMappedRange`：storage 是 guest 自己写的宿主内存，**本来就一致** ⇒ 不假装做了 cache 操作，日志写明为何是 no-op。
     - `GetBufferAddress`：**同一 pool 反复调用必须稳定**（guest 每次访问都把这个地址交回设备），由 pool 身份派生，两个 pool 不会撞。
     - `MapVirtual`：**返回 false + 点名日志**（本运行时不给 guest 造页表），不伪造地址。
     - `Finalize` 释放表项、重复调用安全；`Finalize` 后 `Map` 返 NULL。
  4. **契约与三变体自证（防假绿）**：新增 `--nvn-memory-pool`，覆盖 18 名字 resolve 且同名稳定、builder 往返（defaults/device/storage/size/flags）、**builder 与 pool 对象内存不被写入（0xAA 哨兵）**、无 device 时 Initialize→false、无 storage 时 Initialize→false、真设备下 Initialize→true 且 Map 非 NULL、GetSize/GetFlags 与 builder 一致、**GetBufferAddress 稳定**、MapVirtual 被拒、Finalize 后 Map 返 NULL、末尾 `CHECK(!errors)`。**期望值独立于实现常量**（`NVN_MEMORY_POOL_EXPECT_REAL_POOL` 与实现里的 flag 常量分离）。宿主门 **全过（EXIT=0，`nvn_memory_pool_errors:0`）**；**真机全绿**：`{"nvn_memory_pool_errors":0,"real_pool":true,"initialized":1,"refused":2,"builder_calls":23,"map_virtual_refused":1}`（真 Vulkan 设备下池建成、Map 生效）。
     - **三变体自证入 git**：`scripts/runtime/selfproof_nvn_memory_pool.sh` + `scripts/runtime/make_nvn_memory_pool_variants.py` ⇒ **A 红**（改期望保留末门，exit 1，6 项 check-fail）／**B 假绿**（同处改坏 + 去末门 + 错误数硬编码 0，**exit 0 但 stdout 谎报 `nvn_memory_pool_errors:0`**）／**C 红**（契约不动、池层谎报「无真设备也给池」，exit 1，5 项 check-fail）。
  5. **guest 真实推进（从真机 transcript 读出来，不是猜的）**：device → queue → **memory pool（本次达成）** → **sampler pool（新卡点）** → …。18 个内存池名字在真机日志里**全部 resolve 为 implemented**，但 guest **实际只调用了 `nvnMemoryPoolInitialize`**（其余是 resolve 未调用）⇒ **下一单要按 guest 真实调用顺序推进，不要按名字数量铺满**。
  6. **新卡点（第一致命，已如实记录）**：`[nvn] unimplemented NVN entry "nvnSamplerPoolInitialize" was called (trap slot 23, caller lr=0x721ba570ec)` ⇒ **NVN 陷阱仍是进程级终止**（`_exit(97)`），在 app 里会连 app 一起带走（crash buffer 空 ⇒ 干净退出，不是崩溃）。**进入可玩形态前必须改成只终止 guest 运行**。
  7. **本单的一个弯路（记录下来，别再走）**：先试图用**静态反汇编**找出每个 NVN 名字的 guest 调用点（`scripts/runtime/nvn_pool_callers.py`、`scripts/runtime/nvn_api_usage.py`）。结论是**guest 的调用点极难静态定位**：lookup 表在 `0x82d2000` 页，槽位读取用的基址寄存器**不是** lookup 用的那个 x8（实测是 x31 / 经 `add` 组合的寄存器），静态「槽位被读」判定会**全表误报为 never loaded**（533/533）。⇒ **以真机运行日志为权威**，静态脚本只保留作 lookup 槽位定位工具（能给出名字→槽位映射：`SetStorage`@0x82d29e8、`GetDevice`@0x82d29f8、`GetMemory`@0x82d2a00）。
- **本单未解决 / 风险**：**帧数仍为 0**；NVN 陷阱仍**进程级终止**；`nvnDeviceGetInteger` 的 **53/105** 依据**仍未从 guest 的兼容检查反推**（待复核，不得当已证事实）；`SetDepthMode` 已有状态语义但**还没有管线消费**；**shader 路径（NVN 二进制 → SPIR-V）仍未定案**，命令缓冲之后会卡在这里；`nn::hid` 输入族未动；`nn::vi` 之外仍有 373 个未绑定导入。
- **下一项可证伪实验（E3.4，按 guest 真实调用顺序）**：
  1. **采样器池家族**（`nvnSamplerPoolInitialize` 等）：guest 的**下一个真实调用点**，体量小，先补它把 guest 再往前推。
  2. **纹理池 / 缓冲家族**：`nvnTexturePoolInitialize` / `nvnBufferBuilder*` / `nvnBufferInitialize` 等，把存储真正接上设备资源。
  3. **命令缓冲家族（156 名，量最大）+ 记录/回放 + `nvnQueueSubmitCommands` / `nvnQueuePresentTexture`** ⇒ **第一个 guest 像素**（截图颜色必须可由 guest 的命令流解释，**不是宿主探针色**）。
  4. **shader 路径定案**（Maxwell/NVN 二进制 → SPIR-V），否则 E3.4 之后必然卡死在 shader。

# P-MM-16 进展｜2026-10-24 22:20 Asia/Shanghai — **guest 已经在持窗进程里画图的前置件齐了三块**：`nn::vi` 显示桥（把 app 的 `ANativeWindow` 交给 guest）、**NVN 队列家族**（真 Vulkan 队列）、**NVN 窗口家族**（真 surface + 真 swapchain）；下一个卡点前移到 **`nvnMemoryPoolBuilderSetDefaults`**

- **结论**：E1 之后按 guest 的真实依赖顺序补图形链，本轮补完三块，并**第一次拿到「guest 拿到真 Vulkan 队列」的真机证据**。**仍然没有游戏帧**（帧数 = 0）：guest 还没记录过一条命令。卡点从「队列」前移到「**内存池**」。
  1. **队列家族（真机已验证）**：`nvnQueueInitialize` 在 app 进程内返回 true，且**拿到真 Vulkan 队列**：`platform_queue=0xb4000073aef93ad0`。guest 的 queue flags 是 `0x291fd708`（非平凡值 ⇒ 我们的参数映射是对的；也因此**不能**按未知位去猜 flag 语义，后端改成「只提供它真有的那条图形队列，flags 由队列层如实记录」）。
  2. **窗口家族（真 surface + 真 swapchain）**：`nvnWindowBuilderSetNativeWindow` 就是 guest 把窗口交给 NVN 的缝。**这里修掉一个真 bug**：NVN 后端的实例**没有启用 `VK_KHR_surface` / `VK_KHR_android_surface`** ⇒ 根本没有 `vkCreateAndroidSurfaceKHR` ⇒ 没有窗口也没有帧（P-MM-14 的探针已经踩过同一个坑，当时只修了探针没修运行时）。另外窗口尺寸**取自 surface capabilities**，不去调 `ANativeWindow_getWidth` ⇒ 图形层**不依赖 libandroid.so**（契约的 Android 链接就是被这个符号卡住的）。
  3. **显示桥（nn::vi 5 导入）**：guest 拿到 app 发布的那个 `ANativeWindow`；`GetNativeWindow` 在没有已发布窗口时**如实写 NULL 并说明**，不自己造窗口；vsync 事件用真的 `nn::os` 事件，**初始不置位**（还没有任何一帧被呈现过，置位就是撒谎），等真 present 之后再置。
  4. **两个契约抓出来的真缺陷（不是读代码看出来的）**：
     - **句柄校验会崩宿主**：worker 原来用「读指针里的 magic 值」校验 display/layer 句柄，guest 一旦回传别的地址，宿主就**解引用野指针崩掉**（契约里用假句柄复现）。现在改成**与已发放句柄表比对**，不解引用任何来路不明的地址。
     - **统计读序错误**：队列契约在 `nvnQueueFinalize` **之后**才读「有没有平台队列」，而 finalize 会把队列连同平台队列一起释放 ⇒ 真机读到 0 而实际有队列。是**真机那轮**暴露的。
  5. **契约与自证**：新增 `--nvn-queue` / `--nvn-window` / `--vi-display` 三个用例，宿主门 **202 例全过（EXIT=0）**；真机三个用例全绿（`nvn_queue real_queue=true initialized=1 refused=1`、`nvn_window real_window=true initialized=1 refused=1`、`vi_display errors=0`）。**队列契约三变体自证**（`scripts/runtime/selfproof_nvn_queue.sh`，这次**入 git**）：A（改期望，保留末门）⇒ **红**；B（同处改坏 + 去末门 + 错误数硬编码 0）⇒ **假绿**（stdout 谎报 `nvn_queue_errors:0`，stderr 里有 3 条 check-fail）；C（契约不动，队列层谎报「无真设备也给队列」）⇒ **红**。
  6. **guest 真实推进顺序（从 transcript 的解析顺序读出来的，不是猜的）**：device → queue → **memory pool** →（纹理/缓冲）→ window（用 nn::vi 给它的原生窗口）→ command buffer → 提交/呈现。⇒ **下一块是内存池家族（18 个名字）**，然后是命令缓冲家族（156 个名字，量最大）。
- **本单未解决 / 风险**：**帧数仍为 0**；NVN 陷阱仍是**进程级终止**，在 app 里会连 app 一起带走（crash buffer 为空，是干净退出）；`nvnDeviceGetInteger` 的 **53/105** 依据**仍未从 guest 的兼容检查反推**（待复核，不得当已证事实）；`SetDepthMode` 已有状态语义但**还没有管线消费**；shader 路径（NVN 二进制 → SPIR-V）**仍未定案**，E3 之后会卡在这里；`nn::hid` 输入族未动。
# P-MM-15 闭环｜2026-10-24 21:10 Asia/Shanghai — **E1 达成：guest 跑进了持窗进程**（`app_pid=7933` == `guest_pid=7933`），**app 进程内建成真 Vulkan 设备**（`nvnDeviceInitialize -> true`）；下一个卡点由进程边界换成 **NVN 绘制链本身**：`nvnQueueBuilderSetDefaults`（trap slot 11）

- **结论**：P-MM-14 开题时定下的 E1（架构使能，最小可证伪）**已通过**，且通过的方式是可证伪的：**guest 与窗口在同一个 PID 里**，**真 Vulkan 设备在 app 进程内建成**，**romfs 在 app 进程内挂载成功**。**仍然没有游戏帧**：guest 紧接着就在第一个未实现的 API 上**被点名停下**——`nvnQueueBuilderSetDefaults`（trap slot 11）。**第一个阻塞换了**：不再是「guest 在窗口之外的进程」，而是「**NVN 的队列/命令缓冲/窗口/呈现这段根本不存在**」。
  1. **做法（移动代码，不改行为）**：把自有 boot 序列从 CLI 里**抽成独立翻译单元** `src/runtime/nso/nso_boot.c` / `.h`（`nso_owned_profile()` + `nso_boot_run()`），CLI 退化成薄封装；Android 侧新增静态库 `ffrs_guest_runtime`，**编译的就是契约脚本同一批源文件**（loader + typed HLE + native boundary + NVN device 层），再由 `GameActivity` 的 JNI `nativeStartGuest()` 在**独立线程**上起 guest（**绝不在 UI 线程**，上游 d2r/nxnative 的同层约束）。guest 的 stdout/stderr 被重定向到 app 自己的外部目录（`guest_boot.log`），避免几万行把 logcat 冲掉。
  2. **真机证据（RG477M / Android 14 / Mali-G615 MC6）**：
     - `FFRS-GuestBoot: guest_boot start app_pid=7933 guest_tid=7975 image=/data/local/tmp/ffrs-.../main.nso romfs=/data/local/tmp/ffrs-.../romfs quota=1073741824 stack=2097152 resources=16777216`
     - `pidof org.ffrs.gamewindow` → **`7933`** ⇒ **guest 与窗口同进程**（这是 P-MM-14 那条「`ANativeWindow` 是进程内句柄」推论的直接落地）
     - `[profile] ... build=736837afd390442ed7c74a7de730e71b imports=651 unresolved=373` ⇒ **与 CLI 跑的是同一个 guest 镜像、同一套导入账**（CLI 侧数字完全一致）
     - `[hle] nn::fs::MountRom: mounted 'rom' ... host_path='/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/romfs'` ⇒ **app 进程能读游戏数据**（做法：给游戏目录与 `main.nso` 放开读权限；**没有复制 8.5G romfs**）
     - `[nvn] nvnDeviceInitialize: device=0x5639a03810 builder=0x5a39e80630 flags=0x100 -> true` ⇒ **真 Vulkan 设备在 app 进程内建成**（这一位在 CLI 里是 P-MM-13 才跨过去的）
     - 末尾：`[nvn] unimplemented NVN entry "nvnQueueBuilderSetDefaults" was called (trap slot 11, caller lr=0x72158aaff0)` ⇒ **仍然诚实点名、仍然停下**
     - transcript：`.local/logs/p-mm-15-e1-guest-transcript.log`（59559 行）；装机运行：`.local/logs/p-mm-15-e1-install-run.log`；APK sha256 `261d04a368f3ea9d0440f4b2428b8e69636f4a3ef61005bce3191ef1a93e4454`
  3. **比 CLI 多走了一步的原因**：`SetDepthMode` 等设备家族语义在 P-MM-13 设备家族那笔（`89728b6`）已实现，所以 CLI 的 v65 停在 `nvnDeviceSetDepthMode`（trap slot 8），而这次停在**下一个**未实现 API（slot 11）。⇒ **卡点列表是活的**，不能沿用旧日志里的名字。
  4. **构建侧真问题（运行期抓出来的，不是猜的）**：`.so` 链接时 `native_boundary.S` 用绝对寻址引用 `native_boundary.c` 的全局 `s_slots`，PIE 可行但**共享库不行**（`R_AARCH64_ADD_ABS_LO12_NC cannot be used against symbol 's_slots'`）。修法是**编译选项**（`-fvisibility=hidden` 让链接器本地绑定），**没有改冻结的 boundary 源码**；同时补上契约构建同款的 `-ffixed-x18 -fno-emulated-tls`（**x18 是 guest ABI 的平台寄存器，编译器不许碰**）。
  5. **契约门**：宿主门 **193 例全过（EXIT=0）**。顺带修了一个**门本身的脆弱点**：单个 `-O2` 编译契约 TU 在本机负载下会超过 30 秒挂死保护（实测 30.2s 被 SIGKILL，单独跑 15.2s），把**挂死保护**从 30s 放宽到 300s —— 这是超时阈值，**不是任何断言**。
- **已知后果（记录，不掩盖）**：NVN 陷阱目前是**进程级终止**（`_exit(97)`），所以在 app 里它**会连 app 一起带走**（本轮 app 进程确实因此消失，crash buffer 为空 ⇒ 是干净退出，不是崩溃）。这不影响 E1 的结论，但在「app 要长期开着跑游戏」的形态下必须改成**只终止 guest 运行**。当前开发迭代可接受（每轮重装重起），进入可玩形态前必须处理。
- **下一项可证伪实验（E2 → E3，按 guest 依赖顺序）**：
  1. **E2（显示桥）**：绑定 `nn::vi` 的 5 个导入（**228–232**），其中 `GetNativeWindow` 就是把 app 的 `ANativeWindow` 交给 guest 的接缝（`src/runtime/hle/native_window.c` 已有持有者，`nso_host.c` 侧尚未接进导入路径）。**判定**：guest 能拿到**同一个** `ANativeWindow` 指针（`0xb4000073eef8b330`）而不是自造一个。
  2. **E3（最小 NVN 集，不铺满 534）**：按 guest 的实际调用顺序补 `nvnQueueBuilder*` / `nvnQueueInitialize` → `nvnCommandBuffer*` → `nvnWindowBuilder*` / `nvnWindowInitialize` → swapchain/present 家族；**未实现的一律继续「点名终止」**，禁止万能成功桩、禁止绕过 abort。**判定**：guest 自己的清屏经记录/回放上屏，**截图颜色必须可由 guest 的命令流解释**（不是宿主探针色）。
  3. **E4（shader，后置但要早定）**：guest shader 是 NVN 二进制（IoStore 容器内），Vulkan 要 SPIR-V ⇒ **Maxwell → SPIR-V（离线翻译器）→ 目标语言** 这条链要先定，否则 E3 之后会卡死在 shader。
- **本单未解决 / 风险**：`nvnDeviceGetInteger` 的 **53/105** 依据仍只是「vendored 头文件描述的规范版本」，**没有从 guest 的兼容检查反推** ⇒ **待复核**，不得当作已证事实；`SetDepthMode` 已有状态语义但**还没有管线消费它**，E3 的管线存在时必须真正生效；`nn::vi` 仍未绑定；输入族未动；**当前游戏帧数 = 0**，**不承诺第一章完成时间**。
# P-MM-14 开题｜2026-10-24 20:40 Asia/Shanghai — 方向调整：**目标从「再过一个符号」改成「接通真实绘制链」**；先核对接线（**窗口与 guest 不在同一进程**），再证明 **RG477M 的呈现路径可用**（宿主探针，**不是游戏帧**）

- **结论**：本单把成果口径改了：**不再以「又过一个符号 / unresolved 又少一个 / 契约 PASS」为主成果**，主成果改成 **真实 guest 调用 → NVN 状态与命令 → Vulkan 提交与完成同步 → 窗口呈现**。本轮做了三件事：① **核对运行时接线**（结论见下，**推翻了一个默认假设**）；② **证明 RG477M 上这条窗口的呈现路径可用**（宿主探针，**明确标注为宿主探针成功，不是游戏帧**）；③ **记录上游参考**（`docs/analysis/nvn_graphics_chain_references.md`）。
  1. **接线核对（第一手证据）**：`org.ffrs.gamewindow`（持 SurfaceView 窗口的 Activity 进程）**PID 21208**；用 `adb shell` 启动的 guest 是**另一个 PID 11601**（user `shell`，PPID 1）。⇒ **`ANativeWindow` 是进程内句柄，静态指针不可能跨进程**；guest 必须**跑在持有窗口的那个进程里**。这与两个上游参考的做法一致（d2r-mobile 的 `dm/android/jni_android.c`：`System.loadLibrary` 后 JNI 调 `dm_main`，guest 跑在 app 进程的主机线程上；`nativeSurface` 把窗口交给同一进程的 Vulkan 层）。
  2. **宿主探针（真机，已通过）**：`src/android/native/game_vulkan_probe.cpp` 在 **Activity 进程内**、用**该 Activity 自己的 `ANativeWindow`** 跑完整呈现链：`vkCreateInstance` → `vkCreateAndroidSurfaceKHR` → 选有 graphics+present 的队列族 → `vkCreateDevice` → `vkCreateSwapchainKHR`(1280×960, FIFO, 10 张) → `vkAcquireNextImageKHR` → 清屏 → `vkQueueSubmit` → **`vkWaitForFences`（真 fence）** → `vkQueuePresentKHR`，共 3 帧，**全部 VkResult 0**。**设备截图整屏像素 = (16, 200, 96)，与探针的清屏色精确一致**。**这是「宿主探针成功」，不是游戏帧**：颜色是探针自己的，**没有任何一个像素来自 guest**。日志与截图：`.local/logs/p-mm-14-probe-screen2.png`、`.local/logs/p-mm-14-apk-build.log`、`.local/logs/p-mm-14-apk-install.log`。
  3. **顺带钉死的格式事实**：该 surface **只提供一种格式 `R8G8B8A8_UNORM`(37)**；强行用 `B8G8R8A8_UNORM`(44) 会让**屏幕上的红蓝互换**（显示路径按 RGBA 读缓冲）。⇒ 后面 guest 的渲染目标格式**必须**跟着 surface 的首选格式走，不能想当然选 BGRA。
  4. **过程中修掉的两个真 bug（都是运行期证据抓出来的，不是猜的）**：① 实例**没启用 `VK_KHR_surface` / `VK_KHR_android_surface`** ⇒ `vkCreateAndroidSurfaceKHR` 取不到（`vkGetInstanceProcAddr` 对未启用的扩展返 NULL）；② 我封的 `GetInstanceProc()` **忽略了 name 参数**，永远返回 `vkGetInstanceProcAddr` 自己，于是把 queue family 索引当成函数名去 `strcmp` ⇒ **真机 SIGSEGV**（crash 栈在 `libvulkan.so!GetInstanceProcAddr` ← `ffrs_vulkan_present_probe`）。另外 d8 8.2.2 在**匿名内部类**上报内部 NPE，改成**具名静态嵌套类**才过。
  5. **组件盘点：哪些是真接线的、哪些只是独立探针**（避免重复造轮子）：**启动路径里只有** `src/runtime/nso/nso_loader.c` + `src/runtime/hle/*.c` + `src/graphics/nvn_device/`（本项目新建）；**`src/graphics/nvn/`（旧 53.105 Darksiders ABI 的 dispatch）只被 app 的 CMake 与它自己的测试引用，ABI 已过期且其 init 桩谎报成功**；**`src/graphics/nvn_las/`（记录格式 `command_record.h` + typed transfer）只被 `scripts/graphics/build_las_transfer_contract.sh` 引用**；**`src/graphics/vulkan/buffer_resource.c` 只被探针/契约引用**；**`src/graphics/vulkan_probe/` 是独立探针**。⇒ 可直接复用的是：窗口持有者 `src/runtime/hle/native_window.c`、记录格式 `command_record.h`、buffer 组件、以及已在真机跑通的 Vulkan 设备/队列创建。
  6. **上游参考（已记 URL / revision / 许可证 / 可获取状态）**：**d2r-mobile** `a8782caa28d7b49d1b341e34de488d3168acb86c`（MIT）——同构问题的现成实现，`dm/gfx/vulkan/` 里有**真的 NVN→Vulkan 回放后端**，`INTEGRATION.md` 记录了 `OP_SIGNAL_EVENT` 必须先完成既有 GPU 工作再发布事件值、栅栏用真 `VkFence`、present 用真渲染图像且回调在 blit 与队列完成后才成功、**不得在 UI 线程调 Vulkan**、GPU 耗时/上屏时间未测就写 -1/0 **不许用 CPU 时刻冒充**；**nxnative** `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3`（MIT）——通用化版本，其**app shell 模型**正是我们要的（guest 与图形后端同进程同地址空间）。**不能直接搬的**：Metal 后端、Metal shader 库、零拷贝统一内存假设、以及任一仓库的游戏专用布局/shader 事实。
  7. **第一个阻塞（一句话）**：**guest 还在窗口之外的进程里，而且 NVN 的「队列/窗口/命令缓冲 + 记录回放」这段根本还不存在**。所以现在不是「差几个符号」，而是**差一整条链**。
- **下一项可证伪实验（按小阶段推进，每阶段只报新增证据与下一卡点）**：
  1. **E1（架构使能，最小可证伪）**：把 guest 运行时做成**可被 app 加载的库**，由 Activity 的 JNI 在**同一进程**内起 guest 线程。**证伪点**：日志里 guest 的 PID 必须**等于** app 的 PID，且 guest 能跑到 NVN 初始化（真 Vulkan 设备建成）。若这一步不成立（例如运行时依赖 CLI 形态），立刻可见。
  2. **E2（guest 像素，不是探针）**：绑定 `nn::vi` 5 个导入（`GetNativeWindow` 就是把 app 的 `ANativeWindow` 交给 guest 的接缝），guest 用 **NVN 自己**建窗口并提交**它自己的**清屏；我们按 `nvn_las/command_record.h` 的记录格式**记录并回放**成 Vulkan 命令 → 截图颜色必须是**guest 给的颜色**。**判定**：颜色可由 guest 的命令流解释 ⇒ 首次「像素来自 guest」成立；否则仍是宿主探针。
  3. **E3（最小 NVN 集，按首次绘制依赖补，不铺满 534）**：`nvnQueueBuilder*` + `nvnQueueInitialize` + `nvnCommandBuffer*` + `nvnWindowBuilder*` + `nvnWindowInitialize` + swapchain/present 家族；**未实现的一律继续「点名终止」**，禁止万能成功桩、禁止绕过 abort。
  4. **E4（shader 路径，后置但要早定）**：guest 的 shader 是 NVN 二进制（在 IoStore 容器里），Vulkan 要 SPIR-V；上游的路径是 **Maxwell → SPIR-V（Ryujinx 翻译器，离线）→ 目标语言**。这条要先定清楚，否则 E2 之后会卡在 shader 上。
- **本单未解决 / 风险**：`nvnDeviceGetInteger` 报的 **53/105** 依据仍只是「vendored 53.105 头文件所描述的规范版本」，**没有从 guest 的兼容检查里反推**（guest 用它的 selector 0/1 查主次版本，但查完就丢，没有比较）——**待复核**，不得当作已证事实；`SetDepthMode` 现在**有状态**（记录 guest 设的值并回读一致），但**还没有任何管线消费它**，等 E3 的管线存在时必须真正生效；`nn::vi` 仍未绑定；输入族未动；**当前游戏帧数 = 0**。
# P-MM-13 闭环｜2026-10-24 20:14 Asia/Shanghai — **真的有 NVN 设备了**：RG477M 上用真 Vulkan 设备（Mali-G615 MC6）把 guest 的 NVN 初始化**做成功**，v62/v63 那道「`nvnDeviceInitialize failed` 致命退出」的墙**被跨过去了**；未实现的 NVN API 不再沉默，而是**逐个点名**并停下

- **结论**：工单 P-MM-13 目标闭环。`src/graphics/nvn_device/` 这一层**真的建出了一个 Vulkan 设备**（RG477M 真机：`Mali-G615 MC6`，instance API 1.3，driver 184553472，graphics queue family 0），`nvnDeviceInitialize` **返回 true**，guest **不再**触发 `NVNManager.cpp:1246` 的 `LowLevelFatalError`。guest 接着把 **534 个 NVN 名字全部按名解析完**（与 P-MM-11 静态清单 **534** 精确吻合），然后在**第一个尚未实现的 API** 上被我们**诚实地停下**并**点名**。**未达标题画面 / 可玩**：目前停在 `nvnDeviceSetDepthMode`，离第一帧还差「NVN 队列/窗口/绘制」一整段 + `nn::vi` 显示桥。
  1. **设备是真的，不是桩**：`nvnBackend_vulkan.c` 在 Android 侧走 **Vulkan 1.0**（`vkCreateInstance` → 选带 graphics queue 的物理设备 → `vkCreateDevice` → `vkGetDeviceQueue`），链接 NDK 的 `libvulkan.so` stub、运行时落到设备的 `/system/lib64/libvulkan.so`。**宿主 macOS 侧没有 Vulkan** ⇒ 用 `nvn_backend_none.c`（诚实报失败），**同一套分发/builder/时间戳代码两边共用**，只有后端不同。`nvnDeviceInitialize` **只在真设备建成时返回 true**——这正是 v62 证明 guest 会致命处理的那一位。
  2. **两级分发照 guest 的真实形态做**（`docs/analysis/nvn_device_requirements.md` 第 1 节）：`nvnBootstrapLoader` **只答 2 个名字**（`nvnDeviceInitialize` / `nvnDeviceGetProcAddress`）；其余 **534** 个由 device 级 `GetProcAddress` 按名解析。契约里有一条**更强**的断言：bootstrap 对第三个名字（`nvnDeviceFinalize`）必须返 NULL，否则说明两级被合并成了一级。
  3. **未实现的 API 不再是「假成功」或 NULL，而是**逐个点名的陷阱。guest 从不检查 `GetProcAddress` 的返回值（`0x1430e10` 直接 `str`），返 NULL 会跳 `pc=0`；所以每个未实现的名字解析到**一段专属 trampoline**（`.text` 里 640 槽、每槽 8 字节、索引内嵌在指令流里，**不是**运行期可写内存、**不**需要 W^X 技巧、宿主与设备同一份代码），调用它就打印**是哪个 API**、打印调用点 LR，然后 `_exit(97)`。v64 因此精确停在 `nvnDeviceGetInteger`，v65 精确停在 `nvnDeviceSetDepthMode`。
  4. **纠正了一处我们自己的错误归属（重要）**：先前记录说全局 `0x82d2740` 存的是「时间戳函数」、签名 3 参（device, clock, out）。**真机跑起来后证明这是错的**：`0x82d2740` 存的是 **`nvnDeviceGetInteger`**（device 级解析器 `0x142dbe0` 的第 8 个，步长 8 ⇒ `0x708 + 7*8 = 0x740`），签名 **3 参 `(device, info, int* out)`**，guest 用 selector **0/1** 查的正是 **NVN API 主/次版本号**；时间戳在 `0x82d2748`，签名是 **1 参 `(device)`、结果在 x0**（调用点 `0x1444b18/0x1444b40/0x1444b68/0x1444b90`）。**如果照旧签名实现，时间戳那一版会拿 guest 传的野指针去写**——这次是运行期证据把它抓出来的，已按 guest 调用点改正。**教训**：`0x82d2740` 这类归属**必须**由 guest 调用点反推，不能凭名字相似推断。
  5. **builder 是我们自己的布局**（guest 从不读写它的字段，只把地址交给三个函数，且**紧接着就复用那块栈**）：`nvnDeviceBuilderSetDefaults` / `SetFlags` / `GetFlags` / `nvnDeviceInitialize` 互相自洽，**不还原任天堂布局**。真机证据：guest 设的 flags = **`0x100`**（v64 日志 `flags=0x100`），我们的 `SetFlags` 收到并**在初始化时消费掉**。契约里有一条**会踩 guest 保存寄存器**的守卫：builder 写入**不得越过 64 字节**（用 0xAA 填充的后哨字节验证）。
  6. **契约 `--nvn-device`**（新增，宿主 + 真机都跑）：bootstrap 只答 2 个名字 / device 级解析答 guest 真实解析的 16 个名字且**同名稳定** / 命名空间外与 NULL 必须被拒 / builder 往返 + **64 字节边界** / **API 版本号写死 53/105**（期望值**独立于实现常量**）/ 时间戳非零且单调 / **设备初始化的结果必须与「设备是否真的存在」一致** / **未实现 API 的陷阱必须在子进程里以 97 退出并点名**。
  7. **三变体自证**（`.local/logs/p-mm-13-contract-selfproof.log`）：A（把期望改成「本构建必须有真设备」、保留末尾 `CHECK(!errors)`）⇒ **exit 1（红）**；B（同一处改坏 + 去掉 `CHECK(!errors)` + 错误数**硬编码 0**）⇒ **exit 0（假绿）**，stdout 谎报 `nvn_device_errors:0` 而 `[check-fail] nvn-device-real-device-expected` 暴露了失败被掩盖；C（**契约完全不改**，让**后端谎报设备存在**）⇒ **exit 1（红）**，两条独立检查同时失败（`real-device-expected` 与 `description-matches-device`）。C 正是「后端永远不许宣称不存在的设备」的守门。
  8. **真机证据**：
     - 真机契约：`{"nvn_device_errors":0,"real_device":true,"trapped":true,"named":true,"resolved":28,"implemented":14,"traps":12}`，stderr 有 `[nvn] vulkan: created Vulkan device "Mali-G615 MC6", instance API 1.3, driver 184553472, graphics queue family 0`。
     - boot **v64**（`.local/logs/rg477m-boot-v64-nvn-device.log`，exit **97**，58775 行）：真设备建成 → `nvnDeviceInitialize ... flags=0x100 -> true` → 解析 534 个名字 → 停在 `nvnDeviceGetInteger`。
     - boot **v65**（`.local/logs/rg477m-boot-v65-nvn-device-getinteger.log`，exit **97**，59977 行）：越过 `GetInteger` 后停在 `nvnDeviceSetDepthMode`。
     - 两次 boot **都 `grep -c '\[fault\]'` = 0**：guest **没有崩溃**，是我们**主动**停下。`imports=651` / `unresolved=373` / boundary `imports=275` **全部不变**——本单**不绑任何导入**。
  9. **并行副线 P-MM-13A（Android 游戏窗口形态）已交付**（worker，commit `a1f2013ec8aa4ff62a86bb6bda56c4336212a15e`，**未 push**）：app 侧新增带 **SurfaceView** 的游戏 Activity，把 **`ANativeWindow*`** 交到 native（`ANativeWindow_fromSurface`，取法依据与真机日志在 `docs/analysis/android_game_window_form.md`）；native 侧新增**可移植**的窗口持有者 `src/runtime/hle/native_window.c`（**不** include `android/native_window.h`，宿主与 Android 都能编）。真机：`window_state event=set_surface present=1 pointer=0xb4000073eef8b330 width=1280 height=960 format=1`，退后台 `event=clear_surface` 真的释放了引用。**窗口是空的**：无渲染、无输入、无帧。
- **新的前沿一句话**：**「没有 NVN 设备」这道墙已经跨过去了**；现在的前沿是「**NVN API 覆盖面**」——guest 已经把 534 个名字全部解析在手，每个还没实现的 API 都会被陷阱**点名**，按这个清单逐个补即可（下一个是 `nvnDeviceSetDepthMode`，之后是 queue/window/draw 家族）。**第一帧还需要**：① NVN 队列与命令缓冲家族；② `nn::vi` 显示桥 5 个导入（`GetNativeWindow` 就是把 P-MM-13A 交出来的 `ANativeWindow*` 接到 Vulkan surface 的那一步）；③ `nn::hid` 输入族。
- **产物与证据**：`.local/build-p-mm-13/`（`guest_process_contract.android` sha256 `88300f837684d858…`、`nso_owned_startup.android` sha256 `c922ca0cbea8ef9e…`）；真机日志 `rg477m-boot-v64-nvn-device.log(.exitcode)`、`rg477m-boot-v65-nvn-device-getinteger.log(.exitcode)`；契约日志 `p-mm-13-contract-device.stdout/.stderr`；自证日志 `p-mm-13-contract-selfproof.log`；设备侧文件 `p-mm-13-guest_process_contract.android-88300f837684d858`、`p-mm-13-nso_owned_startup.android-c922ca0cbea8ef9e`。
- **未完成 / 风险**：标题画面与可玩**均未达**；仍有 **373** 个导入未绑定（全模块 `imports=651`），但**最关键的 353 已绑完且现在背后有真设备**；NVN API **只实现了 9 个**（device 家族 8 + 时间戳 1），其余 525 个是**会点名并停下**的陷阱，不是假成功；`nvnDeviceGetInteger` 报的 API 版本 **53/105** 取自随仓库 vendored 的 53.105 头文件所描述的规范版本（**不是**声称 53.105 全部实现——未实现项一律点名停下）；**显示桥仍未接线**（`nn::vi` 5 个导入 + 把 P-MM-13A 的窗口接进 Vulkan surface）。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。

# P-MM-12 闭环｜2026-10-24 20:15 Asia/Shanghai — guest 的**致命错误就地终止**（`nn::diag::detail::AbortImpl` 不再返回），真机 boot v63 从 SIGILL(132) 变成干净的 abort(134)，日志**最后一行就是游戏自己的报错**

- **结论**：工单 P-MM-12 目标闭环。guest 的致命错误现在**在游戏自己宣告的那里**结束运行，不再掉进 guest 的 `udf #0` 陷阱。宿主与 RG477M 真机 `--diag-abort` 契约 PASS；真机 v63 **exit 134**、**日志里一条 `[fault]` 都没有**，最后一行原样是游戏自己的消息。**导入计数不变**（`imports=651`、仍 **373** 未绑定）——**本单不绑任何导入**，game 停下的位置也没变（还是"没有 NVN 设备"），变的是**原因终于成了日志的最后一行**，而不是一条莫名其妙的非法指令。**未达标题画面 / 可玩**。
  1. **v62 暴露出的真实问题**：guest 在 `0x14355c0` 调 `nn::diag::detail::AbortImpl`（import 12），而**运行时桩只是打印后返回**；guest 并不指望这次调用返回——它紧接着就执行 `0x11f8` 的 `udf #0`（`0x11f4` 调完导入紧跟 `udf`）。⇒ 之前每次"游戏自己判定失败"的结局，最终都表现为**一条 SIGILL 陷阱**，真正的原因（`LowLevelFatalError`）要往回翻日志才看得到。**游戏原话在日志里，但结局是假的**。
  2. **改法（最小、诚实、与既有模型一致）**：桩在打印完诊断与游戏消息后，**以真实 `abort()` 的退出码 134 结束运行**（`128 + SIGABRT`），形态与运行时处理 guest 故障时完全一致：**报告 → flush → 离开**（`[fault]` 处理器同样是 `_exit(128+sig)`）。**没有**引入 longjmp／setjmp 改造，**没有**跳过 guest 栈或 TLS 的既有语义，也**没有**改变任何游戏行为或导入计数。
  3. **消息必须是游戏自己的**：SDK 把消息指针放在 `x22`（UTF-16），桩照旧解码，并把它带进最终报告行——`[guest-abort] nn::diag::detail::AbortImpl: the guest aborted: <游戏原话>`。契约**不**接受宿主自造的措辞。
  4. **契约 `--diag-abort`**：改写**既有**的 `diag_abort_contract`（旧版写着「AbortImpl 调用返回 0」，那正是旧政策），保留 `GetBacktrace` 的原有检查，把 abort 检查换成**子进程**执行——因为"这次调用不返回"**在进程内无法观测**。子进程把 stderr 重定向到 `tmpfile()`，父进程 `waitpid` 后断言：**子进程以 134 退出**（而不是返回 0）、**游戏消息出现在 stderr**、**有 `[guest-abort]` 报告行**。消息按 SDK 方式用 `x22` 传递（`__asm__` 里 `mov x22` + `blr` 放在**同一个块**里，保证中间没人能改 x22），所以**报告链路**也被验到，不只是退出码。
  5. **三变体自证**：A（把期望从"子进程以 134 退出"改成"以 0 退出"、保留末尾 `CHECK(!errors)`）⇒ **exit 1（红）**，`[check-fail] diag-abort-does-not-return`；B（同一处改坏、去掉末尾 `CHECK(!errors)` 且报告错误数**硬编码 0**）⇒ **exit 0（假绿）**，stdout 谎报 `diag_abort_errors:0` 而 `aborted:false` 暴露了失败被掩盖；C（**契约完全不改**，让桩**返回而不结束运行**）⇒ **exit 1（红）**——这条正是"guest 致命错误不得回交控制权"的守门。日志 `.local/logs/p-mm-12-contract-selfproof.log`。
  6. **真机 v63 的结果**：exit **134**；`grep -c '\[fault\]'` = **0**；日志最后一行即
     ```
     [guest-abort] nn::diag::detail::AbortImpl: the guest aborted: LowLevelFatalError [File:.\../Platforms/Switch/Source/Runtime/NVNRHI/Private/NVNManager.cpp] [Line: 1246]
     nvnDeviceInitialize failed
     ```
     ⇒ 现在的结局**就是**游戏宣告的那句话。`unresolved` 仍是 **373**、`boundary` 仍是 **275**（本单不绑导入，计数不该变，实测确实没变）。
- **新阻塞一句话**：**没有变**——挡住标题画面的仍然"是这台运行时没有 NVN 设备"（游戏在 NVN 初始化失败时致命退出），那是 **T04 的 NVN 设备**（规格见 `docs/analysis/nvn_device_requirements.md`：1 个 bootstrap 只答 2 个名字 + device 级 `GetProcAddress` 答 534 个名字；**builder 布局不是阻塞项**）+ **`nn::vi` 显示桥**（5 个导入，`GetNativeWindow` 就是 Android 窗口的接缝，规格见 `docs/analysis/display_bridge_requirements.md`）。P-MM-12 只是让"没有设备"这件事**说得清楚**。
- **产物与证据**：
  - 构建目录 `.local/build-p-mm-12/`（`guest_process_contract.android` sha256 `c900e45e…`、`nso_owned_startup.android` sha256 `4ffad55d…`）；manifest 已写入 `android_executed` / `device_contracts` / `device_boot`。
  - 真机日志 `.local/logs/rg477m-boot-v63-terminal-abort.log`（**59376** 行，exit 134）+ `.exitcode`；契约日志 `.local/logs/rg477m-contract-p-mm-12-diag-abort.stdout/.stderr`；自证日志 `.local/logs/p-mm-12-contract-selfproof.log`。
  - 设备侧文件：`p-mm-12-nso_owned_startup.android-4ffad55d`、`p-mm-12-guest_process_contract.android-c900e45e`。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **373** 个导入未绑定（`imports=651`）。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。

# P-MM-11 后续侦察｜2026-10-24 19:55 Asia/Shanghai — 把"没有 NVN 设备"这道墙拆成**可施工的规格**：NVN 设备实现需求 + 显示桥实现需求 + 534 个 API 的确切清单（**只读侦察，不改运行时代码**）

- **结论**：P-MM-11 证明了"挡住标题画面的是没有 NVN 设备"，但**没有回答"要做出这个设备需要什么"**。本轮侦察把这个问题拆成可施工的规格，并**推翻了项目此前记录的一个阻塞项**。产出：
  - `docs/analysis/nvn_device_requirements.md`——NVN 设备实现需求（含**两处对既有设计的修正**）
  - `docs/analysis/display_bridge_requirements.md`——`nn::vi` 显示桥实现需求
  - `scripts/runtime/nvn_name_references.py` + `evidence/nvn/guest_nvn_code_references.tsv`——**534 个 API 的确切清单**（名字 + 查找它的代码偏移 + 引用次数）
  - `docs/analysis/nvn_surface.md` 追加第 5 节，记录运行证据修正
  1. **工作量被精确钉死：534 个 API，全部按名解析**。脚本扫 main NSO 文本段里物化 NVN 名字的 ADRP/ADD 对，并按"后面是否跟一次间接调用"分类：**535 个名字字符串中，534 个被代码引用，且 534 个全部交给间接调用**——即 guest 在运行时**按名解析全部 534 个**；唯一没被代码引用的是 `nvnBootstrapLoader` 自己（它是导入，不是查找）。⇒ T04 的工作量清单是**确认**的：1 个 bootstrap + 534 个入口，不是"大概 535 个"。
  2. **修正一：分发是两级，不是一级**。`nvn_surface.md` 第 1 节说其余 534 个 API 是"传给 `nvnBootstrapLoader`"取得的——**真机证据表明不是**：guest 只向 bootstrap 问 **2 个**名字（`nvnDeviceInitialize`、`nvnDeviceGetProcAddress`，5 个调用点），其余 534 个是**经 bootstrap 交出来的 `nvnDeviceGetProcAddress` 按名解析**（调用点 `0x1435464` / `0x143547c` / `0x1430e10`）。⇒ 桥接层需要 **bootstrap（2 名字）+ device 级 GetProcAddress（534 名字）两级**；把 534 个名字塞进 bootstrap 平表将**永远查不到**。附带一条硬约束：guest **不检查**该返回值（`0x1430e10` 直接 `str` 不判空），所以对 guest 会问的名字**不能返 NULL**（否则跳 `pc=0`），只有命名空间外的名字可以拒。
  3. **修正二（推翻既有阻塞项）：`nvnDeviceBuilder` 布局**不是**阻塞**。项目此前记录"所绑 NVN 头是旧 53.105/Darksiders ABI、不含 `nvnDeviceBuilder*`，builder 结构体布局未知、不得猜"。guest 代码说明这个布局**对 guest 不可观测**：NVN 管理函数帧为 `sub sp,sp,#0x90`（`0x14353a8`），保存寄存器在 `sp+0x40..0x8f`，故局部区 `sp+0x00..0x3f` = **64 字节**；builder 就是这块内存（`mov x0,sp` @ `0x143548c`/`0x1435498`，`mov x1,sp` @ `0x14354a8` 传给 `nvnDeviceInitialize`），而 **guest 从不读写它的任何字段**，只把地址交给 `nvnDeviceBuilderSetDefaults` / `SetFlags` / `nvnDeviceInitialize` 三个 NVN 函数。⇒ **自研设备可以自定义自己的 builder 布局**，只要 (a) 不超出那 64 字节（超出就会踩坏 guest 的保存寄存器），(b) 自身 setter/getter/initialize 互相自洽。本条**不**声称还原任天堂布局，只说明它对 guest 不可观测，所以**不必**还原。
  4. **显示桥比预想小得多：只有 5 个导入，全是 void 返回**。`nn::vi` 族 228–232 各只有 **1 个**调用点，且 5 个调用点相邻成**一段直线块**（`0x51eac4`/`0x51eacc`/`0x51ead8`…见 `display_bridge_requirements.md`），序列为 `Initialize` → `OpenDefaultDisplay` → `GetDisplayVsyncEvent` → `CreateLayer` → `GetNativeWindow`；**返回类型由调用点定论**（每次调用后 `x0` 立刻被覆写或改用作 `x1`，无人消费结果 ⇒ 五个都是 void）。guest 的显示对象 `0x60` 字节，字段：`+0x18` Display*、`+0x20` **native window**、`+0x28` Layer*、`+0x30` vsync 事件、`+0x58` 标志位。**`nn::vi::GetNativeWindow(void**, Layer*)` 就是 Android 桥与 Vulkan 桥的接缝**——它交出的正是 `ANativeWindow*`（`vkCreateAndroidSurfaceKHR` 的输入）。
  5. **但显示桥在 NVN 墙之后**：v62 日志里 `[unresolved-import]` 为 **0** 条，说明 `nn::vi` 那段**没有执行**；那段在文本里也**没有直接调用者**（经间接路径进入）。⇒ 绑定 `nn::vi` **不会**推进当前 boot；**必须与 NVN 设备一起交付**，才会出现第一帧。
  6. **T04 现有 `nvn_dispatch.c` 的两处桩不能直接集成**：`stub_nvnDeviceInitialize` 返回 **1**（**谎报设备初始化成功**——这正是本运行时唯一不允许做的事，v62 证明 guest 把 false 当**致命**错误处理）；`nvnBootstrapLoader` 的 fallback 返回 NULL（会让 guest 跳 `pc=0`）。
  7. **另外两件事已经就位，不必再查**：(a) **包装在位**——`FFRS-Switch.pak` 2958593334B、`FFRS-Switch.ucas` 5976848928B、`FFRS-Switch.utoc` 12382627B、`global.ucas` 3353200B 都在 romfs 镜像里且 guest 已挂载，cooked shader code 在 IoStore 容器内（不在散文件里，所以"清单里没有 shader 路径"不等于"缺 shader"）；(b) **Vulkan 在 RG477M 已被验证**——`src/graphics/vulkan_probe/` 已能在设备上创建 instance/device/queue 并跑通 buffer 传输路径，所以 Vulkan 侧已去风险，**未建的是它上面的 NVN 层**。
- **下一步候选（按价值）**：
  1. **P-MM-12：guest 致命错误应终止**——`nn::diag::detail::AbortImpl`（import 12）桩目前打印后**返回**，guest 于是掉进 `udf #0`（SIGILL, exit 132）。正确行为是就地终止 guest 并把**游戏自己的**消息原样报出。**这不会让游戏启动**，只让每一次"游戏自己判定失败"的结局干净可读。
  2. **NVN 设备实现（T04 正路）**：按 `nvn_device_requirements.md` 实现 bootstrap + 两级分发 + 一个真设备；第一版只要能让 `nvnDeviceInitialize` **真的**成功并返回**真**能力集，后续才有意义。
  3. **显示桥（5 个导入）**：与上面一起交付才有第一帧；`nn::vi::GetNativeWindow` 交 Android `ANativeWindow*`。
  4. **输入桥**：`236/237` 键盘鼠标 + `385` + `449–463` Npad 族；Npad 状态结构是固定大结构，**字段顺序必须从 guest 自己的读法取证**，不能照抄头文件（与 builder 同理）。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **373** 个导入未绑定（`imports=651`），但**其中最关键的 353 已经绑完**，剩下的是"要么没有 NVN 设备、要么没有窗口、要么没有输入"这三类**平台桥**，不再是"逐个补符号"能解决的量级。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。**引擎版本未证**：guest 二进制里查不到版本串（"LowLevelFatalError [File:..] [Line:..]" 这个致命错误格式是 UE4 风格，但既有文档写的是 UE 5.6.1），故代码注释一律只写"Unreal Engine's Switch NVNRHI"，**不宣称版本**。

# P-MM-11 闭环｜2026-10-24 19:35 Asia/Shanghai — 绑定 `nvnBootstrapLoader`（import 353，**整个 main NSO 唯一一个动态导入的 NVN 符号**），真机**越过 v61 的 SIGTRAP**（unresolved 374 → 373），前沿从「未解析导入」推进到 **游戏自己的致命错误**（UE `NVNManager.cpp:1246` `LowLevelFatalError: nvnDeviceInitialize failed`，exit 132）

- **结论**：工单 P-MM-11 目标闭环。`nvnBootstrapLoader`（import 353）已绑定为**诚实空设备后端**；真机 v62 **不再**因该符号触发 `[unresolved-import]`，profile 行 `unresolved` 由 **374 → 373**（**正好 1 个**），boundary 导入计数由 **274 → 275**。**本轮的重大变化不是"又推进了一个导入"，而是游戏第一次走到了它自己的致命错误路径**——也就是说，**挡住标题画面的已经不再是导入绑定，而是"这台运行时没有 NVN 设备"这件事本身**。**未达标题画面 / 可玩**。
  1. **取证：这是 Unreal Engine 的 Switch NVNRHI**。guest 的 NVN 管理代码属于 `.\../Platforms/Switch/Source/Runtime/NVNRHI/Private/NVNManager.cpp`（字符串直接取自 guest 二进制）。`nvnBootstrapLoader` 有 **5 个调用点**（`0x143541c` / `0x1435438` / `0x143544c` / `0x143554c` / `0x1435578`），但只传 **2 个不同的名字**：`"nvnDeviceInitialize"`（字符串在 `0x63fd01f`）与 `"nvnDeviceGetProcAddress"`（`0x636485c`）。
  2. **ABI**：`nvnBootstrapLoader` **没有 mangled 名** ⇒ 是 C 函数，`(const char*) → void*`，**1 参、返回 64 位函数指针**（x0 进、x0 出）。这与本项目此前所有绑定（C++ mangled 名、参数由 mangling 定）都不同，签名门禁按 `arguments=1 / returns=2` 登记。
  3. **决定实现形态的一条硬约束（静态取证）**：guest **从不检查** `nvnDeviceGetProcAddress` 的返回值——解析器 `0x1430e10` 逐个 `blr x19` 取地址后直接 `str x0, [x8, #偏移]` 存进全局，**没有任何 NULL 判断**，之后直接 `blr` 过去。⇒ 若这里返回 NULL，guest 会跳到 **pc=0**，比干净的 SIGTRAP 更难诊断。⇒ 空设备后端必须为**整个 `nvn` 命名空间**提供"存在但不做事"的入口。
  4. **诚实口径（本单的核心）**：
     - `nvnDeviceInitialize` **返回 false**（失败）——本运行时确实没有 NVN 设备，失败是事实，不是占位符。guest 自己读 `w0` 的低字节（`0x14354b4 tst w0,#0xff`）并因此走失败路径。
     - `nvnDeviceGetProcAddress` 只服务 `nvn` 命名空间；**命名空间外的名字一律 NULL**（例如 `notAnNvnName`），**本运行时不提供的名字一律 NULL**（例如 `nvnDeviceThatDoesNotExist`）。
     - 被服务的入口**什么都不做**：x0 返 0（整数/指针结果）、d0 返 0（浮点结果），并计数打点。
     - **不宣称设备、队列、交换链、表面、显示或任何一帧**。
  5. **旧 guard 被改写成更强的 guard（不是简单删掉）**：主契约里原本有一条"`nvnBootstrapLoader` **必须不可解析**"的拒绝项——那是旧政策（"运行时没资格凭空造一个 NVN 设备"）。本单把它换成**更强**的断言：必须可解析，但**设备初始化必须答失败**、未提供的名字必须被拒、命名空间外的名字必须被拒。`--nvn-bootstrap` 契约再把同样的性质钉死在 guest 真实路径上。
  6. **契约 `--nvn-bootstrap`**：自造**单导入** fixture（`nvn-bootstrap.nso`，sha256 `40b7eda9…`）驱动**真实 guest 导入路径**——fixture 自己 `blr` 导入取回两个入口、调用设备初始化、调用名字查找、再调用取回的空入口。宿主 `errors=0`；真机 `native_ran=true`、`guest_marker=true`、`direct_ok=true`。直调路径用**另一组**名字（`nvnDeviceThatDoesNotExist` / `notAnNvnName`）验证桩按名分发而不是无脑返回非空。
  7. **三变体自证**：A（把期望从"初始化必须失败"改成"必须成功"、保留末尾 `CHECK(!errors)`）⇒ **exit 1（红）**；B（同一处改坏、去掉末尾 `CHECK(!errors)` 且报告错误数**硬编码 0**）⇒ **exit 0（假绿）**，stdout 仍谎报 `nvn_bootstrap_errors:0` 而 `direct_ok:false` 暴露了失败被掩盖；C（**契约完全不改**，让桩把设备初始化改成**返回成功**）⇒ **exit 1（红）**——这条正是"后端永远不许宣称设备存在"的守门。日志 `.local/logs/p-mm-11-contract-selfproof.log`。
  8. **真机 v62 的关键发现（比"推进一个导入"重要得多）**：guest 把**整条 NVN 引导链真跑了一遍**——第 57878/57879 行解析出 `nvnDeviceBuilderSetDefaults` 与 `nvnDeviceBuilderSetFlags`，第 57880/57881 行两次调用空入口，第 57882 行调用 `nvnDeviceInitialize` 得到 false；随后 **游戏自己**抛出致命错误：

     ```
     [hle] nn::diag::detail::AbortImpl message: "LowLevelFatalError [File:.\../Platforms/Switch/Source/Runtime/NVNRHI/Private/NVNManager.cpp] [Line: 1246]
     nvnDeviceInitialize failed
     "
     ```

     guest 回溯 `frame[3] = module_offset 0x14355c4` **正好是静态预测的失败路径分支**（`0x14355a0` 记日志 → `0x14355c4` 继续）。`nn::diag::detail::AbortImpl`（import 12）被调用了**两次**，而**运行时桩只是打印后返回**；guest 于是继续往下走，在 **module offset `0x11f8`** 撞上 `udf #0`（`0x11f4` 调完导入紧跟 `udf`）⇒ **SIGILL，exit 132**。
- **新阻塞一句话**：现在挡住标题画面的**不是**未解析导入，而是**这台运行时没有 NVN 设备**——UE 在 NVN 初始化失败时直接 `LowLevelFatalError`，游戏无法在无设备情况下启动。要看到标题画面，**必须**有真的 NVN 设备，那属于 **T04（NVN→Vulkan 桥）**，不是继续绑定导入能解决的。同时有一个**独立的、更小的下一步**：guest 的致命错误路径目前**不是终止性的**——`nn::diag::detail::AbortImpl` 桩打印后返回，guest 于是掉进 `udf #0`；**正确行为是就地终止 guest 并把游戏自己的消息原样报出来**（P-MM-12 候选）。二者不要混淆：**P-MM-12 只让致命错误干净地终止，不会让游戏启动起来**。
- **产物与证据**：
  - 构建目录 `.local/build-p-mm-11/`（`guest_process_contract.android` sha256 `8b148d2a…`、`nso_owned_startup.android` sha256 `b9af4fc1…`、`nvn-bootstrap.nso` sha256 `40b7eda9…`）；manifest 已写入 `android_executed` / `device_contracts` / `device_boot`。
  - 真机日志 `.local/logs/rg477m-boot-v62-nvn-bootstrap-loader.log`（**57947** 行，exit 132）+ `.exitcode`；契约日志 `.local/logs/rg477m-contract-p-mm-11-nvn-bootstrap.stdout/.stderr`；自证日志 `.local/logs/p-mm-11-contract-selfproof.log`。
  - 设备侧文件：`p-mm-11-nso_owned_startup.android-b9af4fc1`、`p-mm-11-guest_process_contract.android-8b148d2a`、`p-mm-11-nvn-bootstrap.nso-40b7eda9`。
  - 取证脚本：`.local/selfproof/decode.py`（Capstone 直接解 main NSO 的 module offset，地址即 guest 自己的寻址，可与 boot 日志逐条对照）。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **373** 个导入未绑定（`imports=651`）。**空设备后端不是图形实现**：它只让游戏走到"没有设备"这个事实面前，**一帧都没有产生**。T04 的 NVN→Vulkan 桥当前只有 `nvnDeviceInitialize` / `nvnDeviceFinalize` 两条桩（fallback 返 NULL），且所绑定的 NVN 头是**旧 53.105/Darksiders ABI**、**不含** `nvnDeviceBuilder*`，builder 结构体布局未知、**不得猜**。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。

# P-MM-10 闭环｜2026-10-24 19:05 Asia/Shanghai — 绑定 `nv::InitializeGraphics(void*, unsigned long)`（import 352），真机**越过 v60 的 SIGTRAP**（unresolved 375 → 374，前沿推进到 **`nvnBootstrapLoader` import 353**＝**进入 NVN 引导加载**）

- **结论**：工单 P-MM-10 目标闭环。NVN 图形初始化调用（import 352 `nv::InitializeGraphics`）已绑定为**如实记录**型桩；真机 v61 **不再**因该符号触发 `[unresolved-import]` / `[fault] signal=5`，profile 行 `unresolved` 由 **375 → 374**（**正好 1 个**），boundary 导入计数由 **273 → 274**，新第一致命推进到 **import 353 `nvnBootstrapLoader`**，exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **ABI 由 guest 定论**（mangling `_ZN2nv18InitializeGraphicsEPvm` 不编码返回类型）：**单调用点 `0x1434cb4`**，`x0` = guest 自己的参数结构体指针、`x1` = 一个 64 位数值，**返回值不被任何指令消费** ⇒ **两参、void**。调用点与 import 350/351 的注册（`0x1434c90` / `0x1434ca4`）**同属一段直线块**，中间只有实参搬运、**无分支** ⇒ 每次 boot 必达。
  2. **静态预测 → 真机实测，逐字节吻合**：静态反汇编得到 `x0 = module+0x82d6000`、`x1 = 0x800000`（8 MiB）。真机 v61 第 **57650** 行记录到 `param=0x72df703000 size=0x800000`；用同一日志里 guest 自己的分配器回调（`alloc=0x72d74494b0`，静态已知为 `module+0x1c4b0`）反推模块基址 `0x72d742d000`，则 `param - 基址 = 0x82d6000`——**与静态预测完全一致**，size 也**完全一致**。这说明静态分析给出的实参值在真机上被原样送达，没有被运行时改写。
  3. **诚实口径：只记录，不宣称存在图形设备**。桩把 `param` 与 `size` 存下来，并明确打印 **`no NVN graphics device exists in this runtime yet`**。**不**返回设备句柄、**不**创建队列/交换链/表面、**不**宣称任何显示能力；guest 参数块仍由 guest 拥有，桩不改写它。本运行时确实没有 NVN 图形设备，这句话是事实陈述而不是占位符。
  4. **不伪造 CPU/GPU 频率、不伪造设备**：沿用 `nn::oe::SetCpuBoostMode`（75）与 `nn::oe::SetPerformanceConfiguration`（239/240）的既有口径，**不做任何宿主侧"模拟设备"的事**。
  5. **契约 `--nv-initialize-graphics`**：自造**单导入** fixture（`nv-initialize-graphics.nso`，sha256 `736a3d2b…`）驱动**真实 guest 导入路径**——guest 自己 `blr` 导入并把 `x0`/`x1` 写进输出槽。宿主 `errors=0`；真机 `native_ran=true`、`guest_marker=true`、`direct_record_ok=true` ⇒ 真实导入路径上参数被如实送达。直调路径用**另一组**哨兵值（`0x1234` / `0x20003`）验证桩不忽略实参。
  6. **三变体自证**：A（故意把期望 size 改成 `0x99999`、保留末尾 `CHECK(!errors)`）⇒ **exit 1（红）**，`[check-fail] nv-initialize-graphics-direct-size actual=0x20003 expected=0x99999`，失败行 `process contract line=4032: !errors`；B（同一处改坏、去掉末尾 `CHECK(!errors)` 且报告错误数**硬编码 0**）⇒ **exit 0（假绿）**，stdout 仍谎报 `direct_record_ok:true`；C（**契约完全不改**，让桩**丢掉 size**）⇒ **exit 1（红）**，`actual=0x0 expected=0x20003`。日志 `.local/logs/p-mm-10-contract-selfproof.log`。
  7. **刻意排除的符号（不夹带）**：**import 353 `nvnBootstrapLoader`**——见下方新阻塞；`89/90/91 nn::nifm`（是否联网取决于设备**真实**网络状态）、`228–232 nn::vi`（显示族，需 Android Surface 桥）、`236/237` 与 Npad 族（需 Android 输入桥）。
- **新阻塞一句话**：真机 v61 现在停在 **import 353 `nvnBootstrapLoader`**（`caller_offset=0x143543c`，exit 133）。这是**整个 main NSO 里唯一一个动态导入的 NVN 符号**——其余 534 个 NVN API 是**运行时按字符串名**解析的（已实证：`nvnBootstrapLoader("nvnDeviceInitialize")`（字符串在 `0x63fd01f`）、`nvnBootstrapLoader("nvnDeviceGetProcAddress")`（字符串在 `0x636485c`），再由 `GetProcAddress(NULL,"nvnDeviceBuilderSetDefaults")`（`0x6361cc6`）与 `("nvnDeviceBuilderSetFlags")`（`0x6378018`）喂 builder vtable）。`nvnBootstrapLoader` **无 mangled 名** ⇒ C 函数，ABI = `(const char*) → void*`。**下一刀不能写成"返回 NULL 的万能桩"**：guest 拿到 NULL 会直接 `blr` 到 `pc=0`，比干净的 SIGTRAP 更难诊断；且所绑定的 NVN 头是**旧 53.105/Darksiders ABI**，**不含** `nvnDeviceBuilder*`，builder 结构体布局未知、**不得猜**。真正的 NVN 设备属于 **T04（NVN→Vulkan）桥**（只读工作区 `.claude/worktrees/t04-nvn`，其 `src/graphics/nvn/src/nvn_dispatch.c` 目前只有 `nvnDeviceInitialize`/`nvnDeviceFinalize` 两条桩、fallback 返 NULL）。⇒ **P-MM-10 有意不绑 353，把它留作一个已完全诊断的前沿**；下一单 P-MM-11 应与 T04 协作评估 `nvnBootstrapLoader` 的真实实现路径。
- **产物与证据**：
  - 构建目录 `.local/build-p-mm-10/`（`guest_process_contract.android` sha256 `7711fbc1…`、`nso_owned_startup.android` sha256 `bd2ad15f…`、`nv-initialize-graphics.nso` sha256 `736a3d2b…`）；manifest 已写入 `android_executed` / `device_contracts` / `device_boot`。
  - 真机日志 `.local/logs/rg477m-boot-v61-nv-initialize-graphics.log`（**57668** 行，exit 133）+ `.exitcode`；契约日志 `.local/logs/rg477m-contract-p-mm-10-nv-initialize-graphics.stdout/.stderr`；自证日志 `.local/logs/p-mm-10-contract-selfproof.log`。
  - 设备侧文件：`p-mm-10-nso_owned_startup.android-bd2ad15f`、`p-mm-10-guest_process_contract.android-7711fbc1`、`p-mm-10-nv-initialize-graphics.nso-736a3d2b`。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **374** 个导入未绑定（`imports=651`），其中**最大的一块仍是 NVN 那 535 个 API**——逐个绑定无法把游戏带到可玩，必须走 T04 的 NVN→Vulkan 桥。`0x82d6000` 处参数结构体的**字段含义未逐字段解码**（本单只如实记录指针与 size，不对其内容下结论）；`0x800000` 只作为**观测到的数值**记录，不宣称它是"显存大小"或任何未证实的语义。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。

# P-MM-9 闭环｜2026-10-24 18:55 Asia/Shanghai — 绑定 NVN 图形分配器注册组（import 350 `nv::SetGraphicsAllocator` + 351 `nv::SetGraphicsDevtoolsAllocator`），真机**越过 v59 的 SIGTRAP**（unresolved 377 → 375，前沿推进到 `nv::InitializeGraphics` import 352＝**进入 NVN 图形初始化**）

- **结论**：工单 P-MM-9 目标闭环。NVN 图形分配器注册对 **2 个符号一次绑定**（import 350 `nv::SetGraphicsAllocator`、351 `nv::SetGraphicsDevtoolsAllocator`）；真机 v60 **不再**因这两个符号触发 `[unresolved-import]` / `[fault] signal=5`，profile 行 `unresolved` 由 **377 → 375**（**正好 2 个**），boundary 导入计数由 **271 → 273**，新第一致命推进到 **import 352 `nv::InitializeGraphics(void*, unsigned long)`**，exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **签名与已绑定的 `movie::SetAllocator`（import 74）完全同形**：`void*(*)(ulong,ulong,void*)` 分配回调、`void(*)(void*,void*)` 释放回调、`void*(*)(void*,ulong,void*)` 重分配回调、`void*` userdata，**四参、无返回**（mangling 也不编码返回类型，由 guest 侧「调用后 w0 立刻被覆写」定论）。
  2. **触达证据**：两个调用点 `0x1434c90`（350）与 `0x1434ca4`（351）**紧邻成一段直线块**，中间只有实参搬运、**无任何分支** ⇒ 每次 boot 必然都被调用。
  3. **guest 自己的回调地址（静态预测 → 真机实测，完全吻合）**：静态反汇编得到 alloc=`module+0x1c4b0`、free=`module+0x1c4c0`、realloc=`module+0x13340`、userdata=`NULL`；真机 v60 第 **58468/58469** 行记录到 `alloc=0x6e2f5d04b0 free=0x6e2f5d04c0 realloc=0x6e2f5c7340 userdata=0x0`，三地址恰好相差 0x10/0x10 且换算回模块基址与静态预测**逐个一致**。
  4. **只记录、不回调**：沿用 P-MM-2 movie 注册的既有口径——从宿主桩回调 guest 函数需要 guest 线程与 guest TLS 上下文，会破坏 guest 线程状态；但**注册本身是 guest 的真实状态变更**，必须如实保存，后续真正分配的 NVN 桩必须用 guest 选定的分配器而不是宿主 malloc。两个注册（graphics / devtools）**分开存储**，因为它们是两次独立注册。
  5. **契约 `--nv-graphics-allocator`**：自造**双导入** fixture（`nv-graphics-allocator.nso`，sha256 `2a4a4949…`）驱动**真实 guest 导入路径**——guest 依次调用两个注册并传入**自己的**三个回调（fixture 偏移 `0xc0/0xc4/0xc8`）。宿主 `errors=0`；真机 `native_ran=true`、`guest_marker=true`，且宿主记录到的三个回调指针与 fixture 布局**逐一吻合**（`0x…80c0/0x…80c4/0x…80c8`、userdata=0）。直调路径用**另一组**哨兵指针（`0x1111/0x2222/0x3333/0x4444` 与 `0x5555/0x6666/0x7777/0x8888`）验证桩不忽略实参。
  6. **三变体自证**：A（故意把期望指针改成 `0x9999`、保留末尾 `CHECK(!errors)`）⇒ **exit 1（红）**，失败行 `process contract line=3941: !errors`；B（同一处改坏、去掉末尾 `CHECK(!errors)` 且报告错误数**硬编码 0**）⇒ **exit 0（假绿）**；C（**契约完全不改**，让桩**不保存**分配回调）⇒ **exit 1（红）**，`actual=0xdead`。日志 `.local/logs/p-mm-9-contract-selfproof.log`。
  7. **过程中两处 fixture 编写错误（已修）**：`mov x0, cb_alloc` 不能带符号地址（须用 `adr`）；加入第二个调用块后代码超过 136 字节，原 `.org 0x80` 落在代码之后（须移到 `0xc0`，并同步测试常量 `NV_FIXTURE_CB_*_OFF`）。
- **新阻塞一句话**：真机 v60 现在停在 **import 352 `nv::InitializeGraphics(void*, unsigned long)`**（`caller_offset=0x1434cb8`，exit 133），**这是第一个需要真实图形路径、而不是「注册」就能满足的导入**。guest 传入的实参是：参数结构体指针 `module+0x82d6000` 与数值 `0x800000`（=8 MiB）；调用后返回值不被消费。**下一刀不能只做「又一个桩」**：必须先查清 ①`0x82d6000` 处的参数结构体内容、②`0x800000` 的含义、③设备句柄从哪来（邻接的 `nvnBootstrapLoader` 有 5 个调用点）、④guest 之后真正触及哪一批 NVN API（本项目 route 文档记的是 535 个 NVN API）。**本项目的 T04（NVN→Vulkan）才是这段的正路**；同时 `nn::vi` 显示族（228–232）与每帧输入族（236/237、Npad 族）是「可操作试玩」所需的另外两座桥。**不得写「假装有显示/输入设备」的桩**。
- **产物与证据**：
  - 构建目录 `.local/build-p-mm-9/`（`guest_process_contract.android` sha256 `b7c21b87…`、`nso_owned_startup.android` sha256 `bc6de5e5…`、`nv-graphics-allocator.nso` sha256 `2a4a4949…`）；manifest 已写入 `android_executed` / `device_contracts` / `device_boot`。
  - 真机日志 `.local/logs/rg477m-boot-v60-nv-graphics-allocator.log`（**5847x** 行，exit 133）+ `.exitcode`；契约日志 `.local/logs/rg477m-contract-p-mm-9-nv-graphics-allocator.stdout/.stderr`；自证日志 `.local/logs/p-mm-9-contract-selfproof.log`。
  - 设备侧文件：`p-mm-9-nso_owned_startup.android-bc6de5e5`、`p-mm-9-guest_process_contract.android-b7c21b87`、`p-mm-9-nv-graphics-allocator.nso-2a4a4949`。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **375** 个导入未绑定（`imports=651`），其中**最大的一块是 535 个 NVN API**，逐个绑定无法把游戏带到可玩，必须走 T04 的 NVN→Vulkan 桥。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。

# P-MM-8 闭环｜2026-10-24 17:15 Asia/Shanghai — 绑定 `nn::oe` 性能配置组（import 239 `SetPerformanceConfiguration` + 240 `GetPerformanceMode`，**一次 2 个**），真机**越过 v58 的 SIGTRAP**（unresolved 379 → 377，前沿推进到 **`nv::SetGraphicsAllocator` import 350**＝进入图形阶段）

- **结论**：工单 P-MM-8 目标闭环。`nn::oe` 性能配置对 **2 个符号一次绑定**（import 239 `SetPerformanceConfiguration(PerformanceMode, int)`、240 `GetPerformanceMode()`），均为**如实记录**型桩；真机 v59 **不再**因这两个符号触发 `[unresolved-import]` / `[fault] signal=5`，profile 行 `unresolved` 由 **379 → 377**（**正好 2 个**），boundary 导入计数由 **269 → 271**，新第一致命推进到 **import 350 `nv::SetGraphicsAllocator()`**，exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **ABI 由 guest 定论（mangling 不编码返回类型）**：
     - **239**：`SetPerformanceConfiguration` 有 **4 个调用点**（`0x520518`/`0x520524`/`0x5205bc`/`0x5205c8`），每一处都在 `bl` 之前立刻 `mov w0, …` / `mov w1, …`，且**调用后 w0 立刻被下一条指令覆写** ⇒ **两参（w0 模式 / w1 时钟码）、无返回**。
     - **240**：`GetPerformanceMode` 只有 **1 个调用点** `0x5203c4`，返回值在 `0x520404` 被 `cmp w0, #1` 消费 ⇒ **零参、返回 32 位模式**。
  2. **guest 实际传的实参**（静态分析 + 真机日志**双重吻合**）：先 `SetPerformanceConfiguration(0, 0x20003 或 0x20004)`，再 `SetPerformanceConfiguration(1, 0x10001)`；真机 v59 第 **53229/53230** 行记录到 `mode=0 clock=0x20004` 与 `mode=1 clock=0x10001`，**与静态预测逐条吻合**。
  3. **不伪造真实频率变更**：沿用已绑定的 `nn::oe::SetCpuBoostMode`（import 75）口径——桩**只记录**模式与时钟码，真实 CPU/GPU 频率仍由 **Android governor** 拥有（cpufreq sysfs 需 root，本项目无 root）。
  4. **`GetPerformanceMode` 返回「最近一次请求的模式」**：参考平台的行为就是「先请求、再读回」；guest 在 `0x520404` 正是要判断「当前是不是 Boost」。初值为 **0**（guest 尚未请求任何模式前生效的模式）。
  5. **契约 `--oe-performance`**：自造**双导入** fixture（`oe-performance.nso`，sha256 `bac43e9f…`）驱动 **guest 真实路径的「设置→读回」往返**——fixture 先经导入设 `mode=5 clock=0x20003`，再经第二个导入读回模式并写进输出槽。宿主 `errors=0`；真机 `native_ran=true`、`guest_marker=true`、`guest_performance_mode=5` ⇒ 真实导入路径上「设 5 读回 5」成立；直调路径用**另一组**值（`7 / 0x10001`）验证桩不忽略实参。
  6. **⚠ 自证两次失效、已修正（本单最重要的方法论产出）**：第一版三变体**全绿**——A 改的是 `OE_FIXTURE_PERFORMANCE_MODE`，而该常量**只**出现在 `#if defined(__ANDROID__)` 的设备端期望里，宿主端根本不编译，宿主自证**看不到任何变化**；改成 `OE_DIRECT_PERFORMANCE_MODE` 后**仍全绿**，因为它**同时是输入和期望**，两边一起动，断言恒真。**修法**：把期望值拆成独立常量 `OE_EXPECTED_DIRECT_MODE` / `OE_EXPECTED_DIRECT_CLOCK`（输入用另一组常量），自证**只改期望**。修正后三变体恢复区分力：A **exit 1（红）**、B **exit 0（假绿**，且报告里 `direct_ok:false` 暴露了失败被掩盖）、C **exit 1（红**，桩被改成**忽略实参**后 `actual=0x63 / clock=0x0`）。失败行 `process contract line=3809: !errors`。日志 `.local/logs/p-mm-8-contract-selfproof.log`。
  7. **刻意排除的符号（不夹带）**：`89/90/91 nn::nifm`（**是否联网取决于设备真实网络状态**，不能凭空断言）、`228–232 nn::vi`（显示族，需 Android Surface 桥）、`236/237` 与 Npad 族（需 Android 输入桥）。
- **新阻塞一句话**：真机 v59 现在停在 **import 350 `nv::SetGraphicsAllocator(...)`**（`caller_offset=0x1434c94`，exit 133）。它的签名与**已绑定的 `movie::SetAllocator`（import 74）完全同形**：`void*(*)(ulong,ulong,void*)` 分配回调、`void(*)(void*,void*)` 释放回调、`void*(*)(void*,ulong,void*)` 重分配回调、`void*` userdata。故障现场寄存器也印证了四参形态（x0/x1/x2 = 三个回调，x3 = userdata = 0）。**下一刀应攻 import 350**，并以 P-MM-2 的 movie 契约 + 自造 fixture 为模板复用（注意：四个回调必须**原样到达宿主**，桩不能只打印）。另注：v59 日志里游戏已在读 `rom:/Unreal Engine/Engine/Config/SystemEngine.ini`、`rom:/FFRS/Config/UserEngine.ini` 等 UE 配置 ⇒ **已进入配置阶段与图形阶段**。同族还有 import 351 `nv::SetGraphicsDevtoolsAllocator`（同签名），是否一并绑定需先证明其调用点可达。
- **产物与证据**：
  - 构建目录 `.local/build-p-mm-8/`（`guest_process_contract.android` sha256 `23a83c61…`、`nso_owned_startup.android` sha256 `d8350f2d…`、`oe-performance.nso` sha256 `bac43e9f…`）；manifest 已写入 `android_executed` / `device_contracts` / `device_boot`。
  - 真机日志 `.local/logs/rg477m-boot-v59-performance-configuration.log`（**56907** 行，exit 133）+ `.exitcode`；契约日志 `.local/logs/rg477m-contract-p-mm-8-oe-performance.stdout/.stderr`；自证日志 `.local/logs/p-mm-8-contract-selfproof.log`。
  - 设备侧文件：`p-mm-8-nso_owned_startup.android-d8350f2d`、`p-mm-8-guest_process_contract.android-23a83c61`、`p-mm-8-oe-performance.nso-bac43e9f`。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **377** 个导入未绑定（`imports=651`）。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。

# P-MM-7 闭环｜2026-10-24 16:55 Asia/Shanghai — 绑定 `nn::ldn::GetState()`（import 88），真机**越过 v57 的 SIGTRAP**（unresolved 380 → 379，前沿推进到 `nn::oe::SetPerformanceConfiguration` import 239）

- **结论**：工单 P-MM-7 目标闭环。`nn::ldn::GetState()`（import 88）已绑定为**如实记录**型桩；真机 v58 **不再**因该符号触发 `[unresolved-import]` / `[fault] signal=5`，profile 行 `unresolved` 由 **380 → 379**（**正好 1 个**），boundary 导入计数由 **268 → 269**，新第一致命推进到 **import 239 `nn::oe::SetPerformanceConfiguration(nn::oe::PerformanceMode, int)`**，exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **触达证据被本轮设备运行改写**：此前 `state.json` 把 index 88 记为「条件分支可达 ⇒ 不可证实触达 ⇒ 不夹带」。v57 真机运行直接打到 `caller_offset=0x51dcac`（`0x51dc9c` 的 `tbz` 本次走了「未置位」分支）⇒ **运行时已证实触达**，静态顾虑被实测取代。
  2. **取值由 guest 四个调用点一致定论**（全部离线反汇编，Capstone 与 llvm-objdump 交叉验证）：
     - `0x15bd4`（刷新函数）：`GetState() != 0` ⇒ 立刻 `return 0`；`== 0` ⇒ 才去拿互斥锁并走**互联网**路径。
     - `0x51dca8` / `0x51ecb8`：guest 把状态并入**三级网络分类**——`nn::nifm::IsNetworkAvailable()`（import 89）真 ⇒ **3（互联网）**；否则状态非 0 ⇒ **2（本地无线）**；否则 ⇒ **1（都没有）**。分类值随后写进全局字节 `0x77741dc`（函数 `0x87680` 比较新旧值，变了才动作）。
     - `0x942edc`：先查状态，再决定是否调刷新函数。
     - `0x943198`：**只有状态 3 与 5 才去取本地无线 IPv4 地址**（`nn::ldn::GetIpv4Address`，import 275）；状态 0 走互联网路径。
     ⇒ 四个调用点**一致**把 **0** 当作「无本地无线会话」。RG477M 是无任天堂本地无线硬件、也不跑本地无线会话的掌机 ⇒ 桩返回 **0**。仓库内无权威 `nn::ldn` 状态枚举表，故**不宣称 0 对应任何名字**。
  3. **ABI 双重确认**：零实参、返回 32 位整数——mangling `_ZN2nn3ldn8GetStateEv` 无参；5 个调用点全部用 `cbz`/`cbnz`/`cmp`/`cinc` 读 **w0**。二者一致。
  4. **契约 `--ldn-get-state`**：自造单导入 fixture（`ldn-get-state.nso`，sha256 `7575abad…`）驱动**真实 guest 导入路径**，guest 自己 `blr` 导入并把返回的 w0 写进输出槽。宿主 `errors=0`；真机 `native_ran=true`、`guest_marker=true`、`guest_ldn_state=0` ⇒ guest 确实经真实导入路径拿回 **0**。
  5. **三变体自证**：变体 A（故意把期望改成 1、保留末尾 `CHECK(!errors)`）⇒ **exit 1（红）**，失败行 `process contract line=3704: !errors`；变体 B（同一处改坏、去掉末尾 `CHECK(!errors)` 且报告错误数**硬编码 0**）⇒ **exit 0（假绿）**，stderr 仍留 `[check-fail]`；变体 C（**契约完全不改**，让桩返回 7）⇒ **exit 1（红）**，证明契约观测的是**桩的真实返回值**。日志 `.local/logs/p-mm-7-contract-selfproof.log`。
  6. **刻意排除的符号（不夹带）**：`89/90/91 nn::nifm` 族（**是否联网取决于设备真实网络状态，不能凭空断言**；且 `89` 只有在 guest 走到互联网分支时才会被调用）、`240 nn::oe::GetPerformanceMode`（单调用点 `0x5203c4`，返回语义尚未从 guest 侧定论）、`228–232 nn::vi`（显示族，需 Android Surface 桥）、`236/237` 与 Npad 族（需 Android 输入桥）。
- **新阻塞一句话**：真机 v58 现在停在 **import 239 `nn::oe::SetPerformanceConfiguration(nn::oe::PerformanceMode, int)`**（`caller_offset=0x5205c0`，exit 133）。guest 有 **4 个调用点**（`0x520518`/`0x520524`/`0x5205bc`/`0x5205c8`），形态一致：`w0` = 模式，`w1` = 时钟码——先 `SetPerformanceConfiguration(0, 0x20003 或 0x20004)`，再 `SetPerformanceConfiguration(1, 0x10001)`；**返回值都不被消费** ⇒ 两参、void。**下一刀应攻 import 239**，且必须沿用 P-MM-3 `SetCpuBoostMode` 的既有口径：**如实记录模式与时钟码，但不伪造真实 CPU/GPU 频率变更**（Android governor 拥有频率，cpufreq sysfs 需 root）。
- **产物与证据**：
  - 构建目录 `.local/build-p-mm-7/`（`guest_process_contract.android` sha256 `71909eb1…`、`nso_owned_startup.android` sha256 `14668a1b…`、`ldn-get-state.nso` sha256 `7575abad…`）；manifest 已写入 `android_executed` / `device_contracts` / `device_boot`。
  - 真机日志 `.local/logs/rg477m-boot-v58-ldn-get-state.log`（**54853** 行，exit 133）+ `.exitcode`；契约日志 `.local/logs/rg477m-contract-p-mm-7-ldn-get-state.stdout/.stderr`；自证日志 `.local/logs/p-mm-7-contract-selfproof.log`。
  - 设备侧文件：`p-mm-7-nso_owned_startup.android-14668a1b`、`p-mm-7-guest_process_contract.android-71909eb1`、`p-mm-7-ldn-get-state.nso-7575abad`。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **379** 个导入未绑定（`imports=651`）。`nn::vi` 显示族、输入族、`nn::nifm` 联网状态族各自需要平台桥或真实状态查询，不能用桩糊过去。已知语义保留项：P-MM-5 的 `GetCurrentFocusState` 桩返回 0（guest 只与 3 比较，行为等价，但无权威枚举，故不宣称其名字）。

# P-MM-6 闭环｜2026-10-24 16:35 Asia/Shanghai — 绑定 `nn::oe::GetOperationMode()`（import 227），真机**越过 v56 的 SIGTRAP**（unresolved 381 → 380，前沿推进到 `nn::ldn::GetState` import 88）

- **结论**：工单 P-MM-6 目标闭环。`nn::oe::GetOperationMode()`（import 227）已绑定为**如实记录**型桩；真机 v57 **不再**因该符号触发 `[unresolved-import]` / `[fault] signal=5`，profile 行 `unresolved` 由 **381 → 380**（**正好 1 个**，即本单绑定数），boundary 导入计数由 **267 → 268**，新第一致命推进到 **import 88 `nn::ldn::GetState()`**，exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **返回值语义由 guest 代码定论（不是猜的）**：本单开工前明确要求「必须先追到该布尔全局标志的消费点再定值」。追查链条如下，全部来自离线反汇编（Capstone，独立于 llvm-objdump 的切片相对寻址，两者交叉验证）：
     - guest 在 **两个无条件调用点** `0x51dc84` / `0x51ee40` 调用本导入，调用后立即 `cmp w0, #1` ⇒ **零实参、32 位整数返回**（Itanium mangling 不编码返回类型，故返回类型由 w0 的消费方式定）。
     - 两次调用都用 `cset w8, eq` + `strb w8, [x9, #0x614]` 把 **`mode == 1`** 缓存成**字节全局标志**，地址 **module offset 0x7c90614**（`adrp x9, #0x7c90000` + 偏移 `0x614`）。
     - 该标志**全模块只有 2 处写、4 处读**（写：`0x51dc94`、`0x51ee58`；读：`0x51e98c`、`0x51f06c`、`0x523e98`、`0x525104`）。4 个读点形态完全一致：`fcsel` 在 **标志置位时取常量 `0x3cf5c290`（float 0.026873588）**，标志未置位时取**默认 0.0**。
     - 该因子随后**乘在运行时屏幕宽高上**（`scvtf` 把两个 int 宽高转 float 再 `fmul`），结果以 `double` 写进屏幕信息结构的 `0x40/0x48/0x60/0x68/0x70/0x78` 偏移；而**像素宽高本身**原样保留在 `0x00/0x04`。
     - ⇒ **标志未置位分支会把缩放后的屏幕尺寸退化成 0**（`0.0 × width = 0`）。已发行的游戏不可能把这一支当常态 ⇒ **游戏的正常工作态就是标志置位那一支** ⇒ 桩必须返回 **1**。
  2. **不宣称枚举名**：仓库内没有 `nn::oe::OperationMode` 的权威枚举定义（NSO 内出现的 `OperationMode` 字符串经核实是 **UE5 反射元数据**里的一个无关属性名，`Handheld` 字符串是 `nn::hid::NpadHandheldState` 的子串，都不是枚举表），所以桩**只实现 guest 要求的数值 1**，不宣称它对应「掌机/底座」中的哪一个。
  3. **触达证据**：两个调用点都是 init 区里**无条件直线序列**上的 `BL`；且真机 v57 日志第 **55238** 行出现真实 guest 调用 `[hle] nn::oe::GetOperationMode: mode=1 recorded`，前后紧邻 P-MM-5 那组焦点/通知调用，与静态预测逐条吻合。
  4. **ABI 双重确认**：返回类型由两处调用点的 `cmp w0, #1` + `cset` 定为 32 位整数；实参数为 0（mangling `_ZN2nn2oe16GetOperationModeEv` 亦为无参）。二者一致，无歧义。
  5. **契约 `--oe-operation-mode`**：自造单导入 fixture NSO（`oe-operation-mode.nso`，sha256 `178d697f…`）驱动**真实 guest 导入路径**——guest 自己 `blr` 导入并把返回的 w0 写进输出槽。宿主侧 `resolved=true`、`errors=0`；真机侧 `native_ran=true`、`guest_marker=true`、`guest_operation_mode=1` ⇒ guest 确实经真实导入路径拿回了 **1**。边界在 guest 运行前校验该导入签名，签名不被接受则 guest 根本不执行。
  6. **三变体自证（比常规两变体多一组反向对照）**：
     - 变体 A：故意把期望值改成 2、保留末尾 `CHECK(!errors)` ⇒ **exit 1（红）**，失败行 `process contract line=3622: !errors`。
     - 变体 B：同一处故意改坏、但去掉末尾 `CHECK(!errors)` 且把报告里的错误数**硬编码为 0** ⇒ **exit 0（假绿）**，stderr 仍留下 `[check-fail]` —— 证明单跑变体 A 无法区分「已修好」与「断言本来就是恒真的」。
     - 变体 C（**新增的反向对照**）：**契约完全不改**，改让桩返回 0 ⇒ **exit 1（红）** —— 证明契约观测的是**桩的真实返回值**而不是写死的常量。
     - 完整日志：`.local/logs/p-mm-6-contract-selfproof.log`；脚本 `.local/selfproof/selfproof_mm6.sh` + `.local/selfproof/make_mm6_variants.py`（均留在 `.local/`，不入 git）。
  7. **刻意排除的符号（不夹带）**：`228–232 nn::vi`（显示/图层/原生窗口，语义重，需 Android Surface 桥）、`233 TryPopNotificationMessage`、每帧输入读取族 236/237 与 Npad 族（需「Android 输入桥」单独一刀）。
- **新阻塞一句话**：真机 v57 现在停在 **import 88 `nn::ldn::GetState()`**（`caller_offset=0x51dcac`，exit 133）。**这修正了此前「88 只是条件分支可达、不可证实触达」的判断**——v57 用运行时证据证明 `0x51dca8` 这一路**确实被走到了**（`0x51dc9c` 的 `tbz` 在本次运行中走了「未置位」分支）。guest 在 `0x51dca8` 调 `nn::ldn::GetState()`，随后 `cmp w0, #0` + `cinc` 把状态映射成 **三值分类 `w0 ∈ {1, 2, 3}`**（形似 离线/本地/互联网 的网络状态分级），最终值经 `0x51dcb8` 的调用被使用。**下一刀应攻 import 88**；需要先从这 5 个调用点（`0x15bd4`、`0x51dca8`、`0x51ecb8`、`0x942edc`、`0x943198`）判定返回类型与取值语义，**不得编造无线连接状态**。
- **产物与证据**：
  - 构建目录 `.local/build-p-mm-6/`（`guest_process_contract.android` sha256 `27dfaadf…`、`nso_owned_startup.android` sha256 `56d09ae7…`、`oe-operation-mode.nso` sha256 `178d697f…`）；manifest 已写入 `android_executed` / `device_contracts` / `device_boot`。
  - 真机日志 `.local/logs/rg477m-boot-v57-oe-operation-mode.log`（**55255** 行，exit 133）、`.local/logs/rg477m-boot-v57-oe-operation-mode.log.exitcode`；契约日志 `.local/logs/rg477m-contract-p-mm-6-oe-operation-mode.stdout/.stderr`。
  - 设备侧文件：`p-mm-6-nso_owned_startup.android-56d09ae7`、`p-mm-6-guest_process_contract.android-27dfaadf`、`p-mm-6-oe-operation-mode.nso-178d697f`。
  - 离线取证：`scripts/runtime/scan_import_range.py`（区间导入扫描）、`scripts/runtime/resolve_import_callers.py`、Capstone 交叉验证脚本（本地临时，未入 git）。
- **未完成 / 风险**：标题画面与可玩**均未达**；全模块仍有 **380** 个导入未绑定（`imports=651`）。`nn::vi` 显示族与输入族需要各自的平台桥，不能靠桩糊过去。P-MM-5 的 `GetCurrentFocusState` 桩返回 0：guest 只与 3 比较，行为上等价，但仓库内无权威枚举，**此处保留为已知语义保留项**（不宣称 0 的名字）。

# P-MM-5 闭环｜2026-10-24 16:15 Asia/Shanghai — 绑定 `nn::oe` 焦点/通知组（import 222–226，**一次性 5 个**），真机**越过 v55 的 SIGTRAP**（unresolved 386 → 381，前沿推进到 `nn::oe::GetOperationMode` import 227）

- **结论**：工单 P-MM-5 目标闭环。`nn::oe` 焦点/通知组 **5 个符号一次绑定**（import 222 `GetCurrentFocusState`、223 `SetPerformanceModeChangedNotificationEnabled`、224 `SetOperationModeChangedNotificationEnabled`、225 `SetFocusHandlingMode`、226 `SetResumeNotificationEnabled`），均为**如实记录**型桩；真机 v56 **不再**因这些符号触发 `[unresolved-import]` / `[fault] signal=5`，profile 行 `unresolved` 由 **386 → 381**（**正好 5 个**，即本单绑定数），新第一致命推进到 **import 227 `nn::oe::GetOperationMode()`**，exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **本单方法升级（可复用的核心产出）**：此前只能「一刀一个导入」推进，因为没有工具回答「一段 guest 代码到底调了哪些导入」。本单新增 **`scripts/runtime/scan_import_range.py`**：重建 NSO 的 PLT→导入映射后，扫描任意 module-offset 区间，列出该区间调用的**全部**导入（按调用顺序）。用它扫 guest 初始化区 `0x51d000..0x51f800`，得到 **22 个不同导入**，并据此把「已证实触达」的符号**成组绑定**，把每个真机周期的推进量从 1 个提到 5 个。
  2. **触达证据（不是推测）**：反汇编 `0x51d9f0..0x51da60` 证明 `0x51da10`(222) → `0x51da30`(80，已绑定) → `0x51da38`(223) → `0x51da40`(224) → `0x51da48`(225) → `0x51da50`(226) 是**一段无条件直线序列**（223–226 彼此仅隔 8 字节），其间无任何分支 ⇒ **5 个符号每次 boot 必然被调用**。
  3. **返回值与实参由 guest 代码定论（mangling 不编码返回类型）**：
     - 222 的返回值被 guest 存到 `[x19,#0x1108]`，随后**只与 3 比较**（`cmp w8, #0x3`；3 是 guest 认定的「有别的东西占据前台」的特例态），并与上一次存的值比较是否变化 ⇒ 前台 Android 应用应返回**非 3 的稳定值** ⇒ 桩返回 **0** 且永不变。仓库内无权威枚举定义，故桩**不宣称 0 对应任何名字**。
     - 223/224/225/226 的实参由 guest 明确给出：`w0 = 1, 1, 2, 1`；四次调用的返回值**都未被消费**（下一条指令立即覆写 w0）⇒ `{arguments=1; returns=0}`。
     - 222 ⇒ `{arguments=0; returns=1}`。
  4. **刻意排除的符号（不夹带）**：`227 GetOperationMode`（返回值语义待证，下一刀）、`88 nn::ldn::GetState`（**条件分支**可达 ⇒ 不可证实触达）、`228–232 nn::vi`（显示/图层/原生窗口，语义重）、`233 TryPopNotificationMessage`、以及每帧输入读取族 236/237 与 Npad 族。
  5. **契约（宿主 + 真机同测）**：新增 `--oe-focus` 契约，由自造 `oe-focus.nso` fixture 驱动**真实 guest 导入路径**，覆盖两种关键 ABI 形态：`222`（0 参、**有返回值**）与 `225`（1 参 void）。boundary 在 guest 运行前会校验**每一个**已绑定导入的签名，因此 guest 成功调用同时也证明另外三个 setter 的签名被接受。
     - 宿主 PASS：`{"oe_focus_errors":0,...,"direct_ok":true,"native_ran":false,"guest_focus_state":-1}`（宿主无 AArch64 执行器，`-1` = 本次未观测，未伪造）。
     - **RG477M 真机 PASS**：`{"oe_focus_errors":0,"focus_state_resolved":true,"focus_handling_resolved":true,"perf_notification_resolved":true,"operation_notification_resolved":true,"resume_notification_resolved":true,"direct_ok":true,"native_ran":true,"guest_marker":true,"guest_focus_state":0}`。
     - 覆盖面：白名单门禁 ✓；**签名门禁**通过（真机 stderr `[boundary-ready] imports=2 actual-primary-owned`，`[boundary-unsupported]` 全程 **0**）✓；**guest 真实拿回返回值 0**（`output[0]`，经真实 thunk 往返）✓；**guest 传入的 `mode=2` 原样送达宿主桩** ✓；另断言直调前状态为初值、且直调后 4 个记录值逐项正确 ⇒ 预置/忽略实参的桩无法蒙混 ✓。
  6. **两变体自证**（日志 `.local/logs/p-mm-5-contract-selfproof.log`，只把一处期望值 `7 → 8`）：
     - **变体 A（当前源码形态：真实错误数 + 末尾 `CHECK(!errors)`）** → `[check-fail] oe-focus-direct-focus-handling-mode actual=0x7 expected=0x8`，**exit 1（红）**。
     - **变体 B（修复前形态：去掉末尾 `CHECK(!errors)` 且错误数硬编码 0）** → 同样打印 `[check-fail]`，却 **exit 0（假绿）** 并输出 `"oe_focus_errors":0`。
     ⇒ 证明契约能检出失败、且修复形态不假绿。
  7. **验收 A（真机 v56）**：`.local/logs/rg477m-boot-v56-oe-focus.log`（**55831** 行，exit 133）实测 `[unresolved-import] index=222` **0 次**、`[boundary-unsupported]` **0 次**、`Aborted` **0 次**、`[snapshot-refused]` **0 次**；第 **55810–55813** 行依次为
     ```
     [hle] nn::oe::SetPerformanceModeChangedNotificationEnabled: enabled=1 recorded
     [hle] nn::oe::SetOperationModeChangedNotificationEnabled: enabled=1 recorded
     [hle] nn::oe::SetFocusHandlingMode: mode=2 recorded
     [hle] nn::oe::SetResumeNotificationEnabled: enabled=1 recorded
     ```
     ⇒ **与静态预测的调用顺序、实参值逐一吻合**（这是对「静态邻接 ⇒ 已证实触达」这一判据的首次真机交叉验证）；profile 行 `imports=651 unresolved=381`。
  8. **验收 B（新阻塞）**：第 **55815–55816** 行
     ```
     [fault] signal=5 pc=0x79b6cf1718 fa=0x79b6cf1718
     [unresolved-import] index=227 name=_ZN2nn2oe16GetOperationModeEv caller_offset=0x51dc88
     ```
     exit **133**（SIGTRAP，未解析**代码**导入）。**新阻塞一句话**：guest 调用未解析的 `nn::oe::GetOperationMode()`（import 227，**零实参、有返回值**，2 个调用点 `0x51dc84` / `0x51ee40`，**两处都是无条件调用**）⇒ 运行时无该符号 typed HLE ⇒ SIGTRAP。**下一刀应攻 import 227**；guest 把返回值与 **1** 比较后存成一个字节全局标志，**该标志的语义必须先追到消费点再定值**，不得猜。
  9. **产物与日志**：
     - 构建：`.local/build-p-mm-5/`（exit 0；宿主全量契约 PASS；`manifest.json` 的 `source_sha256` 与工作树逐项一致、无源码漂移；`android_executed` 已置 true 并补 `device_contracts` / `device_boot` 证据块）。
     - 启动二进制 `nso_owned_startup.android` sha256 前 8 位 `67ab6079`（设备端 `p-mm-5-nso_owned_startup.android-67ab6079`）；契约二进制 `guest_process_contract.android` 前 8 位 `857dfae2`；fixture `oe-focus.nso` 前 8 位 `ba78e1b8`（**自造 fixture，非游戏字节**）。
     - 真机全量 boot：`.local/logs/rg477m-boot-v56-oe-focus.log` + 同名 `.exitcode` = **133**；run 目录与参数与 v47–v55 一致；runner 脚本 `.local/selfproof/boot_v56.sh`。
     - 真机契约：`.local/logs/rg477m-contract-p-mm-5-oe-focus.stdout` / `.stderr`（exit 0）。
     - 新工具：`scripts/runtime/scan_import_range.py`（区间→导入清单，**已入 git**）；诊断用 `.local/selfproof/got_probe.c`、`make_mm5_variants.py`、`selfproof_mm5.sh`。
  10. **改动文件清单**（本单自有）：`src/runtime/hle/nso_host.c`、`src/runtime/hle/nso_host.h`、`src/runtime/hle/guest_process.c`、`tests/native/guest_process_contract.c`、`scripts/runtime/build_guest_process_contract.sh`、**`scripts/runtime/scan_import_range.py`（新增工具）**、`coordination/state.json`、`coordination/HANDOFF.md`（本段）、`coordination/STATUS.md`（脚本生成）。
  11. **口径保持诚实**：`imports=651 unresolved=381` —— **仍有 381 个未绑定**。「unresolved-import 计数 = 0」仍仅指 boot 期没有因未解析导入而产生 fault。
  12. **未达标题画面 / 可玩**；未伪造影片库 / 解码器 / 页表 / RSS / 按键状态 / 焦点枚举名；焦点/通知桩**只记录 guest 的真实请求**。

---

# P-MM-4 闭环｜2026-10-24 15:45 Asia/Shanghai — 绑定 `nn::hid::InitializeKeyboard` + `InitializeMouse`（import 234/235），真机**越过 v54 的 SIGTRAP**（unresolved 388 → 386，前沿推进到 `nn::oe::GetCurrentFocusState` import 222）

- **结论**：工单 P-MM-4 目标闭环。`nn::hid::InitializeKeyboard`（import 234）与 `nn::hid::InitializeMouse`（import 235）已在 HLE 层绑定为**如实记录**初始化状态的宿主桩；真机 v55 **不再**因这两个符号触发 `[unresolved-import]` / `[fault] signal=5`，profile 行 `unresolved` 由 **388 → 386**（**正好 2 个**，即本单绑定的两个），新第一致命推进到 **import 222 `nn::oe::GetCurrentFocusState()`**，exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **三处必改点全部落地**：
     - `src/runtime/hle/nso_host.c`：新增 `nso_host_hid_initialize_keyboard()` / `nso_host_hid_keyboard_initialized()` / `nso_host_hid_initialize_mouse()` / `nso_host_hid_mouse_initialized()`；`nso_host_resolve()` 新增两个绑定分支。
     - `src/runtime/hle/nso_host.h`：新增四个声明（记录型桩，**不**伪造任何按键/鼠标数据）。
     - `src/runtime/hle/guest_process.c`：`typed[]` 白名单新增两个 mangled 名；`import_signature()` 新增显式分支 `{arguments=0;returns=0;}`（两个符号共用）。
  2. **ABI 定论（实测，非猜测）**：`_ZN2nn3hid18InitializeKeyboardEv` / `_ZN2nn3hid15InitializeMouseEv` 经 `c++filt` 确认为 `nn::hid::InitializeKeyboard()` / `InitializeMouse()`，**零实参**。**返回类型不能从 mangled 名判定**（Itanium ABI 不编码返回类型），改由调用点定论：`0x51f618` 与 `0x51f61c` 是**紧邻的两个 BL**（同一 HID 初始化序列，**已证实同处一次执行**），两次调用后 w0 均未被消费 ⇒ **void 返回、0 实参**。
  3. **本刀边界（刻意不扩大）**：`nn::hid::GetKeyboardState`（import 236）与 `GetMouseState`（import 237）各只有 1 个调用点，但它们是**每帧输入轮询**、携带真实数据语义；连同 Npad 族（385 `InitializeNpad`、458–463 `GetNpadState`）一并属于「**Android 输入桥**」这把单独的刀，需要真实输入源，**本单不夹带、不伪造**。本单只做「子系统已初始化」的事实记录。
  4. **契约（宿主 + 真机同测）**：新增 `--hid-init` 契约，由构建脚本自造的 `hid-init.nso` fixture 驱动**真实 guest 导入路径**（fixture 自身依次调用**两个**导入）。
     - 宿主 PASS：`{"hid_init_errors":0,"keyboard_resolved":true,"mouse_resolved":true,"direct_ok":true,"native_ran":false,...}`（宿主无 AArch64 执行器，`native_ran=false` = 未观测，未伪造）。
     - **RG477M 真机 PASS**：`{"hid_init_errors":0,"keyboard_resolved":true,"mouse_resolved":true,"direct_ok":true,"native_ran":true,"guest_marker":true,"guest_keyboard_initialized":true,"guest_mouse_initialized":true}`。
     - 覆盖面：白名单门禁 ✓；**签名门禁**通过（真机 stderr `[boundary-ready] imports=2 actual-primary-owned`，`[boundary-unsupported]` 全程 **0**）✓；**guest 真实调用后两个标志均为 true** ✓（证明两个导入都真被调用，而非只有一个）；契约另断言「直调前标志必须为 false」⇒ 预置标志的桩无法蒙混 ✓。
  5. **两变体自证**（日志 `.local/logs/p-mm-4-contract-selfproof.log`，只把一条期望取反）：
     - **变体 A（当前源码形态：真实错误数 + 末尾 `CHECK(!errors)`）** → `[check-fail] hid-init-direct-keyboard`，**exit 1（红）**。
     - **变体 B（修复前形态：去掉末尾 `CHECK(!errors)` 且错误数硬编码 0）** → 同样打印 `[check-fail]`，却 **exit 0（假绿）** 并输出 `"hid_init_errors":0`。
     ⇒ 证明契约能检出失败、且修复形态不假绿。（自证在 `.local/selfproof/` 用副本完成，**工作树源码未被污染**。）
  6. **根因修复（本单最重要的产出）：自造 NSO 的 `.dynamic` 被按位置索引打错 tag**。首轮真机契约 **exit 139（SIGSEGV，`pc=0x0`、`x16=0x0`）**——guest 的第二个 `blr` 跳到了 0。定位链条：
     - 静态核对：fixture 汇编链接后 `adr` 被松弛为单条指令，两个 GOT 目标分别是 0x8200 / 0x8208（与 `adrp+add` 原意一致）；NSO 的 dynstr / dynsym / DT_RELA 三项内容全部正确。
     - **秒级探针**（不改 runtime，只链接已构建的 `.o`，load 后直接读 `nso_base(module)+0x8200/0x8208/0x8230`）：`got[0x8200]=<键盘桩>` ✓ 而 **`got[0x8208]=0`**、`got[0x8230]=0` ⇒ 重定位**没被应用**。
     - dump `.dynamic` 后确认：构建脚本里的 `struct.pack_into('<Q',data,0x2000+5*16+8,24)` 打的**不是 DT_RELAENT（idx 6）而是 DT_RELASZ（idx 5）**，把重定位表从 72 字节（3 项）截成 24 字节（1 项）⇒ 第二个导入的 GOT 槽从未被填 ⇒ 跳到 0。
     - 修复：hid fixture **不再截断** DT_RELASZ（保留基线 72，3 项重定位全部生效），并把两处 patch 的**真实 tag 名**写进注释（movie fixture 的 24 是其单导入设计所必需，保留不动，且其 sha256 仍为 `4a8dec9b…`，**与 P-MM-2 已提交证据逐字节一致**）。
     - 修复后复测：`got[0x8208]=<鼠标桩>` ✓，宿主 hid 与 movie 契约**均 PASS（无回归）**，真机契约 PASS。
  7. **验收 A（真机 v55）**：`.local/logs/rg477m-boot-v55-hid-initializekeyboard.log`（**54556** 行，exit 133）实测 `[boundary-unsupported]` **0**、`Aborted` **0**、`[snapshot-refused]` **0**；第 **54538 / 54539** 行 `[hle] nn::hid::InitializeKeyboard: keyboard subsystem initialised` / `[hle] nn::hid::InitializeMouse: mouse subsystem initialised` 为**真实 guest 调用**；profile 行 `imports=651 unresolved=386`（388 → 386，**正好 2 个**）。
  8. **验收 B（新阻塞）**：第 **54540–54541** 行
     ```
     [fault] signal=5 pc=0x7bd3d606f0 fa=0x7bd3d606f0
     [unresolved-import] index=222 name=_ZN2nn2oe20GetCurrentFocusStateEv caller_offset=0x51da14
     ```
     exit **133**（SIGTRAP，未解析**代码**导入）。**新阻塞一句话**：guest 调用未解析的 `nn::oe::GetCurrentFocusState()`（import 222，**零实参、有返回值**，共 4 个调用点 `0x51da10 0x51ed40 0x51ed54 0x51f1c8`）⇒ 运行时无该符号 typed HLE ⇒ SIGTRAP。**下一刀应攻 import 222**；`nn::oe` 已有 `nso_host_oe_initialize` / `nso_host_oe_get_desired_language` / `nso_host_oe_set_cpu_boost_mode` 先例可照抄。**注意**：该符号**有返回值**，返回类型与「前台 Android 应用应返回哪个焦点态」必须由 4 个调用点 + guest 消费结果的方式判定，不得猜测。
  9. **产物与日志**：
     - 构建：`.local/build-p-mm-4/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-mm-4 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；宿主全量契约 PASS；`manifest.json` 的 `source_sha256` 与工作树逐项一致、无源码漂移；`android_executed` 已置 true 并补 `device_contracts` / `device_boot` 证据块）。
     - 启动二进制 `nso_owned_startup.android` sha256 `420e661f…`（设备端 `p-mm-4-nso_owned_startup.android-420e661f`）；契约二进制 `guest_process_contract.android` sha256 `5430e88c…`（设备端 `p-mm-4-guest_process_contract.android-5430e88c`）；fixture `hid-init.nso` sha256 `b4286388…`（设备端 `p-mm-4-hid-init.nso-b4286388`，**自造 fixture，非游戏字节**）。
     - 真机全量 boot：`.local/logs/rg477m-boot-v55-hid-initializekeyboard.log` + 同名 `.exitcode` = **133**；run 目录 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/`；参数与 v47–v54 一致：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；runner 脚本 `.local/selfproof/boot_v55.sh`。
     - 真机契约：`.local/logs/rg477m-contract-p-mm-4-hid-init.stdout` / `.stderr`（exit 0）。
     - 诊断工具（只读、`.local/` 内，不进 git）：`.local/selfproof/got_probe.c`（GOT 槽直读探针）、`.local/selfproof/make_mm4_variants.py`、`.local/selfproof/selfproof_mm4.sh`。
  10. **改动文件清单**（本单自有）：`src/runtime/hle/nso_host.c`、`src/runtime/hle/nso_host.h`、`src/runtime/hle/guest_process.c`、`tests/native/guest_process_contract.c`、`scripts/runtime/build_guest_process_contract.sh`、`coordination/state.json`、`coordination/HANDOFF.md`（本段）、`coordination/STATUS.md`（脚本生成）。
  11. **口径保持诚实**：`imports=651 unresolved=386` —— **仍有 386 个未绑定**，本单只推进导入边界两层。「unresolved-import 计数 = 0」仍仅指 boot 期没有因未解析导入而产生 fault，**不是**全部导入已绑定。
  12. **未达标题画面 / 可玩**；未伪造影片库 / 解码器 / 页表 / RSS / 按键状态；两个 HID 桩**只记录初始化事实**，不伪造任何输入数据。

---

# P-MM-3 闭环｜2026-10-24 15:35 Asia/Shanghai — 绑定 `nn::oe::SetCpuBoostMode`（import 75），真机**越过 v53 的 SIGTRAP**（exit 133 → 133，前沿推进到 `nn::hid::InitializeKeyboard` import 234）

- **结论**：工单 P-MM-3 目标闭环。`nn::oe::SetCpuBoostMode`（import 75）已在 HLE 层绑定为**如实记录**请求模式的宿主桩；真机 v54 **不再**因该符号触发 `[unresolved-import] index=75` / `[fault] signal=5`，profile 行 `unresolved` 由 **389 → 388**（正好少 1 个绑定），新第一致命推进到 **import 234 `nn::hid::InitializeKeyboard()`**，exit **133**（SIGTRAP，未解析代码导入）。**未达标题画面 / 可玩**。
  1. **三处必改点全部落地**：
     - `src/runtime/hle/nso_host.c`：`nso_host_oe_set_cpu_boost_mode(mode)` + `nso_host_oe_get_cpu_boost_mode()`（记录型桩，**不**伪造任何频率变更）；`nso_host_resolve()` 新增 `_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE` 绑定分支。
     - `src/runtime/hle/nso_host.h`：新增 `typedef int32_t nn_oe_cpu_boost_mode;` + 两个声明。
     - `src/runtime/hle/guest_process.c`：`typed[]` 白名单新增该 mangled 名；`import_signature()` 新增显式分支 `{arguments=1;returns=0;}`。
  2. **ABI 双重独立确认（实测，非推测）**：
     - **clang 往返**：`void nn::oe::SetCpuBoostMode(nn::oe::CpuBoostMode)` 的 mangled 名**逐字节**等于 `_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE`（`c++filt` 与 `clang++ -target aarch64-linux-gnu` 均复核）；LLVM IR 实测形参为 **i32**（w0）。
     - **调用点静态**：全部 **6 处** BL（`0x14a30 0x14d60 0x14de4 0x14e0c 0x2f030 0x2f060`）目标均为 PLT `0x5e67f70`；**6 处无一消费调用后的 w0**（调用后存的是调用前已算好的 `w19`/`wzr`）⇒ 确证 **void 返回、1 个枚举实参**。离线脚本 `resolve_import_callers.py --index 75` 与逐条手工解码互为印证。
  3. **枚举刻意保持不透明**：仓库内**无**权威 `CpuBoostMode` 枚举定义（已全仓检索确认），guest 实测只出现过 **0 / 1** ⇒ 桩按 **不透明 int32** 记录原始值，不臆造枚举名（`CpuBoostMode_None/Emulation/Max` 一类说法未采信）。
  4. **语义边界（不伪造）**：Switch 上该 API 选择标题的 CPU 提升档位；RG477M 的频率策略归 Android governor（写 cpufreq sysfs 需 root，本项目禁止 root）⇒ 桩**只记录并留日志**，**不**改频率、**不**假装已应用。
  5. **契约（宿主 + 真机同测）**：新增 `--oe-cpu-boost` 契约，由构建脚本自造的 `oe-cpu-boost.nso` fixture 驱动**真实 guest 导入路径**（fixture 自身以 `w0=1` 调用该导入）。
     - 宿主 PASS：`{"oe_cpu_boost_errors":0,"resolved":true,"direct_record_ok":true,"native_ran":false,"guest_marker":false,"guest_mode":-1}`（宿主无 AArch64 执行器，`guest_mode:-1` = 本次未观测，未伪造）。
     - **RG477M 真机 PASS**：`{"oe_cpu_boost_errors":0,"resolved":true,"direct_record_ok":true,"native_ran":true,"guest_marker":true,"guest_mode":1}`。
     - 覆盖面：`guest_process_resolve` 返回非 0（白名单门禁）✓；**签名门禁**通过（真机 stderr `[boundary-ready] imports=1 actual-primary-owned`，全日志 `[boundary-unsupported]` 计数 **0** —— 缺 `import_signature` 条目会让 `prepare_boundary_locked()` 拒绝整个 boundary、guest 根本不会跑）✓；真实 guest 调用后读回的模式 == **1**（实参原样送达，未错位）✓；直调宿主再记 **5** —— 两个**不同**取值 ⇒ 忽略参数的桩无法同时通过 ✓。
  6. **两变体自证**（日志 `.local/logs/p-mm-3-contract-selfproof.log`，只改一处期望值 `5 → 6`）：
     - **变体 A（当前源码形态：真实错误数 + 末尾 `CHECK(!errors)`）** → `[check-fail] oe-cpu-boost-direct-mode actual=0x5 expected=0x6`，**exit 1（红）**。
     - **变体 B（修复前形态：去掉末尾 `CHECK(!errors)` 且错误数硬编码 0）** → 同样打印 `[check-fail]`，却 **exit 0（假绿）** 并输出 `"oe_cpu_boost_errors":0`。
     ⇒ 证明该契约确实能检出失败，且修复形态不会假绿。（自证在 `.local/selfproof/` 用副本完成，**工作树源码未被污染**；变体生成器 `.local/selfproof/make_mm3_variants.py`、运行脚本 `.local/selfproof/selfproof_mm3.sh`。）
  7. **验收 A（真机 v54）**：`.local/logs/rg477m-boot-v54-oe-setcpuboostmode.log`（**55517** 行，exit 133）实测 `[unresolved-import] index=74` **0 次**、`index=75` **0 次**、`[boundary-unsupported]` **0 次**、`Aborted` **0 次**、`[snapshot-refused]` **0 次**；第 **52060** 行 `[hle] nn::oe::SetCpuBoostMode: mode=1 recorded (cpu frequency policy stays with the Android governor)` 为**真实 guest 调用**；boot 由 v53 的 52539 行推进到 55517 行（**+2978**）。
  8. **验收 B（新阻塞）**：第 **55499–55500** 行
     ```
     [fault] signal=5 pc=0x70fcbb1750 fa=0x70fcbb1750
     [unresolved-import] index=234 name=_ZN2nn3hid18InitializeKeyboardEv caller_offset=0x51f61c
     ```
     exit **133**（SIGTRAP，未解析**代码**导入）。**新阻塞一句话**：guest 调用未解析的 `nn::hid::InitializeKeyboard()`（import 234，**零实参**），运行时无该符号 typed HLE ⇒ SIGTRAP。**下一刀应攻 import 234**；`nn::hid` 命名空间已有 `nn::hid::VibrationConverter` 的 `import_signature` 条目可照抄先例。**注意**：Itanium mangling **不编码返回类型**，该符号的返回类型须在派工时用与本单同样的双重确认（clang 往返 + 调用点寄存器使用）判定，不得猜测。
  9. **产物与日志**：
     - 构建：`.local/build-p-mm-3/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-mm-3 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；宿主全量契约 PASS；`manifest.json` 的 `source_sha256` 与工作树**逐项一致、无源码漂移**；`android_executed` 已置 true 并补 `device_contracts` / `device_boot` 证据块）。
     - 启动二进制 `nso_owned_startup.android` sha256 `34059f2f40835a6295791c7fc3a8ea9bef0abd4a87f080e21b4e8e751e06f942`（设备端 `p-mm-3-nso_owned_startup.android-34059f2f`）；契约二进制 `guest_process_contract.android` sha256 `19945b8bccfc080077384f3c8974ca04c3d550f39e98e4316625c44b88764bb7`（设备端 `p-mm-3-guest_process_contract.android-19945b8b`）；fixture `oe-cpu-boost.nso` sha256 `aff3f0db9ab51ce74efc540800ab28a606bfa2f55881120dfe9a7be778a7c226`（设备端 `p-mm-3-oe-cpu-boost.nso-aff3f0db`，**自造 fixture，非游戏字节**）。
     - 真机全量 boot：`.local/logs/rg477m-boot-v54-oe-setcpuboostmode.log` + 同名 `.exitcode` = **133**；run 目录 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/`，cwd 正确；参数与 v47/v51/v52/v53 一致：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；runner 脚本 `.local/selfproof/boot_v54.sh`。
     - 真机契约：`.local/logs/rg477m-contract-p-mm-3-oe-cpu-boost.stdout` / `.stderr`（exit 0）。
     - 离线取证：`main.nso` 副本 `.local/nso/main.nso`（sha256 `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`，只读、不进 git）；反汇编切片 `.local/analysis/p-mm-3/`、`.local/analysis/p-mm-4-call/`。
  10. **改动文件清单**（本单自有，共 6 个）：
     - `src/runtime/hle/nso_host.c`（+17）、`src/runtime/hle/nso_host.h`（+14）、`src/runtime/hle/guest_process.c`（+5）、`tests/native/guest_process_contract.c`（+84）、`scripts/runtime/build_guest_process_contract.sh`（+42）、`coordination/state.json`（+34/-…，写占用与 phase 更新）、`coordination/HANDOFF.md`（本段）、`coordination/STATUS.md`（脚本生成）。
  11. **口径保持诚实**：`imports=651 unresolved=388` —— **仍有 388 个未绑定**，本单只推进导入边界一层。「unresolved-import 计数 = 0」仍仅指 boot 期没有因未解析导入而产生 fault，**不是**全部导入已绑定。唯一可靠的推进方式依旧是「修一个 → 跑一次真机 → 看下一个」。
  12. **未达标题画面 / 可玩**；未伪造影片库 / 解码器 / 页表 / RSS；`nn::oe::SetCpuBoostMode` 桩**如实记录 guest 请求**，不调用任何未验证的系统接口、不改设备频率。

---

# P-MM-2 闭环｜2026-10-24 14:55 Asia/Shanghai — 绑定 `movie::SetAllocator`（import 74），真机**越过 v52 的 SIGTRAP**（exit 133 → 133，前沿推进到 `nn::oe::SetCpuBoostMode` import 75）

- **结论**：工单 P-MM-2 目标闭环。`movie::SetAllocator`（import 74）已在 HLE 层绑定为**如实记录并转发 allocator 注册**的宿主桩；真机 v53 **不再**因该符号触发 `[unresolved-import] index=74` / `[fault] signal=5`，profile 行 `unresolved` 由 **390 → 389**（正好少 1 个绑定），新第一致命推进到 **import 75 `nn::oe::SetCpuBoostMode`**，exit **133**（SIGTRAP，未解析代码导入）。**未达标题画面 / 可玩**。
  1. **三处必改点全部落地**（原工单 §4）：
     - `src/runtime/hle/nso_host.c`：新增 `nso_host_movie_set_allocator(alloc, free, realloc, userdata)` + 三个读取入口（`nso_host_movie_get_allocator/_get_deallocator/_get_reallocator/_get_allocator_userdata`），桩体内**只记录不调用** guest 回调；`:2955` 附近新增 `strcmp` 绑定分支。
     - `src/runtime/hle/guest_process.c`：`typed[]` 白名单新增 `_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_`；`import_signature()` 新增 `{arguments=4;returns=0;}`（4 个通用寄存器参数、无返回值 —— ABI 由 clang 往返逐字节复现 mangled 名确认，**不是**「8 个函数指针 + size」）。
     - `src/runtime/hle/nso_host.h`：新增 `movie_alloc_fn / movie_free_fn / movie_realloc_fn` 三个 typedef 与 5 个声明。
  2. **ABI 与调用点复核（离线静态 == 真机动态，逐位吻合）**：侦察 Agent B 的静态结论是 guest 在 `0x1498c` / `0x46aab8c` 两处把**自己模块内的三个函数指针**（module_offset `0x1c4b0` / `0x1c4c0` / `0x13340`）+ `x3 = NULL` 注册进来。真机 v53 实测 `[hle] movie::SetAllocator: alloc=0x731af574b0 free=0x731af574c0 realloc=0x731af4e340 userdata=0x0`，配合日志第 18 行 `[dso-registered] base=0x731af3b000` 反算即 **0x1c4b0 / 0x1c4c0 / 0x13340 / NULL** —— **与静态预测完全一致**。这同时证明：桩收到的四个实参未被错位或截断。
  3. **契约（宿主 + 真机同测）**：新增 `--movie-allocator` 契约（`tests/native/guest_process_contract.c`），由构建脚本自造的 `movie-allocator.nso` fixture 驱动**真实 guest 导入路径**（fixture 自己用 4 个实参调用该导入，三个回调固定在 module offset `0x80/0x84/0x88`）。
     - 宿主 PASS：`{"movie_allocator_errors":0,"resolved":true,"direct_record_ok":true,"native_ran":false,"guest_marker":false,"guest_userdata_null":false}`
     - **RG477M 真机 PASS**：`{"movie_allocator_errors":0,"resolved":true,"direct_record_ok":true,"native_ran":true,"guest_marker":true,"guest_userdata_null":true}`
     - 覆盖面：`guest_process_resolve` 返回非 0（白名单门禁）✓；**签名门禁**通过（真机 stderr 出现 `[boundary-ready] imports=1 actual-primary-owned`，且全日志 `[boundary-unsupported]` 计数 **0** —— 缺 `import_signature` 条目会让 `prepare_boundary_locked()` 拒绝整个 boundary、guest 根本不会跑）✓；真实 guest 调用后读回的三个指针与 `base+0x80/0x84/0x88` **逐项相等**、`userdata` 为 NULL ✓；直接宿主调用时四个实参（含第 4 个 userdata）**原样记录** ✓。
  4. **「故意改坏 → 契约变红」两变体自证**（日志 `.local/logs/p-mm-2-contract-selfproof.log`，只改一处期望值 `0x1111 → 0x2222`）：
     - **变体 A（当前源码，真实错误数 + 末尾 `CHECK(!errors)`）** → `[check-fail] movie-allocator-direct-alloc actual=0x1111 expected=0x2222` + `process contract line=3246: !errors`，**exit 1（红）**。
     - **变体 B（修复前形态：去掉末尾 `CHECK(!errors)` 且错误数硬编码 0）** → 同样打印 `[check-fail]`，却 **exit 0（假绿）** 并输出 `"movie_allocator_errors":0`。
     ⇒ 证明该契约确实能检出失败，且修复形态不会假绿。（自证在 `.local/selfproof/` 用副本完成，**工作树源码未被改动**。）
  5. **验收 A 达成（真机 v53）**：`.local/logs/rg477m-boot-v53-movie-setallocator.log`（52539 行，exit 133）实测 `[unresolved-import] index=74` **0 次**、`[boundary-unsupported]` **0 次**、`Aborted` **0 次**、`[snapshot-refused]` **0 次**；第 **52523** 行 `[hle] movie::SetAllocator: alloc=0x731af574b0 free=0x731af574c0 realloc=0x731af4e340 userdata=0x0` 为**真实 guest 调用**；boot 由 v52 的 52115 行推进到 52539 行。
  6. **验收 B（新阻塞）**：第 **52525–52526** 行
     ```
     [fault] signal=5 pc=0x75c4abf258 fa=0x75c4abf258
     [unresolved-import] index=75 name=_ZN2nn2oe15SetCpuBoostModeENS0_12CpuBoostModeE caller_offset=0x14a34
     ```
     exit **133**（SIGTRAP，未解析**代码**导入）。**新阻塞一句话**：guest 调用未解析的 `nn::oe::SetCpuBoostMode(nn::oe::CpuBoostMode)`（import 75，1 个枚举实参），运行时无该符号 typed HLE ⇒ SIGTRAP。**下一刀应攻 import 75**；`nn::oe` 命名空间已有 `nso_host_oe_initialize` / `nso_host_oe_get_desired_language` 同族先例可照抄。
  7. **产物与日志**：
     - 构建：`.local/build-p-mm-2/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-mm-2 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；宿主全量契约 PASS；`manifest.json` 的 `source_sha256` 与工作树**逐项一致、无源码漂移**；`android_executed` 已置 true 并补 `device_contracts` / `device_boot` 证据块）。
     - 启动二进制 `nso_owned_startup.android` sha256 `994515920078cce98700db71ec4a915b13fe24c191c9778575563957c5db7f74`（设备端 `p-mm-2-nso_owned_startup.android-99451592`）；契约二进制 `guest_process_contract.android` sha256 `da792210a0bc9b5c68db3a8b4ea1818cd9bbfdbe6e463ed74e641ff64084b1a1`（设备端 `p-mm-2-guest_process_contract.android-da792210`）；fixture `movie-allocator.nso` sha256 `4a8dec9bd9bd1cc924c32903a62afbd6863c948974838dea591f752bacaaaa8c`（设备端 `p-mm-2-movie-allocator.nso-4a8dec9b`，**自造 fixture，非游戏字节**）。
     - 真机全量 boot：`.local/logs/rg477m-boot-v53-movie-setallocator.log` + 同名 `.exitcode` = **133**；run 目录 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/`，cwd 正确；参数与 v47/v51/v52 一致：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；runner 脚本 `.local/selfproof/boot_v53.sh`。
     - 真机契约：`.local/logs/rg477m-contract-p-mm-2-movie-allocator.stdout`（exit 0）。
  8. **协调记录（双写处置，第 2 次）**：14:36 项目经理以「MiniMax 空转 >15 分钟、`lsof` 0 句柄」为由派 **P-CC-9 给 Claude Code（PID 94924）** 并接管主树 `nso_host.c` / `guest_process.c`。**该判定基于陈旧观测**：MiniMax 当时正在 `.local/build-p-mm-2/` 与 `.local/selfproof/` 写盘（不在 `src/**`），故 `lsof` 未见句柄，但主树改动一直存在且未提交。**实测 PID 94924 已退出**（`.local/agent-pm/cc-p-cc-9.log` 仅有 `unrecognized_model` 一行，**未写任何仓库文件**），本单五个文件的 mtime 仍为 MiniMax 的 14:37–14:42，**无第三方写入**。→ **未发生实际双写**，本单改动完整保留。教训：派工方判定「写手空转」时，只看 `lsof` 与主树句柄会漏掉正在 `.local/` 产物目录写盘的写手；应以「主树是否有未提交改动 + 该改动是否随时间推进」为准。
  9. **未达标题画面 / 可玩**；本单只推进导入边界一层。未伪造影片库 / 解码器 / 页表 / RSS；`movie::SetAllocator` 桩**如实记录 guest 注册**，不调用 guest 回调、不静默丢弃注册。

---

# 日间调度（P-CC-9 启动｜2026-10-24 14:36 Asia/Shanghai）

- **切换原因**：主路径写手 MiniMax（P-MM-2）空转 —— `.local/scratch/p-mm-2/` 最新实质写盘 **14:10:58**，距本轮 >15 分钟；`lsof` 显示 MiniMax Code PID 66887 **0** 个 FFRS 文件句柄；主树 clean @ `3ac7fc0`；无活跃 FFRS 实现写手。Antigravity PID 54526 仍处 **429** 额度窗口（cwd=`~`）。按优先级 Antigravity→**Claude Code** 改派。
- **阻塞不变**：真机 v52 `movie::SetAllocator` import 74，exit 133（`.local/logs/rg477m-boot-v52-snapshot-thread.log`）。
- **已派**：P-CC-9 攻 `movie::SetAllocator`（复用 P-MM-2 证据）；提词 `coordination/prompts/P-CC-9-movie-setallocator.md`（详情见 `P-MM-2-movie-setallocator.md`）；claude PID **94924**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-9.log`
- **写锁**：本单接管主树 `nso_host.c` / `guest_process.c`；MiniMax 若仍开着请勿并行改同一文件。
- **基线**：设备 RG477M01445122 在线；pak/ucas PASS；**未达**标题/可玩。

---# 三角色并行收口（P-MM-1 派工 + 双写处置｜2026-10-24 14:20 Asia/Shanghai）

- **双写冲突处置**：派工时发现 **Claude Code PID 44703（工单 P-CC-8，与 P-MM-1 同一任务、同一 `guest_process.c`）** 正在写入。**经用户裁定 MiniMax 接管写锁** → SIGTERM 优雅停止该进程（已退出），**保留其未提交 diff 并逐行复核后继承**（未 reset / clean / stash）。其策略实现复核为正确，予以保留；仅另修掉契约假绿缺陷。同源日志留存为 `.local/logs/rg477m-boot-v52-snapshot-thread.P-CC-8-build.log`。
- **P-MM-1 闭环 @ HEAD `5423dac`**：真机 v52 **越过** `[snapshot-refused] reason=primary-thread` → `[va-resource-invalid]` → `Aborted`（exit 134 → **133**）；新阻塞 = **import 74 `movie::SetAllocator`**（SIGTRAP）。宿主 + 真机 `--snapshot-thread` 契约同 JSON PASS。详见下方 P-MM-1 闭环段。
- **下一刀已升格为正式工单**：`coordination/prompts/P-MM-2-movie-setallocator.md`（`movie::SetAllocator`，import 74）。证据附录 `coordination/prompts/P-MM-2-DRAFT-movie-setallocator.md`（侦察 Agent B 只读产出；总控已更正其 v52 日志行号 —— DRAFT 里的行号来自被覆盖的旧 P-CC-8 那份日志，**源码行号有效、日志行号已作废**）。
- **口径修正（排单必读）**：v52 profile 行 `imports=651 unresolved=390` —— **651 个导入里还有 390 个未绑定**。此前「unresolved-import 计数 = 0」指的是**boot 过程中没有因未解析导入而产生 fault**，**不是**全部导入已绑定。未绑定且有 BL 调用点的共 **341** 个 ⇒ 唯一可靠推进方式是「修一个 → 跑一次真机 → 看下一个」。
- **P-MM-NVN 旁路进度**（Agent C，独立 worktree `task/T04-nvn-firstframe`，**未进主树**、未碰设备）：guest 在 `nvnDeviceInitialize` 成功后**立即**调用两个解析函数（`0x142dbe0` / `0x1430e10`），用 `nvnDeviceGetProcAddress` **一次性解析全部 534 个 NVN 名字**，两函数体各含 **534 条 `blr`、0 条 `cbz/cbnz/tbz/tbnz`**（**全程无 NULL 检查**）⇒ **任一名字解析不到即 NULL → SIGSEGV，第一帧必然出不来**。`nvnBootstrapLoader` = **import 353**，guest 仅 5 处调用、只传 2 个名字。证据：`docs/analysis/P-MM-NVN-firstframe-gaps.md`、`evidence/nvn/guest_nvn_resolution.json`（**在 NVN worktree 内**）。建议第一个可派工单元 = 放行 import 353 + 接线 534 名字注册器。

---

# 日间调度（看守｜2026-10-24 14:06 Asia/Shanghai）

- **里程碑**：P-MM-1 已闭环 @ HEAD `5423dac`；真机 **v52** 越过 snapshot-refused（exit 134→133）；新阻塞 `movie::SetAllocator`（import 74，SIGTRAP exit 133）。证据：`.local/logs/rg477m-boot-v52-snapshot-thread.log`。
- **写手**：P-CC-8 Claude PID 44703 已停（启动即 unrecognized_model）；实际闭环由 MiniMax P-MM-1 完成并提交。Antigravity 仍 429。
- **本轮未新派**：`.local/scratch/p-mm-2/` 在 14:05–14:06 仍有实质侦察（ABI/callers/census），主路径实现写手已退但 MiniMax 团队仍在为下一刀取证；为避免再双写 `nso_host.c`/`guest_process.c`，本轮**不**另拉 Claude。下一张目标：`movie::SetAllocator`（及紧随 movie 族按需）。
- **基线**：设备 RG477M01445122 在线；pak/ucas PASS；**未达**标题/可玩。

---

# P-MM-1 闭环｜2026-10-24 14:05 Asia/Shanghai — 内存/资源快照多线程策略：工作线程取快照放行，真机**越过 primary-thread snapshot abort**（exit 134 → 133），新前沿推进到 `movie::SetAllocator`（import 74）

- **结论**：工单 P-MM-1 目标闭环。**真机彻底越过 `[snapshot-refused] reason=primary-thread` → `[va-resource-invalid]` → `Aborted`（exit 134）这条阻塞链**；guest 的 IOThreadPool 工作线程现在能取到内存/资源快照，新第一致命推进到**未解析的代码导入** `movie::SetAllocator`（import 74），exit **133**（SIGTRAP）。**未达标题画面 / 可玩**。
  1. **对被停止同岗 Agent WIP 的逐行复核结论**：`src/runtime/hle/guest_process.c` 新增的 `snapshot_caller_is_primary()` / `snapshot_caller_bound()` 策略**正确，予以保留**。理由：① 主线程路径行为与改动前完全一致（`snapshot_caller_bound` 对主线程返回 `pthread_equal(...)`）；② 工作线程只要是该 guest **已绑定 OS-TLS 记录**、且记录状态为 LIVE/CLEANING，即可读同一份 primary-owned ledger；③ 未绑定的宿主线程仍被拒（`reason=current-process` / `unbound-caller-thread`）且**输出不被改写**；④ **复用既有 `guest_os_tls_report()` API，未新增 guest_os_tls 接口、未改 TLS 隔离语义** —— 取舍正确，保持不变。`guest_os_tls_report(m,NULL,out)` 不空指针解引用（`thread_link` 走到链表尾返回 NULL 槽 ⇒ `GUEST_OS_TLS_NOT_REGISTERED`），且 `current_os_thread==NULL` 时 `snapshot_caller_bound` 已提前返回 0，走不到 report。
  2. **修掉的假绿缺陷（本单必修项）**：`tests/native/guest_process_contract.c` 的 `snapshot_thread_contract()` 原本把错误数**硬编码**为 `"snapshot_thread_errors":0`，且 `CHECK(... && !errors)` 位于**所有 `expect_*` 断言之前** —— 断言失败只打印 `[check-fail]` 行，契约仍返回 0（假绿）。修法三条：(a) 报告改为 `printf(... "%u" ..., errors)` 打印**真实**错误数；(b) 在报告之前补**最终 `CHECK(!errors)`**（现为第 3133 行），任一 `expect_*` 失败真正导致非 0 退出；(c) 报告里各布尔字段也改为**本次运行实测值**（不再写死 `true`）。
  3. **「故意改坏 → 契约变红」自证**（日志 `.local/logs/p-mm-1-contract-selfproof.log`）：只改一处 —— `expect_value("snapshot-worker-used-matches-primary", c.worker_info.used, c.primary_info.used)` → `... c.primary_info.used+1)`。
     - **修复后的源码** → `[check-fail] snapshot-worker-used-matches-primary actual=0x20000 expected=0x20001` + `process contract line=3133: !errors`，**exit 1（红）**。
     - **恢复修复前形态 + 同一处改坏** → 同样打印 `[check-fail]`，却仍输出 `"snapshot_thread_errors":0` 并 **exit 0（假绿）**。
     ⇒ 证明该缺陷是真实存在的假绿，且本单修复确实让它变红。（自证在 `.local/selfproof/` 下用副本完成，**工作树源码未被改动**，HEAD 源码仍是修复后状态。）
  4. **契约（宿主 + 真机同测，同一份 JSON 逐字相同）**：
     - 宿主 PASS（`.local/build-p-mm-1/host-snapshot-thread-contract/stdout.log`）：`{"snapshot_thread_errors":0,"primary_memory_ok":true,"primary_resource_ok":true,"primary_va_ok":true,"bound_worker_memory_ok":true,"bound_worker_resource_ok":true,"bound_worker_va_ok":true,"worker_matches_primary":true,"unbound_caller_refused":true,"unbound_output_untouched":true}`
     - **RG477M 真机 PASS（逐字相同）**：同上（stdout sha256 `a178625ecd80c589b9d704fc96437d12ec7db54316b12a1c81fcc94187bab15f` 两侧一致）
     - 覆盖面：主线程自问 ✓；**已绑定工作线程**取 memory / resource / VA 三类查询均 OK ✓；工作线程读数与主线程**逐项相等**（total / used / baseline / stack_bytes / thread_objects / resource total+used / VA enabled）✓；未绑定宿主线程被拒且输出未改写 ✓。
  5. **验收 A 达成（真机 v52）**：`.local/logs/rg477m-boot-v52-snapshot-thread.log`（52115 行，exit 133）实测
     - `[snapshot-refused] reason=primary-thread` **0 次**；`reason=os-tls-registered-threads` **0 次**；`[va-resource-invalid]` **0 次**；`Aborted` **0 次**；`[query-invalid]` **0 次**；全日志**无任何** `[snapshot-refused]`。
     - **工作线程侧成功**证据：第 **52093** 行 `[snapshot-thread-policy] caller=bound-worker registered_threads=18 quota=1073741824 charged=748843008 stack_bytes=2097152; same primary-owned ledger read under the process mutex`，紧接第 **52094** 行 `[va-resource-policy] total=16777216 raw_metadata_used=3288320 blocks=12845 reported_used=3812608 SDK_margin=0x80000; logical node pool, not Horizon page tables/RSS`（同一 mutex 下读同一份 ledger，数字与主线程一致）。
  6. **验收 B（新阻塞）**：第 **52101–52102** 行
     ```
     [fault] signal=5 pc=0x7a0451b250 fa=0x7a0451b250
     [unresolved-import] index=74 name=_ZN5movie12SetAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_ caller_offset=0x14990
     ```
     exit **133**（SIGTRAP，未解析**代码**导入）；全日志 `unresolved-import` 仅此 1 条（v51 为 0 条）。
     ⇒ **与同岗 Agent 产物结论一致**（其 `.local/build-p-cc-8/` 的 v52 日志同为该符号、同一 index 74、同一 caller_offset `0x14990`、同一 exit 133）；本单已用 `.local/build-p-mm-1/` **自己的产物独立复跑确认**，未直接引用其日志作证据。
  7. **新阻塞一句话**：guest 在模块初始化后调用 **`movie::SetAllocator`**（未解析导入 index 74），运行时无该符号的 typed HLE ⇒ SIGTRAP 退出（exit 133）；**下一刀应攻 `movie::SetAllocator` 及其同族导入**，不是快照线程策略。
  8. **日志限流决策：不做限流（理由）**：实测 v51 全量 boot 中 `[va-resource-policy]` 仅 **2** 次、`[query-port-policy]` 仅 **4** 次、child TLS 绑定 **17** 次；本单 v52 中工作线程快照只触发 **1** 行 `[snapshot-thread-policy]`。运行时本来就对每次查询逐条打点（`[query-port-policy]` / `[va-resource-policy]` / `[page-port-policy]`），再加一行不构成刷屏；加限流反而可能盖掉本单最需要诊断的「哪次快照来自哪个线程」转换。故**保持每次一行、不加限流**。
  9. **产物与日志**：
     - 构建：`.local/build-p-mm-1/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-mm-1 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；宿主全量契约 **156** case PASS，`normal_contract.errors=0`、`recovery_contract.errors=0`；`manifest.json` 的 `source_sha256` 与工作树 **22 项逐项一致，无源码漂移**；`manifest.json` 的 `android_executed` 已置 true 并补 `device_contracts` / `device_boot` 证据）。
     - 启动二进制 `nso_owned_startup.android` sha256 `6533dea46ce9e6e9a2a8fdf4be84d0ba060d794215ad7e8e34b1a1417e8d3ac1`（设备端 `p-mm-1-nso_owned_startup.android-6533dea4`）；契约二进制 `guest_process_contract.android` sha256 `c75dc9cd3aa55431c6b6f8b77099893842ca7c28232c5c8e90bc8c1730ca4375`（设备端 `p-mm-1-guest_process_contract.android-c75dc9cd`）。
     - 真机全量 boot：`.local/logs/rg477m-boot-v52-snapshot-thread.log` + 同名 `.exitcode` = **133**；run 目录 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/`，cwd 正确；参数与 v47/v51 一致：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`。
     - 真机契约：`.local/logs/rg477m-contract-p-mm-1-snapshot-thread.stdout`（exit 0）。
     - **无效首跑（不作证据）**：`.local/logs/rg477m-boot-v52-snapshot-thread.INVALID-cli-order.log`（exit 2；CLI 参数顺序写成 `--boot` 在前，而 `nso_call.c` 要求选项在 mode 之前，已按正确顺序 `--guest-quota … --guest-stack … --guest-resource-quota … --boot` 重跑）。
     - 同岗 Agent 的旧 v52 日志归档为 `.local/logs/rg477m-boot-v52-snapshot-thread.P-CC-8-build.log`（exit 133，仅作对照，非本单证据）。
     - 自证日志：`.local/logs/p-mm-1-contract-selfproof.log`。
  10. **改动文件清单**：`src/runtime/hle/guest_process.c`（策略，保留同岗 Agent WIP 并经复核）、`src/runtime/hle/guest_process.h`（注释同步）、`tests/native/guest_process_contract.c`（新增 `--snapshot-thread` 契约 + 修掉假绿）、`scripts/runtime/build_guest_process_contract.sh`（+1 行接入 `host-snapshot-thread-contract`）、`coordination/HANDOFF.md`、`coordination/state.json`（协调者写）、`coordination/STATUS.md`（脚本生成）。
  11. **未达 / 边界**：标题画面 / 可玩（仍未进呈现阶段）。本单**未**伪造 Horizon 页表/RSS，**未**改 TLS 隔离语义，**未**降级任何「模块身份 / pages ledger / charged 一致性」校验，**未**新增 guest OS TLS 接口，**未**把拒绝改成静默假成功（拒绝仍保留 `[snapshot-refused] reason=…` 可归因日志）。
  12. **提交**：本单提交 `fix(runtime): allow memory/resource snapshots from guest worker threads`（父提交 `3a9092e`，分支 `work/rg477m-native`，**未 push**；本单提交 SHA 见 `git log -1`）。`coordination/prompts/P-CC-snapshot-thread.md`、`P-MM-1-multiagent-supplement.md`、`P-MM-snapshot-thread-policy.md` 三份派工单据为协调者产物，**未**纳入本单提交，仍是 untracked，留给协调者处理。

---

# 日间调度（P-CC-8 启动｜2026-10-24 13:50 Asia/Shanghai）

- **切换原因**：P-CC-7 claude PID 24728 已退出并写闭环 @ HEAD `3a9092e`；真机最新 v51 越过 EventFd/Poll（unresolved-import=0），阻塞 `[snapshot-refused] reason=primary-thread` → `va-resource-invalid` → exit 134；主树 clean、无活跃 FFRS 主路径写手。Antigravity 仍处 429 额度窗口，按优先级改派 Claude Code。
- **里程碑**：新 HEAD `3a9092e`；真机 v47→v51；socket 事件族落地；导入级 unresolved 清零；新前沿为内存快照线程策略。
- **已派**：P-CC-8 攻 snapshot 线程策略（放宽 primary-thread / 评估 os-tls-registered-threads）；提词 `coordination/prompts/P-CC-snapshot-thread.md`；claude PID **44703**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-8.log`
- **基线**：`.local/logs/rg477m-boot-v51-socket-eventfd-final.log` exit 134；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# 日间调度（P-CC-7 启动｜2026-10-24 13:20 Asia/Shanghai）

- **切换原因**：P-CC-6 claude PID 7295 已退出并写闭环 @ HEAD `2120088`；真机最新 v47 越过 stdout/stdio，阻塞 `nn::socket::EventFd`（import 325，exit 133）；主树 clean、无活跃 FFRS 主路径写手。Antigravity 仍处 429 额度窗口（约 12:09 起，重置约再等数小时），按优先级改派 Claude Code。
- **里程碑**：新 HEAD `2120088`；真机 v46→v47；import 186 stdout SIGSEGV 消除；stdio 数据对象+函数族落地；新前沿 EventFd。
- **已派**：P-CC-7 攻 `nn::socket::EventFd`（及紧随 EventFdWrite/Read）；提词 `coordination/prompts/P-CC-socket-eventfd.md`；claude PID **24728**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-7.log`
- **基线**：`.local/logs/rg477m-boot-v47-stdio-stdout.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-CC-7 闭环｜2026-10-24 13:35 Asia/Shanghai — nn::socket 事件族（EventFd/EventFdWrite/EventFdRead/Close/GetLastError/Poll）落地，真机 **0 个未解析导入**，新前沿推进到「工作线程内存快照被拒」

- **结论**：工单 P-CC-7 目标闭环并超额推进。真机**彻底越过 `nn::socket::EventFd`（import 325）**，并连带清掉同族与等待路径上的 `nnsocketPoll`（import 603）：**v50 全量 boot 日志中 `unresolved-import` 计数为 0**，不再有任何未解析导入致命。
  1. **零售 ABI 取证（离线读零售 NSO，不猜）**：用 `scripts/runtime/inspect_nso.py --slice` 读 Nintendo SDK NSO（build_id `0023a1b06130cd795…`，sha256 `9dfd1cc6…`）：
     - `nn::socket::EventFd(unsigned long, EventFdFlags)` @ sdk `0x159b70`（4 字节 thunk）→ `detail::EventFd` @ `0x165df0`：返回 w0 中的描述符，失败返回 -1 并置 errno。
     - `nn::socket::EventFdWrite(int, unsigned long)` @ `0x159be0`：`str x1,[sp,#8] ; mov w2,#8 ; bl write ; cmp w0,#8 ; csetm w0,ne` → 恰好写 8 字节，成功 0 / 失败 -1。
     - `nn::socket::EventFdRead(int, unsigned long*)` @ `0x159b80`：`cbz x1 → -1 ; mov w2,#8 ; bl read ; cmp w0,#8 ; str x8,[x19]` → 恰好读 8 字节，成功 0 / 失败 -1。
     - `nn::socket::detail::Poll(pollfd*, int, int)` @ `0x164c80` 与 `detail::Poll(PollFd*, unsigned long, int)` @ `0x164a80`：`lsl x8, x21, #3` ⇒ **PollFd 为 8 字节**，与 POSIX `struct pollfd`（fd/int、events/short、revents/short）同布局。
  2. **新增可复用取证工具 `scripts/runtime/resolve_import_callers.py`**（本单新增，进 Git）：离线解析 NSO 动态段（DT_JMPREL/DT_RELA）与 16 字节 PLT stub（`adrp x16,page / ldr x17,[x16,#off] / add x16,x16,#off / br x17`），把 **guest module_offset → import index → 导入名** 打通，并可反查某导入的全部调用点。用它定位到 guest 的事件对象实现：`EventFd(0,4)` @ `0x1091254`、`EventFdWrite(fd,1)` @ `0x10912d4`、`EventFdRead(fd,&local)` @ `0x1091308`、`nn::socket::Close(fd)` @ `0x10912ac`、`nn::socket::GetLastError()` @ `0x10912ec`；并确认 `EventFdRead` 的返回值与栈上局部量**被 guest 完全丢弃**（0x10912f0 处 `bl` 后直接返回），guest 只把它当「排空一次事件」。
  3. **HLE 落地（`src/runtime/hle/nso_host.c` / `.h`）**：
     - `nn::socket::EventFd(initval, flags)` → 返回**真实宿主描述符**（不是哨兵）：Linux/Android 用内核 `eventfd(2)`（`EFD_SEMAPHORE|EFD_NONBLOCK`），其他宿主用等价 pipe 后端（同一套可观察语义），因此 `nn::socket::Close` / `nn::socket::Poll` 能直接作用在同一对象上。
     - `nn::socket::EventFdWrite` / `EventFdRead` 按零售语义搬运 8 字节，成功 0 / 失败 -1；读空返回 EAGAIN 并记 `[hle]` 归因日志。
     - `nn::socket::Close`、`nn::socket::GetLastError`（线程局部错误码）、`nn::socket::Poll`（PollFd 数组就地 poll）、`nnsocketPoll`（C 入口，int nfds）。
     - 安全边界：guest 的 `nn::socket` 描述符就是宿主描述符，故 `Close` 拒绝 0/1/2（零售 socket 命名空间不含进程标准流），避免误关运行时自身 stdout/stderr。
     - **语义选择与偏差（已在源码注释中写明）**：① 采用 **auto-reset（EFD_SEMAPHORE）** 语义——guest 每次 post 1、每次 wait 一次读，普通计数型 eventfd 会把两次 post 一次吃掉而丢唤醒；② **读不阻塞**：零售读会阻塞到有信号，HLE 空读返回 EAGAIN，因为 guest 先 poll 再读且丢弃读返回值（0x10912f0 证据），阻塞会把运行时线程挂死；③ `EventFdFlags` 原值打印但**不重新解释**：零售映射在 socket-framework 服务里（无服务回退 `0x165d10` 直接 EOPNOTSUPP），本地无任何产物说明其位含义，guest 只用到一个值 4。
  4. **契约（宿主 + 真机同测）**：新增 `--socket-eventfd` 契约（`tests/native/guest_process_contract.c`），覆盖创建→写→读往返、空读被拒、空指针被拒、**auto-reset 语义（两次 post 两次排空）**、可 poll 性（POLLIN 报位）、关闭。
     - 宿主 PASS：`{"socket_eventfd_contract_errors":0,"create":true,"write":true,"read_roundtrip":true,"read_empty_rejected":true,"null_output_rejected":true,"auto_reset":true,"pollable":true,"close":true}`
     - **RG477M 真机同契约 PASS（JSON 逐字相同）**，且真机走的是内核 `eventfd` 后端（宿主是 pipe 后端，两侧可观察行为一致）。
  5. **真机推进轨迹（RG477M01445122，与 v47 同一套参数）**：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；guest 输入不变（main NSO build_id `736837afd390442ed7c74a7de730e71b`）。
     - **v48**（`.local/logs/rg477m-boot-v48-socket-eventfd.log`，exit 133）：`unresolved` **397 → 391**（6 个新绑定）；`[hle] nn::socket::EventFd: initval=0 flags=0x4 -> descriptor=6` 与 3 次 `EventFdWrite` 真实落地；前沿推进到 `nnsocketPoll`（import 603，caller_offset=0x5e52a84）。
     - **v49**（`.local/logs/rg477m-boot-v49-socket-poll.log`，exit 134）：`unresolved` **391 → 390**；**全日志 `unresolved-import` 计数 = 0**；guest 进入事件等待循环（`nn::socket::Poll` timeout 100 ms 轮询）、起 `IOThreadPool #0/#1` 工作线程、连续映射 4285 段真实内容（合计 556.8 MiB）。
     - **v50**（`.local/logs/rg477m-boot-v50-socket-poll-diag.log`，exit 134）：同 v49 轨迹，加了快照拒绝原因诊断。
     - **v51 = 最终证据**（`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`，exit 134，产物 `.local/build-p-cc-7d/` 与本单提交源码逐项一致）：`unresolved=390`、**`unresolved-import` 计数 0**、`nn::socket::EventFd` 相关 `[hle]` 行 11 条、`[snapshot-refused] reason=primary-thread`。
  6. **新阻塞前沿（已精确定位，非导入问题）**：`[snapshot-refused] reason=primary-thread` → `[va-resource-invalid] status=1 native=22` → `Aborted`（exit **134**）。即 guest 的**工作线程**（`IOThreadPool #0/#1`）调用了 `nn::os::GetVirtualAddressMemoryResourceUsage`，而运行时 `snapshot_locked` 只允许**主线程**取内存/资源快照（`p->thread.state!=THREAD_RUNNING || !pthread_equal(p->thread.native,pthread_self())`），于是拒答并 abort。证据链：v47/v48/v49 中该查询的最后一次成功发生在日志第 42 行、**早于任何子线程注册**（首个 child TLS 绑定在第 18734 行），v50 起在 17 次子线程绑定后失败。
     - 本单为让该拒绝可归因，把 `snapshot_locked` 的拒绝条件改成**逐项命名**（`[snapshot-refused] reason=…`，拒绝语义完全不变），避免只留 `status=1 native=22` 的不可诊断崩溃。
     - **下一张工单建议**：这是**内存/资源快照的线程策略**（`nn::os::GetMemoryInfo` / `GetVirtualAddressMemoryResourceUsage` 的单主线程限制），不是 socket 族问题；要么允许任意线程取快照（底层 state 本来就有锁），要么改为非 abort 的可归因失败。属策略决定，未由本单擅自更改。
  7. **产物与日志**：
     - 构建（最终）：`.local/build-p-cc-7d/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-cc-7d --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；宿主全量契约 PASS；`manifest.json` 的 `source_sha256` 与工作树逐项一致，无源码漂移）。启动二进制 `nso_owned_startup.android` sha256 `6f2f9ecfffdcea6665cafdebd398ada33b64f204ffda1ae006ba2fb227d7cdf6`（设备端 `p-cc-7d-nso_owned_startup.android-6f2f9ec`）；契约二进制 `guest_process_contract.android` sha256 `7e8e9bd2fd82a926239deff9061e497e1889b6ae5849ec8b530c7b13424915ed`（设备端 `p-cc-7d-guest_process_contract.android-7e8e9bd`，已跑 `--socket-eventfd` PASS）。
     - 真机证据：`.local/logs/rg477m-boot-v48-socket-eventfd.log`（exit 133）、`.local/logs/rg477m-boot-v49-socket-poll.log`（exit 134）、`.local/logs/rg477m-boot-v50-socket-poll-diag.log`（exit 134）、`.local/logs/rg477m-boot-v51-socket-eventfd-final.log`（exit 134，最终），各带 `.exitcode`。
  8. **未达**：标题画面 / 可玩（仍未进呈现阶段）。本次只推进导入边界与 socket 事件族；**未**伪造 TLS 握手、证书校验、在线账号、socket 服务或任何网络在线状态；`nn::socket` 的其余族（Socket/Bind/Connect/Send/Recv/SetSockOpt/Select/Fcntl 等）仍未实现，guest 一旦调用即会成为新前沿。

---

# P-CC-6 闭环｜2026-10-24 13:09 Asia/Shanghai — stdout/stderr/stdin 数据导入真机越过，前沿推进至 nn::socket::EventFd（import 325）

- **结论**：工单 P-CC-6 目标闭环。真机**彻底越过 `stdout` 数据导入（import 186）**：v46 的 SIGSEGV 前沿（exit 139）已消除，v47 推进到更远的 **未解析代码导入 `nn::socket::EventFd`（import 325，SIGTRAP/exit 133）**。
  1. **根因复核（不是假设，是可算的证据）**：v46 故障 `fa=0x7b39ee35d0`，而 `0x7b39ee35d0 − 186×8 = 0x7b39ee3000` 恰为页对齐 ⇒ 该地址正是 loader 给**第 186 号数据导入**分配的 `object_traps` PROT_NONE 槽（`nso_loader.c`：`m->imports[i] = (e[4]&15)==1 ? object_traps : import_traps` + `i*8`）。故障指令 `module_offset 0x5e5a678` 反汇编为 `ldr x8, [x8]`，其 x8 来自 `ldr x8, [x8, #0x888]` ⇒ GOT 槽 `module_offset 0x7767888`，而该槽对 `stdout` 的重定位正是 **R_GLOB_DAT、addend 0**（脚本解析 main NSO 动态表确认；`stdin` 0x77678a0、`stderr` 0x7768ee0 同型）。因此 guest 的读法是「GOT → 对象地址 → 对象里的 FILE*」两跳，v46 的第二跳撞上 PROT_NONE ⇒ SIGSEGV。独立佐证：用 NDK clang 编译 `fprintf(stdout,…)` 得到同一形态 `adrp/ldr :got:stdout; ldr x0,[x8]`。
  2. **数据导入落地（关键差异：对象不是函数）**：`nso_host.c` 新增 stdio 段——`stdout`/`stdin`/`stderr` 各绑定到**宿主 `FILE*` 的可写镜像槽地址**（`&stream->object`，镜像副本而非 libc 自身变量，避免 guest 写穿宿主 stdio 状态），日志 `[hle] stdio-object: stdout -> host FILE*=0x… (object address=0x…, writable mirror)`。**绝不返回函数入口或 BRK 哨兵**。
  3. **loader/边界语义修正（此前会直接失败）**：`nso_retarget_imports` 原本只接受 `sym_type∈{0,2}`，STT_OBJECT 会被判 `[retarget-unsupported]` 而整轮边界准备失败；且边界会把每个绑定导入做成**可调用 thunk**，对数据导入是错的。现做法：新增 `nso_symbol_type()`（loader 权威符号类型），`prepare_boundary_locked` 把 STT_OBJECT 导入**排除出可调用绑定与 retarget**，保留 loader 已写入 GOT 的对象地址 ⇒ 日志 `[boundary-data-object] index=186 name=stdout address=0x…; resolved data import kept as host object, not callable`，`[boundary-ready] imports=251 actual-primary-owned; data objects=3`（251 可调用 + 3 数据对象 = 254 已绑定；651−254 = 397 unresolved，与 profile 行一致）。
  4. **stdio 函数族落地**：`printf`/`fprintf`/`sprintf`（变参）、`fflush`/`fwrite`/`fputs`/`fputc`/`puts`（非变参）。**变参 ABI 用本仓库既有的 snprintf 网关模式推广**，而不是直接透传：
     - 新增 `nb_import_variadic1_gateway`（1 个具名参数：printf）与 `nb_import_variadic2_gateway`（2 个具名参数：fprintf/sprintf），在 `native_boundary.S` 用 `NB_VARIADIC_GATEWAY` 宏生成，按 AAPCS64 重建寄存器保存区（`q0-q7@sp+0`、`x1-x7@sp+128`、`__gr_top=sp+184`、`__vr_top=sp+128`、`__gr_offs=-8×(8-named)`、`__vr_offs=-128`、`__stack` = **guest 原始入栈 SP**），并把 `va_list*` 放到具名参数之后的那个参数寄存器。
     - ABI 取证：实测 NDK clang（AArch64）**变参调用不设置 `al`**（无 vector 计数寄存器），且 clang 生成的 `va_start` 保存区布局与网关一致（`x1-x7` 于 +128、`q0-q7` 于 +0、`__gr_offs=-56`（1 具名）/`-48`（2 具名）/`-40`（3 具名，如既有 snprintf））。故宿主一律调用**非变参** `vprintf/vfprintf` 形态，不依赖 `al`。
     - 宿主要求 `v*` 形态（`nso_host_vprintf/vfprintf/vsprintf`），`guest_process.c` 的 `import_signature` 用新 `version=4 / NB_SIG_VARIADIC_STDIO` 声明 1 或 2 个具名 GP 参数；`native_boundary.c` 校验并选择对应网关。
  5. **安全边界（guest 字符串不是宿主可信输入）**：格式串与文本参数先拷进**有界宿主缓冲**（格式串 4096 B、文本 64 KiB）再交给宿主 libc 解析，超界/未终止即拒绝并记账；`sprintf` 无 guest 侧长度，改用有界缓冲生成后再拷出，截断明确记账（guest 仍拿到应有长度）。`vfprintf`/`vsnprintf`（guest 传 musl 风格 `va_list` 指针）**本单未绑定**：guest 侧 `va_list` 布局未经证实前不得当作宿主 `va_list` 透传，宁可留作未解析导入。
  6. **契约（宿主 + 真机同测）**：新增 `--stdio` 契约（`tests/native/guest_process_contract.c`）。宿主 PASS：`{"stdio_contract_errors":0,"objects_bound":true,"object_holds_host_stream":true,"writable_mirror":true,"fwrite_roundtrip":true,"fputs_roundtrip":true,"fputc_roundtrip":true,"puts_roundtrip":true,"fflush_roundtrip":true,"callables_bound":true}`。**RG477M 真机同契约 PASS**（同一 JSON），且在真机上走了**真实 native boundary 网关**：`fprintf` 变参 11 个实参（含 `%.2f` 双精度与 4 个溢出到栈的整型）输出逐字节核对 `contract 7 text 1.50 1 2 3 4 5 6 7 8\n`；`printf`（1 具名）网关输出 `one 42 tail` 均正确 ⇒ 变参尾（含栈溢出区）重建无误。
  7. **真机推进（RG477M01445122，v47 与 v46 同一套参数）**：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；guest 输入不变（main NSO build_id `736837afd390442ed7c74a7de730e71b`）。`unresolved` **408 → 397**（正是 3 数据对象 + 8 函数）。v46 故障点 `module_offset 0x5e5a678` 已越过，guest 继续创建 `HttpManagerThread`（tid 24412）、`atexit` 登记攀升至 13,618。
  8. **新阻塞前沿**：`[fault] signal=5 pc=0x76cd4f3a28 fa=0x76cd4f3a28` + `[unresolved-import] index=325 name=_ZN2nn6socket7EventFdEmNS0_12EventFdFlagsE caller_offset=0x1091278`；退出码 **133**（SIGTRAP，未解析**代码**导入）。⇒ 下一张工单应攻 **nn::socket 族**（`nn::socket::EventFd` 等），而非 stdio 族。
  9. **产物与日志**：
     - 宿主/Android 构建：`.local/build-p-cc-6c/`（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-cc-6c --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0；全量宿主契约 PASS；`manifest.json` 记录 `source_sha256` 与当前工作树**逐项一致，无源码漂移**）。启动二进制 `nso_owned_startup.android` sha256 `e1363149cdce632f5f65d8fb3a53d83086c66d02d565647812f594f7eb33cdbc`；契约二进制 `guest_process_contract.android` sha256 `533023b84e8ecce38e50842e5a6cc6792e3ebca3aaa0a79cd38fb8732ff337da`。
     - 真机证据：`.local/logs/rg477m-boot-v47-stdio-stdout.log` + `.exitcode` = **133**（设备端 `p-cc-6c-nso_owned_startup.android-e1363149`，run 目录 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259/`，cwd 正确）。
     - 契约真机执行：设备端 `p-cc-6c-guest_process_contract.android-533023b8 --stdio` PASS。
     - 栈预算说明：stdio 的格式/文本/输出缓冲（4 KiB / 16 KiB / 16 KiB）跑在 **guest 栈**（宿主线程栈即 guest 栈，2 MiB）上，故取小上限；单次调用最大 scratch 约 20 KiB。
  10. **未达**：标题画面 / 可玩（仍未进呈现阶段）。本次仅推进导入边界，**未**伪造任何 TLS 握手、证书校验、在线账号或 socket 服务；`nn::socket::EventFd` 等 socket 语义未实现。

---

# 日间调度（P-CC-6 启动｜2026-10-24 12:53 Asia/Shanghai）

- **切换原因**：P-CC-5 claude PID 92401 已退出并写闭环 @ HEAD `f9fd30b`；真机最新 v46 越过 `nnsslInitialize`，阻塞 `stdout`（import 186，STT_OBJECT，exit 139）；主树 clean、无活跃 FFRS 主路径写手。Antigravity 仍处 429 额度窗口（约 12:09 起，重置约再等数小时），按优先级改派 Claude Code。
- **里程碑**：新 HEAD `f9fd30b`；真机 v45/v46 越过 nnssl；import 606/608 消除；新前沿 stdio 数据导入。
- **已派**：P-CC-6 攻 stdio 族（`stdout`/`stdin`/`stderr` 对象 + printf/fprintf/fwrite/…）；提词 `coordination/prompts/P-CC-stdio-stdout.md`；claude PID **7295**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-6.log`
- **基线**：`.local/logs/rg477m-boot-v46-nnssl-initialize.log` exit 139；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-23 / P-CC-5 闭环｜2026-10-24 12:34 Asia/Shanghai — nnsslInitialize（import 606）真机越过，前沿推进至 stdout（import 186）

- **结论**：工单 P-AG-23（Antigravity 因 429 额度耗尽中途退出）+ P-CC-5（Claude Code 接力）目标闭环：真机**彻底越过 `nnsslInitialize`（import 606）**，新前沿归因为 `stdout`（import 186，STT_OBJECT 数据导入）。
  1. **接力范围**：agy 已落地 `nnsslInitialize`/`nnsslFinalize` HLE、白名单、`import_signature`、resolve 与 `--nnssl` 契约；P-CC-5 **未重写该桩**，只做三件事：(a) 核对 WIP 与既有产物是否匹配并重编；(b) 按 v44 **同一套** diagnostic port-policy 跑通真机全量 `--boot`；(c) 依零售 SDK 反汇编证据修正 `nnsslFinalize` 的 ABI（`void` → 返回 masked `nn::Result`）。
  2. **产物一致性核对**：`.local/build-p-ag-23/nso_owned_startup.android`（sha256 `9a2fa25651cbc18668527126626568437794bc10b54c6b9023f9b6099f41ecfa`，mtime 12:08:22）晚于全部 nnssl 源码改动（12:05:02–12:06:01），且 `strings` 含 `nnsslInitialize`/`nnsslFinalize` 与 `[hle] nnsslInitialize…` 日志串 → 与当时 WIP 源码匹配，可直接上机。因随后修正了 `nnsslFinalize` ABI，最终以新目录 **`.local/build-p-cc-5/`** 重编（`sh scripts/runtime/build_guest_process_contract.sh --out .local/build-p-cc-5 --toolchain .local/minimal-ndk/toolchain --resource-quota-bytes 16777216`，exit 0），**最终真机证据全部来自该产物**（`nso_owned_startup.android` sha256 `f0d2ec172cb342411f4c7559746bff6f5e34b81db28952ef26c1d25cdf7158e5`）。
  3. **ABI 证据（零售 Nintendo SDK NSO，build_id `0023a1b06130cd795f7bc4613772af0af71c8cfd…`，sha256 `9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9`，`scripts/runtime/inspect_nso.py --slice` 取证）**：
     - `nnsslInitialize` @ sdk module_offset `0x4335e0`（size 24）：`stp x29,x30,[sp,#-0x10]! ; mov x29,sp ; bl <internal> ; and w0,w0,#0x3fffff ; ldp x29,x30,[sp],#0x10 ; ret` —— 调用前**不布置任何参数** ⇒ 零参；返回 w0 为 masked `nn::Result`（bit 0..21 = module|description，0 = 成功）。
     - `nnsslFinalize` @ sdk module_offset `0x433620`（size 24）：**同一形态** ⇒ 同样返回 masked `nn::Result`，**并非 `void`**。
     - `nn::ssl::Initialize()` @ `0x437670`、`nn::ssl::Finalize()` @ `0x4378c0`：亦为 0 参 + 返回 `nn::Result`（成功路径 `mov w0, wzr`）。
     - 影响面：`nb_import_gateway`（`src/runtime/nso/native_boundary.S`）只保存/恢复 `x18/x29/x30/x16`，**不恢复 x0** ⇒ void 桩会把不确定值交给 guest 当 `nn::Result` 读。已把 `nso_host_ssl_finalize` 改为返回 `int32_t 0`，`import_signature` 由 `returns=0` 改为 `returns=1`，契约新增 `finalize_result_zero` 断言；源码注释内嵌上述模块地址与反汇编。
  4. **真机推进轨迹（RG477M01445122，v44 同一套参数）**：
     - 参数：`quota=1073741824 stack=2097152 image=162869248 resource_quota=16777216 guest_argc=0`；guest 输入不变（main NSO build_id `736837afd390442ed7c74a7de730e71b`，sha256 `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`）。
     - `unresolved` 计数 **410 → 408**（正是 `nnsslInitialize` + `nnsslFinalize` 两个新绑定）。
     - `[hle] nnsslInitialize: initialized Nintendo SSL service (stub) -> returning 0` —— import 606 被真机**真实调用并越过**。
     - 引擎随即 `[hle] nnResultIsFailure: res=0x0 -> 0` ⇒ guest 把返回值当 `nn::Result` 判为成功，**反证 `nnsslInitialize` 返回 `nn::Result` 且 0 = 成功**，与 SDK 反汇编一致。
     - LibcurlGc 子线程完成事件同步（SignalEvent / WaitEvent / FinalizeEvent）后继续推进；父线程 `atexit` 登记攀升至 13616，插件 Pak 目录探测继续。
  5. **新阻塞前沿**：
     - `[fault] signal=11 pc=0x789c88f678 fa=0x7b39ee35d0 module_offset=0x5e5a678`
     - `[unresolved-import] index=186 name=stdout caller_offset=0x5e5a874`
     - 退出码 **139**（SIGSEGV）。
     - 归因：`stdout` 在 main NSO dynsym 中 `st_info&0xf == 1`（**STT_OBJECT，数据导入**），loader 对未解析的**数据**导入填 PROT_NONE 的 object sentinel ⇒ guest **读写 `stdout` 对象槽**即 SIGSEGV（与代码分支未解析导入触发 SIGTRAP 不同）。
     - 同族仍未解析：`stdin`(188)、`stderr`(306) 数据对象，以及 `printf`(175)、`fprintf`(307)、`vfprintf`(538)、`fwrite`(263)、`fputs`(532)、`snprintf`(183)、`vsnprintf`(145)、`sprintf`(526) 函数。**下一张工单应攻 stdio 族（3 数据 + 8 函数），而非 ssl 族**（ssl 族在 `stdout` 之前不会成为前沿）。
  6. **契约**：宿主 `--nnssl` PASS `{"nnssl_contract_errors":0,"initialize":true,"finalize":true,"baseline_zero":true,"finalize_result_zero":true}`；RG477M 真机 `--nnssl` 同结果 PASS。
  7. **日志**：
     - 最终证据（= 提交源码构建）：`.local/logs/rg477m-boot-v46-nnssl-initialize.log` + `.exitcode` = **139**（产物 `.local/build-p-cc-5/`）。
     - 过程证据（= P-AG-23 产物，pre-ABI-fix）：`.local/logs/rg477m-boot-v45-nnssl-initialize.log` + `.exitcode` = 139，结论一致。
     - 无效首跑（cwd 错误导致 `cannot open file`）已备份为 `.local/logs/rg477m-boot-v45-nnssl-initialize.INVALID-cwd.log`，不再作为证据。
  8. **未达**：标题画面 / 可玩（仍未进呈现阶段）。未伪造 TLS 握手/证书校验/在线账号；`nnssl*` 仅为生命周期成功桩。

---

# 日间调度（P-CC-5 启动｜2026-10-24 12:26 Asia/Shanghai）

- **切换原因**：P-AG-23 agy PID 78326 已退出；Antigravity 429 额度耗尽（约 12:09，重置约再等 ~5h）；nnssl HLE/契约/Android 构建已落地但未提交，真机全量 boot 未完成。现有 v45 日志无效（`cannot open file`/exit 1）。主树 dirty、无活跃 FFRS 主路径写手。按优先级改派 Claude Code。
- **已派**：P-CC-5 接力完成 nnsslInitialize 真机 boot + 提交；提词 `coordination/prompts/P-CC-nnssl-boot-finish.md`；claude PID **92401**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/cc-p-cc-5.log`
- **基线**：HEAD `4393ed9` + dirty WIP；产物 `.local/build-p-ag-23/nso_owned_startup.android`；阻塞仍 v44 `nnsslInitialize` import 606 exit 133；设备 RG477M01445122；pak/ucas PASS
- **未达**：标题 / 可玩

---

# 日间调度（P-AG-23 启动｜2026-10-24 12:00 Asia/Shanghai）

- **切换原因**：P-AG-22 agy PID 70112 已退出并写闭环 @ HEAD `4393ed9`；真机最新 v44 越过 `nnResultIsFailure`，阻塞 `nnsslInitialize`（import 606，exit 133）；主树 clean、无活跃 FFRS 主路径写手。
- **里程碑**：新 HEAD `4393ed9`；真机 v43→v44；import 607 消除；LibcurlGc 子线程已起并完成事件同步；pak/ucas PASS。
- **已派**：P-AG-23 攻 `nnsslInitialize`（及紧随 nnsslFinalize / ssl 族按需）；提词 `coordination/prompts/P-AG-nnssl-initialize.md`；agy PID **78326**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-23.log`
- **基线**：`.local/logs/rg477m-boot-v44-nn-result-is-failure.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-22 闭环｜2026-10-24 12:00 Asia/Shanghai — nnResultIsFailure（import 607）及 nnResult 族落地，真机推进至 nnsslInitialize

- **结论**：工单 P-AG-22 目标圆满闭环。
  1. 落地最小可用 `nnResultIsFailure` 及 `nnResult*` 族 HLE（Nintendo SDK 规范：`nnResultIsFailure` 返回 `res != 0`，`nnResultIsSuccess` 返回 `res == 0`，`nnResultGetModule` 提取低 9 位 `res & 0x1ff`，`nnResultGetDescription` 提取 13 位描述 `(res >> 9) & 0x1fff`，`nnResultSuccess` 返回 0；清晰打印 `[hle] nnResultIsFailure: res=0x%x -> %u` 等归因日志）。
  2. `guest_process.c` 中 `typed[]` 白名单与 `import_signature`（1 GP arg, 1 return GP register for IsFailure/IsSuccess/GetModule/GetDescription; 0 args, 1 return for Success）完整绑定。
  3. 宿主契约与 RG477M 真机契约测试均通过（`{"nn_result_contract_errors":0,"is_failure_works":true,"is_success_works":true,"get_module_works":true,"get_description_works":true,"success_constant_zero":true}`）。
  4. RG477M 真机实测彻底越过 index 607 `nnResultIsFailure`（被 LibcurlGc 线程创建流程两次成功调用并校验结果为 0/success），引擎继续设置子线程名 `nn::os::SetThreadNamePointer(name='LibcurlGc')`，初始化两组事件（`nn::os::InitializeEvent`，AutoClear），通过 `nn::os::StartThread` 成功唤醒调度子线程，子线程成功绑定专属 guest OS TLS 记录并进入 guest_entry（`0x7454b37450`），父子线程完成同步等待与事件销毁（`SignalEvent` / `WaitEvent` / `FinalizeEvent`）。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x76f4b2b2f0 fa=0x76f4b2b2f0`
  - `[unresolved-import] index=606 name=nnsslInitialize caller_offset=0x5e647e8`（SSL 子系统初始化）
  - 退出码 133（SIGTRAP，未解析导入触发）；基线日志 `.local/logs/rg477m-boot-v44-nn-result-is-failure.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-22 启动｜2026-10-24 11:45 Asia/Shanghai）

- **切换原因**：P-AG-21 agy PID 49431 已退出并写闭环 @ HEAD `c61fbac`；真机最新 v43 越过 `GetCurrentProcessHandle`，阻塞 `nnResultIsFailure`（import 607，exit 133）；主树 clean、无活跃 FFRS 主路径写手（P-CC-4 Claude 43082 已退出）。
- **里程碑**：新 HEAD `c61fbac`；真机 v42→v43；import 105 消除；CreateThread 子线程已起；pak/ucas PASS。
- **已派**：P-AG-22 攻 `nnResultIsFailure`（及紧随 Result 族）；提词 `coordination/prompts/P-AG-nn-result-is-failure.md`；agy PID **70112**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-22.log`
- **基线**：`.local/logs/rg477m-boot-v43-os-process-handle.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-21 闭环｜2026-10-24 11:35 Asia/Shanghai — nn::os::GetCurrentProcessHandle（import 105）落地，真机推进至 nnResultIsFailure

- **结论**：工单 P-AG-21 目标圆满闭环。
  1. 落地最小可用 `nn::os::GetCurrentProcessHandle` HLE（Nintendo SDK 规范：无参，返回当前进程 `nn::os::NativeHandle`，映射宿主当前进程 pid 作为稳定句柄；清晰打印 `[hle] nn::os::GetCurrentProcessHandle -> handle=0x%x (pid=%d)` 归因日志）。
  2. `guest_process.c` 中 `typed[]` 白名单与 `import_signature`（0 args, 1 return GP register）完整绑定 `_ZN2nn2os23GetCurrentProcessHandleEv` 与 `nnosGetCurrentProcessHandle`。
  3. 宿主契约与 RG477M 真机契约测试均通过（`{"os_process_handle_contract_errors":0,"get_current_process_handle":true,"handle_positive":true,"matches_getpid":true}`）。
  4. RG477M 真机实测彻底越过 index 105 `nn::os::GetCurrentProcessHandle`（被 UE 平台初始化流程成功调用并保存进程句柄），引擎继续注册 22 项 atexit 回调（总数攀升至 13,616），持续探测数十个插件 Pak 目录（CriWare、Paper2D、ControlRig、Metasound、Chaos、CommonUI 等），执行多段 4KB/24KB 虚存映射，校验 SDK link anchor（`nn::util::ReferSymbol`），并通过 `nn::os::CreateThread` 成功创建并调度子线程（tid=26311，entry=0x785096f450，stack=0x6eeaa1f000）。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x7af08462f8 fa=0x7af08462f8`
  - `[unresolved-import] index=607 name=nnResultIsFailure caller_offset=0x5e663c0`（子线程执行中调用 `nnResultIsFailure`）
  - 退出码 133（SIGTRAP，未解析导入触发）；基线日志 `.local/logs/rg477m-boot-v43-os-process-handle.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-21 启动｜2026-10-24 11:16 Asia/Shanghai）

- **切换原因**：P-AG-20 agy PID 35135 已退出；HANDOFF 已写闭环 @ HEAD `6e8cbc1`；真机最新 v42 越过 `gettimeofday`/`getpid`，阻塞 `nn::os::GetCurrentProcessHandle`（import 105，exit 133）；主树无活跃 FFRS 主路径写手（P-CC-4 Claude PID 43082 并行 NVN 预研仍活跃，勿杀）。
- **里程碑**：新 HEAD `6e8cbc1`；真机 v41→v42；gettimeofday import 211 消除；pak/VM/事件继续。
- **已派**：P-AG-21 攻 `nn::os::GetCurrentProcessHandle`（及紧随进程/句柄族）；提词 `coordination/prompts/P-AG-get-current-process-handle.md`；agy PID **49431**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-21.log`
- **并行**：P-CC-4 Claude PID 43082 @ `Documents/Codex/2026-10-24/task/ffrs-nvn-firstframe`（`task/T04-nvn-firstframe`）
- **基线**：`.local/logs/rg477m-boot-v42-gettimeofday.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-20 闭环｜2026-10-24 11:10 Asia/Shanghai — gettimeofday（import 211）与 getpid 落地，真机推进至 nn::os::GetCurrentProcessHandle

- **结论**：工单 P-AG-20 目标圆满闭环。
  1. 落地最小可用 `gettimeofday` HLE（POSIX 契约：填充 `struct timeval`，返回 0，timezone 忽略；清晰打印 `[hle] gettimeofday: tv=%p tz=%p -> sec=%ld usec=%ld rc=0` 归因日志）。
  2. 同步覆盖紧随 C 运行时 `getpid`（返回宿主 pid，清晰打印 `[hle] getpid -> %d`）。
  3. `guest_process.c` 中 `is_typed_runtime_export`、`import_signature`（2 args, 1 return GP register for gettimeofday; 0 args, 1 return GP register for getpid）完整绑定。
  4. 宿主契约与 RG477M 真机契约测试均通过（`{"gettimeofday_contract_errors":0,"valid_timeval":true,"tv_sec_positive":true,"tv_usec_bounded":true,"null_tv_rejected":true,"getpid_positive":true}`）。
  5. RG477M 真机实测彻底越过 index 211 `gettimeofday`（先后两次被 UE 成功调用并获取时间戳），引擎继续执行两个虚存映射（57,344 B @ `0x560582a000`、94,208 B @ `0x58ca182000`）、连续完成多段 pak 物理读取（15,251 B、13,513 B、23,992 B、30,288 B、12,734 B）、多线程事件同步等待（`nn::os::SignalEvent` / `nn::os::WaitEvent`），`atexit` 登记回调数攀升至 13,594，成功探测引擎构建版本描述符。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x6ff7e2a348 fa=0x6ff7e2a348`
  - `[unresolved-import] index=105 name=_ZN2nn2os23GetCurrentProcessHandleEv caller_offset=0x7f1f4`（`nn::os::GetCurrentProcessHandle()`）
  - 退出码 133（SIGTRAP，未解析导入触发）；基线日志 `.local/logs/rg477m-boot-v42-gettimeofday.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-20 启动｜2026-10-24 10:54 Asia/Shanghai）

- **切换原因**：P-AG-19 agy PID 26521 已退出；HANDOFF 已写闭环 @ HEAD `902ef4a`；真机最新 v41 越过 `nn::oe::Initialize` / `GetDesiredLanguage`，阻塞 `gettimeofday`（import 211，exit 133）；主树 clean、无活跃 FFRS 写手。
- **里程碑**：新 HEAD `902ef4a`；真机 v40→v41；oe import 80/81 消除；ICU/pak/事件同步继续。
- **已派**：P-AG-20 攻 `gettimeofday`（及紧随时钟族符号）；提词 `coordination/prompts/P-AG-gettimeofday.md`；agy PID **35135**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-20.log`
- **基线**：`.local/logs/rg477m-boot-v41-oe-init.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-19 闭环｜2026-10-24 10:55 Asia/Shanghai — nn::oe::Initialize 与 nn::oe::GetDesiredLanguage 落地与推进

- **结论**：工单 P-AG-19 目标圆满闭环。
  1. 落地最小可用 `nn::oe::Initialize` HLE（无参，基线成功返回 0 / ResultSuccess，清晰归因 `[hle] nn::oe::Initialize: initialized operation environment -> returning 0` 日志）。
  2. 紧随其后的 `nn::oe::GetDesiredLanguage`（dynsym index 81）按 Nintendo SDK `nn::settings::LanguageCode`（8 字节 packed ASCII / NUL-terminated 字符串）以 ARM64 ABI `x0` 返回 `"en-US"`（与零售包 ICU 资源查找 `en_US` 强一致），打清晰 `[hle] nn::oe::GetDesiredLanguage: returning 'en-US'` 归因日志。
  3. `guest_process.c` 中 `is_typed_runtime_export` 与 `import_signature`（0 args, 1 return GP register）完整绑定 `_ZN2nn2oe10InitializeEv` / `nnoeInitialize` 与 `_ZN2nn2oe18GetDesiredLanguageEv` / `nnoeGetDesiredLanguage`。
  4. 宿主契约与 RG477M 真机契约测试均通过（`{"oe_contract_errors":0,"initialize":true,"get_desired_language":true,"baseline_locale":"en-US"}`）。
  5. 真机实测越过 index 80 与 index 81，两次成功完成 `Initialize` 与 `GetDesiredLanguage` 流程，引擎推进跨过本地化区域设置，继续物理读取 pak、多线程事件同步等待（`nn::os::SignalEvent` / `nn::os::WaitEvent`），atexit 登记进一步攀升至 13,593。
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x7668e4c698 fa=0x7668e4c698`
  - `[unresolved-import] index=211 name=gettimeofday caller_offset=0x5920c94`
  - 退出码 133（SIGTRAP，未解析导入导致）；基线日志 `.local/logs/rg477m-boot-v41-oe-init.log`（及 `.exitcode`=133）。
- **未达**：标题画面 / 可玩（仍未进呈现阶段）。

---

# 日间调度（P-AG-19 启动｜2026-10-24 10:38 Asia/Shanghai）

- **切换原因**：P-AG-18 agy PID 88093 已退出；HANDOFF 已写闭环 @ HEAD `15636ca`；真机最新 v40 越过 setlocale / TaskGraph SIGSEGV，阻塞 `nn::oe::Initialize`（import 80，exit 133）；主树 clean、无活跃 FFRS 写手。
- **里程碑**：新 HEAD `15636ca`；真机 v36→v40；TaskGraph SIGSEGV（exit 139）消除；ICU/pak 读推进。
- **已派**：P-AG-19 攻 `nn::oe::Initialize`（及紧随 GetDesiredLanguage）；提词 `coordination/prompts/P-AG-oe-initialize.md`；agy PID **26521**（若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-19.log`
- **基线**：`.local/logs/rg477m-boot-v40-setlocale.log` exit 133；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-18 闭环｜2026-10-24 10:30 Asia/Shanghai — UE TaskGraph worker SIGSEGV 根因修复与 libc++/locale 推进

- **结论**：v36 的 TaskGraph worker `SIGSEGV (signal 11, fa=0x4, module_offset=0x3cb84)` 根因确认为 **子线程（Worker 线程）与 GameThread 共享单个 guest OS TLS 槽 0 记录**（缺少 `_Thread_local guest_os_tls_thread *current_os_thread`）。子线程并发调用 `nn::os::GetTlsValue(slot=0)` / `SetTlsValue` 篡改 `FThreadLocalData` 锁计数器，导致锁计数器下溢为负数（-3）；解锁慢路径 `0x3cb20`（`ldr x20, [x9, #8]`，`x9 = ((-3 & ~1) | 2) & ~3 = -4`）将 `-4 + 8 = 0x4` 当作队列节点指针访问，触发 SIGSEGV `fa=0x4`。
- **修复动作**：
  1. `src/runtime/hle/guest_process.c`：
     - 新增 `static _Thread_local guest_os_tls_thread *current_os_thread;`；
     - `guest_process_bind_child_thread()` 中通过 `guest_os_tls_register(p->os_tls, &current_os_thread)` 为子线程独立注册专属 guest OS TLS 线程记录；
     - `guest_process_unbind_child_thread()` 中通过 `guest_os_tls_cleanup` 与 `guest_os_tls_unregister` 独立解绑与注销；
     - `tls_get()` / `tls_set()` 统一通过 `current_os_thread ? current_os_thread : p->os_thread` 路由，彻底隔离多线程 OS TLS 槽 0 状态。
  2. `tests/native/guest_process_contract.c`：
     - 在 `exercise_os_wrappers` 中新增子线程独立 OS TLS 验证用例，实测子线程初值归零、独立赋值生效、主线程槽位值不被干扰。
  3. **补齐后继 libc++ / C 运行时动态符号**（`src/runtime/hle/nso_host.c` / `nso_host.h` / `guest_process.c`）：
     - `_ZNSt3__15mutex4lockEv` / `_ZNSt3__15mutex6unlockEv`
     - `_ZNSt3__118condition_variable10notify_oneEv` / `_ZNSt3__118condition_variable10notify_allEv` / `_ZNSt3__118condition_variable4waitERNS_11unique_lockINS_5mutexEEE`
     - `_ZNSt3__16chrono12steady_clock3nowEv` / `pthread_cond_clockwait`
     - `_ZNSt3__120__throw_system_errorEiPKc` / `_ZNSt3__122__libcpp_verbose_abortEPKcz`
     - `setlocale`（返回 `"C"`，避免 libc++ 试图从不存在的外部路径加载本地化数据）
     - `getenv` / `nl_langinfo` / `__cxa_pure_virtual`
- **真机复测证据（RG477M01445122）**：
  - 日志：`.local/logs/rg477m-boot-v40-setlocale.log`（退出码 133 / SIGTRAP）；
  - **TaskGraph worker SIGSEGV (`fa=0x4 / module_offset=0x3cb84`) 彻底消除！退出码 139 彻底翻篇**；
  - 虚幻引擎顺利跨过 TaskGraph 启动、ICU 本地化数据加载与资源字典解析、连续物理读取 pak/ucas/romfs（读取 24224 B、4856 B、11474 B、5386 B、216 B、2282 B、2080 B、96 B 等），登记 atexit 回调达 13,591 个！
- **最新阻塞前沿**：
  - `[fault] signal=5 pc=0x7a20cc7280 fa=0x7a20cc7280`
  - `[unresolved-import] index=80 name=_ZN2nn2oe10InitializeEv caller_offset=0x15440`
  - 紧邻其后调用的下一个导入为 `_ZN2nn2oe18GetDesiredLanguageEv`（index 81）。
- **未达**：标题画面 / 可玩（仍未进渲染呈现阶段）。

---

# 日间调度（P-AG-18 启动｜2026-10-24 09:52 Asia/Shanghai）

- **切换原因**：P-AG-17 Claude（PID 46203，会话 fe01711c）已 end_turn 闭环；HEAD 已到 `9c3f24d`；真机 v36 越过 PreInit，阻塞 TaskGraph worker SIGSEGV fa=0x4 / module 0x3cb84（exit 139）；主树无活跃写手。
- **里程碑**：新 HEAD `9c3f24d`；真机 v33→v36；PreInit 不再失败；pak/IoStore 由 UE 自挂。
- **已派**：P-AG-18 攻 UE TaskGraph worker SIGSEGV；提词 `coordination/prompts/P-AG-taskgraph-sigsegv.md`；agy PID **88093**（优先，用户已确认登录；若 --dangerously-skip-permissions 弹出审批请点批准）；日志 `.local/agent-pm/agy-p-ag-18.log`
- **基线**：`.local/logs/rg477m-boot-v36-aoc-event.log` exit 139；设备 RG477M01445122；pak/ucas PASS（`.local/romfs/FFRS/Content/Paks/`）
- **未达**：标题 / 可玩

---

# P-AG-17 闭环｜2026-10-24 09:45 Asia/Shanghai — PreInit() failed 根因修复

- **结论**：v32 的 `LowLevelFatalError [LaunchNintendo.cpp:241] PreInit() failed! Aborting` 根因是**真机 romfs 镜像缺少 `FFRS-Switch.pak` / `FFRS-Switch.ucas`**（宿主 08:03 已物化，但从未推给设备）。补齐后引擎自行挂载 pak/IoStore，PreInit 不再失败，已推进到 UE TaskGraph worker 启动阶段。
- **新阻塞**：guest SIGSEGV（fa=0x4，module offset 0x3cb84）发生在 `Background Worker (Standby #1)`，非未绑定 import、非缺容器。详见下文「下一前沿」。
- **基线日志**：`.local/logs/rg477m-boot-v36-aoc-event.log`（exit 139）；上一版 `.local/logs/rg477m-boot-v35-aoc-event.log` 同一阻塞可复现
- **未达**：标题 / 可玩（仍未出任何一帧）

---

## 1. 根因（已证实，非推测）

| 证据 | 内容 |
|---|---|
| `.local/romfs/Manifest_UFSFiles_Switch.txt:1` | `FFRS/FFRS.uproject` 是 **UFS** 文件 → 存在 pak 内，不是散文件 |
| `FFRS-Switch.pak` TOC 目录树 @ 文件偏移 `0xb058625c` | 条目 `FFRS.uproject` → 数据偏移 `0x39ac0`；同级还有 `AssetRegistry.bin`、`FFRS/Config/*.ini` |
| v32 日志 `18786` | `OpenDirectory 'rom:/FFRS/Content/Paks/' total_entries=3` → 真机只有 utoc/ucas 三件套，**没有 .pak** |
| 真机目录 | `FFRS-Switch.utoc` / `global.ucas` / `global.utoc`；宿主 `.local/romfs/.../Paks/` 有 5 件（.pak/.ucas 08:03 才物化，未推送） |
| 字节一致性 | 物化 .pak 与基线 NSP RomFS 原文**逐字节一致**：末 512 B sha256 `5a8e206fac85ead47ca941bee7da9dfcc5e7d4eb8de4e20addeff6b9754cd48a` 两侧相同；pak 页脚 magic `0x5A6F12E1` 位于 `2958593130`、文件尾 119 B 为零，是**原作固有布局**，不是物化缺陷 |

→ `PreInitPreStartupScreen` 之前找不到 pak → 项目描述符不可达 → `nn::fs::OpenFile('rom:/FFRS/FFRS.uproject')` 失败 → PreInit 返回非零 → 致命错误。

## 2. 修复动作

1. **补齐真机容器**：向 RG477M 推送 `FFRS-Switch.pak`(2,958,593,334 B) 与 `FFRS-Switch.ucas`(5,976,848,928 B)。推送日志 `.local/logs/p-ag-17-push-paks.log`。未删任何 `.local/romfs` 大文件，未 root/刷机/清数据。
2. **子线程故障可归因**（`src/runtime/nso/nso_loader.c`）：原 `fault_module` 是 `_Thread_local`，运行时创建的 guest 子线程没有该作用域 → 崩溃只报一个无归属 PC。改为进程级 `fault_scope_owner`（信号处置本来就是进程级的），并新增 `nso_fault_scope_set_thread_stack()`，由 HLE 发布 **guest 栈区间**（`guest_sp - size .. guest_sp`，guest SP 从栈顶向下增长）→ 子线程也能走 guest 栈帧回溯。
3. **绑定新 import**（`src/runtime/hle/nso_host.c` / `guest_process.c`）：
   - `nn::aoc::GetAddOnContentListChangedEvent(nn::os::SystemEvent*)`（dynsym index 56）
   - `nn::os::WaitSystemEvent` / `SignalSystemEvent` / `DestroySystemEvent`
   - 语义：宿主自己持有**信号状态**，**不改写 guest 对象内存**（保留游戏原 `nn::os::SystemEvent` 布局）；本机无法安装任何追加内容 → 列表永不变化 → 事件恒为未触发，等待方阻塞，与零售卡真机行为一致。
4. **宿主契约**：新增 `--diag-aoc-event`（guest 对象 64 B 调用前后逐字节不变、signal→wait 往返不阻塞、null 入参不崩溃）。宿主与 RG477M 均通过：`{"diag_aoc_event_errors":0,"resolve":true,"guest_object_untouched":true,"signal_wait_roundtrip":true}`。

## 3. 真机复测证据（RG477M01445122）

```
[hle] nn::aoc::GetAddOnContentListChangedEvent: event=0x6352a9a9c0 -> unsignaled (no add-on content can be installed)
[hle] nn::os::WaitSystemEvent: event=0x6352a9a9c0 waiting
[hle] nn::fs::OpenFile: path='rom:/FFRS/Content/Paks/FFRS-Switch.pak' size=2958593334
[hle] nn::fs::OpenFile: path='rom:/FFRS/Content/Paks/FFRS-Switch.utoc' size=12382627
[hle] nn::fs::OpenFile: path='rom:/FFRS/Content/Paks/FFRS-Switch.ucas' size=5976848928
```
- **PreInit 不再失败**：全日志无 `AbortImpl` / `LowLevelFatalError`；UE **自己**打开了 pak（读页脚区 offset 2958172777）、`.utoc`、`.ucas`（尾部 2.4 MB）→ pak/IoStore 挂载由游戏原生代码完成。
- 已创建线程：`GameThread` / `FHeartBeatThread` / `Nintendo AddOnContent Thread` / `Foreground Worker #0` / `Foreground Worker (Standby #0)` / `Background Worker #0` / `Background Worker #1` / `Background Worker (Standby #1)`。
- 退出码 139（SIGSEGV），v35 / v36 同一阻塞可复现。

## 4. 下一前沿（已刻画，未根因）

```
[fault] signal=11 pc=0x7292b87b84 fa=0x4 module_offset=0x3cb84
 x1=0xfffffffffffffffd x8=0xfffffffffffffffe x9=0xfffffffffffffffc
 frame[0..4] module_offset = 0x37b44 / 0x248c4 / 0x25210 / 0xec908 / 0x1aaf8
```
- 崩溃指令 `module 0x3cb84: ldr x20, [x9, #8]`，其中 `x9 = x8 & ~3`、`x8 = (x1 & ~1) | 2`；`x1 = 0xfffffffffffffffd` → 读地址 `0x4` → SIGSEGV。
- 上下文是无锁侵入式双向链表 push（`ldaxr/stxr` + 头指针 tag bit1），即**节点指针被污染**，不是未绑定 import（日志无 `[unresolved-import]`），也不是缺容器。
- 线程：`Background Worker (Standby #1)`（TaskGraph worker 启动阶段）。
- 归属说明：崩溃函数未保存 x30，`frame[0]` 来自上层栈帧，**调用者链不可靠**；要定位需从 `module 0x3cb24` 的调用点反向追。

### 并行排查出的两条「非阻塞」缺失路径
- `rom:/FFRS/Shaders`、`rom:/.nrr/ModuleInfo-Shipping.nrr`：引擎会探测，但 `Manifest_UFSFiles_Switch.txt` 与 `Manifest_NonUFSFiles_Switch.txt` **都不含任何 shader 路径、也没有 .nrr** → 零售包本来就没有，不是可补的宿主文件。零售包的 shader 容器仍未定位，属独立工作项。

---

# 日间调度（P-AG-17 Claude 接力｜2026-10-24 09:16 Asia/Shanghai）

- **切换原因**：agy 31803 空转约 14 分钟日志 0 字节（疑卡审批）；用户要求白天持续推到可玩。
- **已杀**：agy 31803
- **已派**：同提词 P-AG-preinit.md → Claude Code（请求 Fable，网关落 MiniMax-M3.1-Flash）；PID **46203**；日志 .local/agent-pm/cc-p-ag-17.log
- **看守**：已重建「FFRS 日间推进看守」*/15 9–22、「FFRS 夜间推进看守」*/15 1–8
- **未达**：标题 / 可玩

---

# 日间调度（P-AG-17 启动｜2026-10-24 09:02 Asia/Shanghai）

- **切换原因**：P-AG-16 已闭环（HEAD 95d18ef）；agy 7126 已退出；主树无活跃写手；用户 09:02 要求继续推进。
- **已派**：P-AG-17 攻 GEngineLoop.PreInit / PreInitPreStartupScreen；提词 coordination/prompts/P-AG-preinit.md；agy PID **31803**（日志 .local/agent-pm/agy-p-ag-17.log；若 --dangerously-skip-permissions 弹出审批请点批准）
- **基线**：真机 v32 AbortImpl 越过；阻塞 PreInit() failed（exit 132）；设备 RG477M01445122；pak/ucas PASS
- **未达**：标题 / 可玩

---

# 夜守调度（P-AG-16 闭环｜2026-10-24 08:50 Asia/Shanghai）

- **切换原因**：P-AG-16 已闭环；真机 v32 越过 `AbortImpl`；未链接导入 12 彻底消除（signal 5 消除）；成功捕获并归因虚幻引擎致命错误 `LowLevelFatalError: PreInit() failed! Aborting`；新阻塞推进至 UE 致命错误退出指令 `udf #0`（signal 4 / exitcode 132）。主树无活跃 agy/claude 写手。
- **已闭环**：P-AG-16 Antigravity 攻克 AbortImpl 并完成根因归因
- **设备**：RG477M01445122 在线；pak/ucas 已物化 PASS
- **未达**：标题 / 可玩

---

# Antigravity接力入口：越过 AbortImpl，归因 PreInit() failed!（P-AG-16）

## 调度备注（夜守 PM）
- **当前阻塞**：虚幻引擎初始化失败 `GEngineLoop.PreInit()` 返回错误码，触发 `LaunchNintendo.cpp:241` 的 `LowLevelFatalError [PreInit() failed! Aborting]`，经由 `FOutputDeviceError::Serialize` 调用 `AbortImpl()` 后进入致命退出指令 `0x11f8: udf #0`（signal 4, exitcode 132）。
- **基线 HEAD**：本轮提交检查点（`nn::diag::detail::AbortImpl` & `nn::diag::GetBacktrace` HLE 已过，PreInit 根因归因）；日志 `.local/logs/rg477m-boot-v32-abortimpl.log`（exitcode 132）
- **P-AG-16**：已闭环（`AbortImpl` 与 `GetBacktrace` 最小可归因 HLE 落地并接入 Native Boundary 网关；真机实测越过 import 12，消除未链接导入 signal 5；完整捕获寄存器快照、栈帧回溯及 UTF-16 致命错误内容；彻底定位到 `GEngineLoop.PreInit()` 失败逻辑）；无其他并行 agy worker
- **阶段**：`T02_ABORT_IMPL_PASSED_PREINIT_ATTRIBUTED_ON_RG477M`
- **未达**：标题画面 / 可操作场景（勿伪称可玩）

本轮按工单 P-AG-16 推进方案 A（《最终幻想：共鸣》Switch→RG477M Android ARM64 原生移植），严格保护密钥与敏感数据，严禁FC27或无依据改动。

## 最新重大突破

1. **`nn::diag::detail::AbortImpl` 与 `nn::diag::GetBacktrace` 最小可归因 HLE 落地**：
   - 在 `src/runtime/hle/nso_host.c` 落地安全探活与可归因诊断 HLE；
   - 捕获完整寄存器快照（`x0`, `x19`, `x20`, `x21`, `x22`, `x8`, `fp`, `lr`）；
   - 安全探活并遍历 Frame Pointer 链表，输出完整的调用栈回溯（Frame 0～Frame 10）；
   - 探活 `x22` 指向的 UTF-16 错误描述字符串并转码输出为可读文本；
   - 伴生落地 `_ZN2nn4diag12GetBacktraceEPmi`（`nso_host_diag_get_backtrace`）；
   - 在 `guest_process.c` 中将两符号登记入 `typed` 白名单，并在 `import_signature`（0 参 1 返 与 2 参 1 返，`register_only`）绑定，GameThread 及衍生子线程无缝复用 Native Boundary 统一网关。

2. **深度逆向与调用点根因归因**：
   - 符号调用点位于 `main.nso` 偏移 `0x11b8`，属于虚幻引擎的 `FOutputDeviceError::Serialize` 虚方法（vtable `0x67d8c50` 的偏移 `0x10`）；
   - 栈帧回溯显示：
     - `frame[1]`: `ret=0x73fb3c81bc`（偏移 `0x11bc`，`FOutputDeviceError::Serialize`）
     - `frame[2]`: `ret=0x73fb5a9154`（偏移 `0x1e2154`）
     - `frame[3]`: `ret=0x73fb5399f0`（偏移 `0x1729f0`，`LowLevelFatalError` 格式化）
     - `frame[4]`: `ret=0x73fe8f6884`（偏移 `0x352f884`，`LaunchNintendo.cpp` 第 241 行）
   - 代码逻辑定位：
     - `LaunchNintendo.cpp` 中调用 `GEngineLoop.PreInit(CmdLine)`（位于偏移 `0x3530700`）；
     - `0x3530700` 调用内部 `FEngineLoop::PreInitPreStartupScreen`（偏移 `0x3530920`）；
     - `PreInitPreStartupScreen` 返回了非零错误码；
     - `0x352f8c0: cbnz w0, #0x352f96c` 判定返回非零直接跳转至致命错误分支（Line 241）；
     - `0x352f97c: mov w1, #0xf1`（`0xf1 = 241`，即源码第 241 行）调用致命错误上报；
   - 提取到完整错误信息：
     ```text
     LowLevelFatalError [File:.\../Platforms/Nintendo/Source/Runtime/Launch/Private/LaunchNintendo.cpp] [Line: 241] 
     PreInit() failed! Aborting
     ```
   - 破译 v31 日志中 `x0=0x6259` 的来源：`0x6259` 为十进制 `25177`，正是 `GameThread` 的真实线程 ID，是 `0x1e3f10` 处调用 `nn::os::GetThreadId` 获取的，并非 `nn::Result` 错误码。

3. **宿主与真机契约全绿**：
   - 增加 `--diag-abort` 独立契约测试用例 `diag_abort_contract`，覆盖符号动态解析、AbortImpl 安全返回 0、GetBacktrace 边界与深度探查校验；
   - `./scripts/runtime/build_guest_process_contract.sh` 编译并跑通全量 37 项宿主契约（含 `host-diag-abort-contract`，returncode 0）；
   - 在 RG477M (`RG477M01445122`) 上运行 `./guest_process_contract.android --diag-abort`：
     `{"diag_abort_errors":0,"resolve":true,"return_zero":true,"backtrace":true}`；
   - 复跑 `--cxa-thread-atexit`、`--posix-memalign`、`--os-event`、`--threads` 与 `--guest-tp` 均全绿通过。

4. **真机实测重大突破：越过 import 12，消除 signal 5，捕获致命错误现场**：
   - 运行 `./nso_owned_startup.android main.nso --guest-quota 1073741824 --guest-stack 2097152 --guest-resource-quota 16777216 --boot`：
   - 日志实证（`.local/logs/rg477m-boot-v32-abortimpl.log`）：
     ```text
     [hle] nn::fs::OpenFile: path='rom:/FFRS/FFRS.uproject' mode=1 (host_path='romfs/FFRS/FFRS.uproject') -> open failed errno=2 (No such file or directory)
     [crt] atexit registered callback=0x73fb58f340 pending=13568; deferred
     [page-port-policy] op=map address=0x6bdb220000 bytes=65536 result=0 after real VM/accounting transition
     [hle] __cxa_thread_atexit: dtor=0x73fb5b7440 obj=0xb40000754068be40 dso=0x7402ae2a00 pending=3
     [hle] nn::diag::detail::AbortImpl: lr=0x5603b5b368 fp=0x6e3b680690 x0=0x4685 x19=0x74030579e8 x20=0x6e3b680690 x21=0x5 x22=0x6e3b680878 x8=0x740300caa4
     [hle]   frame[0]: fp=0x6e3b6806f0 ret=0x5603b5b368
     [hle]   frame[1]: fp=0x6e3b680730 ret=0x73fb3c81bc
     [hle]   frame[2]: fp=0x6e3b680ca0 ret=0x73fb5a9154
     [hle]   frame[3]: fp=0x6e3b680de0 ret=0x73fb5399f0
     [hle]   frame[4]: fp=0x6e3b680fa0 ret=0x73fe8f6884
     [hle]   frame[5]: fp=0x740a52f940 ret=0x5603b5ba34
     [hle]   frame[6]: fp=0x740a52fb20 ret=0x5603b4fc8c
     [hle]   frame[7]: fp=0x740a52fb50 ret=0x76a0752cd0
     [hle]   frame[8]: fp=0x740a52fb80 ret=0x76a06e6b04
     [hle]   frame[9]: fp=0x0 ret=0x0
     [hle] nn::diag::detail::AbortImpl message: "LowLevelFatalError [File:.\../Platforms/Nintendo/Source/Runtime/Launch/Private/LaunchNintendo.cpp] [Line: 241] 
     PreInit() failed! Aborting


     "
     [hle] nn::diag::detail::AbortImpl: lr=0x5603b5b368 fp=0x6e3b680680 x0=0x4685 x19=0x74030579e8 x20=0x6e3b680680 x21=0x5 x22=0x6e3b680878 x8=0x740300caa4
     [hle]   frame[0]: fp=0x6e3b6806e0 ret=0x5603b5b368
     [hle]   frame[1]: fp=0x6e3b680710 ret=0x73fb3c81f8
     ...
     [fault] signal=4 pc=0x73fb3c81f8 fa=0x73fb3c81f8
     ```
   - 未链接导入符号 12 彻底消除，GameThread 顺利越过 `AbortImpl`；
   - 进程执行退出路径并达到硬断言 `0x11f8: udf #0`，退出码由 133 变为 132。

5. **明确未完成项与下一前沿标注**：
   - **下一前沿**：虚幻引擎 `FEngineLoop::PreInitPreStartupScreen`（偏移 `0x3530920`）执行失败并返回非零错误码；需进一步分析其失败原因（检查配置文件如 `rom:/Engine/Config/StagedBuild_FFRS.ini`、Pak / IoStore 挂载与解密、或渲染/音频子系统预初始化等）；
   - `ProtectRelro` 当前仍为桩函数日志记录，未真正实施页面写保护；
   - 尚无标题画面 / 可操作场景证据；
   - NVN→Vulkan 完整桥接、大文件 Pak/IoStore（2.9GB/5.9GB）读取管线未完成。

---

## 阶段验收证据汇总

| 检查项 | 状态与依据 |
|---|---|
| **全量构造函数 (1890/1890)** | **已通过**：构造函数 1890/1890 顺利执行完毕返回 0。 |
| **nnMain 游戏主入口** | **已越过并真机通过**：成功进入 offset `0x352f160`。 |
| **CreateThread / StartThread** | **已越过并真机通过**：成功派生 GameThread 与 FHeartBeatThread，独立 guest TP 绑定无崩溃。 |
| **nn::fs::SetAllocator** | **已越过并真机通过**：成功保存 UE 分配器回调。 |
| **nn::fs::QueryMountRomCacheSize** | **已越过并真机通过**：成功写出 64KB 缓存尺寸。 |
| **nn::fs::MountRom** | **已越过并真机通过**：成功挂载本地 `romfs` 目录返回 0。 |
| **nn::fs::OpenFile / GetFileSize / ReadFile / CloseFile** | **已越过并真机通过**：实测完整打开、查询、读取 `rom:/UECommandLine.txt`（38B）并关闭。 |
| **nn::aoc::CountAddOnContent / ListAddOnContent** | **已越过并真机通过**：实测 Count 返回 0，List 返回 0 项，顺利越过。 |
| **nn::nifm::Initialize** | **已越过并真机通过**：成功初始化网络接口管理返回 0。 |
| **nn::socket::Initialize** | **已越过并真机通过**：成功初始化 6MB 缓冲区套接字系统返回 0。 |
| **nn::account::Initialize** | **已越过并真机通过**：成功初始化账号子系统返回 0。 |
| **nn::fs::OpenDirectory / GetDirectoryEntryCount / ReadDirectory / CloseDirectory** | **已越过并真机通过**：实测成功打开 `rom:/` 与 `rom:/FFRS/Content/Paks/`，读取条目并安全关闭，GameThread 顺利越过！ |
| **nn::os::InitializeEvent / FinalizeEvent / SignalEvent / ClearEvent / WaitEvent** | **已越过并真机通过**：实测成功初始化 Event（AutoClear 与 ManualClear），跨线程 Signal/Wait 唤醒全部通过！ |
| **posix_memalign / memalign** | **已越过并真机通过**：实测成功完成 64 字节对齐分配（40 字节），GameThread 顺利越过！ |
| **__cxa_thread_atexit / __cxa_thread_atexit_impl** | **已越过并真机通过**：实测 3 次成功登记线程局部析构器，GameThread 顺利越过！ |
| **nn::diag::detail::AbortImpl / GetBacktrace** | **已越过并真机通过**：实测越过 import 12，消除未链接导入 signal 5；完整捕获寄存器快照、栈帧回溯及 UTF-16 致命错误内容，彻底定位到 `GEngineLoop.PreInit()` 失败逻辑！ |
| **当前阻塞前沿** | **确证为虚幻引擎 `GEngineLoop.PreInit()` 失败**（偏移 `0x3530920` 返回错误码，触发 `LaunchNintendo.cpp:241` 的 `LowLevelFatalError: PreInit() failed! Aborting`，最终到达 `0x11f8: udf #0`，signal 4 / exitcode 132，日志 `.local/logs/rg477m-boot-v32-abortimpl.log`）。 |

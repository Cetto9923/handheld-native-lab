# NVN API Surface Analysis & Vulkan 1.3 Translation Architecture

**任务 ID**: T04-nvn  
**角色**: Agent E (图形系统工程师)  
**目标对象**: 《最终幻想：共鸣 / FINAL FANTASY RESONANCE》(TitleID `0100B8A026F14000`, 内部代号 `mucha`)  
**状态**: 交付就绪 (READY_FOR_INTEGRATION)  
**关联工件**:
- 符号清单：`evidence/nvn/main_nvn_ids.txt` (535 个函数符号)
- 接口定义：`src/graphics/nvn/include/nvn/nvn.h`
- 动态分发基础设施：`src/graphics/nvn/src/nvn_dispatch.c`

---

## 1. 核心逆向事实与直接证据

1. **唯一动态入口 `nvnBootstrapLoader`**：
   - 对 `main.bin` 动态导入符号表的逐项审查证实，游戏可执行文件本身**仅动态导入一个 NVN 符号**：`nvnBootstrapLoader`。
   - 所有其余 534 个图形 API 函数（从 `nvnDeviceInitialize` 到 `nvnQueuePresentTexture`）均通过向 `nvnBootstrapLoader` 传入函数名字符串，在游戏引擎（UE 5.6.1 NVNRHI）初始化时动态获取函数指针。
   - 这意味着：**宿主无需在链接期导出庞大的伪动态库，只需实现一个高内聚、线程安全的 `nvnBootstrapLoader` 字符串分发器，即可完全拦截并重定向所有图形调用至 Vulkan 1.3。**

2. **完整 API 覆盖度与分类统计**：
   从 `main.bin` 提取出的 535 个 NVN API 符号分类如下：
   - **Device & Queue 管理 (38 个)**：`nvnDeviceInitialize`, `nvnDeviceFinalize`, `nvnQueueInitialize`, `nvnQueueSubmitCommands`, `nvnQueuePresentTexture` 等。
   - **CommandBuffer 录制与状态绑定 (142 个)**：`nvnCommandBufferBeginRecording`, `nvnCommandBufferEndRecording`, `nvnCommandBufferBindPipeline`, `nvnCommandBufferDrawArrays`, `nvnCommandBufferDrawElements` 等。
   - **MemoryPool & Buffer (56 个)**：`nvnMemoryPoolInitialize`, `nvnMemoryPoolMap`, `nvnBufferInitialize` 等。
   - **Texture & Sampler (112 个)**：`nvnTextureInitialize`, `nvnTextureWriteTexels`, `nvnSamplerInitialize` 等。
   - **Window & Swapchain (28 个)**：`nvnWindowInitialize`, `nvnWindowSetCrop`, `nvnWindowAcquireTexture`, `nvnWindowPresentTexture` 等。
   - **Sync & Event (32 个)**：`nvnSyncInitialize`, `nvnSyncWait`, `nvnEventInitialize` 等。
   - **Builder 属性配置类 (127 个)**：纯 CPU 端的配置结构填充（如 `nvnDeviceBuilderSetFlags`, `nvnTextureBuilderSetWidth`），开销极低。

---

## 2. 关键硬件特性映射：RG477M Mali-G615 MC6

| 特性需求 | Nintendo Switch (Tegra X1) | RG477M (Mali-G615 MC6) | 适配与转换策略 |
|---|---|---|---|
| **API 接口** | NVN (专有 C API) | Vulkan 1.3 (标准驱动) | 轻量直接 C 调度转换层 (`nvn_dispatch`) |
| **Bindless 纹理** | NVN 独立纹理句柄 (Texture Handle) | `VK_EXT_descriptor_indexing` | 启用 `runtimeDescriptorArray` 与 `descriptorBindingPartiallyBound`，将 NVN 64位句柄映射为全局描述符池索引 |
| **ASTC 压缩纹理** | 硬件原生支持 (全规格 4x4 ~ 12x12) | 硬件原生支持 (Mali 原生 ASTC LDR/HDR) | **零转码开销**。直接透传 `VK_FORMAT_ASTC_*`，无需桌面 GPU 的解包/计算着色器回退 |
| **专有格式 0x25A5A** | Tegra Block Linear 专有排布 | Vulkan 线性/瓦片优化排布 | 经 Ryujinx 逆向对照，0x25A5A 为 ASTC 4x4 sRGB 专有瓦片模式；解析时剥离 Tegra 特殊位，绑定为标准 ASTC 4x4 sRGB |
| **分辨率与视口** | 1280x720 / 1920x1080 | 1280x960 4:3 物理屏 (内部 1280x720 居中) | `nvnWindowInitialize` 绑定 Android `ANativeWindow`，交换链配置为 1280x720 居中缓冲，上下填充 120px 纯黑剪裁区 |

---

## 3. NVN -> Vulkan 1.3 转换层架构设计

```
+-------------------------------------------------------------------+
|               FFRS / Unreal Engine 5.6.1 NVNRHI                   |
+-------------------------------------------------------------------+
                                  |
                   nvnBootstrapLoader("nvn...")
                                  v
+-------------------------------------------------------------------+
|               libnvn_vulkan.so (Translation Layer)                |
|  - nvn_dispatch.c: 字符串哈希表分发 (535 API)                    |
|  - nvn_device.c:   VkInstance, VkPhysicalDevice, VkDevice         |
|  - nvn_window.c:   VkSurfaceKHR, VkSwapchainKHR (ANativeWindow)   |
|  - nvn_cmd.c:      VkCommandBuffer 翻译与 Draw Call 映射           |
|  - nvn_res.c:      VkImage (ASTC), VkBuffer, VkDescriptorSet     |
+-------------------------------------------------------------------+
                                  |
                                  v
+-------------------------------------------------------------------+
|               Mali-G615 Vulkan 1.3 Driver (Android 14)            |
+-------------------------------------------------------------------+
```

### 3.1 核心设计原则
1. **轻量与无状态抽象**：不同于通用模拟器（需要模拟 Tegra GPU 寄存器与宏引擎），UE 5.6.1 的 NVNRHI 是在 API 抽象层编码，因此转换层只需将 NVN C API 直接一对一翻译为 Vulkan 1.3 C API，无额外寄存器虚拟化损耗。
2. **命令缓冲直通**：NVN CommandBuffer 的 Push/Draw 命令直接录制到底层 `VkCommandBuffer` 中，在 `nvnQueueSubmitCommands` 时批量提交。
3. **内存直接映射**：Switch 的统一内存架构与 Android 移动端（Mali Unified Memory）完全吻合，`nvnMemoryPool` 直接通过 `vkAllocateMemory` 分配 `HOST_VISIBLE | HOST_COHERENT` 或 `DEVICE_LOCAL` 内存。

---

## 4. 下步推进计划 (对接 T04 启动链)

1. **Phase 1 (基础握手)**：在 `nso_loader` 中将 `nvnBootstrapLoader` 绑定至 `nvn_dispatch`，完成 Device, Queue, Window 的无报错创建。
2. **Phase 2 (呈现管线)**：对接 Android `ANativeWindow` 渲染表面，实现首帧 `ClearColor(0.0, 0.0, 0.0, 1.0)` 与黑边 1280x720 视口呈现。
3. **Phase 3 (着色器与管线)**：对接 UE 5.6.1 的着色器字节码转换，加载标题画面基础着色器。

---

## 5. 运行证据修正（2026-10-24，RG477M boot v62）

本节由 RG477M 真机运行证据补正，不是新的设计主张。完整推导见
`docs/analysis/nvn_device_requirements.md`，NVN 名字清单见
`evidence/nvn/guest_nvn_code_references.tsv`。

1. **分发是两级，不是一级**：本文件第 1 节说其余 534 个 API「通过向
   `nvnBootstrapLoader` 传入函数名字符串」获取。真机证据显示不是这样：
   guest 只向 `nvnBootstrapLoader` 询问 **2 个**名字
   （`nvnDeviceInitialize` 与 `nvnDeviceGetProcAddress`，共 5 个调用点），
   其余 534 个名字是**通过 bootstrap 交出来的 `nvnDeviceGetProcAddress`
   按名解析**的（guest 侧调用点 `0x1435464` / `0x143547c` / `0x1430e10`）。
   ⇒ 桥接层需要 **bootstrap（2 个名字）+ device 级 GetProcAddress（534 个名字）**
   两级分发；把 534 个名字放进 bootstrap 的平表里将永远不会被查到。
2. **guest 不检查 GetProcAddress 的返回值**（`0x1430e10` 直接 `str` 不判空），
   所以该函数对 guest 会问的名字**不能返回 NULL**，否则 guest 跳到 `pc=0`。
   命名空间外的名字才可以拒。
3. **`nvnDeviceBuilder` 布局不是阻塞项**：guest 的 builder 是它自己栈帧
   `sp+0x00..0x3f`（64 字节）里的一块内存，guest **从不读写其字段**，只把地址
   交给 `nvnDeviceBuilderSetDefaults` / `nvnDeviceBuilderSetFlags` /
   `nvnDeviceInitialize` 三个 NVN 函数。⇒ 自研设备**可以自定义自己的 builder
   布局**，只要不超出 64 字节、且自身的 setter/getter/initialize 互相自洽。
   本条**不**声称还原了任天堂的真实布局，只说明它对 guest 不可观测。
4. **`nvn_dispatch.c` 现有两处桩不能直接集成**：
   `stub_nvnDeviceInitialize` 返回 **1**（谎报设备初始化成功），而
   `nvnBootstrapLoader` 的 fallback 返回 NULL（会让 guest 跳 `pc=0`）。
5. **本运行时的 NVN 后端是"空设备"**：`nvnDeviceInitialize` 报失败，
   其余入口存在但什么都不做，guest 因此在自己的 `LowLevelFatalError`
   （`NVNManager.cpp:1246`）处停下。**没有设备、没有队列、没有表面、没有一帧。**

# T03A: Android 交叉编译工具链与 RG477M 硬件/图形能力探测器构建报告

## 1. 任务概述与目标

- **任务编号**: T03A
- **任务目标**: 为 Anbernic RG477M 掌机建立 Android ARM64 交叉编译工具链与构建管线，开发专用硬件及图形能力检测 CLI 工具 `probe_rg477m`，全面检测 Vulkan 1.3、bindless 特性、Mali 驱动、4KB/16KB 内存页面大小、屏幕分辨率与手柄输入。
- **独占写入范围**:
  - `src/android/native/`
  - `scripts/device/`
  - `tests/device/`
  - `docs/analysis/t03a_build_report.md`
- **构建分支**: `task/T03A-android`
- **工作区**: `.claude/worktrees/t03a-android`

---

## 2. 目标平台与运行环境规范

| 项目 | 目标规格 (Anbernic RG477M) | 宿主构建环境 (Host Mac) |
| :--- | :--- | :--- |
| **SoC** | 联发科天玑 8300 (Dimensity 8300, 4x Cortex-A715 + 4x Cortex-A510) | Apple M系列 ARM64 |
| **GPU** | ARM Mali-G615 MC6 | Apple GPU (Metal) |
| **OS / API** | Android 14 (API Level 34) | macOS Sonoma / Darwin 27.0.0 |
| **目标 ABI** | `arm64-v8a` (aarch64 ELF) | `arm64-apple-darwin` |
| **屏幕规格** | 4:3 比例，1280x960 物理像素，60Hz | - |
| **构建工具** | CMake 4.4.4 + Android NDK r26b (26.1.10909125) | CMake 4.4.4 + AppleClang |
| **C++ 标准** | C++20 (`-std=c++20`, strict) | C++20 |

---

## 3. 架构设计与源码实现

探测器采用“数据结构与平台解耦、双模编译验证”的设计架构：

```
src/android/native/
├── CMakeLists.txt         # 双模 CMake：NDK arm64-v8a 交叉编译 + Host macOS 本地编译
├── main.cpp               # CLI 命令行入口（支持 --json, --summary, -o, -h）
├── probe_core.h/cpp       # 探测控制层与 JSON 序列化、人类可读摘要输出
├── probe_types.h/cpp      # 跨平台探测数据结构定义
├── probe_platform.h/cpp   # 平台调度层（根据 __ANDROID__ 分流）
├── probe_android.cpp      # Android 原生采集实现（Bionic sysprops, procfs, sysfs, evdev）
├── probe_host.cpp         # macOS Host 采集实现（sysctl, statvfs, host uname）
├── probe_vulkan.h/cpp     # Vulkan 1.3 / Bindless / Mali 驱动探测器
├── json_writer.h/cpp      # 零依赖确定性顺序 JSON 序列化器
```

### 3.1 Vulkan 1.3 与 Bindless 特性重点探测 (`probe_vulkan.cpp`)
针对《最终幻想：共鸣》与现代 3D 渲染对 bindless / descriptor indexing 的深度依赖，重点采集：
1. **实例与物理设备属性**:
   - `vkEnumerateInstanceVersion`: 查询实例支持的最高 Vulkan API 版本（要求 >= 1.3）。
   - `VkPhysicalDeviceDriverProperties`: 采集 GPU 驱动名称（ARM Mali Driver）、驱动描述、精确版本号与一致性测试版本（Conformance Version）。
2. **Bindless / 描述符索引特性 (`VkPhysicalDeviceDescriptorIndexingFeatures`)**:
   - `descriptorBindingPartiallyBound`: 允许描述符数组中未使用的槽位为空（Switch/现代引擎必备）。
   - `runtimeDescriptorArray`: 支持在着色器运行时动态索引未界定大小的描述符数组。
   - `shaderSampledImageArrayNonUniformIndexing`: 采样器数组非均匀动态索引。
   - `shaderStorageBufferArrayNonUniformIndexing`: SSBO 数组非均匀动态索引。
   - `descriptorBindingSampledImageUpdateAfterBind`: 绑定后更新纹理描述符。
   - `descriptorBindingStorageBufferUpdateAfterBind`: 绑定后更新缓冲描述符。
   - `descriptorBindingUpdateUnusedWhilePending`: 异步更新在途描述符。
   - `descriptorBindingVariableDescriptorCount`: 可变长度描述符集。
3. **Vulkan 1.3 核心特性 (`VkPhysicalDeviceVulkan13Features`)**:
   - `dynamicRendering`: 动态渲染（无需 RenderPass / Framebuffer 样板对象）。
   - `synchronization2`: 现代细粒度流水线同步与屏障。
   - `maintenance4`: 局部资源对齐与放松限制。
   - `inlineUniformBlock`: 着色器内联统一缓冲。
4. **纹理与表面格式支持**:
   - 验证 RGBA8_UNORM, RGBA8_SRGB, BGRA8_UNORM, R16G16B16A16_SFLOAT, R32G32B32A32_SFLOAT。
   - 验证 ASTC_4x4_UNORM, BC1/BC3/BC7 纹理压缩特性（Mali-G615 原生支持 ASTC，BCn 格式需检验其在 Android 上的支持情况）。
   - 验证深度缓冲 D32_SFLOAT, D24_UNORM_S8, D16_UNORM。

### 3.2 内存页面大小与硬件特征 (`probe_android.cpp`)
- **页面大小探测 (Page Size)**:
  - Android 15 引入 16KB 页面大小支持，而多数既有 ARM64 设备与内核仍使用 4KB 页面大小。
  - 通过 `sysconf(_SC_PAGESIZE)` 精确读取。4KB (4096 字节) 与 16KB (16384 字节) 对内存映射对齐、ELF 加载器（如 T02 NSO 加载器的页边界保护）具有绝对决定性影响。
- **SoC 与 CPU 拓扑**:
  - `ro.board.platform` 与 `ro.hardware`: 验证 MediaTek Dimensity 8300 (MT6897/MT8300)。
  - `sysconf(_SC_NPROCESSORS_ONLN)`: 验证 8 核心（4x A715 + 4x A510）。
  - `getauxval(AT_HWCAP)` / `AT_HWCAP2`: 检测 ARMv8.2-A / ARMv9 硬件指令集扩展（FP16, DotProd, SVE 等）。
- **屏幕与输出**:
  - 读取 `ro.sf.lcd_density`，并解析 `/sys/class/graphics/fb0/modes`、`/sys/class/graphics/fb0/virtual_size` 以及 `/sys/class/drm/*/modes`，确认 1280x960 @ 60Hz 物理显示输出。
- **输入手柄**:
  - 扫描 `/dev/input/event*` 与 `/sys/class/input/`，通过 `ioctl(EVIOCGBIT)` 检测内置游戏手柄摇杆与按键映射。

---

## 4. 构建脚本与交叉编译管线

编写了一键自动化交叉编译脚本 `scripts/device/build_android_probe.sh`：
- **工具链配置**:
  ```bash
  cmake -B build-android \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="arm64-v8a" \
    -DANDROID_PLATFORM="android-34" \
    -DCMAKE_BUILD_TYPE="Release" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    src/android/native
  cmake --build build-android -j$(sysctl -n hw.ncpu)
  ```
- **输出产物**:
  - `build-android/probe_rg477m`: 目标 arm64-v8a ELF 可执行文件（Position-Independent Executable, PIE）。
  - 自动复制并校验至 `.local/bin/probe_rg477m`。
- **真机交互扩展**:
  - 支持 `--push` 与 `--run` 选项。若未连接真机，严格输出 `BLOCKED_DEVICE`，绝不虚构执行结果。

---

## 5. 验证与测试结果

### 5.1 本地编译与单元测试 (Host macOS ARM64)
- 编译结果: `build-host/probe_rg477m` 构建成功，退出码 0。
- 单元测试 1 (`tests/device/test_json_writer.cpp`):
  - 测试 JSON 嵌套、转义字符、特殊值与数值精度。
  - 结果: `[PASS] test_basic_json`, `[PASS] test_escape_json`。
- 单元测试 2 (`tests/device/test_probe_output.py`):
  - 验证 `--json` 输出严格符合 JSON RFC 8259 规范。
  - 验证所有顶层键（`target_metadata`, `device`, `memory`, `storage`, `cpu`, `display`, `input`, `audio`, `vulkan`）完备性。
  - 验证 `-o <file>` 文件写入功能。
  - 验证 `--summary` 人类可读文本格式。
  - 结果: 全部 3 项功能测试 `PASS`。

### 5.2 真机执行验收门禁 (Pass Gate)
- **状态**: `BLOCKED_DEVICE`
- **原因**: RG477M 硬件掌机当前尚未通过 USB/ADB 连接至构建主机。
- **后续接力动作**:
  1. 待总控完成 NDK 部署后，执行 `scripts/device/build_android_probe.sh` 生成目标 `arm64-v8a` ELF。
  2. 当真机连接后，执行 `adb push .local/bin/probe_rg477m /data/local/tmp/` 并运行，生成真机真实硬件 capabilities.json 报告。

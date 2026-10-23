# 项目交接记录 (Handoff to Codex)

**交接时间**：2026-10-23T14:55:00+08:00  
**交接总控**：Antigravity → Codex  
**交付集成分支**：`work/rg477m-native`  
**状态**：已完成全部检查点集成，已停止写入，移交 Codex 接管。

---

## 1. 现场基线与 Git 状态

- **当前分支**：`work/rg477m-native`
- **当前 HEAD**：由当前提交提供（所有 T00, T00R, T01, T03A, T04-nvn, T04-assets 全部串行合并至 `work/rg477m-native`）
- **工作区状态**：Clean（无未提交的脏文件，无遗留冲突）
- **活跃写者 (`active_writers`)**：`[]`（全部已核验并安全释放）
- **后台长进程**：无挂起任务（下载守护脚本与子 Agent 已在交接前全部清理）
- **磁盘剩余空间**：`/System/Volumes/Data` 剩余约 28 GiB

---

## 2. 已通过验收的核心成果与机器证据

### 2.1 T01：技术路线实证与架构裁决 (PASSED)
- **核心裁定**：**全面采纳【路线 A：原生动态加载器 + HLE 运行时】，彻底否决【路线 B：静态重编译 / C++ 源码重构】**。
- **直接证据**：
  - 机器指令全量扫描：24,750,208 条指令中 **`svc` 系统调用指令为 0**！游戏与 Switch 内核无直接特权交互，完全通过 651 个动态符号导入闭环。
  - 动态导入符号分类：197 个 (30.3%) libc/musl/libc++ 直接透传 Android Bionic；262 个 (40.2%) `nn::os`/`nn::fs`/`nn::hid`/`nn::socket` 语义适配；192 个 (29.5%) 专有图形 (NVN) 与音频 (CRIWARE)。
  - 虚幻 5.6.1 子系统解构：`NVNRHI`、`FMallocBinned3`、`CriAtom`、`Chaos` 物理套件、`EnhancedInput`、`LowLevelTasks` 多线程（1,489 处 TLS 读取）。
- **产物文档**：
  - 路线裁决：`docs/analysis/route.md`
  - 架构报告：`docs/analysis/t01_architecture_report.md`
  - 机器证据：`evidence/t01/*.json`
  - 分析工具：`tools/t01_architecture_probe.py`

### 2.2 T03A：Android 掌机宿主与探针交叉编译 (PASSED)
- **完成工作**：
  - 双平台标准构建配置：`src/android/native/CMakeLists.txt`（支持 NDK arm64-v8a、C++20、API 34、libvulkan/libandroid/liblog，同时支持 macOS Host 本地快速测试）。
  - RG477M 硬件/Vulkan 探测 CLI 工具 `probe_rg477m`：采集 Vulkan 1.3、bindless 描述符索引（`descriptorBindingPartiallyBound`、`runtimeDescriptorArray` 等）、Mali-G615 驱动信息、4KB/16KB 内存页面大小、屏幕分辨率与手柄事件。
  - 本地自动化测试：`tests/device/test_probe_output.py` 与 `test_json_writer.cpp` 全部 PASS。
  - 一键编译与推送脚本：`scripts/device/build_android_probe.sh`（真机执行项按规范保持 `BLOCKED_DEVICE` 门禁）。
- **产物文档**：
  - 构建报告：`docs/analysis/t03a_build_report.md`
  - 源码与脚本：`src/android/native/`、`scripts/device/build_android_probe.sh`

### 2.3 T04-nvn：NVN 图形 API 映射与分发架构 (PASSED)
- **直接证据**：`main.bin` 动态导入表**仅含唯一 NVN 符号 `nvnBootstrapLoader`**。全部 534 个图形 API 均在引擎启动时通过 `nvnBootstrapLoader("nvn...")` 动态获取。
- **完成工作**：
  - 提取并分类全量 535 个 NVN API 符号表（`evidence/nvn/main_nvn_ids.txt`）。
  - 引入标准 NVN 头文件（`src/graphics/nvn/include/nvn/nvn.h`）。
  - 实现动态分发器与打桩基础设施（`src/graphics/nvn/include/nvn/nvn_dispatch.h` 与 `src/graphics/nvn/src/nvn_dispatch.c`）。
  - 论证 NVN 到 Vulkan 1.3 转换架构，Mali-G615 原生 ASTC 支持（零解包开销）及 Tegra 专有格式 `0x25A5A`（ASTC 4x4 sRGB）映射。
- **产物文档**：
  - 图形架构报告：`docs/analysis/nvn_surface.md`

### 2.4 T04-assets：RomFS 流式解密与 UE 5.6.1 资产全景 (PASSED)
- **Rights-ID Title Key 密码学实证**：
  - Ticket Rights ID: `0100b8a026f140000000000000000016`
  - Title Key 推导：`AES-128-ECB-Decrypt(encrypted_key, titlekek_15) = 14d985e5730ed0e8b847102332f5ad99`
  - 双重解密验证：正确解出 Section 0 (ExeFS PFS0) 与 Section 1 (RomFS Header)。
- **Zero-Disk-Waste (零磁盘损耗) 访问器**：
  - 编写 `scripts/extract/romfs_stream.py`，基于 AES-128-CTR 随机流式寻址，无需在磁盘解压 8.5 GB RomFS。
- **全量资产全景 (106,968 项)**：
  - `FFRS/Content/Paks/FFRS-Switch.ucas` (5.57 GiB) + `FFRS-Switch.utoc` (11.81 MB)：IoStore TOC v8，63,910 个 Chunk，518,224 个压缩块 (Oodle)。
  - `FFRS/Content/Paks/FFRS-Switch.pak` (2.76 GiB)：Pak v11，2,363 个文件（包含 154 个 CRIWARE USM VP9 视频）。
  - 5 份着色器字节码库：格式确认为 Switch 专有 `SF_GLSL_SWITCH` / `SP_GLSL_SWITCH`。
- **产物文档**：
  - 资产报告：`docs/analysis/asset_inventory.md`
  - 机器证据：`evidence/assets/romfs_inventory.json`、`evidence/assets/shader_inventory.json`
  - 工具脚本：`scripts/extract/titlekey_derivation.py`、`scripts/extract/romfs_stream.py`

### 2.5 T02-PREP / 加载器与重定位重大突破
- **彻底推翻“链接基址为 0 且不可重定位”的前期假定**：
  - 前序测试将基址为 0 时 `MAP_FIXED` 误判为被系统忽略，且扫描仅捕获 4,612 条 standard RELA。
  - 深入解析 `MOD0 -> .dynamic`，发现现代 LLVM 紧凑相对重定位表 **`DT_RELR`（标签 `0x24`，大小 `0x3fa80`）**，成功解码出 **1,370,974 条相对重定位**！
  - 已在 `src/runtime/nso/nso_loader.c` 实现 `DT_RELR` 解码与高位地址映射，已实证在 `0x10ce1c000` / `0x110804000` 等任意虚拟地址完成无损重定位。详见 [evidence/coordinator/relr_verification_report.md](file:///Users/yuyan9923/GitHub/ff-resonance-rg477m-native/evidence/coordinator/relr_verification_report.md)。
- **`malloc` 崩溃根因已完全查明**：
  - `malloc` (`0x132d0`) 在全局分配器未初始化时进入慢速分支 `0xb7064`。
  - `0xb7064` 调用 `0xbe270` 进行分配器初始化，但 `0xbe270` 在给对象赋予 vtable 之前调用了 `0x86208`，而 `0x86208` 递归调用了 `malloc(0x40000)`！
  - 递归 `malloc` 误以为分配器对象非空，进而解引用空 vtable 导致 `0xb705c` 处的 `ldr x3, [x8, #0x30]` 产生 `fa=0x30` 段错误。
  - 解决方案：在运行游戏内部代码前，提供完整的 CRT/TLS 上下文，或在运行时层承接/hook 全局分配器入口，使外部调用安全返回。

---

## 3. 接手 Codex 立即执行的前三条精确命令

```bash
# 1. 确认当前分支干净且处于 work/rg477m-native 最新状态
git status && git log -n 3 --oneline

# 2. 验证本地 NSO 加载器对 main.bin 的 DT_RELR 重定位与符号加载
clang -O2 -Wall src/runtime/nso/nso_loader.c src/runtime/nso/nso_call.c -o /tmp/nso_call && /tmp/nso_call .local/uncompressed/main.bin --info

# 3. 运行已集成的 RomFS 流式测试与 Android probe 验证
python3 scripts/extract/romfs_stream.py --test && python3 tests/device/test_probe_output.py /tmp/probe_test/probe_rg477m
```

---

## 4. 下一步任务建议 (Codex 重点攻坚)

1. **推进 T02 (运行时与行为对照样例)**：
   - 为 NSO 加载器补齐极简 `nn::os`（Mutex, Event, ThreadLocalStorage）、Bionic CRT 基础桩。
   - 解决分配器启动递归死锁/空指针，原生调用 `nninitStartup` 并推进至 `nnMain` 入口。
2. **推进 Android NDK 部署与交叉构建**：
   - 完成 `/Users/yuyan9923/Library/Android/sdk/ndk/26.1.10909125` 的安装。
   - 调用 `scripts/device/build_android_probe.sh` 生成 `arm64-v8a` ELF 二进制。
3. **推进 T04 启动链路 (运行时 + RomFS 挂载 + NVN 窗口)**：
   - 将 `scripts/extract/romfs_stream.py` 逻辑对接至 `nn::fs` C API 兼容层。
   - 将 `src/graphics/nvn/src/nvn_dispatch.c` 对接至 `nvnBootstrapLoader`，实现首帧清屏与 1280x720 视口输出。

---

**总控声明**：Antigravity 已收口全部当前阶段工作，未保留任何活跃写入占用，已停止项目写入。

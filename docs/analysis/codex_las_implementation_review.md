# 三个生产实现与 CPU 工具适用性补证

**LAS 的 NVN renderer 确有实现体，可以减少重写；尚不能直接绑定本游戏。** 此处仅追加三文件核验，不扩展全仓。固定版本均为 Beta4 `fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93`；自有代码由该版本 [MIT LICENSE](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/LICENSE) 覆盖，第三方授权仍单列。

| 直接核验的生产文件 | 真实函数体证据 | 直接依赖/移植限制 |
|---|---|---|
| [device.c](https://github.com/peiikai/LASRecomp/blob/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/runtime/nvn/vulkan/device.c) | 实际 `vkCreateDevice`、`vmaCreateAllocator`；`vk_frame_end` 结束 rendering、提交 semaphore 后调用 `vkQueuePresentKHR`，处理 out-of-date/suboptimal。 | SDL3 window/surface、VMA、全局 `g_vk`、settings/menu/renderer。请求 Vulkan1.3 dynamicRendering、synchronization2 等及1.2 timeline/8-bit功能；须映射到目标设备真实能力，不能当 Vulkan1.1 后端直接接。 |
| [exec.c](https://github.com/peiikai/LASRecomp/blob/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/runtime/nvn/vulkan/exec.c) | `backend_execute` 调 `exec_list` 再 submit/flush；实际建 graphics pipeline、descriptor/vertex binding，调用 `vkCmdDraw`/`vkCmdDrawIndexed`。 | `kernel.h`、renderer、shader/texture状态、guest address/heap及共享提交生命周期。混有织梦岛随机化 `pot_glow`/`pot_gold_tint` 分支；ASTC buffer→texture 分支明确 skip。是实际实现，覆盖却有游戏特定边界，不能整块声称通用。 |
| [shaders.c](https://github.com/peiikai/LASRecomp/blob/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/runtime/nvn/vulkan/shaders.c) | `vk_glsl_to_spirv` 真调 `shaderc_compile_into_spv`，目标 Vulkan1.3；`vk_module` 真调 `vkCreateShaderModule`；有缓存和 descriptor reflection 实现。 | shaderc、pthread compiler TLS、`shaders.h`/共享 Vulkan状态。这个文件输入是已生成 GLSL，不能据此认为本游戏 Maxwell→GLSL、控制数据/绑定已兼容；该前端本次未扩查。 |

与前轮 header/Makefile 证据合并，足以把 LAS 列为**优先提取评审的 renderer 实现**。本轮未追加 NVN前门 `objects/cmdbuf` 实现核验，未闭合完整依赖、资源所有权或 shader 覆盖。`CpuContext` 取参、guest opaque 对象布局、平台呈现和上述游戏专用分支仍需适配，不能直接把 LAS 函数地址塞进本项目 typed dispatch。下一项应是选择明确资源/命令路径做生产接入；不再继续泛搜成熟案例，也不为此阻断 HostArgs 开发。

`librecomp` 不是通用 Switch 编译器。已核主仓 [N64ModernRuntime `cdf5abbd5026fef5c364c676e4667c45e42b6863`](https://github.com/N64Recomp/N64ModernRuntime/commit/cdf5abbd5026fef5c364c676e4667c45e42b6863)，[README](https://raw.githubusercontent.com/N64Recomp/N64ModernRuntime/cdf5abbd5026fef5c364c676e4667c45e42b6863/README.md) 将其定义为 N64Recomp 生成代码到 ultramodern/libultra 的桥，含 overlay/PI DMA/存档；[COPYING](https://raw.githubusercontent.com/N64Recomp/N64ModernRuntime/cdf5abbd5026fef5c364c676e4667c45e42b6863/COPYING) 为 GPLv3。它不能替代 Switch TLS/CRT/NVN。

真正的 [N64Recomp `81213c1831fab2521a6a5459c67b63437d67e253`](https://github.com/N64Recomp/N64Recomp/commit/81213c1831fab2521a6a5459c67b63437d67e253) 是 [MIT](https://raw.githubusercontent.com/N64Recomp/N64Recomp/81213c1831fab2521a6a5459c67b63437d67e253/LICENSE) 的 N64/MIPS→C 工具，[说明](https://raw.githubusercontent.com/N64Recomp/N64Recomp/81213c1831fab2521a6a5459c67b63437d67e253/README.md) 明确 MIPS 指令/延迟槽和配套 runtime。其 ARM64 **宿主输出编译能力**不等于可输入 Switch AArch64；不把它引入当前 CPU 路线。

若确实要采用 **AArch64 静态 CPU 重编译**，本轮已固定的候选是 LAS 同版本自有 `tools/gen.py/lifter.py` 流程（MIT，见 [Makefile](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/Makefile) 的 `gen` 目标），而非 librecomp。其生成 main/sdk C、连接自己的 HLE 与 CPU context，ARM64分支使用 SIMD inline asm；这是一种真实路线，但本轮未审其指令/间接跳转覆盖，亦未证明本游戏可直接输入。当前 ARM64原代码直跑可保留；先抽 renderer 不要求同时换 CPU 执行方式，TLS/CRT语义仍须明确提供。

所有新增证据保存为 `.local/audit_web8..12.txt`；full SHA 由官方 commit patch 首行闭合。无新代码、binary分析、设备或 Git 改动；以上为静态源码证据，不是构建/运行或完整游戏验收。

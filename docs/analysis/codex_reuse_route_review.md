# 原生移植复用路线：有界审计

2026-10-23 UTC。**应调整图形复用优先级，而不是继续按“没有现成 NVN API 层”从零展开。已找到可固定版本读取的 Switch 静态重编译 beta 案例 LAS Recomp；它值得先做生产代码提取评审，但不是本游戏/Android 已完成底座。** 旧 `route.md` 的“94 MB 必生成 15–20 GB、必 OOM”没有生成日志、峰值内存或编译测量，顶部也已明确撤回；不能作为否决路线的依据。“switch-recomp 针对 N64”未给确切仓库，混淆了项目名称，不能沿用。

本轮只核验下列三个确切候选；未执行它们或读取游戏输入。完成状态均为作者主仓库自述与可读源码边界，未独立验证通关或性能。

随后按总控要求限定补核了 LAS 的 device/command execution/shader 三个真实生产实现体；函数及直接依赖见 [实现补证](codex_las_implementation_review.md)。这将“只有 header/构建线索”提升为“确有 renderer 实现可提取”，仍未提升为本游戏兼容。

| 候选、固定版本与来源 | 许可证、实际可获取性、完成程度 | 对当前计划的意义 |
|---|---|---|
| **[peiikai/LASRecomp](https://github.com/peiikai/LASRecomp)**，Beta 4 对应 [`fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93`](https://github.com/peiikai/LASRecomp/commit/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93)。[release](https://github.com/peiikai/LASRecomp/releases)、[固定 README](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/README.md)。 | [MIT](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/LICENSE) 仅覆盖自有代码，third_party 保留各自授权。支持《织梦岛》Switch v1.0.0；已发布 beta，仍列崩溃/渲染问题。macOS Apple Silicon/Metal、Windows/Linux x64/Vulkan；Linux ARM64 明说 built、未真实硬件测试。不是 Android 或本游戏完成案例。 | [NVN header](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/runtime/nvn/nvn.h) 有 guest 对象→host 对象、texture/program/command recording、backend 接口；[Makefile](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/Makefile) 实际包含 NVN/Maxwell 与 Vulkan 构建路径。它是 API 层复用线索，优先于只考虑命令流仿真。其入口从 `c->x/c->v` 取参数，假定对象首字保存 host 指针；不能直接套用本游戏 opaque 布局或现有 typed ABI。尚未核清选取后端文件的依赖闭包/覆盖。 |
| **BelmanteGu/SuperMarioWonderRecomp**，`010ed7a596a621c15d3a8dfa5795ca8c3668b066`。固定引用来自 [mk8-recomp 自有 UPSTREAM.json](https://github.com/dougchansan/mk8-recomp/blob/main/integrations/belmante-runtime/UPSTREAM.json)。 | 该 provenance 声明上游 MIT、修改为 GPL-3.0-or-later，并列 NVN Vulkan/平台文件。**本轮官方主仓库与固定 LICENSE/header URL 都返回 404**；未取到上游许可证/实现，不把下游引用当作本轮已获取成熟底座。 | Wonder/MK8 的可玩声明不能代替上游审查或本游戏兼容性。当前阻塞是可获取性/选取代码授权与版本，不是“它用了重编译所以拒绝”。不建议现在以它为冻结依赖，也未绕到不明镜像。 |
| **[bgyss/SwitchRecomp](https://github.com/bgyss/SwitchRecomp)**，仅 main 可读；不可变 SHA 在本轮访问限制内未闭合，**未达到采纳门槛**。 | README 明说 draft specifications、exploratory Rust scaffolding；未证明已完成任何支持游戏。[Cargo.toml](https://github.com/bgyss/SwitchRecomp/blob/main/Cargo.toml) 声明 workspace `MIT OR Apache-2.0`，未独立核清完整授权覆盖。 | 是实际 Switch 研究项目，不是 N64 工具或成熟移植案例。不能据其名称推定旧报告所指就是它，也不能把其存在当作可替代当前运行时的成品。 |

**已经复用的部分**：现有 typed C++ guard 链接真实 NDK `libc++abi.a(cxa_guard.cpp.o)`、`libunwind.a`；Android API/既有 partial NDK headers、CRT、libc、compiler-rt 和 LLD 已用于实际交叉产物；Vulkan core API 已驱动真实 buffer/transfer 组件。这些不是重写标准库。partial NDK 只验证实际文件 CRC/size/hash，不能称整包已独立校验，也不能从 guard 契约推出所有 C++/SDK ABI 相同。依据现有 `codex_cxx_guard_review.md`、构建脚本和 `codex_vulkan_buffer_report.md`，本轮未复跑。

**已评估、尚未接入的部分**：复用原 13 文件、约149 KB 证据，Strato `ae1566a48285816a87e81d4aeb40bd2f4e56e60b` 是 GPU 命令流/engine→Vulkan；Hades `8717c8f6d7deab58032e042b027ed84838833824` 是 Maxwell→IR→SPIR-V shader 库。前者与 DeviceState/guest memory/channel 等耦合；后者缺 NVN 对象、queue、同步、present，且旧评估保留文件授权链缺口。没有技术依据把它们说成完全不可用；也没有把它们实际接入本项目的证据。详见原 [图形底座报告](../../../tls-research/GRAPHICS_FOUNDATION_REPORT.md)，本轮未重抓这些文件。

整套模拟器/NCE 应用作为交付不符合“本游戏原生移植、不得模拟器演示替代”的目标；**NCE 名称本身不是拒绝复用模块的理由**。抽取 GPU/shader/服务实现仍可服务原生 CPU 目标。静态 CPU 翻译或同架构直跑，都不会自动提供正确的 guest TLS/CRT 数据、析构生命周期、线程对象与 SDK allocator；这些仍需明确兼容层。既有 TLS/页 owner 工作不因此全部作废，也不能据此拒绝成熟代码复用。

**建议的一项具体改法**：把下一项图形实现任务从继续手写通用 NVN 入口，改为“固定 LAS Beta4 的 API 层接口/renderer 代码选取与 Android 接入评审”，由图形负责人逐项标注自有/第三方许可、CpuContext→现有 typed ABI 适配、本游戏已证明的对象布局、资源/命令/同步依赖，随后提交一条真实资源到命令路径的生产实现。以复用外部 renderer 为默认，缺语义才补局部；保留现有 loader、页/TLS owner 与 Vulkan 生命周期组件，不新增演示探针，也不在证据不足时全换 CPU 路线。是否可移植、shader 覆盖和 RG477M 表现仍待实现验证，不能给可玩性或工期承诺。

证据：`.local/manifest.json` 保存本地资料 hash、候选版本与缺口；`.local/audit_web*.txt` 保存此次 primary 页面返回。公开 HTTP 的 DNS/API/tree/404 失败均有记录，无 clone、安装、源码/设备/Git 变更，无新二进制分析。LAS 的部分后端目录/猜测文件路径未能读取，因此本轮没有声称完整依赖闭合；Belmante 可获取性及 bgyss SHA 是明确未完成项。

归档说明：源报告../reuse-audit/REUSE_AUDIT.md；这里只调整本地报告链接，原始hash/primary返回见原目录manifest。

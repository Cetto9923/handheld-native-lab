# REA使用与成熟组件复用核验

本轮首次补查前，未找到当前Codex实际读取该Skill或执行REA引擎的证据；已有接口证据来自保存的LLVM/机器码与实际主机/掌机运行，不能改称REA Evidence。旧T00基线仅记Antigravity MCP207工具已加载，也不能代替本轮调用记录。现在已实际读取并应用该Skill流程，尚未为此重复反编译已闭合函数。

- Skill：reverse-engineer-anything，version42，`/Users/yuyan9923/.codex/skills/reverse-engineer-anything/SKILL.md`，实际resolve `/Users/yuyan9923/.cc-switch/skills/reverse-engineer-anything/SKILL.md`；SHA2563456aab99d48c3294726065c9d5bd586bc165f2849fd5a3d5080381630564715。已读main、native-and-artifacts及connection-and-recovery。
- 当前ALL_TOOLS没有REA MCP工具。公开`~/.local/bin/rea`只是固定`npx -y rea-agents@6.5.0` launcher。首次doctor30秒超时、version因npmDNS ENOTFOUND退出1、离线帮助ENOTCACHED退出1，均保留。
- 随后仅在项目`.local/rea-method-audit/npm-cache`隔离获取固定6.5.0（禁安装脚本），实际doctor **exit0/no timeout**：Codex scope healthy=true，Skill42/CLI6.5.0 aligned，Ghidra available=true；environment_healthy=false与active MCP0仍明确保留。未修改MCP/全局配置、未安装Hopper或重启其他代理。当前CLI帮助也已真实0。不能把doctor0说成游戏反编译/执行完成。
- 按Skill“reuse matching state / do not repeat identical analysis”复用已有hash匹配证据；普通C/ASM源代码开发及公开源码架构分析按Skill不用REA。新的未解二进制问题使用现有CLI/Ghidra定点工作流，保存原Evidence；现有LLVM结果保持原工具归因。该Skill是逆向分析与证据流程，不是自动提供Switch SDK/TLS/NVN兼容的一键移植器。

## 具体成熟案例、边界与改法

| 固定候选 | 已核实内容和许可 | 当前采用情况 |
|---|---|---|
| [LASRecomp Beta4 fe66f31a](https://github.com/peiikai/LASRecomp/commit/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93) | 自有MIT、third_party另计；织梦岛Switch1.0.0 beta，Mac/Metal及x64/Vulkan，LinuxARM64 built未真机，非Android/本游戏完成。device/present、pipeline/draw、GLSL→SPIRV/module有真实实现体；SDL3/VMA/shaderc/全局CPU-context依赖及游戏hack仍须适配。 | 提升为首要API级NVN renderer提取候选，已派图形负责人闭合最短资源→命令路径，尚未复制/接入，不宣称drop-in。 |
| [Strato ae1566a](https://github.com/strato-emu/strato/tree/ae1566a48285816a87e81d4aeb40bd2f4e56e60b) / [Hades8717c8](https://github.com/strato-emu/shader-compiler/tree/8717c8f6d7deab58032e042b027ed84838833824) | 已有13限定文件149KB证据；GPU命令流/engine→Vulkan与Maxwell→IR→SPIRV。Strato整体GPL3+、选读文件MPL2；HadesREADME与文件GPL2+头部授权链待核。 | 仅评估未接入；不是NVN对象API桥，依赖guest地址/engine状态；不因NCE名称拒绝可用模块，也不把其shader库当完整renderer。 |
| Belmante Wonder010ed7 | 主仓/固定文件本轮404；仅下游UPSTREAM.json有MIT原件/GPL3修改声明。 | 不把未能取到的源码与许可作为已可用冻结底座；无不明镜像替代。 |
| [N64Recomp81213c](https://github.com/N64Recomp/N64Recomp/commit/81213c1831fab2521a6a5459c67b63437d67e253) / N64ModernRuntime cdf5abb | 前者MIT输入MIPS，后者GPL3为libultra runtime桥；可编ARM64宿主不等于可输入SwitchAArch64。 | 不用于当前CPU输入。LAS自身gen/lifter是实际AArch64静态候选，但本游戏指令/间接跳转覆盖未核，暂不全换CPU路线。 |

此前“94MB必生成15–20GB且必OOM”没有生成/编译/峰值内存实测，不能作为拒绝静态重编译的事实。bgyss/SwitchRecomp只是draft/scaffold，本轮SHA未闭合，不是已验证成熟移植器。保留当前ARM64直跑与HLE能复用既有loader、页/栈/TLS生命周期；静态翻译能调整CPU执行方式，仍需guest TLS/CRT、线程/服务与图形运行时。应先提取现成renderer，缺已证实语义才局部补，避免继续手写全部NVN或盲目整换CPU基础。

已实际复用NDK libc++abi guard、unwind/CRT/libc/compiler-rt与Vulkan API；这些有真实构建/运行证据。LAS/Strato/Hades尚未集成；不能把“选中”写作“已复用”。shaderc只处理该文件的GLSL输入，未证明本游戏Maxwell前端/绑定兼容。详细primary URLs、版本/许可/范围见同目录codex_reuse_route_review.md与codex_las_implementation_review.md；本轮不重复游戏leaf次数来证明通用性。

证据：root`.local/rea-method-audit/{doctor,offline-help,isolated-doctor,isolated-help,function-help}/run.json/stdout.log/stderr.log`（首次version使用tool session81813真实exit1；publiclauncher源码已读）；外部限定源码审计`../reuse-audit/.local/manifest.json`及12份primary返回。无新游戏/密钥、刷机、购买、远端发布或模拟器演示。

## New focused locale query: actual REA attempt, explicit failure

63 strncasecmp_l after9dc is a fresh binary question, not repeat Info16/17. Skill42/native/connection read; public6.5.0 CLI function actually invoked on explicit172B rawSDKslice/VA59c7e0, bounded55s. Actual6.957s/exit1, not timeout: beforeGhidra startup ProcessRunTokenReader Swift helper could not write defaultclangModuleCache under sandbox and reportedSwiftcompiler/SDKmismatch. No successfulfunctionEvidence/snapshot; no repeateddoctor/setup/install or blindretry. This is a toolfailure, notautoapproval rejection. The actual functionABI/ASCII/ignoredlocale conclusion is from preciseLLVM+PLT/RELA, separately attributed. Fullcommand/stdout/run andoriginalhash preserved in ../rea-followups/locale-compare/.local/. [Completed scoped report](codex_locale_compare_report.md), originalSHA483f79ab710081c244d361b87720d910021d0d5e14cc2554f5867af92453bed6.

Mature implementation chosen: systemBionic non_l strncasecmp, typedfourargwrapper ignoresguestlocale asactualSDKdoes; nohostlocale_t cast/locale-manager/selfmadealgorithm. AOSPandroid14tag production sources read; exactimmutableAOSPtagcommit resolutionDNSfailed and isUNKNOWN. ActualRGlibc importexecution stillnextstage, notinstaticanalystpass. Runtime new12filescope implementingthisalongside66realmanagementprovider.

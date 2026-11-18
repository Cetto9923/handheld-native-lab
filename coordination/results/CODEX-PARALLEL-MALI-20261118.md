# 天玑崩溃复核与暖布局配方加固

2026-11-18，任务 CODEX-PARALLEL-MALI-20261118，执行者 /root/mali_crash_fix。

## 接手基线

- 工作区 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/worktrees/codex-mali-20261118`，分支 `task/codex-parallel-mali-20261118`，起始 HEAD `273c240155f95db92e2f08050b61164aec401f29`。
- 已套用总控冻结的生命周期候选，输入为 `.local/codex-dots-takeover-20261118/lifecycle-existing.patch` 及两个指定未跟踪源码文件。既有候选继续保留在本工作区未提交区。
- 本次新增改动限定为 `xbg_game.cpp` 的 `warm::make_layout`、一个定向宿主测试和本报告。原始游戏、发布包、存档、设备状态均由总控保管。

## 原事故证据

直接复核 `~/.cetto-arsenal-inputs/crash73-new/guest_boot.txt` 和 `engine.txt`。原设备为 vivo V2405A、MT6991、Mali-G925-Immortalis MC12，实际版本 1.0.73。末段在 frame1174 载入 `073ce354a4f65e2f` compute 模块后记录 SIGSEGV，故障地址 0x8，vendor PC 相对偏移 0x1eda09c、LR 相对偏移 0x1eda024。当前证据没有给出该故障所处 Vulkan API 或故障线程身份。

已复用现有 compute create-info、目标六个 descriptor binding、缓存并发、设备销毁和内存回收审查。目标 metadata 的两个 UBO、两个 combined sampler 和两个 storage image，以及本机 ample descriptor limits，均未提供新的原事故成因证据。原事故归因继续开放。

## 本次确认的独立缺口

`warm::make_layout` 会把磁盘暖管线配方还原为 Vulkan descriptor/pipeline layout。原实现检查了字数、binding 数量、set 数量和 range 数量，结构完整的非法布局仍能抵达两个 Vulkan 创建调用。实际生产函数的驱动桩执行证明，重复 binding、错误 descriptor/stage 值及非法 push constant 范围可以被原路径接受。

新增校验在两个创建调用前执行，覆盖重复 binding、当前逻辑设备未启用的 extension descriptor/stage 形状、input attachment stage 范围、push constant 对齐与大小、同一 stage 重复 range，以及按当前设备 `maxPushConstantsSize` 检查范围。范围使用先比较 offset、再做减法的形式，避免加法溢出。拒绝的可选暖配方沿既有 warm build 失败路径处理，普通运行时管线构建继续使用其原入口。

合法 core descriptor 0 至 10、数组、稀疏 binding、零 count 保留项、`VK_SHADER_STAGE_ALL`、不同 stage 的重叠 push 字节范围和有效缓存复用均有执行覆盖。当前逻辑设备仅请求 core features、选定 Vulkan12 features、swapchain 及 mirror-clamp，新增检查对应此 enabled 范围。

Vulkan 规范要求 binding 唯一，input attachment 限定 fragment 或零 stage，push offset/size 按 4 字节对齐且服从设备上限，同一 stage 只能出现在一个 push range。参考规范页面均于本轮成功读取。Vulkan-Docs HEAD 为 `f7de774cace1509af0f10b8d859c973b5354ff62`，项目许可证入口为同 revision 的 LICENSE。这里只引用接口规则，无上游实现代码复制。[descriptor binding](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutBinding.html) [descriptor layout](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutCreateInfo.html) [push range](https://docs.vulkan.org/refpages/latest/refpages/source/VkPushConstantRange.html) [pipeline layout](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineLayoutCreateInfo.html) [固定 revision](https://github.com/KhronosGroup/Vulkan-Docs/tree/f7de774cace1509af0f10b8d859c973b5354ff62) [许可证](https://github.com/KhronosGroup/Vulkan-Docs/blob/f7de774cace1509af0f10b8d859c973b5354ff62/LICENSE.adoc)

## 实际验证

日志位于本工作区 `.local/mali-layout-20261118/`。

| 检查 | 实际结果 | 覆盖 |
| --- | --- | --- |
| `python3 tests/native/test_warm_layout_validation.py` | exit0，ASan/UBSan 通过 | 提取当前完整 Reader 和 make_layout，驱动桩记录创建调用；合法布局、缓存复用、坏布局拒绝 |
| 同测试传入 `xbg_game.baseline.cpp` | 旧函数执行 SIGABRT，测试进程 exit1 | 完成编译后在接受结果断言失败，旧路径负对照成立 |
| `python3 tests/native/test_warm_layout.py` | exit0 | 既有并发发布、获胜对象保全及失败竞争对象释放 |
| `python3 -m unittest tests.installer.test_pipeline_warm_inspector tests.installer.test_apk_pipeline_warm_seed` | 17 tests，exit0 | 现有配方发行、身份及原子 seed 路径 |
| 两份既有种子布局回放 | 1750 与 1898 条记录中的 641 个唯一 layout，exit0，ASan/UBSan 通过 | 当前完整 make_layout，maxPushConstantsSize=128，驱动桩创建 |
| Android API28 原生产编译参数语法检查 | exit0 | 当前完整 xbg_game.cpp；命令在 syntax-plan.json |
| `git diff --check` | exit0 | 空白与补丁格式 |

两份种子 SHA256 分别为 `595d5e1825e06ec1a3d6931e939c1c3a6d5d2649a95700d3cdd766bf50b84b8f` 和 `c7a1d89d498e013d9a8407268892f913193626fcb83e0e91fa5c416e702dd287`，完整路径、记录数量和结果在 `seed-layouts.json`。

首次负对照因驱动桩常量未使用触发编译警告，修正桩标注后重新执行，`negative-control-runtime.log` 保留了运行期断言失败。首次本地种子解析脚本存在 Python 复合赋值求值次序错误，修正后重新运行，两份输入以完整 framing 解析且全部唯一布局通过。历史失败日志予以保留。

## 覆盖边界与接续

本次交付为可选暖配方输入加固。G925 玩家报告中的原故障与坏布局之间尚无证据关联。完整 Android APK 构建、G925 原机、实际暖驱动对象和两台现有设备的游戏回归均为 NOT_RUN，设备未被本代理操作。

总控已将暖布局加固作为独立小修复集成。后续独立审查发现完整生命周期候选存在合法多窗口回归，总控已将其隔离。原事故继续需要能对齐故障线程和 API 的证据，现有 frame1174 相邻模块记录单独保留。

## 后续独立审查补充

本次暖布局候选限定于已列出的基础 binding/type/stage 与 push range 校验。layout flags、descriptor aggregate limits 和 per-stage resource totals 等的完整校验仍待后续补充。窗口生命周期与性能补丁组合的独立审查另见 CODEX-PARALLEL-LIFECYCLE-REVIEW-20261118.md。

## G925 原事故的 CS073ce 精确复核

### 输入与实际执行

本节继续使用玩家 1.0.73 报告 `/Users/yuyan9923/.cetto-arsenal-inputs/crash73-new/`，report.zip SHA256 为 `a1c6f559111f52defdce905edaf7df5c6e8a2048687586892248e69a98e5abd4`。设备为 vivo V2405A、MT6991、Mali-G925-Immortalis MC12、Vulkan 1.3.278、driver `0xc401000`。guest_boot.txt 21654–21657 行给出 `073ce354a4f65e2f` stage5、9023 words、frame1174，随后出现 vendor PC `+0x1eda09c`、LR `+0x1eda024`、fa `0x8`。报告仍缺故障 TID 和 API 调用收据。

离线输入复用已有实际 `shaders.hnla` 中导出的 canonical XGS/SPIR-V，路径为根 `.local/arsenal-mali-crash-review/`。SPIR-V 为 36092B、9023 words，SHA256 为 `af089341d38ab2ae27fc1ddf95a8b588407b5b479ba5b00aa320d0aaead1942f`。该输入与原机之间尚缺最终字节身份核对。

本次新证据位于隔离工作区 `.local/g925-cs073ce-review-20261118/`。`reproduce.py` 直接包含当前生产 narrow、stencil、integer-buffer、linear-sample、indirect-instance 和 typed-storage 分析头文件，并从实际 xbg_game.cpp 提取完整 `spirv_uniform_extent`。构建启用 `-Wall -Wextra -Werror -fsanitize=address,undefined`，执行 exit0。`CHECKPOINT.json` 保留命令、输入摘要、生产源码摘要和日志路径。恢复命令为 `python3 .local/g925-cs073ce-review-20261118/reproduce.py`。

### 运行时改写与变体

按 G925 该次启动记录逐项核对当前实现，得到下表。该表描述现有源码和记录配置下的离线路径。

| 路径 | 精确边界与结果 |
| --- | --- |
| SPV override | 读取 shader_directory 上级的 spv-override/key.spv，成功替换必记 `NVN spv override`。该报告无目标替换记录，原机最终字节仍待捕获 |
| Arsenal/boss/terrain | stage0 或指定 stage4/key，目标 stage5 无匹配 |
| histogram sync | 仅启用的 Honor profile 和指定 ef4326df31036d24，目标无匹配 |
| stencil | 实际头函数传入 stage5/key 后返回0，逐字不变 |
| Honor helper inline | 日志为0 |
| quad/warp | quad 限 stage4，warp 限 Adreno 且 subgroupSize>32，目标 G925 subgroup16 无匹配 |
| narrow | 实际函数用 subgroup16、supportedStages=0x3f30 执行，wanted=0、result=0，逐字不变 |
| compaction sum | 仅 Adreno subgroup64 和指定 b673e10f3da2451c，目标无匹配 |
| tidy/position | nan/ldc/temps 日志均0，position 限 stage<4 |
| rewritten reuse | shader_reuse=0、目标 loaded reused=0 |
| integer buffer | 实际分析候选为空，全部 sampled bindings 均为2D image |
| typed storage | 当前运行时仅 storage-texel 或有输入的 vertex shader 保留 module_words。目标只有2D storage image，linear候选也为空，因此该 canonical 的 typed rescue 入口直接返回原模块 |
| linear sample | 实际候选为空，模块含6条 ImageFetch、2条 ImageWrite，无采样指令候选 |

运行已启用且能够触及该目标的实际 narrow/stencil 后，输出 `g925-active-passes.spv` 的大小、SHA256 与 canonical 完全一致。未来原机现有 api-breadcrumb runtime_module 收据可先核对 FNV64 `dcd609ecefa41335`。它是本次 canonical 的精确预期值，不能替代原机最终字节捕获。

原始与 active-passes 输出分别使用 `spirv-val --target-env vulkan1.1` 和 `vulkan1.3`，四次均 exit0。实际工具版本为 `SPIRV-Tools v2026.4 unknown hash, 2026-09-10T13:28:18+00:00`。Khronos 推荐在变换后使用 validator，并说明其覆盖仍不完整。本次结果覆盖离线已实现的规则检查，设备资源、动态输入和驱动执行另需证据。[SPIRV-Tools 官方说明](https://github.com/KhronosGroup/SPIRV-Tools/blob/727277f679857e5a680cf559586268b2df35c48d/README.md)

### 实际资源范围与 feature 边界

实际生产 uniform-extent 分析将 binding35 从包声明的65536B收缩为32B。该 Uniform 变量 ID19 的全部18个非元数据引用均是直接 AccessChain，索引只覆盖前两个 vec4 的分量，字节 offset 集合为8、12、16、20，最大访问结束位置24B。该目标的32B范围覆盖这些实际读取，binding5 support仍为1392B。完整访问表在 results.json。此前仅按 XGS 声明65536B描述该 binding，应以本节的实际收缩结果补充。

两条 storage binding23/29 的实际分析均为 float scalar、2D、Unknown format、写4分量，XGS formatless-write flags与之相符。原报告支持 shaderStorageImageWriteWithoutFormat=1、gatherExtended=1，canonical capability列表为56/1/47/25/50/46，未发现目标声明要求与日志中已启用feature冲突。canonical没有 subgroup op、原子或执行/内存 barrier。LocalSize为8×8×1，共64invocations，日志 max workgroupInvocations=1024。Private uint[1024]保持原状，它与 Workgroup 共享存储的资源类别不同。

Descriptor 路径复用已有 DESCRIPTORS.md 结论。目标六个 binding 唯一且count=1，实际 payload vectors预留容量，动态offset按binding排序，compute不进入graphics memo。原报告缺两张storage image及两张sampled image的确切view格式、extent、subresource、usage，也缺当前UBO有效offset与底层Buffer界限。因此这些动态接口关系仍待原机核对。全局 `formatless-write image=1` 探测不能替代每个实际bound format的支持判断。Vulkan dispatch同时要求有效且兼容的pipeline、descriptor和资源关系。[Vulkan dispatch 规范](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDispatch.html)

### 模块构建与 dispatch 边界

runtime loader 在所有改写及分析后，以仍存活的 `vector<uint32_t>` 传入 VkShaderModuleCreateInfo，codeSize为words×4，成功创建并登记后才输出 loaded。目标该行能证明 module创建已返回成功。warm loader也读实际bundle、检查stage与identity，并沿相同改写顺序创建module。先前 compute-create-info 审查未发现该目标的局部悬空create-info指针。

当前 dispatch 先做整数/typed/linear选择和CPU preflight，再处理间接参数、descriptor、pipeline、绑定和命令记录。直接或CPU fallback counts经过负值、零与maxComputeWorkGroupCount检查。GPU间接分支依赖resident range与后续slot不被退休的断言，原报告仍无目标的原始xyz/间接参数和实际分支。其他设备日志中的 `groups=11,7,1` 只保留其本次会话归属。

现有目标 loaded 行之后还可能执行descriptor内的Vulkan调用、runtime pipeline或其他线程的warm pipeline。当前实际代码与原报告尚未把故障线程对齐到其中任何一处。本轮没有找到能由该真实canonical和已有动态日志共同证明的新SPIR-V改写或dispatch修复，原G925故障继续保持未归因。

### 接续所需的最小原机证据

复用现有 `gfx-api-breadcrumb`，先核对该次session PID、runtime_module最终words/FNV、descriptors及warm/runtime pipeline、dispatch_record的begin/end和TID。若 module FNV不同，优先保全原机实际final bytes与variant identity。若 descriptors begin后缺end，再定位内部layout/view/allocate/update；若pipeline begin后缺end，再保全matching vendor BuildID/栈与该次真实layout/module。目标实际bound image格式和dispatch输入应来自同一session。该工作需要原G925设备或它导出的新增证据，本代理未操作设备。

以上采用官方SPIRV-Tools与Vulkan文档作为外部核对资料。2026-11-18获取到的上游HEAD分别为 `727277f679857e5a680cf559586268b2df35c48d` 和 `f7de774cace1509af0f10b8d859c973b5354ff62`，可取得源码。现有宿主工具的构建revision标为unknown hash，不能把该HEAD认作本机工具的构建revision。工具适用范围为SPIR-V离线处理，当前报告没有G925驱动离线编译/实机执行。未拷贝上游代码。[SPIRV-Tools 许可证](https://github.com/KhronosGroup/SPIRV-Tools/blob/727277f679857e5a680cf559586268b2df35c48d/LICENSE) [Vulkan-Docs 许可证](https://github.com/KhronosGroup/Vulkan-Docs/blob/f7de774cace1509af0f10b8d859c973b5354ff62/LICENSE.adoc)

# P-CX-TERRA-dark-limit-black-NEXT-20261106

## 目标与接续边界
只修 Honor 暗魔法 / Limit 异常纯黑，先正确性；不扩展到 60Hz、内部1080或同步性能。接续 Codex 01a1b150-ce60-73c1-bd72-da262406362b 的证据，不预设共同根因。

工作树 ONLY `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`，分支 `work/honor-gtpro-30hz`，核对 HEAD `032e5f68172c101cb2b6e44ee06f02974dab2da8`（若已推进，先核对新增变化，不回退）。认领现有大量 dirty/untracked，记录自己的 diff，禁 reset/clean/stash -u/另开 worktree 覆盖 WIP。禁碰 RG 主仓、`.worktrees/codex-black-screen`；禁清 app 数据/存档/FFRS，禁卸载；禁 adb kill-server。

先完整读 AGENTS.md、AGENTS-HONOR.md、本单 RESULT、HANDOFF 顶部及暗魔法段、原 P-CX-TERRA-dark-limit-black-20261106.md、`coordination/prompts/_STANDING-audio-acceptance.md`。本 NEXT 纠正原提词对“已有可重放 capture”“颜色恒为0”的过强表述；冲突以当前实际证据为准。

## 已知事实 / 不可越界的结论
- 最新 f23465：`codex-probe-28053-f23465/`，g80 pre 749147 samples / g96 color 419（VK_SUCCESS / available1）。颜色不是所有帧都为0；不要继续声称恒为0。
- 该同帧两 pass binding18 GPU 有效两字节均 00 00，仅否定该帧两次读取值不同；不证明解码正确、输出正确或其他帧相同。
- WIP 当前 binding18 VkFormat98 R32_UINT + explicit range2 是明确 Vulkan 合规风险；原始 float Buffer 与 integer rewrite 的实际 SPIR-V 类型需核对。旧 f10935 VkFormat76 R16_SFLOAT 时已黑，因此“非法 view”不是已证历史首因。
- f9558 的 UBO、f10564 的 GPU texels、f8115 的 CPU mesh 来自不同帧，不得拼装冒充同帧 GPU 重放；最新 binding18 读回也未补齐其余资源。
- binding42 VkFormat139 是 BC4；旧把块当普通像素的 probe sample 不证明纹理正确或可重放。
- 原始 SPIR-V：pre FS c0adc062e4b8e8fa 有条件 OpKill；color FS b7dd0f48aea00196 无 kill/demote，二者写 FragDepth。静态存在这些指令不等于运行时首错定位。
- 黑块早于最终 present，在 HDR 中已有；不重开 RG16/LUT/最终合成大循环。ALWAYS/cull NONE、空FS等侵入诊断未证明修复。

证据根 `.local/honor-device-profiles/flame-20261104/`：
`dark-depth-pair/` 四套原始 VS/FS；`codex-probe-28053-f23465/` 最新捕获；`codex-probe-31699-f9558/`、`codex-probe-3882-f10564/`、`codex-probe-1031-f8115/` 分散旧捕获；`shield-candidate/` 旧诊断；`switch-reference-20261106/01.jpg`–`05.jpg` 参考照；`.local/baselines/honor-flame-stable-20261104/` 已知好基线不得破坏。

## 第一刀：先消除 / 隔离非法 view，不冒认根因
1. 核对实际 shader module（原始与重写后）、descriptor 类型、buffer offset/range/format、读入后的数值转换及缓存 key；分清诊断4-byte copy与渲染2-byte view，不混为一谈。
2. 限域修复或隔离 R32_UINT/range2：对2字节采用设备支持、与 shader 语义匹配的合法格式及范围，或避免创建非法 view。不能仅改格式却保持错误整数/浮点解释；不能简单将 range 扩成4读取无权/未定义邻字节。若必须重新打包，需证明填充值、边界、布局、同步与所有权正确。
3. 覆盖范围/块大小、offset alignment、format feature、shader取数类型的一致性，增加可复现正反控制和针对性回归证据；不泛化修改不相关shader。
4. 单独报告 VIEW_VALIDITY_FIXED / NOT_FIXED。即便合法化成功，也绝不等于 DARK_PASS 或 LIMIT_PASS；旧 R16 黑块仍需解释。

Vulkan出处（本轮已核验）：https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferViewCreateInfo.html 。显式 range 须为格式 texel block 大小整数倍（00929）；同时检查 offset alignment、buffer边界和format features。不要以 VK_WHOLE_SIZE 逃避语义/边界检查。

## 第二刀：找到第一个错误 GPU 输出，而非继续堆输入相等日志
先列资源覆盖矩阵：每个资源所属 frame/draw、CPU还是GPU可见、完整还是抽样、已缺项及获取方式。复用有效同帧资产与 dark-depth-pair，但不得假定当前 capture 已可完整重放。

若资产不齐，先写出一次最小活体捕获计划，补齐同一帧的 GPU几何/索引、instance、全部UBO/texel（含binding18）、binding42完整压缩图像及view/sampler、关联纹理、颜色/深度附件、pipeline/draw状态及资源同步世代。保存相应原始/实际重写shader。采用真正保留上述资源的帧捕获或有资源清单的opt-in诊断；每次捕获须校验目标pair存在并完整END。

暗魔法 pair：pre VS db5f1e2dd1c9ef3d / FS c0adc062e4b8e8fa；color VS 41a5a80e5242a26c / FS b7dd0f48aea00196（EQUAL）。在相同已证明输入的受控重放，逐项判别 Position/关键varying → raster覆盖 → discard → FragDepth/depth fail → FS颜色输出及blend写入。给出第一个偏离客体预期语义/可信对照的GPU输出及具体primitive/pixel证据；两个pass本可有不同覆盖，单纯“不一致”不是错误证明。每次只检验一个假设，保留正常状态对照和扰动说明。

Limit另一pair：pre VS 085afbc955a0744f / FS 0066ad2ea935247d；color VS 632c207422d773c5 / FS 17ab021c45c63390（GEQUAL，实际捕获需核实）。暗魔法打穿后独立定位/验收，不能自动套补丁和PASS。

禁用作交付：暂停后目标draw缺失的错相位“证据”、空FS、删预通道、ALWAYS/全局关depth、禁颜色写、跳effect draw、全局waitIdle、泛化关缓存/删同步。探针限域opt-in，关闭后不改变正常语义；诊断计数不用于正常帧率证明。

## 真机最小影响与停点
设备 ONLY `adb -s A42BBB5109000333`。设备是否空闲不确定时仅离线；不要把日志里历史PID/marker当实时状态。需要构建/覆盖安装/冷启/战斗输入时先列最小操作与影响并协调占用；无明确协调不碰机。禁止清数据/卸载/kill-server。若需 install -r，先记录包名、APK SHA与保留数据方案；捕获完还原 marker0 并记录现场状态，不随意杀进程。

先做离线资产清单和view合法性验证；仅当完整重放确实缺输入时安排一次有验收清单的活体捕获。不要再构建“只证明输入某两字节相等”的孤立诊断APK。若工具/设备阻塞，明确唯一最小缺项与下一步，绝不把NOT_RUN写PASS。

## 正确性验收与交付
普通非probe marker0下，暗魔法与Limit分别列PASS/FAIL/NOT_RUN：暗魔法蓝白亮核+紫红外圈、正常动态结构与遮挡，非异常纯黑；Limit按后三张带Skip的不同阶段验证紫色背景、红蓝光柱、蓝色冰晶等结构。参考照有反光/曝光和时相差异，仅作结构/颜色对照，不逐像素比较，不拿不同阶段当修复前后AB。保留shield/fire/boss基线。仅黑像素减少不算PASS。

验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。AAudio默认PERFORMANCE_MODE_NONE，fence_spin_us默认0，蓝牙A2DP野外/战斗各≥2分钟主观无断续，连接/断开自动重开流、TIMEOUT重试、缓冲不回退；记录包名/APK SHA。未测音频即NOT_RUN，不得报整刀PASS。

更新 `coordination/results/P-CX-TERRA-dark-limit-black-RESULT.md` 并在 HANDOFF 顶部追加：
- 当前HEAD/自己的diff范围/实际shader与APK SHA、marker和设备状态；保留原有历史记录。
- view合规修复、第一错误GPU输出、暗魔法视觉、Limit视觉、遮挡回归、音频、独立QA分别状态。
- 每条结论绑定同帧/同阶段证据路径、输入覆盖清单和缺项；已证伪假设注明范围，不越推。
- OSS-NOTES含可访问来源及其对应假设；未经独立核验的Ryujinx链接不得当事实。
- 最小修复前后同条件证据、未闭合项和下一步；不声称60/内部1080。

完成可验证修复与普通运行验收后停；或精确报告阻塞（缺哪一个输出/资源、一次最小操作如何补齐）后停。不要循环空转堆APK。

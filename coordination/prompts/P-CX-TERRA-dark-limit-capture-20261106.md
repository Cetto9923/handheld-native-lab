# P-CX-TERRA-dark-limit-capture-20261106

## 0. 接续 / 单写手 / 设备窗口
写手仍为 Codex gpt-6.1-terra，同一线程 `01a1b150-ce60-73c1-bd72-da262406362b`。这是上一刀 task_complete 后的捕获能力下一刀，不另开线程/工作树，不引入第二个 writer。只在 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro` 现有 dirty 树工作，分支 work/honor-gtpro-30hz，已核 HEAD 032e5f68172c101cb2b6e44ee06f02974dab2da8；若 HEAD 已推进先核对，不回退。保留全部既有 dirty/untracked，记录自己的改动范围。禁 reset/clean/stash -u/覆盖 WIP。

用户 t144u「那你帮我跟进到底 我pad连着的」已确认 Honor 空闲给本刀。授权调试窗口至约 **2026-11-07 03:00 Asia/Shanghai**；过点不得自行延长设备操作。可为本目标做必要、最小的构建、保留数据覆盖安装、同包冷启及战斗复现/捕获，不再把“设备是否空闲未知”当作旧阻塞。若实时发现设备断开、另有占用或新冲突则停止设备操作并报告。

设备命令一律 `adb -s A42BBB5109000333 ...`；禁碰 RG477M（即使 devices 列表也显示它），禁 adb kill-server。禁动 ff-resonance-rg477m-native 和 .worktrees/codex-black-screen。**傻瓜数据目录/自动发现规则不变**，禁止清 `/storage/emulated/0/FFRS`、app files、存档、卸载清数据、数据迁移或目录改名。先正确性，不扩展 60Hz、内部1080、同步/性能重构，不以牺牲声音冲帧。

2026-11-06 23:46 Asia/Shanghai 协调复核：A42BBB5109000333=device，前台完整 component `org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity`，包 `org.ffrs.gamewindow` PID28053。**GameActivity 的类名包不是安装包名**，不要把 org.ffrs.diagnostic 当安装包。开工再核实时状态，能复用就复用；必要时冷启同包，不能凭历史 PID 操作。协调端本次未安装/输入/重启。

## 1. 先读与证据纠偏
先读 AGENTS.md、AGENTS-HONOR.md、`coordination/results/P-CX-TERRA-dark-limit-black-RESULT.md`、`coordination/HANDOFF.md` 顶部与暗魔法/Limit相关段、`coordination/prompts/P-CX-TERRA-dark-limit-black-20261106.md`、`coordination/prompts/P-CX-TERRA-dark-limit-black-NEXT-20261106.md`、`coordination/prompts/_STANDING-audio-acceptance.md`。只读复用 `.local/honor-device-profiles/flame-20261104/codex-probe-28053-f23465/` 及 dark-depth-pair、旧分散 probe、Switch参考与既有基线，绝不删旧探针。

本单将 NEXT 的先后优先级明确调整为：**首先实现并验证同 draw 完整 GPU 捕获能力**。不是再写一份“需要捕获”的计划、继续证明输入相等、或只生成离线 shader 变体后退出。请自主调查选择可实际运行的 Android Vulkan 捕获工具或项目限域 opt-in 捕获实现；上刀有限路径未发现 RenderDoc/gfxreconstruct/AGI 不等于工具无法准备，也不等于目标不可实现。不要借机读取凭证/账户数据、绕过授权或安装侵入式全局 hook；若需要额外权限/不可逆外部动作则停前报告具体需求。

必须保留纠正：
- 最新 f23465 g80 pre=749147 / g96 color=419（VK_SUCCESS、available1），**color 不恒为0**。
- 同帧两 pass binding18 的 GPU 有效两字节均 0000；仅证明该帧两个值相同，不证明格式/数值语义正确，不外推所有帧。
- binding18 R32_UINT(VkFormat98)+explicit range2 违反 VUID-00929，是须限域纠正的真实 view 风险，但旧 R16_SFLOAT(VkFormat76)时已黑，**不等于黑特效根因**。
- binding42 VkFormat139=BC4，旧普通像素式缩样不代表完整压缩纹理。f9558/f10564/f8115 等跨帧证据不能拼作同一 draw 输入。
- 离线重建/spirv-val通过不是设备实际 shader module dump，更不是实际GPU输出或视觉验收。
- 黑块在 HDR 中间图已存在，不重开最终合成alpha/RG16/LUT循环。两 pass 覆盖不同本身也不等于错误。

## 2. 第一优先级：建立同 draw 完整捕获并验证完整性
唯一精确阻塞是缺同 draw 的完整 GPU 可见输入、实际下发 shader 与附件，无法可信同帧/离线重放。把这项能力做出来并实际捕获，优先打穿暗魔法再独立处理 Limit。

目标 pair：
- 暗魔法 depth pre：VS db5f1e2dd1c9ef3d / FS c0adc062e4b8e8fa；color：VS 41a5a80e5242a26c / FS b7dd0f48aea00196（EQUAL）。
- Limit 候选 pre：VS 085afbc955a0744f / FS 0066ad2ea935247d；color：VS 632c207422d773c5 / FS 17ab021c45c63390（GEQUAL，须以本次实际draw核实）；禁止假设同根因。

产物在 `.local/honor-device-profiles/flame-20261104/` 建本刀全新子目录，如 `dark-limit-capture-20261106-<time>/`，旧目录只读。捕获必须有 frame/submit/draw/group/pass 唯一身份、目标pair存在、完整END、文件清单/尺寸/hash和可复现读取/重放方法。按 shader实际读取范围与动态绑定覆盖，而不是只写死旧日志里的几个binding。至少：
1. 目标draw前GPU可见 vertex/index/instance、所有UBO/SSBO/texel buffer和实际buffer view字节；保存 offset/range/format/stride/bind与资源世代。证明copy发生于正确时点且有正确producer→consumer同步与fence，不拿CPU mapped prepared快照冒充GPU输入。
2. 所有实际绑定且shader可访问的纹理；binding42完整可达BC4 mip原始压缩块（旧样本10mip，实际核实），含层/面/子资源、image/view格式与swizzle、sampler/filter/address/LOD、layout。不能只存128×72缩样或把BC4块按像素解读。
3. 实际送入 vkCreateShaderModule 的完整SPIR-V字节（包括当前重写后版本）及hash，并关联具体pipeline与draw；若采用可重放源必须能证明与实际下发一致。缓存命中也须可追溯实际module，不以当前源码离线重建替代。
4. 所有参与color/depth/stencil附件draw前后完整相关子资源，保留原格式/样本数、RT/view、load/store/clear、subpass/renderpass依赖等；还原depth/stencil/blend/color mask/viewport/scissor/cull/raster/MSAA、push constants、descriptor/dynamic offsets、index/instance/baseInstance等必要pipeline/draw状态。深度/模板各aspect不能漏。
5. 生命周期与提交依赖、resource alias/重用/写入世代清楚；没有完成/越界/缺资源要显式标记，捕获不完整时不得 CAPTURE_COMPLETE=YES。

实现限域 opt-in、默认路径语义不变。有限触发/重试、单一目标清单先闭合，避免海量全局dump、磁盘打满或每次只多抓两个字节的孤立APK。捕获前定义资源覆盖矩阵与负控制，落盘后实际核对所有资源/状态/字节、目标pair/END及同帧一致性。复放至少给出未扰动重放与原始同draw输出的一致性证据；无法做到时明确差异和缺项，不能靠“文件都有”宣称可信。禁止暂停后目标effect draw不存在的错相位证据。

## 3. view 合法性可并行限域修，但不能替代捕获
捕获过程中能安全修 VUID-00929 就修，修完继续捕获，不把合法化成功当视觉修复。核对实际shader取数类型、来源格式、range/offset/alignment、设备format features、view cache key/生命周期及同步；覆盖非零、负零/边界及动态descriptor正反例。half3c00经R16_SFLOAT→float的比特是3f800000，简单零填充R32是00003c00，0000相同不证明数值语义。

不得盲目把range2扩为4越界读取邻字节，不得全局删整数重写表项而破坏其它绑定。诊断4字节copy不是渲染view修复；仅离线 spirv-val 不算实际格式/语义验证。VUID来源 https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferViewCreateInfo.html ，独立核验来源后写OSS-NOTES。若安全修复暂时不能闭合，单独标记VIEW_VALIDITY未修/对可信重放的影响；不要隐去UB后再称重放有效。

## 4. 捕获够用后立即首错定位和最小修复
具备可信同帧输入与shader/附件后，立即受控对比 **Position/关键varying → raster覆盖 → discard/kill → FragDepth/depth-stencil → FS color/blend**，定位第一处偏离可信客体语义/对照的GPU输出，给具体primitive/pixel、输入/输出、可复现实验与证据路径。两pass本可不同，不能只用计数/相等性定根因。每次只改一项假设，标明探针对语义/时序的扰动；Limit另对独立定性。

禁止作为修复：ALWAYS、删除预通道、全局关depth/同步/cache、空FS、跳effect draw、禁颜色写或全局waitIdle。若需要诊断扰动必须限域可关，交付普通marker0路径。

## 5. 视觉和音频硬门
有画面修复就实际做暗魔法+Limit分别视觉验收与shield/fire/boss/遮挡回归，marker0普通运行，同条件修复前后证据。Switch参考目录 `switch-reference-20261106/01.jpg`–`05.jpg`：暗魔法蓝白亮核/紫红外圈；Limit不同阶段紫背景/红蓝光柱/蓝冰晶。拍屏曝光/时相不同，参考结构颜色不作逐像素金标准；黑像素比例下降不单独算PASS。

**验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`**。任何装包/隔离实验/抬帧都受其约束；默认AAudio PERFORMANCE_MODE_NONE、fence_spin_us=0，TIMEOUT重试、DISCONNECTED/设备变化自动重开、缓冲不得为冲帧砍薄。A2DP野外/战斗各≥2分钟主观连续、蓝牙连接断开重开流等逐项记录，不能用xruns=0或能出声冒充PASS。若当前没有耳机/人耳主观判断条件，明确NOT_RUN/阻塞，不伪造主观验收，不报整刀PASS。音频坏立即遵循常设门回已知顺滑配置，不继续冲帧。

构建/安装先记录包名/APK SHA/source与最小影响，仅 install -r 保留数据；复用/冷启包 org.ffrs.gamewindow，运行component以实时查询为准。捕获后还原marker0并记录设备/前台/现场状态。不要随意杀进程。

## 6. 交付 / 停点
更新既有 `coordination/results/P-CX-TERRA-dark-limit-black-RESULT.md` 并在 `coordination/HANDOFF.md` 顶部追加本刀记录，保留历史。可另存capture manifest/README，但不能只写新文件不更新旧入口。必须含：
- CAPTURE_COMPLETE=YES/NO；暗魔法/Limit各自完整性状态，具体缺什么/为何/最小下一步；捕获目录、同draw身份、资源矩阵、实际shader hash、验证/复放命令与输出。
- VIEW_VALIDITY=FIXED/NOT_FIXED/NOT_RUN（兼容保留VIEW_VALIDITY_FIXED字段），区分格式合规与真实语义证据，不混同黑块根因。
- FIRST_WRONG=定位结论或NOT_FOUND/NOT_RUN；阶段、draw/primitive/pixel与可信对照证据。
- VISUAL_DARK、VISUAL_LIMIT、AUDIO 各PASS/FAIL/NOT_RUN及证据；遮挡/shield/fire/boss、独立QA分列；音频常设门逐项状态。
- HEAD、自己的diff范围、实际source/APK/shader SHA、设备/marker状态、命令/构建日志、OSS-NOTES、剩余缺口。时间均用Asia/Shanghai。

停止条件：可验证最小修复+普通视觉/音频验收；或确实尝试构建捕获能力后遇到具体不可跨越的工具/资源/权限阻塞并给出精确缺项与最小下一步；或达到授权窗口03:00 Asia/Shanghai。不要又因旧“未发现现成工具”/“设备未知”原样退出，不空转堆探针APK，不把NOT_RUN写PASS，不声称60fps/内部1080。

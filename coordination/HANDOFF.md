# 原生移植接力入口：完整游戏尚未启动

更新时间 `2026-10-23T13:08:46.175383+00:00`。先读本仓库 `AGENTS.md`，然后核对Git/当前代理和本文件引用的原始证据；旧聊天不作为当前工作树状态。

## 最新现场（优先于下面历史冻结快照）

3db资源/locale已同RG13组件合同0+info0；原startup空argv越66到67/+137a4/133，非空越63到strcasecmp61/+1345c/133。malloc出现真实139/main+b705c/fa30；脚本保留原码并停止，3db CRT本轮NOT_RUN，独立QA进行中。root尚未集成3db。当前运行时写者已移新树ffrs-startup-next/task/T02-startup-next/base3db五文件做61/67，旧ffrs-va-resource/3db clean冻结；另只读mallocprovenance在startup-followups/malloc-null。不要填默认对象/假能力/写TP。

LAS81已集成root7e5123c且独立19命令FINAL；root GP21header组合重建58expected后新ELF同RG0/113.853958ms、64KiB+256BzeroMismatch、validator0。当前root交接/reportWIP待checkpoint，原交付仍实核f9284ea clean。

## 目标与边界

《最终幻想：共鸣 / FINAL FANTASY RESONANCE》Switch版→RG477M Android ARM64真正原生可玩；荣耀GT Pro平板等比例缩放/必要留边、不拉伸裁切。用户授权并行开发、本地提交和设备诊断；不推远端/发布/购买/清数据/root/刷机，不获取新游戏或密钥，不停止或消息其他工具，不reset/clean/stash别人成果。

当前没有标题/可操作场景。诊断v6 JSON页面不是游戏画面。无可靠总量，不报整体百分比。无CreateGoal/UpdateGoal入口，当前是本回合持续执行目标，未开启持久后台引擎。

## 仓库与当前写者

- 原交付 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，branch `work/rg477m-native`，用户明确允许快进后唯一重试成功，本次只读核验clean `f9284ea44f10a0d3a838c554508a3e911635b49f`，未push；旧拒绝仅历史，当前无待批。后续用git重查，不能猜包含本文档的自身SHA。
- 总控 `../ffrs-integration`，`task/codex-integration`，本轮只读HEAD `7698a9e6b6f3676277367523296651f3b5501133`（已保存LAS真机0及旧审批历史）；9dc→3af578a、7be→9cd3352。root唯一ADB/Git，DOC四文件现已暂停交回root串行保存；恢复writer前不再写，不能把未提交文档当原交付已快进。
- 冻结运行时 `../ffrs-runtime` / `task/T02-codex-runtime`：9dc8aa73… clean；禁止在此树覆盖冻结产物。
- **活跃运行时** `../ffrs-va-resource` / `task/T02-va-resource`，base3af578a，`/root/runtime`十二既有文件：Context/pages/region/Process c/h、CLI、contract、builder、report。真实management arena/quota/metadata和66返回、官方Bionic63复用在编码，host合同0，Android构建中/未冻结/未设备验收；不要自动换5c依赖到7be。
- **冻结图形** `../ffrs-las-transfer` / `task/T04-las-transfer`，clean **81b1917adb261f80ce1780e2c16d8d31c536a693** 十一文件；typed→CmdRec→vkCmdCopyBuffer已同RG exit0/100.1925ms、validator0。作者已交回idle；独立`functional-review/las-transfer/`合同0、报告收口，尚未FINAL，未报告代码集成。
- **ABI报告已交回** `/root/tls_research` idle，63 [locale报告](../docs/analysis/codex_locale_compare_report.md)SHA483f79ab…bed6；实际REA函数切片前Swifthelper编译失败1，非timeout、无functionEvidence。独立LLVM精确证明SDK忽略locale，官方Bionic对应实现可复用；不称REA成功反编译。当前活跃QA是LAS，不是已FINAL的9dc。
- pages4695/OS9c/boundary5c均保留独立冻结树及FINAL报告；stack13 `../ffrs-native-stack13` clean7be，本轮root已集成，review完整C编译timeout限制仍保留。

详细占用是 `state.json.active_writers`，它是协调记录，不是OS锁。接手先确认原写者是否还活跃及WIP，不按时间抢占。`.local`符号链接先readlink核对，不能覆盖共享证据；新产物用唯一新目录。

## 9dc历史冻结关卡（新3db见上）

| 实际RG477M/9dc | 原始码 | 结论 |
|---|---:|---|
| 六个正常/参数/native参数/恢复/guest析构/retained合同 | 0 | 参数真实复制、计费、native getter、析构仍存活、真unmap后退款 |
| 原始nninitStartup空argv | 133 | 已越59/60/61，停VA usage66/caller+0x13744 |
| 原始nninitStartup两个argv | 133 | 停strncasecmp_l63/+0x13498，n29/locale0 |
| 原始direct nnMain / malloc / CRT | 133 | 分别CreateThread126 / VA66 / musl515，尚未完成初始化 |

全部无timeout，main SHA前后相同。真实执行[报告](../docs/analysis/codex_guest_process_device_report.md)，9dc [独立审查](../docs/analysis/codex_guest_arguments_review.md)53命令符合实际预期0/1/-6/2、无timeout；reviewer Android NOT_RUN，不把root真机算独立设备QA。空argv也拥有/计费4KiB，当前Query1GiB是显式port-policy，不是原SVC RAM或host RSS。GP21两合同另RG0，只证明固定21词双向运输，不是musl实现；[review完整C编译20秒超时](../docs/analysis/codex_native_stack13_review.md)。

原始日志：总控 `.local/device/rg477m-session1/guest-arguments-v1-usb-ready-{plan,stage,execution}.json`、各stdout/stderr、`run_guest_arguments_v1_usb_ready.py`。每例含完整argv/timeout/原始码/产物hash。首次两次identity失败1没有push/执行，日志保留；USB后已真实成功，无需重复要求用户连接。

原始main SHA `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`；SDK SHA `9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9`。素材只引用原交付 `.local/uncompressed/`；不提交素材/生成游戏代码。诊断APK v6保持451092B/SHA4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a，无本轮更新安装。

## 方法与复用

REA Skill42由相关分析代理本轮实际读取；此前Codex实际使用未找到证据，不能把LLVM分析改称REA。当前MCP0，公开CLI6.5.0用项目隔离npm缓存、Ghidra12.1.4已可用，无全局设置变更。新63定点CLI在函数切片前helper编译失败1，无函数Evidence；LLVM SDK切片与官方Bionic来源另记，不能归为成功REA。匹配已有结果不重分析，普通开源源码用正常工具。详见[方法审计](../docs/analysis/codex_method_reuse_audit.md)。

优先采用LASRecomp Beta4 `fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93`自有MIT的命令记录与API语义，保留许可证和来源；该最小生产接入81已冻结并RG实测0，不整套CPU架构重写。它只证明另一游戏Beta/部分平台，不证明本游戏Android；其COPY_BUFFER CPUmemmove不当GPU证据、ClearBuffer缺executor不复制假成功、GetInfo16/17常量0不采用、shaderc输入已是GLSL不当Maxwell翻译器。Strato/Hades许可证/guest依赖界限及静态路线OOM旧论据撤回，见[复用评估](../docs/analysis/codex_reuse_route_review.md)、[LAS核验](../docs/analysis/codex_las_implementation_review.md)。

可复用已实测：NSO/重定位和记录导入重定向、Context/stack/pages唯一票据、native x18/SCS typed边界、32槽逻辑OS TLS生命周期、owned argv、生产VkBuffer。适用已声明ABI子集；本游戏专用main/SDK白名单及偏移不当任意游戏支持。自有9216B无游戏字节boundary fixture已在RG两次真实native调用0。仅研究/未完成：全CRT/ELF TLS/ThreadType/alias、NVN shader/present、资源挂载、游戏输入音频保存/性能。

## 恢复与下一步

1. `git status --short`/`rev-parse HEAD`核对各树，检查 `state.json`写者；读取最新freeze/manifest，核验hash后复用日志，不能盲重放stage。root/代理活动通过实际任务与本地产物核对，无假后台进度。
2. 运行时完成真实有界management pool/usage双寄存器返回，自有fixture/耗尽/回滚/cleanup后冻结→独立QA→rootRG复测原始startup。63需可证locale语义，禁止guest locale_t强转host或假返回0。
3. LAS最小生产接入/原driver同Mali真实copy/readback已0，独立报告尚未FINAL；后续依据真实调用链接NVN/资源/着色器/present，仍无游戏NVN绑定。继续原启动/CRT→真正图形/title→场景，不在探针或单项合同通过处停。
4. 构建命令以当前阶段builder manifest完整argv为准：`scripts/runtime/build_guest_process_contract.sh --out <本树.local唯一目录> --toolchain <既有partialNDK>`；默认依赖完整集成树。不能把完整NDK归档NOT_VERIFIED说成已完整安装。
5. ADB仅总控：`/Users/yuyan9923/Library/Android/sdk/platform-tools/adb -s RG477M01445122 ...`，device timeout15/host22（组件受控子进程例30/40），保原始退出码；不tail掩盖。真机型号/GPU/ABI复核已完成，面板120Hz不当游戏FPS。AVD历史独立记载，本轮未新启动；荣耀平板未连接。

已否定路线：mmaptest未设MAP_FIXED不能推断必须地址0；tail曾掩盖139不能算成功；最长候选relocation不是完整性；简单叶函数或musl早return2不能算CRT；没有测试证据的94MB必OOM论据撤回。不重试旧被拦安全/异常QA，不再读取被拒Codex UI。当前无待审批或不确定写入；只有runtime、DOC、functional QA三活跃worker，vulkan/tls已交回idle。编译/QA精确日志由各活跃writer产物manifest记录，不能按进程存在宣称推进。

详细历史/命令索引：[证据索引](../docs/analysis/codex_handoff_evidence_index.md)；当前分阶段事实：[stage status](../docs/analysis/codex_stage_status.md)。

**可复制接手句：** 先读AGENTS.md和coordination/HANDOFF.md，核对HEAD/WIP/代理写入归属并保留成果，从当前真实启动66/63与CRT515继续，优先复用已核验组件，禁止以诊断合同或空画面冒充游戏成功。

## 最新事件：LAS真机与原分支审批

图形81b1917已同RG真实执行0/100.1925ms、无timeout、stderr空；新typedrecord路径64KiB复制及256B清除回读0差异，31VkResults0，12destroy/3unmap，validator0。typed_record_path=true/copy1/65536/gpu_completion_retired=true，未写opaque layout，无shader/present。证据 `las-transfer-v1-{plan,stage,execution,result,validator}.json`；独立新组件QA合同0、报告收口，尚未FINAL或集成；没有shader/present/game。

**历史拒绝（已解除）**：向原交付work/rg477m-native快进f9284ea曾被自动审批拒绝且未执行，审核要求原分支单独授权；该拒绝记录保留。随后用户明确批准该分支，唯一重试成功，原树已f9284ea clean、无push。当前从本任务树更新后的HANDOFF接手，不再blocked/pending，也不重放已完成操作。

解除核验时间2026-10-23T13:04:26.996605+00:00；本轮再次只读确认原分支f9284ea clean。任务树7698a9e另保存LAS真机事件；后续仍先用git核验。

最新LAS集成事件：独立review已FINAL，19命令符合原始预期11×0/5×2/2×3/1×4，无timeout，报告[LAS review](../docs/analysis/codex_las_transfer_review.md)SHAf52134352d7c57c179156c69d7ca491f7fd23ba4760a33e449c8f489708a2b49。81已集成root7e5123c；当前root因7be header组合做一次独立新产物重建，session43347/logs.local/las-transfer/integrated-7e5123c，尚未据此声称新组合二进制devicePASS。root重新持有四交接文件，DOC已暂停。

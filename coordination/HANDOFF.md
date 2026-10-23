# 原生移植接力入口：完整游戏尚未启动

更新时间 `2026-10-23T12:58:49.280245+00:00`。先读本仓库 `AGENTS.md`，然后核对Git/当前代理和本文件引用的原始证据；旧聊天不作为当前工作树状态。

## 目标与边界

《最终幻想：共鸣 / FINAL FANTASY RESONANCE》Switch版→RG477M Android ARM64真正原生可玩；荣耀GT Pro平板等比例缩放/必要留边、不拉伸裁切。用户授权并行开发、本地提交和设备诊断；不推远端/发布/购买/清数据/root/刷机，不获取新游戏或密钥，不停止或消息其他工具，不reset/clean/stash别人成果。

当前没有标题/可操作场景。诊断v6 JSON页面不是游戏画面。无可靠总量，不报整体百分比。无CreateGoal/UpdateGoal入口，当前是本回合持续执行目标，未开启持久后台引擎。

## 仓库与当前写者

- 原交付 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，branch `work/rg477m-native`，本次实际核验clean `245dfa222f22da6998819d4e0773bde9d8e6ae71`。后续用git重查，不能猜包含本文档的自身SHA。
- 总控 `../ffrs-integration`，`task/codex-integration`，本文档写入前HEAD `9cd335209082e9cac0d11d53e45aec60ca2b56fd`：9dc→3af578a、7be→9cd3352；root独占ADB/Git/四交接文件。本文档和报告待串行保存后才safeff原交付。
- 冻结运行时 `../ffrs-runtime` / `task/T02-codex-runtime`：9dc8aa73… clean；禁止在此树覆盖冻结产物。
- **活跃运行时** `../ffrs-va-resource` / `task/T02-va-resource`，base3af578a，`/root/runtime`十二既有文件：Context/pages/region/Process c/h、CLI、contract、builder、report。实施真实management quota/metadata allocator；不要自动换5c依赖到7be。
- **活跃图形** `../ffrs-las-transfer` / `task/T04-las-transfer`，base245dfa2，`/root/vulkan`十一文件：MIT许可/UPSTREAM、nvn_las五文件、contract/builder、既有Vulkan driver、report。真实typed→CmdRec→vkCmdCopyBuffer接入，冻结构建中，Android本轮NOT_RUN。
- **活跃只读ABI** `/root/tls_research`，`../rea-followups/locale-compare/`，新63 locale语义；不改源码/不ADB。9dc独立review `/root/functional_review`已FINAL/暂停；旧安全QA被平台中止，禁止重试该异常workflow。
- pages4695/OS9c/boundary5c均保留独立冻结树及FINAL报告；stack13 `../ffrs-native-stack13` clean7be，本轮root已集成，review完整C编译timeout限制仍保留。

详细占用是 `state.json.active_writers`，它是协调记录，不是OS锁。接手先确认原写者是否还活跃及WIP，不按时间抢占。`.local`符号链接先readlink核对，不能覆盖共享证据；新产物用唯一新目录。

## 最新真实关卡

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

REA Skill42本轮实际读取；此前Codex实际使用未找到证据，不能把LLVM分析改称REA。当前MCP0，公开CLI6.5.0用项目隔离npm缓存、Ghidra12.1.4已可用，无全局设置变更。新未解binary问题定点分析并保留Evidence；匹配已有结果不重分析，普通开源源码用正常工具。详见[方法审计](../docs/analysis/codex_method_reuse_audit.md)。

优先采用LASRecomp Beta4 `fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93`自有MIT的命令记录与API语义，保留许可证和来源；现有图形Agent正在实际接入，不整套CPU架构重写。它只证明另一游戏Beta/部分平台，不证明本游戏Android；其COPY_BUFFER CPUmemmove不当GPU证据、ClearBuffer缺executor不复制假成功、GetInfo16/17常量0不采用、shaderc输入已是GLSL不当Maxwell翻译器。Strato/Hades许可证/guest依赖界限及静态路线OOM旧论据撤回，见[复用评估](../docs/analysis/codex_reuse_route_review.md)、[LAS核验](../docs/analysis/codex_las_implementation_review.md)。

可复用已实测：NSO/重定位和记录导入重定向、Context/stack/pages唯一票据、native x18/SCS typed边界、32槽逻辑OS TLS生命周期、owned argv、生产VkBuffer。适用已声明ABI子集；本游戏专用main/SDK白名单及偏移不当任意游戏支持。自有9216B无游戏字节boundary fixture已在RG两次真实native调用0。仅研究/未完成：全CRT/ELF TLS/ThreadType/alias、NVN shader/present、资源挂载、游戏输入音频保存/性能。

## 恢复与下一步

1. `git status --short`/`rev-parse HEAD`核对各树，检查 `state.json`写者；读取最新freeze/manifest，核验hash后复用日志，不能盲重放stage。root/代理活动通过实际任务与本地产物核对，无假后台进度。
2. 运行时完成真实有界management pool/usage双寄存器返回，自有fixture/耗尽/回滚/cleanup后冻结→独立QA→rootRG复测原始startup。63需可证locale语义，禁止guest locale_t强转host或假返回0。
3. 图形完成最小LAS API生产接入→原driver同Mali真实copy/readback；这仍不等于游戏NVN绑定。继续原启动/CRT→真正图形/title→场景，不在探针或单项合同通过处停。
4. 构建命令以当前阶段builder manifest完整argv为准：`scripts/runtime/build_guest_process_contract.sh --out <本树.local唯一目录> --toolchain <既有partialNDK>`；默认依赖完整集成树。不能把完整NDK归档NOT_VERIFIED说成已完整安装。
5. ADB仅总控：`/Users/yuyan9923/Library/Android/sdk/platform-tools/adb -s RG477M01445122 ...`，device timeout15/host22（组件受控子进程例30/40），保原始退出码；不tail掩盖。真机型号/GPU/ABI复核已完成，面板120Hz不当游戏FPS。AVD历史独立记载，本轮未新启动；荣耀平板未连接。

已否定路线：mmaptest未设MAP_FIXED不能推断必须地址0；tail曾掩盖139不能算成功；最长候选relocation不是完整性；简单叶函数或musl早return2不能算CRT；没有测试证据的94MB必OOM论据撤回。不重试旧被拦安全/异常QA，不再读取被拒Codex UI。当前无待审批；正在编译/分析的精确日志由各活跃writer产物manifest记录，不能按进程存在宣称推进。

详细历史/命令索引：[证据索引](../docs/analysis/codex_handoff_evidence_index.md)；当前分阶段事实：[stage status](../docs/analysis/codex_stage_status.md)。

**可复制接手句：** 先读AGENTS.md和coordination/HANDOFF.md，核对HEAD/WIP/代理写入归属并保留成果，从当前真实启动66/63与CRT515继续，优先复用已核验组件，禁止以诊断合同或空画面冒充游戏成功。

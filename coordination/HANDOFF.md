# Codex 原生移植接力：尚未完成游戏启动

## 当前事实与授权

目标《最终幻想：共鸣 / FINAL FANTASY RESONANCE》，RG477M Android ARM64原生运行；荣耀平板GT Pro为附加目标，按源画幅等比例缩放/必要留边，不拉伸或裁切UI。用户已直接授权本地开发、并行子代理、本地提交，已连接掌机并两次亲自操作；不推远端或发布，不购买、刷机、root、清数据或读取凭据。不向Claude/Antigravity发消息、不停止他人任务，不reset/clean/stash他人成果。

用户说掌机页面“一串代码”后，实际截屏核对为正常诊断JSON，标题明确Game startup and rendering: NOT_RUN。不是崩溃，也不是游戏画面，无需用户手工输入代码。

## 分支 / 工作树

- 原交付：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` / `work/rg477m-native`，已实际safeff **a3cb435ec2569fc84ba1068c73bfbfbb96dddcf0**，本轮只读确认clean；后续快照实时重查，不循环猜自身SHA。
- 总控：`../ffrs-integration` / `task/codex-integration`，known_at **4c07e21f555f236327de2be5c1fdaf894a267970**（root证据提交；28已集成fb6d08b）；四文档/真机报告在更新，原交付最后核验a3，后续实际HEAD读git，未提前交付fb。DOC四文件现已暂停交回root串行checkpoint；恢复writer前不再写，root仍唯一Git/设备。
- Native-profile：85八文件freeze已集成70c，真实RG五合同0/info0，两个真实自有guest调用、raw_fallback=false、ended_session_rejected=true，33准hostimports保护x18；85当时原nnMain126/malloc66/startup59/CRT515仍133；后续28 startup60见下，旧85结果不改写。
- Runtime：`../ffrs-runtime` / `task/T02-codex-runtime`，五文件clean冻结 **28dc1a792946ae990690f11d71e89a60bf5d8b8b**→rootfb6d08b。actualprimary注册/同IDLEscope typedcallbackcleanup已真实接入，RG七合同0/info0；真实startup越59到60。新28接线独立QA已实际ACTIVE，下一ownedargv六文件已冻结9dc8aa73，原生设备验证待总控。
- Android：`../ffrs-android`，`task/T03A-codex-android`，冻结15da4c2，v6音频及EH链接已集成fb7d870。
- Vulkan：`../ffrs-vulkan`，`task/T03B-vulkan-ops`，新production VkBuffer冻结 **fa76ca979d711c68beebe9e6ae699914f8a1cdf0**、最后核验clean；旧源f7ac445已集成4695bca，新组件已集成34dd0c8且同RG contract/validator均0；仅transfer范围通过。
- Pages：`../ffrs-pages` / `task/T02-guest-pages`，4695e3c四文件freeze→d8，RG normal0/39.74ms/76checks；独立FINAL20commands0、无组件缺陷，scope仅owner，不覆盖Process绑定。旧作者/页QA lease释放；[归档review](../docs/analysis/codex_guest_pages_review.md)。
- OS TLS：9c独立FINAL已过（REVIEW SHA8823424f…59346；11captured0、normal1351/recovery1419及并发重入0），仅组件、Android归因root；28五文件接线有root RG实测，其独立QA已ACTIVE尚未FINAL。
- Boundary：5c20 repair独立FINAL已接受（综合报告SHA78a0ecaa…6312d7），旧2f失败和修复RG0均保留；仅组件非游戏绑定，register-only/stackargs/foreignunwind限制保持。
- 独立普通功能审查：`../functional-review/`；GuestContext FINAL PASS已释放旧lease，新startup-profile已接收freeze15e498c开始独立普通源码/host contract审查，旧15e已交回host路径通过/宏关闭P2/RG失败，等新freeze复核。fa76ca9 buffer_resource.c/h独立review已FINAL接受组件：精确8文件，40普通mock+2ownership+自编两种各7组PASS，ASan/UBSan无误；reviewer非新组件作者、未ADB，范围不含游戏图形。静态TLS/图形底座评估：`../tls-research/`，底座评估已完成并释放lease；只写各自证据。旧QA异常NSO流程被平台安全检查中止，不重试，其WIP保留，不冒充完整QA。

实际工作模型GPT-6.1-sol/xhigh，无持久目标开关。5c/9c组件独立FINAL已过；85+4695+5c独立QA已FINAL（42命令41×0/1预期拒绝1，无新缺陷）；28新接线独立host审查已完成；额外AndroidC编译timeout明确保留。Stack13五文件clean7beeb2冻结，rootRG normal/cleanup已0，独立审查进行中，未完成全部验收；DOC四文件已暂停供root串行checkpoint，root唯一Git/设备。

各树.local独立，仅private uncompressed/exefs为原素材链接。保留原交付开发签名，不读取或复制私钥。原交接历史曾含敏感内容，当前文本已清理；未来上传需另做历史审查，当前禁止push。

## 最新实证（2026-10-23；版本与最后核验时间见证据索引）

本检查点与可复制命令/旧失败否定路径先看 [接力证据索引](../docs/analysis/codex_handoff_evidence_index.md)；文档按事件更新，没有后台自动刷新。详见 [真机报告](../docs/analysis/codex_device_report.md)、[新启动RG实证](../docs/analysis/codex_guest_process_device_report.md)、[阶段验收](../docs/analysis/codex_stage_status.md)；[旧15e独立review](../docs/analysis/codex_guest_process_review.md)与[VkBuffer FINAL review](../docs/analysis/codex_vulkan_buffer_review.md)版本分开。所有原始真机证据在总控 `.local/device/rg477m-session1/`。

- 真实RG477M Android14/API34，MT6897/ARM64/nativebridge0/4KiB。窗口1280×960、内容1280×918、280dpi，当前120Hz是面板而非游戏帧率。
- canonical v6 APK已更新并冷启动195ms，旧事件前缀和leaf摘要保留。无申请权限，451092B，SHA256 **4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a**。路径原交付`.local/artifacts/android/ffrs-diagnostic-debug.apk`；clean sourcefb7d870，35构建步骤0、输入稳定。重新构建必须重新核对build-evidence及摘要。
- 原始main+0x694bd0 leaf44B，在Mac/ARM64 AVD/真实RG各16875比较0差异；仅单函数、无构造器，不是正式T02/full game。APK JNI只load/relocate125577976B主模块，651imports/4612RELA/1370974RELR，NULLresolver，未调用game entry。
- **最新RG原始入口突破**：28dc1a七合同/info0；真实`nninitStartup`越AllocateTlsSlot59→GetHostArgc60/+0x13420/133，149.603916ms，无timeout/mainhash保持；typed OS callback A0/B9/A5/order0x121。直接nnMain仍126/+0x352f1b8、malloc66/+0x144b8、CRT515/+0x5920a34，均133。证据`os-tls-profile-v2-{plan,stage,execution}.json`；新28绑定独立QA已ACTIVE未FINAL，1MiB写者只证明控制流经过，未BSS读回。
- **保留旧失败**：15e startup contract649672B实际1/42.27ms，e95诊断-only663040B实际1/36.30ms，唯一pthread usable64752 vs65536差784B；原断言未删，这些不是d4的新结果。
- 历史同RG production HLE CLI info0；nnMain/malloc严格在QueryMemoryInfo +0x27a50/+0x1398c exit133；CRT __nnmusl_init_dso +0x5920a34/133；startup AllocateTlsSlot +0x133a0/133。无超时。不要用tail的0掩盖真实退出码，旧SIGSEGV139不是最新结论。
- C++guard/EH真实修复：a22d0dc含.eh_frame_hdr/PT_GNU_EH_FRAME，RG8线程80000调用/初始化1/abortwinner1/异常attempts2/errors0。未注册guest NSO unwind或完成CRT。
- 最新生产VkBuffer组件fa76ca9→34dd0c8确由原transfer driver使用，同RG ELF198584B/SHA2088ffd…3b9真实exit0/90.15ms/no timeout、validator0/PASS；64KiB copy/256B clear读回零差异、31VkResult0、12destroy/3unmap。source bundlea43b88d…26f；嵌入HEAD2ca5d35为提交前构建标记，须用bundle/freeze核对源码。不是新probe或NVN桥。
- 历史Vulkan真实GPU操作exit0：64KiB buffer copy及8×8 RGBA clear/readback零差异、31VkResult0。Mali-G615 MC6/Vulkan1.3.247。没有shader、surface/present、descriptor indexing实际操作或NVN后端验收。
- owned-pages e112d65真机normal contract0：4KiB、8线程×32周期、32release竞争，mapped0/validtrue/errors0。62GiB只虚拟预留，实际只映射4096B。独立主机rollback/quarantine审查通过；真机未故障注入。组件未绑定SDK，mapped不是physical resident budget。
- 实体方向/B/左杆已记录；补测右Z/RZ均[-1,1]、L2/R2数字键8/10事件，模拟触发轴仍0。v5公开InputDevice枚举识别retrogame_joypad，旧raw-node误报修复。不代表游戏映射。
- v6显式音频0.25秒/PCM16peak327：实测48000Hz/256frames，扬声器route，12000frames写入/播放推进、246ms、stop/release正常，系统音量未改。用户“刚才没注意”，听感未确认，不记游戏音频PASS。
- 最新sourcec5bbddb（运行时dirty仅四文档）主机23集成检查均符合真实预期（覆盖exit3、normalexit23、_Exit24，其余0），原始输入前后hash一致，见`.local/host-verification/checkpoint-c5bbddb/summary.json`；旧1997f27检查点另留，不能用旧结果覆盖后续源码。独立普通组件review见docs/analysis/codex_*_review.md，不能替代完整QA。

## 核心阻塞及下一步

1. **真实argv与后续startup**：28已在注册primary上接入APP59/Get/Set/Free及typed析构，原`nninitStartup`现停GetHostArgc60；真正ownedargv六文件已冻结9dc8aa73，设备复测待总控。BSS77c51b8由+133ec/+133f4写worker默认1MiB的来源已闭合，新运行只证明经过写者，未直接读回。`-MainThreadStackSize=`可覆盖（<=4095按MiB），与NPDM primary64KiB不同；直接nnMain仍CreateThread126/133，不能给stack0假默认或pthread旁路。musl515/CRT、ThreadType/ELF TLS/SDK池仍未通过。
2. **TLS/CRT**：另有调用边界ABI欠账：runtime只读d4 Android CLI+0x22134为BRx8未保存/恢复x18，imports直指host typed，原nnMain严格trap时x18=1；证据在`.local/call-boundary-x18/`。尚未证明导致当前133；register-only gateway组件2f旧normalRG0保留，但旧错误路径缺陷已5c20修复并组件实测0、独立repairFINAL已接受；85已实际接入entry及33准hostimports，85独立profileQA已FINAL，28接线QA进行中、21参musl的stackargs不支持，不能宣称游戏ABI缺口闭合。main模块TLS镜像initialized0x220/total0x1019/align8/prefix16，独立复制/清零/对齐正常，不代表header/指针生命周期兼容。SDK OS32槽与ELF destructor LIFO是不同机制；应用槽0–15、SDK31–17、16未知；OS最多5轮且callback后清值。ELF节点48B/align16，可重入且无固定轮数。main303e0释放依赖guest allocator虚表，不能hostfree。静态报告在docs/analysis/codex_tls_sdk_lifecycle_report.md。可空GLOBAL OBJECT hook安装99ba0→9ae40、fini分支99ea0清零已闭合；普通线程返回依次OS槽→结束状态/通知→hook→ELF drain（内部门控非零条件），异常及初始线程退出不外推，详见codex_tls_exit_hook_report.md。不要执行写TPIDR的原SDK helper、批量改MRS或强制CRTgate。
3. **真实游戏图形/资源**：NVN仅2/535查询入口，533未实现，59缺PFN类型；当前GPU探针不等于翻译后端。Strato GPU/Hades评估已结束：有真实代码但无直接NVN ABI桥，需图形负责人定生产接入边界；整游戏可行性仍未知。按启动真实调用链补接口，不能批量假桩或继续用新probe替代后端。合法本地素材保留，实际挂载/IoStore/着色器/呈现/标题/场景都未验收。
4. **设备余项**：听感、完整键轴/休眠、真实游戏输入音频存档；30FPS实际场景/45分钟未运行。荣耀平板未连接，官方profile3000×1920/8sGen3/144Hz仅规格，不是adb身份或GPU兼容证据。

## 能复用什么，不能推断什么

当前没有可靠总量，撤回旧10–15%估计，不再用百分比代表游戏完成度。已实现的是可测试的兼容层子集；还没有完整图形或可玩流程。

| 范围 | 已实现/证据 | 换游戏时的边界 |
|---|---|---|
| 通用NSO/内存基础 | loader/重定位与记录导入重绑定、Context真实原生栈、唯一页owner/预算/释放重试；独立合同和RG通过 | 格式/重定位种类和Android ARM64 ABI须匹配；不能推出其他游戏能启动 |
| 原生调用与OS TLS | x18/SCS register-only网关5c；32槽OS TLS9c/五轮typed析构，均独立review及RG组件0 | 精确声明签名、SDK池/生命周期；28已接入APP池/真实startup越59到60，新接线独立QA进行未FINAL；ELF TLS另算，21参栈运输正在独立编码 |
| 图形子集 | production VkBuffer创建/绑定/map/copy/clear/释放，Mali实际读回零差异 | 只有transfer资源操作；没有NVN着色器翻译、surface/present、标题 |
| 当前游戏专用 | main SHA6e6…68a5、BuildID7368…e71b、nnMain/59/CRT/1MiB写者地址和leaf+694bd0验证；原游戏文件未补丁 | 下个版本/游戏必须重核签名、偏移、SDK语义，不能搬地址；leaf次数不证明任意游戏可玩 |
| 仅研究/未实现 | 全CRT/ELF TLS/ThreadType/alias栈、NVN bridge/着色器/呈现/资源挂载、完整输入音频保存 | 静态报告可复用方法和否定路线；实现与适用性仍未证明 |

最小无游戏字节fixture已经实际运行：85构建的9216B `boundary.nso`，SHA256 `0214c62fdc6ffb00093ad7bce051955b1003ff1ad73abd10deb53f110d82278f`，自行声明普通NSO导入并故意改x18，真实RG两次native调用/跨host import后校验值，exit0、raw_fallback=false、已结束primary拒绝再次执行。证据总控 `.local/device/rg477m-session1/native-profile-v2-contract-2.stdout` 及execution.json；这证明该loader/import/ABI子集不依赖本游戏指令，85独立profile审查已FINAL（不覆盖28新接线），不保证其他游戏/SDK/图形接口兼容。复跑精确argv含源/依赖/产物hash见同prefix plan/execution；不必为说明复用再重复跑。

新并行 `T02-NATIVE-STACK13`：`../ffrs-native-stack13` / `task/T02-native-stack13`，五文件clean冻结 **7beeb22294311cc3c896ec23ac23561fca9d6b6d**，rootRG normal0/227.31ms、cleanup0/55.49ms，独立审查进行中（额外AndroidC编译timeout保留）；仅明确GP21/13×u64两向运输，不绑定musl假桩/不写TP，原5c冻树保持。

## 方法核验与成熟底座调整

本轮刚实际读REASkill42：[安装入口](/Users/yuyan9923/.codex/skills/reverse-engineer-anything/SKILL.md)。此前当前Codex读取/REA引擎调用未找到实证，旧Antigravity MCP207 loaded不能代替；原分析仍归LLVM/机器码/实机。当前REA MCP0；publicCLI首次npmDNS/cache缺失后，项目隔离缓存固定6.5.0实际doctor0/Ghidraavailable，无全局改动。遵Skill复用已有hash匹配结果，新未解binary问题走定点REA，普通源码开发不用REA。

成熟代码优先级已调整：LASRecomp Beta4 fe66f31a、自有MIT，确有NVN API renderer device/present、draw、GLSL→SPIRV实现；不是本游戏或Android完成案例。图形Agent先闭合其CpuContext/opaque布局/SDL3/VMA/shaderc与最短资源→命令生产接入；不继续泛搜或从零填535入口。当前loader/页/TLS保留。旧94MB必OOM论据撤回；N64Recomp输入MIPS、librecomp为N64 runtime不当Switch编译器。详见[方法及复用核验](../docs/analysis/codex_method_reuse_audit.md)及其中固定版本/许可/代码范围链接。

当前新增Stack13 source7be真实RG normal0/227.31ms、cleanup0/55.49ms，仅显式21参数native运输，独立QA进行/未集成，不是CRT完成；原startup已到60，runtime真正ownedargv六文件已冻结9dc8aa73待真机。

## 恢复与构建

先只读核对工作树/HEAD/WIP/agents/进程；root c5bbddb主机回归session22690已结束0，23checks符合真实预期0/3/23/24、main前后hash一致，日志`.local/host-verification/checkpoint-c5bbddb`；不用重复起同一回归；d4capacity独立FINAL组件已通过，后续Process/pages新WIP另需冻结复核。当前总控报告无ADB长命令、未决审批或不确定写入。旧文档ps127保留为历史失败；总控10:40:12UTC新恢复快照中ps/adb均0、限定AVD/qemu/gradle/cmake/nso名字空、仅授权RG。该时间点快照不等于永久当前，接力须重查。exec连接中断时不要重放不确定写入。原有AVD多次随执行环境中断消失，启动前核对adb与进程名，不wipe、不重复实例。RG已连接时无需为了探针再开AVD。

```sh
python3 scripts/project_status.py
python3 scripts/verify_host.py --runtime-contracts --game-leaf --output .local/host-verification/NEW-CHECKPOINT
FFRS_USE_MINIMAL_TOOLCHAIN=1 scripts/device/build_diagnostic_apk.sh
FFRS_USE_MINIMAL_TOOLCHAIN=1 scripts/device/build_runtime_hle.sh
```

使用新日志目录并保存原始exit。部分Google官方r27d sysroot每成员长度/CRC验证，AppleClang21/RustLLD交叉链接；完整NDK归档校验NOT_VERIFIED，不能称完整官方工具链安装。CMake静态库曾被Darwin ar/ranlib阻塞，minimal链实际成功；不要将交叉configure0写成build通过。

设备操作用显式序列号（保存在本地证据），先身份核验，安装前记录包名/权限/hash/签名；只访问本项目应用/新shell诊断目录。当前shell目录`/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259`保留，不清理。adb exec-out远端错误可能仍return0，须解析JSON、长度和摘要。不要读取私钥、别的应用或系统凭据。

下一优先级是GetHostArgc60真实ownedargv9dc冻结版本→同RG原startup复测，并行完成Stack13冻结版本真机与独立review；不重做已闭合stack来源，不默认补值/pthread旁路。完整CRT/SDK线程/图形/标题/可玩仍未过。

> 短接力提示：单入口a3已safeff原交付且核clean，保留复用表、自有boundaryfixture与撤回百分比口径。28五文件已集成rootfb、真实startup59→60/133，nnMain126/malloc66/CRT515仍133；原交付最后核验a3，后续实际HEAD读git。28接线独立审查已完成（额外交叉C编译timeout保留）、85QA FINAL，Stack13冻结7b已RG两0/独立QA进行；ownedargv9dc六文件冻结待设备。先核HEAD/WIP/lease，DOC四文件暂停交回/root唯一Git设备，不重复跑已确认合同。

主界面只读核验`cua.getApp('Codex')`被CUA安全规则DENIED，无截图/AX、不重试绕过，不阻开发且不是Git待审批。内部`/root/...`仅协作标识，未创建四个sidebar任务，UI可见性UNKNOWN。

短目标：原始nnMain越过Query/TLS/CRT→真实图形/标题→可操作场景→游戏输入音频存档性能。持续循环“定位真实阻塞→最小功能修复→编译→真机复测→本地检查点→下一关”；各scope并行，少重复调研、无假桩，仅报实质突破/用户操作/真实阻塞。工具无CreateGoal/UpdateGoal/persistent入口，仅当前回合按目标持续执行，不声称持久参数或后台已开启。

28接线独立报告见[consumer审查](../docs/analysis/codex_os_tls_profile_review.md)：host正常/恢复及OOM/register/providerfailstop补测符合，root七项RG0单独归因；额外AndroidC编译30s超时、旧stderr复制读取中断130及环境pgrep3均保留，不计实现通过。root只读进程核验12:35:47UTC无clang/cc1条目，不停止其他已有进程。

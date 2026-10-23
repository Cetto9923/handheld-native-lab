# Codex 原生移植接力：尚未完成游戏启动

## 当前事实与授权

目标《最终幻想：共鸣 / FINAL FANTASY RESONANCE》，RG477M Android ARM64原生运行；荣耀平板GT Pro为附加目标，按源画幅等比例缩放/必要留边，不拉伸或裁切UI。用户已直接授权本地开发、并行子代理、本地提交，已连接掌机并两次亲自操作；不推远端或发布，不购买、刷机、root、清数据或读取凭据。不向Claude/Antigravity发消息、不停止他人任务，不reset/clean/stash他人成果。

用户说掌机页面“一串代码”后，实际截屏核对为正常诊断JSON，标题明确Game startup and rendering: NOT_RUN。不是崩溃，也不是游戏画面，无需用户手工输入代码。

## 分支 / 工作树

- 原交付：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` / `work/rg477m-native`，已安全ff至 **f4cd524fa10e52dc7846a7be994a9446f8906bae**，最后只读核验clean（2026-10-23T11:14:47.133988+00:00）；后续提交前实时重查，不覆盖新成果。
- 总控：`../ffrs-integration` / `task/codex-integration`，known_at **c1ddecb5cd179b80bf8f14c84171fb336ee26a05**（2026-10-23T12:05:14.571827+00:00），85已cherry70c；doc已交回四文件写权，root在保存本检查点。原交付最后f4，未来safeff待root实际执行；唯一Git/设备操作者root。
- Native-profile：85八文件freeze已集成70c，真实RG五合同0/info0，两个真实自有guest调用、raw_fallback=false、ended_session_rejected=true，33准hostimports保护x18；原nnMain126/malloc66/startup59/CRT515仍133，全部无timeout/mainhash保持，未越原启动gate。
- Runtime当前phase：root/runtime已实际编码5files（Processc/h、contract/buildscript/report），基于85+OS9c+Boundary5c。注册actualprimary→正常return同scopeIDLE typedcallbackcleanup→unregister→ThreadReturned；核APP59/Get/Set/Free真实符号，providerfailstop不假成功，目标真实startup59后到60，未达到。
- Android：`../ffrs-android`，`task/T03A-codex-android`，冻结15da4c2，v6音频及EH链接已集成fb7d870。
- Vulkan：`../ffrs-vulkan`，`task/T03B-vulkan-ops`，新production VkBuffer冻结 **fa76ca979d711c68beebe9e6ae699914f8a1cdf0**、最后核验clean；旧源f7ac445已集成4695bca，新组件已集成34dd0c8且同RG contract/validator均0；仅transfer范围通过。
- Pages：`../ffrs-pages` / `task/T02-guest-pages`，4695e3c四文件freeze→d8，RG normal0/39.74ms/76checks；独立FINAL20commands0、无组件缺陷，scope仅owner，不覆盖Process绑定。旧作者/页QA lease释放；[归档review](../docs/analysis/codex_guest_pages_review.md)。
- OS TLS：9c独立FINAL已过（REVIEW SHA8823424f…59346；11captured0、normal1351/recovery1419及并发重入0），仅组件、Android归因root；新OS HLE5fileWIP不在PASS内。
- Boundary：5c20 repair独立FINAL已接受（综合报告SHA78a0ecaa…6312d7），旧2f失败和修复RG0均保留；仅组件非游戏绑定，register-only/stackargs/foreignunwind限制保持。
- 独立普通功能审查：`../functional-review/`；GuestContext FINAL PASS已释放旧lease，新startup-profile已接收freeze15e498c开始独立普通源码/host contract审查，旧15e已交回host路径通过/宏关闭P2/RG失败，等新freeze复核。fa76ca9 buffer_resource.c/h独立review已FINAL接受组件：精确8文件，40普通mock+2ownership+自编两种各7组PASS，ASan/UBSan无误；reviewer非新组件作者、未ADB，范围不含游戏图形。静态TLS/图形底座评估：`../tls-research/`，底座评估已完成并释放lease；只写各自证据。旧QA异常NSO流程被平台安全检查中止，不重试，其WIP保留，不冒充完整QA。

实际工作模型GPT-6.1-sol/xhigh，无持久目标开关。5c/9c组件独立FINAL已过；85实机组件合同0/集成70c，原启动gate未变。新85+4695+5c独立QA实际ACTIVE，runtime新5fileOS HLE实际编码。root七报告已c1提交，仅四文档待commit；doc立即暂停供单入口交付。

各树.local独立，仅private uncompressed/exefs为原素材链接。保留原交付开发签名，不读取或复制私钥。原交接历史曾含敏感内容，当前文本已清理；未来上传需另做历史审查，当前禁止push。

## 最新实证（2026-10-23；版本与最后核验时间见证据索引）

本检查点与可复制命令/旧失败否定路径先看 [接力证据索引](../docs/analysis/codex_handoff_evidence_index.md)；文档按事件更新，没有后台自动刷新。详见 [真机报告](../docs/analysis/codex_device_report.md)、[新启动RG实证](../docs/analysis/codex_guest_process_device_report.md)、[阶段验收](../docs/analysis/codex_stage_status.md)；[旧15e独立review](../docs/analysis/codex_guest_process_review.md)与[VkBuffer FINAL review](../docs/analysis/codex_vulkan_buffer_review.md)版本分开。所有原始真机证据在总控 `.local/device/rg477m-session1/`。

- 真实RG477M Android14/API34，MT6897/ARM64/nativebridge0/4KiB。窗口1280×960、内容1280×918、280dpi，当前120Hz是面板而非游戏帧率。
- canonical v6 APK已更新并冷启动195ms，旧事件前缀和leaf摘要保留。无申请权限，451092B，SHA256 **4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a**。路径原交付`.local/artifacts/android/ffrs-diagnostic-debug.apk`；clean sourcefb7d870，35构建步骤0、输入稳定。重新构建必须重新核对build-evidence及摘要。
- 原始main+0x694bd0 leaf44B，在Mac/ARM64 AVD/真实RG各16875比较0差异；仅单函数、无构造器，不是正式T02/full game。APK JNI只load/relocate125577976B主模块，651imports/4612RELA/1370974RELR，NULLresolver，未调用game entry。
- **最新RG原始入口突破**：f24+4695 contract/info0；nnMain Query→真实map65536→CreateThread126/+0x352f1b8/133，malloc仍VA66/+0x144b8/133，无timeout/mainhash保持。stack0来源已闭合（下列）；同f24真实`--init-wrapper --info`仍musl515/+0x5920a34/133、169.57ms、无timeout，CRT未完成。
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

1. **先OS TLS上下文与真实nninitStartup**：BSS77c51b8由main+133ec MOV1MiB/+133f4 STR写，nnMain+352f150先走nninitStartup却在+1339c AllocateTlsSlot59 trap，故worker stack全局仍0；不是凭空DT_INIT_ARRAY ctor。`-MainThreadStackSize=`可覆盖（<=4095按MiB），worker默认1MiB与NPDM primary64KiB不同。callback main18c60非NULL；SDK CreateThread不为size0选默认，MapAliasStack依赖alias manager/真实SVC，不可简单pthread旁路。新OS TLS9c组件已冻结实测0但未HLE，32槽/注册线程/Free/5轮dtor+typedinvoker供后续gateway，不绑16槽PoC/ELFTLS/SDKTP/ThreadType/直接游戏callback。musl CRT门槛仍严格133，SDK初始化/游戏未过。
2. **TLS/CRT**：另有调用边界ABI欠账：runtime只读d4 Android CLI+0x22134为BRx8未保存/恢复x18，imports直指host typed，原nnMain严格trap时x18=1；证据在`.local/call-boundary-x18/`。尚未证明导致当前133；register-only gateway组件2f旧normalRG0保留，但旧错误路径缺陷已5c20修复并组件实测0、独立repairFINAL已接受；85已实际接入entry及33准hostimports，独立profileQA进行中、21参musl的stackargs不支持，不能宣称游戏ABI缺口闭合。main模块TLS镜像initialized0x220/total0x1019/align8/prefix16，独立复制/清零/对齐正常，不代表header/指针生命周期兼容。SDK OS32槽与ELF destructor LIFO是不同机制；应用槽0–15、SDK31–17、16未知；OS最多5轮且callback后清值。ELF节点48B/align16，可重入且无固定轮数。main303e0释放依赖guest allocator虚表，不能hostfree。静态报告在docs/analysis/codex_tls_sdk_lifecycle_report.md。可空GLOBAL OBJECT hook安装99ba0→9ae40、fini分支99ea0清零已闭合；普通线程返回依次OS槽→结束状态/通知→hook→ELF drain（内部门控非零条件），异常及初始线程退出不外推，详见codex_tls_exit_hook_report.md。不要执行写TPIDR的原SDK helper、批量改MRS或强制CRTgate。
3. **真实游戏图形/资源**：NVN仅2/535查询入口，533未实现，59缺PFN类型；当前GPU探针不等于翻译后端。Strato GPU/Hades评估已结束：有真实代码但无直接NVN ABI桥，需图形负责人定生产接入边界；整游戏可行性仍未知。按启动真实调用链补接口，不能批量假桩或继续用新probe替代后端。合法本地素材保留，实际挂载/IoStore/着色器/呈现/标题/场景都未验收。
4. **设备余项**：听感、完整键轴/休眠、真实游戏输入音频存档；30FPS实际场景/45分钟未运行。荣耀平板未连接，官方profile3000×1920/8sGen3/144Hz仅规格，不是adb身份或GPU兼容证据。

## 能复用什么，不能推断什么

当前没有可靠总量，撤回旧10–15%估计，不再用百分比代表游戏完成度。已实现的是可测试的兼容层子集；还没有完整图形或可玩流程。

| 范围 | 已实现/证据 | 换游戏时的边界 |
|---|---|---|
| 通用NSO/内存基础 | loader/重定位与记录导入重绑定、Context真实原生栈、唯一页owner/预算/释放重试；独立合同和RG通过 | 格式/重定位种类和Android ARM64 ABI须匹配；不能推出其他游戏能启动 |
| 原生调用与OS TLS | x18/SCS register-only网关5c；32槽OS TLS9c/五轮typed析构，均独立review及RG组件0 | 精确声明签名、SDK池/生命周期；9c尚未接入真实startup，ELF TLS另算；21参栈运输正在独立编码 |
| 图形子集 | production VkBuffer创建/绑定/map/copy/clear/释放，Mali实际读回零差异 | 只有transfer资源操作；没有NVN着色器翻译、surface/present、标题 |
| 当前游戏专用 | main SHA6e6…68a5、BuildID7368…e71b、nnMain/59/CRT/1MiB写者地址和leaf+694bd0验证；原游戏文件未补丁 | 下个版本/游戏必须重核签名、偏移、SDK语义，不能搬地址；leaf次数不证明任意游戏可玩 |
| 仅研究/未实现 | 全CRT/ELF TLS/ThreadType/alias栈、NVN bridge/着色器/呈现/资源挂载、完整输入音频保存 | 静态报告可复用方法和否定路线；实现与适用性仍未证明 |

最小无游戏字节fixture已经实际运行：85构建的9216B `boundary.nso`，SHA256 `0214c62fdc6ffb00093ad7bce051955b1003ff1ad73abd10deb53f110d82278f`，自行声明普通NSO导入并故意改x18，真实RG两次native调用/跨host import后校验值，exit0、raw_fallback=false、已结束primary拒绝再次执行。证据总控 `.local/device/rg477m-session1/native-profile-v2-contract-2.stdout` 及execution.json；这证明该loader/import/ABI子集不依赖本游戏指令，独立profile审查仍进行中，不保证其他游戏/SDK/图形接口兼容。复跑精确argv含源/依赖/产物hash见同prefix plan/execution；不必为说明复用再重复跑。

新并行 `T02-NATIVE-STACK13`：`../ffrs-native-stack13` / `task/T02-native-stack13` 从5c独立创建，仅五boundary文件。实现明确GP21/13×u64栈参数运输，两方向真实C/SCS合同；不绑定musl假桩、不写TP、不改原5c冻树，尚无代码/设备验收。

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

下一优先级是typed32槽OS TLS真实manager与native gateway普通组件→注册线程上下文→真实nninitStartup/CRT初始化→合法SDK线程前置→同RG复测；不再反复研究已闭合stack来源，也不默认补值/pthread旁路。新profile独立QA并行，正式图形/标题/可玩仍未过。

> 短接力提示：85已实机五合同0/集成70c，仅entry/33imports边界；原126/66/59/515仍133。5c/9c独立FINAL，新85+4695+5cQA ACTIVE且不读WIP；runtime5fileprimary/59OS HLE实际编码，目标59后60未达。七报告c1及OS独立报告e742已存；DOC已交回四文件，root串行保存并safe-ff，root唯一Git/设备。

主界面只读核验`cua.getApp('Codex')`被CUA安全规则DENIED，无截图/AX、不重试绕过，不阻开发且不是Git待审批。内部`/root/...`仅协作标识，未创建四个sidebar任务，UI可见性UNKNOWN。

短目标：原始nnMain越过Query/TLS/CRT→真实图形/标题→可操作场景→游戏输入音频存档性能。持续循环“定位真实阻塞→最小功能修复→编译→真机复测→本地检查点→下一关”；各scope并行，少重复调研、无假桩，仅报实质突破/用户操作/真实阻塞。工具无CreateGoal/UpdateGoal/persistent入口，仅当前回合按目标持续执行，不声称持久参数或后台已开启。

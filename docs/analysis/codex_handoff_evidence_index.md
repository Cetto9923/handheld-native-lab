# 实时接力证据索引

本索引只整理现有证据，未重跑游戏/设备/构建。入口是 [HANDOFF](../../coordination/HANDOFF.md) 与 [state](../../coordination/state.json)，STATUS只能由 `python3 scripts/project_status.py --write` 生成。第一检查点已提交/快进至 `11e861127c84a2c7904804ec2bc4748b1df71fdb`，后续最后核验事实分时记录；旧采样基线为root `d9a1667`；后续先重新核对Git，勿把本文当自动刷新状态。

## 当前所有权与恢复点

| 对象 | 已核对状态 / 唯一写者 |
| --- | --- |
| 原交付 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` | `work/rg477m-native` / `2e444a16a1d80fe3ff1407e69eb5ba554490b528`，本次采样clean；只由总控安全快进 |
| integration `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration` | `task/codex-integration` / `34dd0c8642e1006fb72401785e89c2cd3be1393e`；目标文档2e444a1之后集成startup profile1997f27和GPU组件34dd0c8；四文档WIP保留；总控持有报告、Git与设备操作 |
| 本文档代理 | 用户明确委派DOC-HANDOFF-LIVE，当前仅四文件：HANDOFF/state/生成STATUS/本索引；不stage/commit，交总控串行提交 |
| `../ffrs-runtime` | `task/T02-codex-runtime` / `15e498c0985aee20c973632b6406e2b976628d81`；当前HEAD `e95e521c79bead1c6de4f64d5b01416f1f639ec1`，最后核验8文件dirty（2build脚本、Context c/h、process c/h、2contracts），容量/guard修复未冻结；15e498c仅此前production freeze，不是当前clean HEAD |
| `../ffrs-android` | `task/T03A-codex-android` / `15da4c2ef7a11138de45d098574f9bd66ef60b66`，采样时clean；已集成v6，当前不再开新外围probe |
| `../functional-review` | GuestContext完成并释放lease；startup-profile/旧freeze15e498c独立host正常路径通过、宏关闭P2/RG失败；等新修复freeze复核 |
| `../tls-research` | 图形底座有界评估完成并退出，lease释放；原证据保留，不解释为后端实现或新probe |

当前无待处理审批或不确定写入（总控最新消息）。本代理没有后台进程；总控声明当前真实RG命令均已结束。历史AVD PID49247/63060/71987不能当存活证据：本代理执行 `ps -p 49247,63060,71987 -o pid=,comm=` 被执行沙箱拒绝，exit127。恢复后由有权限的总控重新只读核验，不能盲重启、wipe或杀他人进程。

## 证据与真实退出码

| 范围 | 证据入口 | 已核对的命令 / 结果与限度 |
| --- | --- | --- |
| 主机普通集成检查 | [.local summary](../../.local/host-verification/checkpoint-fb7d870/summary.json) | `python3 scripts/verify_host.py --runtime-contracts --game-leaf --output .local/host-verification/checkpoint-fb7d870`；23项按记录符合预期：NVN coverage真实3、normal exit23、immediate exit24，其余有返回码项0；不是全23项都退出0。源fb7d870且有明确文档WIP |
| 真实RG入口与诊断 | [device report](codex_device_report.md)、[阶段状态](codex_stage_status.md)、[原始runtime exits](../../.local/device/rg477m-session1/runtime-execution.json) | `nso_runtime_hle ... --info`0；原nnMain/malloc在QueryMemoryInfo caller+0x27a50/+0x1398c均133；CRT __nnmusl_init_dso133、startup AllocateTlsSlot133，无timeout。完整入口未通过 |
| v6 canonical APK/音频/输入 | [Android报告](codex_android_report.md)、[device report](codex_device_report.md)、[音频执行](../../.local/device/rg477m-session1/audio-v6-execution.json) | 原交付`.local/artifacts/android/build-evidence.json`：source fb7d870/35 build steps0、451092B/SHA4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a；安装-r0/冷启动195ms。12000frames推进及route/cleanup不证明听到，用户未注意；游戏音频NOT_RUN |
| owned-pages与GuestContext | [context report](codex_guest_context_report.md)、[真实RG manifest](../../.local/device/rg477m-session1/guest-context-execution.json) | Context freeze e846dc1，RG命令真实returncode0/no timeout/35.5ms，artifact SHA e3d3500d9919d5aad1c67385802635fdaa29df4c340bc8529507ce2fb30d6ec1；64KiB owned SP/guard/TLS/join清账，valid mask0；pages组件normal0，均尚未绑定原游戏 |
| Context独立FINAL review | [归档副本](codex_guest_context_review.md)、[原始manifest](../../../functional-review/guest-context/manifest.json) | `clang -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic -pthread ...` production/recovery/校正补测编译0且运行0；`xcrun llvm-nm --defined-only ...`0、冻结四文件diff0。首个补测driver1且child码未落盘，不编造；确认失败1源自未承诺的TLS teardown假设，生产源码未改。仅普通组件PASS，Android由总控另执行 |
| SDK语义/TLS/NPDM | [runtime report](codex_runtime_report.md)、[TLS lifecycle](codex_tls_sdk_lifecycle_report.md)、[exit hook](codex_tls_exit_hook_report.md)、[NPDM memory](codex_npdm_memory_report.md) | Query为全局SDK七字段，非地址查询；64KiB栈有NPDM来源、20MiB是kernel管理保留。独立module TLS/host TLS成功不等于SDK TP/CRT兼容；禁止写TPIDR helper或批量补MRS假通过 |
| 新startup profile | [只读设计](../../../ffrs-runtime/.local/loader-owner-plan/PLAN.md) / `../functional-review/startup-profile/` | 已冻结15e498c并集成1997f27，原始Mac入口真实越过Query；v2证据见下。Android ELF构建通过但最新RG contract1/line202；原始入口、独立FINAL review和CRT/TLS未过 |
| 图形底座评估 | [归档报告](codex_graphics_foundation_report.md) | Strato GPU `ae1566a48285816a87e81d4aeb40bd2f4e56e60b` 与Hades `8717c8f6d7deab58032e042b027ed84838833824` 有真实GPU/shader实现，无直接NVN ABI桥；逐文件许可证和生产接入边界仍需图形负责人核清。只读评估完成，非运行/可玩证明 |

## 必须保留的否定结论

- `mmap(0, ..., MAP_PRIVATE|MAP_ANONYMOUS, ...)` 的0是地址hint；未用MAP_FIXED就不能据此声称必须映射零址。不得沿此误诊继续改平台策略。
- 保存被测命令自己的真实return code；`... | tail` 的0不能覆盖旧SIGSEGV139。最新严格入口的可定位133是另一版本/路径，二者都不是游戏成功。
- “最长RELA候选”只证明扫描选到候选，不能证明表完整；以validated动态元数据、RELA/RELR边界及当前输入版本核对，不能把旧3985项与当前4612项混算。
- CMake **configure0不等于build0**：Android worker额外partial CMake build2，Darwin ar/ranlib不能索引ELF。minimal实际35步0；部分Google官方r27d成员经长度/CRC验证，完整NDK checksum仍NOT_VERIFIED，不称完整安装。
- 被平台中止的旧异常NSO安全QA不重试；普通功能review并未替代它或完整游戏QA。APK安装、leaf16875/0、GPU拷贝/clear、用例/行数都不等于正式T02、标题或可玩通过。

## 下一接力动作

1. 核对各树HEAD/dirty/active_writers，保持现有WIP和不可变输入；只在移交scope内写。总控继续唯一设备、集成、提交；四文档归DOC-HANDOFF-LIVE，角色转交须明确。
2. 旧startup profile15e498c的独立review与RG失败已交回；优先修真实RG容量差/guard编译前置条件，保留断言/失败artifact，待新freeze独立复核，再由总控复测contract和原始caller/exit，优先CRT/TLS/NVN生产路径，不新增外围probe。
3. 图形负责人据已完成评估选择真实接入边界；535名字表不能生成backend。资源挂载、shader、present、正式标题、场景、30FPS/45分钟、游戏存档/音频和荣耀平板实测均未过。
4. exec断连时先核对上次动作是否执行、Git/index/文件与工具进程，再补缺；有真实审批拒绝保留动作/目标/原因，不换工具或路径绕过。当前直接本地开发/提交和完全访问授权已覆盖旧只读限制；仍无push/root/刷机/清数据或新游戏密钥下载授权。

最近16提交按主要目的粗分5直接runtime/启动链（53bea0d/731b9ae/8b817b6/7d6c09f/d9a1667）、5诊断包装、6分析记录。该分类不是工时、代码产出或完成率计算，整游戏可行性与可玩目标仍未知。

文档检查：`python3 scripts/project_status.py --write` exit0；`git diff --check` exit0。仅上述四文件由文档代理修改，未暂存/提交。

10:37 UTC总控追加核验：runtime e846dc1的loader.c/h编码WIP已在，尚无其余新profile文件；QA设计预审快照a5de2bd3要求invalid Query fatal且不写output、validity/ownership同一串行快照。实现未冻结，不能把预审记运行PASS。文档代理完成此四文件检查点后暂停写入，待总控一次本地提交/原交付快进并明确通知后恢复；开发继续。

10:38 UTC实际仅runtime/doc两路running，图形评估与QA预审已交回；新profile review为pending/等待freeze，已释放预审lease，不能写四路持续编码。本文档已落盘，交检查点后暂停写入供总控一笔提交+ff。

UI只读动作`cua.getApp('Codex')`被CUA明确DENIED：`Computer Use is not allowed to use the app com.openai.codex for safety reasons`。没有截图或AX，不重试/绕过；UI可见性UNKNOWN。内部`/root/...`是协作标识，未创建四个sidebar任务。此读动作拒绝不阻开发，也不是Git审批pending。

## 11e8611后新增关键事件（最后核验，不是永久当前状态）

- 总控已提交/原交付安全快进11e8611，文档恢复四文件独占；新增T04-VULKAN-BUFFER-RESOURCE从既有driver提取真正VkBuffer分配/map/coherency/cleanup组件，复用原contract，非新probe/UI或猜NVN ABI；scope以state为准。QA预审完成、执行待freeze。
- [10:40:12UTC恢复快照](../../.local/handoff-runtime-check/snapshot.json)：真实`ps -axo pid=,comm=`0、限定AVD/qemu/gradle/cmake/nso名字为空；`adb devices -l`0，仅授权RG。旧ps127仍是历史失败，新快照不解决CUAUI DENIED，也不代表未来进程状态或推进量。
- [首轮host build](../../../ffrs-runtime/.local/owned-profile-first-build/run.json)0、[普通fixture run](../../../ffrs-runtime/.local/owned-profile-first-run/run.json)0：7字段/padding/owned stack/ThreadRecord/join与5阶段清理失败/retained retry、errors0。源码是e846dc1之上的未提交WIP，不是冻结版独立QA PASS。
- [10:42:46UTC原nnMain run](../../../ffrs-runtime/.local/owned-profile-first-nnmain/run.json)真实133、0.6416s/no timeout；命令为`.local/nso_profile.first.host .local/uncompressed/main.bin --guest-quota 1073741824 --guest-stack 65536 --symbol nnMain`。原始+0x352f160在64KiB owned primary实际越过Query；[stderr](../../../ffrs-runtime/.local/owned-profile-first-nnmain/stderr.log)显示valid0x7f、total1073741824/used162938880/heap0,0(READY_EMPTY)/baseline162873344/stack65536/count1，随后AllocateMemoryPages symbol71 caller+0x27f90，ownedVA+0x10000/size0x10000。作者报告main未patch且hash保持；文档未读游戏字节。
- **1GiB是显式诊断port-policy配额，不是原SVC实测RAM。** 当前仅Mac16KiB host突破，Android/RG复核、独立冻结review、TLS/CRT/游戏仍未过。下一关先冻结并交Android同RG复核，再做真实pages quota所有权（当前仅只读ABI），不假桩跳过AllocateMemoryPages。
- 持续目标：原始nnMain→Query/TLS/CRT→真实图形/标题→可操作场景→游戏输入音频存档性能；按“真实阻塞→最小修复→编译→真机→本地检查点→下一关”推进。仅报实质突破/用户操作/真实阻塞；无CreateGoal/UpdateGoal/persistent工具，不伪称后台或持久参数已开启。

## 冻结startup profile事件（最后核验 2026-10-23T10:52:24.868673+00:00）

- Runtime clean freeze `15e498c0985aee20c973632b6406e2b976628d81` 已由总控集成为 `1997f27aaaf68621e2770fe14f3a592e1146740f`；原交付最后核验 `2e444a16a1d80fe3ff1407e69eb5ba554490b528` clean。作者此刻只读下一pages ABI；不再把loader写为未冻结WIP。
- [v2 manifest](../../../ffrs-runtime/.local/owned-startup-v2/manifest.json)共33个真实命令记录：invalid Query `-6`、module quota `1`、[原nnMain](../../../ffrs-runtime/.local/owned-startup-v2/host-original-nnmain/run.json)与[原malloc](../../../ffrs-runtime/.local/owned-startup-v2/host-original-malloc/run.json)各`133`为预期，其余29项`0`、无timeout；没有“33步全0”的结论。命令/完整stdout/stderr在各case目录。
- Mac原nnMain仍在`AllocateMemoryPages`71/caller+0x27f90，malloc在VA usage66/+0x144b8。此前真实越过七字段Query；1GiB仍是明确port-policy而非原SVC实测RAM，原游戏未patch。
- [冻结指纹](../../../ffrs-runtime/.local/owned-startup-v2/frozen-checkpoint.json)确认source_hashes_match=true；Context/address-region四源未改。Android contract649672B/SHA646adf09e31758e9e813e70aa9a486a9e3e9d4ee880065223ed6e5fa0455ab95；CLI648848B/SHA6ac2cf2599d436dbdf34b96d69782c7d78d7cbff13f942da735b6f4e15f33c85。部分官方工具链完整归档校验仍NOT_VERIFIED。
- 独立QA已从预审转冻结实现审查；[review manifest](../../../functional-review/startup-profile/manifest.json)10:48:57UTC为awaiting_freeze=false、implementation_executed=false。同RG复核由总控执行，本文档未操作设备；该冻结Android执行、FINAL review、TLS/CRT与游戏成功仍待证。

## RG失败与GPU源码交回（2026-10-23T10:54:01.337598+00:00）

- [真实RG执行](../../.local/device/rg477m-session1/owned-profile-execution.json)命令`toybox timeout -s KILL 15 <已验证contract> <已验证fixture>`实际returncode1（expected0）、42.27ms、host/remote均无timeout。[stderr](../../.local/device/rg477m-session1/owned-profile-contract.stderr)保留`process contract line=202: value==&returned_token && !errors`；contract649672B/SHA646adf…ab95，fixturea846…7493。Query输出valid0x7f、total1048576/used102400/baseline36864/stacks65536为1MiB fixture政策，不是游戏1GiB run或原SVC RAM，更不是contract通过。
- 该冻结版info/原nnMain/malloc未执行。作者仅在原8文件scope补per-check诊断，原断言与失败artifact保持；QA独立查平台假设，暂停pages新实现，先过RG gate。前节Android NOT_RUN是执行前历史状态，已由此失败结果取代。
- GPU生产组件冻结`fa76ca979d711c68beebe9e6ae699914f8a1cdf0`、ffrs-vulkan最后核验clean，8文件含2production VkBuffer源码，既有驱动实际使用。作者报告41mock/2ownership/17validator PASS与Android编译PASS；本代理未复跑，尚待总控集成/同RG，不能称NVN backend或游戏图形通过。

## 生产VkBuffer同RG验收与主机检查点（2026-10-23T10:55:33.623785+00:00）

- 生产组件`fa76ca9`已集成root`34dd0c8642e1006fb72401785e89c2cd3be1393e`；[plan](../../.local/device/rg477m-session1/vulkan-buffer-plan.json)、[execution](../../.local/device/rg477m-session1/vulkan-buffer-execution.json)、[result](../../.local/device/rg477m-session1/vulkan-buffer-result.json)、[validator](../../.local/device/rg477m-session1/vulkan-buffer-validator.json)已核读。ELF198584B/SHA2088ffd84187f8f868f6d892584a4d4164e09c6091bddc0f0531c948347ed3b9，同RG0/90.15ms、总控报告无timeout；31VkResult0、buffer65536B/image256B零差异，12destroy/3unmap，validator0/PASS。
- 精确source bundle为`a43b88df0741e61581f069d95e33342d6f1759deb58b60605665538cd540d26f`；result内git_head_at_build仍为提交前2ca5d35，不能仅凭该HEAD关联源码。组件真正被既有driver使用，任务仅组件passed并释放旧lease；game_rendering=false、无NVN bridge/title。
- [clean1997f27主机summary](../../.local/host-verification/checkpoint-1997f27/summary.json)23检查符合真实预期0/3/23/24，原始输入前后hash一致。它早于GPU34dd0c8；GPU用本次自身同RG/validator复核，不能声称旧host检查覆盖后续源码。原交付最后核验仍2e444a1，等核心RG修复/QA再由总控ff。
- RG startup contract line202实际失败仍是第一阻塞；GPU组件成功不改变该结果，完整TLS/CRT/图形/游戏仍未通过。

## RG分项定位与独立review（2026-10-23T10:58:43.711770+00:00）

- 诊断-only commit`e95e521c79bead1c6de4f64d5b01416f1f639ec1`未改production、保留原assert。[plan](../../.local/device/rg477m-session1/owned-profile-diagnostic-plan.json)给出663040B/SHA`d02c86845607be6f1da3a546a04b515c986d31c1402601f73e4af7ec715e36ef`；[execution](../../.local/device/rg477m-session1/owned-profile-diagnostic-execution.json)实际1/36.30ms，[stderr](../../.local/device/rg477m-session1/owned-profile-diagnostic.stderr)唯一`pthread-reported-stack-bytes actual0xfcf0=64752 expected0x10000=65536`、差784。reported bounds/实际SP/local正常、altstackflags0，Query七字段/padding/VA/returned_token检查正常；未据此猜Bionic内部布局。
- 作者原8文件scope新增Context四文件（guest_context.c/h、guest_context_contract.c、build_guest_context_contract.sh），修真实usable容量，区分rawmapping/nativeusable/logical64KiB/admin，保留capacityassert；新pages暂缓。新源码尚无PASS，info/原nnMain/malloc尚未在该冻结RG路径执行。
- [独立REVIEW](../../../functional-review/startup-profile/REVIEW.md)对旧15e运行29命令（26个0、invalidQuery -6、modulequota1、guardmacro-off probe1）：normal/recovery/supplement host路径通过，但发现P2 guardfeature关闭可落extension；宏开启对照0。当前指定与Android不受影响，不解释容量差。作者需typed guard编译前置条件，旧15eHostPASS/RGFAIL不能算新代码PASS；待新freeze复核。

- 2026-10-23T10:59:17.066895+00:00 作者/QA对齐修复**方案**：public pthread测native usable；不足64KiB不调用guest，join→成功raw munmap→refund后按实测deficit向上补有界admin页重建（最多3次+admin上限、无硬编码784）。rawmapping/outerguard/nativeusable/精确64KiBlogical/admin分列，SP/local在logical内，capacity验证后Queryready；quota仍65536/admin另列。尚未实现或验收。另reserved `T07-VULKAN-BUFFER-REVIEW`，独立旧Vulkan作者只读freezefa76ca9新buffer_resource.c/h，输出`../functional-review/vulkan-buffer/`，无ADB/源码写；待总控实际派发，不阻启动修复。

文档检查点 2026-10-23T11:01:32.772142+00:00：仅HANDOFF/state/生成STATUS/本索引四文件WIP，未stage/commit；文档暂停写入供总控一笔本地提交，收到恢复通知后继续按事件增量维护。`project_status.py --write`与`git diff --check`均实际0；无待审批/不确定写入、无本代理后台进程。下一短提词：保留现有WIP并核对HEAD/lease；先修RG真实usable容量/guard前置条件，冻结→独立QA→同RG复测，再原入口与pages，禁止把旧结果当新PASS。

提交前补充 2026-10-23T11:03:54.737228+00:00：T07-VULKAN-BUFFER-REVIEW已真实运行，[snapshot](../../../functional-review/vulkan-buffer/snapshot-manifest.json)冻结fa76ca9八文件，[host summary](../../../functional-review/vulkan-buffer/host-suite/summary.json)独立40普通mock、2ownership及coherent/noncoherent各7失败/释放组已执行，包含预期非零失败码；目前无确认缺陷、报告保存中，**不标最终QA PASS**，reviewer设备执行NOT_RUN。runtime当前HEAD`e95e521c79bead1c6de4f64d5b01416f1f639ec1`与8文件dirty已只读复核；15e498c只是旧production freeze，容量/guard新代码未冻结/未验收。四文档再次暂停，交总控串行提交；目标工具仍无CreateGoal/UpdateGoal，未开启真实持久开关。

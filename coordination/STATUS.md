# 当前项目状态（生成文件）

状态事件时间：2026-10-23T11:03:54.737228+00:00
阶段：NATIVE_RUNTIME_STARTUP_PROFILE_IN_PROGRESS
总控：Codex integration/device owner; DOC-HANDOFF-LIVE delegated sole coordination writer
更新方式：按事件更新，没有后台自动刷新。

| 任务 | 角色 | 状态 | 实际负责人 | 启动依赖 | 验收门禁 | 分支 |
|---|---|---|---|---|---|---|
| T00 | RE | passed | Antigravity (RE) | 无 | 无额外门禁 | work/rg477m-native |
| T00R | QA | passed | Agent C (QA, 独立复核) | T00 | 无额外门禁 | task/T00-qa-review |
| T01 | RE | passed | Agent A (RE) | T00 | 无额外门禁 | task/T01-route |
| T02-PREP | CORE | in_progress | Codex coordinator (integrated strict loader and true exit-code infrastructure) | T00 | 无额外门禁 | task/codex-integration |
| T02 | CORE | in_progress | Codex runtime worker | T01 | 无额外门禁 | task/T02-codex-runtime |
| T03A | ANDROID | passed | Codex Android worker | T00 | 无额外门禁 | task/T03A-codex-android |
| T03B | ANDROID | in_progress | Coordinator: actual RG477M connected and authorized, identity confirmed; capability acceptance pending | T03A | 无额外门禁 | task/codex-integration |
| T04-assets | RESOURCE | passed | Agent D (RESOURCE) | T00 | 无额外门禁 | task/T04-assets |
| T04-nvn | GRAPHICS | passed | Agent E (GRAPHICS) | T00 | 无额外门禁 | task/T04-nvn |
| T04 | CORE_ANDROID | pending | 未领取 | T02, T03A | T03B | 未创建 |
| T05 | CORE_GPU | pending | 未领取 | T04 | 无额外门禁 | 未创建 |
| T06 | GPU_PERF | pending | 未领取 | T05 | 无额外门禁 | 未创建 |
| T07 | QA | pending | 未领取 | T06 | 无额外门禁 | 未创建 |
| T07-PREP | QA | blocked | Codex independent QA (platform safety check interrupted; no further retries) | T00R | 无额外门禁 | task/T07-codex-qa |
| T04-NVN-RUNTIME | GRAPHICS | in_progress | Codex coordinator | T01 | 无额外门禁 | task/codex-integration |
| T07-FUNCTIONAL-REVIEW | QA | passed | Independent ordinary functional reviewer | T00R | 无额外门禁 | 未创建 |
| T02-TLS-ANALYSIS | RE | passed | Static scope completed only; not TLS compatibility | T00 | 无额外门禁 | 未创建 |
| T07-MODULE-TLS-REVIEW | QA | passed | Independent ordinary C module TLS reviewer | T00R | 无额外门禁 | 未创建 |
| T02-CXX-GUARD | CORE | passed | Runtime implementation + coordinator real AVD guard/EH contract; scope only compiler runtime | T01 | 无额外门禁 | task/T02-codex-runtime |
| T03B-VM-DISPLAY-FIX | ANDROID | passed | Android implementation; independent API28/29 semantics followup fixed fbea2e7, host+API34 actual evidence only | T03A | 无额外门禁 | task/T03A-codex-android |
| T03B-INPUT-SNAPSHOT | ANDROID | passed | Android public API snapshot; root actual RG physical events, component only | T03A | 无额外门禁 | task/T03A-codex-android |
| T02-SDK-TLS-LIFECYCLE-ANALYSIS | Static analysis | passed | Independent static analyst | T00R | 无额外门禁 | 未创建 |
| T07-OWNED-PAGE-REVIEW | QA | passed | Independent ordinary POSIX component reviewer | T00R | 无额外门禁 | 未创建 |
| T07-AUDIO-REVIEW | QA | passed | Independent ordinary Java/Android component reviewer | T00R | 无额外门禁 | 未创建 |
| T02-GUEST-PRIMARY-STACK | CORE | passed | Runtime engineer; coordinator RG contract0; independent host review PASS | T00R | 无额外门禁 | task/T02-codex-runtime |
| T02-TLS-EXIT-HOOK-ANALYSIS | RE | passed | Static analyst bounded successful normal return path only | T00R | 无额外门禁 | 未创建 |
| T02-NPDM-MEMORY-CONFIG | RE | passed | Coordinator read-only local metadata inspection | T00 | 无额外门禁 | task/codex-integration |
| T07-GUEST-CONTEXT-REVIEW | QA | passed | Independent ordinary POSIX/native stack reviewer | T00R | 无额外门禁 | 未创建 |
| T04-GRAPHICS-FOUNDATION-ASSESSMENT | GRAPHICS_ANALYSIS | passed | Graphics feasibility analyst (completed; no new probe) | T00R | 无额外门禁 | 未创建 |
| T02-OWNED-STARTUP-PROFILE | CORE | in_progress | Runtime engineer | T02-GUEST-PRIMARY-STACK | 无额外门禁 | task/T02-codex-runtime |
| T07-STARTUP-PROFILE-REVIEW | QA | blocked | Independent ordinary native startup profile reviewer | T00R | 无额外门禁 | 未创建 |
| DOC-HANDOFF-LIVE | DOC | in_progress | Independent documentation agent (delegated sole coordination writer) | T00R | 无额外门禁 | task/codex-integration |
| T04-VULKAN-BUFFER-RESOURCE | GRAPHICS | passed | Graphics implementation engineer | T03A | 无额外门禁 | task/T03B-vulkan-ops |
| T07-VULKAN-BUFFER-REVIEW | QA_GRAPHICS | in_progress | Independent prior Vulkan agent (not buffer component author) | T04-VULKAN-BUFFER-RESOURCE | 无额外门禁 | 未创建 |

## 独占写入

- {"task": "T03B-RG477M-DIAGNOSTIC", "owner": "Codex coordinator — sole device operator", "paths": [".local/device/rg477m-session1/", "docs/analysis/codex_device_report.md", "docs/analysis/codex_stage_status.md", "docs/analysis/codex_guest_context_report.md"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration", "branch": "task/codex-integration", "acquired_at": "2026-10-23T09:29:16.172503+00:00", "checkpoint": "d9a1667", "objective": "User-connected RG477M actual install/load/JNI/Vulkan/display/leaf/compiler-runtime diagnostics; no data clear or fake full-game PASS"}
- {"task": "T02-OWNED-STARTUP-PROFILE", "owner": "Runtime engineer", "paths": ["src/runtime/nso/nso_loader.c", "src/runtime/nso/nso_loader.h", "src/runtime/nso/nso_call.c", "src/runtime/hle/guest_process.c", "src/runtime/hle/guest_process.h", "tests/native/guest_process_contract.c", "scripts/runtime/build_guest_process_contract.sh", "docs/analysis/codex_guest_process_report.md", "src/runtime/hle/guest_context.c", "src/runtime/hle/guest_context.h", "tests/native/guest_context_contract.c", "scripts/runtime/build_guest_context_contract.sh"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime", "branch": "task/T02-codex-runtime", "acquired_at": "2026-10-23T10:33:12.677393+00:00", "checkpoint": "e95e521c79bead1c6de4f64d5b01416f1f639ec1", "objective": "Fix actual64752 vslogical65536 capacity preserving assertion; distinguish raw/native/logical/admin. Original8+Context4 authorized. Enforce typed guard compilation prerequisite. Do not implement pages yet.", "write_state": "RG_NATIVE_USABLE_STACK_CAPACITY_AND_GUARD_PREREQUISITE_FIX"}
- {"task": "DOC-HANDOFF-LIVE", "owner": "Independent documentation agent (reuse Android worker; sole coordination writer until handback)", "paths": ["coordination/HANDOFF.md", "coordination/state.json", "coordination/STATUS.md", "docs/analysis/codex_handoff_evidence_index.md"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration", "branch": "task/codex-integration", "acquired_at": "2026-10-23T10:33:12.677393+00:00", "checkpoint": "2e444a1 -> current34dd0c8 event docs WIP ready 2026-10-23T11:01:32.772142+00:00", "objective": "User explicitly requests recording in parallel with development/review; preserve current dirty coordination WIP; no runtime edits or Git mutations", "write_state": "PAUSED_FOR_COORDINATOR_SERIAL_COMMIT"}
- {"task": "T07-STARTUP-PROFILE-REVIEW", "owner": "Independent ordinary native startup profile reviewer", "paths": ["startup-profile/"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/functional-review", "branch": null, "acquired_at": "2026-10-23T10:52:24.868673+00:00", "checkpoint": "15e498c0985aee20c973632b6406e2b976628d81", "objective": "Frozen15e host normal paths PASS but macro-off P2 and actual RG capacity FAIL; report published, await immutable corrected source before acceptance/retest", "write_state": "REVIEW_PUBLISHED_PENDING_NEW_FIX_FREEZE"}
- {"task": "T07-VULKAN-BUFFER-REVIEW", "owner": "Independent prior Vulkan agent (not buffer component author)", "paths": ["vulkan-buffer/"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/functional-review", "branch": null, "acquired_at": "2026-10-23T10:59:17.066895+00:00", "checkpoint": "fa76ca979d711c68beebe9e6ae699914f8a1cdf0", "write_state": "ACTUAL_REVIEW_RUNNING_REPORT_SAVING", "objective": "Actually dispatched reviewer snapshottedfa76ca9 and independently ran40 ordinary mock cases,2ownership groups,coherent/noncoherent7failure-release groups each; report saving, no final QA PASS, no ADB/source edits"}

## 阻塞

- RG分项诊断artifact663040B/SHAd02c868...36ef实际1/36.30ms，唯一失败pthread-reported-stack-bytes64752(0xfcf0) vs65536少784B。reported bounds/真实SP/local、altstack flags0、Query七字段/padding/VA/returned_token均正确；原因只定位至实际容量差，未外推底层布局。诊断-only e95e521保留全部断言，production未改；作者获准扩原Context4文件修真实容量并分清rawmapping/nativeusable/logical64KiB/admin。先修RG contract，暂停新pages实现；旧15eHostPASS/RGFAIL不是新修复PASS。
- NVN535名字仅2查询入口可解析、533缺失，59缺PFN类型不能猜ABI。新生产VkBuffer组件已同RG transfer contract0，但尚无游戏NVN→Vulkan资源/队列/着色器/present桥接，标题/game_rendering仍未过。
- 既有 RomFS 流式工具和资源清单不等于游戏运行时挂载/IoStore 解压成功；尚未集成。保留合法本地素材，不获取新游戏或密钥。
- 旧完整NSO安全/异常QA被平台检查中止，不重试；普通组件独立review已分别通过，不替代完整游戏QA。
- 完整NDK归档校验NOT_VERIFIED；Google官方部分sysroot逐成员长度/CRC验证+AppleClang/RustLLD minimal真实出包通过。额外partial CMake configure0/build2：Darwin ar/ranlib不能索引ELF，尚未配置兼容交叉归档器。
- 环境未提供 CreateGoal / UpdateGoal 入口，未开启持久目标引擎；当前回合按完整目标执行。
- RG477M诊断GPU传输/owned-pages/数字L2R2与右杆实测通过；听感未确认、所需GPU更多操作/完整输入/休眠待验收。荣耀平板未接入；游戏本体未启动。
- Startup profile独立review在冻结15e498c发现P2：host编译关闭NSO_WITH_CXX_GUARDS时guard名称可能走extension（probe1，宏开对照0）；当前指定/Android配置不受影响，不解释RG容量失败。作者需加真实typed guard编译前置条件；新freeze待复核。

## 已验证

- Antigravity 交接基线 d26d3f6，接管时工作树干净、active_writers=[]；新开发使用独立分支/工作树，旧成果保留。
- 继承 T00/T00R 记录：内部元数据 mucha / FFRS，SQUARE ENIX，v1.0.0；ExeFS 与本地游戏包指纹保留。Codex 本回合不重新解密或读取密钥。
- 游戏引擎 UE 5.6.1 的既有直接字符串证据保留；sdk=SDK 20.5.17，subsdk0=movie，rtld=nnrtld.nss。
- 严格加载器实际解析当前 main.bin：651 个导入、4612 RELA、1370974 RELR；未知导入使用可定位 sentinel，不返回假成功；resolver callback 与正常有效输入契约已验证。
- main.bin SHA256 6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5；原始 leaf +0x694bd0 的 16875 组 Mac ARM64 真实执行与数学 oracle 零差异，前后输入哈希一致。不是完整游戏或正式 T02 PASS。
- 已实际生成并由两个临时Android ARM64 AVD会话重复执行的libffrs_leaf.so：2176B，SHA8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8；每次16875比较0差异，无构造器。仅单函数，不是完整T02。
- SDK 已证实的 ReferSymbol typed anchor、Allocate/FreeAddressRegion 的真实 PROT_NONE 预留已实现；4KiB/64KiB/2MiB/62GiB 预留、保护查询、释放通过，不触及实体页。
- 真实 POSIX Mutex 家族依据本地 SDK 字段 ABI 实现；8线程80000次 increments/overlap0、递归深度3、争用trylock、100次同址重建通过。Opaque owner token 不冒充完整 Switch ThreadType。
- 独立 guest-TLS PoC：8线程16槽、128000次值检查零差异且宿主 pthread key / TPIDRRO 保留。尚未绑定游戏 SDK TLS，不证明完整 TLS 兼容。
- Android T03A 包装范围通过：实际 AArch64 ELF/诊断CLI/JNI与 org.ffrs.diagnostic 签名 APK；DEX/Manifest/签名、16KiB LOAD、无 W+X/TEXTREL 静态检查通过。未安装或执行游戏。
- 诊断UI硬件事件/生命周期/app-private临时文件往返已在ARM64 AVD执行；不等于实体手柄/游戏音频/存档。
- 主机普通功能测试、NVN typed registry 并发行为通过；NVN coverage 真实退出3、2/535解析明确表示无渲染完成。
- 部分已校验 Android 工具缓存已复制到原仓库新建 .local/minimal-ndk（3330文件，未覆盖），后续可在交付目录重建。
- e0024a0 集成真实退出回调注册/DSO筛选/重入/并发及明确单guest实验scope；注册不会提前执行，正常exit返回23运行一次，_Exit返回24不执行，卸载丢弃不会调用已卸载代码。真实nnMain当前QueryMemoryInfo，仍非完整启动。
- 独立普通功能审查完成（adc6e8e / 5e165d0）：6次严格Clang编译均0，9次运行符合0/23/24预期；未发现正常Mutex/独立TLS/地址预留/退出回调范围需修缺陷。没有重启被中止的NSO安全/异常输入流程，不证明完整QA或SDK兼容。
- 独立main caller/TLS静态研究：21参数全部RELR；TLS模板+0x7767108/初始化0x220/总长0x1019/对齐8；已有59相邻常量外新证TP+0x258；49非相邻站点有界复核完成，33精确/15仅选定路径/1汇入未知；发现__cxa_thread_atexit，未绑定宿主TP。
- Android14 ARM64 AVD pvz2/nativebridge0实际安装冷启动org.ffrs.diagnostic（c280e2a，422420B，SHA8ec9d704ae331f77074e67cc63ee56b398ff89d6f3bf148f8de1b3f9637bb07d）；原始leaf实际16875比较0差异，构造器未调用，scope仅单函数。
- 历史6b7437f Android HLE CLI实际加载main0，旧guard缺失133；最新14ed86a已越过guard到QueryMemoryInfo133，完整CRT仍__nnmusl_init_dso133，startup仍AllocateTlsSlot133。
- 444115b独立module TLS镜像已真实复制/清零/对齐/释放；主机正常多线程与映射模板验证通过；独立纯C review 32968000检查无错。未绑定HLE、未写TP，复制的指针值不证明指向对象卸载后仍有效。
- AVD图形探针实测Apple M5/MoltenVK Vulkan1.3，仅虚拟GPU；版本4实际显示与leaf记录在session2，不能替代Mali/Adreno/游戏渲染。
- TLS49 static bounded study completed:33 local exact address formations,15 selected paths,1 unknown join +0x32221bc; newly confirmed TP+0x258. All49 no matched FDE coverage, reachability/whole-path absence not proven; no gate opened.
- aa8a623 canonical版本4 APK426516B SHA bb8bca99af247c52dd7f2890b81f3c2aeabc88574a7dca98c6625c23ee3bb646真实临时ARM64 AVD安装冷启动；WindowMetrics1080x2340/content1080x2138/dpi440/60.000004Hz，音频unknown0，原leaf重测16875/0。历史API28/29 insets P2已由fbea2e7修复并集成；旧API设备实测仍NOT_RUN。
- 14ed86a guard真实AVD：info0、nnMain/malloc越过guard到QueryMemoryInfo133；构造器/启动边界仍严格。独立8线程+异常guard contract134，stderr uncaught exception int，可能缺EH frame hdr，未验收。
- a22d0dc真实Android同AVD修复闭环：保留完整throw/catch/retry，guard contract0，8线程80000/初始化1/abortwinner1/异常attempts2/errors0；补.eh_frame_hdr/PT_GNU_EH_FRAME。info0；main/malloc仍QueryMemoryInfo133。没有注册guest NSO unwind或执行完整CRT。
- root checkpoint4690706普通host23检查均符合真实退出码（含NVN预期3、正常exit23、_Exit24），原始leaf和当前host JSON通过；不替代Android/游戏/T02验收。
- RG477M actual device: canonical v4 install0/cold219ms; Mali-G615 MC6/Vulkan1.3.247/descriptor indexing; window1280x960/content1280x918/dpi280/current120Hz (panel not FPS), modes60/90/120. Original leaf16875/0 on real hardware. Production no-overwrite staging helper copied/verified2176B successfully.
- User-confirmed real buttons/sticks: own app recorded45key+47motion+pause/destroy after baseline; directions/B/BACK and leftX/Y[-1,1], right axes not observed. Native /dev-input probe falsely saysok/0devices under permission restrictions; public InputDevice snapshot fix assigned, no root/SELinux workaround.
- RG477M正常生产链HLE CLI与独立C++guard实跑：guard0、info0、main/malloc QueryMemoryInfo133、CRT __nnmusl_init_dso133、startup AllocateTlsSlot133；均未超时。APK JNI另真实加载125577976B授权主模块，651imports/4612RELA/1370974RELR，mode load_and_relocate_only/游戏执行false。
- 用户在本任务明确批准继续修改/暂存/提交代码和报告到本地任务分支（不推送远端），随后补充完全访问授权。runtime原2文件提交拒绝在此新直接人类授权后仅重试一次成功f7a2ca1，无未决审批阻塞。
- RG477M actual Vulkan operation probe exit0:64KiB buffer/8x8 RGBA256B zero mismatch,31 VkResults0,realqueue/fence/readback,12destroy/3unmap. Not shader,descriptorindexing,timeline or game renderer. Frozen C SHA2e11d78de212d9b1a6d974ba9a68a88474cf42506865a7d318d852950c457403; artifact6e66aced9f0db6e67db754a2122be75febd51173d005515a831f9c60c36869b8.
- canonical v6 cleanfb7d870:35 buildsteps0,451092B SHA4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a;RG install-r0/cold195ms/events+leaf preserved/no permissions.
- RG owned-pages e112d65 normalcontract0,4KiB,8threads×32,32release races,mapped0/validtrue/errors0;host independent rollback/quarantine reviewPASS,not device fault-injection nor SDK Query binding.
- RG v6 actual rightZ/RZ[-1,1],L2/R2 digital keys8/10;analog trigger sample0. Audio48000Hz/12000frameswritten+headadvance/246ms/speakerroute/cleanupok/volumeunchanged;human did not notice, audibility NOT_CONFIRMED.
- root fb7d870 ordinary host23checks all expected0/3/23/24, original hashes stable.
- NPDM actualmain-thread stack0x10000=65536B,systemresource0x1400000=20MiB distinctkernelmanagement reservation,Application class;readonly parser0/inputSHAstable. Totalguestbudget not encoded by these fields, no Query binding.
- TLS可空GLOBAL OBJECT hook安装/清零和普通新建线程返回OSslots→state/notify→hook→ELFdrain限定顺序闭合；790 evidence lines逐字节核验，非STB_WEAK，异常/初始线程不外推。
- RG v6真实再次JNI/moduleload:LOADED_DIAGNOSTIC_ONLY，651unresolved明确未调用；report changed + fresh start/finish events verified，非沿用v5旧报告。
- e846dc1真实显式quota+ownedprimary stack组件RG contract0/35.5ms/no timeout:SP verified/65536B/双guard/hostTLS/join清账，8threads2accepted6LIMIT，memory_info_valid0/errors0；production无故障hooks，mapfailurecases0。未绑定Query或game。
- GuestContext e846dc1独立FINAL PASS：production/recovery/校正补测严格编译0、运行0；pending并发2 accepted/6 LIMIT、pthread析构阻塞期间保留费用、join后退款，七字段仍invalid0。初次补测未承诺的TLS析构顺序假设已纠正，保留失败1/初次child code未落盘事实；非游戏或完整QA通过。
- 图形底座有界评估已结束：Strato GPU ae1566a和Hades shader 8717c8f真实存在，但没有直接NVN ABI桥；需图形负责人选择生产接入边界并核清逐文件许可证。未clone/build/probe，不证明整游戏可行。
- 用户质疑外围投入后，停止新增外围probe；主优先级收紧为真实Query/CRT/TLS与NVN生产路径。最近16提交按主要目的粗分5直接runtime/启动链、5诊断包装、6分析记录，仅描述提交用途，不是工时、完成率或游戏进度。
- 第一接力检查点11e861127c84a2c7904804ec2bc4748b1df71fdb已由总控本地提交；原交付work/rg477m-native安全ff到同SHA、采样时两树clean。文档现恢复四文件独占，Git/设备仍总控唯一。
- 10:40:12UTC总控真实恢复快照.local/handoff-runtime-check/snapshot.json：ps -axo pid=,comm= exit0，限定AVD/qemu/gradle/cmake/nso名字匹配空；adb devices -l exit0只有授权RG477M。仅last-verified快照，不表示今后永久无进程或没有开发推进。
- 未冻结startup profile首轮host normal fixture真实build0/run0：errors0/calls2/extension_calls0/stack65536/query_fields7/padding preserved/thread joined/signal dispositions restored，控制失败+retained retry5阶段。仅普通fixture，非被中止的旧异常NSO QA。
- 10:42:46UTC runtime未冻结Mac16KiB host原nnMain+0x352f160真实Query valid0x7f：total1073741824=明确1GiB诊断port-policy，used162938880/heap0,0(READY_EMPTY)/baseline162873344/stack65536/count1；随后AllocateMemoryPages sym71 caller+0x27f90，ownedVA+0x10000/size0x10000，exit133/0.6416s/no timeout。作者报告原main未patch且hash保持；文档未读取原始游戏字节。Android/RG与TLS/CRT/game尚未通过。
- Frozen startup profile15e498c / integration1997f27: .local/owned-startup-v2 records33 steps, expected invalid Query -6/module quota1/original nnMain133/malloc133 and remaining steps0; no timeout. Source hashes match frozen checkpoint; Android contract649672B and CLI648848B built, at this pre-RG freeze snapshot Android execution was NOT_RUN; subsequent RG contract1 is recorded separately. Mac nnMain passed Query to pages71/+27f90; malloc to VA usage66/+144b8. Explicit1GiB diagnostic port-policy, no complete CRT/TLS/game claim.
- Actual RG frozen15e498c ordinary startup fixture contract649672B/SHA646adf...ab95 failed: exit1 expected0,42.27ms/no timeout,line202 value==&returned_token && !errors. Query valid0x7f outputs fixture1MiB/used102400/baseline36864/stacks65536; not acceptance. info/original nnMain/malloc not executed for this freeze. Raw owned-profile-execution.json and owned-profile-contract.stderr retained; original assertion not relaxed.
- Production VkBuffer freezefa76ca9 integrated34dd0c8: same RG actual ELF198584B SHA2088ffd84187f8f868f6d892584a4d4164e09c6091bddc0f0531c948347ed3b9, exact source bundlea43b88df0741e61581f069d95e33342d6f1759deb58b60605665538cd540d26f; execution0/90.15ms/no timeout,31VkResult0,64KiB buffer/256B image zero mismatches,12destroy/3unmap,validator0/PASS. Existing driver uses production resource component; no NVN bridge/game_rendering=false.
- Integration source1997f27 clean host checkpoint:23 checks meet actual expected0/3/23/24 and original input hashes unchanged; summary .local/host-verification/checkpoint-1997f27/summary.json. Later GPU integration34dd0c8 has its own same-RG/validator verification, not falsely covered by the earlier host source checkpoint.
- RG diagnostic-only e95e521 preserves all assertions, production unchanged:663040B SHAd02c86845607be6f1da3a546a04b515c986d31c1402601f73e4af7ec715e36ef actual1/36.30ms; sole tag pthread-reported-stack-bytes64752 vs65536 delta784. Bounds/SP/local/altstackflags0/Query7/padding/VA/returned_token normal. No capacity fix PASS yet. Separate frozen15e QA P2 macro-off extension guard does not explain RG failure; current Android macro config unaffected.

## 未验证

- 完整CRT/TLS/线程/guest内存上下文与真正完整入口，正式T02参考行为验收。
- 游戏资源挂载、NVN→Vulkan后端、着色器转换、surface/present、标题与可玩场景。
- 诊断听感、完整键轴/休眠；游戏输入/音频/存读档；荣耀平板实际身份及GPU能力。
- 真实场景30FPS/45分钟稳定性及完整独立QA。
- Bundled NVN header explicitly reconstructs older53.105/Darksiders ABI; matching current game object layouts not yet proven. New generic Vulkan buffer component must not expose guessed NVN structs.

具体恢复命令与未完成修改见 HANDOFF.md。

# RG477M startup capacity correction and original-entry gate

Latest gate, page profile `f24b826b5d118b2479a476ec61aba778df26e021` (coordinator integration `c414aadbff878c179d4f1687c27df60e2ecf6e54`): same RG normal contract/info **0**, original nnMain passed Query and **real RW map65536B**, then strict `CreateThread` import126 / caller`+0x352f1b8` / **133**; malloc still VAUsage66/`+0x144b8`/133. No timeout. New contract also checks Query page charges, exact-free refund, recommit-zero, whole-owner release and output padding; errors0. See [page-owner and profile report](codex_guest_pages_report.md), raw `page-profile-v1-*` alongside earlier `capacity-v1-*`. The old d4 test scope and old failures below remain preserved. New page-profile independent host review completed in [codex_guest_page_profile_review.md](codex_guest_page_profile_review.md), using f24 process files and4695 page sources; distinct from d4 review.

Actual six-argument CreateThread has x3=owned VA+0x1000, **x4 stack bytes=0**, x5 priority16; mapped pages were VA+0x10000..+0x20000. These are raw observed arguments, not a proved reason or valid thread precondition. Read-only provenance is now closed: actual nninitStartup must allocate OS TLS slot59 before it writes the1MiB worker configuration atmain+133f4; the current directnnMain call has not run that startup. See [initialization-order evidence](codex_startup_thread_gate_report.md). Do not choose a default size, mark success, or bypass initialization to advance a trace.

Source freeze: `d4ded908096feef6fff2876ab2ffdaa9e48303b5`; integrated as `c5bbddbe2487e27e16a05627580607c0b956351a`. This report records coordinator device execution, not reviewer device execution. The prior `15e498c` normal contract exit1 and diagnostic exit1 remain preserved.

On the same RG477M Android14 ARM64/4KiB device, the first native stack report remains 64752B for a 65536B raw RW range; callback is withheld. After joining, actual unmap/refund and checked page-rounded administrative capacity rebuild, attempt2 has raw69632B, native68848B, administrative4096B, exact logical65536B. Callback SP/local are inside the logical window. Both outer guards are checked using raw bounds. Query continues to charge logical65536B only. This does not hardcode the observed 784B deficit or claim a general Bionic private layout.

| Actual device command | Exit | Scope |
|---|---:|---|
| standalone Context contract | 0 | verified raw/native/logical relations, guards, SP, joined cleanup, quota competition |
| startup profile normal contract | 0 | two callbacks, seven fields/padding, VA/return value/altstack/disposition recovery, errors0/extension0 |
| original module profile info | 0 | image162869248B, imports651/unresolved619; CRT/TLS explicitly uninitialized |
| original nnMain +0x352f160 | 133 | crossed Query caller+0x27a50, stopped strict unresolved AllocateMemoryPages sym71/caller+0x27f90 with address in owned VA and bytes0x10000 |
| original malloc(4096) +0x132d0 | 133 | crossed Query+0x1398c/+0x27a50, stopped strict GetVirtualAddressMemoryResourceUsage sym66/caller+0x144b8 |

No timeout; no tail pipeline. A zero contract/info exit is not a successful game entry. No constructors/complete CRT/TLS, title, rendered game frame, playable scene, game input/audio/save or performance has passed. No APK update, data clearing, firmware operation or permission request occurred. Raw self-built ELF only in the existing project-owned shell directory. The source main was not patched; SHA256 before and after is `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`. The 1GiB quota is explicit diagnostic port policy, not measured Horizon physical RAM. The 62GiB address reservation remains virtual only.

## Reproduction and evidence

Coordinator checkout `.local/device/rg477m-session1/capacity-v1-plan.json` records source hashes, build-manifest hashes, artifact paths/full SHA256, existing material identities and explicit policy; `capacity-v1-stage.json` records exact ADB argv/readback; `capacity-v1-execution.json` records actual commands, return codes, elapsed time and stdout/stderr hashes. The same prefix's per-case stdout/stderr are retained. `run_capacity_v1.py` is the executed script. Do not replay its staging blindly: fresh hash-qualified paths already exist. First inspect stage/execution; verify/reuse only explicitly. Each command has device timeout15s and host timeout22s.

- `context`: 78432B, SHA256 `6d7d46bae9a0b97f8a25221b6d1213f02ec748f8d17c92e6144ee198b761dda5`; local `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime/.local/guest-context-capacity-v2/guest_context_contract.android`.
- `contract`: 675792B, SHA256 `aa82fc592bb4db849375aa54efdc0cfb17b703cde29f3d5f4456aad77abb2d86`; local `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime/.local/owned-startup-capacity-v1/guest_process_contract.android`.
- `cli`: 655104B, SHA256 `1e5953a8144855d65f93310c5d68fce215abe097d1ff94f8e6fed9cd8d6a6d69`; local `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime/.local/owned-startup-capacity-v1/nso_owned_startup.android`.

Independent host review of the new frozen Context/profile completed with no remaining specific component defect; see [codex_native_stack_capacity_review.md](codex_native_stack_capacity_review.md) and `../functional-review/startup-capacity/`. Native capacity, retained cleanup and cross-TU typed guard coverage passed within the stated component scope; this is not a game or reviewer device PASS. The earlier review in [codex_guest_process_review.md](codex_guest_process_review.md) applies to15e and records the old macro-off P2. New code directly references typed guards and rejects an untyped host profile build; review will verify cross-TU behavior.

Next production gate is a single owner for real VA and mapped-page tickets, integrated into Query only after successful real map/commit, retained on ambiguous failures, exact free and whole-region refund only after actual release. No guessed SDK error mapping or success stubs.

## Native-boundary integration85a13c, same RG actual execution

Source85a13c79378550fe850ffb5ff182cdbdfec177f8 with pages4695e3c/boundary5c20b064, integrated70c3e84. Exact source/dependency SHA checked before staging; main SHA6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5 unchanged before/after. Five ordinary positive/native ABI/cleanup contracts all exit0 (63.98/60.93/61.18/54.74/50.33ms); two actual self-authored guest calls, raw_fallback=false, ended_session_rejected=true. Init executor and recorded-import-only retarget accepted; readonly unsupported leaves bytes unchanged. Controlled failed RX/rollback retained ownership,3 unmap attempts/1actual release, no guest execution in retained case.

Info0/147.10ms. Real original nnMain through nb_call and33 accepted import thunks still reaches Query then real RW65536 map and CreateThread126/+352f1b8/133,154.75ms; x4=0 because true startup has not completed. Original malloc133/66/+144b8,187.42ms; nninitStartup133/59/+133a0 with nonnullcallback,165.62ms; true DT_INIT wrapper133/515/+5920a34,165.52ms. All no timeout. These133 are strict missing implementations, not startup success. New boundary closes known register-only x18 domain-switch debt, not original TLS/CRT/21-argument ABI or ThreadType/alias semantics.

Contract771408B/SHA0f4d57dd3243893ef2f2cf4f61e80bb0638b7d2e72739b32dba55d740cb40e82; productionCLI725256B/SHA88004a43021e49662f0b5fe1268c0d9604f675197382347f2259c8a2599a27e7; retainedcontract779416B/SHA5c6d581225f95c59e0eaf59d9ad7bd678d6b1b45c6bdac762e30afae60250140. Logs/argv/real exits/stdout+stderr SHA in .local/device/rg477m-session1/native-profile-v2-{plan,stage,execution}.json and eachcase stdout/stderr; executed run_native_profile_v2.py. Component-independent review5c FINAL is not this new profile independent review; profile QA pending.

## Actual OS TLS binding28dc1a: original startup now beyond59

Source28dc1a792946ae990690f11d71e89a60bf5d8b8b→rootfb6d08b971d1ebdd12aa25224a52a518b431b168; exact dependency sources pages4695/boundary5c/OS9c checked. Actual RG7 contracts0 plusinfo0, no timeout. APP wrapper16slot/exhaust4611/free-keeps-value/reallocation-clears/two distinct primary records0. Self-authored native callback fixture9216B/SHAfe0c3532a6d7783624d094317ca07a5661cf4fb68ce262a0626d63688a37ac45 actually invokes A(0)→B(9)→A(5),order0x121,3real invocations/3rounds/errors0; B reenters Set and Query while ThreadRecordRUNNING/thread_objects1/stack65536. This is native guest callback execution and does not run this game's main+18c60 destructor. Retained boundary test still3attempt/1actualunmap and0.

**Real original nninitStartup now passes AllocateTlsSlot59 and stops at GetHostArgc60/+0x13420**, exit133/149.603916ms/no timeout. Actual slot0 registration keeps nonnull callback main+18c60. Reaching60 follows the original straight-line1MiB-store block, a control-flow+static evidence level; no live BSS readback was performed and no BSS value was patched. Startup has not returned, therefore original main+18c60 cleanup and full OS TLS compatibility remain unverified.

Direct nnMain without preceding completed startup remains126/+352f1b8/133,150.97ms, Query→realmap65536 passed but x4=0. Malloc remains66/+144b8/133,145.65ms. ActualDT_INIT wrapper still515/+5920a34/133,160.18ms; 21 fixed scalar-word transport is separate in-progress code, not musl semantics. Main SHA6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5 unchanged before/after.

ELFs: contract824656B/SHA7fa988417a9c3a147edf90d43734299bc78dd488dad10cb192d01471ff4130c3;CLI759352B/SHAb142eb52f0f17eb48d5f29f400181f576ae021eb82c67726e75bd4e1ca6c9973;controlledcleanup832768B/SHAba358bd81b6335b908bc47ffd70c9033a996a89d411f7e7327a1af140a1d2cb5. Root plan/source/dependency hashes, fresh staging/readbacks, actual argv/exits/stdout/stderr hashes at .local/device/rg477m-session1/os-tls-profile-v2-{plan,stage,execution}.json and eachcase output; executed run_os_tls_profile_v2.py. No APK/data/permission/firmware/TP changes.

Author finalv2:54captured commands match their expected0/1/-6 with no timeout; separatelygenerated fixtureinfo0. Earlier v1 fixture linker used an incorrect image-base flag and actually exited1; retained v1 log is a producer tooling correction, not a hidden game crash. Finalv2/freeze and manifest in ../ffrs-runtime/.local/os-tls-profile-v2/. Independent9c component acceptance does not substitute for new28 consumer review (pending). Missing provider in ordinaryhost contract really aborts134 before cleanup success/stack return; failstop is not a recoverable release. ELF TLS/SDK ThreadType/alias manager/CRT remain unsupported.

## Owned guest arguments9dc: current actual RG checkpoint

Source `9dc8aa73c6e2d038c2b1c89efd898b65fbf0d1bb` integrated as `3af578a`. Real RG477M/Android14/ARM64/4KiB, same original main SHA before/after. No APK update or data clear. The first two identity attempts returned1/device-not-found before any push/execution; later USB identity verified and the fresh `guest-arguments-v1-usb-ready-*` run completed.

| Actual RG command | Raw exit | ms | Evidence scope |
|---|---:|---:|---|
| normal | 0 | 50.725 | actual primary/Query/page lifecycle |
| arguments | 0 | 46.7765 | empty charged page, copied mutable strings/vector/NULL tail, multi-page |
| arguments-native | 0 | 62.240041 | 3 actual self-authored native getter calls; no raw fallback |
| arguments-recovery | 0 | 60.010917 | quota before VM/map rollback/retained unmap retry |
| OS callback + argv lifetime | 0 | 48.16525 | real A0/B9/A5, argv still live inside guest cleanup |
| boundary-retained | 0 | 43.349167 | 3 unmap attempts/1 actual release |
| info | 0 | 158.545458 | imports651/unresolved613, not game startup |
| original startup, empty argv | 133 | 146.479125 | passed60/62, nowVA resource usage66/caller+0x13744 |
| original startup, argv ffrs/option=123 | 133 | 161.929625 | strncasecmp_l63/caller+0x13498, n29/locale0 |
| original direct nnMain | 133 | 150.707542 | CreateThread126/+0x352f1b8, pre-init experiment |
| original malloc | 133 | 151.632 | VA resource usage66/+0x144b8 |
| original CRT wrapper | 133 | 148.647708 | __nnmusl_init_dso515/+0x5920a34 |

All runs no timeout. Raw plan/source/ELF/fixture hashes, exact argv and exits: `.local/device/rg477m-session1/guest-arguments-v1-usb-ready-{plan,stage,execution}.json`; executed `run_guest_arguments_v1_usb_ready.py`. Freeze `../ffrs-runtime/.local/guest-arguments-v1/freeze.json` SHA256 `7c51d0a928123956f90b6ec38903426ef693f0b66bd124c97fa812e706a99354`; source build manifest SHA256 `cac385920d0e37a41f9a203f802d52920bf3071018b66a82a7827eab4d891c51`.

Actual CLI771864B/SHA`ff26a2887a723301b076f0e390fcaad3aad82589daf9f82916cefa287b7cda96`; normal853288B/SHA`430dcb1feccb757997b29a0b06abcfad1de0e689979afeffc53c83fbc052dd86`; argument recovery867920B/SHA`b62ab981f388603d25c465ec10acf09646b5fb1c850dd79b8aeb6df32b60f95e`; boundary cleanup861992B/SHA`bef567c61896d09ceeed823dede301fcad8326ff74bb6cd98bd23ba181f3320c`. Updated OS fixture SHA`ec889611349d57808b800086ec387e8102fd658d29c08a20ee8611b38012bf15`, not old28 fixture. Empty argv now really charges4KiB: startup Query baseline162873344/active162938880; this does not mean host RAM or original physical SVC measurements.

[Independent9dc review](codex_guest_arguments_review.md):53 commands meet expected48×0,guard1,twoSIGABRT(-6),twoUsage2; no timeouts. Ownership/free ordering, caller-free copies and metadata failure independently exercised. Reviewer Android NOT_RUN; root device results separately attributed. This advances startup interfaces, not complete CRT/title/gameplay.

## Resource/locale3db: actual RG advancement and new malloc139

Frozen3dbb0dcc81543b790c75510e9320981e429046f2, ../ffrs-va-resource clean, exact12 source files/base3af reviewed separately. Actual sameRG13 normal/arguments/native/OS wrappers/callback/resource/native/locale-native/owner/locale/resource-recovery/argument-recovery/boundary-retained contracts all0, info0. Native resource fixture actually3quota dualx0/x1 calls withx18; Bionic locale component65536 unsignedpairs and6actual typedguestcalls0. No timeout. Query remains explicit1GiB data quota,64KiB stack and1MiB actualmanagementarena, not physicalRAM/RSS.

| Original entry, newfreeze | Raw exit | ms | Actual observed next gate |
|---|---:|---:|---|
| startup-empty | 133 | 143.098833 | passed66; IsVirtualAddressMemoryEnabled67/+0x137a4 |
| startup-nonempty | 133 | 176.639125 | passedstrncasecmp_l63; strcasecmp61/+0x1345c |
| directnnMain | 133 | 148.612625 | pre-initCreateThread126/+0x352f1b8 |
| malloc4096 | **139** | 143.75425 | passed66, realPROT_NONE VA reserve66571993088B, thenSIGSEGV main+0xb705c/fa0x30/x8=0 |
| CRTwrapper | **NOT_RUN thisfreeze** | — | testsequence stopped after unexpected139; old9dc515/133 not copied as newrun |

Do not call malloc139 a pass or attribute it toTLS/CRT before provenance. Fullstderr contains truePC/registers/frameoffsets. The capture script intentionally exits1 after retainingunexpected139 and checkingmainSHAafter; this is not hidden bytail. Inputmainbefore/after6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5 unchanged. No APK/data/permission/TP change.

Evidence .local/device/rg477m-session1/va-resource-v1-{plan,stage,execution}.json and eachcase.stdout/.stderr; executedrun_va_resource_v1.py. Fullfreeze ../ffrs-va-resource/.local/va-resource-v3/freeze.json SHA1c2687b49c775733e38d70be4129eed60d049aabf6f3eeff584cda0b32d1e4db;manifestSHAfcb0cbd0a64133f4a37f2a8d8980ed3aa8f19f118566ae4a3bfcf1e68cfdfbfa,88 actualexpectedcommands/noTimeout. CLI803896B/SHA72405ea9a9201579dfebc00b2162249e5ebd33166861f4f180c9b9a59ae91ba3;normal936296B/SHA91d3ad61d27723f7495aa94882dd99541666d4648399d93609ca189e77890542;resourcerecovery950336B/SHAeaa0018963e298723cb085f9f86cc3eda65807744129b00eed81b9a6d5c54d96.

New independent3dbsourceQAactive; root hasnotintegrated3dborclaimedfullgame. Runtime nowseparateffrs-startup-next/base3db fivefiles61/67; mallocnullprovenanceonlystartup-followups/malloc-null. Preservefreezeandraw139 whilefixingnewgate.

## ff964e9: original startup reaches actual allocator math

Five-file freezeff964e9d479e288e2b695b5100836def780ca4df preserves3db tree. ActualRG8cases/noTimeout: strcasecmp-native0/93.011625ms (65536u8pairs+6actualguestcalls),va-enabled-native0/86.696167ms (unconfigured0/explicitpolicy1 ×5actualreserve/map/free states=10guestcalls,x18),normal0/76.22175ms,strcasecmp0/89.895875ms,capability0/84.631209ms,info0/164.938375ms. Bothunmodifiedoriginalstartupempty/nonempty nowpass61/67 andstrictstop**logf153/caller+0xb3eac**,raw133/172.596584ms and166.659667ms. Frames137f4→ae93c→af320 are inoriginalallocatorconstruction,not startupreturn/game success. Exactoldmalloc139isnotrerun;CRT unchangedNOTRUNthisfreeze.

Capability truth followsactualowner/completeledger/explicit resource_total>0,notconstant1 or requirement toalreadyhaveallocation. Actualoperationsreserve/map/decommit/releasearecovered;scope isnativeownerpolicy,notfullHorizon VM. MatureBionicstrcasecmp isactualtyped2argcall withverifiedSDKASCII/u8 semantics. Source/binaryandself-containednewfixtures allhashverifiedbeforeremotechmod,pushreadbackandexecution. Main6e6d...a5 before/aftermatches. No gamebytes/APK/data/TPchanges.

freeze .local/startup-next-v1/freeze.json inffrs-startup-nextSHA6fc7d78db3edfdc02a8d2a843bc561c20e305f3b8da97e7ca47fb8fd966533a8,97actualexpectedbuildercommands/noTimeout. CLI807440B/SHA06c22db0428ee2dc5c7a5614edba3949a9601f116e67eff9e534320f595c3797,normal966232B/SHA21ac601bd9ba617194604350b0f80f777698bf94e0adf8ab7687c29b2e6d2f03. Fixtures9216B each:strcasecmpSHA0f76d2220ab857358f8d63284941c350cbf0d264031f2ad16ca3c5b0730e62ef;vaenabledSHA639cd080e82577fd44ecad9433122957a5a3643b2a8b327a65ff3c24a9084074. Rawrootstartup-next-v1-{plan,stage,execution}.json/stdout/stderrandrun_startup_next_v1.pycontainexactargv/rawcodes.

[139provenance](codex_malloc_startup_prerequisite.md) identifiesb705c LDRx3,[x8,#30] afterb7058 LDRx8,[x0],x0=BSS77c5210nonzero/firstqwordzero. Realstartupwriter137f0→ae770 writesvptratae7ac/ae7ec;previousdirectmallocwasbeforethatstage,notTPorx18fault. Thisdoesnotprovefullconstructorsuccess. Nowactualstartuphasreachedthatconstructingcallchain,butmath153stillpreventscompletion. Do not manually fillvptroraddfakeheap.


## Combined92fd886: root integration actually rerun on RG477M

Production source commit `92fd886e4338cb3b02a717dbaa991452489f7130` combines GP21 boundary7be, resource/locale3db, strcasecmp/capabilityff964e9, libm logfb197 and diagnostic-only caller bounds0708. This uses a fresh root build, not the producer binaries linked against old5c. Build `.local/runtime/integrated-92fd886/manifest.json` recorded101 commands with expected host usage/failstop/unsupported outcomes and no timeout. Source/dependency SHA matched current committed production files before deployment; only coordination/report WIP existed.

Actual same RG477M/Android14/ARM64: systemlogf0/76.093041ms; native15-FPcall contract0/87.782208ms; normal0/86.732083ms; info0/225.082458ms. Original startup empty133/182.680ms and nonempty133/194.006334ms both reach `SetMemoryAllocatorForThreadLocal` index69. Both now correctly report a raw `caller_address` and `caller_outside_module=true` because the real tail call restored a host LR; no unsigned-wrapped offset is treated as game code. All cases no timeout; main SHA before/after unchanged. No gamebytes/APK/data/TP changes. Full startup return/CRT/title/game remain NOT_PASSED.

Build freeze SHA256 `45f0222985aeee189525b98dc1a74267c0b7ab0ce42f37eaa63dadc072bd0383`. CLI807776B/SHA `8300d0a19ef7d02ad8083d8bf3999dcb8d43cc237ad947e0a792a15bd7f37388`; normal977456B/SHA `13f7dbc08031c98ef7ab3ce22b898f2105511f63185daab3b8113d97ec802d6e`. Raw exact commands/exits/hashes and stdout/stderr: `.local/device/rg477m-session1/integrated-runtime-v1-{plan,stage,execution}.json`, executed `run_integrated_runtime_v1.py`. Prior math implementation device results remain `math-runtime-v1-*`; [independent math review](codex_math_runtime_review.md)12 commands0/noTimeout, Android NOT_RUN by reviewer and root device separately attributed. Root combined rerun is additional integration evidence, not a new independent device review.

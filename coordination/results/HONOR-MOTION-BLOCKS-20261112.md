# Honor Pad rendering and movement regression

## CPU cache deployment update

Source d465e3f caches immutable family getter function addresses with pthread_once and looks up existing Shader variants by sorted binding/format pairs before constructing a text identity. Underlying getter calls, missing-entry handling, bindings, resource lifetimes and stable ib1 identities are preserved. Host8-thread value-changing cache tests and source review passed.

Build5 and preserving-data install-r exited0. APK .local/android-apk/FFRS-Integer-Buffer-CPU.apk SHA-2560db386552bf4ab01f8ca9b513f592f43b581db7ef47b913812ca52a3cff802d4 matches installed base.apk. New PID31064, GPU marker1, startup warm1212failed0 in2879ms. saves-before-cpu-cache.tar preserves preinstall saves.

CPU profiling collected3542 RHI user-cycle samples with frame-pointer chains. Many chains failed, so children percentages are not used as a benefit estimate. Flat self samples include memcpy6.09percent, strcmp4.60percent, descriptors_build4.10percent, descriptors3.16percent andIntegerBufferSelection1.99percent. A separate DWARF run collected3464samples. Among119strcmp samples,48.86percent of sampled cost has a recovered TextureInitialize parent. Unexpanded stacks remain uncertain. Immutable lookup scans in actual source provide the independent optimization rationale. CPU-clock and cpu-cycles kernel-inclusive attempts were unsupported. cpu-cycles:u recording succeeded without security-setting changes. Raw PMU, command plans and reports are retained locally.

The game moved from Shrine entry to an inn and then a narrow passage during the prior process. New Continue restores a cave passage with an equip-phantom objective. These are separate workloads. Current field30FPS log windows do not prove same-scene improvement over the heavy entry movement test. Actual moving, battle and transition regression continues. The corridor-first window282distinct frames29.7757FPS, P9533.6885ms, max83.4826ms,3above50ms and0above100ms was interrupted by a tutorial dialogue. Root inspected all four screenshots afterward and withdrew its movement classification. Exclude it from movement acceptance. The later5second south traversal155frames30.0007FPS also enters the post-equip partner dialogue and is a mixed progression window. Current native moving30FPS is still pending. Readable persistent regression tests/installer/texture_family_lookup_contract.c was separately compiled and rerun by root under ASan/UBSan in normal/missing-builder/missing-pool modes, all exit0.

## Guarded deployment update

Source checkpoint d5a3340 built successfully. Installed .local/android-apk/FFRS-Integer-Buffer-Resident.apk with SHA-2561e2ca2121aa0e9c3fca3628e8d7f30c73b01c36e89899c99df62722cab05b56e, verified against installed base.apk. PID28177, preserving-data install-r succeeded. gfx-indexed-resident is now1. Normal Continue restored the Shrine entry.

First guarded no-record moving test234 distinct frames24.993FPS, P9553.294ms, max103.125ms,21 above50ms. Warm repeat242frames25.560FPS, P9558.720ms, max84.922ms,23 above50ms. Screenshots verify actual forward/backward displacement. This entry camera has approximately987draws/frame, while the earlier bridge camera had approximately608. Those measurements are different workloads. Existing liveGPU29FPS numbers cannot establish guarded entry-scene acceptance.

Current route reports sync_wb0, roughly10submissions/frame, no new pipeline creation, CPU23–24ms and GPU16–18ms in nearby windows. Startup warm1212 successful, failed0 in3146ms. Pulled warm list has1212unique records and15specialized identities, including21ee and63a5. Inspector validates all identities and framing. Actual warm reconstruction succeeded on this device.

RHIThread was92.5%CPU in a bounded top snapshot. User-only cpu-cycles profiling is in progress. Initial simpleperf cpu-clock/cpu-cycles attempts were unsupported, while cpu-cycles:u runs without security-setting changes. Further CPU bottleneck work and battle/transition regression continue.

## Previous checkpoint

Work remains in progress. Device A42BBB5109000333 is ROD2-W69, SM8635, Adreno 735. The active worktree is /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro on task/T-honor-shrine-20261111. Other writers and the separate stability worktree are preserved. No RG477M operation or remote push occurred.

Public integer-buffer candidate installed with adb install -r, exit 0. Installed base.apk SHA-256 matches 594898edd86005e3e516f857e0e2880a786607365d6dcb3101220b868353885d. APK is .local/android-apk/FFRS-Integer-Buffer-Specialization.apk, build3.log exit 0. PID23697 reached the actual Shrine and a bat battle through normal Continue/gameplay. Existing saves remained available. Full save backup before installation is .local/honor-motion-20261112/all-saves-before-public-fix.tar.

The renderer selected 21ee00f1479f5931:ib1:64_74 and 63a540158580d1d5:ib1:64_74 at runtime. Current inspected Shrine and battle images show lighting, crystals and characters without the previously reported rectangular ground patches. This covers the inspected views. Whole-game visual acceptance remains open. Several other UINT Buffer consumers are structurally unsupported and explicitly logged unchanged.

GPU indexed resident is currently enabled only through a reversible live diagnostic in PID23697. Its persistent marker remains 0 at this checkpoint. The next candidate adds safety guards before persistent enablement. Build4 is running with tool session47784, ordinary log .local/honor-motion-20261112/build4.log and exact build plan build4-plan.json. Do not attach LLDB using its changing unstripped library against PID23697.

## Systemic rendering change

The observed independent defect is a floating sampled-buffer declaration paired with an actual R16_UINT Vulkan buffer view. The prior FTZ explanation was unproven. Vulkan requires the relevant loads, stores and bitcasts to preserve these bits.

The new helper checks the complete supported consumer graph, including direct integer bitcasts and single-store Function temporaries with dominance checks. It clones the descriptor type chain and keeps native R16 elements and ranges. It uses no shader hash or fixed binding whitelist. Actual R16_UINT and R32_UINT views select immutable variants before descriptor and pipeline memo lookup. Float views retain their base module. Registered view-format overrides are honored consistently by selection and descriptor creation.

Runtime and warm loader share versioned identities. Legacy16-character base records remain readable. Specialized recipes include binding and actual format, use separate module and pipeline identities, and apply the same transformations in the same order. Startup rebuilt1182 learned pipelines with failed0 in3921ms. This includes earlier learned spell pipelines. New specialized recipes still require actual warm-restart validation.

Conservative analysis rejects unknown data flows and some literal-ID collisions. The synthetic shared-type fixture exposed StorageClass literals colliding with descriptor ID3. Type declarations and OpVariable StorageClass literals are now excluded from value-consumer scanning. This repair is included in build4 and is host-validated. Unsupported graphs remain an explicit coverage limitation.

## Actual performance evidence

All evidence below is local to .local/honor-motion-20261112/. Frame counts use CLOCK_MONOTONIC and new image-writer changes. Recording overhead is identified. No screen refresh count substitutes for game frame rate.

| Candidate and scope | New frames | FPS | P95 ms | Max ms | Above50ms | Recording |
| --- | --- | --- | --- | --- | --- | --- |
| Public fix, battle menus/action/result mixed30s | 492 | 16.0263 | 107.5863 | 252.3379 | 233 | yes |
| Public fix, nearby10s battle/result window | 236 | 23.6348 | 94.0059 | 116.1657 | 34 | possible overlap |
| Live GPU route, stationary Shrine observation | 416 | 30.0002 | 33.5599 | 57.9481 | 4 | no |
| Live GPU route, verified vertical motion video | 219 | 28.3281 | 43.4194 | 51.7926 | 1 | yes |
| Live GPU route, four vertical legs | 277 | 29.2072 | 41.2897 | 53.7739 | 6 | no |
| CPU route, same four vertical legs | 244 | 25.9650 | 45.8466 | 57.6271 | 4 | no |
| Live GPU route, repeated four legs | 274 | 29.2981 | 39.2392 | 55.5052 | 5 | no |

public-paired-move-contact.png verifies character and camera displacement on both routes. The stationary observation was initially named user-movement but its screenshots show no displacement, so it is excluded from movement acceptance. public-gpu-actual-movement.mp4 verifies motion separately. Paired legs use the same process, scene, inputs and no recording. They are sequential observations, with thermal and workload drift possible.

CPU route slow windows show created0, sync_wb60 per60frames, why5 accesses, roughly11 submissions and waits per frame. CPU submission was25–52ms and GPU14–33ms across nearby windows. The live GPU route removed this readback synchronization and reached approximately29.2–29.3FPS during verified movement. Stable30FPS is not passed.

Sampling10 selected frames exposed descriptor work about6.8ms, draw3.6ms and snapshots2.2ms in a nearby stationary window. Diagnostic sampling was restored to0 with TracerPid0. These category values describe sampled CPU scope time. They are separate from frame-time acceptance.

## Safe GPU path follow-up

Read-only review identified a freshness defect in the existing fast path. An older template could be reused after a newer conflicting write or after guest CPU overwrite. The next candidate accepts only the newest overlapping pending writeback, requires complete coverage and an exact template row, and removes the recent-batch fallback. Clean guest arguments use the existing synchronized CPU decode. why5 synchronization remains intact.

UBO eligibility now checks uniform_range(bytes), matching descriptor consumption. Logging no longer reads unfinished GPU argument buffers on the CPU. Independent allocations and producer/consumer retirement references remain in place.

Production lookup extracted into a host harness passed ASan and UBSan for latest row, partial overwrite, missing proof, missing row and retired source. Source extraction and harness are in indirect-guard-regression.py/.cpp. Actual-device regression of these guards is pending build4.

## Validation and reproduction

The updated tests/device/reflection_uint_fetch.spvasm is self-authored. Its companion C++ test covers direct and Function consumers, a shared floating descriptor, arbitrary binding77, missing binding and malformed stream. Compile with clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined. Assemble and validate with spirv-as/spirv-val --target-env vulkan1.1. public-regression-validation-fixed.log records exit0. An initial fixture run exposed the literal-ID analysis defect and failed its positive assertion before that defect was corrected.

Actual device97ae/63a5/21ee inputs, arbitrary binding77, signed/unsigned consumers, new type declarations, sequential bindings and negative2D/3D/float/alias/conflicting-store/malformed cases were tested with sanitizers and spirv-val. Raw logs and exact local inputs are in semantic-tests/. Identity parsing, warm-record roundtrip and backward archive lookup were also checked. No full test-suite pass or independent device QA is claimed.

Build4 command is FFRS_APK_NAME=FFRS-Integer-Buffer-Resident.apk bash scripts/android/build_game_window_apk.sh. After successful build, install-r only, verify installed SHA and new PID, load ordinary save, persist gfx-indexed-resident1 only with the guarded runtime, then repeat motion, battle and transition tests. Preserve data, input archives and existing caches.

## Diagnosis history

Starting baseline63d3652, APK02bc980c and PID17545 produced verified movement29.57FPS. deaa9b3 narrowed the old broad binding64 rule to97ae. User initially saw a clear view, then corrected that moving and stationary blocks remained. PID20700 capture frame47807 first develops rectangles at group97, FS63a540, native R16_UINT. Commit9d0e9c4 added that exact variant and opt-in CPU timing. Its APK a3f3f9d installed as PID31827, then user encountered blocks in Shrine FS21ee. Capture frame43758 group146 confirmed the same float-Buffer/UINT mismatch. These successive scene variants motivated the format-driven structural implementation.

Earlier first-use fire action produced five new graphics pipeline creations totaling1210.3ms, plus compute-module rewrites262.84ms. Other slow windows created zero new pipelines. Prewarming alone does not account for every low-FPS interval. Earlier video labels fire-and-counter and repeat-fire include dialogue/tutorial/cinematic work, so they do not establish repeated spell-effect acceptance. Castle movement video28.25FPS is valid. Earlier stationary or NPC-dialogue movement attempts are excluded.

## Primary references

Khronos Vulkan-Docs98ab10166b5cb93c17acabdd4e911d10cf853a3a documents numeric format matching and bit-preserving operations. https://github.com/KhronosGroup/Vulkan-Docs/blob/98ab10166b5cb93c17acabdd4e911d10cf853a3a/chapters/textures.adoc#L469 and https://github.com/KhronosGroup/Vulkan-Docs/blob/98ab10166b5cb93c17acabdd4e911d10cf853a3a/appendices/spirvenv.adoc#L3718

nxnative b149a1e49e6c26dce296a91d3f06324e88a3ff07 is MIT with vendored MIT Ryujinx components. Its inspected backend targets Metal. Resource-derived types and versioned shader-cache recipes informed this design. https://github.com/seedds/nxnative/blob/b149a1e49e6c26dce296a91d3f06324e88a3ff07/dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader/CodeGen/Spirv/Declarations.cs#L158

d2r-mobile209dcd667fe937d23a153c88fd8fff97bdadc242 is MIT and demonstrates structural Adreno subgroup rewriting with explicit device coverage. It provides no acceptance evidence for this game. https://github.com/d2r-mobile/d2r-mobile/blob/209dcd667fe937d23a153c88fd8fff97bdadc242/docs/android-notes/2026-10-27-adreno-750-inline.md

Bluetooth/audio, independent device QA, cold/warm spell repetition, guarded GPU movement, cache restart and scene-transition acceptance remain open. User authorization covers gameplay and necessary preserving-data installs. Continue autonomously.

## Tutorial and input checkpoint

Help page1of2 exposes only L/R paging. R1 advances to page2, which exposes BClose. Page2 then B, Help-list B, System-menu B returns to actual field. X then opens actual main menu. Source GameActivity buttonBit/TouchPad matches nativehid_bridge, which latches presses. Verified normal gamepad input command is adb -s A42BBB5109000333 shell input gamepad keyevent --duration 160 KEYCODE_BUTTON_X, with the appropriate button name. Menu transitions are verified with screenshots. Opaque OEM logcat output did not establish input state. No input-code patch was made.

To advance the authorized regression route, root used normal Equipment/Ability UI to equip Rain with the first available Vision, 特伦. Current preexisting party is level86, as shown by actual main menu and existing autosave metadata. No cheat or save-byte editing occurred. After leaving toward the south, partner dialogue changed to 看来你准备好了, confirming tutorial progression. Current PID31064 is still alive. The5second traverse is partly dialogue and is excluded from pure motion acceptance.


## Monotonic video alignment and field progression

Current device screenrecord supports --show-frame-time --monotonic-time. AOSP implementation draws the actual Surface frame CLOCK_MONOTONIC timestamp, so video overlay can align directly with native frame CSV. Source consulted https://android.googlesource.com/platform/frameworks/av/+/refs/heads/main/cmds/screenrecord/Overlay.cpp and screenrecord.cpp. Device command succeeded exit0, no app runtime changes.

New cpu-cache-south-full-stick sample uses2s record prepad, DOWN/MOVE to full south,4s hold, UP in finally. Raw input interval1443288205591288..1443292301871755 contains122distinct writer intervals,4066.541ms,30.0009FPS, P9533.577ms, max33.733ms. Root contact sheet shows continuous field/world displacement with no dialogue. Independent media/data review is pending, and this light corridor does not establish heavy Shrine or battle acceptance. Recording overhead applies.

Next cpu-cache-south-exit video records cave traversal and black transition to inn, retained as mixed traversal/load. North inn door was inspected and leads back into cave. Normal return followed by east/south movement exits the inn into actual Mitra town. Current town screenshot cpu-cache-inn-south.png. No save-byte changes or source edits in this progression.


Independent read-only reviewer combat_perf_audit decoded and reviewed all364 encoded frames of cpu-cache-south-full-stick.mp4 and strict monotonic105480 CSV rows. Confirmed sustained south displacement approximately1.85..6.03s, no wall/dialogue/menu/load, release then stationary. Because original window starts near visible-motion onset and CPU-present/Surface may differ a few frames, reviewer also used original bounds contracted150ms each end. Conservative1443288355591288..1443292151871755 window has113new writer intervals over3766.162342ms,30.004017FPS, P9533.577187ms, max33.727448ms,0above50/100ms. All selected presents successful, counters monotonic, frame continuity and CSVtail coverage verified. This supports recorded short corridor moving30FPS only. Independent device-executed QA remainsNOT_RUN.


## World-map indirect producer coverage and structural follow-up

Actual worldmap traversal reached from Mitra inn/town. Existing GPU route still fell back once perframe with why5 and60sync_wb per60frames. Nearby counters20.81/20.98FPS, submitCPU36ms andGPU28.7ms. Mode3 bounded fallback logging shows latest64KiB pendingwriter hasmetadata0 and firstdepth-only VS a455.../fsnone. Current logging cs is lastboundcompute, so9bee cannot be called itsproducer.

One complete actualframe captured in world-probe-31064-capture.txt. Group6 CS5e165 B29 writesmatching64KiB range, while later groups7..29 storagewrites and priorVS groups31..32 are disjoint/read-only. Devicearchive slices were fetched only for neededentries, SHA/index world-device-shader-slices.json.5e165 devicebundle67628B SHAabc0e0da6b21af9318d110c59a5d36edfa234d9001d77bb6a6cfe3ad7e0ee5f6 matcheslocal original. Themodule only atomically adds instanceCount atWorkGroupID.z*5+1 and also writes compactinstances throughSSBO B23.

New structural helper recognizes supported immutable target-use graph and affine address without modulekey or binding whitelist. Final optimized compute modules cache{binding,axis}. Strict graph, alias/escape, additionalwrite and malformednegatives covered. Raw and officialinlineddeviceSPV, warp32/ldc/temps combinations, arbitrarybinding/axis pass ASanUBSan. Root reran exact tests.

Runtime candidate usesactualsingleR32_UINTstorage descriptor andCPUclean boundedguest snapshot, actualdispatchaxisrowcount, signed-range index metadata andzero basevertex/baseinstance. Other writable descriptors are independently recorded before existing alias merge/dedup and must be disjoint. Latest pendingwriteback authority and ownedcopy/barriers/retire retained. Depthonly readiness now permits absentFS withactualdepthattachment, existinggraphicsstagevalidation stillruns.

Independent source reviewer found twoP1s in initialintegration, both corrected before build. Merged d.written alone had hidden rawdescriptor aliases, and readback_buffer may deferupload toGPU so mappedCPUcontents were insufficientlyfresh. Rawrange records and EngineRead+clean guest input addressboth. Actual productionmetadatafunction extraction ASanUBSan tests include singlemergedwrite plus rawfull/partialalias negatives,unknown/overflow,nonaliaspositive, invalidargs/grid andlegacytemplate cases. Source reviewer rechecked actualdiff and exactproductionfunction. Build/device acceptance pending.

Normalgame encountered twoLv4 forestrodents with preexistingLv86party. Lasswell清霜 reducedMP489to484 and defeatedA. Rain消失突袭MP421to343 executedagainstB, visiblefirevortex andfullbreak, thenresonancehelppage. Video cpu-cache-fire-assault-action includesactualaction followedby help and cannotbeaggregatecombatFPS. Firstfire actionvisiblystalled forlongintervals, retainedforcausaldiagnosis andwarmrepeat. RootclosedonepagehelpwithB andbattle resultappeared. No completedcombat30PASS.

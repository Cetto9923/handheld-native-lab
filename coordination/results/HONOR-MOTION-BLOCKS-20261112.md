# Honor Pad rendering and movement regression

## Current checkpoint

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

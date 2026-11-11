# Honor Pad movement blocks and combat stutter, 2026-11-12

## Current result

Unresolved. User explicitly corrected the earlier limited visual clearance. Moving blocks around the character and stationary rectangular ground shading remain. Preserve the live scene on A42BBB5109000333, ROD2-W69, SM8635. PID20700. Latest user instruction prohibits directions, story progression, restart and scene transition while collecting this evidence. Other connected device was never operated.

## Baseline and change

Source worktree /Users/yuyan9923/GitHub/ff-resonance-honor-gtpro, task/T-honor-shrine-20261111, clean starting63d3652. Concurrent stability worktree preserved. Original installed version1.0.8 APK backed up under .local/honor-motion-20261112/baseline-1.0.8.apk, SHA25602bc980ce8cf70c220bdb37dffc21f888fb4aba6517c4f0420ba813097c1ec50.

Commit deaa9b3 restores exact97ae64dbc5e7ff26 shader scope for the historical B64 integer-fetch repair. The previous generic rule touched14 additional fragment shader keys, including2D/3D floating-point images. This is a verified scope defect, but its contribution to the remaining visible blocks is unproven. Build exit0, install-r exit0, APK .local/android-apk/FFRS-Motion-Scope.apk SHA25636739797a22d1b147e7b722987694145a6e840a694b77c61822861a988f20fad, internal version remains1.0.8. No resolution, speed, audio configuration, data or shader archive modifications.

## Evidence

Raw artifacts reside in .local/honor-motion-20261112/. user-stopped-blocks.png is the original3000x1920 stationary screenshot. feet-crop.png is a lossless diagnostic crop x1550,y1150,width1000,height500. Ground below/right of the character shows rectangular light/dark patches. No claim of a causal GPU pass from screenshots. Current scene and process retained.

Movement baseline420 new-writer frames29.5686FPS, P9538.9449ms, max66.9657ms,8 frames above50ms. Candidate first recorded movement420frames29.0781FPS, P9542.0659ms, max88.3932ms. Later unrecorded movement300frames23.4651FPS, P9569.0478ms, max818.8528ms. These are sequential traversal windows with changed positions and possible encounter/loading work, not exact A/B or cache-cold acceptance. The initial capture's small drags did not produce useful movement and is excluded.

Actual combat/action capture battle-effect.mp4 has420frames22.3429FPS, P9581.0691ms, max230.3716ms,129 above50ms. Recording overhead is included. Current engine has new-pipeline creation127ms in one historical window, plus repeated slow windows with created0. Synchronization and GPU work need further attribution. Appending engine.log includes previous processes and must be segmented for future comparisons. Both artifact removal and stable30FPS remain NOT_PASSED.

Normal game input progressed through two cavalry combat, spell selection/action, reinforcement dialogue and map return. Then advanced north to the map boundary. User corrected the route to the right, a short rightward move was observed. Further story progression is suspended under the latest instruction. Game stats evolved through ordinary gameplay. No cheats or save editing. Initial external save pull had a permissions error, GameSaveData0 was separately backed up through run-as. No uninstall/data clear.

## Static diagnosis

Actual captured sh1871 input was transformed with the production helper under ASan/UBSan and validated with spirv-val Vulkan1.1, exit0. Legacy reflection test fails its fetch/cast assertion before this change because its synthetic fixture predates the new helper shape. The first validation attempt mistakenly used a historical synthetic file named public-reflection-original, which fails SPIR-V validation with the current helper. The actual captured shader was subsequently tested successfully. No full test-suite pass.

Two other lighting candidates63a540158580d1d5 and21ee00f1479f5931 use Buffer B64 and two fetches. Exact device archive slices were read from /sdcard/FFRS/shaders.hnla, SHA and offsets in device-shader-slices.json. Their existing pure-integer helper transformations pass ASan/UBSan and spirv-val Vulkan1.1. Runtime log shows63a5 used in this session with rewrites0 under the exact97ae gate. This suggests a narrowly verified variant repair may be needed, but descriptor-format equivalence and causal rendering still require evidence. No device archive/override modifications were made.

## Recovery and next steps

Keep PID20700 and current character position. Use only read-only log/shader/screenshot work until the scene-preservation restriction is lifted or a live capture route is established. Existing probe_init arms only when its marker exists at process initialization. The current process did not arm it, so creating a marker now does not enable the capture. Gfxreconstruct layer capture also requires a new Vulkan instance/process. No debugger injection attempted. When allowed, preserve the scene through a verified game save/checkpoint, enable a bounded capture and find the first wrong pass at the character feet. Validate lighting variants and GPU indexed-resident configuration independently. Existing gfx-indexed-resident0 and why5 GPU synchronization remain intact. No speculative bypass of synchronization.

Independent QA, full Bluetooth audio acceptance, game-mode confirmation and whole-game acceptance are NOT_RUN. Both core user goals remain open. No push.

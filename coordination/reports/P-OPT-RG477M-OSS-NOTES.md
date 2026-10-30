# P-OPT-RG477M-30hz-game-mode — public-source notes

2026-10-29 Asia/Shanghai. Read before any new runtime change. No upstream code copied in this intake.

- Android Frame Pacing: https://developer.android.com/games/sdk/frame-pacing/ — synchronization/queue stuffing can produce uneven presentation; target=30 alone does not establish 30 real frames/s. Investigate measured wait/queue time, preserve game clocks. Documentation, no source revision imported.
- Android AAudio: https://developer.android.com/ndk/guides/audio/aaudio/aaudio — burst/capacity and disconnected stream recreation; do not thin Bluetooth buffers. Documentation, no source revision imported.
- AOSP immutable header: https://android.googlesource.com/platform/frameworks/av/+/a22302ec9a1fd4cbeb06c8e3a9d36a54805d8f21/media/libaaudio/include/aaudio/AAudio.h — AAUDIO_PERFORMANCE_MODE_NONE=10, POWER_SAVING=11, LOW_LATENCY=12. Local Android NDK header independently agrees. Standing acceptance's performance=0 / performance=10 LOW_LATENCY wording conflicts with actual API enum. Preserve entire checklist; report conflict, never falsify logs or change NONE to zero.
- Google Oboe disconnect note: https://chromium.googlesource.com/external/github.com/google/oboe/+/refs/heads/1.4-stable/docs/notes/disconnect.md — route changes can change burst and latency; read available (branch reference, not immutable pin); no code imported.
- nxnative: https://github.com/seedds/nxnative — public repository available, NVN→Metal Apple scope, no evidence of FFRS Android compatibility. No source imported, revision/license not established in this intake.
- d2r-mobile: https://github.com/d2r-mobile/d2r-mobile — available public repository. Prior checkpoint references a8782caa28d7b49d1b341e34de488d3168acb86c and MIT fixture; not revalidated here. No transplant: observed pool 0xe2 is CPU_UNCACHED and does not qualify for CPU_NO_ACCESS skip. Do not enable unsafe deferred clear/writeback skipping.

Current diagnosis: 30 limiter already active, mailbox already selected; recent 60-present aggregate windows are only 8–10 FPS while CG is on screen. These are not movement acceptance or frame percentiles. No evidence yet that another limiter/present-mode change solves this. Need exact installed build/source identity, per-frame traces and synchronized scene evidence before editing synchronization.

2026-10-29 16:16 Vulkan primary reference read: https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html ; FIFO=2, MAILBOX=1; app marker encoding differs (marker0=FIFO, marker2=MAILBOX). FIFO queues presents; mailbox replaces pending entry. Documentation only, no source copied, no immutable code revision imported. Isolated uses FIFO, main mailbox; APK native hashes also differ, so cross-package figures cannot establish causal improvement.

16:33 app-display candidate: existing Android Frame Pacing source above motivates matching content pacing and display cadence. Primary API reference URL https://developer.android.com/reference/android/view/Surface#setFrameRate(float,int) ; optimization guide fetch https://developer.android.com/media/optimize/performance/frame-rate returned only partial navigation HTML due timeout; do not treat that partial fetch as verified article. No external code imported. Restore this repository's prior supported-mode selection selectively for target>30/unthrottled, preserving game-mode CPU policy. Mathematical cadence 120/40=3 versus 60/40=1.5 is a hypothesis, not measured fix. Existing self-source snapshot resolution-source GameActivity at13:41 selected120, latest removed it15:21; latest CG115s25.53 presents/s, thermal/battery drift confounds comparison to old31. No causal performance claim.

## 2026-10-29 operation-mode mismatch before change

Primary upstream local clone verified at immutable `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3`, MIT, `dm/host/hle_oe.c`: startup frontend selects operation mode, distinguishes handheld0/console1 from host clock governor. URL https://github.com/seedds/nxnative/blob/7a029f66d92c951ae6a86e38bb34eeb0cb7527e3/dm/host/hle_oe.c . Web open cache-miss; actual local Git source and LICENSE read, no external code copied. Scope/reference only, no FFRS Android compatibility inference.

Our old isolated log reports mode0 and1280x720 presents; current native reports mode1 and later1920x1080 presents scaled to720. Current HLE defaults1 and reads main-package marker even when installed as isolated. Repair at Android startup: derive own directory from actual transcript path; set existing FFRS_OPERATION_MODE environment to own marker0/1 or handheld0 default, before guest startup. This preserves clocks, original files, fixed720 policy, and explicit docked override. Not adaptive resolution, not changing Anbernic system game/extreme mode. FPS benefit remains unverified until same-scene test.

2026-10-29 720P battle diagnosis: Khronos official WaitIdle/Fences sample https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html read in full. Guidance: avoid unnecessary global waits, retain real dependencies; source revision not imported (documentation only). Current runtime already completion_wait=1/legacy_waitall=0, so this is not evidence that flipping those flags fixes it. Battle aggregate sync_wb~1/frame, access_by[3] storage descriptor CPU fallback; pending_view rejects partial coverage and resolve_pending drains pending writebacks. No waits deleted or CPU_NO_ACCESS assumptions introduced. Need live per-range producer evidence before changing it.

Shield investigation: https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html official fulltext read2026-10-29. Dependencies between renderpasses cannot be assumed; excessive barriers can stall, insufficient barriers corrupt output. Documentation-only/no code imported/no immutable code revision applicable. Same-APK attachment-defer-off diagnostic A/B planned; no causal claim or performance PASS.

Semitransparent body shield explicitlyconfirmed, separate from purple ring. Official Depth guide https://docs.vulkan.org/guide/latest/depth.html read2026-10-29; depth sampling/layout/access masks and depthtest/write state matter for visibility. Documentation only, no code revision imported. Use one-frame opt-in render trace to inspect actual states before anyblend/depth workaround; no causal assertion.

2026-10-30: Android StrictMode API https://developer.android.com/reference/android/os/StrictMode consulted for synchronousdisk-I/O performance investigation; no code copied/revisionnotapplicable. SameAPK probe-file A/B did not show improvement; hypothesis unproven. Actual app-vs-runas Unixgroups/marker0660 mismatch verified via procstatus/id and runtimebefore-after logs; config-onlychmod0644 to four knownmarker values fixed reads, not a claimed FPSfix.

Vulkan memoryflag officialref https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryPropertyFlagBits.html read2026-10-30: cachedtypes intendedhostreads,noncoherentinvalidate required. Existingcode Buffer.invalidate retained. Samea1APK cachedreadback markerpermissiontrial reducedCPUcopy butworsenedoverall/GPU,rolledback. Documentation-only,nocodecopied;notproofactualcausewithoutfrequencytrace.

Independent readbackbenchmark uses official https://docs.vulkan.org/guide/latest/synchronization_examples.html CPUreadback section: transfer visibilitybarriers,fencewait,noncoherentinvalidate retained. No externalcodecopied/docrevisionnotapplicable. AllbytescheckedonRG477M;microbenchcannotprovegameFPS orshaderbufferperformance.

## 2026-10-30 title scheduling and wait-path diagnosis
Android https://developer.android.com/topic/performance/tracing/command-line (accessed2026-10-30): sched/freq/gfx tracing locates thread activity/waits; traced values carry profiling overhead. Khronos https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html : global waits can cause CPU/GPU bubbles; retain per-resource/fence completion and lifetime. Documentation only, no code copied; no imported source revision/license change. Existing568 built-in per-target-fence path A/B, not sync bypass.

AAudio independent output diagnostic: https://developer.android.com/ndk/guides/audio/aaudio/aaudio accessed2026-10-30, verify actual stream format/buffer and blocking write returns; keepNONE/100ms timeout and burst*2. Own.local tone.c, no externalcode copied.2ch/6ch alone and withgame usercontinuous; not fullaudioacceptance, no producer/schedulerrootcauseproof.

## Audio completion/notification references 2026-10-30
AOSP Android15 commit1863cb0ea5a9e1340f11a435618ff863e017da29, Apache2.0, files media/libaaudio/src/legacy/AudioStreamTrack.cpp (write443,FramesPerBurst513), AudioStreamLegacy.cpp105(FixedBlockReader), services/audioflinger/Threads.cpp2579(normalnotification), Tracks.cpp1159(startthreshold), media/libaudioclient/AudioTrackShared.cpp931(futexminimum). BaseURL https://android.googlesource.com/platform/frameworks/av/+/1863cb0ea5a9e1340f11a435618ff863e017da29/ ; sourceactuallyreadbyindependentreadonlyreviewer, Android14tagalsofetchedlocalAOSP14-AudioStreamTrack.cpp.txt. Vendoridentityunknown; no codecopied. Libnxv4.12.0 commit7644c9b26099aa2d2145bc72a21ee24190e92085 ISC https://github.com/switchbrew/libnx/blob/7644c9b26099aa2d2145bc72a21ee24190e92085/nx/include/switch/services/audout.h documentscompletion/waitfinish, not fixed5msnotificationpromise. CRI official https://game.criware.jp/manual/native/adx2_en/latest/criatom_trblsht_execution_confirmation_server_processing.html and https://game.criware.jp/manual/unity_plugin_en/latest/contents/atom4u_keys_sonicsync.html : regularaudio/serverthreadexecution matters; no FF-specificfixproof, docsnotcode.

## 2026-10-30 Opt-in paced AudioOut prototype

Owned bounded FIFO + real sample clock separates endpoint completion from AAudio write acceptance. Public basis pinned to AOSP1863cb0 and libnx7644c9 above; no upstream source copied. Full limits and same-APK PCM result: coordination/reports/P-OPT-RG477M-PACED-AUDIO-20261030.md. Default OFF; physical latency/A2DP/full gameplay gates remain open.

## 2026-10-30 Body-shield diagnosis — no rendering fix asserted

Khronos Vulkan-Docs v1.3.296 commit `7353f6ced118815d61852a739b9eae6f0055d77a` (git ls-remote verified): https://github.com/KhronosGroup/Vulkan-Docs/blob/7353f6ced118815d61852a739b9eae6f0055d77a/chapters/framebuffer.adoc ; license Apache-2.0 / CC-BY-4.0 as applicable to code/spec. Full per-target source/destination RGB+alpha factors, operations, write masks and constants matter to transparency. Added default-off probe-only diagnostic logging of original NVN and existing converter Vulkan values; no normal-render behavior change, no upstream code copied. Build92348 exit0; isolated2c60e8bc deviceSHAverified, ownsource manifest .local/rg477m-opt-30hz-game-mode/audio-paced-transport-20261030/fx-probe/.

Public producer case: Unity official issue https://issuetracker.unity3d.com/issues/quest-materials-are-not-rendering-in-player-when-single-pass-instanced-slash-multi-view-is-selected-and-surface-type-is-set-to-transparent documents transparent soft particles requiring depth. Cross-engine investigation lesson only; not proof this game uses that exact shader. Actual same372a depth-aspect1->0 replay67512 exit0 did not confirm restored body shield, restored1. Ryubing docs https://github.com/Ryubing/docs/blob/main/docs/info/faq-%26-troubleshooting.md warns async shaders can produce glitches; actual current baseline logs async0/skips0, so not causal evidence here. No blanket barriers or altered game material/opacity introduced.

## 2026-10-30 Next ClearBuffer investigation (not implemented)

Actual marker3 records OP_CLEAR_BUF/CPUaccesswhy4,128bytes inside43008byte pending result fromanother slot. PERF tuple fourth=reason4; initial reason3 interpretation corrected before any performancecode edit. Public primary fetched through GitHub connector: d2r-mobile/d2r-mobile a8782caa28d7b49d1b341e34de488d3168acb86c [fullrange GPUcopy andpartialfallback](https://github.com/d2r-mobile/d2r-mobile/blob/a8782caa28d7b49d1b341e34de488d3168acb86c/dm/gfx/vulkan/engine/xbg_game.cpp), LICENSE verifiedMIT(c2026jhaizhou). nxnative7a029f66d92c951ae6a86e38bb34eeb0cb7527e3 [mtl_sub_fill](https://github.com/seedds/nxnative/blob/7a029f66d92c951ae6a86e38bb34eeb0cb7527e3/dm/gfx/nvn/nvn_mtl.m) actuallyqueuesGPUblitfill/subcopy andpendingmetadata. MIT perpriorverifiedrepo. No sourcecopied. Candidatecopy-on-writefullpendingrange+partialfill must preserve oldproducerfence data, CPU-visiblewriteback, lifetime/alias/queryguards. Not permitted toskip pool0xe2 asCPUNOACCESS. No newimplementation orcausalperformanceclaim. SameAPKcleanfield19.43Hz vslaterfullrepeat30.0001Hz discrepancyretained; complete data andnextplanin.local/audio-paced-transport-20261030/.


## 2026-10-30 producer-retirement experiment
OfficialKhronos Mali WaitIdle/Fences case readbeforechange: https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html . Documentationonly/noexternalcodecopied. Immutableengine reference https://github.com/d2r-mobile/d2r-mobile/blob/a8782caa28d7b49d1b341e34de488d3168acb86c/dm/gfx/vulkan/engine/xbg_game.cpp , MITverified, existingwait_fence/retire_slotpattern. LocaldefaultOFF gfx-clear-range-wait onlysmallCPUwrite<=64KiB overlappingalready-submittedGPUproducers; retireinqueueorder andretainoriginalwritebacks/queries/events, fallbackforcurrentrecordingwriters/events/reports/imagealiases/ownedcopies. Mode2additionaloriginalfulldrain CPUbytecompare >=120checks0mismatches, NOTFPSPASS; true120s0/1sameAPKfieldcomparison pending. Noaudio/gameclock/shader/qualitychange. COWfullbufferprototypearchived/reversedownhunksonly; event-order/lifetimefastmodeunproved. Eventguardversion0eligiblechecksNOTproveneventcaused: actualdefer_events0, eligibilitydifferenceUNKNOWN. Neveradopted.


## 2026-10-30 C2/6018 transparent draw follow-up
Read primary public reports https://github.com/Ryubing/Issues/issues/476 (Apple NaN texture coordinates, author reproduction/fix, notMali/FFRS evidence) and https://github.com/leegao/bcn_layer/issues/45 (Mali G77 missing geometry/effects, unresolved wrapper hypotheses; notproofBCdecoder). No external code imported/revision notapplicable forissue discussion. Local C2 SPIR-V already contains OpIsNan/OpSelect coordinate guard; do notblindlyreapply. Newprobe-only boundedUBallowlist adds onlyactualtrace VS6018ff2b44b763d7/FSc2ca757a9f517e96; same64KiBbinding/8MiBtotal caps andnostorageCPUread. No shader/material/renderbehavior changes. Drawidentityasbodyshield still unproved; exactphase andownUBrequired.


2026-10-30 temporaryshadercoverage diagnostic: officialRenderDocdoc actuallyreadviaGitHubconnector afterwrongoldfilename404; immutable https://github.com/baldurk/renderdoc/blob/34c3c40787f440da9c2947cd63a41e6c4d1f95b9/docs/how/how_edit_shader.rst (v1.38), MITLICENSEread. Scene-shaderreplacement temporarilyaffectsallactionsusingthemodule; restorationrequired. Referenceonly/noexternalcodecopied. SPIRV-Tools validatorguidance https://github.com/KhronosGroup/SPIRV-Tools/blob/vulkan-sdk-1.3.296.0/README.md read. LocalF62diagnostic changesONLY4finaloutputvalueIDs toopaque red, originalandcandidate spirv-val Vulkan1.3exit0, same48,600bytes/interface/depth/stencil/cull/discard. NOT materialfix/acceptance. Ownisolatedexistingoverridepathmustbeabsentbeforecreate, owncreatedfilearchivedoutsideactivepathinfinally/appstop; originalshaderrestoreverifiedbeforeacceptance. No gamepackage/XGS/rawresource/mainpackagechanges.

## 2026-10-30 F62 raster-state / existing-query observation
Read RenderDoc v1.38 MIT documentation, immutable https://github.com/baldurk/renderdoc/blob/34c3c40787f440da9c2947cd63a41e6c4d1f95b9/docs/how/how_inspect_pixel.rst : pixel history distinguishes failed pipeline tests from shader output and arbitrary writes. Prior how_debug_pixel.rst guessed filename404 not source. Khronos query rule https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html read: availability accompanies result, non-WAIT may be NOT_READY; use real fence and no query reset between recorded use and observation. Revision for spec reference https://github.com/KhronosGroup/Vulkan-Docs/blob/7353f6ced118815d61852a739b9eae6f0055d77a/chapters/queries.adoc (v1.3.296); no upstream code copied. Plan default-off logging raster state; capture ONLY existing occlusion handles before end, read after existing diagnostic GPU fence and BEFORE image readback can record resets. Do not create/reset extra queries or change visibility/counter/game behavior. Batch query is aggregate, not per-draw result.

### Raster isolation diagnostic, not fix
Based on same pinned RenderDoc pixel-history docs and Khronos rasterization https://docs.vulkan.org/spec/latest/chapters/primsrast.html / VkFrontFace official reference; culling and depth are separate possible rejection causes, zero samples alone does not identify either. Add explicit separate opt-in marker read ONLY at probe_start; mode1 disables F62 culling, mode2 depth test, mode3 both, ONLY within captured diagnostic frame and F62. No stencil override, depth writes or shader mutation. Disable fast memo lookup for that diagnostic draw so derived state enters full pipeline key and never contaminates normal memo. This is a counterfactual observation; cannot ship/claim solved by no-depth/no-cull. Clear marker after each trial. No outside code copied.

## 2026-10-30 system audio recording method
Public scrcpy v3.3.1 doc https://github.com/Genymobile/scrcpy/blob/f01231dff8294fe2c99045a4f9a14b233a71bb86/doc/audio.md read (tag peeled commit verified git ls-remote): output maps REMOTE_SUBMIX and disables device playback; playback is separate Android13+ opt-out-capable method. Documentation reference only, no external code imported/changed. Bundled hostclient3.3.1 playback smoke crashed exit-11, no artifact; not game crash. Output method works after user unmuted stream MUSIC volume2; no programmatic volume/profile change. Capture reroutes audio and cannot count as actual A2DP ears, AVsync or clean FPS acceptance.

## 2026-10-30 F62 geometry inputs, bounded diagnostic extension
The actual precise queries were0 even in cull/depth counterfactual; no transform-feedback unsupported warning in tested log. Next extend existing85581ae GPU readback ONLY for observed F62VS: texelbindings18/24/30/36/42/48 and raw storage12. Same<=64KiB perbinding,8MiB combinedprobe budget, originalcurrentcommand-buffer copy, realfence/invalidate/lifetime; no normalrender/materialchange. Storage copy uses exact descriptor offset/range and bounds checks. References remain pinned Khronos synchronization/copy docs in85581ae plus RenderDoc pixel-history34c3... above; no externalcodecopied. Purpose verify actual geometry/transform data and shader identity, not assume F62 is shield.

## 2026-10-30 actual numeric mismatch and particle-collapse source
Actual shader b028... binding18 OpTypeImage floatBuffer but liveVkBufferView format98R32_UINT; F62draw108 query0 confirmed. Vulkan VUID-vkCmdDraw-format-07753 https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdDraw.html explicitlyrequiresnumeric type match. Plan temporary existing shader override, typed uintImageFetch then OpBitcast originalfloatvector toretain originalregisterbits; notformat reinterpret/floatnumeric conversion/no default adoption. SPIRVvalidator original/candidateexit0; patchmanifest private. Separately upstream latest immutable https://github.com/seedds/nxnative/blob/8e0916bf04f6fb8542c52ed01b166a8796158ca0/docs/reference/shaders.md read: known FFRS Niagara particle meshes collapse from removed phi bindless reads;3,197shaders fixed,26reads in14shaders unresolved, not runtimePASS. Oldlocalreference7a029f lacks thatdoc404; no upstream source copied/merged or shader bundle rebuilt. These are separate hypotheses; actual b18selectedindex3/4 and0/1allread0, so cannot assume numeric mismatch already explains zero samples. Must test.

## 2026-10-30 single-key public-source translation comparison
Pinned nxnative8e0916bf04f6fb8542c52ed01b166a8796158ca0 MIT source/license checked. Existing prepare_ffrs_xlate.py private-copy workflow used; reviewed upstream accessor now QueryTextureArrayLengthFromPool1<<20 instead of32, preserve it and insert only existing nonconstantOffset Vulkan fallback. Private SDK10.0.403 obtained from official Microsoft release metadata + official Azure blob; SHA5125893c66b... matches full publishedhash beforeextraction, no global install/profilechanges. uilman/ooz9c67c6e2f892c462689592a28908576fe1a6a511 GPL3+ actualkraken.cppheaderread; private x86decoder only, existingRosetta, never APK/Git; portablememicmp CLIhelper isolated. Existingreleasepack utocSHA stored, originalucasread-only; originalshaderindex reused toextract1group/key, no5919groups rescan. SingleVScontrolb028/contentc55d:1translation/0errors/1XGS,spirv-valVulkan1.1PASS; newXGSSHA67778f21d0a9411889f32c090f3212c15ef7818901dae7298bc6e764d6f7bd7b,18vs17bindings, addedbufferreadslot130/binding66. Source/docmethod notgamecompatibilityPASS. IsolatedtemporaryprimaryXGS overlay only, exactshader/cachebackups+finallyrestore; nooriginalaltgamebundle/main/savewrites.

### Existing guarded-Phi proof adapter, private diagnostic only
Two newly translated effect VS retained runtime texture arrays which XGS reflection explicitly refuses; failures retained, none deployed. LLVM official PHI semantics https://llvm.org/docs/LangRef.html#phi-instruction actually read at7591–7608: value follows executed predecessor. Reuse existing project scripts/graphics/prepare_ffrs_xlate.py HandleUseProof unchanged (bounded mandatory-edge proof, cycles/feasibleundef refuse, no uniform guessing). Adapt three originalstaticmatching calls and audit placement to pinned8e091 new two-sweep pass in privatecopy only. No projectsource compiler edit, Booleanextension, zero fallback or runtimearray removal. Preserve upstream fallback; unsupported bundle remainsFAIL. Need real-input audit + validation before any deployment; not infer full correct from compile.

### 2026-10-30 私有 guarded-Phi 适配审计

来源保持 nxnative `8e0916bf04f6fb8542c52ed01b166a8796158ca0` 的 [BindlessElimination](https://github.com/seedds/nxnative/blob/8e0916bf04f6fb8542c52ed01b166a8796158ca0/dm/third_party/ryujinx/src/Ryujinx.Graphics.Shader/Translation/Optimizations/BindlessElimination.cs)，复用项目既有有界 HandleUseProof。v1 三份缓存输入试译2成功1失败：1300结果引用审计拒绝，未部署。源码复核 TryTableHandle 会在第二次静态转换前合法重写结果并插入 select，v2 将第二次转换的快照移到其紧邻入口，保留第一次及所有坐标/结果/type/flags断言。仅私有工具，护罩已接受候选不依赖此扩展。失败与后续结果保留 retranslate/proof-three-cached/、proof-v2-*。

### 2026-10-30 倒地法师条纹闪烁：先采证据

复用同shader完整60秒视频已见枪击结束后白法师脸/倒地黑法师局部暗条纹。依据 [Khronos诊断局限](https://github.khronos.org/Vulkan-Site/tutorial/latest/AI_Assisted_Vulkan/04_multimodal_ai/04_expectations_limits.html) 不以像素直接判定Z-fighting或正常受击；[Meta dithering原始技术说明](https://developers.meta.com/vr/blog/tech-note-shader-snippets-for-efficient-2d-dithering/) 和 [Epic AA文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine) 仅给出假设。最小诊断扩充现有默认关闭单帧probe：仅VS18a3/FSb5e9已有uniform快照和该FS两张静态atlas binding10/22的≤1MiB完整GPU读回，沿用原有barrier/fence/readback生命周期，不改绑定/采样/绘制/深度/受击时间。要用实机源数据区分atlas本身、UV/材质参数及后处理；未有原因证据前不做渲染修复。

### 2026-10-30 sprite深度预通道的验证线索

原抓帧存在同样24 indices的深度预通道 VS5b2f/FSade50 与 base pass VS18a3/FSb5e9，后者depthFunc3(EQUAL)、写FragDepth，故需验证其深度一致性；目前只是候选原因。依据 [Khronos invariance](https://docs.vulkan.org/spec/latest/appendices/invariance.html) 规则5和 [GLSL invariant条件](https://docs.vulkan.org/glsl/latest/chapters/variables.html)，任意带像素深度偏移的独立shader并不自动保证等值深度。不改比较函数、不加偏移去掩盖；先用原始atlas与完整draw证据判断。两份base pass shader经上游8e091重译与设备原XGS逐字节完全相同，单独升编译器不构成修复。

### 2026-10-30 一目录安装与Surface首启

官方 [AOSP SurfaceView lifecycle](https://source.android.google.cn/docs/core/graphics/arch-sv-glsv?hl=en) 明确surfaceCreated可能在onPause之后，熄屏可能仅onPause无surfaceDestroyed；[SurfaceView API](https://developer.android.com/reference/android/view/SurfaceView)限定Surface有效期。因此先将guest首启约束为resumed+window focus+interactive+valid Surface，surfaceDestroyed明确复位surfaceReady，恢复路径统一重检；不改游戏时间/音频/GPU等待。资料为官方API/说明，非引入上游源码，无额外第三方代码许可证。

数据导入按 [Android SAF官方说明](https://developer.android.com/training/data-storage/shared/documents-files)，计划ACTION_OPEN_DOCUMENT只读流式导入app私有目录，不依赖Android/data目录选择或所有文件权限；[Android Zip Path Traversal](https://developer.android.com/privacy-and-security/risks/zip-path-traversal)要求拒绝越界路径。后续实现需校验manifest/版本/空间/大小/SHA、暂存完整后切换、保留旧版本与存档，不能仅凭扩展名或盲解压。此处记录设计来源，尚未宣称导入或多机型通过。

### 2026-10-30 安卓本机导入实现

再次实际阅读 [Android app-specific files](https://developer.android.com/training/data-storage/app-specific) 和 [SAF](https://developer.android.com/training/data-storage/shared/documents-files)：通过getExternalFilesDirs枚举实际挂载且可写的应用专属卷，按实际剩余空间呈现/切换，不硬编码SD UUID，不请求MANAGE_EXTERNAL_STORAGE；ACTION_OPEN_DOCUMENT只获取用户选中文件的只读流。应用卸载会删除专属目录，UI明确提示升级覆盖安装、先备份存档。官方API说明为依据，无外部源码引入。APK内携带清理后的可信数据索引摘要，原生库摘要由实际安装APK计算；不信任包自身声明。版本根与内部稳定saveRoot分离，保留旧nativeStartGuest ABI；恢复只接受当前APK合同匹配的完整版本。

### 2026-10-30 用户改为解压目录直读：公开成熟实现核对

用户要求优先“解压目录→拷贝到手机/SD→安装APK→自动找路径运行”，ZIP导入改为备选。本轮实际读取Xash3D [README](https://github.com/FWGS/xash3d-fwgs/blob/3daa67eca9a9f9651b37d63babe6dfc8ec6961ac/README.md)及 [AndroidManifest](https://github.com/FWGS/xash3d-fwgs/blob/3daa67eca9a9f9651b37d63babe6dfc8ec6961ac/android/app/src/main/AndroidManifest.xml)、[XashActivity](https://github.com/FWGS/xash3d-fwgs/blob/3daa67eca9a9f9651b37d63babe6dfc8ec6961ac/android/app/src/main/java/su/xash/engine/XashActivity.java)：固定xash目录、可显式basedir、声明MANAGE_EXTERNAL_STORAGE；并非无条件自动读取任意路径。其 [2187](https://github.com/FWGS/xash3d-fwgs/issues/2187)直接讨论避免SAF复制， [2266](https://github.com/FWGS/xash3d-fwgs/issues/2266)外部卷卡顿是用户报告且维护者提出其他原因，不能当成本项目实测结论。只借鉴路径/权限方式，无代码复制；根LICENSE请求404，后续真实许可证路径再核，不以此认定许可证。

EasyRPG [GameBrowserHelper](https://github.com/EasyRPG/Player/blob/096c004def987f3545198304b1b7a8352f284a50/builds/android/app/src/main/java/org/easyrpg/player/game_browser/GameBrowserHelper.java)与同revision COPYING(GPLv3)实际读取：ACTION_OPEN_DOCUMENT_TREE+持久URI授权。ScummVM [Android指南](https://docs.scummvm.org/en/latest/other_platforms/android.html)同样先选游戏目录授权。它们有自己的SAF文件系统；本项目现有native stdio/mmap不能把content URI直接伪装成绝对路径授权。

[Android11存储约束](https://developer.android.com/about/versions/11/privacy/storage)、[all-files官方API](https://developer.android.com/training/data-storage/manage-all-files)实际读取：target>=30的requestLegacyExternalStorage无效；公共目录NDK直读需要相应权限，不能读他包Android/data。当前私有侧载候选优先固定HandheldNativeLab/data的有界查找+一次显式系统文件授权，保留SAF导入为可选路径；不扫描无关全盘/凭据，不改target绕限制。原数据只读，日志/cache/profile与稳定存档放app自有目录。只借鉴交互与API，无上游源代码引入。

### 目录拷贝测试的传输停滞（不冒充游戏卡顿）

源目录73513文件(含2metadata)，tar发送子进程已Z，设备tar638仍等待且只见60文件；没有认为已完成。改传输方式前查读AOSP [shell_service.cpp](https://android.googlesource.com/platform/system/core/+/46c313b/adb/shell_service.cpp) raw/PTY协议说明与 [ADB手册](https://android.googlesource.com/platform/packages/modules/adb/+/HEAD/docs/user/adb.1.md)，主机实际adb37.0.1help核对push --sync。尚不能断言是PTY/EOF哪种原因；仅终止本任务tar638，保留半目录和失败日志，再用官方sync协议续传本任务数据，应用保持停止，最终由APK初次全SHA验证。此处ADB仅为测试布置，不成为用户安装条件。

### 2026-10-30 小文件交付调查（尚未修改渲染）

本项目 `xbg_game.cpp` 的 shader_open 通过 `<key>.xgs` fopen，并支持 FFRS_SHADER_ALT 目录回退；当前73500余个分散文件在设备续传中非常慢。行业线索：[Android官方资产打包说明](https://developer.android.com/games/optimize/game-size)和 [ScarletNX shader cache 文档](https://github.com/scarletnx/ScarletNX/blob/main/docs/USAGE.md)描述合并资产/着色器包按需读取；这是研究方向，不证明我们的格式、性能或游戏兼容。尚未引入代码、修改运行时或决定迁移，正式方案须核对上游不可变revision/许可证、错误处理、原XGS字节一致性和RG音画帧率回归。现有续传保持单写。

补充：`open_shader_bundle` 有两个实际调用点，主加载按FILE长度完整读取，预热路径也依赖FILE接口；因此包方案不能只改目录路径，须同时验证两个调用点、索引边界及字节等价。公开仓库 ScarletNX main 查询到 revision 8efbf9c44dd8f1b4368ced2b87179bb09a124795，但对应固定revision文档/许可证在本次浏览返回 cache miss；许可证和对应实现未核实，不能据此复制代码或当成已验证上游。当前仅记录研究，不修改渲染。

后续通过 GitHub API 和固定revision raw 实际确认 ScarletNX 根 LICENSE 为 GPL-3.0（https://github.com/scarletnx/ScarletNX/blob/8efbf9c44dd8f1b4368ced2b87179bb09a124795/LICENSE）。此处仅研究公开设计，不复制其代码或引入依赖；固定revision USAGE 内容仍待直接核实，不能把搜索摘要当成本项目运行证据。

固定revision USAGE 已通过 raw 实际读取：https://raw.githubusercontent.com/scarletnx/ScarletNX/8efbf9c44dd8f1b4368ced2b87179bb09a124795/docs/USAGE.md；文档确有预先准备的 vkshipped 单文件和首次绘制按需读取描述。该项目及格式与本项目不同，仍不能推断本游戏兼容或速度改善。

### 2026-10-30 目录直读候选战斗内存崩溃调查资料
- Android 官方内存管理 https://developer.android.com/topic/performance/memory-overview ：内存压力下分配失败可导致 native SIGABRT；需结合现场证据，不能据此断言单一根因。
- Android 官方 native 内存分析 https://developer.android.com/topic/performance/memory/guide/native-memory ：heapprofd 可采样原生分配与释放；需 profileable 或 debuggable 构建。后续仅对隔离候选采集并对照冷暖缓存与长时间驻留，先量测再改；不能由此推断 shader 或安装代码必然泄漏。
- Perfetto 官方原生堆采样与周期快照：https://perfetto.dev/docs/data-sources/native-heap-profiler ，按包名可覆盖启动时 malloc/new；采样不覆盖所有 mmap/GPU 分配，采样运行不能当作性能验收。004 隔离包以 65536 字节采样、每 60 秒 dump 做独立诊断。
- Perfetto TracePacket 官方枚举 https://perfetto.dev/docs/reference/trace-packet-proto#profilepacketprocessheapsamplesclienterror 定义 client_error=2 为 INVALID_STACK_BOUNDS；本轮 004 采样报此错误且仅一份早期快照，不可据此定位 GB 级增长。Python trace processor 用官方 https://perfetto.dev/docs/analysis/trace-processor-python ，本地 venv perfetto 0.59.1 与清单固定 v59.1 mac-arm64 预构建 SHA 校验后运行，未更改全局环境。
- Linux 内核 `/proc/PID/smaps` 描述 https://docs.kernel.org/filesystems/proc.html 与 Android 官方工具概览 https://developer.android.com/topic/performance/memory/guide/tools-overview ：按映射汇总 PSS、RSS、swap 可作为堆采样失效时的补充；映射名称不提供分配栈，GPU/驱动内存未必完整包含，读取中映射变化也可能造成偏差。工具只读，不将结果等同泄漏证明。
- Vulkan 官方描述符池生命周期及同步要求：https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html ；官方性能样例讨论按帧回收和缓存：https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html 。本次仅增加默认关闭的资源计数，不重置/释放池，也不把对象数量当字节数或内存根因。
- 命令池对照依据：Vulkan规范 https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html 定义reset复用资源和RELEASE_RESOURCES归还系统资源；官方性能样例 https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html 区分pool reset与buffer reset及性能权衡。只据此设计既有gfx-no-slot-pools隔离对照，不能推断当前Mali驱动存在某缺陷。
- Android NDK 官方 app-local wrap.sh：https://developer.android.com/ndk/guides/wrap-script ，仅debuggable APK且native lib目录打包；Bionic固定revision malloc_debug文档：https://android.googlesource.com/platform/bionic/+/94657009839a4918337ac069652085906e3ee322/libc/malloc_debug/README.md ，支持backtrace_enable_on_signal=16和dump_prefix。新诊断构建不改全局setprop；默认采样关闭，短窗口记录的开销不用于性能或音频验收。

## 2026-10-31 公开案例复核：采用目录优先、首次授权、原位读取

用户最新指令：数据预先解压，用户拷贝到手机或储存卡，安装APK后自动检索加载。此次为资料与现有实现核对，无设备操作、无APK重建、无渲染/音频修改。

证据：
- ScummVM Android官方文档 https://docs.scummvm.org/en/latest/other_platforms/android.html 明确支持microSD/USB拷贝游戏文件，然后通过系统选择目录并授权。借鉴的是文件交付与授权流程，不是其运行引擎。
- 固定源码 revision 38af1883a2eb7a622c31bfc3708888bbcc47db48（v2.9.0解引用commit，git ls-remote实际返回）：https://github.com/scummvm/scummvm/blob/38af1883a2eb7a622c31bfc3708888bbcc47db48/backends/platform/android/org/scummvm/scummvm/SAFFSTree.java 。实际读取确认持久URI授权与ContentResolver桥接，不是把content URI转为任意文件路径。根COPYING为GPLv3文本：https://github.com/scummvm/scummvm/blob/38af1883a2eb7a622c31bfc3708888bbcc47db48/COPYING 。仅参考设计，无复制源码或新增依赖。
- Android官方全文件访问 https://developer.android.com/training/data-storage/manage-all-files ：Android11+需要用户显式允许；可访问共享存储及SD/OTG根，不能借此访问其他app私有Android/data；上架Play另有严格用途限制。当前侧载目录直读可以使用该明确授权，不保证任意分发渠道均适用。
- Android官方SAF https://developer.android.com/training/data-storage/shared/documents-files ：选择目录获得URI访问，持久化授权可供后续使用；Android11限制选储存根/Download根及Android/data、obb，应该选HandheldNativeLab子目录。SAF直读需适配现有native fopen/mmap路径，现有ZIP导入备选不等于已经实现SAF目录原位读取。

落实口径：
1. 交付 HandheldNativeLab/，内含APK、data/、简短说明和版本清单。用户整目录拷贝到手机或SD卡根，或Games/、Download/下，安装APK后打开。
2. 首次先有界检查这些约定目录，不递归扫全盘、不假设知道APK安装前所在目录、不硬编码SD UUID。找到有效唯一匹配数据后校验并原位读，不再复制整份大数据。
3. 无权限时明确引导首次授权；拒绝可以返回/重试，不循环弹窗、不黑屏。未找到、缺文件、错误版本、SD移除均显示具体恢复办法。多个匹配目录的选择规则需验收，不能任意选过期目录。
4. 首次完整SHA显示进度；后续快速检查有明确边界，数据更新后重新验证。存档、缓存和日志放app自己的独立目录，覆盖升级保留存档；卸载会清除app私有存档，应先提供备份路径。
5. 当前DataBundleInstaller已有手机/SD卷枚举+三个约定目录+合同校验原位读取。此轮不另造一套安装器。SAF目录原位访问仍是后续兼容方案，不能承诺目前各种机型均可零授权启动。
6. 验收补充：手机/SD、根/Games/Download位置；新装无权限/同意/拒绝/重开；目录缺失/不完整/不匹配/移除；覆盖升级存档；首检进度和暖启动。只有实际测过的设备进入支持矩阵。安装流程通过不替代仍开放的30Hz、音频、视觉及内存增长门。

### 2026-10-31 CPU纹理注册描述的所有权修复
公开资料复核：https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html （Khronos官方，关于移动端高频描述符分配/更新的CPU成本及复用；未复制代码，不能将其示例FPS套到本游戏）。本修复不复用尚在使用的VkDescriptorSet、不改GPU资源回收或图像内容，而是消除本项目桥接CPU值的多余永久分配。
直接代码依据：本地d8733d8的xbg_register_texture与register_texture_record在同步路径把texture/view按值放入RegisteredTexture，异步路径lambda也按值捕获；guest_boot007显示约63秒initialized/finalized各+1229748、live1065不变，Scudo PSS+190908KiB。旧bridge_texture永久分配被热注册路径调用，具有明确可减的CPU副本持有。新增caller-owned texture_copy仅在engine注册路径使用；命令录制原bridge_texture继续返回永久不可变指针。最新未逃逸描述可覆盖复用，逃逸后永不覆盖；retirement先在锁内复制再回调，避免复用期间读取可变存储。CPU合同与旧路径反例、ASan/UBSan日志在.local/installer-20261030/texture-snapshot-fix/，不替代设备音画或稳定30Hz验收。

### 2026-10-31 命令视图按值快照
沿用已实际复核的Khronos CPU描述符管理资料 https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html ，仅参考减少热路径重复分配的原则，未复制上游代码。直接问题证据为本地010有效堆栈：nvn_bridge_view→copy_view→SetRenderTargets，16B×12535存活分配；旧7c50987 copy_view随后立即memcpy16B进命令，不保留指针。自有修复e723f84改为直接从family取得值再memcpy，null/未知view仍has=0；没有改变纹理/GPU对象回收。真实command recorder CPU合同10000代逐16B比对+null/未知view+零永久getter调用，ASan/UBSan退出0；旧代码同一永久getter断言失败。另加默认关闭profile中的memo_entries计数，用于检验缓存增长是否最终趋稳，不降低既有pin阈值或更改cache策略。

### 2026-10-31 大文件SHA进度
Android AOSP固定revision299fe6f5d6fc6f1af7c3411dcf4e5efdf7217368（android-14.0.0_r1解引用commit，ls-remote实际核对）：https://github.com/aosp-mirror/platform_frameworks_base/blob/299fe6f5d6fc6f1af7c3411dcf4e5efdf7217368/core/java/android/os/FileUtils.java ，文件头Apache-2.0；固定revision raw已实际读取。该实现周期上报真实已处理字节并检查取消，而非等整个文件完成。本项目仅参考进度原则，不复制其代码或调用hidden API。现场011校验到59.9%后等待大shader归档时，旧verifyAt仅在整文件结束回调；新fileSha在读取中每512KiB上报真实byte（UI仍200ms节流），保留每128KiB取消点及完整SHA比较，长度变化提前拒绝。实际Java25项合同含文件内进度/范围单调性/中途取消只读，旧代码同测试在无文件内进度断言失败；不以主机fixture替代Android界面复验。

## 目录交付方案再次核对（2026-10-31）

本轮实际重新读取ScummVM Android文档、固定38af1883a2eb7a622c31bfc3708888bbcc47db48的SAFFSTree.java和Android官方manage-all-files/documents-files页面，URL及许可证沿用前述完整来源。结论保持目录优先：整目录拷贝→安装APK→首次必要授权→有界自动发现→首次完整校验→原位读取。不能承诺无授权扫描任意路径；SAF树授权不能自动变成native普通文件路径，尚未实现该原位桥接。不新增安装器或音画改动。

当前源码900ab5d实读：DataBundleInstaller.directoryCandidates枚举实际挂载卷，在根/Games/Download下找HandheldNativeLab/data；scanDirectory跳过校验失败项，首次完整SHA、同路径同合同后续长度/索引检查。多份有效匹配按候选顺序取首份，尚无用户选择器，作为开放验收项，不能描述为已解决多目录冲突。GameActivity保留明确setup/授权/重试入口。当前013系统安装与SD直读证据已在RESULT记录；其他手机未测试。

### 2026-10-31 present结果必须与尝试分开
官方Vulkan vkQueuePresentKHR参考 https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html 实读：SUCCESS/SUBOPTIMAL为成功，OUT_OF_DATE/SURFACE_LOST等不能当成功展示；调用成功也只证明排队，非显示屏扫描输出。固定Vulkan-Docs v1.3.280解引用commit69ba4fefbafa045d0fd1b07060b768b3b1c115cc（本轮git ls-remote实核）：https://github.com/KhronosGroup/Vulkan-Docs/blob/69ba4fefbafa045d0fd1b07060b768b3b1c115cc/chapters/VK_KHR_surface/wsi.adoc 。仅引用规范、不复制实现。
现场013 HOME五秒→同PID恢复截图成功，但CSV在后台仍增长；源码backend无窗口/重建/错误分支return1，engine又将所有结果return0，trace不记录结果。拟保留非致命跳过策略和尝试计数，增加明确成功/跳过/失败状态至CSV；旧CSV只能作无结果状态的尝试时序，不能冒称逐帧确认成功。修复不改变限帧、绘制、音频、游戏时间或后台策略。
另查官方管线管理 https://docs.vulkan.org/samples/latest/samples/performance/pipeline_cache/README.html 与 https://docs.vulkan.org/guide/latest/pipeline_cache.html ：首次创建/持久缓存是卡顿排查方向。013首暴雷尝试窗口14.58s/24.96fps/max1521.777ms，附近created6/1160.6ms及created7/631.7ms，属相关线索，未实证单一根因；先修度量与暖重复，不启用丢特效的异步策略。

Vulkan-Docs fixed69ba4fefbafa045d0fd1b07060b768b3b1c115cc LICENSE.adoc read: specification documents CC-BY-4.0. URL https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/69ba4fefbafa045d0fd1b07060b768b3b1c115cc/LICENSE.adoc . Specification reference only; no implementation copied.

# P-CX-TERRA-device-profiles — IN PROGRESS, NOT PASS

2026-11-01 Asia/Shanghai; work/honor-gtpro-30hz; HEAD 032e5f68172c101cb2b6e44ee06f02974dab2da8 plus existing installer WIP and profile WIP. Only A42BBB5109000333 operated. User says this tablet has no separate game mode and requests speaker testing rather than Bluetooth testing.

- Candidate `.local/honor-device-profiles/profile-test.apk`, SHA-256 `6c45338ecf9dd5a3f974be3420e5d14d80467cf3befa343bb63372179446aae3`; installed `-r` with existing signature, no data clear. Build exit 0; full command/log `.local/honor-device-profiles/build-logs/`. Previous APK `.local/android-apk/ffrs-game-window-debug.apk`, SHA b6f4f9e41e00d9d2efb11bd23a1e6fb408295b1dc58971e6b64ee2898b032819, preserved.
- Cold launch no path injection: `tablet_sd8s`, SoC SM8635, display short edge 1920 / 120 Hz; actual Vulkan logs Adreno 735; native `xbg_refresh_target_fps` 30. resolveGameDir autodiscovered `/storage/emulated/0/FFRS`. Logs `.local/honor-device-profiles/profile-log.txt` and `runtime-evidence.txt`.
- Host profile selection test passed for SM8635, MT6897, unknown and overrides; code is WIP. `device-profile` marker selects preset but **1080/720 internal render sizing is not applied**: no existing safe render scale hook found. Thus profile acceptance is incomplete; do not represent output surface size or profile metadata as internal rendering resolution.
- Guest fatal missing shader `fae0b835a11360e2.xgs`; `/sdcard/FFRS/shaders` contains 25,223 files and lacks it. This affects both previous installed APK and candidate. Previous autodiscover PASS proves discovery, not gameplay. Full compatible shader bundle location requested from user; no data or existing saves deleted.
- Source sets AAudio NONE and fence spin 0; runtime reads `performance=10` and fence 0. NDK 26.1 `AAudio.h` defines NONE=10, LOW_LATENCY=12; standing document's `performance=0`/`10` statement conflicts with actual getter and enum. Speaker playback and continuity are NOT_RUN; Bluetooth requested waived by user. No audio PASS asserted.
- Phase B not started: dependency Phase A not passed. No present stability, gameplay, visual or subjective sound acceptance. OSS notes `.local/honor-device-profiles/OSS-NOTES.md`.

## Revised goal and diagnosis, 2026-11-01

User revised Honor target to 1080p/60 and supplied the read-only full pack. User waived Bluetooth testing in favor of speakers and says the tablet does not distinguish game mode. `tablet_sd8s` candidate now requests 60 and existing guest docked mode; `handheld_mid` remains 30/handheld. Candidate `.local/honor-device-profiles/profile60-docked.apk`, SHA 302fa42cafec7428840483dc2b96be507e1ef9b5c69aa8de53e4dbb9fa6368d8; build exit 0; not yet installed or device validated.

First missing shader actually existed under `romfs/shaders`: additive last-resort read path produced real 1280x720 presents but later failed `Image binding slot out of range`. Exact shader 4e2ec305060ff42d from romfs has bad binding slots 257; canonical full-pack same file has slots 130 and 129, explains this specific failure. Full-pack loose directory contains 73496 names, device initially 25223, 48273 absent (3985022864 bytes). Missing-only transfer running: shell PID 6817, tar 6821, adb 6822, session 87963, logs/command `.local/honor-device-profiles/shader-transfer*`; exit file signals completion. No source mutation. Do not accept/benchmark during transfer.

- 09:31 profile60-docked candidate installed `-r`; SHA 302fa42cafec7428840483dc2b96be507e1ef9b5c69aa8de53e4dbb9fa6368d8. During incomplete transfer only, two canonical files `4e2ec305060ff42d.xgs` and `64619ac90fe3dfa2.xgs` were placed in previously absent app-specific `files/shaders/` to diagnose; preserve and later verify source SHA before removing only those own-created overrides. Cold launch showed tablet profile, limiter 60, operation mode 1, initial present 1280x720 and automatic discovery. First diagnostic failed at old romfs shader 646 with slot 257; canonical replacement placed and a retry launched. This is NOT performance or gameplay acceptance.

## 2026-11-01 09:40+ checkpoint

- User explicitly requires handheld unaffected. Source now gates Honor-only identity surface transform and romfs fallback on selected tablet profile; retains handheld operation mode, default limiter and existing environment/marker override. `profile60-isolation-diag.apk` SHA 788fb7b4eff6ed59fb33b928941ec0b0606f3ca0d6cb70acaf4274de99f4746f installed on Honor only. Subsequent activity-reselection fix is source WIP, not built. RG477M not tested.
- Archive transfer complete; source/device SHA `529aede4fc1a1e3bb165757e5011332fb4d4088553587ecb76772013221c0ed8`. 73495 XGS archive keys match full loose set; `index.tsv` is extra directory entry. Honor archive and owned marker enabled. Missing-only loose transfer remains live: session 87963, PIDs 6817/6821/6822.
- Five matching shader cases were observed on Adreno 735 compiler and tested via SPIRV-Tools entry-point inline; sixth is prepared. Overrides owned by this run under app `spv-override/`; generated code in `.local/honor-device-profiles/`. Pipeline diagnostic logs show original crashing key and advance to next. Do not claim equivalent graphics. Full source subgroup helper scan PID 17593/session 50438, log `.local/honor-device-profiles/subgroup-scan.log`; generated data remains local.
- 1080 source observed at frame 300; Android current transform 0x4, identity path produced upright menu screenshots. One static menu sample 43 attempts/s while transfers running is NOT actual moving-scene acceptance. External speaker subjective acceptance and 60fps gameplay remain NOT_RUN.
- `save-before-newgame.tar` and `save-before-confirm.tar` are invalid/incomplete, due private files permission denied, despite zero adb exec-out status on second. Do not treat either as backup. Visible save directory had only `OptionSaveData` and `EnhancedInputUserSettings`; no Continue was displayed before initiating new game, but existing private settings were not backed up. No uninstall or clear-data.

## Later checkpoint

- Read-only census 73495 XGS, 1725 helper-subgroup matches, 0 parse errors; all six observed fault keys included. Local optimizer produced 1725 candidates, each original/result validated, output structural check found zero residual helper subgroup cases. This is structural validation only, not semantic or visual PASS. All 1725 staged to Honor app directory with per-file SHA match (mismatches 0), earlier six preserved under `spv-override-six-backup`. Data and generated shaders remain local; no changes to handheld device or baseline repo.
- Current installed APK SHA `206869a96a4d4790caff4e727832f858732849cd19684d9685869304ad1a81bd` includes handheld isolation and activity reselection; not yet launched with batch. Last valid 1080/upright screenshot was previous APK a14bcf4 and previous isolation code. Game is stopped while another app is foreground; user asked asynchronously when available.
- Loose transfer remains PID 6817/6821/6822, session 87963. Next: confirm its exit/status and compare full source/device hashes, then launch candidate when appropriate, retain logs, check actual gameplay/visuals/audio and FPS. Do not mistake 60 limiter, static menu, or GPU refresh for moving present acceptance.

- Prepared and installed `profile60-display-ab.apk` SHA `a68d57a60d608272395fcef5fbc5dedf5cf099bd5810f4efc41388b03c85ba75`; not launched. Tablet-only marker `gfx-display-compatible=1` is absent by default; requests an integer-multiple supported refresh for A/B (actual modes 60/90/120/144). App data retained. User active in another app; waiting for response to async request rather than bringing game to foreground.
- Honor-only `gfx-codex-frame-metrics=1` marker created after verifying absence; next launch records per-attempt timestamp and outcome for later analysis. It is a diagnostic with overhead, not independent physical scanout proof. Source SHA manifest completed; next compare once transfer stops.

## Shader completion checkpoint

- Missing-only loose transfer exited 0; source/device 73496 names match with 0 missing/extra. SHA-256 checks of all 73496 source/device files exited 0 and compare 0 mismatches (`.local/honor-device-profiles/shader-content-compare.json`, source/device manifests). The `shaders.hnla` 6089256224-byte archive also matches full SHA `529aede4fc1a1e3bb165757e5011332fb4d4088553587ecb76772013221c0ed8`; 73495 shader keys plus index.tsv. No source or preexisting device shaders overwritten by the missing-only transfer.
- Latest built `.local/honor-device-profiles/profile60-canonical.apk` SHA `a085fd99c67530dc11c32fe6c7ceb4d013e055f8208b0aad27ba00df598c1e5a`, removes stale `romfs/shaders` fallback, gates pipeline trace and tablet-specific changes, includes 120Hz cadence A/B marker default off. Not installed or tested; installed APK is a68d57a6. Real-device A/B and user speaker/visual acceptance remain open.
- Host verification: `scripts/android/test_device_profiles.sh` passed selection contract; `python3 tests/installer/test_shader_archive.py` passed 13 synthetic cases; `python3 -m unittest tests/installer/test_present_outcome.py` passed 2. These do not establish device visuals, audio, frame timing or handheld regression.

## 2026-11-01 continuation — handheld isolation contracts

- `python3 tests/device/test_profile_environment.py` exit 0 (2 cases). It compiles the actual JNI profile-environment body and actual Vulkan swapchain selection expression from this worktree: handheld -> tablet -> handheld resets the profile default, external `FFRS_TARGET_FPS=40` remains intact, handheld/unset profile keeps `currentTransform`, and tablet identity is selected only when supported. `scripts/android/test_device_profiles.sh` passed. These are host component tests, not device or gameplay acceptance.
- Honor device A42BBB5109000333 remains online with `com.hongguo.noad` foreground; `org.ffrs.gamewindow` not running. Prior async request to switch back remains pending. No game foreground action, install, or RG477M operation in this continuation.

- Additional protection: auto `tablet_sd8s` now requires exact `SM8635` and `smallestScreenWidthDp >= 600`; compact/unknown falls back to `handheld_mid`, explicit override remains available. Honor read-only system config reported `sw768dp`. Android 600dp is a layout heuristic, not physical-device proof; real Honor/RG acceptance not claimed. `scripts/android/test_device_profiles.sh` passes 10 selection examples including compact SM8635 and near-match SM86350.
- `python3 tests/device/test_target_fps_marker.py` passes actual native reader checks for default30, environment60, marker30, invalid marker, marker0 and restore; the first fixture compile failed due two unused extracted variables and was fixed without changing production logic. `python3 tests/device/test_profile_environment.py` passes 2.
- New candidate `.local/honor-device-profiles/profile60-screen-guard.apk`, SHA `fa43397f3b15a0977eadc08db601fd16e91414baa602da6a225d29fe6d0d11ef`, build exit 0 and logs `.local/honor-device-profiles/build-logs/screen-guard.log`. It supersedes a085fd99 for next device validation, but is not installed or tested on device. Honor still in another app; no new foreground action, installation, or RG477M operation.


## 2026-11-01 continuation — optional Adreno subgroup runtime

- Experimental runtime integration uses upstream d2r-mobile revision `f1e4b081af8ea65d95f0bf490a568cb657c3917e` (MIT), bundled Khronos SPIRV-Tools `vulkan-sdk-1.4.357.0` (Apache-2.0) and SPIRV-Headers (see `.local/honor-device-profiles/OSS-NOTES.md`). Upstream three-pass transform differs bytewise from the previously staged two-pass outputs for all six cases; validation does not establish equivalent rendering.
- Six actual previously crashing shader inputs passed the actual wrapper on host and Honor standalone ARM64, produced identical host/device bytes, passed `spirv-val`, and were idempotent; details `.local/honor-device-profiles/runtime-inline-proof/actual-six-{host,device}.json`. This tests the optimizer component, not game execution, driver correctness, performance, or sound.
- Optional helper is invoked only with tablet profile, Vulkan vendor `0x5143`, and device name `Adreno (TM) 735`; `gfx-honor-inline=0` disables it. Existing shader overrides take precedence. Default build leaves SPIRV-Tools absent; clean default APK SHA `26ded5c07da9630f7fb474863927c28fd9d15ad65307dfa7ced06f501fe0f913`, build exit 0, no helper symbol. Host profile/environment tests pass (3 cases), profile selection and FPS marker tests pass; real handheld regression is NOT_RUN.
- Latest optional candidate `.local/honor-device-profiles/runtime-inline-proof/profile60-inline-auto-license.apk`, SHA `3d9cfb2f07e0eac70b304c612e67821607c9f3e4f72017178ba56d44ffb596e4`, build exit 0; archive contains project notice, MIT, Apache-2.0 and headers license. Build command `.local/honor-device-profiles/runtime-inline-proof/auto-license-command.txt`; pinned source acquisition `scripts/android/prepare_honor_spirv_tools.sh`. Candidate NOT installed or run. Existing Honor staged 1725 overrides would bypass runtime rewrite; if testing it, preserve and move only our own overrides to a new unique backup after verifying game stopped, never delete them.
- Honor was in another app; prior availability request remains unanswered. No game takeover, package install, or handheld operation this continuation. Installed APK remains `a68d57a60d608272395fcef5fbc5dedf5cf099bd5810f4efc41388b03c85ba75`. Device-profile acceptance and 1080p/60 moving gameplay, visuals and speaker acceptance remain NOT_RUN; no PASS. Existing attempts at settings backup failed and must not be treated as valid.


## 2026-11-01 continuation — actual runtime wrapper batch

- Host executed the production `xbg_inline_subgroup_helpers` wrapper on all 1,725 canonical shader inputs selected by the earlier census. All returned Changed, then Unchanged with identical bytes on a second pass, and each output passed `spirv-val --target-env vulkan1.1`: 1,725/1,725, zero failures, elapsed 28.38 seconds with two host workers; process exit 0. Command, full results, source script, and generated local inputs/outputs under `.local/honor-device-profiles/runtime-inline-proof/batch-actual*`. Outputs are game-derived local data and must not be committed.
- This host batch is component evidence, not Android runtime timing, Vulkan driver behavior, visual equivalence, audio, or moving gameplay acceptance. Device A42BBB5109000333 read-only status online with `com.hongguo.noad` foreground; earlier availability request still pending; no install or foreground change.


## 2026-11-01 continuation — measurement readiness and batch comparison

- Compared all 1,725 host production-wrapper outputs with the previously staged two-pass offline candidates: 236 SHA-identical and 1,489 different; output-size ratio median 1.0010, maximum 1.4897 (`f5bf1172b12811b9`). Full comparison `.local/honor-device-profiles/runtime-inline-proof/batch-actual-comparison.json`. Both sets' structural validation is not evidence of semantic/visual equivalence or Android runtime performance.
- Existing `python3 tests/native/test_frame_metrics.py` exit 0, 9 cases: result filtering, legacy trace handling, 60Hz synthetic fixture, duplicates and long-stall retention. This is analyzer verification only; queued callbacks still do not prove physical scanout or actual gameplay.
- Read-only Honor device state: online, `com.hongguo.noad` foreground. Earlier availability request remains pending; no install or game foreground. Next actual A/B must preserve our staged overrides and user app data as in HANDOFF.


## 2026-11-01 continuation — pinned source cold preparation

- `scripts/android/prepare_honor_spirv_tools.sh` was run against a previously nonexistent checkout under `.local/honor-device-profiles/runtime-inline-proof/fresh-d2r-f1e4b08`; fresh sparse clone exit 0, HEAD `f1e4b081af8ea65d95f0bf490a568cb657c3917e`. Recursive diff of its SPIRV-Tools subset against the earlier build input exit 0 with no differences; reuse of the new checkout also exited 0. Exact command/stdout/stderr/exit and diff logs in sibling `prepare-fresh*`, `prepare-reuse.stdout`. This verifies source preparation only, not the Android game.
- Device A42BBB5109000333 read-only: online, `com.hongguo.noad` foreground, prior availability response pending. No app install, game launch or handheld operation.


## 2026-11-01 continuation — pacing/measurement audit

- Source audit: `xbg_refresh_target_fps` sets the present pacing interval; baseline `nso_host.c` separately starts an approximately 60Hz VI event ticker. Neither proves that game simulation, animation, physics, cutscenes or sound retain normal speed at a 60 target. This remains an explicit real-game acceptance gate; no frame-rate success can override it. No timing behavior was changed.
- Found a pre-existing diagnostics limitation in `xbg_frame_pace`: `now_ns` and `start_ns` are equal and `now_ns` is not refreshed after `nanosleep`, so its returned pace time is always zero; the caller's `perf.present_cpu_ns` may include pacing sleep. Confirmed same code in current HEAD. Do not interpret `present_cpu_ms` as pure non-pacing CPU work or use it alone for bottleneck ranking; use trace timestamps and additional direct evidence. No shared handheld timing code modified.
- Honor A42BBB5109000333 online in `com.hongguo.noad`; availability request pending; no launch, installation or handheld action.


## 2026-11-01 continuation — present outcome path

- Audited `nvn_vulkan_present_image` -> `engine_present` -> frame CSV -> `analyze_frame_metrics.py`: Vulkan success/suboptimal is queued, out-of-date/surface-lost is skipped, other errors are failed; adapter maps queued/skipped/failed to 0/1/-1 as the analyzer expects. `python3 -m unittest tests/installer/test_present_outcome.py tests/native/test_frame_metrics.py` exit 0, 11 host cases. These validate contracts only; Vulkan queueing is not physical scanout and they do not establish gameplay performance.
- Device A42BBB5109000333 online, `com.hongguo.noad` foreground; previous request for availability unanswered. Further validation of selected candidate, real movement, sound and user-perceived speed requires tablet availability. No app installation, foreground takeover or handheld action.


## 2026-11-01 — user available, ADB device absent

- User explicitly confirmed tablet available for continued testing. `adb -s A42BBB5109000333 get-state` attempted twice: both `error: device 'A42BBB5109000333' not found`, exit 1. No install, app launch or device write occurred. USB connection/debug authorization request sent; no other device queried.
- Candidate SHA reverified `3d9cfb2f07e0eac70b304c612e67821607c9f3e4f72017178ba56d44ffb596e4`; after reconnect check game stopped, preserve our override directory as documented and install `-r`. Device and audio/performance acceptance NOT_RUN.


## 2026-11-01 16:35+ — Honor real run, audio failure

- User made A42BBB5109000333 available; online/USB, battery 100%. Game stopped before test; our 1,725 prior overrides moved intact to `spv-override-batch-preserved-20261101-runtime-ab` (count 1725), six-file earlier backup untouched. Installed candidate `profile60-inline-auto-license.apk` with `adb -s A42BBB5109000333 install -r`; installed SHA `3d9cfb2f07e0eac70b304c612e67821607c9f3e4f72017178ba56d44ffb596e4`; no clear/uninstall. PID 11288; cold `resolveGameDir` found `/storage/emulated/0/FFRS`; tablet_sd8s selected, limiter 60; runtime subgroup inline enabled and at least shaders 3f6e/4885 rewritten. Real guest present source 1920x1080 by frame 300, upright title (`honor-runtime-now.png`); no inference about later full game.
- User: title sound/launch normal; opening animation audio and video extremely stutter. 30 s descriptive post hoc CSV window during reported stutter: 267 queued presents / 29.9559 s, 8.88 Hz, p50 103.65 ms, p95 147.33, p99 155.98, max 158.58; 187 intervals >100 ms, 0 reported failed/skipped. Queue callbacks not physical scanout. Raw and derived `runtime-inline-proof/honor-run11288-intro*`. NOT 60Hz or audio PASS.
- In compliance with standing audio gate, created previously absent Honor-only `gfx-target-fps` marker with 30; runtime log confirms 30. User confirms still very choppy. Recent PERF windows ~8.6–9.2 attempt/s, GPU sample ~60ms, ~9.8 submits/waits per frame, 0 new pipelines in cited windows; does not prove single root cause. Existing PCM xrun counts (1/2) and audio buffer logs cannot overrule listening. No 60/40 increase until audio recovers and is verified.
- PID 11288 remains running at opening; no valid save backup exists, do not clear/uninstall or assume backup. Logs `runtime-inline-proof/honor-run11288-after30.log` and CSV, screenshots alongside. Device profiles / speaker acceptance remain INCOMPLETE.


## User visual report — Honor run 11288

- User directly reports effects look wrong and some flames appear/disappear, alongside low frame rate. This is a FAIL for visual correctness; exact cause and whether it is driver, shader rewrite, game timing or existing rendering behavior are unproven. Do not disable effects, relax scene fidelity, or characterize inline validation as visual success. Need compare repeated scene, source/three-pass and previously preserved two-pass overrides without destroying saves.
- Current 30-limited guest performance windows ~9–10 attempts/s, each with ~9 GPU submissions/waits and ~47 render passes; no meaningful real-time performance acceptance. The previous captured 8.88 Hz interval was queue callback timing, not a scanout measurement.
- `simpleperf stat` attempt failed: task-clock event unsupported; `.local/honor-device-profiles/runtime-inline-proof/simpleperf-run11288.txt`. Khronos synchronization sample and Android mobile render-pass guidance recorded in local OSS-NOTES; observed waits are not evidence that synchronization can be removed.


## Active fire scene — visual black and live pass profile

- After tutorial and user-authorized 10 s movement, multiple separate screenshots `fire-field-ready.png`, `fire-motion1.png`, `fire-black-repeat1.png`, `fire-black-repeat2.png` show mostly black game image with small map and UI; app PID 11288 remains alive. Movement inferred from map icon, scene image unreadable; visual FAIL. User confirmation of their physical screen requested, pending. This is not a valid full 2-minute moving-scene performance acceptance; do not label tutorial or dark captures as such.
- Used preexisting `gfx-pass-live` one-shot marker for 3 frames; marker consumed; frames 22713–22715 each 12 slots, 318 queries, zero skipped slots. `honor-run11288-passlive.log` and `passlive-summary.json` store 477 pass/compute records. A 1384x780 pass with 179 draws/444405 vertices took 4.2–4.8ms per sampled frame; compute shader key `073ce354a4f65e2f` groups 11x7x1 took 3.24–3.29ms. These are timestamps around instrumented passes, not proof of root cause or permission to skip work. Disassembled canonical compute SPIR-V shows 8x8x1 local size, two loops and image fetch/write; role/semantics remain unproven (`heavy-073ce354.spvasm`, local game-derived).
- User authorized direct movement. 30 target remains active following audio failure; no 60 performance or sound PASS. No new code or synchronization changes.


## 2026-11-01 — visual confirmation, cache diagnostic and log-volume candidate

- User confirmed device also shows black main scene, and observed flames flicker when moving. Repeated screenshots `fire-black-repeat*.png` and `flame-sequence-*.png` show black majority with minimap; visual FAIL. Live diagnostics disabled image cache at present 26910, cross-frame memo at 29820, descriptor memo at 31590, binding cache at 33870 (cumulative within PID 11288); screenshots remained black after each. Movement overlapped first test, so not strict same-scene performance A/B. Marker files were moved to uniquely named `gfx-live-off-used-*` (no active live-off file); flags remain off until app restart. No cache toggle proved a fix.
- Current real run 11288 generated 78,513 `NVN texture lifecycle retired` lines in 229,139-line snapshot; stderr uses unbuffered mode. OSS-NOTES records Android trace overhead guidance and upstream revision. Source now limits repeated >=1MiB retirement diagnostics only on tablet profile (first 32, then every 1024); the actual retirement call still runs every time, other profiles keep prior logging. Build `profile60-inline-loglimit.apk` SHA `3bd9e07e00b1a2af996646959c9c8a47a25d7a83cf4613e69be9fe6903ae7eab`, exit 0, host profile tests 3 passed. Not yet device-tested; does not purport to fix black or audio, no performance claim.
- Before install, full current guest log and CSV pulled to `honor-run11288-before-loglimit-install.*`. Auto-created `codex-local-save/GameSaveData0` exists (304144 bytes) but app-private mode prevents shell and run-as reads, so content/hash backup is NOT available; install only with `-r`, no data clear/uninstall. 30 marker remains, user audio failure unresolved.


## Pause checkpoint — loglimit installed and final readback contrast

- Loglimit candidate `3bd9e07e00b1a2af996646959c9c8a47a25d7a83cf4613e69be9fe6903ae7eab` was installed -r and device SHA verified. Continue appears and loads existing GameSaveData0. PID23570 still black; cited PERF windows ~7.6 attempts/s, retirement log count reduced to36 in sampled transcript; no demonstrated performance or visual recovery. Prior cache toggles reset on restart; own active live-off markers all archived.
- Full one-frame probe PID27508/frame7667 complete with END, 221 events/61384180 bytes, 1133 files pulled. Readback groups209–212 show world/fire, while normal screenshots before/after black. Final-only normal-recording probe PID10866/frame10174 also complete with END, 36864 bytes, shows world/fire with some abnormal regions. Different runs/frames, readback adds synchronization; cannot conclude cache root cause. Comparison `.local/honor-device-profiles/runtime-inline-proof/probe-mode-comparison.png`, raw captures in sibling `codex-probe-27508/` and `codex-probe-10866/`.
- Last-known PID10866 running at pause, 30 marker. Probe marker archived to gfx-codex-probe-used-10866, earlier used-27508 retained; no active probe marker. Full latest PID10866 guest log not yet pulled; retrieve before restart when resumed. Internal viewports below1080 observed, final source1920x1080 does not prove internal1080. Goal explicitly paused, then user requested handoff prompt only; no additional device work in prompt-writing turn.
- Resume task card: `coordination/prompts/P-CX-TERRA-HONOR-render-correctness-resume.md`. All final gameplay/audio/visual/performance gates remain open.


## 2026-11-01 19:00–19:30 — resume read-only audit; black-scene ROOT CAUSE identified (fix NOT yet built)

Goal still user-paused at the time of this audit; per the resume card, only read-only整理 was performed before the user restores the goal. No new APK was built, installed, launched or uninstalled. Device `A42BBB5109000333` (ROD2-W69, Android 16 / API 36) was read only; `RG477M01445122` was not touched.

### State verified against the resume card

- Branch `work/honor-gtpro-30hz`, HEAD `032e5f68172c101cb2b6e44ee06f02974dab2da8`, 37 dirty/untracked paths — unchanged, no branch switch, no cleanup.
- Installed `org.ffrs.gamewindow` base.apk SHA256 `3bd9e07e00b1a2af996646959c9c8a47a25d7a83cf4613e69be9fe6903ae7eab` == local `profile60-inline-loglimit.apk` — matches the card exactly.
- Markers: `gfx-target-fps`=30, `gfx-codex-frame-metrics`=1, `gfx-shader-archive`=1. No active `gfx-codex-probe`, no active `gfx-live-off`; the four `gfx-live-off-used-*` archives are present as recorded. `spv-override` absent; `spv-override-batch-preserved-20261101-runtime-ab`=1725 and `spv-override-six-backup`=6.
- Game was still running as PID **10866**, foreground `GameActivity`, `guest_boot.log` still growing. The card's "preserve PID10866 guest log before any restart" item was done first: pulled to `.local/honor-device-profiles/runtime-inline-proof/pause-snapshot-10866/` (guest_boot.log 16,440,838 B; codex-frame-10866.csv 1,507,379 B; vk-pipeline-list-10866.bin 111,368 B; codex-probe-10866-device/). Save `codex-local-save/GameSaveData0` = 304,144 B, unchanged; no install/clear/uninstall was performed.

### ROOT CAUSE of the black scene (previously "readback visible but physical screen black")

The two probe captures are not a coincidence and not a timing artifact. The presented image itself carries the defect:

| Evidence | Result |
|---|---|
| `codex-probe-10866` final-only present readback (frame 10174) | 9,216 sampled pixels: **alpha==0 for 8,486 (92.1%)**, alpha==255 for 22; **99.9% of pixels still carry non-zero RGB** |
| `codex-probe-27508` full-process present readback (group 221, frame 7667) | **identical distribution**: 8,486/9,216 alpha==0 (92.1%), 22 alpha==255 |
| Full-resolution ROI `f7667-e221-...-roi.bin` (640×540 = 345,600 px) | **100.00% alpha==0**, 99.96% of them with real RGB (e.g. fire `(86,37,17)`) |
| Rendered view of that readback | street, character and flames all correct — RGB is fine, **alpha is not** |
| Non-zero-alpha pixels' spatial map | confined to the **top-right mini-map/HUD** (col-bands 6–7, row-bands 0–3); the whole 3D world area is alpha 0 |
| Physical screen capture (`physical-screen-now.png`, 3000×1920) | 81.0% pure black; mini-map window visible at top-right, 3D world entirely black — **spatially the same region** |
| Final present pass shader `cdd473028af69d48` (group 212, viewport 0,0,1920,1080) | `OpStore %203 %float_0` — the shader **writes alpha = 0.0 explicitly** into the colour output |
| That shader vs. canonical game original | XGS1 container extracted at offset 128 → SPIR-V SHA256 `47bdae33…` **identical** to the device-captured `.spv` ⇒ the alpha=0 write is the **game's own behaviour**, not introduced by our runtime inline rewrite |
| Blit geometry | crop is always `(0,0,0,0)`; src 1920×1080 → dst 3000×1687 at (0,116) on a 3000×1920 swapchain, scale 1.5625 — **geometry is correct** |
| Composite alpha in source | `swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`, and the app never sets a native window blending mode ⇒ platform default decides |
| Android window format | app forces `PixelFormat.RGBA_8888`; SurfaceFlinger layer reports `format: RGBA_8888_UBWC`, `alpha: 255`; the Activity window is `fmt=TRANSLUCENT` |

**Mechanism.** The game renders its final frame with alpha deliberately 0 (UE-style, alpha used only for the HUD pass). Our present path blits that image into the swapchain with `VK_FILTER_LINEAR`, and the blit **preserves the source alpha** into a format that has a real alpha channel. Because the swapchain is created with `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR` and no native blending mode is set, the Honor compositor takes the platform default, which **honours the alpha channel** and multiplies the world away. The mini-map/HUD is the only region with alpha≠0, so it is the only region that survives. The probe readback bypasses the compositor, so it shows the picture — which is exactly why the two observations looked contradictory.

This also explains, with one cause, three separate user reports: mostly-black world, visible mini-map, and flickering fire (fire is alpha-blended, so it is exactly the content that alpha decides).

Note the RG477M baseline is not directly comparable: it presents through a **three-step render-pass path** (`nvn_present.c` → `begin_render_pass` → `submit_and_present`, rendering straight into the swapchain image), whereas this tree's engine path renders into the guest's own target and blits it. The blit is what introduces the alpha-carrying copy.

### Planned minimal fix (normative sources recorded in OSS-NOTES; NOT yet implemented)

Two single-variable candidates, to be tried in this order, each Honor-gated, keeping `handheld_mid` defaults and existing override precedence:

1. **Composite alpha → OPAQUE** (`VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` when the device reports it in `supportedCompositeAlpha`): the spec defines this as "the alpha component … is ignored in the compositing process. Instead, the image is treated as if it has a constant alpha of 1.0". Zero effect on the guest's own render target; only the compositor decision changes.
2. **Swapchain format → X-component** (e.g. `VK_FORMAT_R8G8B8X8_UNORM`) when the surface offers it, plus `PixelFormat.RGBX_8888`/`PixelFormat.OPAQUE` on the Android side: per the blit rules, "if the source image format has an X component, the value of the X component is ignored, and the destination image's A component is set to 1.0", so the destination becomes opaque by construction.

Neither changes game logic, does not skip drawing, adds no global `waitIdle`, and does not remove any synchronization. Acceptance must be a **real physical screen capture** showing the world visible — not a readback image — plus flame/transparent/occlusion/character/UI checks, and then the standing audio gate and the 60 Hz goal.

### Status unchanged

Black scene: **FAIL**, root cause now identified but unfixed and unverified on device. Audio: **FAIL** (user-reported), speaker acceptance NOT_RUN, Bluetooth USER_WAIVED. Performance: no 60 Hz result; 30 Hz limiter marker still active as the audio-failure protection. Visual/handheld regression: NOT_RUN (no RG477M operation this round). Device-profile acceptance, 1080P internal-render proof and game-speed acceptance remain **INCOMPLETE**. No PASS is claimed anywhere.

# RG477M-SYNC-1.0.9 — RESULT

One APK for both devices: the RG477M handheld (4:3, 1280x960, MT6897 /
Mali-G615 MC6) and the Honor tablet, whose accepted behaviour is left
alone.

- Date: 2026-11-12
- Branch: `task/T-rg477m-sync-20261112`
- Worktree: `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/rg477m-sync-20261112`
- Baseline: `db094f8` (the 1.0.7 packaging line: moon fix, indirect fix, cheat menu)
- Sources: `coordination/results/RG477M-SYNC-1.0.9-OSS-NOTES.md`
- Device evidence (local, not in git): `.local/rg477m-adapt/1.0.9-evidence/`

## 1. What the code now does

### 1.1 The resolution tier is applied, not only logged

`DeviceProfile` carries the short edge of the internal render target
(`targetShortEdge`). It was logged as `render_resolution=unapplied`: no
code acted on it. Now the tier decides the operation mode the guest renders
in, and the guest's render target follows from that.

- `DeviceProfile.renderTierWidth()/renderTierHeight()`: the tier as a render
  target — 720 → 1280x720, 1080 → 1920x1080 (the game is 16:9).
- `DeviceProfile.usesDockedRendering()` is now derived from the tier
  (`targetShortEdge >= 1080`), so the tier and the operation mode can no
  longer disagree.
- The operation mode reaches the guest through the path that already
  existed: `GameWindowNative.setProfileTargetFps` → `FFRS_PROFILE_OPERATION_MODE`
  → `oe-operation-mode` marker → `nn::oe::GetOperationMode()`, which the
  guest (Unreal Engine's Switch NVN RHI) uses to pick its render target.

**On the device this is confirmed end to end.** The app log says

```
device_profile id=handheld_mid soc=MT6897 override=none smallest_width_dp=549
form_factor_width_dp=548 target_short_edge=720 target_fps=30
display_short_edge=960 display_hz=60.0 gpu=see_vulkan_device_log
render_tier=1280x720 render_short_edge=720 operation_mode=handheld
```

and the guest transcript says the operation mode really reached the guest:

```
[port] operation_mode=0 source=default-handheld
[nvn] vulkan present #0: source=1280x720 ... -> dst=1280x720 at (0,120)
```

The guest's render target is 1280x720, which is the tier. The tier is
applied, not merely logged.

**The existing profile mapping was not changed.** mt6897 / model RG477M →
`handheld_mid` (720, 30) as before; `tablet_sd8s` (1080, 60) and
`phone_sd8g3` (1080, 60) keep their tiers and their docked mode. One
reserved slot moves: `odin_elite` declares 1080 and therefore now uses the
docked mode, where it previously used the handheld one. `odin_elite` is a
reserved profile with no device (the planned Odin 3 slot), so no hardware
behaviour changes.

### 1.2 The 4:3 case: pillarbox, no stretch

The present path already fitted the game's picture into the surface without
stretching it: the smaller of the two axis factors is used, the picture is
centred, and the leftover room is cleared once per swapchain image. That is
the pillarbox. The change makes it a named, testable and observable thing
instead of inline arithmetic:

- `nvn_vulkan_fit_preserve_aspect()` in `src/graphics/nvn_device/nvn_backend_vulkan.c`
  holds the fit. The arithmetic is unchanged, so the picture on the screen
  does not move.
- The present log now reports the geometry of the frame instead of leaving
  it to be guessed.
- A host contract compiles that exact function out of the production file
  and checks the 4:3 case.

**On the device the geometry is exactly what the contract predicted:**

```
[nvn] vulkan present #0: source=1280x720 crop=(0,0,0,0) -> dst=1280x720 at
(0,120) on swapchain 1280x960 (scale=1.0000) aspect=preserve bars_top=120
bars_bottom=120 stretch=no
```

16:9 content on the 4:3 panel: the whole picture, centred, 120 black
pixels above and below it, scale 1.0, nothing cut away, nothing stretched.
This is what `docs/PROJECT.md` asks for. The game keeps its own
proportions and its own time: nothing in this change touches the frame
pacing, the simulation or the audio.

### 1.3 The target frame rate is declared to the platform

A 30 FPS profile asks for no display mode, so nothing told the platform
what the game produces. The app now declares it:
`Surface.setFrameRate(30, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS)` (the three argument form exists from
Android 12; Android 11 gets the two argument form, which the platform
documents as the same request). The engine's own limiter is untouched, so
the game time does not change.

**On the device:**

```
display_request: target_fps=30 requested_hz=0.0 mode_id=0
surface_frame_rate_declaration: hz=30 compatibility=default strategy=only_if_seamless
```

No display mode is asked for (the panel stays at its 60 Hz mode) and the
game's own 30 FPS is declared. The declaration is limited to the **handheld
tier**. The Honor tablet currently runs with a 30 FPS marker on a 144 Hz
panel; declaring 30 there would have changed the scheduling of a device
whose 1.0.7 acceptance is signed off, and that is not this task's decision.
The Honor behaviour is therefore the same as in 1.0.7.

A marker `gfx-surface-frame-rate` in the app's own file directory turns the
declaration off, so both ways can be compared on the device without a new
build. It holds "0" to turn it off; it is written with "1" on first start.

### 1.4 Data root and seed discovery: unchanged, and correct as it is

`resolveGameDir()` resolves the data root in the order intent override →
managed root → autodiscovery, and no path was hardcoded. The pipeline warm
list is found relative to the resolved guest image
(`<image dir>/pipeline-warm-list.bin`, used only when it is readable), so
the seed follows the data root automatically. No change was needed and none
was made.

**On the device the right root was found, not the Honor one:**

```
resolveGameDir: autodiscovered game_dir=/storage/emulated/0/HandheldNativeLab/data
live_save_root=/storage/emulated/0/Android/data/org.ffrs.gamewindow/files/codex-local-save
```

### 1.5 Version

`versionCode` 7 → 9, `versionName` 1.0.7 → 1.0.9. One APK covers both
devices: the RG477M had code 8 installed and the Honor tablet code 7, and
both are below 9, so an overwrite install works on either.

## 2. The artifact

| Item | Value |
|---|---|
| APK | `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/rg477m-sync-20261112/.local/android-apk/ffrs-game-window-1.0.9.apk` |
| Copy at | `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/apk/ffrs-game-window-1.0.9.apk` |
| Bytes | 3523650 |
| SHA-256 | `2bd65685f83bb675b24523b7fa2daa1947b7e874b736a589c95c89ffb1324e22` |
| Package | `org.ffrs.gamewindow`, versionCode 9, versionName 1.0.9, minSdk 28, targetSdk 34 |
| Signer certificate SHA-256 | `d11967a31fcef92f3d6b44adf9d9678c8d2b07bf8f3246d51a791617ed61f36e` |
| Contents | `classes.dex`, `lib/arm64-v8a/libffrs_game_window.so`, `assets/ffrs_save_key.bin`, `assets/licenses/ffrs-subgroup-inline/` (4 notices) |

**The signer matters.** The first build of this session signed with a key
that the worktree generated on its own (certificate `dd471110…`), which
would have failed an overwrite install on the RG477M with
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`, and the only cure would have been an
uninstall with the app-private saves in it. The build was redone with the
key that already signed the Honor 1.0.7 and the RG477M's installed 1.0.8
(certificate `d11967a3…f36e`, verified against `installed-1.0.8.apk` and
against the device after the install). The key lives in `.local/` and is
not in git.

Build command (all 18 steps exited 0, logs in
`.local/logs/android-game-window/`):

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/rg477m-sync-20261112
FFRS_SPIRV_CHECKOUT=/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/third-party/d2r-mobile-f1e4b08 \
FFRS_APK_NAME=ffrs-game-window-1.0.9.apk \
bash scripts/android/build_game_window_apk.sh
```

The SPIRV-Tools subset is the pinned upstream checkout
(`f1e4b081af8ea65d95f0bf590a568cb657c3917e`, clean), reused read-only from
the Honor tree so the expensive fetch is done once. Its runtime inline
wrapper stays Adreno-only, so it is inert on the RG477M's Mali-G615.

## 3. The change in `nvn_backend_vulkan.c`, and how it stacks

This file is the **presentation backend of the NVN device layer**, not the
game engine. The engine line lives in
`src/graphics/nvn_vulkan/engine/xbg_game.cpp`; the backend is the thin
Vulkan side that owns the surface, the swapchain and the copy into it.

What the change does:

1. The inline fit arithmetic is moved out of
   `nvn_vulkan_present_image()` into a named function
   `nvn_vulkan_fit_preserve_aspect()`. The operations and their order are
   unchanged, so the picture on the screen does not move on either device.
2. The present log line gains the border the picture leaves
   (`aspect=preserve bars_top=… bars_bottom=… stretch=no`).

Why it had to be changed on the 477M side: the 4:3 panel is the case where
"keep the game's proportions" has to be visible and checkable. Before, the
fit was inline arithmetic in a function that no host test could reach, and
the log only printed the destination rectangle — the bar size had to be
worked out from the sizes by hand. The RG477M needs the pillarbox to be
stated in the log, because on a 4:3 panel it is the difference between
"16:9 content, centred, untouched" and "a stretched picture".

How it stacks with the `xbg_game.cpp` line: the two do not overlap. The
engine commits `7229b02` and `d5a3340` touch
`src/graphics/nvn_vulkan/engine/xbg_game.cpp`,
`xbg_spirv_indirect_instances.h`, `xbg_spirv_integer_buffer.h`,
`xbg_spirv_stencil.h`, `scripts/distribution/inspect_pipeline_warm_list.py`,
`coordination/*` and two `tests/device/*` files. This task touches
`nvn_backend_vulkan.c`, `DeviceProfile.java`, `GameActivity.java`, the
game window manifest and two test files. There is **no file in common**, so
the profile/resolution/aspect/frame-rate line merges into the engine line
without a conflict. The Honor workers' `xbg_game.cpp` changes (effect
warm-up and cutscene timing) are in the same engine file and are not
touched, absorbed or anticipated here.

## 4. Device validation on the RG477M

Device: `RG477M01445122`, model RG477M, `ro.soc.model=MT6897`,
`ro.board.platform=mt6897`, GPU **Mali-G615 MC6**, panel 1280x960 4:3,
density 280, active mode 60 Hz. Evidence in
`.local/rg477m-adapt/1.0.9-evidence/`.

### 4.1 Install over 1.0.8, no uninstall

```
adb -s RG477M01445122 install -r .../ffrs-game-window-1.0.9.apk
Performing Streamed Install
Success
dumpsys package org.ffrs.gamewindow -> versionCode=9 versionName=1.0.9
lastUpdateTime=2026-11-12 23:09:45
```

The app was on 1.0.8 / code 8 before, with the same certificate. No
uninstall, no data clear: the app-private directory, the markers and the
saves were left in place. The save directory was backed up before the
install (the existing backup at `.local/rg477m-adapt/backup-20261112/`
covers the live saves, the historical saves and the markers; the live saves
on the device still matched the backup byte counts).

### 4.2 Profile, tier and frame rate declaration

```
device_profile id=handheld_mid soc=MT6897 override=none smallest_width_dp=549
form_factor_width_dp=548 target_short_edge=720 target_fps=30
display_short_edge=960 display_hz=60.0 gpu=see_vulkan_device_log
render_tier=1280x720 render_short_edge=720 operation_mode=handheld
display_request: target_fps=30 requested_hz=0.0 mode_id=0
surface_frame_rate_declaration: hz=30 compatibility=default strategy=only_if_seamless
window_form display_width_px=1280 display_height_px=960 surface_width_px=1280
surface_height_px=960 density_dpi=280 refresh_rate_hz=60.0
[port] operation_mode=0 source=default-handheld
[engine] xbg_refresh_target_fps: locked target_fps=30 target_ns=33333333
```

### 4.3 The 4:3 picture geometry (from the device's own present log)

```
[nvn] vulkan present #0: source=1280x720 crop=(0,0,0,0) -> dst=1280x720 at
(0,120) on swapchain 1280x960 (scale=1.0000) aspect=preserve bars_top=120
bars_bottom=120 stretch=no
```

The content is the full 1280x720, centred on the 1280x960 panel, 120 black
pixels above and below, scaled 1.0, not stretched.

A screenshot with **visible game content** was **NOT obtained**: the guest
dies a few seconds into the boot (section 4.6), before a title frame is
drawn, so the screen shows the app's own overlay over a black game area.
The screenshots that exist are `rg477m-1.0.9-cheat4.png` (the cheat
dialog) and `rg477m-1.0.9-t3.png` (the black game area with the app
overlay). The geometry above is the direct evidence for the pillarbox; the
picture is not.

### 4.4 Frame rate (CSV, queue callbacks)

Source: `/sdcard/Android/data/org.ffrs.gamewindow/files/codex-frame-8432.csv`,
pulled to `.local/rg477m-adapt/1.0.9-evidence/`, analysed with
`scripts/graphics/analyze_frame_metrics.py`
(`frame-metrics-8432.json`). No screenrecord, no gfx-flame-diag.

| Measure | Value |
|---|---|
| Attempts | 21 (all queued, 0 skipped, 0 failed) |
| Interval p50 | 33.329 ms → **30.0 FPS** |
| Interval p95 | 33.943 ms |
| Longest interval | 1730.1 ms (one stall, between frame 3 and frame 4) |
| Window average | 8.46 presents/s over 2.363 s |

The pacing is 30 FPS with one long stall; the window average is dragged
down by that stall because the window is only 2.4 seconds long. This is a
boot window, not gameplay: the analyzer's own verdict is
`acceptance_decision: NOT_EVALUATED`, and a queue callback is not proof of
physical scanout. **No gameplay frame rate is claimed.**

### 4.5 The cheat (金手指) and the save

The menu opens and lists 金手指. The cheat dialog opens and reads the
save:

```
金手指
存档槽: GameSaveData0
单位数: 8
选择角色  单位1001  HP 270/-1  MP 35/-1  EXP 0
生命值 HP（当前） 270     生命上限 -1
魔法值 MP（当前） 35      魔法上限 -1
```

This is the payoff of the key question: the save on the RG477M is
**decrypted with the honor key** and parses into real game state (8 units,
HP 270, MP 35), so the cheat works on this machine. The guest itself read
the save in the same run:

```
[hle] nn::fs::MountSaveData: save0 -> .../files/codex-local-save
[hle] nn::fs::OpenFile: path='save0:/OptionSaveData' ... size=32704 -> handle=...
```

Writing a changed value was **NOT tested**: the dialog says the change
takes effect after a Load Game at the title, and the title is unreachable
(section 4.6).

### 4.6 The guest dies at boot — a pre-existing engine defect, not a regression

Every run ends the same way, about four to five seconds after the launch:

```
[engine] NVN texel format=98 bufferFeatures=0x78 formatFlags2=1
[engine] FATAL: Texel format lacks formatless-write support
[engine] FATAL: Texel format lacks formatless-write support
```

and the process dies (`Process org.ffrs.gamewindow (pid 8432) has died`).
The engine also writes `engine.log.fatal` with the same line.

**This pre-exists and is not caused by this change.** The 1.0.8 run's own
`engine.log` on the device ends with the identical lines, before 1.0.9 was
ever installed:

```
[engine] NVN texel format=98 bufferFeatures=0x78 formatFlags2=1
[engine] FATAL: Texel format lacks formatless-write support
[engine] FATAL: Texel format lacks formatless-write support
```

The check that fails is in the engine line, not in this task's line:
`src/graphics/nvn_vulkan/engine/xbg_game.cpp:3867`

```c
if(b.flags&1)need(cfg.enabled_core.shaderStorageImageWriteWithoutFormat &&
    (format_feature_flags2?(features&VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT)
                           :storage_without_format_listed(tf)),
    "Texel format lacks formatless-write support");
```

The device log narrows it further: the **device feature is present and
enabled** on this Mali,

```
[nvn] vulkan: feature shaderStorageImageWriteWithoutFormat supported=1 enabled=1
[nvn] vulkan: created Vulkan device "Mali-G615 MC6", instance API 1.3
```

so the failing half of the condition is the **per-format** bit for the NVN
texel format 98 on this driver, not a missing device capability. The Honor
tablet (Adreno 735) runs the same code and reaches the title, which points
at a driver difference in the format feature bit.

**This file is not this task's file.** It is the engine line that the
coordinator and the engine workers own, and the fix was deliberately not
attempted here: changing the engine without a measured baseline on a device
that is currently the project's blocker would be a guess. The diagnosis is
recorded here so the engine owner can act on it.

### 4.7 The inherited defect `NVN GPU_INDEXED_FAIL` is still there

The new baselines `7229b02` (structural recognition of instance-only
indirect writes) and `d5a3340` (resident indirect argument guard) are
**ancestors of this task's baseline `db094f8`** — verified with
`git merge-base --is-ancestor` — so the 1.0.9 APK already carries them.
The defect nevertheless still occurs on the device:

```
[engine] NVN GPU_INDEXED_FAIL ind=0 pool_res=1 frame=5
[engine] NVN GPU_INDEXED_FAIL ind=0 pool_res=1 frame=6
[engine] NVN GPU_INDEXED_FAIL ind=0 pool_res=1 frame=7
```

5 occurrences, in the first frames. **The new baselines did not remove it.**
Reported as measured, not as fixed.

### 4.8 What is NOT proven on the device

- **Title screen: NOT reached.** The guest aborts at boot (section 4.6).
- **Battle: NOT reached.**
- **Gameplay frame rate: NOT measured.** The 30 FPS numbers are a boot
  window.
- **A 4:3 screenshot with game content: NOT obtained** (section 4.3).
- **Audio: NOT tested.**
- **Writing a cheat value and reloading it: NOT tested.**
- **Cold and warm cache rounds separately: NOT done.** Every run was a cold
  launch of the app; the pipeline cache warmed up over the runs (the guest
  reached the fault faster each time), but no warm run was measured on its
  own.

## 5. Host validation (run, passed)

| Command | Result |
|---|---|
| `bash scripts/android/test_device_profiles.sh` | exit 0 — "profile selection checks passed" |
| `python3 tests/device/test_profile_environment.py` | exit 0 — 4 cases OK (profile switch and external override, the handheld surface transform, the Adreno-only inline gate, and the 4:3 aspect contract) |
| `bash scripts/android/build_game_window_apk.sh` | exit 0 — all 18 steps exit code 0 |
| `apksigner verify --print-certs` on the artifact | signer `d11967a3…f36e`, equal to the RG477M's installed 1.0.8 |
| `aapt2 dump badging` on the artifact | versionCode 9, versionName 1.0.9 |
| `scripts/graphics/analyze_frame_metrics.py` on the device CSV | exit 0, JSON written; `acceptance_decision: NOT_EVALUATED` |

## 6. The same APK on the Honor tablet (same package regression)

The 1.0.9 APK carries the same certificate as the Honor 1.0.7, so it
installs there too. That run is **NOT_RUN** in this task: the Honor
tablet (`A42BBB5109000333`) is the main line's device and another worker
was using it, and this task was told not to take it over. The command, when
the tablet is free:

```bash
adb -s A42BBB5109000333 install -r \
  /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/apk/ffrs-game-window-1.0.9.apk
adb -s A42BBB5109000333 shell am start -W \
  -n org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity
adb -s A42BBB5109000333 logcat -d -s FFRS-GameWindow:I '*:S' \
  | grep -E "device_profile|surface_frame_rate"
adb -s A42BBB5109000333 shell "grep -m2 'vulkan present' \
  /sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log"
```

Expected there, and the reason the Honor behaviour was left alone: the
profile is `tablet_sd8s` (1080, 60), the operation mode is docked, the
surface frame rate declaration is **skipped**
(`tier_declares=false`, because the tablet tier keeps its own display
mode request), and the picture keeps its own aspect as before.

## 7. Risks and open items

1. **The guest FATAL at boot is the project's blocker on this device**, and
   it is in the engine line (section 4.6). Nothing in this task's line
   reaches it: the profile, the tier, the pillarbox, the frame rate
   declaration, the data root and the cheat all work on the device.
2. **`NVN GPU_INDEXED_FAIL` is still there** with the new baselines in the
   build (section 4.7).
3. **The frame rate declaration is unmeasured in gameplay.** The Android
   documentation is explicit that a frame rate request is a request, and
   the RG477M's system also carries a `frameRateOverride{frameRateHz=60.0}`
   for this uid. The marker `gfx-surface-frame-rate` is the A/B.
4. **The handheld keeps the RGBA_8888 surface.** The Honor line made the
   surface opaque for the tablet because the compositor blended the world's
   alpha away. The RG477M has always run the RGBA path, so whether it needs
   the same treatment is open; it cannot be judged while the guest dies
   before the first title frame. It was not changed here, because changing
   it without a picture on the device would be a guess.
5. **The truncated destination.** The fit truncates the destination size
   instead of rounding it, so a scaled frame can be one pixel short on the
   far edge. The handheld case is exact (the scale is 1.0). It was left as
   it is, because rounding would move the Honor tablet's picture by one
   pixel and that device's acceptance is already signed off.
6. **The 30 FPS floor is unchanged.** The engine still locks 30 FPS for the
   handheld profile and the frame pacing is untouched. 60 FPS on this
   hardware is not claimed.

## 8. Files

Changed in this task:

- `src/android/app/java/org/ffrs/diagnostic/DeviceProfile.java`
- `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`
- `src/graphics/nvn_device/nvn_backend_vulkan.c`
- `src/android/app/gamewindow/AndroidManifest.xml`
- `tests/device/DeviceProfileContract.java`
- `tests/device/test_profile_environment.py`

Added in this task:

- `coordination/results/RG477M-SYNC-1.0.9-OSS-NOTES.md`
- `coordination/results/RG477M-SYNC-1.0.9-RESULT.md`

Not touched: the RG477M main worktree's uncommitted work, `main`/`master`,
the Honor tree, the game data, the saves, `src/graphics/nvn_vulkan/engine/xbg_game.cpp`,
and every file owned by another writer. `coordination/state.json` and
`coordination/HANDOFF.md` are the coordinator's files and were left alone
for the same reason.

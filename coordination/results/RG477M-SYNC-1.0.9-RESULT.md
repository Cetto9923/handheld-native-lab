# RG477M-SYNC-1.0.9 — RESULT

One APK for both devices: the RG477M handheld (4:3, 1280x960, MT6897 /
Mali-G615 MC6) and the Honor tablet, which keeps its accepted behaviour.

- Date: 2026-11-12
- Branch: `task/T-rg477m-sync-20261112`
- Worktree: `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/rg477m-sync-20261112`
- Baseline: `db094f8` (the 1.0.7 packaging line: moon fix, indirect fix, cheat menu)
- Sources: see `coordination/results/RG477M-SYNC-1.0.9-OSS-NOTES.md`
- Device validation: **BLOCKED — the RG477M was not attached to adb during this
  session.** Nothing below claims a device result.

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
- The app log now says which tier was applied instead of `unapplied`:
  `device_profile id=handheld_mid ... render_tier=1280x720 render_short_edge=720
  operation_mode=handheld`.

**The existing profile mapping was not changed.** mt6897 / model RG477M →
`handheld_mid` (720, 30) as before; `tablet_sd8s` (1080, 60) and
`phone_sd8g3` (1080, 60) keep their tiers and their docked mode. One
reserved slot moves: `odin_elite` declares 1080 and therefore now uses the
docked mode, where it previously used the handheld one. `odin_elite` is a
reserved profile with no device (it is the planned Odin 3 slot), so no
hardware behaviour changes.

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
  it to be guessed: `aspect=preserve bars_top=120 bars_bottom=120 stretch=no`.
- A host contract compiles that exact function out of the production file and
  checks the 4:3 case: 1280x720 content on a 1280x960 surface lands at
  (0,120) with the size 1280x720, scale 1.0, the source shape kept, nothing
  cut away and nothing stretched. The docked tier (1920x1080) on the same
  panel is checked to stay whole, centred and unstretched.

On the RG477M panel (1280x960) with the handheld tier (1280x720 content)
the picture occupies 1280x720 with a 120 pixel black bar above and below —
what `docs/PROJECT.md` asks for. The game keeps its own proportions and its
own time: nothing in this change touches the frame pacing, the simulation
or the audio.

### 1.3 The target frame rate is declared to the platform

A 30 FPS profile asks for no display mode, so nothing told the platform
what the game produces. The app now declares it:
`Surface.setFrameRate(30, FRAME_RATE_COMPATIBILITY_DEFAULT,
CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS)` (the three argument form exists from
Android 12; Android 11 gets the two argument form, which the platform
documents as the same request). The engine's own limiter is untouched, so
the game time does not change.

- A display mode request still wins when there is one: the tablet and phone
  profiles keep their existing request untouched.
- The declaration is limited to the **handheld tier**. The Honor tablet
  currently runs with a 30 FPS marker on a 144 Hz panel; declaring 30 there
  would have changed the scheduling of a device whose 1.0.7 acceptance is
  signed off, and that is not this task's decision. The Honor behaviour is
  therefore bit-for-bit the same as in 1.0.7.
- A marker `gfx-surface-frame-rate` in the app's own file directory turns
  the declaration off, so both ways can be compared on the device without a
  new build. It holds "0" to turn it off; it is written with "1" on first
  start.

### 1.4 Data root and seed discovery: unchanged, and correct as it is

`resolveGameDir()` already resolves the data root in the order intent
override → managed root → autodiscovery, and no path was hardcoded. The
pipeline warm list is found relative to the resolved guest image
(`<image dir>/pipeline-warm-list.bin`, used only when it is readable), so
the seed follows the data root automatically. No change was needed and none
was made. The seed file exists on the RG477M side (905028 B, measured
before this session); whether the 1.0.9 build finds it is part of the
device run below.

### 1.5 Version

`versionCode` 7 → 9, `versionName` 1.0.7 → 1.0.9. One APK covers both
devices: the RG477M has code 8 installed and the Honor tablet code 7, and
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
(certificate `d11967a3…f36e`, verified against `installed-1.0.8.apk`).
The key lives in `.local/` and is not in git.

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

## 3. Host validation (run, passed)

| Command | Result |
|---|---|
| `bash scripts/android/test_device_profiles.sh` | exit 0 — "profile selection checks passed" |
| `python3 tests/device/test_profile_environment.py` | exit 0 — 4 cases OK (profile switch and external override, surface transform for the handheld path, the Adreno-only inline gate, and the new 4:3 aspect contract) |
| `bash scripts/android/build_game_window_apk.sh` | exit 0 — all 18 steps exit code 0 |
| `apksigner verify --print-certs` on the artifact | signer `d11967a3…f36e`, equal to the RG477M's installed 1.0.8 |
| `aapt2 dump badging` on the artifact | versionCode 9, versionName 1.0.9 |

These are host contracts and a build. They are not a device result, not a
frame rate result and not a picture result.

## 4. Device validation — BLOCKED

The RG477M (`RG477M01445122`) was **not attached to adb at any point in
this session**. `adb devices -l` listed only the Honor tablet
(`A42BBB5109000333`) at 22:40 and again at 23:02, `adb -s
RG477M01445122 get-state` answered "device not found", and the USB bus
enumeration showed no Anbernic device. The device was never queried, so no
state on it was changed.

**Therefore: title, battle, 30 FPS, the profile log, the present geometry and
the 4:3 picture are all NOT_RUN. No green is claimed anywhere.**

### 4.1 The exact commands to run when the device is back

```bash
WT=/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.worktrees/rg477m-sync-20261112
APK=/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/apk/ffrs-game-window-1.0.9.apk

# 0. the device must be online; do not kill the adb server
adb devices -l                       # expect RG477M01445122 in the list

# 1. install over 1.0.8 (same certificate, no data clear, no uninstall)
adb -s RG477M01445122 install -r "$APK"

# 2. launch and collect the app log, the guest transcript and the window state
FFRS_APK="$APK" FFRS_GUEST_WAIT_SECONDS=45 \
  bash "$WT/scripts/android/install_game_window_apk.sh" RG477M01445122

# 3. the profile tier that was applied
adb -s RG477M01445122 logcat -d -s FFRS-GameWindow:I '*:S' \
  | grep -E "device_profile|surface_frame_rate_declaration|surface_frame_rate_request"

# 4. the picture geometry: source size, destination, the black bars
adb -s RG477M01445122 shell \
  "grep 'vulkan present' /sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log | head -5"

# 5. the frame rate of the running game
adb -s RG477M01445122 shell \
  "grep -m1 'locked target_fps' /sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log"
adb -s RG477M01445122 shell "ls /sdcard/Android/data/org.ffrs.gamewindow/files/ | grep codex-frame"
adb -s RG477M01445122 pull /sdcard/Android/data/org.ffrs.gamewindow/files/codex-frame-<pid>.csv .
python3 "$WT/scripts/graphics/analyze_frame_metrics.py" codex-frame-<pid>.csv --label rg477m-1.0.9

# 6. the 4:3 picture: 1280x960 with the content in 1280x720
adb -s RG477M01445122 exec-out screencap -p > rg477m-1.0.9-4x3.png
sips -g pixelWidth -g pixelHeight rg477m-1.0.9-4x3.png

# 7. the data root and the warm seed
adb -s RG477M01445122 logcat -d -s FFRS-GameWindow:I '*:S' | grep -E "game_dir|resolveGameDir"
adb -s RG477M01445122 shell \
  "ls -l /sdcard/Android/data/org.ffrs.gamewindow/files/pipeline-warm-list.bin"
```

What to look for: the profile line must say `render_tier=1280x720
operation_mode=handheld`; the present line must say `dst=1280x720 at (0,120)`
with `bars_top=120 bars_bottom=120 stretch=no`; the frame rate declaration
must appear once (`surface_frame_rate_declaration: hz=30`). If the picture is
black except for the HUD, that is the known Honor alpha issue (the game
writes alpha 0 over the world; the Honor fix gates an opaque surface on the
docked profile). The RG477M keeps the handheld RGBA_8888 surface it has
always used, so the symptom would be pre-existing and not caused by this
change; the fix for it belongs in a separate, measured step.

### 4.2 A/B for the frame rate declaration, without a new build

```bash
# turn the declaration off
adb -s RG477M01445122 shell \
  "echo 0 > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-surface-frame-rate"
# turn it back on
adb -s RG477M01445122 shell \
  "echo 1 > /sdcard/Android/data/org.ffrs.gamewindow/files/gfx-surface-frame-rate"
```

## 5. Risks and open items

1. **The declaration is unmeasured on hardware.** The Android documentation
   is explicit that a frame rate request is a request, and that the
   scheduler does not promise the requested rate. The RG477M's system also
   carries a `frameRateOverride{frameRateHz=60.0}` for this uid. The
   request may change how the frames are placed, and the frame interval
   measurement in step 5 above is what decides whether it helps or hurts.
   The marker in section 4.2 is the A/B.
2. **The handheld keeps the RGBA_8888 surface.** The Honor line made the
   surface opaque for the tablet because the compositor blended the world's
   alpha away. The RG477M has always run the RGBA path, so whether it needs
   the same treatment is an open question that only a picture on the device
   can answer. It was not changed here, because changing it without a
   device would be a guess.
3. **The truncated destination.** The fit truncates the destination size
   instead of rounding it, so a scaled frame can be one pixel short on the
   far edge (the handheld case is exact: the scale is 1.0). This was left
   as it is, because rounding would move the Honor tablet's picture by one
   pixel and that device's acceptance is already signed off.
4. **The tier is a declaration until the device confirms it.** The tier now
   decides the operation mode, which is how the guest picks its render
   target. What the guest actually rendered is reported by the present log
   (`source=WxH`), and the two have to be compared on the device.
5. **The 30 FPS floor is unchanged.** The engine still locks 30 FPS for the
   handheld profile and the frame pacing is untouched. 60 FPS on this
   hardware is not claimed.

## 6. Files

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
the Honor tree, the game data, the saves, and every file owned by another
writer. `coordination/state.json` and `coordination/HANDOFF.md` are the
coordinator's files and were left alone for the same reason.

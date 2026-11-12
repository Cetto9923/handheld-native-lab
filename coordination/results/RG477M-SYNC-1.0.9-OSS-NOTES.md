# RG477M-SYNC-1.0.9 — OSS-NOTES

Scope: the outside sources that informed the 1.0.9 changes for the RG477M port
(resolution tier, 4:3 pillarbox, frame rate declaration).

Every entry says what was actually retrieved. A source that was not read is
not cited as if it had been, and nothing here was copied into the tree as
code.

- Date: 2026-11-12
- Branch: `task/T-rg477m-sync-20261112`
- Baseline: `db094f8` (the 1.0.7 packaging line: moon fix, indirect fix, cheat menu)

## 1. Unreal Engine 4.26 — Device Profiles

- URL: https://docs.unrealengine.com/4.26/zh-CN/SharingAndReleasing/DeviceProfiles
- Retrieved: **yes**, HTTP 200, full article body, 2026-11-12.
- Revision: the documentation is version-pinned by URL (4.26); the doc site
  publishes no commit hash for the page.
- License: Epic Games documentation, read as a reference. No Epic source
  code and no Epic asset was copied into this tree.
- Support scope actually used: the device-profile idea (one build, a profile
  per device) and the fractional scale factor as the way to ask for more
  pixels than the device needs, at a performance cost. Quoted from the page:
  "如希望获得较高分辨率但不愿运行原生分辨率造成性能损失，可使用1.5之类的
  小数进行获取", with the per-device file `Config/DefaultDeviceProfiles.ini`
  and `+CVars=r.MobileContentScaleFactor=2`.
- What it informed here: the profile carries a resolution tier, and the tier
  decides the operation mode the guest renders in.
- **Not used, and why:** no UE console variable was set from the host. This
  port has no `r.ScreenPercentage` or `r.MobileContentScaleFactor` path, so
  the UE scale-factor route could not be adopted here; the tier is applied
  through the operation mode instead (see the result report).

## 2. Android — Frame rate API guide

- URL: https://developer.android.com/media/optimize/performance/frame-rate
- Retrieved: **yes**, HTTP 200, full article body, 2026-11-12.
- License: Google developer documentation, read as a reference.
- Support scope actually used, quoted from the page:
  - "a game that intends to not run higher than 60Hz (to reduce power usage
    and achieve longer play sessions) can call
    `Surface.setFrameRate(60, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)`"
  - "`FRAME_RATE_COMPATIBILITY_FIXED_SOURCE` is intended only for video apps.
    For non-video usage, use `FRAME_RATE_COMPATIBILITY_DEFAULT`."
  - "The scheduler takes a number of factors into consideration in deciding
    on the display refresh rate, so there is no guarantee that your app will
    get the frame rate you request with `setFrameRate()`."
  - "even if the device only supports 60Hz, call `setFrameRate()` with the
    frame rate your app prefers."
  - `CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS` is the strategy to use when the
    app does not want a mode switch it cannot hide.
- What it informed here: the 30 FPS handheld profile declares 30 to the
  platform with `FRAME_RATE_COMPATIBILITY_DEFAULT` and
  `CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS`. `FIXED_SOURCE` is not used, because
  this is a game and not a video source.

## 3. Android — `Surface` reference

- URL: https://developer.android.com/reference/android/view/Surface
- Retrieved: **yes**, HTTP 200, 2026-11-12. The reference body is rendered by
  script, so the fetched page carries the shell rather than the prose; the
  signatures the code depends on were read from the local platform jar
  `android-34/android.jar` with `javap`, which reports
  `public void setFrameRate(float, int, int)` and
  `public void setFrameRate(float, int)` — **both return void** — and the
  three argument form is the API 31 one.
- License: Google developer documentation, read as a reference.
- What it informed here: the declaration reads no return value (there is
  none) and the three argument call is guarded by `SDK_INT >= 31`, with the
  two argument form on API 30.

## 4. PCGamingWiki — Glossary: Scaling

- URL: https://www.pcgamingwiki.com/wiki/Glossary:Scaling
- Retrieved: **no.** A direct read returns HTTP 403 (Cloudflare challenge)
  for `/wiki/Scaling`, `/wiki/Glossary:Scaling`, the `action=raw` form
  and the `api.php` form; the Wayback availability endpoint was unreachable
  from here as well. The text below was obtained through a search index of
  the same page, **not** through a direct read of the page, and that is
  stated here rather than dressed up as a read.
- License: PCGamingWiki text is published under CC BY-NC-SA 4.0 by the
  wiki's general terms; that footer could not be read in this environment
  (403), so the license line is reported from the wiki's known terms and is
  **not verified here**.
- Support scope actually used: terminology only — the difference between
  maintaining the aspect ratio and stretching the output. Page text as
  obtained: "Older games from the 90s and early 2000s are typically designed
  for a 4:3 aspect ratio, while newer games are typically designed for a
  16:9 aspect ratio", and on full-screen scaling "In this mode the output
  stretches to fit the monitor, often with unwanted results (e.g. fat
  characters)".
- What it informed here: the vocabulary of the result report (pillarbox vs
  stretch). No implementation and no text was copied into the tree.

## 5. Skyline emulator

- URL: https://github.com/skyline-emu/skyline
- Retrieved: **yes**, but only the repository root, 2026-11-12, HTTP 200 via
  the GitHub contents API on branch `ftx1`: the root holds **`README.md`
  only** (2831 bytes). The source code is gone; there is nothing to read.
- License: the project was MPL-2.0.
- Support scope actually used: **none.** It is a paradigm reference only (a
  native re-implementation that carries its own device profile tiers), and
  because the implementation is no longer published, no implementation of it
  may be cited. No Skyline code, configuration or asset was copied into this
  tree.

## 6. AetherSX2

- URL: https://github.com/THEGAMER00/AetherSX2
- Retrieved: **no**, HTTP 404 (GitHub API, 2026-11-12). The repository does
  not exist under that path.
- Support scope actually used: **none.** Not citable, not used.

## 7. Pinned build inputs reused by this build

These are not new sources; they are the pinned inputs the 1.0.9 build
consumed, recorded here so the artifact is reproducible.

| Input | Location | Revision / digest | License |
|---|---|---|---|
| d2r-mobile shader-optimizer subset (optional runtime inline wrapper, Adreno-only gate) | `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/third-party/d2r-mobile-f1e4b08` | commit `f1e4b081af8ea65d95f0bf590a568cb657c3917e`, clean checkout | MIT (see the bundled `licenses/ffrs-subgroup-inline` assets) |
| Khronos SPIRV-Tools / SPIRV-Headers | inside the same checkout | as pinned by `scripts/android/prepare_honor_spirv_tools.sh` | Apache-2.0 / Apache-2.0 |
| Local development signing key | `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/signing/gamewindow-debug.p12` (staged read-only into this worktree's `.local/`) | certificate SHA-256 `d11967a31fcef92f3d6b44adf9d9678c8d2b07bf8f3246d51a791617ed61f36e` — the same certificate the RG477M's installed 1.0.8 carries | local development key, not a release credential |

The save key used by the cheat menu stays in `.local/` and is not in git.

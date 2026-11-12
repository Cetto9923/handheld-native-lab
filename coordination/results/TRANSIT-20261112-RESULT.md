# TRANSIT 2026-11-12: the map transition's 30 FPS, re-attributed

Worktree: `.worktrees/transit-20261112` (branch `task/T-transit-20261112`, base `e7ddc44`).
Scope: the map transition (入图过场) on the Honor tablet A42BBB5109000333, without changing the game's logic time.

## 1. What this task found, in one paragraph

The frame-time spike in the transition is **not** the deferred-event path, and in the run measured here it is **not** the
warm-up either. It is the **CPU blocking inside `vkQueueSubmit`**, together with the **readback drains** the engine
performs several times per frame in that scene. The background pipeline builds (both the list prewarm and the async
builds) do not correlate with the slow frames at all: the session measured here had `warm list=0`, so the prewarm did
nothing, and the slow windows still appeared. The deferred-event retirement this task implemented stays in the tree as a
dormant, tested lever, because the deferral itself is off by decision.

## 2. The re-attribution, with numbers

Source: the device's own `guest_boot.log` (pulled read-only; 86498 lines, one boot, 123 perf windows of 60 frames).
Every number below is from the engine's own `NVN PERF` lines, not from a screen recording.

Averages over the windows that were slower than 2100 ms (28 windows) against the windows that were not (95 windows):

| counter | slow windows | fast windows | reading |
|---|---|---|---|
| `submit_cpu_ms` (time inside `vkQueueSubmit`) | **24.35** | 13.22 | the dominant cost of a slow frame |
| `gpu_ms` (the GPU's own timeline) | 15.43 | 16.82 | the GPU is **not** busier in a slow frame |
| `async_built` (background pipeline builds) | 15.68 | 14.37 | the background builds do **not** correlate |
| `warm_built` / `warm_ms` (the list prewarm) | 0 / 0 | 0 / 0 | this run had no prewarm at all |
| `created` (pipelines the game thread built) | 10.68 | 0.38 | new pipelines appear in the slow windows |
| `create_ms` (their build time) | 4.26 | 0.17 | the game's own pipeline cost, small |
| `sync_rb` (readback drains) | 5.04 | 0.02 | the readback drains appear only in the slow windows |

The transition window itself (window 3) reads `wall_ms=3433.7 fps=17.47 submit_cpu_ms=28.51 sync_rb=7`, and the two
worst windows of the run read `submit_cpu_ms=42.75` (with `created=56`) and `submit_cpu_ms=49.71/51.33` (with
`sync_rb=18/24` and no pipeline creation at all).

What that rules out, with the data of this run:

- **The warm-up list prewarm is not the cause of the spikes in this run.** `NVN warm list=0 enabled=1`,
  `warm_built=0`, `warm_ms=0` for all 123 windows, and the spikes are there anyway.
- **The background async builds are not the cause.** `async_built` is the same in slow and fast windows.
- **The GPU is not the bottleneck of these frames.** `gpu_ms` is flat.
- **The deferred events are not the cause of these spikes.** This run had `NVN defer events=0`, and the spikes are there.

## 3. The two questions the total control asked

**Does entering a map trigger a concentrated prewarm?** No. `warm::start()` runs once, at engine init
(`xbg_game.cpp:6582` area, `warm::start()`), and the worker threads chew through the whole list from there
(`warm::worker()`: three threads, `next.fetch_add(1)`, no pacing at all). Nothing in the transition path reloads or
restarts the list; `warm::flush()` only *appends* to the list file at shutdown, so the list grows from session to
session and every boot rebuilds all of it. So the prewarm is a **boot-time burst**, and it overlaps the transition only
when the transition happens early in the session (the tutorial's first map). The one mitigation that was there,
`setpriority(PRIO_PROCESS,0,5)`, lowers the thread's priority in the **scheduler**, while the contention is in the
**driver** (the pipeline builds land in the same driver the game submits into) - it lowers the wrong resource.

**Is `warm::take()`'s `if(reuse_rejected) return false;` currently armed?** No. `reuse_rejected` is set in exactly two
places: `warm::reject_reuse()`, which is only reached from the shader-reuse *comparison* path (the marker
`gfx-shader-reuse=2`), and `warm::take_rewritten()` on an input mismatch (which logs "NVN shader reuse INPUT MISMATCH").
On the device the marker `gfx-shader-reuse` holds `0`, so `optimize_runtime_shader()` short-circuits before
`take_rewritten()` is ever called, the log has **0** "INPUT MISMATCH" lines, and `reuse_rejected` is therefore false:
the pipeline reuse is not disabled, and it is not the cause of the repeated creations.

**One correction to the evidence that was handed over:** the cold-start window `wall_ms=9351.2` and the warm counters
(`warm list=1560`, `warm_built=1560`, `warm_ms=7010`) come from **two different runs**. The log on the device right now
reports the same cold-start window (`wall_ms=9351.2`) with `warm list=0`, and the warm counters of that run are all
zero. The warm numbers are therefore not evidence about the cold-start window; they belong to a run whose own cold-start
window has to be re-read before they are used.

## 4. What was implemented

1. **The retirement rule for a deferred event** (the direction the previous investigation recommended; the question of
   whether it holds was re-derived from the code first).
   - New production header `src/graphics/nvn_vulkan/engine/xbg_event_retire.h`: the newest signal to an address decides
     the submission that has to retire before the guest may read that value.
   - `xbg_game.cpp`: `retire_through()` retires the submissions in order up to the one that carries the event, and
     `publish_event_value()` replaces the old full drain (`complete_all()`) at the guest's read point and at the
     command-buffer wait. The submissions queued after the event stay in flight.
   - The value the guest reads is unchanged: every signal the guest already issued to that address is published, so the
     guest sees the value it signalled, as it would without the deferral.
   - With the deferral off (the shipped default, and the user's decision of 2026-11-12 22:56) the change is a **no-op**:
     no event is pending, so nothing is retired. It is a dormant lever, ready for the case where the deferral is used
     again.
2. **The warm-up's share of the driver** (the lever the total control named). Two markers, both off by default:
   - `gfx-warm-budget` = the most pipelines the warm threads may build per engine frame (0 = the old unlimited burst).
   - `gfx-warm-interval` = the pause after every build, in milliseconds.
   With the budget the warm thread now waits for the game's own frames instead of running a continuous burst against the
   driver. The existing `gfx-no-warm` marker (the prewarm off) is the third arm.
3. **The measurement**: `scripts/device/transit_ab.sh` replays one recorded path over several arms on the same save,
   takes the transition window from the device clock at the path's `TRANSITION_START` / `TRANSITION_END` notes, and
   reads the frames from the buffered `codex-frame-<pid>.csv`. It never records the screen for timing.

## 5. Validation that was actually run

| check | result |
|---|---|
| `scripts/runtime/selfproof_xbg_event_retire.sh` | **PASS** - baseline exit 0, mutated header (answers the oldest signal) exit 1. The contract is not vacuous. |
| `tests/native/xbg_event_retire_contract.cpp` | 2000 randomized lists plus the hand-written cases agree with an independent model. |
| NDK compile of `xbg_game.cpp` for aarch64 (android28) | the only error is the pre-existing `honor_inline_enabled` one, which the base commit `e7ddc44` has as well (verified by compiling the base file). |
| APK build `FFRS_APK_NAME=FFRS-Transit-20261112.apk` | exit 0, SHA-256 `5d9d258e99de1f1fabddaeb937b061995a7d14ab90dedec881f32940a51463cd` (built before the warm-throttle change; it has to be rebuilt before it is installed). |
| device log analysis | read-only; the numbers in section 2 come from the device's own perf lines. |

## 6. The device A/B: NOT_RUN

The tablet was reserved for the effects worker for the whole time this task was active (the app's pid changed twice
under it), so no arm was run and no APK was installed. Nothing was pushed to the device.

To run it once the device is free:

```bash
cd <worktree>
FFRS_APK_NAME=FFRS-Transit-20261112.apk bash scripts/android/build_game_window_apk.sh
adb -s A42BBB5109000333 install -r .local/android-apk/FFRS-Transit-20261112.apk
bash scripts/device/transit_ab.sh --path <path-file> --out .local/transit-ab/run1 --arms warm-plain,warm-off,warm-budget
# and the cold run of the same arms:
bash scripts/device/transit_ab.sh --path <path-file> --out .local/transit-ab/run2 --arms warm-plain,warm-off,warm-budget
```

The path file is a list of steps (`key KEYCODE`, `sleep SECONDS`, `note LABEL`); `TRANSITION_START` and
`TRANSITION_END` bound the window. The same save, the same path and the same operation have to be used in every arm,
and the cold and the warm round have to be reported separately.

The acceptance remains: **the transition at ≥30 FPS, and the transition's wall-clock length within 5% of the
defer=0 reference**, measured on the transition segment only, with the warm-up spikes and the steady state counted
apart.

## 7. What is left, and for whom

The measured evidence points the next step at the two costs that actually correlate with the slow frames:

- `submit_cpu_ms` of 28-51 ms per frame: the CPU blocks inside `vkQueueSubmit`. Whether that is the driver waiting
  for a free command-buffer slot (queue back-pressure) or for the pipeline compiler is not answered yet; it needs a
  driver-side trace, not a host change.
- `sync_rb` of 7-24 per frame: every readback calls `gpu_sync()` (`xbg_game.cpp`, `void gpu_sync(uint64_t &reason)`
  -> `submit_slots(true)`), which retires **every** submission and waits for all fences. That is the same class of
  over-wait as the deferred-event path, on the readback path, and it serialises the CPU against the GPU once per
  readback. The retirement rule this task implemented for events is the shape the readback path would need as well:
  wait for the submission that owns the readback, not for the whole queue.

Both of those are outside this task's write scope (the engine file is shared with the effects worker), so they are
recorded here rather than changed.

## 8. Files

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp` - the retirement rule, the warm-up throttle, the perf counters.
- `src/graphics/nvn_vulkan/engine/xbg_event_retire.h` - the rule, as production code the host test drives.
- `tests/native/xbg_event_retire_contract.cpp` - the contract of the rule.
- `scripts/runtime/selfproof_xbg_event_retire.sh` - the self-proof (baseline green, mutation red).
- `scripts/device/transit_ab.sh` - the paired device A/B.
- `coordination/results/TRANSIT-20261112-OSS-NOTES.md` - the sources, with what was really opened.

## 9. The merge list (for the serial integration)

My worktree is `.worktrees/transit-20261112`, branch `task/T-transit-20261112`, base `e7ddc44`, HEAD `97d1a4f`
(two commits: the engine change, and the tracked metrics tool). The engine file `src/graphics/nvn_vulkan/engine/xbg_game.cpp`
is touched in the following places, all of them in the anonymous namespace of that file, all of them free of the effects
worker's areas (shader seeds, pipeline cache):

| place (base line) | what | why |
|---|---|---|
| includes, after `xbg_indirect_template.h` (line 42) | `#include "xbg_event_retire.h"` | the rule, as production code |
| `struct PendingEvent` (line 620) | one more field `uint64_t order`, plus `event_order`, `event_signals`, `event_targeted`, `event_slots_targeted`, `event_fallback`, `event_retire_targeted` | the order the signals were issued in, and the counters |
| after `retire_slot()` (line 1067) | new `retire_through()` | retires the submissions up to the one that carries the event |
| `perf_frame()` (line 1265) | the `NVN PERF events` line gained `targeted=`, `slots=`, `fallback=` | the counters have to be visible on the device |
| after `complete_all()` (line 2043) | new `publish_event_value()` | the read point retires only what the event needs |
| `warm::worker()` (line 3034) | the body is paced by `warm::budget_per_frame` / `warm::interval_us` | the warm-up against the game's own frames |
| `OP_SIGNAL_EVENT` (line 6106) | the two `push_back` calls carry `++event_order` | the order of the signals |
| `XBG_OP_EXTENSION_BASE+4` (line 6232) | `complete_all()` -> `publish_event_value()`, with the old drain as the fallback | the command-buffer wait |
| `xbg_game_initialize()` (line 6513) | reads the marker `gfx-event-retire` | the A/B switch of the rule |
| `xbg_game_initialize()` (line 6542) | reads the markers `gfx-warm-budget` and `gfx-warm-interval` before `warm::start()` | the A/B switches of the warm-up |
| `xbg_game_event_read()` (line 6613) | `complete_all()` -> `publish_event_value()` | the guest's read |

Nothing else in the file changed. `AndroidManifest.xml` was **not** touched (the version number is the total control's).

The APK with this tree and the warm-up throttle: `FFRS-Transit-20261112.apk`, SHA-256
`2be0cd631abae669ffa1404983ca9662695c40e2527d6ecfbea17d7961d20b1c` (built from `97d1a4f`).


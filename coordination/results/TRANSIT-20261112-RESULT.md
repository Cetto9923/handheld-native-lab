# TRANSIT 2026-11-12: the map transition's 30 FPS, re-attributed

Worktree: `.worktrees/transit-20261112` (branch `task/T-transit-20261112`, base `e7ddc44`).
Scope: the map transition (入图过场) on the Honor tablet A42BBB5109000333, without changing the game's logic time.

> **This file covers two rounds.** Sections 1-8 are the first round (the deferred-event retirement and the warm-up
> throttle). Section 9 is the merge list and section 10 is the second round: the **readback's targeted retirement**,
> which is what the total control asked for after the first round's attribution. The tablet belongs to the user since
> 2026-11-12 23:12, so everything in the second round is offline: the implementation, the host proof, the NDK check,
> the APK, the scripts - and the device A/B is written down, not run.
>
> **The headline of the second round, from the device's own counters:** the readback drain's *blocking* is 1.8-3.9 ms
> per frame in the transition windows, while the CPU blocks **22-37 ms per frame inside `vkQueueSubmit`** and the GPU
> is mostly idle (5-16 ms of work per 60 ms frame). The over-wait this change removes is real, but it is a small part of
> the frame; the change cannot by itself reach 30 FPS, and section 10.4 says what the remaining lever is.

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

The second round implemented the readback half of this (section 10). The first half - the blocking inside
`vkQueueSubmit` - is untouched by that change, and section 10.4 compares the two with the device's own counters: the
readback drain blocks 1.8-3.9 ms per frame, the submit blocks 22-37 ms per frame. The submit path is therefore the
next lever, and it is not in this task's write scope.

## 8. Files

First round:

- `src/graphics/nvn_vulkan/engine/xbg_game.cpp` - the retirement rule, the warm-up throttle, the perf counters.
- `src/graphics/nvn_vulkan/engine/xbg_event_retire.h` - the rule, as production code the host test drives.
- `tests/native/xbg_event_retire_contract.cpp` - the contract of the rule.
- `scripts/runtime/selfproof_xbg_event_retire.sh` - the self-proof (baseline green, mutation red).
- `scripts/device/transit_ab.sh` - the paired device A/B.
- `coordination/results/TRANSIT-20261112-OSS-NOTES.md` - the sources, with what was really opened.

Second round:

- `src/graphics/nvn_vulkan/engine/xbg_readback_retire.h` - the readback's rule, as production code the host test drives.
- `src/graphics/nvn_vulkan/engine/xbg_game.cpp` - `readback_sync()`, the marker, the perf counters (the same file).
- `tests/native/xbg_readback_retire_contract.cpp` - the contract of the readback's rule.
- `scripts/runtime/selfproof_xbg_readback_retire.sh` - the self-proof (baseline green, two mutations red).
- `scripts/graphics/analyze_stalls.py` - the acceptance metrics, plus the 60-frame window comparison.
- `scripts/device/transit_ab.sh` - the readback arms (`rb-drain`, `rb-target`), the perf-window analysis, the marker restore.

## 9. The merge list (for the serial integration)

My worktree is `.worktrees/transit-20261112`, branch `task/T-transit-20261112`, base `e7ddc44`. The engine file
`src/graphics/nvn_vulkan/engine/xbg_game.cpp` is touched in the following places, all of them in the anonymous namespace
of that file, all of them free of the effects worker's areas (shader seeds, pipeline cache). **First round** (the line
numbers are the ones of the base `e7ddc44`):

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

### 9.1 The second round's merge list (the readback retirement)

The same file, the same worktree and branch. The line numbers below are the ones of the current HEAD of this branch
(`585bf28` + the commits of this round); the "what" column says what the line is now, the "why" column says why it is
there. Every hunk is a separate, self-contained edit, so the three worktrees can be merged in any order.

| place (line in this branch) | what | why |
|---|---|---|
| includes, after `xbg_event_retire.h` (line 44) | `#include "xbg_readback_retire.h"` | the rule, as production code |
| `struct Perf` (line 917) | five more fields: `readback_targeted`, `readback_no_wait`, `readback_fallback`, `readback_inflight`, `sync_drain` | the counters of the round, per window |
| `perf_frame()` (line 1292) | a new line `NVN PERF readback targeted= nowait= fallback= inflight= drain=` | the counters have to be visible on the device; **the main `NVN PERF v=1` line is not touched**, so `submit_cpu_ms` / `sync_rb` / `gpu_ms` / `created` stay comparable with the baseline |
| after `gpu_sync()` (lines 1346-1390) | new `bool readback_retire_targeted`, new `retire_completed_slots()`, new `readback_sync()` | the readback waits for the submission that owns it |
| `capture_image()` (line 1903) | `gpu_sync(perf.sync_readback)` -> `readback_sync(perf.sync_readback)` | the capture's readback |
| `copy_texture_buffer()` (line 5183) | `++perf.sync_drain;` before the old `gpu_sync(perf.sync_readback)` | the 64 MB command-pool drain is **not** a readback: it needs every submission retired, so it keeps the full drain and is counted apart |
| `copy_texture_buffer()` (lines 5246, 5275, 5333) | `gpu_sync(...)` -> `readback_sync(perf.sync_readback)` (the depth, the BC and the colour readback) | the guest's texture readbacks |
| the RG16 proxy probe (line 6481) | `gpu_sync(...)` -> `readback_sync(perf.sync_readback)` | the probe's readback |
| `xbg_game_initialize()` (line 6622) | reads the marker `gfx-readback-retire` (default 1) | the A/B switch and the rollback |

The new files (no conflict with the other worktrees): `src/graphics/nvn_vulkan/engine/xbg_readback_retire.h`,
`tests/native/xbg_readback_retire_contract.cpp`, `scripts/runtime/selfproof_xbg_readback_retire.sh`.

`AndroidManifest.xml` was **not** touched in this round either.

The APK with this round's tree: `FFRS-Transit-Readback-20261112.apk`, SHA-256
`41332adb9f9098d1cc131550d05a6a6afb2a00e85862ff210a9994d0379d39a7`, 3535868 bytes (built from this branch's HEAD).
**It is not merged and not installed**: it only proves that the tree builds.


## 10. The second round: the readback's targeted retirement

### 10.1 The design: which submission owns a readback, and where the wait ends

A readback copies a GPU result into the guest's memory. In this engine the copy and the buffer it writes belong to
**one** slot:

- `readback_buffer()` allocates the destination in `recording_slot().readback` - the slot that is recording,
- `Commands c;` records the copy into `recording_slot().cmd` - the same slot.

So the submission that carries the readback's copy is the submission of that slot, and **the guest's bytes are final
when that submission's fence has signalled**. The boundary of the wait is therefore one fence:

- the owner is the slot that carries the copy;
- the host waits for the owner's fence and for nothing else;
- a submission that is not in flight needs no wait at all - its data is final;
- the state of the other submissions never changes the answer.

The rule is production code, not a comment: `src/graphics/nvn_vulkan/engine/xbg_readback_retire.h`
(`xbg_readback_decide()`), and the engine calls it for every readback.

### 10.2 What the engine does now

`readback_sync()` (in `xbg_game.cpp`, after `gpu_sync()`):

1. the owner is the slot that is recording; a recording slot is submitted here, so that it can be waited for;
2. the rule decides: the owner is in flight -> wait; the owner already retired -> **no wait at all**, the data is final;
   no owner -> the old full drain (the fallback);
3. the submissions that have **completed** are retired without waiting for them (`retire_completed_slots()`: the fence
   is queried first, the order of the retirements is kept, the ones still in flight stay in flight). Their retirement
   work overlaps the wait for the owner;
4. the wait: the owner's fence, and nothing else;
5. the rest of the completed submissions are retired.

The marker `gfx-readback-retire`:

| value | behaviour |
|---|---|
| **1 (the default, the shipped behaviour of this change)** | the readback waits for the submission that owns it |
| 0 | the old path: `submit_slots(true)`, the drain of every submission in flight (the rollback, and the A/B's reference arm) |

The counters (a new perf line, `NVN PERF readback ...`):

| counter | meaning |
|---|---|
| `targeted` | the readbacks that waited for their owner |
| `nowait` | the readbacks whose owner had already retired: no wait at all |
| `fallback` | the readbacks that fell back to the full drain |
| `inflight` | the submissions the old path would have drained at those readbacks - the over-wait, counted |
| `drain` | the full drains that are **not** readbacks (the 64 MB command-pool drain), counted apart |

The main `NVN PERF v=1` line is **not** touched: `submit_cpu_ms`, `sync_rb`, `gpu_ms` and `created` stay
comparable with the baseline of section 2.

### 10.3 What the first round's two levers do at defer=0

- **The event's targeted retirement** (`gfx-event-retire`): at `defer=0` it is a **no-op**. With the deferral off, no
  signal is pending, `publish_event_value()` finds no signal to an address and returns without retiring anything; the
  old `complete_all()` is not called either. It stays in the tree as a dormant, tested lever for the case where the
  deferral is used again.
- **`gfx-warm-budget` / `gfx-warm-interval`**: these are **not** no-ops. The list prewarm (1560 pipelines in the run
  that measured it) is built at the start of the session, and the markers pace those builds against the game's own
  frames instead of letting the warm threads run a continuous burst against the driver. With the markers unset (0) the
  behaviour is exactly as before. They matter for the **cold start**, not for the steady state and not for the
  transition once the warm-up is done. In the session this round analysed on the device the prewarm list was empty
  (`NVN warm list=0`), so the warm-up did nothing in that run at all.

### 10.4 What the device's own counters say about the size of this lever

The tablet is the user's since 2026-11-12 23:12, so nothing was run on it. The numbers below come from a device log of
another session that was already on the machine (the effects worker's run, read-only, never touched): 126 perf windows
of 60 frames, 12 of them with readbacks in them. The new analyzer
(`scripts/graphics/analyze_stalls.py --perf-log`) reads it and reproduces the first round's attribution on this second
session:

| counter | slow windows (> 2100 ms) | fast windows | baseline of section 2 |
|---|---|---|---|
| `submit_cpu_ms` | **23.12** | 12.74 | 24.35 / 13.22 |
| `sync_rb` | **4.35** | 0.02 | 5.04 / 0.02 |
| `gpu_ms` | 15.95 | 17.17 | 15.43 / 16.82 |
| `created` | 13.7 | 0.14 | 10.68 / 0.38 |

The per-window detail of the transition windows of that run, and it is the decisive measurement of this round:

| window | wall_ms | fps | sync_rb | submit_cpu_ms | gpu_ms | **the drain's own blocking** (`gpu_sync=ms/calls`) | the guest's fence wait (`legacy_wait`) |
|---|---|---|---|---|---|---|---|
| 1 | 4228.9 | 14.19 | 5 | 22.47 | 7.32 | 0.40 / 0.08 | 8.33 / 2.37 |
| 2 | 3938.1 | 15.24 | 20 | 35.73 | 8.34 | **3.85 / 0.33** | 6.52 / 2.32 |
| 3 | 3784.0 | 15.86 | 18 | 37.73 | 15.64 | 3.11 / 0.33 | 12.90 / 2.60 |
| 4 | 3778.3 | 15.88 | 24 | 32.66 | 5.87 | 3.21 / 0.42 | 4.56 / 2.05 |
| 5 | 3403.4 | 17.63 | 16 | 28.78 | 12.41 | 2.33 / 0.27 | 8.69 / 2.88 |
| 6 | 3385.9 | 17.72 | 7 | 27.17 | 12.66 | 1.79 / 0.12 | 10.32 / 3.12 |

What that says, plainly:

1. **The readback drain's blocking is 1.8-3.9 ms per frame**, and it happens in **0.33 blocking drains per frame** out
   of 7-24 readbacks. The other readbacks per frame found their fence already signalled: their cost was the drain's
   CPU work, not a wait.
2. **The CPU blocks 22-37 ms per frame inside `vkQueueSubmit`** in the very same windows, and the GPU is mostly idle
   there (`gpu_ms` 5.9-15.6 ms per 60 ms frame). The GPU is not the bottleneck of these frames and the readback
   drain is not either.
3. **The retirement work is 0.9-1.9 ms per frame** (`cleanup_ms_per_frame`: queries 0.03, writebacks 0.6-0.7,
   views 0.1-0.2, retire 0.2-0.8, arena pools 0.1-0.9).

So the readback path's total cost in the worst window is about **4-6 ms of a 63-70 ms frame**. The over-wait this
change removes is real, but it is a small part of the frame, and **this change alone cannot bring the transition to
30 FPS**. The structural reason is in the code: the copy is recorded into the slot that is being recorded, so the
owner's submission is the **newest** one, the GPU reaches it last, and the wait is bounded by the queue depth - the
older submissions' fences were mostly signalled already, which is why only 0.33 drains per frame blocked at all.

**What the next lever is, for the total control to decide:**

- the submit path: `submit_by other=5.78-8.88` plus `present=1.00` submissions per frame, with the CPU blocked
  22-37 ms inside `vkQueueSubmit` while the GPU is idle. Fewer submissions per frame, or a shallower queue, is where
  that time is;
- the pipeline creation: `created` 13.7 per frame in the slow windows against 0.14 in the fast ones (and 10.68 against
  0.38 in the baseline of section 2), with `create_ms` 4.26 in the first round's data;
- the number of readbacks the guest asks for in the transition (7-24 per frame): each one is a submission and a wait.

Both are outside this task's write scope, and the total control owns the decision about which one to take next.

### 10.5 The validation that was actually run (all offline)

| check | result |
|---|---|
| `scripts/runtime/selfproof_xbg_readback_retire.sh` | **PASS** - the baseline (the production header) exits 0; the two mutations (a rule that waits for the newest submission in flight, a rule that always waits) exit 1. The contract is not vacuous. |
| `tests/native/xbg_readback_retire_contract.cpp` | 2000 randomised rings plus the hand-written cases agree with a model that is given the owner's own state only, and the "the other submissions do not change the answer" property is checked over every owner and both owner states. |
| NDK compile of `xbg_game.cpp` for aarch64 (android28, `XBG_HAVE_SPIRV_OPT=1`, the build's own flags) | **0 errors**, and the 14 warnings are the same set as the base commit's (the normalised diff of the warning lists is empty). The direct compile without `XBG_HAVE_SPIRV_OPT=1` reports one error, `use of undeclared identifier 'honor_inline_enabled'`, and the base file reports the same one at its own line - it is pre-existing and is not touched here. |
| APK build `FFRS_APK_NAME=FFRS-Transit-Readback-20261112.apk` | exit 0, `.local/android-apk/FFRS-Transit-Readback-20261112.apk`, SHA-256 `41332adb9f9098d1cc131550d05a6a6afb2a00e85862ff210a9994d0379d39a7`, 3535868 bytes. **Not installed, not merged**: it only proves the tree builds. |
| `scripts/graphics/analyze_stalls.py --perf-log` | reads a real device log (126 windows) and reproduces the baseline of section 2 on that second session (table above). The CSV mode of the script is unchanged. |
| the device | **not touched**. The tablet belongs to the user since 2026-11-12 23:12. |

### 10.6 The device A/B: NOT_RUN, and the exact commands for after the merge

The A/B has to run on the merged 1.0.9 package, because a single line of this task measures the old parts of the
engine. Nothing of the following was executed.

```bash
# 0. build the merged package (the total control's tree, not this worktree)
cd <merged-worktree>
FFRS_APK_NAME=FFRS-1.0.9.apk bash scripts/android/build_game_window_apk.sh
adb -s A42BBB5109000333 install -r .local/android-apk/FFRS-1.0.9.apk

# 1. the readback arms, cold round then warm round, same save, same path, same operation
cd <merged-worktree>
bash scripts/device/transit_ab.sh --path <path-file> --out .local/transit-ab/readback-cold --arms rb-drain,rb-target
bash scripts/device/transit_ab.sh --path <path-file> --out .local/transit-ab/readback-warm --arms rb-drain,rb-target --warm

# 2. the metrics of the same run, with the same yardstick as the baseline
python3 scripts/graphics/analyze_stalls.py --perf-log .local/transit-ab/readback-cold/rb-target-cold-guest_boot.log \
    --json .local/transit-ab/readback-cold/rb-target-perf-windows.json
python3 scripts/graphics/analyze_stalls.py .local/transit-ab/readback-cold/rb-target-cold-frame.csv \
    --baseline --json .local/transit-ab/readback-cold/rb-target-stalls.json

# 3. what the change itself did, from the engine's own counters
grep -E "NVN readback retirement|NVN PERF readback" .local/transit-ab/readback-cold/rb-target-cold-guest_boot.log | head
```

The path file is a list of steps (`key KEYCODE`, `sleep SECONDS`, `note LABEL`); `TRANSITION_START` and
`TRANSITION_END` bound the window. The script restores the markers at the end (it removes them, which is the shipped
default state), pulls `codex-frame-<pid>.csv` and `guest_boot.log` per arm, and writes the stall metrics, the perf
window comparison and a summary per arm.

What to read out of it:

- the acceptance: the transition segment at **>= 30 FPS**, and its wall-clock length **within 5% of the defer=0
  reference** - measured on the transition segment only;
- the change's own counters: `targeted` (the readbacks that waited for their owner), `nowait`, `inflight` (the
  submissions the old path would have drained) and the `gpu_sync=` line of `NVN PERF block_ms` (the drain's own
  blocking, the number this change is supposed to move);
- the acceptance metrics of the whole run: `stalls_over_100ms`, `stall_time_s`, `stall_share_pct`,
  `median_gap_ms` against the baseline of section 2 (93 / 40.1 s / 13.6% / 33.34 ms).

The rollback, if the merged build misbehaves: write `0` into `gfx-readback-retire` in the app's files directory and
restart the app - the readback goes back to the old full drain, with no rebuild.

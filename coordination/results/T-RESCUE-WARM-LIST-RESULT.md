# RESULT — T-rescue-1.0.7-20261112: the slow frames, the learned pipeline list, the seed entity

Worktree: `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/rescue-1.0.7-20261112`
Branch: `task/T-rescue-1.0.7-20261112`
Device: Honor GT Pro `A42BBB5109000333` — **used for measurement only, returned to the user in the
baseline state (section 9); no further device operation from this branch.**
Measurement: `codex-frame-<pid>.csv` monotonic deltas + `.local/tools/analyze_frames.py`,
and the engine's own 60-frame `NVN PERF v=1 ...` windows.
No `gfx-flame-diag`, no `screenrecord` timing, no per-draw logging.

**This report supersedes the earlier framing of the task twice.** The spikes are *not*
synchronous pipeline creation, and the seed is *not* a key mismatch. Both are corrected
below with device evidence.

---

## 1. Headline

| question | answer | evidence |
|---|---|---|
| Are the 100 ms+ spikes synchronous pipeline creation? | **No.** The largest single pipeline creation in the whole measured session is **1.3 ms**. | section 3 |
| What is the spike made of? | The **CPU blocked in the submit path** (`submit_cpu_ms` 28.1 vs 13.5 ms/frame) while the GPU is *less* busy (13.5 vs 17.3 ms). | section 3 |
| What marks a slow window? | **Readback/writeback drains** (`sync_rb` 9.8 vs 0.03, `sync_wb` 10.4 vs 0.00 per window) and, independently, first-time pipeline lookups. | section 3 |
| Do readbacks cause the creations, or the creations the readbacks? | **Neither.** A window with readbacks and **no** creation is still slow (45.0 ms/frame), and a window with creations and **no** readback is also slow (43.5 ms/frame). They are independent co-occurring causes. | section 4 |
| Does the warm-up cause the giant frames? | **No.** The 4.5-4.8 s cold-start frame appears with the warm-up **on**, **off**, and with **no cache at all**. | section 5 |
| Does the seed match the runtime's requests? | **Yes, byte for byte.** All 282 keys the runtime asked for are identical to seed records; the seed served **106** pipelines with **0** creations. | section 6 |
| Is the seed a win? | It removes the in-frame creation cost (created=0), but the shipped 1560-record seed costs **7.0 s** of background driver work to serve ~106 hits. The win is real but the seed is over-broad. | section 7 |

## 2. The baseline, reproduced from the device

`codex-frame-7742.csv` (pid 7742, release build 1.0.7):

    frames 7260 | span_s 295.16 | avg_fps 24.59 | median_gap_ms 33.34 | steady_fps 30.0
    steady_share_pct 93.0 | stalls_over_100ms 93 | stall_time_s 40.1 | stall_share_pct 13.6
    fps_excluding_stalls 28.46
    worst_stalls: [[29,6506],[6037,2691],[3690,2328],[387,1828],[3653,1824],[4,1768],...]

**Correction to an earlier reading of this task**: the window that printed
`created=0 create_max_ms=0.0` is a **60-frame window with no pipeline activity** (the
title screen). `warm_hits` and `created` are reset every window, so one such line says
nothing about the session. The session totals are in section 6.

## 3. Slow-window vs fast-window comparison (the core measurement)

Source: `guest_boot.log` of the warm-off session (pid 16129, 126 windows), pulled to
`.local/rescue-1.0.7-20261112/device/guest_boot-16129-warm-off.log`. A window is
**slow** when its average frame time (`wall_ms / frames`) exceeds 50 ms; a clean
window sits at 33.4 ms.

| counter (per 60-frame window) | SLOW (n=10) | FAST (n=116) |
|---|---|---|
| avg frame time (ms) | **68.78** | 33.86 |
| **submit_cpu_ms** | **28.09** | **13.47** |
| gpu_ms | 13.48 | 17.25 |
| sync_rb (readback drains) | **9.80** | **0.03** |
| sync_wb (writeback drains) | **10.40** | **0.00** |
| pipelines_new | 27.80 | 1.58 |
| created | 17.20 | 1.35 |
| create_ms | 6.81 | 0.58 |
| **create_max_ms** | **0.55** | **0.09** |
| draws | 101.08 | 234.74 |
| dispatches | 43.22 | 60.27 |
| passes | 52.79 | 66.33 |

Three things follow directly:

1. **The GPU is not the bottleneck**: `gpu_ms` is *lower* in the slow windows than in
   the fast ones. The frame is slow while the GPU is idle.
2. **The CPU is stuck in the submit path**: `submit_cpu_ms` is +14.6 ms/frame. The
   engine's own per-part CPU breakdown (`NVN PERF parts_ms_per_frame`) is **all zeros
   with `timed=0`** in this build, so the split inside the submit path
   (drain vs. wait) is not measurable here — a stated limit, not a result.
3. **No single pipeline creation can explain a 100 ms frame**: the maximum
   `create_max_ms` over the entire session is **1.3 ms**. The 17 creations of a slow
   window cost 6.8 ms *in total*.

**The biggest window is not a submit stall at all.** Window 0 (the cold start,
avg 151.6 ms/frame) has `submit_cpu_ms=11.5`, `gpu_ms=6.6`, `rb=0`, `wb=0` — about
133 ms per frame are spent outside every counter the engine reports. It is the scene
and resource load, not the graphics path.

**What precedes a slow window**: the window *before* a slow window is a clean window
(`sync_rb=0`, `created=0`, `submit_cpu_ms` 8-18 ms). There is **no precursor** in
the previous window's counters; the slow windows arrive in adjacent pairs and the
trigger is the *arrival* of drains and first-time pipeline lookups inside the window
itself. There is no leading indicator in the current counters that a window is about to
go slow.

## 4. created vs sync_rb — the ordering question

Four groups, same session:

| group | n | avg frame (ms) | submit_cpu (ms) | gpu (ms) | rb | wb | created | create_ms |
|---|---|---|---|---|---|---|---|---|
| rb=0, created=0 (clean) | 96 | **33.37** | 12.55 | 17.15 | 0 | 0 | 0 | 0 |
| rb=0, created>0 | 18 | **43.51** | 18.94 | 17.92 | 0 | 5.78 | 9.00 | 4.22 |
| rb>0, created=0 | 2 | **45.03** | 19.84 | 16.49 | 8.50 | 0 | 0 | 0 |
| rb>0, created>0 | 10 | **53.83** | 25.87 | 13.40 | 8.50 | 0 | 16.70 | 5.97 |

- A readback drain with **zero** pipeline creations still costs ~11.7 ms/frame.
- Pipeline creations with **zero** readbacks still cost ~10.1 ms/frame.
- Together they are roughly additive (53.8 ms/frame).

**Answer to the question as posed: neither causes the other.** The readback drain does
not trigger the pipeline creation, and the pipeline creation does not pile up readbacks.
They are two independent costs of the same kind of frame, and both land while the
submit path is the thing that is slow. The repair point is therefore **the submit
path / the drain scheduling**, not the pipeline path. (The readback targeted-retirement
work in `transit-20261112` attacks exactly that; this branch does not touch it.)

## 5. warm-plain vs warm-off, and the giant cold-start frame

Four sessions, same build (rescue + A-class fix), same route (title → field → walk
south). The arms are **not scene-matched** (different session lengths and different
positions on the map), so the stall counts are indicative; the cold-start behaviour is
not, because it is the same opening every time.

| run | pid | cache state | frames | span_s | median_gap_ms | steady_% | stalls>100ms | stall_time_s | stall_share_% | worst stall |
|---|---|---|---|---|---|---|---|---|---|---|
| run 2 | 14135 | none (no list, no seed; `warm_built=0`) | 5100 | 189.77 | 33.33 | 97.6 | 43 | 19.8 | 10.4 | frame 29 = **4489 ms** |
| run 3 | 15089 | seed 1560 + learned list (`warm_ms=7017`) | 4260 | 171.33 | 33.52 | 98.3 | 18 | 14.0 | 8.2 | frame 28 = **4800 ms** |
| run 4 | 16129 | seed + learned list, **warm-up disabled** (`gfx-no-warm`) | 6900 | 254.90 | 33.33 | 97.1 | 54 | 23.3 | 9.2 | frame 26 = **4690 ms** |
| run 1 | 11545 | seed only, long walk | 16740 | 592.39 | 33.36 | 99.2 | 22 | 13.7 | 2.3 | frame 28 = **4621 ms** |

**The conclusion the coordinator asked for, with the data**: the giant cold-start frame
is present **with the warm-up on (4800 ms), with it off (4690 ms) and with no cache at
all (4489 ms)**. The warm-up is therefore **not** the cause of the 6.5 s class of
frame, and throttling it would not remove that stall. The run-4 log is the proof: it
has `warm list=1560 enabled=0`, `warm_built=0` in all 126 windows, and the spikes
are still there.

What the warm-up *does* cost is real but different: `warm_ms=7017` of background
pipeline creation at start-up, which competes with the main thread for the driver
during the cold start. That is a **cost to be budgeted**, not the cause of the spikes.

## 6. P0a (downgraded to P1) — the seed does match

**Evidence 1 — byte-level key comparison** (`scripts/distribution/compare_pipeline_warm_keys.py`,
new in this branch): the shipped seed (1560 records) against the 282 records the
runtime itself asked for in the cold session:

    shared_exact_keys                : 282      <- every runtime request is in the seed
    only_in_second_exact_keys        : 0        <- the runtime asked for nothing the seed lacks
    shared_render_pass_variants      : 19  (seed 21, runtime 19)
    only_in_first_stage_module_pairs : 1107

**Evidence 2 — the seed really serves the runtime** (device log, seed + learned list):

    pipelines_new=33 warm_hits=21 created=0
    pipelines_new=92 warm_hits=67 created=0
    pipelines_new=12 warm_hits=4  created=0      (+ 2, 4, 1, 1, 5, 1 in later windows)
    => 106 warm hits, 0 synchronous creations

**Why `warm_hits` looks small**: `pipeline_fast` memoises a pipeline after its first
use, so only the **first** draw of each distinct pipeline consults the warm list. 106
first-time hits can cover thousands of later draws. `warm_hits` is also reset every
window, so a window without first-time lookups prints 0 — which is what the earlier
reading saw.

**What a seed hit is worth (code-level)**: the pipeline is still created **once**. A hit
means the recipe was known *and* the background thread had already built it; the draw
takes the finished pipeline instead of creating one. If the draw arrives first, the draw
creates it and pays for it in the frame. **A seed hit is not free: it moves the cost
out of the frame, it does not remove it.**

## 7. What putting the seed into the distribution actually buys (asked)

**Judgement, with its basis:**

- **It buys the first entry into a scene.** With the seed, the pipelines the scene
  asks for are already built when the scene starts, so the scene's first frames create
  nothing (`created=0` measured, 106 warm hits). Without it, the same pipelines are
  created inside the frames that need them. This is the whole point and it is real.
- **It does not buy a smaller seed by itself.** The shipped 1560-record seed builds
  **1560** pipelines in the background (`warm_ms=7017`, `warm_failed=0`) to serve the
  ~106 the session actually requests. The other ~1450 are built and never used.
- **The risk to the user is at start-up, not in the battle**: the warm-up's 7 s of
  driver work runs concurrently with the cold start, where the device is already
  spending 4.5-4.8 s in one frame. The two are not the same stall (section 5), but they
  do overlap in time.
- **Therefore the acceptance rule for shipping the seed is**: the seed may not add
  multi-second stalls to the start-up. On the evidence available here the warm-up does
  **not** add a multi-second stall of its own (the giant frame is present with the
  warm-up off), so the seed can ship — but the seed should be **narrowed to the
  pipelines the shipping content actually requests**, otherwise the device pays 7 s of
  background driver work per start for ~106 hits.
- **The instruments for that decision** are already in place: `warm_built`,
  `warm_ms`, `warm_hits`, `created`, `create_max_ms` in the engine's PERF line,
  and `stalls_over_100ms` / `stall_time_s` from `.local/tools/analyze_frames.py`.

## 8. A-class flush fix — independent, and device-proven

The defect: `warm::flush()` was only called from `save_pipeline_cache()`, which
returns early unless `pipelines_unsaved != 0`, and `pipelines_unsaved` only
increments for creations that took **≥ 2 ms**. A pipeline the driver built in under
2 ms was recorded by `warm::record()` and then never reached the disk.

**Device proof (run 2, cold: no seed and no list)**: every creation in that session was
fast —

    pipelines_new=9 created=9 create_ms=2.3 create_max_ms=0.4
    pipelines_new=7 created=5 create_ms=2.8 create_max_ms=0.8
    pipelines_new=1 created=1 create_ms=0.3 create_max_ms=0.3

— and the learned list was nevertheless **created and grown during the session**:

    0 B (removed) → 229318 B / 282 records at 23:00 → 376615 B at 23:02

Under the old rule the file would not have existed: no creation reached 2 ms, so
`pipelines_unsaved` would have stayed 0 and `warm::flush()` would never have run.
A second effect of the old rule was that a killed process lost the whole pending
batch; the new take/restore keeps the bytes across a failed write.

**Independent of the attribution work above**: it fixes a real durability bug in the
device's own learning. It does not depend on the key-matching question, because it
persists exactly the keys the runtime asked for — and the keys do match (section 6), so
it does not inflate an invalid seed.

**Host contract**: `tests/graphics/test_warm_flush_contract.cpp` covers the schedule,
the pending bytes, the failed-write path, concurrent record/flush, the file append and
the engine wiring. A negative control (the defect reintroduced in a copy of the source)
makes it fail with three specific messages, so it is not a constant-true assertion.

## 9. Device record — every item this branch touched (honor A42BBB5109000333)

| device item | before | during | now (restored) |
|---|---|---|---|
| `/sdcard/Android/data/org.ffrs.gamewindow/files/gfx-no-warm` | absent | created, "1" (run 4 arm) | **removed** |
| `/sdcard/FFRS/pipeline-warm-list.bin` | absent | pushed, 1638144 B, sha256 `34bcecdf…` | **removed** |
| `…/files/vk-pipeline-list.bin` | 1644232 B, sha256 `8adc9290…` | grew to 376615 B (the A-class fix working) | **restored to 1644232 B, sha256 `8adc9290…`** |
| `…/files/vk-pipeline-cache.bin` | 30130629 B, sha256 `95148390…` | unchanged | **unchanged** (sha identical) |
| `/sdcard/Download/rescue-list-backup.bin`, `rescue-cache-backup.bin` | absent | temporary backups | **removed** |
| markers `gfx-defer-events`, `gfx-honor-pipeline-diag`, `gfx-indirect-proof`, `gfx-codex-probe`, `gfx-draw-param-trace`, `gfx-report-trace`, `gfx-target-fps`, `gfx-depth-aspect` | 0/0/0/0/0/0/30/auto | **not touched** | unchanged |
| `gfx-event-guard` | absent | not touched | absent |
| app process | running | stopped and restarted four times for the arms | **stopped** (`am force-stop`) |

**The game's save may have advanced**: the measurement runs walked the character south
across the field, and the app exports the save when it stops. No save was restored or
overwritten by this branch, and no older save was rolled back (that would have
destroyed the user's progress). The character's position in the save may therefore be
further south than before the measurement.

**Device returned.** No further sampling, key injection, marker change or app restart
will be performed on A42BBB5109000333 from this branch. All remaining work is offline;
the device acceptance is deferred to a single run after 1.0.9 is assembled.

## 10. Code changes on this branch (merge notes for the coordinator)

Committed by the coordinator on this branch at 23:07:

- `4d1c98c` — **engine**: `src/graphics/nvn_vulkan/engine/xbg_warm_flush.h` (new),
  `src/graphics/nvn_vulkan/engine/xbg_game.cpp`, `tests/graphics/test_warm_flush_contract.cpp` (new),
  `scripts/verify_host.py`.
  - `xbg_game.cpp` regions touched: the include block (one line after `xbg_pipeline_key.h`);
    the `pipelines_unsaved`/cadence declaration line (~1192); `save_pipeline_cache()`
    (~1217); `perf_frame()` (~1237); the `warm` namespace's `pending` declaration,
    `record()` and `flush()` (~2865-2900). Nothing else in the file was modified, so
    the `transit-20261112` readback work and this branch do not overlap in the same
    functions.
- `ede7790` — **distribution**: `scripts/distribution/package_honor_portable.py`
  (whitelist + seed entity validation + `--add-to` / `--restore` / `--refresh-delivery`),
  `scripts/distribution/inspect_pipeline_warm_list.py`,
  `tests/installer/test_package_honor_portable.py` (new).

Added by this report:

- `scripts/distribution/compare_pipeline_warm_keys.py` (new) — the read-only key
  comparator used for the P0a evidence.
- `OSS-NOTES.md` (new) — sources with URL, revision, licence, support scope and what
  was actually retrieved.
- `coordination/results/T-RESCUE-WARM-LIST-RESULT.md` (new) — this report.

**Not touched**: `src/android/app/gamewindow/AndroidManifest.xml` (version stays with
the coordinator), the `transit-20261112` worktree, the RG477M tree, no draw skipped,
no effect precision lowered, no depth disabled, no stubbed module.

## 11. Open work this branch does not close

1. **The 4.5-4.8 s cold-start frame** — about 133 ms/frame of it are spent outside every
   counter the engine reports (window 0: `submit_cpu=11.5`, `gpu=6.6`, no drains). It
   is the largest single contributor to `stall_time_s` and is still unexplained. The
   engine's per-part CPU breakdown is disabled in this build (`timed=0`), so the next
   step is to enable the timed sampling and re-measure, not to guess.
2. **The submit-path stall** — `submit_cpu_ms` +14.6 ms/frame in slow windows with the
   GPU idle, marked by readback/writeback drains. The repair belongs to the drain
   scheduling, which the `transit-20261112` readback work is on.
3. **The over-broad seed** — 1560 pipelines / 7.0 s for ~106 hits (section 7).
4. **Mali (RG477M) is unmeasured.** Every number here is the Honor GT Pro. The
   "Maleoon driver does not grow the VkPipelineCache" note in the engine is upstream
   history about the console driver and says nothing about Mali (`OSS-NOTES.md` §4).

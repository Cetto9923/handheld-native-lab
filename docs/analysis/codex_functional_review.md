# Ordinary functional review

- Source: `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`, HEAD `adc6e8e3bf8d48369daa2b8fc0145a57bea80389`, branch `task/codex-integration`.
- Read `AGENTS.md`. Repository was already dirty in four coordination/stage-status files; this review does not modify the repository.
- Host: macOS arm64 Darwin 27.0.0, Apple Clang 21.0.0 (`clang-2100.3.34.2`).
- Scope: author-owned `mutex`, `address_region`, `guest_tls` ordinary valid-input function behavior, their C contract tests, and later frozen standalone `exit_registry` C callbacks. No game input, binary input, NSO loader/parser/bridge, network, or discontinued QA workflow.

## Result

No actionable normal-contract functional finding in the four reviewed libraries. This is a bounded host function review, not full-game, T02, device, SDK inline-operation, or SDK TLS validation.

Baseline and supplemental builds used `clang -std=c11 -Wall -Wextra -Werror -pedantic -pthread` and returned **0**. All executable runs returned **0**:

| Test | Evidence |
|---|---|
| `mutex_contract` | 8 threads; 80,000 increments; zero overlaps/errors; recursive depth 3; contended try-lock false; 100 lifecycle reuses |
| `address_region_contract` | Four reservations (4 KiB, 64 KiB, 2 MiB, 62 GiB); alignment and macOS VM `PROT_NONE` queries pass; all released |
| `guest_tls_threads` | 8 concurrent contexts, 16 zero-initialized slots each, 128,000 value comparisons; host pthread storage and TPIDRRO_EL0 preserved |
| `guest_tls_threads --host-only-control` | Zero errors; TPIDR_EL0 varied in both guest and host-only run, so no invariance claim |
| independent `mutex_valid_concurrency` | Recursive depth 3, waiter bit observed, failed try-lock leaves depth/token/critical fields unchanged, handoff only after final unlock, finalize writes byte 0 only |

Exact argv, cwd, real exit codes, logs: `baseline-manifest.json`, `extra-manifest.json`. Source hashes: `baseline-source-hashes.json`. Additional test source: `mutex_valid_concurrency.c`.

## Source reasoning

- `mutex.c:62-68,102-114`: borrowed operation counters protect entries until the operation finishes; active/owned entries are not finalized. `119-149` serializes mirrored ownership/depth transitions, including releasing the native mutex before dropping the registry lock. Recursive native lock counts and mirrored depth track each other; failed normal try-lock preserves owner and depth.
- `address_region.c:27-49,54-72`: host page rounding, SDK alignment, and guard reservation are retained in ownership metadata; free uses the original mapping and is serialized. The test queries VM protection without accessing the reservation interior.
- `guest_tls.c:4-13`: C thread-local zero-initialized storage gives one independent 16-slot array per thread. The API is explicitly unbound to SDK slot/thread layout.

## Frozen exit registry review

- Exact freeze: `5e165d08a22d6b19826467ef0ed861ce168bb5fb` (`task/T02-codex-runtime`). The three authorized files were read with `git show` and copied under `exit-freeze/`; no mutable runtime WIP was used.
- Strict Clang build of `exit_registry_contract.c` + `exit_registry.c`: **exit 0**.
- Standalone contract: **exit 0**, errors 0, 4,000 registrations executed once, LIFO and DSO filtering, repeated/concurrent/recursive finalize, callback-added registration, destroy/scope discard without execution.
- `exit_registry_contract --normal-exit`: **exit 23**, as designed. Registration returns first, then exactly one callback executes.
- `exit_registry_contract --immediate-exit`: **exit 24**, as designed. Registration returns and callback is absent.
- Independent `exit_valid_concurrency.c`: **build exit 0 / run exit 0**. Eight live producers and two live drainers overlap; all 4,000 unique registrations execute exactly once, callbacks never overlap, pending count ends at zero.
- Exact commands, SHA, source hashes, stdout assertions and logs: `exit-manifest.json`.

Source anchors in frozen `src/runtime/hle/exit_registry.c`:

- `33-52`: registration appends metadata without invocation.
- `54-73`: reverse registration order, DSO selection, remove-before-call and recursive finalize mutex; callback runs outside the list mutex.
- `75-88`: discard/destroy frees registrations without invoking their functions.
- `116-136`: one module scope at a time; end discards its registrations. `exit_registry.h:8,21-22` and implementation line 128 explicitly require guest threads quiescent before destroy/unload.
- `138-142`: normal guest exit finalizes first; immediate exit skips finalization.

## Limits

This establishes ordinary host C function behavior only. The caller must enforce thread quiescence before unloading callback code; that lifecycle precondition is not a join operation inside this library. Loader integration, guest ABI/SDK CRT semantics, multiple live modules, NSO bridge/parser behavior, full game/T02, Android/RG477M and device performance were not reviewed or passed. `tests/native/test_exit_registry.py` and `exit_registry_bridge.c` were neither read nor run per the parent restriction.

No denial occurred in this scope. Two read/setup attempts failed harmlessly before verification: a discovery search named an absent root `CMakeLists.txt` (exit 2), and the first execution request used the not-yet-created review working directory (process not created). The directory was then created in the authorized workspace and all checks above completed.

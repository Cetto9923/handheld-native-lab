# Codex native-port checkpoint — project incomplete

Goal: native Android ARM64 execution of FINAL FANTASY RESONANCE on RG477M,
then actual title/playable flow, controller, audio, saves, resolution, frame
pacing and sustained-run evidence. Neither host checks nor an APK alone meet it.

## Ownership and execution

Antigravity handed off d26d3f6 with active_writers=[] on 2026-10-23. The current
Codex coordinator works in the isolated `task/codex-integration` worktree at
`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`.
The original `work/rg477m-native` checkout and old worktrees were left intact.
Three actual subagents were dispatched as GPT-6.1 (gpt-6.1-sol), xhigh reasoning.
There is no CreateGoal tool in this environment: no persistent goal was armed.
The full objective remains unfulfilled and guides active execution.

| Task | Worktree / branch | Owned paths |
|---|---|---|
| Runtime | ../ffrs-runtime / task/T02-codex-runtime | src/runtime/nso, src/runtime/hle, tests/native, scripts/runtime |
| Android | ../ffrs-android / task/T03A-codex-android | src/android, scripts/device, tests/device |
| Graphics/integration | this worktree / task/codex-integration | src/graphics/nvn, tests/graphics, scripts/verify_host.py, coordination |
| Independent QA | ../ffrs-qa / task/T07-codex-qa | interrupted by platform safety check; no longer writing; existing WIP preserved |

No reset, clean, stash, forced push, remote publish or stop of other agents was
performed. Each new worktree has its own ignored .local directory; only
uncompressed and exefs input directories link to the original private artifacts.
New logs, builds and downloads are isolated. Never commit game files or keys.

## Verified checkpoints

- d26d3f6: Antigravity integrated T01 analysis, host probe, NVN inventory,
  stream resource tooling, and DT_RELR support. Existing PASSED labels do not
  prove the cross-build, renderer or playable target.
- 9a51034: NVN typed dispatch and observable failure boundary; removed generic
  zero-return fallback and fictitious DeviceInitialize success.
- 7a360a8: repeatable host verification runner with real subprocess return codes.
- Host verification: `.local/host-verification/summary.json`; native loader
  --info, host probe JSON tests and NVN dispatch checks pass. Android/game entry/
  title/playable/device are explicitly NOT_RUN in this suite.
- `.local/artifacts/nvn/coverage.json`: 535 required API names, 2 bootstrap
  symbols resolved, 533 missing; process exits 3 and proves_rendering=false.

## Active work and evidence limits

The runtime worker has reproduced the old SIGSEGVs, then repaired resolver
wiring in its worktree. Normal experiments now stop at named unsupported imports
rather than a silent success stub: nnMain -> ReferSymbol; startup -> AllocateTlsSlot;
init -> __nnmusl_init_dso; malloc -> QueryMemoryInfo. These results still require
integration and rerun. See that worker's `.local/runtime/` logs.

The Android worker is building an Activity/JNI APK carrier and a command-line
loader. Existing Android SDK build-tools/platforms are usable; NDK installation
was incomplete. An official Google NDK package is being downloaded to its own
.local with a recorded official checksum. No cross-build or APK pass is claimed
until real ARM64 ELF/DEX/package validation succeeds. Actual elevated `adb devices`
returned an empty list: RG477M checks remain BLOCKED_DEVICE.

The QA subagent's subsequent turns were stopped by the platform safety check.
Do not retry its interrupted probes or label its review passed. Ordinary host
baseline logs in ../ffrs-qa/.local/qa/baseline remain available; uncommitted files
in that worktree are preserved and are not included in integration commits.

## Next execution

1. Integrate the runtime worker's reviewed commit and reproduce its normal
   module-load and behavior tests with captured real exit codes.
2. Complete official NDK package validation; build target ELF and diagnostic APK;
   inspect ABI, native libraries, DEX, manifest, signing and checksums.
3. Connect native resolver with the typed NVN boundary. No renderer exists yet:
   shaders/resources/commands/presentation need actual implementation and GPU tests.
4. Continue real CRT/TLS/HLE initialization using local binary evidence. Preserve
   host TLS registers and do not guess unknown SDK layouts for apparent success.
5. Only after actual game startup and connected hardware, exercise title/playable,
   controller/audio/save and specified frame/stability acceptance tests.

Run `python3 scripts/verify_host.py` for ordinary host checks. Read its summary
scope before using results. Per-worker reports and coordination/state.json are
the recovery entrypoints; inspect current branches and WIP before taking ownership.

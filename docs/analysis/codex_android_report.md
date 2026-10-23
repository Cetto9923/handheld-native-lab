# T03A Codex Android — verified cross-build, APK carrier and HLE CLI

2026-10-23, worktree `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-android`, branch `task/T03A-codex-android`, starting commit `d26d3f6`.

## Scope and status

This is an Android carrier and load-only diagnostic pipeline. It does not claim game startup, rendering, title screen, gameplay, or performance. The NSO mode invokes the shared `src/runtime/nso/nso_loader.c` in place, then releases the module; it never calls constructors or a game entry point. No game module/resource/credential is packaged. Missing `modules/main.nso` is reported as `BLOCKED_MODULE`.

- Source implementation: JNI Activity, native diagnostic CLI/shared library, optional NVN dispatch diagnostic reference, offline SDK packaging and artifact inspection scripts are implemented.
- Host CMake probe build and existing three probe output checks: exit 0.
- Host diagnostic compile: exit 0; missing/truncated/compressed input checks return 1 with the expected structured error and `game_code_executed=false`.
- Java Activity compilation, DEX generation (4,624 bytes), and binary manifest packaging: exit 0, using installed JDK 23.0.2 and Android build-tools 34.0.0. Anonymous classes trigger an internal error in the installed older D8; the final single Activity implements callbacks directly and produces valid DEX.
- Android ELF/APK full build: **PASS_CROSS_BUILD / PASS_PACKAGING_ONLY**, command exit 0. This uses Apple Clang 21 + Rust LLD 22.1.8 + separately CRC-verified official NDK members. It does not claim a complete NDK installation or any device execution.
- Device execution: `BLOCKED_DEVICE`. Normal authorized `adb devices -l` returned exit 0 and an empty device list. Initial sandbox attempt returned 1 because its daemon could not open its local listener; authorized read-only retry resolved that permission issue. No emulator was started.

## Toolchain evidence

- Complete, readable SDK components: `$HOME/Library/Android/sdk/build-tools/34.0.0`, `platforms/android-34/android.jar`, `cmdline-tools/latest` revision 19.
- Java: Homebrew OpenJDK 23.0.2 arm64. CMake: `/opt/homebrew/bin/cmake` 4.4.4. Make: `/usr/bin/make`. No Gradle in PATH; packaging uses javac/jar/d8/aapt/zipalign/apksigner directly and needs no Gradle or Maven fetch.
- Global `ndk/26.1.10909125` and `cmake/3.22.1` contain only `.installer` state, not completed installations. Their existence is not evidence of a working NDK/CMake.
- Old global r26b partial archive is only 5,455,840 bytes. No complete package was found in the directed agent-b cache check.
- NDK r27d chosen from Google's official package index: revision 27.3.13750724, archive size 839,006,233 bytes, official SHA1 `2970926d705988f79baa9b04c51b4f7914dd8c56`. SHA1 here checks agreement with the published package metadata, not an independent cryptographic signature.
- Download source: <https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip>. Published package index: <https://dl.google.com/android/repository/repository2-3.xml>. Reference: <https://developer.android.com/ndk/downloads>.
- Initial sandbox sdkmanager request failed remote manifest access. Authorized sdkmanager downloaded 92,231,048 bytes before the task stopped its own Java PID 7659 and preserved the partial archive. The same official URL supports HTTP206 range requests; curl resumes the preserved archive. No alternate mirror, global installation, proxy reconfiguration, or unrelated process termination is used.

## Commands and evidence locations

All paths below are relative to this worktree. `.local` is ignored by Git.

| Command/check | Evidence | Result |
| --- | --- | --- |
| Host CMake configure/build | `.local/logs/host-{configure,build}.{log,exitcode}` | 0 / 0 |
| `python3 tests/device/test_probe_output.py .local/build/host/probe_rg477m` | `.local/logs/host-probe-tests.{log,exitcode}` | 0; three checks pass |
| Host diagnostic compile | `.local/logs/host-{loader,diagnostic}-compile.{log,exitcode}` | 0 / 0 |
| Missing/truncated/compressed NSO | `.local/logs/host-diag-*.{json,exitcode}` | each 1; expected error diagnostics |
| Final Java compile / D8 | `.local/logs/{java-single,dex-single}.{log,exitcode}` | 0 / 0 |
| Binary manifest package | `.local/logs/manifest-preflight.{log,exitcode}` | 0 |
| Initial sdkmanager / authorized sdkmanager | `.local/logs/ndk-install*.log` | first failed; second intentionally stopped after preserving bytes |
| Curl official HEAD / 1 MiB range | `.local/logs/ndk-official-headers.*`, `ndk-range-check.*` | 0 / 0; HTTP206 sample 1,048,576 bytes |
| Resumable NDK installer | `.local/logs/ndk-local-installer.*`, `ndk-transfer-summary.log`, `ndk-transfer-*.log` | running; retain actual exit and byte records |

The complete downloader exited 4 after four no-growth TLS failures. Its preserved partial archive `.local/downloads/android-ndk-r27d-darwin.zip` is exactly 111,508,458 bytes; full expected size is 839,006,233. No downloader remains active. No full-archive SHA1 pass is claimed. `scripts/device/install_local_ndk.sh` can resume it later.

The successful partial toolchain is `.local/minimal-ndk/toolchain`. The final fetch command exited 0, selecting 3,280 members in 15 logical range groups (23,203,759 bytes in the range plan). `fetch_minimal_ndk.py` validates TLS origin, exact HTTP Content-Range and length, then ZIP CRC32 and decompressed size for every extracted member. Requests larger than 2 MiB are split. Logs are `.local/logs/minimal-ndk-final.{log,exitcode}`; `.local/minimal-ndk/validated-members.json` lists every selected member and CRC, and `range-transfer.jsonl` preserves requests and actual curl exit codes. This is a **partial source package, not a completed/whole-checksum-validated NDK**.

The host Rust LLD is `$HOME/.rustup/toolchains/stable-aarch64-apple-darwin/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld`; it requires the task-local command environment `DYLD_LIBRARY_PATH=$RUST_ROOT/lib` to find its already-installed libLLVM.dylib. No global library or shell configuration was changed.

Successful reproducible command for this worktree (runtime and graphics read from the integration worktree, never copied or edited here):

```bash
FFRS_USE_MINIMAL_TOOLCHAIN=1 \
FFRS_RUNTIME_DIR=/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration/src/runtime/nso \
FFRS_NVN_DIR=/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration/src/graphics/nvn \
scripts/device/build_diagnostic_apk.sh
```

Runtime input: integration commit `72aeca5` (source commit `987256b`), including Bionic signal context and resolver fixes. Graphics input: `9a51034` NVN diagnostic dispatch. The APK does **not** install an experimental global fault/signal trap. `nso_load_file` is invoked with a null resolver, reports the actual loader unresolved import count, and is followed by `nso_free`. Host dlsym visibility is reported separately, not as a bound-import count. NVN bootstrap is linked and observable, with no graphics backend installed or rendering claim.

On the final integrated tree, omit both source-directory overrides to use the relative runtime and graphics implementations in place. A full installed NDK can instead use the default CMake path without `FFRS_USE_MINIMAL_TOOLCHAIN=1`.

`build_android_probe.sh` defaults to the hardware probe alone, permitting an actual ELF cross-build even while runtime integration is pending. `--all` additionally builds the load-only CLI and JNI shared library. Native objects target Android API28/arm64-v8a with static libc++, explicit 16KiB ELF load alignment, and symbols retained in `.local/build/android-arm64-minimal` (partial toolchain) or `.local/build/android-arm64` (NDK/CMake). APK target SDK is 34. Device runtime compatibility remains unmeasured.

`build_diagnostic_apk.sh` generates a new local development signing key inside `.local/signing` only when absent. It does not open existing user/release credentials. APK verification checks signature, compiled manifest, DEX, AArch64 ELF machine, JNI and loader symbols, text relocations, and absence of private NSO/game input members. The successfully generated output APK is `.local/artifacts/android/ffrs-diagnostic-debug.apk`. The command preserves its actual exit code in `.local/logs/android-apk-build.exitcode` (0), with full output in the sibling log. Per-step configure/compile/link, signature, manifest, and structure logs/exit codes are under `.local/logs/android/`. The complete ELF symbol inspection can be saved beside the APK as `.elf-inspection.txt`; only concise summaries are printed by the artifact verifier.

The app writes `diagnostic.json` beneath its own external files directory (falling back to internal files), showing the actual path on screen. User-authorized uncompressed module input belongs at that directory's `modules/main.nso`. Capability fields retaining existing headless limitations (e.g. AudioTrack, display and input sysfs permissions) must not be treated as measured Java-backed results.

## First verified checkpoint artifacts (`9be2705`)

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| `.local/artifacts/android/checkpoint-9be2705/probe_rg477m` | 2872776 | `f4b939ad90a008555ba4eb8da6960e722207d3f8fb9642538ee8ceca70ae7ece` |
| `.local/artifacts/android/checkpoint-9be2705/nso_diagnostic` | 2877208 | `f0616514f16b35d9dd0c82dae527bcdc09a3288e7b689cf342746400de91cf5c` |
| `.local/artifacts/android/checkpoint-9be2705/libffrs_diagnostic.so` | 3234592 | `0e585d95eccb6699a98abf6a344a0fba1d357c345601800423ca4dbb9ab58213` |
| `.local/artifacts/android/checkpoint-9be2705/ffrs-diagnostic-debug.apk` | 410072 | `4ab22db59be7b6a49df36b4d7aeb8013e390b2a7b6ae8eeff79ba23d15f48039` |

`llvm-readelf` confirms ELF64 AArch64, `/system/bin/linker64` for both CLI executables, and 0x4000 alignment on all LOAD segments. APK signature verification passes v3; the compiled manifest reports package `org.ffrs.diagnostic`, min SDK28, target34, debuggable, and native ABI `arm64-v8a`. Required JNI and loader symbols are present, with no ELF TEXTREL and no private game inputs. The JNI shared object depends only on Android system libc/libm/libdl/liblog/libandroid/libvulkan; libc++ is statically linked. Signed APK verification and packaging tests are host-side structural evidence only.

## Hardware diagnostic checkpoint (version 0.2)

The first source checkpoint is `9be2705e6f149e4bef40367d79c4520c4d6c4611`; its original APK and native outputs remain preserved under `.local/artifacts/android/checkpoint-9be2705/`. This second checkpoint adds only Android host diagnostics, packaging, verification and this report.

- Manifest versionCode 2 / versionName `0.2-hardware-diagnostic`, compiled application label `FFRS 诊断宿主`, touchscreen and gamepad both optional. The unused GLES feature declaration is removed.
- `dispatchKeyEvent` captures physical key source/device/action/keycode/scancode/repeat and event timestamp while preserving normal Android handling. `dispatchGenericMotionEvent` captures common stick/hat/trigger axes before views handle the event. Motion is sampled every 50 ms and the skipped event count is recorded explicitly.
- onCreate/onResume/onPause/onDestroy and diagnostic start/finish are written to `FFRS-Hardware` logcat and the app-private `hardware-events.jsonl`. The existing private evidence file is retained when it reaches 1 MiB, while logcat continues; it is not silently truncated.
- The private storage probe uses `File.createTempFile` under `getFilesDir()`, writes/fsyncs/reads and compares only this newly created sample, then deletes only that sample in `finally`. It reports the cleanup result. It does not touch game saves or external/user files. No audio is played and no volume setting is changed.
- These input/lifecycle/storage implementations are **NOT_RUN on device**. A future `PASS_FILE_LOOPBACK_ONLY` result concerns only the diagnostic app's private-file round trip, never a game-save gate.

The complete build/Java/DEX/aapt/signature/ELF inspection command exited 0. Evidence is `.local/logs/android-apk-hardware-frozen.{log,exitcode}`, with per-step outputs under `.local/logs/android/`. `source-guard.exitcode` is 0. Both the before/after-compile and before/final source snapshots match for all 34 files; runtime repository HEAD remained frozen at `e860f999ac08a584fb66731d0ca11373fe268185`. Source changes during a build now stop packaging with exit 5 instead of accepting a mixed candidate. The three preserved fingerprints are `build-input-before.json`, `build-input-after-compile.json` and `build-input-after.json`.

Frozen compiled dependency SHA256:

| File | SHA256 |
| --- | --- |
| runtime `nso_loader.c` | `621b7b54869484e95f3de92c143fe39b52a10243c4ba472f59206eb9b65a579f` |
| runtime `nso_loader.h` | `4c40b8c84d4d3c0ee7dfdec6324e1e335ba8bc57d1e6e5235ea20894dd4ed882` |
| graphics `nvn_dispatch.c` | `bcf1a852be4b8a80bdd24ec0672a301e1a460825aadc0d05eff5dbcae03d0393` |

The earlier intermediate APK with SHA256 `0d2f1c1c772c30a05c76f838aab8092b77f8fddc00cf4513c2c794a87835c4c7` is explicitly **MIXED_SOURCE_NOT_ACCEPTED**: integration runtime changed during that build, despite successful structural checks. It is preserved separately at `.local/artifacts/android/mixed-source-not-accepted-0d2f1c1c/` with a status manifest. It is not the accepted hardware checkpoint.

Accepted hardware checkpoint artifacts:

These are historical version 0.2 identities. The original APK remains at `.local/artifacts/android/checkpoint-40ce791/ffrs-diagnostic-debug.apk`; live output paths are replaced by the following version.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| `.local/artifacts/android/probe_rg477m` | 2872776 | `f4b939ad90a008555ba4eb8da6960e722207d3f8fb9642538ee8ceca70ae7ece` |
| `.local/artifacts/android/nso_diagnostic` | 2878608 | `9233ec028bd750ccd1ac77596b42302b2307e76d50842b220d88c05071e48e33` |
| `.local/artifacts/android/libffrs_diagnostic.so` | 3235968 | `8494df957ab097c374b4ccb7d425e160d32bef5188acc40c0f64aa6f8699cd5b` |
| `.local/artifacts/android/ffrs-diagnostic-debug.apk` | 414228 | `42d379034ea53b5cff5ce23eac452d0df4325e288857a77fefc4ae27aeb1aefa` |

The APK passes v3 signature verification and aapt reports version 0.2, Chinese label, API28/target34 and arm64-v8a. The artifact verifier additionally checks ET_DYN, AArch64, valid 16 KiB LOAD congruence and no writable+executable LOAD segment. All device/game gates remain BLOCKED_DEVICE/NOT_RUN.

## Optional original-leaf runner (version 0.3)

This checkpoint builds the diagnostic app's optional leaf-validation entry. It does **not** execute the leaf on an Android device during this task. Current Android leaf status is **NOT_RUN**; game startup, constructors, rendering and gameplay remain NOT_RUN. No USB action is requested or awaited for this host-side work.

The UI has a separate explicit button. Input is only the app-private `getFilesDir()/libffrs_leaf.so`; output is app-private `leaf-report.json`. The reviewed locally generated library is not bundled in source or APK. `tests/device/verify_leaf_exclusion.py` reads the separate reviewed artifact, checks its identity, and searches every decompressed APK member for both the library name as a member and the original 44-byte instruction sequence: neither is embedded (exit 0). Generated game bytes were neither copied into source nor printed.

The immutable script at integration `41a79716dafb9f48cfc34b3e8209dae88b04ec82`, `scripts/runtime/build_leaf_library.py`, establishes:

| Item | Identity |
| --- | --- |
| source module SHA256 | `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5` |
| original leaf | main `+0x694bd0`, 44 bytes |
| original leaf SHA256 | `8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9` |
| generated ELF | 2,176 bytes; SHA256 `8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8` |
| exports | `ffrs_leaf_694bd0`, `ffrs_leaf_reference` |
| exact typed signature | `uint32_t(int32_t, int32_t, int32_t, uint32_t)` |

The runner opens a regular, non-symlink 2,176-byte file and bounds-checks its AArch64 ELF64 ET_DYN headers. It rejects nonzero entry points, an ELF interpreter, writable/executable segments and dynamic tags outside the generated artifact's narrow metadata allowlist. Dependencies, init/fini functions and init/fini arrays are prohibited before loading. Java MessageDigest computes SHA256 of the native-read bytes; only the reviewed whole-library digest is accepted. `dlopen` names that checked descriptor through `/proc/self/fd/N`; `dlsym` resolves the two explicitly typed exports. No NSO/game startup or constructor API is invoked.

The comparison grid is 15 signed input values cubed, times five flag values (0, 1, 2, 3, UINT32_MAX): **16,875 deterministic samples**. Each original result and the library's handwritten reference result are compared against local C++ integer mathematics (`std::bit_width` of the unsigned signed-max result OR 1). Structured JSON reports source/module/library SHA256, actual execution architecture, planned/executed counts, mismatch count and the first failing tuple/results. Missing input is NOT_RUN; read, hash, preflight, loader, export and comparison failures are FAIL. Only the actual Android AArch64 comparison path can emit `PASS_LEAF_BEHAVIOR`; host synthetic tests emit `HOST_TEST_COMPARISON_ONLY`.

Host checks (all command exits 0):

- `test_leaf_validation.cpp`: 16,875 unique **synthetic** typed calls, boundary values, wrong-original and wrong-reference mismatch detection, null export, absent file, absent digest, malformed ELF, entry-point and constructor/dependency rejection. The reviewed Android ELF is parsed only; original leaf bytes are not loaded or executed on the host in this test.
- `test_build_evidence.py`: synthetic artifacts confirm fresh SHA recomputation and rejection after source change or a failed command, while device/leaf execution remain NOT_RUN.
- Java/DEX/native ELF/APK build, v3 signature, aapt version 0.3, AArch64/16 KiB/no-WX checks, both JNI exports and original-leaf exclusion all pass static inspection.
- `stage_leaf_library.py` was run only in its default local-plan mode, with serial `RG477M-NOT-RUN`; its `steps` list is empty. It performed **no ADB or device operation**.

### Fixed build inputs and automatic machine evidence

New Android code is read from this worker tree via explicit `FFRS_SOURCE_ROOT`. Runtime and NVN dependencies are a task-local `git archive 41a7971` snapshot at `.local/source-inputs/41a7971/`; they do not follow the coordinator's live integrations. The source guard fingerprints **all ordinary files** under Android source/config/resources, device build scripts, device tests/validators and dependency source directories, excluding only Python cache files and `.DS_Store`. CMakeLists.txt, APK validation code, resource/version configuration and new evidence/staging scripts are included. All 47 input-file fingerprints match before compilation, after compilation and after verification.

Every build gets a distinct step-log directory. On success or failure, `record_build_evidence.py` automatically writes current `build-evidence.json` and artifact `manifest.json`; no previous handwritten hash is reused. It records source HEAD and dirty paths at build time, pinned dependency revision, complete input fingerprints and stability, actual overall/step exit codes, current artifact size/SHA256 and explicit `HOST_STATIC_CROSS_BUILD_AND_PACKAGING_ONLY` scope. A failed/mixed build cannot be accepted. Device, Android leaf, game startup and gameplay are always NOT_RUN in this host-build record.

Final build evidence: `.local/artifacts/android/build-evidence.json` and `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-android/.local/logs/android-build.WLFvFv/build-evidence.json`. Build-time source HEAD is `40ce791` plus the recorded leaf-checkpoint dirty files; the forthcoming atomic source commit contains these compiled source fingerprints. Dependency revision is `41a79716dafb9f48cfc34b3e8209dae88b04ec82`. All recorded step exits and the overall exit are 0. The official NDK remains a **partial, per-member CRC-verified toolchain**; whole archive checksum is NOT_VERIFIED.

```bash
FFRS_SOURCE_ROOT="$PWD" FFRS_USE_MINIMAL_TOOLCHAIN=1 \
FFRS_DEPENDENCY_REVISION=41a79716dafb9f48cfc34b3e8209dae88b04ec82 \
FFRS_RUNTIME_DIR="$PWD/.local/source-inputs/41a7971/src/runtime/nso" \
FFRS_NVN_DIR="$PWD/.local/source-inputs/41a7971/src/graphics/nvn" \
FFRS_REVIEWED_LEAF_LIBRARY=/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration/.local/runtime/leaf-e860f99/libffrs_leaf.so \
scripts/device/build_diagnostic_apk.sh
```

Current accepted static artifacts:

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| `probe_rg477m` | 2872776 | `f4b939ad90a008555ba4eb8da6960e722207d3f8fb9642538ee8ceca70ae7ece` |
| `nso_diagnostic` | 2878448 | `459a99463d4b2a70a0ebf1db8749cc9bc79957149b98fba6f2f7e74baf6b6cb2` |
| `libffrs_diagnostic.so` | 3471144 | `3e8a284af7d80c452da49c440dbc431204283e69e8c6d1e533ae6a1971550ce7` |
| `ffrs-diagnostic-debug.apk` | 422420 | `7270b1080e4a4e2ac7b1544a8610f84f4ae050047e12528029018492b127d2c8` |

Artifacts are in `.local/artifacts/android/`; full command/output/exit evidence is `.local/logs/android-apk-leaf-final.*` plus the unique step directory above. Host synthetic checks are `.local/logs/leaf-host-{compile,tests}.*` and `.local/logs/build-evidence-tests.*`.

### Later private-file staging

`stage_leaf_library.py --serial <explicit-selected-RG477M> --library <local-generated-lib>` only prints a plan. The initial optional-leaf checkpoint used an exclusive temporary file followed by hard-link publication. The later staging compatibility checkpoint below replaces that publication after an actual Android permission failure. It never installs/uninstalls a package, clears app data, touches game/save paths or selects another device. A separate APK install, if later requested, must stop on a signature conflict; removing the installed package is not an automatic remedy.

Android namespace/permission behavior for descriptor-based dlopen, actual typed calls, hardware/UI operation and RG477M leaf results remain NOT_RUN until a later device session. This host task does not wait for one.

### Prior handoff: startup build dependency

At the optional-leaf checkpoint, read-only inspection identified `nso_call.c`'s verified HLE resolver, enabled by `NSO_WITH_HOST_HLE`, and its typed startup adapters as the next cross-link dependency. The separate HLE CLI checkpoint below completes that build item against a coordinator-approved immutable runtime revision.

## Independent HLE CLI checkpoint

Scope: **PASS_STATIC_ANDROID_HLE_CLI_ONLY**. Android execution, guest initialization, game startup, gameplay and RG477M performance remain **NOT_RUN**. No device, emulator, APK installation, guest function, constructor or full game entry was executed by this worker. Guest TLS SDK binding is **NOT_IMPLEMENTED**. This unit adds build support and validation, with no new runtime functions or import stubs.

The immutable runtime dependency is `e0024a040b7de948b751dc861809312ab794435e` (coordinator integration `5e165d0`). `git archive` saved only `src/runtime/nso` and `src/runtime/hle` into `.local/source-inputs/e0024a0/`. Every one of the 14 extracted files was compared to its exact Git blob identity; `.local/source-inputs/e0024a0/archive-evidence.json` records that comparison, SHA256 for each source and the tar archive. The build reads this fixed snapshot, never the integration tree's changing HEAD. No runtime or graphics source is modified by this checkpoint.

`scripts/device/build_runtime_hle.sh` builds an independent `nso_runtime_hle` executable from exactly:

- `nso_call.c` and `nso_loader.c`;
- `nso_host.c`, `address_region.c`, `mutex.c`, `exit_registry.c`.

All six compile with `NSO_WITH_HOST_HLE=1`, C11, `-Wall -Wextra -Werror -Wformat=2`, using Bionic headers for Android API28/AArch64. Each compile and the strict `--no-undefined` link exited 0; no warning suppression or Bionic source patch was needed. The inherited exact-name typed resolver, unknown-import BRK/fatal path, module unload observer, scoped guest exit callbacks and real `exit`/`_Exit` handling are compiled unchanged. This is source/link evidence, not a behavioral execution result. The CLI's experimental fault handler remains in that standalone executable; HLE and its handler are not added to JNI.

The CMake target is opt-in (`FFRS_BUILD_RUNTIME_HLE=ON`) and uses the same six sources, strict warnings and post-link static checker. It defaults OFF for the APK. `FFRS_HLE_DIR`, `FFRS_RUNTIME_DIR` and `FFRS_READELF` select the dependency and checker tool. The CMake target was also actually configured and built with the available partial Apple Clang/Rust LLD/Bionic toolchain: both commands exit 0 and its post-build ELF gate passes. That local CMake configuration describes a Linux cross system with an explicit Android compiler target; it is not a completed NDK installation. The installed-NDK CMake toolchain branch remains NOT_RUN because the full NDK is unavailable.

One preliminary CMake configure exited 1 because macOS stripped the inherited `DYLD_LIBRARY_PATH` before launching Rust LLD. A task-local linker wrapper sets that variable immediately before executing the existing linker. The corrected configure/build exited 0. The initial error and successful commands/output/exit codes are retained separately under `.local/logs/android-hle-checks/`; no global toolchain/library setting was changed.

`tests/device/verify_runtime_hle_elf.py` never executes its input. It verifies ELF64/AArch64 ET_DYN, a nonzero executable entry, `/system/bin/linker64`, 16 KiB-compatible LOAD segments, no writable/executable segment or executable stack, RELRO/immediate binding, no text relocations, only Android `libc.so`/`libm.so`/`libdl.so` dependencies, and defined typed resolver/Mutex/address-region/exit cleanup functions. It records all undefined imports as well as required definitions. The original unsupported imports are not reclassified as supported by the static gate.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| `.local/artifacts/android-runtime-hle/nso_runtime_hle` (minimal script) | 121720 | `10fb9e80292e6a7d9f3d953745f2d365ecd39e36d8586174cfb7ec40c4cc350d` |
| `.local/build/android-hle-cmake-check/nso_runtime_hle` (CMake partial cross configuration) | 123456 | `fed000da8b0752345c8b1c869b8bfb3f82f86389ab98fa63db43055bdfecfe90` |

The script artifact has four LOAD segments, all aligned `0x4000`; NEEDED is exactly `libc.so`, `libm.so`, `libdl.so`. Both artifacts pass the same static verifier. Different compile/link driver settings account for their distinct artifact identities. The diagnostic APK remains byte-for-byte the leaf checkpoint's 422420-byte package, SHA256 `7270b1080e4a4e2ac7b1544a8610f84f4ae050047e12528029018492b127d2c8`; this HLE build does not rebuild or replace it.

Reproduction after making the same immutable source snapshot:

```bash
FFRS_SOURCE_ROOT="$PWD" FFRS_USE_MINIMAL_TOOLCHAIN=1 \
FFRS_DEPENDENCY_REVISION=e0024a040b7de948b751dc861809312ab794435e \
FFRS_RUNTIME_DIR="$PWD/.local/source-inputs/e0024a0/src/runtime/nso" \
FFRS_HLE_DIR="$PWD/.local/source-inputs/e0024a0/src/runtime/hle" \
scripts/device/build_runtime_hle.sh
```

The precommit build exited 0 (`.local/logs/android-hle-precommit.{log,exitcode}`); its 10 step exits are integer 0. Its full per-step command, output and exit files, readelf dump, ELF summary and before/after fingerprints are in `.local/logs/android-hle-build.DUqLN8/`. Current authoritative evidence is `.local/artifacts/android-runtime-hle/build-evidence.json`, including the actual source HEAD/dirty state and unique log-directory pointer; a clean post-commit rebuild may update that pointer without changing this report's compiled inputs. All 56 input fingerprints are stable across the three snapshots, including `CMakeLists.txt`, the APK/CLI checkers and all new scripts/configuration. The shared `snapshot_build_inputs.py` now supplies both APK and CLI guards.

Machine evidence acceptance now requires **every recorded step to be integer 0**, every required terminal gate present, and all required artifacts present as nonempty files. APK mode requires its four artifacts; the independent HLE mode requires its own CLI in its separate artifact directory. Overall failure or source changes also reject acceptance. Host synthetic tests check every missing/empty artifact, missing terminal gate, additional failing/unknown/empty step, fresh hashing, separate CLI scope and source-change rejection. Actual-ELF negative tests reject altered architecture, missing entry, WX, 4 KiB LOAD alignment, wrong interpreter, executable stack, non-Android dependencies, missing typed resolver/scope-end, absent immediate binding and text relocations. These tests only inspect files; they never execute the Android binaries. Both regression scripts exit 0; the ELF regression covers both actual CLI layouts. Logs and exact commands are `.local/logs/android-hle-checks/*-final.{command.json,log,exitcode}`.

The official NDK remains a partial, per-member CRC-verified toolchain. Its whole-archive checksum remains NOT_VERIFIED. The useful next execution step belongs to a separately authorized Android diagnostic session; RG477M hardware and guest TLS/CRT compatibility remain distinct gates.

## Measured Activity display checkpoint (version 0.4)

The coordinator's actual Android 14/AArch64 emulator evidence reproduced a reporting bug: `identity-before-install.json` records `wm size` 1080x2340, density 440 and native bridge 0, but its old JNI `diagnostic.json` reported display `ok` at 1280x960/60 Hz and density 0. The discrepancy came from the hardcoded RG fallback in `probe_android.cpp`. That evidence is read-only at the integration worktree's `.local/emulator/pvz2-arm64/`; this worker did not select, start or operate that VM.

Version 4 replaces the fallback with a UI-thread `DisplaySnapshot`. On each hardware diagnostic click, Activity captures current window bounds, its measured content view dimensions, available system-bar/cutout insets, Activity resource DisplayMetrics density, display ID/rotation, current display refresh rate and raw supported physical modes/current mode ID. Android API30+ uses `WindowManager.getCurrentWindowMetrics()`; API28/29 uses laid-out decor-view bounds or the actual app DisplayMetrics if layout is unavailable. Snapshot time is retained as elapsed realtime nanoseconds. JNI reads typed fields and arrays, validates unknown/invalid values, and passes one snapshot to the shared collector/serializer. The same serialized report is written to `diagnostic.json` and returned to the UI; no separate display-text substitute is used. The unused JavaVM/Activity pointer fields are removed in favor of the synchronous snapshot pointer, so the background collector retains no Activity/JVM reference.

`width_px`/`height_px` now explicitly describe **Activity window bounds**, including system areas where reported by WindowMetrics. Content dimensions and insets are separately available for later UI fitting. Supported-mode dimensions remain the display's physical mode coordinates, and are never forced into the current window orientation or used as the app's window size. This distinction follows the [WindowMetrics API contract](https://developer.android.com/reference/android/view/WindowMetrics#getBounds()). Display refresh is labeled `display_refresh_not_game_fps`; it comes from [Display.getRefreshRate](https://developer.android.com/reference/android/view/Display#getRefreshRate()), not an assumed 60 Hz or a game-frame measurement. No render scaling, stretching, UI cropping, orientation lock, system display setting or input mapping is changed by this checkpoint.

The CLI has no Activity snapshot and now returns `unavailable_no_activity_context`, zeros and an empty mode list. It no longer guesses active window size/rate from RG model defaults or sysfs mode lists. Unknown JNI values also stay zero/unavailable or partial, rather than becoming 60 Hz. The AudioTrack probe remains unimplemented: Android audio status is `not_run_no_audiotrack_probe`, every unmeasured numeric capability is 0 and channel configuration is unknown/empty. ALSA card count is not presented as Android output-device count. No audio is played, no AudioTrack is created and no system volume is changed. Game/audio acceptance and this new APK's Android display execution remain **NOT_RUN** pending the coordinator's separate execution check.

Meaningful host interface validation uses the production `DisplaySnapshot.java`, `ReadDisplaySnapshot`, `CollectDisplay` and JSON serializer through a real host JVM native library with `-Xcheck:jni`. Eleven cases cover 1080x2340/density440, rotation with unchanged physical-mode coordinates, a **synthetic** 3000x1920/144-Hz data set, a smaller multiwindow viewport, NaN refresh, invalid density, absent dimensions, mismatched mode arrays, empty modes, a null snapshot and an object of the wrong class. They confirm exact values survive Java-to-JNI-to-JSON, missing values are not invented, and invalid interfaces leave no pending JNI exception. Synthetic tablet dimensions/rates are test inputs and are not Honor or RG477M measurements. The CLI's existing schema/file/summary tests also check display/audio zeros and unknown status. Host configure/build, warning-strict JNI test-library compile, Java fixture compile, 11-case JVM test, headless probe tests and evidence-regression tests all exit 0. Full commands, output and exits are in `.local/logs/display-fix/`; Java's installed JDK emits its existing source/target8 deprecation warnings, with compile exit0.

The v4 APK is cross-built from an immutable existing `41a79716dafb9f48cfc34b3e8209dae88b04ec82` runtime/graphics snapshot to isolate this Android measurement change. Runtime, HLE, graphics and coordination sources are unchanged. The first build exits 0 with all 31 recorded step exits0, stable source fingerprints, `apksigner verify`0, aapt package `org.ffrs.diagnostic`/versionCode4/`0.4-measured-display`, and AArch64/16KiB/JNI/DEX structure checks0. Its precommit package is preserved at `.local/artifacts/android/checkpoint-display-v4-precommit/ffrs-diagnostic-debug.apk`: 426516 bytes, SHA256 `f03694cd6975d6a6117341f6b1cf1e339cf1ff53c90265e2f420dd80b83be563`. Complete first-build logs: `.local/logs/android-display-precommit.{log,exitcode}` and `.local/logs/android-build.AXUfcn/`. Current authoritative artifact identities/source HEAD/dirty state are generated by `.local/artifacts/android/build-evidence.json`; a clean source-commit rebuild updates that evidence and its unique step-log pointer. This uses the same partial, per-member CRC-verified official toolchain, with whole NDK archive checksum still NOT_VERIFIED.

An environment disconnect interrupted one later patch, not a build. After recovery, read-only inspection confirmed HEAD `e59c2fa`, all 15 prior changes present, and the insets-availability/test additions absent. Only the verified missing changes were then applied, followed by the real tests/build above. No unknown patch or command was blindly replayed.

The emulator hard-link staging permission failure is handled independently in the following helper checkpoint. The display unit does not change SELinux/root policy or publish/copy private guest files. The coordinator rebuilds with its own existing local signing identity before updating its installed diagnostic host. No signing private key is copied between workers.

## Private-file staging compatibility checkpoint

Actual coordinator Android `run-as ... ln` returned Permission denied. The helper now uses `adb -s <explicit serial> shell -T` with **one complete remote command string**, `run-as org.ffrs.diagnostic sh -c <shlex-quoted script>`. The script uses `umask 077; set -C; cat > files/libffrs_leaf.so` for direct exclusive creation. No hard link, rename, automatic destination deletion, root or SELinux change is used. Production CLI validation still requires the exact reviewed 2176-byte local artifact/SHA256 before any ADB action. Default remains dry-run, with no device steps. Copy mode retains online-state, RG477M model and already-installed debuggable-app gates.

After creation, exact size and a complete binary readback SHA256 must match before chmod0400 and COPY_COMPLETE_ONLY. Neither exit0 from `exec-out` nor success-looking text establishes a copy. Repeating the command with the identical existing regular file returns ALREADY_PRESENT_VERIFIED after read-only size/hash verification, preserving its inode, contents, mtime and existing permissions. An existing different file, directory or symlink (including a dangling symlink) is refused without modification. This is direct creation, **not atomic completed-file publication**: the user must wait for a successful verified result before explicitly invoking leaf validation.

If copying, transport, readback or chmod fails, the target may be partial or belong to a competing writer. The helper leaves it in place, reports FAIL_STAGE/PARTIAL_OR_UNCERTAIN_RETAINED with readiness=false and a recovery instruction, and never automatically deletes/overwrites it to retry. A subprocess timeout or spawn failure is recorded as UNKNOWN_NOT_CONFIRMED. Existing-destination failures separately state that the file remains unchanged. All results keep leaf execution and game startup NOT_RUN.

`tests/device/test_stage_leaf_library.py` uses synthetic bytes, a fake ADB transport and isolated host temporary directories. It actually exercises the production POSIX noclobber script on those host paths, verifying exclusive creation/readback/0400, no-op dry-run, normal idempotence, preservation of same/different existing files and permissions, directory/dangling-symlink protection, a racing writer, partial transfer, timeout, readback corruption, chmod denial, false-success exec-out error output and non-RG device rejection. It also rejects unreviewed payloads and missing serials. No ADB process, device operation or guest byte execution occurs in the test. Actual helper execution against RG477M remains NOT_RUN. Logs/exit codes: `.local/logs/cxx-staging/stage-tests.*`.

## Typed compiler-runtime link checkpoint

The production standalone HLE pipeline now consumes immutable runtime `14ed86aa0587857b30be45181af742aa430824a4`, archived under `.local/source-inputs/14ed86a/`; all 16 runtime/NSO snapshot files were compared to the exact Git blobs. That runtime supplies typed Android strong references to `__cxa_guard_acquire`, `__cxa_guard_release` and `__cxa_guard_abort` in `nso_host.c`, while excluding those names from Android's generic dlsym allowlist. No guard function or runtime core file is implemented or changed here.

Both the minimal script and opt-in CMake target link only selected members from this group, in order: `libc++abi.a`, `libc++_static.a`, `libunwind.a`, Android builtins, then `--end-group -lc -lm -ldl`. There is no whole-archive or synthetic guard implementation. The build rechecks the four cached official archives against the original member size/CRC manifest, records their SHA256 and compares them before/after linking. The source fingerprint guard and strict C11 warnings remain enabled. CMake sysroot/compiler-resource paths are explicit for partial-toolchain builds and derive from the configured compiler for an installed NDK.

The common `build_runtime_hle.sh --verify-cxx-link <readelf> <elf> <map>` static gate is also called by CMake. It requires all three guard definitions, selection of `libc++abi.a(cxa_guard.cpp.o)` in the linker map, and a `GNU_EH_FRAME` program header. Direct LLD and CMake both explicitly use `--eh-frame-hdr`. The first direct-link artifact was confirmed to have `.eh_frame` but no header; the corrected build has the header. The coordinator separately reported an older Android guard-contract exception/catch abort134, so static definitions alone are **not** labeled behavioral compatibility. No Android ELF, guest function or guard contract is executed by this worker; Android contract/unwind behavior remains NOT_RUN here, with the prior reported failure awaiting a separate updated-device check.

Minimal and partial-toolchain CMake configure/builds exit0, each passing the ordinary AArch64/PIE/16KiB/dependency gate plus the compiler-runtime static gate. The minimal candidate is 558312 bytes, SHA256 `86e831e6a47e86771f7f1be2288c57a54f92b317ed5979e4fa8eb49da3ef6dea`; CMake's independently compiled candidate is 521392 bytes, SHA256 `1cb0ca59bef591b93c98720f04f4b5971571a66399b868aafcb5de3b7092d1b0`. Different driver flags/layout account for their distinct identities; neither is conflated with the runtime worker's prototype. The CLI still needs only dynamic `libc.so`, `libm.so`, `libdl.so`. Its host object relocations confirm real strong references to all three guard symbols. Negative checks reject a map missing the official guard member and the actual earlier ELF lacking `GNU_EH_FRAME` (both exit1 as expected). Logs and command/actual exit records are `.local/logs/cxx-staging/`, `.local/logs/android-hle-build.BC85ei/`; the current authoritative artifact/evidence is `.local/artifacts/android-runtime-hle/build-evidence.json`. The precommit build has 14 step exits0 and 65 stable source fingerprints; a clean source-commit rebuild updates HEAD/dirty evidence and its unique log pointer.

This unit leaves APK/JNI load-only, guest TLS SDK binding unimplemented, and unsupported imports fatal. No device operation or system policy change occurs. The official toolchain is still partial, with complete NDK archive checksum NOT_VERIFIED.

## Legacy API28/29 inset semantics correction

The reviewed API28/29 branch used `getSystemWindowInset*()`, whose [Android contract](https://developer.android.com/reference/android/view/WindowInsets#getSystemWindowInsetBottom()) includes IME and depends on visibility. It now measures each side as the maximum of [stable system-bar insets](https://developer.android.com/reference/android/view/WindowInsets#getStableInsetBottom()) and [DisplayCutout safe insets](https://developer.android.com/reference/android/view/DisplayCutout#getSafeInsetBottom()). The API30+ branch retains `getInsetsIgnoringVisibility(systemBars | displayCutout)`. Both branches therefore describe system bars and cutout geometry excluding IME, with the source API and explicit scope serialized inside `system_bar_cutout_insets_px`. No viewport fitting, render scale, input mapping or system setting changes.

Null/consumed legacy WindowInsets remain unavailable. The pure Java combiner clears a previous result before rejecting invalid values; native normalization also clears measurements with absent source or negative sides. JNI transfers the same source and values to the ordinary report/UI, rather than changing display text alone.

The real host JVM interface regression now has 15 synthetic cases. Added checks cover per-side stable/cutout maxima, unchanged inset geometry with a smaller content viewport, invalid legacy capture clearing stale values, and a missing measurement source rejected by native normalization. All previous orientation, density, window/mode and invalid-interface cases remain. Host CMake configure/build, strict JNI library compile, Java fixture compile, the 15-case `-Xcheck:jni` test, the existing headless probe tests, Activity compile against Android34, three strict Android API28/AArch64 C++ compile-only commands and `git diff --check` all exit0. Exact commands/output/exit files are `.local/logs/legacy-insets/`; Java source/target8 deprecation notices retain compile exit0. The Android C++ checks use the same partial official NDK members, Apple Clang and Bionic headers; complete NDK archive checksum remains NOT_VERIFIED.

This checkpoint starts from clean Android source `af049ab9c41530c47365248c8a5a38e31e41ebda` and does not rebuild or replace any APK. Its compile/test products are isolated under `.local/build/legacy-insets/` and `.local/build/host/`. API28/29 device execution is **NOT_RUN**; synthetic JVM inputs are not hardware measurements. The coordinator now owns the connected RG477M session. This worker performs no ADB/device operation and does not read or modify that session's logs. Game startup, audio playback and gameplay acceptance are unchanged by this correction.

## Remaining gates

1. Total NDK archive SHA1 remains pending. The working partial official toolchain is sufficient for the recorded real Android cross-build; do not equate that with a completed SDK-manager package installation.
2. The parent should integrate this independent HLE CLI checkpoint and rebuild against its approved, frozen runtime revision. Carrier, hardware UI and optional-leaf checkpoints have already been integrated. The machine evidence generator records each build identity automatically.
3. The coordinator owns the now-connected RG477M install/JNI/module/Vulkan/crash checks. This worker's host compilation and synthetic interface tests do not establish any hardware result or request another USB connection.
4. This worker used no Android device or emulator. No game constructor/entry point, title screen, scene, gameplay or performance gate passed. Any later coordinator emulator results must identify their own artifact and execution scope.

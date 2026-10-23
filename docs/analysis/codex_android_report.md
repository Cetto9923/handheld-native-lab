# T03A Codex Android — verified cross-build and APK carrier

2026-10-23, worktree `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-android`, branch `task/T03A-codex-android`, starting commit `d26d3f6`.

## Scope and status

This is an Android carrier and load-only diagnostic pipeline. It does not claim game startup, rendering, title screen, gameplay, or performance. The application invokes the shared `src/runtime/nso/nso_loader.c` in place, then releases the module; it never calls constructors or a game entry point. No game module/resource/credential is packaged. Missing `modules/main.nso` is reported as `BLOCKED_MODULE`.

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

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| `.local/artifacts/android/probe_rg477m` | 2872776 | `f4b939ad90a008555ba4eb8da6960e722207d3f8fb9642538ee8ceca70ae7ece` |
| `.local/artifacts/android/nso_diagnostic` | 2878608 | `9233ec028bd750ccd1ac77596b42302b2307e76d50842b220d88c05071e48e33` |
| `.local/artifacts/android/libffrs_diagnostic.so` | 3235968 | `8494df957ab097c374b4ccb7d425e160d32bef5188acc40c0f64aa6f8699cd5b` |
| `.local/artifacts/android/ffrs-diagnostic-debug.apk` | 414228 | `42d379034ea53b5cff5ce23eac452d0df4325e288857a77fefc4ae27aeb1aefa` |

The APK passes v3 signature verification and aapt reports version 0.2, Chinese label, API28/target34 and arm64-v8a. The artifact verifier additionally checks ET_DYN, AArch64, valid 16 KiB LOAD congruence and no writable+executable LOAD segment. All device/game gates remain BLOCKED_DEVICE/NOT_RUN.

## Remaining gates

1. Total NDK archive SHA1 remains pending. The working partial official toolchain is sufficient for the recorded real Android cross-build; do not equate that with a completed SDK-manager package installation.
2. The parent should integrate the hardware UI checkpoint onto its frozen `e860f99` tree, then rebuild from relative source paths to establish the final integrated build identity. The first Android checkpoint has already been integrated by the parent.
3. Connect the user's RG477M for install, JNI invocation, module load, Vulkan capability capture and crash evidence. Until then all on-device and game-execution gates remain NOT_RUN/BLOCKED_DEVICE.
4. No Android device or emulator was used. No game constructor/entry point, title screen, scene, gameplay or performance gate passed.

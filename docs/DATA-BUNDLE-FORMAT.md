# Local native-data bundle v1 (implementation in progress)

One deliverable directory contains the APK and its matching `.hnldata` file. The latter is a ZIP64 stream, not a downloadable game supplied by the source repository. Build it only from the user's authorized local inputs. Game/SDK/generated shader bytes remain under `.local`; saves, keys and firmware are excluded.

The archive has exactly this order:

1. `bundle.properties` (≤4096 bytes): `format=hnl-native-data/1`, `abi=arm64-v8a`, `bundle_id`, `native_sha256`, `index_sha256`, `file_count`, `payload_bytes`. Duplicate or unknown fields are refused.
2. `files.tsv` (≤32MiB): one `sha256<TAB>size<TAB>relative_path<LF>` row per payload file, ordered by Java String/UTF-16 ordinal comparison, without duplicates. Each path is relative, ≤1024 characters, and cannot contain traversal, empty segments, backslashes, drive prefixes or reserved importer metadata names.
3. One `payload/<relative_path>` entry per index row, in the same order. No extra or omitted entries are accepted. Large already-compressed Paks are stored; generated XGS may be deflated. ZipInputStream verification is based on complete indexed payload bytes, not a trusted filename or ZIP size claim.

The ID is SHA256 of `index_sha256 + "\n" + native_sha256` (UTF-8). The APK must carry its trusted index digest and critical input hashes; the importer must derive/verify the actual installed native library digest. The archive header cannot supply its own trust contract. Changing native/runtime or data inputs requires a matched package. No hash check is represented as a license grant.

The streaming Java core validates metadata, ABI/runtime/index pairing, space, index totals, required input hashes, canonical paths, each file's actual length/SHA and end of archive. It writes a new staging/version directory and switches `active.properties` atomically only after verification. Previous complete versions and saves are not overwritten. Failed imports retain a deterministic .hnl-import-<id> stage; selecting the same bundle again reuses only SHA-verified files and rechecks the complete stream. Existing completed versions are fully rehashed before reactivation. Per-file data and metadata are synced; real power-loss recovery is not yet verified. Do not call this a finished installer yet.

Payload roots are `main.nso`, `sdk.nso`, `romfs/` and canonical `shaders/<16hex>.xgs`. Device profiles, logs, caches and stable saves are runtime-owned; they must not be imported from a user's old save or diagnostic dump. Preserve stable saves across version changes. The actual frozen input index currently has73511 files/15192663714 bytes. That requires explicit storage preflight; a compressed package on the same volume needs additional space during import.

Reproduction:

```sh
python3 scripts/distribution/native_data_bundle.py index --game-root <local-game-root> --runtime-root <local-sdk-and-shaders> --out .local/<new-index>
python3 scripts/distribution/native_data_bundle.py pack --index .local/<new-index> --apk .local/<matching-apk> --out .local/<new-release>/data.hnldata
bash scripts/distribution/check_data_bundle_contract.sh
python3 tests/installer/check_bundle_interop.py
```

`index.json` contains private local source paths for the build host and never ships inside the data archive or GitHub. The APK contract contains only cleaned metadata/digests. SAF UI, sanitized trusted APK assets, per-volume space display, cancellation/recovery, and stable internal save/runtime path wiring are implemented in preview002. Actual full ZIP64 import and end-to-end managed startup are under test; component checks alone are not device acceptance. All full game/audio/performance/device gates remain mandatory beyond these component checks.

## Directory-first variant (user preference, 2026-10-30)

`export_data_directory.py --index <prepared> --apk <directory-mode-apk> --out .local/<new>` verifies the APK contract and source inventory, creates a new `HandheldNativeLab/` containing APK, `data/` and a Chinese readme, and hashes every exported file. No saves or private source inventory are exported. Visible `data/hnl-index.tsv` and `hnl-data.properties` identify the compatible data; directory validation is read-only. The app checks a bounded set of fixed locations on actual Android storage volumes, never a recursive whole-disk search. Modern private sideload builds expose an explicit system all-files access page as required for native direct paths; ZIP/SAF remains an alternative build.

The first successful directory use fully hashes all indexed files. A remembered path+contract permits subsequent length/index checks; this is not a per-launch full corruption scan. Runtime markers/cache/logs are separate under the app internal runtime/<versionID>, shader fallback and SDK use the read-only copied data, and saves remain at internal files/saves. Actual SD startup/performance and whole-game gates remain pending.

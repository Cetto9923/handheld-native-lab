# Next stage: one-directory installation and device adaptation

User requirement recorded2026-10-30: keep the accepted baseline recoverable, then provide an easy installer and data package in one directory, aiming for installation followed by immediate launch across different device models. This is a future deliverable, not a property of the current diagnostic baseline.

The directory should contain the application APK, a manifest identifying its matching authorized local data package, a simple installation entry point, and concise recovery instructions. Exact package design and supported-model list still require implementation and real-device evidence. Do not upload third-party game data or treat a private GitHub repository as permission to redistribute it.

Required behavior for the next task:

1. Recognize ABI/device capabilities and choose a verified graphics/resolution/frame-pacing profile. Preserve game time, inputs, audio and cutscene speed. Maintain30Hz as the initial floor; higher targets require actual field/battle and audio validation on each device. No RG477M result may stand in for another model.
2. Resolve the game directory reliably with the existing candidates and Intent override. Check required Paks/version/checksums, available space and permissions; show an actionable setup screen for missing or mismatched data. No silent hang, root requirement or manual hidden-marker editing in the intended end-user flow.
3. Install matching app/data safely, preserve saves, distinguish upgrades from clean installation, and provide recoverable rollback. Separate internal rendering, UI/output resolution and display refresh rate.
4. Retain the complete standing audio checklist: A2DP field/battle ear checks, NONE, buffer policy, TIMEOUT retry, disconnect/reopen, fence spin0, package+SHA. Include graphics effects, fallen-character flicker, UI, buildings, black-screen/block and RG16 regression gates.
5. Demonstrate cold start, Continue, movement, cutscene, battle, suspend/resume, data-error recovery and reinstall/upgrade on every claimed model. Publish only the tested compatibility matrix and known limits; do not label an untested universal installer ready.

The first action when resuming is to restore/verify this baseline, then design the installer around the recorded acceptance gaps. Do not overwrite the baseline tag, APK or data backup while developing the next version.

## User refinement: directory-first delivery

2026-10-30 user prefers an already-extracted `HandheldNativeLab/` directory containing APK + `data/`, copied once to phone or SD root. Prioritize bounded automatic discovery and direct native reads after an explicit first-use file-access grant when Android requires it. Do not force a second full import/copy. Keep ZIP/SAF import as an optional fallback. A directory must match the APK index/ABI/runtime and contain all SDK/shaders/Paks; missing data must show setup. Preserve existing Intent/legacy candidate behavior. Save/cache/log/profile directories remain app-owned and separate from copied data. Modern Android cannot silently read arbitrary shared directories without applicable access; no fake URI-to-path grant or lowered target SDK workaround.

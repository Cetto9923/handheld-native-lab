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

## 2026-10-31 公开案例复核：采用目录优先、首次授权、原位读取

用户最新指令：数据预先解压，用户拷贝到手机或储存卡，安装APK后自动检索加载。此次为资料与现有实现核对，无设备操作、无APK重建、无渲染/音频修改。

证据：
- ScummVM Android官方文档 https://docs.scummvm.org/en/latest/other_platforms/android.html 明确支持microSD/USB拷贝游戏文件，然后通过系统选择目录并授权。借鉴的是文件交付与授权流程，不是其运行引擎。
- 固定源码 revision 38af1883a2eb7a622c31bfc3708888bbcc47db48（v2.9.0解引用commit，git ls-remote实际返回）：https://github.com/scummvm/scummvm/blob/38af1883a2eb7a622c31bfc3708888bbcc47db48/backends/platform/android/org/scummvm/scummvm/SAFFSTree.java 。实际读取确认持久URI授权与ContentResolver桥接，不是把content URI转为任意文件路径。根COPYING为GPLv3文本：https://github.com/scummvm/scummvm/blob/38af1883a2eb7a622c31bfc3708888bbcc47db48/COPYING 。仅参考设计，无复制源码或新增依赖。
- Android官方全文件访问 https://developer.android.com/training/data-storage/manage-all-files ：Android11+需要用户显式允许；可访问共享存储及SD/OTG根，不能借此访问其他app私有Android/data；上架Play另有严格用途限制。当前侧载目录直读可以使用该明确授权，不保证任意分发渠道均适用。
- Android官方SAF https://developer.android.com/training/data-storage/shared/documents-files ：选择目录获得URI访问，持久化授权可供后续使用；Android11限制选储存根/Download根及Android/data、obb，应该选HandheldNativeLab子目录。SAF直读需适配现有native fopen/mmap路径，现有ZIP导入备选不等于已经实现SAF目录原位读取。

落实口径：
1. 交付 HandheldNativeLab/，内含APK、data/、简短说明和版本清单。用户整目录拷贝到手机或SD卡根，或Games/、Download/下，安装APK后打开。
2. 首次先有界检查这些约定目录，不递归扫全盘、不假设知道APK安装前所在目录、不硬编码SD UUID。找到有效唯一匹配数据后校验并原位读，不再复制整份大数据。
3. 无权限时明确引导首次授权；拒绝可以返回/重试，不循环弹窗、不黑屏。未找到、缺文件、错误版本、SD移除均显示具体恢复办法。多个匹配目录的选择规则需验收，不能任意选过期目录。
4. 首次完整SHA显示进度；后续快速检查有明确边界，数据更新后重新验证。存档、缓存和日志放app自己的独立目录，覆盖升级保留存档；卸载会清除app私有存档，应先提供备份路径。
5. 当前DataBundleInstaller已有手机/SD卷枚举+三个约定目录+合同校验原位读取。此轮不另造一套安装器。SAF目录原位访问仍是后续兼容方案，不能承诺目前各种机型均可零授权启动。
6. 验收补充：手机/SD、根/Games/Download位置；新装无权限/同意/拒绝/重开；目录缺失/不完整/不匹配/移除；覆盖升级存档；首检进度和暖启动。只有实际测过的设备进入支持矩阵。安装流程通过不替代仍开放的30Hz、音频、视觉及内存增长门。

## Candidate015 functional evidence (2026-10-31)
RG477M only: actual system permission revoke -> setup -> app permission entry -> real system grant -> automatic recheck -> real title verified.014 did full15.20GB SHA,015 reused the exact same native/data contract and performed the documented warm index/length check (not a second full hash).015 setup now hides repeated raw EACCES paths while permission is absent, retaining scan diagnostics in logs. Three save-file hashes survived both upgrades.013 has an actual Files/SystemInstaller install test;014/015 upgrades used ADB for diagnosis and are not claimed as clean first installs.015 artifact digests and raw screenshots/logs are indexed in.local/installer-20261030/RESULT.md.
Native present CSV now records queued/skipped/failed results; old traces without that field cannot establish successful-present FPS. RG477M014 lifecycle evidence separated338 queued from142 skipped calls. This is a title/background-return test at8% battery, not gameplay30Hz or full audio/visual acceptance. Candidate gameplay performance and standing audio gates remain open.

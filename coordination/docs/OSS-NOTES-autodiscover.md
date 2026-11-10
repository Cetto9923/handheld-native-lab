# OSS-NOTES｜Android 自动发现与多存储卷扫描技术出处

> **工单**：`P-AGY-INSTALL-autodiscover` / `P-DIST-1-foolproof-install-autodiscover`  
> **写手**：Antigravity (agy)  
> **日期**：Asia/Shanghai 2026-11-01  
> **标准**：无公开出处大改 = FAIL。先查行业与开源社区经验，在此记录出处、URL、Commit/日期与工程借鉴点。

---

## 1. Context.getExternalFilesDirs() 存储根路径解析（物理 SD 卡 / 次级卷）

- **开源项目**：Simple Mobile Tools / Simple Commons (Android 开源文件管理器与基础库)
- **仓库地址**：`https://github.com/SimpleMobileTools/Simple-Commons`
- **文件参考**：`commons/src/main/kotlin/com/simplemobiletools/commons/extensions/Context.kt`
- **提交哈希**：`7be50d2ef07cf64dd7ba24300bf8dbd2524eeec6` (2023-09-14)
- **核心逻辑**：
  在 Android 4.4+ 至 Android 15/16 中，系统并不总是提供统一且免权限的物理 SD 卡/次级卷根目录公共 API。通过调用 `context.getExternalFilesDirs(null)`，系统会返回所有已挂载卷上的应用私有目录数组（`File[]`）：
  - 索引 0：主内部存储（如 `/storage/emulated/0/Android/data/<pkg>/files`）
  - 索引 1+：外置物理 SD 卡、次级存储卷或 USB OTG（如 `/storage/XXXX-XXXX/Android/data/<pkg>/files`）
  通过截取 `/Android/data` 之前的子串即可可靠提取该外置介质的根目录（`/storage/XXXX-XXXX`），进而扫描挂载介质上的游戏目录（如 `FFRS` / `Games/FFRS`），且完全不依赖硬编码 UUID 或设备型号。
- **本仓应用**：
  在 `GameActivity.java` 的 `candidateGameDirs()` 中引入该解析逻辑，同时将 `ext/game`、`ext/FFRS` 与 `ext` 本身作为候选，确保用户拷到外置卡或应用专属目录时均能即开即用。

---

## 2. AOSP StorageManager.getStorageVolumes() 动态存储卷枚举

- **开源项目**：Android Open Source Project (AOSP) `platform/frameworks/base`
- **参考源码**：`core/java/android/os/storage/StorageManager.java` & `StorageVolume.java`
- **源码链接**：`https://android.googlesource.com/platform/frameworks/base/+/refs/heads/main/core/java/android/os/storage/StorageManager.java`
- **官方文档**：`https://developer.android.com/reference/android/os/storage/StorageManager#getStorageVolumes()`
- **核心逻辑**：
  从 Android 7.0 (API 24) 引入 `getStorageVolumes()`，并在 Android 11 (API 30) 提供公开的 `StorageVolume.getDirectory()` 返回挂载根目录 `File`。对于处于 `MEDIA_MOUNTED` 或 `MEDIA_MOUNTED_READ_ONLY` 状态的卷，直接获取其根路径。
- **本仓应用**：
  在 `GameActivity.java` 的 `candidateGameDirs()` 中通过 `StorageManager.getStorageVolumes()` 动态遍历所有在线挂载卷，结合 `/storage` 与 `/mnt/media_rw` 目录枚举，确保掌机（RG477M）与平板（荣耀 GT Pro）在有/无外置 TF 卡、USB 随身盘时均能枚举真实挂载根。

---

## 3. PPSSPP Android 作用域存储与游戏目录回退发现机制

- **开源项目**：PPSSPP (PlayStation Portable emulator for Android)
- **仓库地址**：`https://github.com/hrydgard/ppsspp`
- **议题与提交**：Issue #11997 ("Android 12 scoped storage hell") & Commit `bb0ddad1c4f9f7ba3043b8110b6feeeceb161295`
- **讨论链接**：`https://github.com/hrydgard/ppsspp/issues/11997`
- **核心经验**：
  当 Android 11+ 启用 Scoped Storage 后，用户若未授权 `MANAGE_EXTERNAL_STORAGE`，应用无法直接遍历 `/sdcard/` 根目录。PPSSPP 评估并引导用户将游戏存放在 `getExternalFilesDir(null)` 下（即 `/sdcard/Android/data/<pkg>/files`），因为该目录无需任何系统运行时权限即可读写，是权限被拒时的免特权保底路径。
- **本仓应用**：
  在 `candidateGameDirs()` 中保留 `getExternalFilesDirs(null)/game` 作为免权限候选；若全盘权限被拒，引导界面明确告知用户可使用该免权限路径放置数据包。

---

## 4. Android 官方 All Files Access (MANAGE_EXTERNAL_STORAGE) 引导模式

- **参考文档**：Android Developers: "Manage all files on a storage device"
- **文档链接**：`https://developer.android.com/training/data-storage/manage-all-files`
- **核心逻辑**：
  对于拥有 `MANAGE_EXTERNAL_STORAGE` 权限声明的应用，在 Android 11+ 上使用 `Environment.isExternalStorageManager()` 检查权限；未授权时，通过 `Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION` 配合 `package:<packageName>` URI 精确跳转至当前应用的管理页面，若厂商 ROM 拦截则降级跳转至全量权限列表 `Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION`。
- **本仓应用**：
  在 `GameActivity.java` 的 `requestFileAccess()` 与 `refreshSetup()` 中完整实现该规范，并在用户授权返回 (`onResume` / `onWindowFocusChanged`) 时自动触发 `recheckGame()`。

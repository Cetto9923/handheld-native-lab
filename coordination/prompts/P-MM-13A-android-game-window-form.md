# P-MM-13A 工单卡｜Android 游戏窗口形态（GameActivity + SurfaceView + ANativeWindow 交接）

- **工单 ID**：P-MM-13A
- **角色**：ANDROID 实现 Agent（写手）
- **派发方**：MiniMax Code 总控（本轮主写手负责 NVN 设备主线，路径互不重叠）
- **分支**：`work/rg477m-native`（**同仓库同分支**，主写手与本 Agent 的写入路径互不重叠，见下"写入范围"）
- **前置**：T02 / T03A 已过；P-MM-11 侦察结论已在 `docs/analysis/display_bridge_requirements.md` 与 `docs/analysis/nvn_device_requirements.md`

## 1. 目标

主树当前的 Android 侧只有**诊断用途**的 Activity：

- `src/android/app/java/org/ffrs/diagnostic/DiagnosticActivity.java` 是唯一的 Activity；
- `src/android/app/java/org/ffrs/diagnostic/DisplaySnapshot.java` 只交接显示**参数**（尺寸 / insets / 密度 / 刷新率 / 旋转），**没有 ANativeWindow**；
- `src/android/app/AndroidManifest.xml` 里没有 gamepad required、没有 surface 相关声明；
- native 由 Activity 的 JNI 一次性调用（`runNativeDiagnostic` / `runNativeLeafDiagnostic`）。

⇒ **运行时拿不到任何可呈现的窗口**。`nn::vi::GetNativeWindow` 的接缝（规格见 `docs/analysis/display_bridge_requirements.md`）交出的应该是 Android 的 `ANativeWindow*`，而**唯一现实来源就是 Activity 里的 SurfaceView**。

本工单只做**窗口形态**：让 app 拥有一个游戏 Activity，它的 SurfaceView 把 `ANativeWindow*` 交到 native，native 侧有一个**可被运行时读取的窗口持有者**。本工单**不**实现 `nn::vi` 导入（那是主线 `src/runtime/hle/nso_host.c` 的文件，写入权在主写手），**不**实现 Vulkan/NVN（主线在 `src/graphics/**`）。

## 2. 输入（可只读）

- `docs/analysis/display_bridge_requirements.md`：显示桥规格（`nn::vi` 5 个导入、guest 显示对象 0x60 字节布局、`GetNativeWindow` 接缝）
- `src/android/app/**`、`src/android/native/**`：现有诊断 app 与 JNI 形态（**照抄它的风格**）
- `src/android/native/CMakeLists.txt`：现有 native 构建接线
- 宿主能力：`~/Library/Android/sdk` 有 `build-tools/34.0.0` 与 `platforms/android-34`；JDK 23 可用；**没有 gradle**，需要自己写构建脚本（参考仓库既有脚本风格：`scripts/graphics/build_vulkan_transfer_android.sh`）

## 3. 输出（交付物）

1. **游戏 Activity**：新增 `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`（或更合适的包名，保持与既有 `org.ffrs.diagnostic` 一致），拥有 `SurfaceView`，在 `surfaceCreated` 时把 `SurfaceHolder.getSurface()` 的 `ANativeWindow` 交给 native，在 `surfaceDestroyed` 时释放。
2. **JNI 交接**：新增 `src/android/native/game_window_jni.h` / `.cpp`，提供 `nativeSetSurface(long windowPtr)` 与 `nativeClearSurface()`（名字可自定，但语义必须明确）。**必须**用 `ANativeWindow_fromSurface` 之外的正确取法：Java 侧 `Surface` 的 native 指针由 `SurfaceHolder`/`Surface` 的 `getSurface()` → JNI `GetObjectClass`/`GetFieldID` 取不到可靠的 `ANativeWindow*`，**正确做法**是 Java 侧持 `Surface`、native 侧用 `ANativeWindow_fromSurface(env, surface)`（NDK `android/native_window.h` + `android/native_window_jni.h`）。请自行核实并说明取法依据。
3. **native 窗口持有者（新建文件）**：`src/runtime/hle/native_window.h` / `src/runtime/hle/native_window.c`——一个**极小**的持有者：存下 `ANativeWindow*`、提供 `nso_host_native_window_get()`（返回当前窗口或 NULL）、`nso_host_native_window_set(ANativeWindow*)`、`nso_host_native_window_clear()`。
   - **硬约束**：`src/runtime/hle/native_window.c` **不得** include `android/native_window.h`（否则宿主 macOS 编译不过）。窗口指针用 `void*` 存，`native_window.c` 只做存取与日志；JNI 层负责 `ANativeWindow` 类型转换。
   - 该文件**不**接入运行时主流程（接入由主写手在 `src/runtime/hle/nso_host.c` 做），但**必须**能被宿主与 Android 两种编译通过。
4. **构建脚本**：新增 `scripts/android/build_game_window_apk.sh`（名字可自定），用 `aapt2` + `d8` + `apksigner` + `javac` + NDK clang 手工打出**可安装的 APK**（不依赖 gradle），产物落在 `.local/android-apk/`（**产物不入 git**）。脚本必须打印每一步真实退出码，失败即失败。
5. **验证**：在 RG477M 上安装并启动该 APK，确认 Activity 起来、SurfaceView 拿到窗口、native 收到 `ANativeWindow*`（用日志证明，并打印窗口尺寸/format，与 `DisplaySnapshot` 已有参数对照）。**不**许清设备数据、不 root、不刷机。

## 4. 写入范围（**唯一写者**，不要越界）

允许写：

- `src/android/app/**`
- `src/android/native/game_window_jni.h`、`src/android/native/game_window_jni.cpp`、`src/android/native/game_activity_main.cpp`（如需）
- `src/android/native/CMakeLists.txt`
- `src/runtime/hle/native_window.h`、`src/runtime/hle/native_window.c`（**新建**，主写手不会碰这两个文件）
- `scripts/android/**`
- `docs/analysis/android_game_window_form.md`（自己的报告）

**禁止写**：`src/runtime/hle/nso_host.c`、`src/runtime/hle/nso_host.h`、`src/graphics/**`、`tests/native/**`、`scripts/runtime/**`、`coordination/**`、`src/runtime/nso/**`。若确实需要改这些，**停下并在报告里写出需要的改动**，交主写手集成。

**禁止**：不改 `main`/`master`，不 push，不 force-push，不 `reset --hard`/`clean -fd`/`stash pop`，不删 `.local/romfs/**`，不改 `task/T04-nvn-firstframe` 与 `.claude/worktrees/*`，不调用 agy。

## 5. 验收

- [ ] APK 构建脚本真实退出码 0，产物路径 + sha256 落盘
- [ ] APK 安装到 `RG477M01445122` 并启动成功（真机证据，非推断）
- [ ] 日志证明 native 侧拿到了 `ANativeWindow*`，且尺寸与 `DisplaySnapshot` 的参数一致
- [ ] `src/runtime/hle/native_window.c` 在**宿主 macOS** 与 **Android arm64** 两种编译下都通过（宿主编译命令见 `scripts/runtime/build_guest_process_contract.sh` 里的 `strict` 变量）
- [ ] 未修改任何禁止路径（`git status` 自证）
- [ ] 报告写进 `docs/analysis/android_game_window_form.md`

## 6. 口径红线

- **不许**宣称已经出现画面 / 已经可玩——本工单只交付**窗口形态**，不交付渲染。
- **不许**伪造窗口尺寸、刷新率、输入数据、帧率；所有数字来自真机日志。
- 窗口拿不到就如实写"拿不到"，不要用占位窗口顶替。
- 不要动游戏包、存档、romfs。

## 7. 恢复点

若中途受阻：保留未提交改动（不要 clean），在报告里写清「已完成到哪一步 / 下一步精确命令 / 阻塞原因」，并在 `docs/analysis/android_game_window_form.md` 顶部写明检查点。

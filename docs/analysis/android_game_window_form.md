# P-MM-13A · Android 游戏窗口形态（GameActivity + SurfaceView + ANativeWindow 交接）

- 工单：P-MM-13A ｜ 角色：ANDROID 实现 Agent（写手）｜ 日期：2026-10-24
- 仓库：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`
- 分支：`work/rg477m-native` ｜ 起点 HEAD：`7b3801dd3327b1a4dd6c6f48664945f0e0bc3465`
- 设备：`RG477M01445122`（Anbernic RG477M，Android 14 / SDK 34，Mali，MTK）

## 0. 口径声明（先读这一段）

本工单**只交付窗口形态**：Activity → SurfaceView → JNI → native 侧可被运行时读取的
`ANativeWindow*` 持有者。

- **没有画面**：本工单不渲染任何像素，窗口里是空的 SurfaceView，屏幕上是系统背景色。
- **没有可玩、没有帧、没有帧率、没有输入**：不执行游戏代码，不绑定 `nn::vi` 导入，不做 Vulkan/NVN。
- 报告里出现的 `1280x960`、`format=1`、`120.00000762939453 Hz` 全部来自真机日志原文，
  没有一个数字是写死的或推算出来的。
- 窗口拿不到就写"拿不到"——本次窗口**拿到了**，证据见 §6。

## 1. 结论摘要

| 项 | 结果 | 证据 |
|---|---|---|
| Activity → SurfaceView → ANativeWindow → native 持有者 | **通过** | §6 真机日志 |
| native 侧窗口尺寸与 `DisplaySnapshot` 已有参数一致 | **通过**（1280×960 = 1280×960） | §7 |
| `src/runtime/hle/native_window.c` 宿主 macOS 编译 | **通过**（`-Wall -Wextra -Werror`） | §8 |
| `src/runtime/hle/native_window.c` Android arm64 编译 | **通过**（`-Wall -Wextra -Werror`） | §8 |
| APK 构建脚本真实退出码 0 | **通过**（17 步全 0） | §5 |
| APK 安装 + 启动 | **通过**（`Status: ok`，crash buffer 空） | §6 |
| 产物路径 + sha256 落盘 `.local/android-apk/` | **通过** | §5 |
| 未改禁止路径 | **通过**（我方改动全部在白名单内） | §9 |

## 2. 窗口链路

```
GameActivity (org.ffrs.diagnostic.GameActivity)
  └─ SurfaceView(holder.setFormat(RGBA_8888))
       └─ SurfaceHolder.Callback.surfaceCreated
            └─ holder.getSurface()  →  java Surface 对象
                 └─ JNI: GameWindowNative.setSurface(Surface)
                      └─ ANativeWindow_fromSurface(env, surface)   ← 唯一的窗口真源
                           └─ nso_host_native_window_set(window)   ← runtime/hle 持有者
                                └─ 运行时读 nso_host_native_window_get()
```

销毁路径对称：`surfaceDestroyed` / `onDestroy` → `GameWindowNative.clearSurface()`
→ `ANativeWindow_release()` + `nso_host_native_window_clear()`。

## 3. ANativeWindow 取法依据（为什么不是"从 Surface 里抠字段"）

工单要求"自行核实并说明取法依据"，核实结果如下。

1. **NDK 官方接口就是为这件事准备的。**
   NDK r26c sysroot `usr/include/android/native_window_jni.h` 声明：

   ```c
   /**
    * Return the ANativeWindow associated with a Java Surface object,
    * for interacting with it through native code.  This acquires a reference
    * on the ANativeWindow that is returned; be sure to use ANativeWindow_release()
    * when done with it so that it doesn't leak.
    */
   ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface);
   ```

   同文件头注释明确它属于 `Native Activity` API 组，唯一职责就是"Java Surface →
   ANativeWindow"的映射。取法与本工单一致：`Java` 侧持 `Surface`，`native` 侧调
   `ANativeWindow_fromSurface(env, surface)`。

2. **"用 GetFieldID 从 Surface 抠 native 指针"这条路在 API 34 上走不通。**
   `javap -p android.view.Surface`（`platforms/android-34/android.jar`）输出的全部成员里
   **没有任何字段**（只有方法和一个 `static {}`）——SDK stub 根本不暴露 `Surface` 的原生句柄，
   也就没有任何"受支持的字段"可读。JNI 确实能绕过 Java 访问控制读到私有成员，但那是
   **未文档、无版本保证的内部实现契约**，读出来的也未必是 `ANativeWindow*`；
   NDK 只承诺用 `ANativeWindow_fromSurface()` 完成这个映射，并把引用计数一并管好。
   结论：唯一可核实、可依赖的取法就是 `ANativeWindow_fromSurface`。

3. **设备真的提供这个符号。** 从真机拉出 `/system/lib64/libandroid.so` 后用
   `llvm-readelf --dyn-syms` 实测：

   ```
   559: 00000000000194a0  120 FUNC  GLOBAL DEFAULT 15 ANativeWindow_fromSurface@@LIBANDROID
   460: 0000000000025380   96 FUNC  GLOBAL DEFAULT 15 ANativeWindow_getWidth@@LIBANDROID
   637: 0000000000025440   96 FUNC  GLOBAL DEFAULT 15 ANativeWindow_getFormat@@LIBANDROID
   ```

   NDK 侧 `libandroid.so` stub 的导出表也一致（`ANativeWindow_fromSurface` @ 0x64b8）。
   本工单的 `libffrs_game_window.so` 实际 NEEDED 只有
   `libandroid.so` / `liblog.so` / `libm.so` / `libdl.so` / `libc.so`。

4. **引用计数必须自己管。** 头文件写明 `ANativeWindow_fromSurface` 会 *acquire* 一份引用。
   所以 `native_window.c` 只存裸指针 + 由 JNI 层负责 release；宿主 macOS 侧因此完全不需要
   NDK 头文件（见 §8）。

## 4. 交付物清单

| 文件 | 职责 |
|---|---|
| `src/runtime/hle/native_window.h`（新） | 窗口持有者接口：`nso_host_native_window_get/set/clear` + `generation`。指针类型是 `void*`，不含任何 Android 头 |
| `src/runtime/hle/native_window.c`（新） | 持有者实现：C11 原子存取 + 世代计数。**不 include `android/native_window.h`**，不解引用指针 |
| `src/android/native/game_window_jni.h`（新） | JNI 接口：`ffrs_game_window_state` 结构 + 三个 `Java_org_ffrs_diagnostic_GameWindowNative_*` 导出声明 |
| `src/android/native/game_window_jni.cpp`（新） | JNI 实现：`ANativeWindow_fromSurface` 取窗、`ANativeWindow_release` 放窗、互斥锁串行化持有者访问、`__android_log_print` 打证据日志 |
| `src/android/app/java/org/ffrs/diagnostic/GameWindowNative.java`（新） | Java 侧 native 入口（`setSurface` / `clearSurface` / `describeWindow`），负责加载 `libffrs_game_window` |
| `src/android/app/java/org/ffrs/diagnostic/GameActivity.java`（新） | 游戏 Activity：全屏 SurfaceView(RGBA_8888)、`SurfaceHolder.Callback`、`DisplaySnapshot` 采集、尺寸对照日志 |
| `src/android/app/gamewindow/AndroidManifest.xml`（新） | 独立验证用 manifest（application id `org.ffrs.gamewindow`，**不覆盖**设备上已有的 `org.ffrs.diagnostic`） |
| `src/android/app/gamewindow/res/values/strings.xml`（新） | 独立验证 APK 的应用名 |
| `src/android/native/CMakeLists.txt`（改） | 新增 `ffrs_game_window` 共享库目标（C11、链接 `android`/`log`、并入既有 eh_frame 校验与链接选项循环） |
| `scripts/android/build_game_window_apk.sh`（新） | 无 gradle 构建：javac → jar → d8 → aapt2 compile/link → zip → zipalign → apksigner；每步打印真实退出码 |
| `scripts/android/install_game_window_apk.sh`（新） | 真机安装 / 启动 / logcat 取证脚本（每步打印真实退出码） |
| `docs/analysis/android_game_window_form.md`（本文件） | 本报告 |

**没有触碰**（工单禁止路径）：`src/runtime/hle/nso_host.c`、`src/runtime/hle/nso_host.h`、
`src/graphics/**`、`tests/native/**`、`scripts/runtime/**`、`coordination/**`、`src/runtime/nso/**`。

## 5. 构建证据

命令：

```bash
bash scripts/android/build_game_window_apk.sh
```

真实退出码：**0**（17 个步骤全部 `exit_code=0`，逐条记录在
`.local/logs/android-game-window/<step>.exitcode` 与 `.log`）：

```
exit_code[host-compile-native-window]=0     exit_code[cmake-configure]=0
exit_code[cmake-build-game-window]=0        exit_code[android-elf-header]=0
exit_code[android-compile-native-window]=0  exit_code[javac]=0
exit_code[jar]=0                            exit_code[d8]=0
exit_code[aapt2-compile]=0                  exit_code[aapt2-link]=0
exit_code[strip]=0                          exit_code[payload-zip]=0
exit_code[zipalign]=0                       exit_code[apksigner-sign]=0
exit_code[apksigner-verify]=0               exit_code[aapt2-badging]=0
exit_code[sha256]=0
```

产物（**不入 git**，`.local/` 已在 `.gitignore`）：

```
.local/android-apk/ffrs-game-window-debug.apk
.local/android-apk/ffrs-game-window-debug.apk.sha256
sha256: db09f13e22195fe225515b88a5e641c234f88acc23a88b35da073c5ea87594ba
```

> 注：APK 是 zip 容器，条目带打包时间戳，因此**每次重建的 sha256 都不同**
> （本次两次重建分别为 `c93c9064…` 与 `db09f13e…`）。报告与 `.sha256` 文件里的值对应
> **最后一次构建**；判断"设备上跑的是哪一版"要靠 §6 里从设备 pull 回来重算的 sha256 比对。

**清零重建也通过**：删掉 `.local/build/android-arm64-game-window`（cmake 构建目录）
后重跑构建脚本，真实退出码仍为 **0**，17 步全 0——即 `src/android/native/CMakeLists.txt`
里新增的 `ffrs_game_window` 目标可以从零配置、编译、校验，不依赖任何缓存产物。

工具链：宿主 `~/Library/Android/sdk`（build-tools 34.0.0 / platform android-34）、
NDK `26.1.10909125`（`toolchains/llvm/prebuilt/darwin-x86_64`）、
JDK 23.0.2 / javac、CMake 4.4.4。**没有 gradle，没有联网。**

APK 自检（`aapt2 dump badging` 实测输出）：

```
package: name='org.ffrs.gamewindow' versionCode='1' versionName='0.1-game-window-form'
launchable-activity: name='org.ffrs.diagnostic.GameActivity'
native-code: 'arm64-v8a'
```

签名：`apksigner verify` 通过（v3 scheme，CN=FFRS Local Game Window，本地开发自签密钥，
与任何用户/发布凭据无关）。

## 6. 真机证据

命令（`scripts/android/install_game_window_apk.sh` 逐步执行，每步真实退出码）：

```bash
adb -s RG477M01445122 install -r .local/android-apk/ffrs-game-window-debug.apk   # exit 0, Success
adb -s RG477M01445122 shell am force-stop org.ffrs.gamewindow                    # exit 0
adb -s RG477M01445122 logcat -c                                                  # exit 0
adb -s RG477M01445122 shell am start -W -n org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity
# exit 0
# Status: ok
# Activity: org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity
# WaitTime: 93
adb -s RG477M01445122 logcat -d -s FFRS-GameWindow:I '*:S'                        # exit 0
adb -s RG477M01445122 logcat -d -b crash -t 200                                  # exit 0（空）
```

**安装件与产物逐字节一致**（从设备 pull 回来算 sha256，与最后一次构建比对）：

```
本地   db09f13e22195fe225515b88a5e641c234f88acc23a88b35da073c5ea87594ba  .local/android-apk/ffrs-game-window-debug.apk
设备上 db09f13e22195fe225515b88a5e641c234f88acc23a88b35da073c5ea87594ba  /data/app/~~6riNgs_0LR2J2Vi8wNXNFA==/org.ffrs.gamewindow-JVTQGNjoxEe3jWSMiwNIwQ==/base.apk
```

### 6.1 native 侧拿到窗口（logcat 原文，最后一次构建）

```
10-24 19:49:04.872 21208 21208 I FFRS-GameWindow: window_state event=set_surface present=1 pointer=0xb4000073eef8b330 width=1280 height=960 format=1 generation=1 released=0x0
```

- `present=1`：持有者里确实有一个非空窗口指针。
- `pointer=0xb4000073eef8b330`：这是 `ANativeWindow*` 本体（真机 ASLR 地址，每次运行不同）。
- `width/height/format` 是 native 侧用 `ANativeWindow_getWidth/Height/Format` 读回来的
  **缓冲区几何**，不是 Java 侧抄过去的数字。

### 6.2 Java 侧与 native 侧对照（同一行日志）

```
10-24 19:49:04.873 21208 21208 I FFRS-GameWindow: window_form display_width_px=1280 display_height_px=960 content_width_px=1280 content_height_px=960 surface_width_px=1280 surface_height_px=960 inset_left_px=0 inset_top_px=42 inset_right_px=0 inset_bottom_px=0 size_source=WindowManager.currentWindowMetrics.bounds insets_source=WindowInsets.getInsetsIgnoringVisibility(systemBars|displayCutout) density_dpi=280 refresh_rate_hz=120.00000762939453 rotation=0 native_present=1 native_pointer=0xb4000073eef8b330 native_width_px=1280 native_height_px=960 native_format=1 native_generation=1 error=
```

### 6.3 Surface 回调与持有者回读

```
10-24 19:49:04.873 21208 21208 I FFRS-GameWindow: surface_changed format=1 width_px=1280 height_px=960
10-24 19:49:04.873 21208 21208 I FFRS-GameWindow: window_readback present=1 pointer=0xb4000073eef8b330 width_px=1280 height_px=960 format=1 generation=1
```

### 6.4 销毁路径（按 HOME 让 Activity 退到后台，surface 真的被销毁）

```
10-24 19:49:08.221 21208 21208 I FFRS-GameWindow: window_state event=clear_surface present=0 pointer=0x0 width=0 height=0 format=0 generation=2 released=0xb4000073eef8b330
10-24 19:49:08.221 21208 21208 I FFRS-GameWindow: surface_destroyed native_window_cleared
```

`released=` 是被 `ANativeWindow_release` 掉的那一份引用，`generation` 从 1 变 2，
说明持有者确实被清空、引用确实被归还（不泄漏）。

> 说明：`adb shell am force-stop` 是直接杀进程，不走生命周期，所以不会触发
> `surfaceDestroyed`；销毁路径是用 HOME 键触发 Activity 退后台验证的。

### 6.5 崩溃缓冲

`adb logcat -d -b crash` 在最终构建上**为空**（`.local/logs/android-game-window/logcat-crash.log`
当前为空文件）。

第一次安装时确实发生过一次真实崩溃，已定位并修复，原始堆栈摘录如下（**这是历史证据**，
该次运行的日志文件已被后续成功运行覆盖；复现方式见 §11.1）：

```
E AndroidRuntime: FATAL EXCEPTION: main
E AndroidRuntime: Process: org.ffrs.gamewindow, PID: 17465
E AndroidRuntime: java.lang.RuntimeException: Unable to start activity ComponentInfo{org.ffrs.gamewindow/org.ffrs.diagnostic.GameActivity}: java.lang.NullPointerException: Attempt to invoke virtual method 'android.view.WindowInsetsController com.android.internal.policy.DecorView.getWindowInsetsController()' on a null object reference
E AndroidRuntime: Caused by: java.lang.NullPointerException: ... DecorView.getWindowInsetsController() ...
E AndroidRuntime: 	at org.ffrs.diagnostic.GameActivity.configureWindow(GameActivity.java:53)
E AndroidRuntime: 	at org.ffrs.diagnostic.GameActivity.onCreate(GameActivity.java:39)
```

## 7. 尺寸一致性对照

| 来源 | 宽 | 高 | 出处 |
|---|---|---|---|
| `DisplaySnapshot.width_px/height_px`（`WindowManager.currentWindowMetrics.bounds`） | 1280 | 960 | logcat `window_form` |
| `DisplaySnapshot.content_width_px/height_px` | 1280 | 960 | logcat `window_form` |
| `SurfaceView` 实测宽高 | 1280 | 960 | logcat `window_form` |
| `SurfaceHolder.Callback.surfaceChanged` | 1280 | 960 | logcat `surface_changed` |
| **native `ANativeWindow_getWidth/Height`** | **1280** | **960** | logcat `window_state` |

五项一致。`format=1` 即 `WINDOW_FORMAT_RGBA_8888`（SurfaceView 显式
`setFormat(PixelFormat.RGBA_8888)` 的结果，也是后续 Vulkan surface 需要的格式）。

补充实测量（同一行日志，非推断）：`density_dpi=280`、`refresh_rate_hz=120.00000762939453`
（设备当前 mode 2 = 120 Hz）、`rotation=0`、`inset_top_px=42`（状态栏区域，
`getInsetsIgnoringVisibility` 口径，与既有 `DisplaySnapshot` 语义一致）。

## 8. 双平台编译验证

`src/runtime/hle/native_window.c` **不 include 任何 Android 头**，指针以 `void*` 存储，
指针的解释权完全在 JNI 层。两种编译都由构建脚本真实执行（`exit_code=0`）：

| 目标 | 命令 | 结果 |
|---|---|---|
| 宿主 macOS | `clang -std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -I src/runtime/hle -c src/runtime/hle/native_window.c` | exit 0 |
| Android arm64 | `<ndk>/bin/clang --target=aarch64-linux-android28 --sysroot=<ndk>/sysroot -fPIC -std=c11 -Wall -Wextra -Werror -I src/runtime/hle -c src/runtime/hle/native_window.c` | exit 0 |

宿主侧的 strict 标志与 `scripts/runtime/build_guest_process_contract.sh` 里的 `strict` 变量一致。
另外 `libffrs_game_window.so` 也走了仓库既有的 `tests/device/verify_eh_frame.py` 后置校验
（`.local/build/android-arm64-game-window/libffrs_game_window.so.eh.json`：
`eh_frame_bytes=644`、`eh_frame_hdr_bytes=148`、`PT_GNU_EH_FRAME=true`）。

## 9. `git status` 自证

我方改动**全部**落在工单白名单内（`src/android/app/**`、`src/android/native/game_window_jni.*`、
`src/android/native/CMakeLists.txt`、`src/runtime/hle/native_window.*`、`scripts/android/**`、
本报告）。禁止路径的改动是**主写手在我工作期间并发产生的 WIP**，不是本工单写的：

```
 M coordination/state.json                     ← 主写手（协调记录，非本工单）
 M scripts/runtime/build_guest_process_contract.sh  ← 主写手（NVN device 接入，非本工单）
 M src/runtime/hle/nso_host.c                   ← 主写手（NVN device 接入，非本工单）
?? src/graphics/nvn_device/                    ← 主写手（NVN device 接入，非本工单）
?? coordination/prompts/P-MM-13A-android-game-window-form.md  ← 总控下发的工单卡，非本工单
 M src/android/native/CMakeLists.txt           ← 本工单（仅新增 ffrs_game_window 目标）
?? src/android/app/gamewindow/                 ← 本工单
?? src/android/app/java/org/ffrs/diagnostic/GameActivity.java      ← 本工单
?? src/android/app/java/org/ffrs/diagnostic/GameWindowNative.java  ← 本工单
?? src/android/native/game_window_jni.cpp       ← 本工单
?? src/android/native/game_window_jni.h         ← 本工单
?? src/runtime/hle/native_window.c              ← 本工单
?? src/runtime/hle/native_window.h              ← 本工单
?? scripts/android/                             ← 本工单
```

`src/android/native/CMakeLists.txt` 的完整 diff 只有一处新增（`ffrs_game_window` 目标 +
把它并进既有的链接选项 / eh_frame 校验循环），没有触碰既有目标的行为。

## 10. 需要主写手集成的改动（本工单按工单要求**没有**做）

1. **`nn::vi::GetNativeWindow` 接线**（`src/runtime/hle/nso_host.c`，写入权在主写手）：
   在 `GetNativeWindow(void** out, Layer*)` 里把 `*out = nso_host_native_window_get()`；
   窗口为空时按规格如实失败，不要写占位指针。
2. **运行时构建接线**（`scripts/runtime/build_guest_process_contract.sh`，写入权在主写手）：
   `core_sources` 的 hle 列表里要加上 `native_window`，否则 `nso_host.c` 引用
   `nso_host_native_window_get()` 时链接不到 `native_window.c`。
   当前该列表形如：
   `[*[hle/(s+'.c') for s in ['guest_context','guest_process','nso_host','address_region','mutex','exit_registry','module_tls','guest_tp']]]`
   → 需要补 `'native_window'`。
3. **`src/android/native/CMakeLists.txt` 的 `nso_runtime_hle` 目标**（我的文件，但语义上属于
   主写手的运行时目标）：若要让运行时 CLI 链上持有者，需要把
   `"${FFRS_HLE_DIR}/native_window.c"` 加进该 target 的源文件列表。本工单**没有**加，
   以免影响主写手正在进行的 NVN 设备接线与 `verify_runtime_hle_elf.py` 门禁。
4. **游戏 Activity 并入正式 app**：把 `GameActivity.java` / `GameWindowNative.java` 与
   `src/android/app/gamewindow/AndroidManifest.xml` 里的 `<activity>` 声明并入正式
   `org.ffrs.diagnostic` manifest，并把 `libffrs_game_window.so` 挂进正式构建的
   `ffrs_diagnostic`（或独立库）打包流程。

## 11. 风险与未完成项

1. **首次安装时确实崩溃过一次，已修**：`onCreate` 里直接调 `getWindow().getInsetsController()`
   触发 `PhoneWindow.getInsetsController()` NPE（DecorView 尚未安装）。原始堆栈见 §6.5。
   修法：先取 `getDecorView()` 安装 DecorView，且把隐藏系统栏的动作挪到
   `onWindowFocusChanged(hasFocus=true)`（此时 DecorView 已 attach）。
   **修复后的最终构建 crash buffer 为空**；把 `configureWindow()` 改回在 `onCreate` 里直接取
   insets controller 即可复现该崩溃。
2. **窗口只是"空"的**：本工单没有渲染任何像素，屏幕上是空的 SurfaceView。
   **不得**据此声称出现画面、可玩或有帧率。
3. **`nn::vi` 五导入仍未绑定**，`GetNativeWindow` 尚未接线（见 §10），运行时目前
   仍然拿不到窗口——本工单交付的是**取得窗口的那条通路**，不是接线本身。
4. **持有者尚未被运行时读取**：`nso_host_native_window_get()` 目前只有 JNI 层与
   `describeWindow` 回读在用，运行时主流程还没接（按工单要求接入属于主写手范围）。
5. **引用计数**：`ANativeWindow_fromSurface` 的引用由 JNI 层持有并释放；
   `native_window.c` 只存裸指针。如果运行时在 `surfaceDestroyed` 之后仍缓存这个裸指针，
   就是 use-after-free —— 主写手接线时必须遵守"窗口失效即停止使用"的约定。
6. **应用 id 分离**：验证 APK 用 `org.ffrs.gamewindow`，设备上原有的
   `org.ffrs.diagnostic` 诊断 app **没有被覆盖**（见 §10.4 的并入动作）。
7. **窗口尺寸随设备旋转/分辨率变化**：本次只验证了当前 1280×960、120 Hz、
   rotation 0 的状态。分辨率适配与 UI 分辨率分离仍按 `docs/PROJECT.md` 的方案走，
   本工单没有触碰。
8. **真机 QA 未独立复跑**：本工单是实现者自测，设备证据可复现，但
   `coordination/acceptance_matrix.md` 里的独立 QA 复跑不在本工单范围内。

## 12. 复现命令

```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native

# 1) 构建（宿主 macOS + Android arm64 双编译 + APK）
bash scripts/android/build_game_window_apk.sh ; echo "exit=$?"

# 2) 真机安装 + 启动 + 取证
adb devices                       # 确认 RG477M01445122 在线
bash scripts/android/install_game_window_apk.sh ; echo "exit=$?"

# 3) 产物校验
shasum -a 256 .local/android-apk/ffrs-game-window-debug.apk
cat .local/android-apk/ffrs-game-window-debug.apk.sha256
```

日志留档：`.local/logs/android-game-window/`（每步 `.log` + `.exitcode`）。

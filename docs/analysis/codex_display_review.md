# Display 普通功能独立审查

冻结提交：`dcae98e878fd5df0902e6e0bde7c00a51b1f4074`。本报告仅为源码、正常主机 JVM/JNI 及 JSON 功能检查，不是设备测量。

## 需修复：P2 — API 28/29 与 API 30+ 的 inset 字段含义不一致

位置：`src/android/app/java/org/ffrs/diagnostic/DiagnosticActivity.java:174-180`，对照 `155-161`；JSON 输出位于 `src/android/native/probe_core.cpp:175-180`。

API 28/29 分支用 `getSystemWindowInset*()` 填充统一的 `system_bar_cutout_insets_px`。旧 getter 可以包含 IME 等系统窗口，并随这些窗口的可见状态变化；API 30+ 分支则调用 `getInsetsIgnoringVisibility(systemBars | displayCutout)`。因此旧系统下可能把键盘遮挡量标为导航栏/cutout，隐藏系统栏时也与新分支口径不同。依据为 Android 官方 [WindowInsets 文档](https://developer.android.com/reference/android/view/WindowInsets#getSystemWindowInsetBottom())；该发现来自 API 契约及源码对照，尚无 API 28/29 设备复现。

最小建议：旧分支用四侧 `getStableInset*()`，与可获得的 `DisplayCutout.getSafeInset*()` 逐侧取最大值；无法可靠取得对应测量时保留 unavailable，或单独输出 legacy 来源/可见性口径。稳定 inset 与忽略可见性的 system bar API 对应关系见同一官方文档。

## 已完成的检查

- Java UI 线程捕获快照，`Thread.start()` 后由 worker 同步交给 JNI；native 复制数据，未保留 Activity/JVM 引用。`DisplaySnapshot` 完成后没有继续修改，所有字段在同一次 report 中使用。
- 窗口 bounds、实际 content 尺寸、物理 mode 尺寸及旋转分开记录。API 30+ bounds 包含系统栏，mode 的物理尺寸可与应用尺寸不同，符合官方 [WindowMetrics](https://developer.android.com/reference/android/view/WindowMetrics#getBounds()) 与 [Display.Mode](https://developer.android.com/reference/android/view/Display.Mode#getPhysicalWidth()) 定义。
- 未提供 Activity 快照时显示值保持 0/unavailable；Android 音频保持 `not_run_no_audiotrack_probe`，未填 RG477M 分辨率、刷新率或音频默认值。
- 冻结版内部 JSON 消费者已通过普通测试；数值字段仍保留，但窗口 `display.width_px/height_px` 不可直接当验收 schema 的 `physical_resolution`。`tests/acceptance/schema/device-capability.schema.json:175-215` 是另一份验收结构，需从 mode/独立设备证据映射，不代表 probe JSON 已通过设备验收。

## 实际命令结果

Apple Clang 21.0.0 / macOS arm64；C++ 使用 `-std=c++20 -Wall -Wextra -Werror -pedantic`。

| 检查 | 真实退出码 |
|---|---:|
| JNI 测试 dylib 严格编译 | 0 |
| Host probe 严格编译 | 0 |
| 原 11 例 Java → 生产 JNI → CollectDisplay → JSON，`-Xcheck:jni` | 0 |
| Host probe JSON/file/summary 测试 | 0 |
| DisplaySnapshot/fixture Java 编译 | 0 |
| Activity/DisplaySnapshot 对本地 API 34 SDK 编译 | 0 |

最初 Java `-Xlint:all -Werror --release 8` 因 JDK 23 对旧 release 选项的弃用提示退出 1；改用 `-Xlint:all,-options -Werror` 后通过，源码 lint 仍按错误处理。全部 argv、cwd、日志、真实退出码及冻结源码 SHA-256 位于同目录 `manifest.json`。

## 限制及断连恢复确认

API 28/29/30-34 真机行为、Activity 实际旋转/多窗口、Android 音频、RG477M、游戏及 T02 均未在本轮执行。11 例输入均为主机合成 fixture，不能视为任何设备测量。

补充并发 fixture 创建请求曾因 exec-server 断连失败。随后已只读确认 `DisplaySnapshotConcurrentTest.java` 不存在，manifest 没有并发命令；因此补充并发测试未执行，且未重试。未执行游戏、NSO、TP 操作、VM 或旧中止 QA 流程，仓库未修改。

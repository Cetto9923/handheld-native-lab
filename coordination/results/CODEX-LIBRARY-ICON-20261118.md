# CODEX-LIBRARY-ICON-20261118 — Cetto 图标接入

任务所有者：`/root/library_icon_design`。用户要求为游戏仓库 APP 设计图标并实际替换。总控分配本任务；仅写本报告及独立图标目录，不修改库主线源码、manifest、设备或其他候选。当前状态：资源与安全100纯图标 APK 验证通过，等待总控装机与桌面验收。

## 设计与历史核对

采用上轮已经准备的 Cetto 深蓝底、薄荷青至青蓝渐变 C 形符号。原 README 明确记录“本设计任务未改工程”“工程接入及设备验收由游戏库开发线负责”，与用户遗漏项吻合。未重复生成新图像；本轮阅读 imagegen/icon-maker 技能后，执行的是已有设计的工程落地与审证。

原图：`/Users/yuyan9923/Documents/Codex/cetto-app-icon-20261118/cetto-icon.png`，1254×1254，无 alpha，1192426 bytes，SHA256 `bd7e3f001fa7bc0c5867c0b435988b56b20c12f1940a9d5d907905c8b5c6a494`。图标没有文字、角色或平台商标。设计文件与 README 已复制入独立目录保存出处。

资源包：`/Users/yuyan9923/GitHub/02-游戏原生移植/switch-android-port-rules/.local/codex-library-icon-20261118/`。`res/` 含15个图标资源，来自101候选并与当前103候选逐字节一致，避免改动已选设计。主线唯一写者为 `/root/library_oot_fix`。

## 资源与清单接入

- 旧版 mipmap：mdpi48、hdpi72、xhdpi96、xxhdpi144、xxxhdpi192，各含 `ic_launcher.png` 和带圆形 alpha 的 `ic_launcher_round.png`。
- `mipmap-anydpi-v26/ic_launcher.xml` 与 `ic_launcher_round.xml`：同一 adaptive 图标。
- `drawable/ic_launcher_foreground.xml`：fill bitmap 引用 `drawable-nodpi/ic_launcher_flat.png`（432×432）。
- `values/launcher_colors.xml`：背景 `#0f1828`。
- `<application>` 必须引用 `android:icon="@mipmap/ic_launcher" android:roundIcon="@mipmap/ic_launcher_round"`。

实际检查发现，`Manifest103.xml` 已有这两个属性，最早 code87 APK 也已包含它们。基础 `main-host/shell/android/AndroidManifest.xml` 缺失，已要求库主线代理串行补齐，避免独立构建回退到文字 tile。图标代理没有并发写入其 manifest/res。

adaptive 前景使用原始完整不透明平面图，未宣称分离成透明 C 前景。中心72dp视口与圆形/圆角遮罩的确定性预览在 `mask-and-small-size-preview.png`；图形实测最大半径30.03dp，小于108dp图层内33dp安全圆。48/32/24px预览保留清楚的 C 开口。系统动画/桌面实际裁切仍需真机观察。

## 已执行验证

1. `sh .../verify-resources.sh`：Android SDK build-tools35.0.0、platform35，aapt2 compile/link 退出0，日志 `resource-compile.log`。链接检查 APK 仅验证资源图，不是可安装应用交付。
2. `python3 .../verify-apk.py /var/folders/w0/hmtgdsj910x3ktwnfz0mnf780000gp/T/cetto-library.kPHvXw/cetto-portlab-preview.apk`：退出0，实际早期候选 versionCode87、versionName0.10.3-oot-internal、APK SHA256 `18cc9ba268f64aa910cbfaf86c970370ab2d172c442b866bf8c2123eb72f5115`。
3. 独立解析 packaged binary manifest 的 icon/roundIcon resource ID、两条 adaptive XML、前景 bitmap、背景颜色，完整链路通过。11张 PNG 的尺寸、alpha和所有可见 RGBA 像素与资源包匹配。aapt2 会把 alpha0像素RGB归零，仅该不可见RGB差异作规范化，alpha及可见像素保持严格比较。证据 `apk-audit-18cc9ba268f6/`。

上述是资源包及早期候选的审证，不等于最终重建或桌面显示通过。没有独立装机，没有改变应用数据和存档。

## 当前旧图标的实际原因

只读检查安全回退100/code84 APK `.../.local/delivery/cetto-portlab-0.10.0-fallback071.apk`，SHA256 `697e323d579570a3e5b4829fea920d7e80c41f15e73137e970e4cc7bcefc6b5b`：binary manifest 无 icon/roundIcon，aapt2 输出 `application: label='Cetto 移植库' icon=''`。因此该包的旧 tile 有直接打包原因，不能仅归为桌面缓存。证据 `installed-baseline100-manifest.txt` 和 `installed-baseline100-badging.txt`。若测试结束回退到原100，会同时回退图标；已提醒总控最终替换必须保留图标引用。

## 安全100纯图标内部包

总控追加明确授权后，已在 `baseline100-icon/` 独立构建，不等待OOT功能线。输出 `cetto-portlab-0.10.0-fallback071-icon.apk`，17609394 bytes，SHA256 `6ee483541961913e8bc89efb25727261f3f96ced29a6ca050ca90cd7cd96694e`。保留 versionCode84、versionName0.10.0-fallback071；来源安全100 SHA697e323d。

仅重链manifest与资源：使用100源码清单增加两项icon属性，原封面资源ID固定为0x7f010000/1；运行条目直接从100APK逐项复制，没有重新编译DEX。构建遵循既有build-tools35.0.0 aapt2→zipalign4→apksigner本地调试签名入口，工具消费原有keystore；未读取或输出私钥内容。

`verify-delta.py` 通过：原30个运行/封面条目均字节一致；包含全部classes、native及assets。binary manifest删去两条icon/roundIcon属性后与100 dump完全一致，无新增网络权限。允许变化为manifest、resources.arsc、新图标条目及重签名元数据；证书SHA256相同，原包与新包均通过V3签名验证。`verify-apk.py` 对新APK完整图标引用链、11 PNG尺寸/alpha及可见像素复验通过，证据 `apk-audit-6ee483541961/`。

恢复点：原100APK及099/101未修改，候选只在本任务独立目录；如需恢复由总控使用原100安装，应用数据不清除。

总控安装入口：`adb -P 5038 -s A42BBB5109000333 install -r <上述APK>`。本代理未执行安装。构建计划 `baseline100-icon/BUILD-PLAN.json`，普通日志 `build.log`，重建命令 `sh baseline100-icon/build-icon-only.sh`，差异验证命令 `python3 baseline100-icon/verify-delta.py`。

## 待完成

- 总控安装安全100纯图标内部包，并完成荣耀桌面与应用列表截图验收。
- 库主线唯一写者补齐基础 manifest 属性，最终串行重建87并交付APK。
- 对最终 APK 再执行 `verify-apk.py`，记录最终 SHA256。
- 总控安排安装并截图实际桌面/应用列表，核对 Cetto 新图标与圆形裁切，保留用户数据。

## 检查点

FF报告所属 worktree HEAD：`eb6b7ad9b73ed189ff34cf2901eee8bce805d15d`，分支 `task/T-codex-recovery-20261114`。主树已有他人 WIP，本任务未暂存、提交或修改它们。独立资源目录是未入 Git 的本地产物，完整资源字节与摘要在 `resource-audit.json`，检查点在 `CHECKPOINT.json`，复验命令在 `verify-resources.sh` 与 `verify-apk.py`。

## 独立视觉复核补充 · 2026-11-18

总控已在荣耀 `A42BBB5109000333` 安装100纯图标包，设备回读SHA256与 `6ee483541961913e8bc89efb25727261f3f96ced29a6ca050ca90cd7cd96694e` 一致。图标代理独立查看 `.../.local/codex-dots-takeover-20261118/icon-review/desktop-view.png`，第三排“Cetto 移植库”显示深蓝圆角底、完整青绿C形，开口和渐变清楚，图形边缘完整。`launched-view.png` 显示 CETTO 我的游戏页、两张游戏卡及开始游戏按钮，与总控记录的点击该桌面图标动作对应。以上覆盖此荣耀桌面与启动入口的实际替换验收。

设备记录 `icon-review/device.json` 提供安装与包哈希证据。外部目录52文件摘要检查通过。私有目录因应用不可调试而未获得摘要，报告按实际能力保留该检查缺口。

另对后续OOT87 APK `ee71fd8375458b6bfde42d032a4f0100c97e8d3a9bf51a6004a9506c02014840` 独立资源检查，发现OOT overlay 的 `values/colors.xml` 同名 `ic_launcher_background` 覆盖为棕色 `#ff3a1210`。已反馈库主线唯一写者移除该launcher专用颜色并重建，等待新APK复验。此后续候选问题与已装100图标包分别记录。

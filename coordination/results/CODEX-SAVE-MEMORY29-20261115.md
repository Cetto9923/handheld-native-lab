# 29 存档导出与内存回收修复验证

## 输入与范围

用户截图显示 Download 下连续生成 FFRS-save-时间戳目录，另一张为 OEM“严重的运行内存异常”后关闭应用通知。后者的具体退出原因仍待对应手机的 exit-info、logcat 或 tombstone，当前没有证明是 OOM。

本轮用户授权 RG477M01445122 独占测试及后续直接覆盖安装。荣耀平板 A42BBB5109000333 按用户指令暂缓。原5份未提交报告WIP保留并排除构建。游戏模块 Build ID 为 736837afd390442ed7c74a7de730e71b，原存档与历史APK保留。

## 存档目录根因与修复

生产 GameActivity.onStop 每次调用 exportSaves，后者创建 Download/FFRS-save-yyyyMMdd-HHmmss 并复制存档。28实机一次 Home 从26目录增至27，新增 FFRS-save-20261115-135130，证据 exports-before.txt、exports-after-home.txt。

29移除此生命周期自动调用，保留菜单的手动导出。此处选择手动导出以消除反复切后台时的公共目录增长。应用正常存档路径保持原实现。JVM抽取生产 onStop 连续调用3次，super计数3、导出计数0，显式导出计数1。重新植入旧调用反例能被同测试检出。设备验收待追加。

## 内存基线与拒绝候选

28初次世界地图 PID16067 TOTAL PSS 7,215,350KiB、Graphics 4,097,095KiB、Private Other 2,456,736KiB。62GiB为客机虚拟地址保留范围，不能直接算作实际RAM。

同APK1427条管线预热均失败0，16→4→16提交槽对照如下。每槽并非一个显示帧。

| 窗口 | PID | FPS | P95 ms | P99 ms | 最大 ms | GPU中位 ms |
| --- | --- | --- | --- | --- | --- | --- |
| A16 | 25352 | 29.99984 | 34.455 | 41.415 | 52.734 | 24.80 |
| B4 | 29843 | 14.80553 | 73.900 | 82.002 | 85.667 | 53.27 |
| Areturn16-valid | 5528 | 30.00014 | 34.968 | 41.101 | 52.208 | 24.57 |

A16末PSS6,851,356KiB、Graphics3,785,623KiB。B4世界PSS4,875,739KiB、Graphics1,911,655KiB。4槽节省内存但帧率退化，候选拒绝，源码恢复16，实验marker归档，未构建或交付4槽APK。Areturn16早期窗口误含loading，原始目录与无效标记保留，只使用已确认world的Areturn16-valid。

## 29候选实现与检查

Android arena最小块从32MiB调至4MiB，大请求仍按实际需要分配。保留16并发槽。readback trim按实际guard保留所有仍被其它提交引用的buffer，包含oversized块，并回收其它闲置块。销毁buffer推进memo_epoch，避免数值handle复用时命中旧descriptor。

ASan/UBSan抽取生产trim/destroy验证保护标准与oversized块、释放闲置块、重复trim、单次销毁和epoch变化，平台常量断言通过。复用gfx-memory-profile补MEMORY_RESIDENT统计texel_arena和vertex/index缓存，此前MEMORY_COUNTS未计入前者。未修改shader精度、深度、纹理格式或游戏逻辑时间。

构建源为27e9df8加root自有29修改，105项宿主检查与Android构建退出0，52份输入构建前后摘要一致。运行目录save-memory29-20261115/build29，命令与环境见plan.json、run.py及普通run.log。

APK .local/codex-recovery-20261114/apk-history/ffrs-1.0.29-preview-save-memory.apk，3,699,783字节，SHA256 ccb08cf9d9372ff685f5c882a4ebbe3863f1d393f07284029976bcc2538d7eef。

29已经覆盖安装RG，安装前后完整codex-local-save tar逐字一致，设备回读APK摘要一致。实际PID13758，预热1427/failed0。证据save-memory29-20261115/device29/plan.json、run.log及before/after-saves.tar。真机场景、内存和生命周期回归正在进行。

## 未完成

29同机位稳态FPS、战斗技能与开场显示/声音、长时内存增长、设备3次Home切换与菜单显式导出均待实际验证。其它机型的强退尚无本轮对应日志。独立QA未执行，当前不宣称战斗卡顿或全机型兼容已通过。

## 29初次world对照

PID13758、确认实际world后30秒903个间隔，30.00165FPS、P95 36.171ms、P99 49.318ms、最大61.360ms，9个间隔超过50ms。GPU中位24.90ms、submit CPU中位13.08ms、draws354.2、dispatch53.7，未出现新pipeline编译。PSS4,424,654KiB、Graphics1,536,911KiB、Native426,176KiB、PrivateOther2,420,804KiB。与28基线相比PSS约降35.4%，Graphics约降59.4%，同scene平均FPS仍30，帧尾抖动需更多样本核对。证据C29-16-small/。

## 真机生命周期与手动导出

29 PID13758连续3次Home/返回同一Activity，logcat对应3次onStop。公共Download的FFRS-save目录数保持27，集合逐字一致，进程PID保持。通过真实可见菜单“导出存档”后仅新增FFRS-save-20261115-142656，5份文件均与liveSaveRoot当前字节逐个匹配，原目录集合全部保留。证据lifecycle29/plan.json、logcat.txt、before/after-0..2.txt、manual-result.json、manual-readme.txt。

该生命周期回归自测通过。首次world内存与FPS均值改善已测，战斗、长时增长、帧尾分布与其它机型仍待后续验证。独立QA为NOT_RUN。

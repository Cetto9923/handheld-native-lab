# 29 存档导出与内存回收修复验证

## 输入与范围

用户截图显示 Download 下连续生成 FFRS-save-时间戳目录，另一张为 OEM“严重的运行内存异常”后关闭应用通知。后者的具体退出原因仍待对应手机的 exit-info、logcat 或 tombstone，当前没有证明是 OOM。

本轮用户授权 RG477M01445122 独占测试及后续直接覆盖安装。荣耀平板 A42BBB5109000333 按用户指令暂缓。原5份未提交报告WIP保留并排除构建。游戏模块 Build ID 为 736837afd390442ed7c74a7de730e71b，原存档与历史APK保留。

## 存档目录根因与修复

生产 GameActivity.onStop 每次调用 exportSaves，后者创建 Download/FFRS-save-yyyyMMdd-HHmmss 并复制存档。28实机一次 Home 从26目录增至27，新增 FFRS-save-20261115-135130，证据 exports-before.txt、exports-after-home.txt。

29移除此生命周期自动调用，保留菜单的手动导出。此处选择手动导出以消除反复切后台时的公共目录增长。应用正常存档路径保持原实现。JVM抽取生产 onStop 连续调用3次，super计数3、导出计数0，显式导出计数1。重新植入旧调用反例能被同测试检出。设备验收见下文，3次Home与手动导出均已实测。

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

29首同机位稳态FPS、战斗三技能流程、3次Home与菜单显式导出已执行，结果见下文。发行默认配置复验已完成，见末节。开场显示/声音、长期增长与其它机型强退尚无本轮完整验证。独立QA为NOT_RUN，当前不宣称战斗卡顿或全机型兼容已通过。

## 29初次world对照

PID13758、确认实际world后30秒903个间隔，30.00165FPS、P95 36.171ms、P99 49.318ms、最大61.360ms，9个间隔超过50ms。GPU中位24.90ms、submit CPU中位13.08ms、draws354.2、dispatch53.7，未出现新pipeline编译。PSS4,424,654KiB、Graphics1,536,911KiB、Native426,176KiB、PrivateOther2,420,804KiB。与28基线相比PSS约降35.4%，Graphics约降59.4%，同scene平均FPS仍30，帧尾抖动需更多样本核对。证据C29-16-small/。

## 真机生命周期与手动导出

29 PID13758连续3次Home/返回同一Activity，logcat对应3次onStop。公共Download的FFRS-save目录数保持27，集合逐字一致，进程PID保持。通过真实可见菜单“导出存档”后仅新增FFRS-save-20261115-142656，5份文件均与liveSaveRoot当前字节逐个匹配，原目录集合全部保留。证据lifecycle29/plan.json、logcat.txt、before/after-0..2.txt、manual-result.json、manual-readme.txt。

该生命周期回归自测通过。首次world内存与FPS均值改善已测，战斗、长时增长、帧尾分布与其它机型仍待后续验证。独立QA为NOT_RUN。

## 29技能流程回归与剩余长帧

同一PID13758通过正常草地遇敌、明镜、菲娜防御、机械陆行鸟、火炎剑到胜利结果页，人物及场景截图留存，未发生退出。本轮不同敌人存活数和时间窗口无法与27的主动阶段直接计算加速比。明镜标记后的约12秒含菜单与动作均值29.528FPS、最大142.979ms。机械陆行鸟标记后同类窗口均值29.281FPS、最大96.464ms。均值包含菜单，不能用于宣称特效稳30。

机械陆行鸟frame15024无新shader/pipeline，GPU73.95ms，持续GPU重负荷仍存在。火炎剑frame16465出现272.5ms间隔，其中pipeline创建248.7ms；16475出现626.5ms间隔，2条管线创建累计421.7ms、shader132.6ms，说明1427种子仍有实际组合缺口。

场景内存快照包括初入战斗PSS5,317,406KiB/Graphics2,279,818KiB、机械陆行鸟结束PSS5,407,256KiB/Graphics2,319,122KiB、火炎剑结果PSS5,500,720KiB/Graphics2,380,146KiB。该增长跨资源增加的不同阶段，无法据此称为泄漏。旧28同技能同机位内存尚无严格配对。

C29-repeat原30秒窗口末尾误与步行输入交叠，plan已显式标无效，不作同机位稳态对照。原始数据保留。首C29-16-small正常窗口未受影响。

正在保全PID13758最终日志/CSV/学习表和完整存档，并将本轮gfx-memory-profile=1重命名归档，恢复原始不存在状态。gfx-slot-count与gfx-no-warm原始均不存在，恢复核验纳入default29脚本。荣耀截图通知只证明该用户设备被系统关闭，掌机退出历史最近均为本轮工具显式force-stop，无法代替手机崩溃日志。

## 发行默认配置复验完成

gfx-memory-profile已归档恢复原始不存在，gfx-slot-count/gfx-no-warm同样不存在。新PID21740，Continue实际world已截图核对，1427预热失败0。D29-default在确认场景后无输入/无其它设备采样的30秒，903间隔，29.99983FPS、P95 35.517ms、P99 41.670ms、最大61.653ms、5个>50ms。GPU中位24.92ms、submitCPU13.10ms、draws353.9、dispatch53.4。PSS4,480,964KiB、Graphics1,560,117KiB、Native424,136KiB、PrivateOther2,456,460KiB。

对照28的有效同world基线PSS6,851,356KiB，29默认约下降34.6%；Graphics从3,785,623降到1,560,117KiB，约下降58.8%。P99接近原窗口，仍有单帧61.65ms，无法宣称全程锁帧。场景内存缩减与重复导出问题完成本轮实机自测，长时和其它机型尚未覆盖。旧导出目录27份以及本轮显式测试新增1份均保全，最终28份，无额外自动新增。

源码检查点7b5a8e4。APK字节为ccb08cf9，与设备安装回读一致。当前设备为29默认world、PID21740。本轮未执行荣耀平板测试，独立QA为NOT_RUN。主战斗GPU、初见shader加载与未覆盖pipeline创建仍开放。

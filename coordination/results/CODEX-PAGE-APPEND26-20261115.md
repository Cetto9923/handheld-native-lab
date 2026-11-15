# 26版页映射追加路径与预热补充

## 输入及改动

用户充电期间继续授权离线优化和测试。输入源码d0d5759，25压缩改写缓存候选已经构建，真实游戏验收待测。24录屏43秒附近有无编译长帧及连续页映射，主线程分项归因尚未取得。

源码检查发现guest_pages_map逐次遍历本区域全部allocation做重叠检查，nso_host_owned_pages_map也从accessible链表头查找插入点。连续追加N个新映射会重复扫描已建立的链表，累计工作量随N平方增长。

guest_pages在区域内保存已发布allocation的最高end。新地址达到该边界时，已有所有allocation均在新地址之前，可以直接进入原配额、VM和提交路径。释放后保留旧上界，低地址复用继续完整检查。分割只细分原范围，符合该保守边界。

address_region在区域内保存实际末尾interval。追加地址达到末尾end时直接使用next插入位置。free入口在原region_mutex内撤销提示，再沿原搜索、洞检查、分割、替换和回收路线执行。失败映射尚未发布新尾节点，隔离后的区域仍拒绝后续映射。区域整体释放沿原销毁次序清理。

两个提示均为私有字段，沿已有owner锁保护。源码差异集中在guest_pages.c和address_region.c，公开API和资源块粒度沿原实现。

## 组件验证

保全原两份源文件到.local/codex-recovery-20261114/offline-page26-20261115/baseline，分别构建前后版本。自有benchmark调用真实guest_pages与Context/VM入口，6个区域各追加4096页，共24576次映射，核对真实账本并完整回收。计时范围只包含该批映射循环，未执行游戏。

RG477M01445122的4KiB真实页组件在游戏停止时交替执行before/after/after/before/before/after。

| 路径 | 三轮毫秒 | 中位毫秒 |
| --- | --- | --- |
| 原实现 | 628.454、687.500、660.890 | 660.890 |
| 追加提示 | 30.883、26.838、27.538 | 27.538 |

这组组件循环减少约24倍耗时。每轮ledger_valid和cleanup均为1。Mac原先16KiB页同类循环847.902至18.619毫秒，实际游戏和RG数据各自记录。组件循环收益不能直接换算游戏FPS，也未证明43秒的499毫秒停顿已解决。

既有guest_pages正常和8类恢复契约通过。新增用例覆盖连续128个4KiB映射、低地址重复拒绝、删除末页后复用、释放中间连续块、再分割与重新映射、跳地址追加、中间插入、跨边界重叠拒绝、最终账本和回收。

owned_pages的正常与4类恢复路径经宿主ASan/UBSan通过，保留8线程各32轮独立区域操作、32轮map/release竞态和大VA保留测试。新增尾节点删除后追加、旧洞重填、中间分割、跨洞释放拒绝、再次追加及重叠拒绝，RG的真实4KiB组件独立执行正常契约通过。

RG guest_pages正常契约281项检查、errors0，最终pending/charged/tickets/raw_regions均0。普通宿主和恢复契约、ASan及RG执行日志都保存在offline-page26-20261115。当前没有独立Agent复跑，以上为root自测。

## 新预热种子

用户重连时掌机电量41%、充电中，共鸣无运行PID，桌面前台。先保全24的最终guest日志、23308帧CSV及完整存档。

上一轮交接写的files/dm/vk-pipeline-list-learned.bin路径是历史路径推测，实际文件不存在，该111字节cat错误输出标为无效。根据同PID启动日志，当前正确文件为应用files/vk-pipeline-list.bin。已从此实际路径重新取回1380314字节，SHA256为59c7d4aa3a0c5f69ceb49c80c2b348358049315fbb40110dba8c73d75ee6734b。

原inspector验证1326条唯一recipe，其中1222 graphics、104 compute，1284个source shader key在既有授权archive索引中均存在。完整serialized identity集合包含旧APK全部1162条，增加164条，其中10条来自24本次进程启动后的学习。新assets/pipeline-warm-list.bin直接复用这份已核有效数据，不修改原游戏资源。

## 构建与设备

26包含25的无损改写缓存、页追加提示和1326条APK补充recipe。APK为ffrs-1.0.26-preview-page-warm.apk，3695687字节，SHA256为0a010363a5718e11fb8b9af4127643356a33a2f28597c4bbf0998011d8cb753d。build26的102项宿主检查及Android构建退出0，输入摘要前后相同。

覆盖安装前确认游戏已停止，完整存档tar前后逐字相同，安装APK回读SHA一致，启动PID8416。device26中保存安装计划及日志。实际缓存覆盖、加载、技能首用/重复、声音和显示回归继续实测，暂未给出帧率改善结论。

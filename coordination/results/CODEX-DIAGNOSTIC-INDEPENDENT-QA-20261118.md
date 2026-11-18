# CODEX-DIAGNOSTIC-INDEPENDENT-QA-20261118

2026-11-18，独立复核者 `/root/library_icon_design`，实现作者 `/root/battle_performance_fix`，总控 `/root`。本次独立复核通过，覆盖实际源码语义、两组宿主组件执行，以及总控保存的荣耀实机归档和导出界面证据。原 G925 设备上的故障修复验收仍待该设备新日志。

## 输入版本与执行范围

工作树 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro`，HEAD `eb6b7ad9b73ed189ff34cf2901eee8bce805d15d`，分支 `task/T-codex-recovery-20261114`。独立比较确认 `xbg_game.cpp`、`xbg_api_breadcrumb.h`、`test_api_breadcrumb.py` 与作者提交 `459b6ad` 对应文件逐字节相同。当前构建所登记的四个文件摘要与实际源码相同，证据 `build-source-identity-check.json`。

本次写入本报告及 `.local/codex-diagnostic-qa-20261118/`。图标任务另按总控指令在其原报告补充视觉复核。复核全程读取既有设备证据，未发出设备操作。宿主命令、源码摘要及暂存补丁快照保存于 `PLAN.json`、`staged-diagnostic.patch`。

## 源码语义复核

| 检查项 | 独立检查结果 |
| --- | --- |
| Image.views 创建键 | 实际类型为 `std::array<uint32_t,9>`。索引0至8依次为base mip、mip count、base layer、layer count、VkFormat、VkImageViewType、aspects、usage、swizzle。诊断读取0至7，与创建时最终 `VkImageViewCreateInfo` 和usage扩展对应。 |
| 最终VkImageView反查 | 使用实际传入 `VkDescriptorImageInfo.imageView` 与已发布 `Image.views` handle比较，遍历原Image及filtered链。记录匹配对象的真实 `Image.create.extent`，字段名为image_extent。查不到则metadata=0。覆盖普通绑定与图像缓存快速路径。 |
| dynamic UBO | offset/range直接取最终 `write.pBufferInfo`。dynamic路径的descriptor offset为0，实际动态位移另取刚加入dynamic_list的uint32值。随后按binding/element排序形成 `out.dynamic_offsets`，保持传入vkCmdBindDescriptorSets的语义。 |
| 四个真实更新点 | 两处 `vkUpdateDescriptorSetWithTemplate`、两处 `vkUpdateDescriptorSets` 返回后调用批次收据。set-cache命中在回调前return。首次预算由实际更新成功返回后的descriptor_write领取。 |
| 五类API | runtime module围绕真实vkCreateShaderModule并记录最终words/FNV。warm pipeline、runtime pipeline分别围绕既有创建调用或resilient包装。descriptors围绕实际准备入口。dispatch_record在pipeline已取得后记录最终direct xyz或GPU indirect offset，并包围实际dispatch命令。 |
| 一次性预算 | 每API类别原子领取一次begin，每次有效begin对应一次end。descriptor批次独立原子领取，最多8项。1条session、10条API、1条dispatch、1条批次头和8条绑定合计最多21行。 |
| 时间与旋转 | 每条新记录使用CLOCK_MONOTONIC，持锁写出并fflush。configure在静止初始化阶段执行，重启先旋转为.prev，旋转失败保留原日志并禁用本轮。 |
| 默认flag0 | header缺省值0，初始化使用编译值，build脚本缺省显式传0。目标详细绑定开关短路为false，禁用组件无新日志。 |
| Vulkan调用 | 独立统计当前源码与eb6b7ad的调用清单，所有函数及次数相同。诊断Image反查只读取现有对象元数据。 |

完整检查摘要与调用清单保存于 `source-review.json`。dispatch的end记录CPU命令录制返回情况，GPU执行完成状态继续由原有提交和栅栏证据判断。

## 独立执行

两条命令在当前源码单独执行，结果分别保存在日志与result.json中。

| 命令 | 结果与覆盖 |
| --- | --- |
| `python3 tests/native/test_api_breadcrumb.py` | 退出0。clang++ C++17、ASan/UBSan、Wall/Wextra/Werror实际编译运行。覆盖禁用/错误key、16线程21行上限、单调时钟、FNV、绑定截断、直接与间接派发、旋转与失败、真实Image反查，并检查四条更新接线。 |
| `python3 -m unittest tests.device.test_fresh_descriptor_cache` | 退出0，3项通过。实际编译运行production descriptor finalization片段，覆盖真实更新回调、缓存命中、fresh输出生命周期及恢复旧逻辑的负对照。 |

复核者没有重建Android APK。读取总控已有构建记录，启用flag1，输入记录标识为 `eb6b7ad + 459b6ad staged diagnostic changes + verify_host integration`。源码身份复验与该记录吻合。

## 真实导出ZIP的独立核对

输入为 `.local/codex-dots-takeover-20261118/diagnostic-device/exported-report.zip`，476371 bytes，SHA256 `a5d41a9e42ad272b6b7be1467d7548e13fdfe83a5c2e452e81f13c972a3e402d`。ZIP含10个文件，collection显示当前api-breadcrumbs.log完整采集3246 bytes，truncated=false。ZIP内当前日志与总控单独采集的api-breadcrumbs.log逐字节相同。

metadata记录主进程PID25897，诊断界面进程PID26477。当前session头PID25897与main_pid一致。设备为HONOR ROD2-W69、SM8635、Android API36，版本 `1.0.73-mali-diag` code73，base APK SHA256 `c1fdfb06f5747ccf5693195b35cf13f6a9c3651527d6e31cf68cd80d3b52f0ef`，与安装后回读及本地候选一致。

当前目标 `073ce354a4f65e2f` 共19行，时间戳为正且按写出顺序单调。五类begin/end均完整，result均0。warm发生在frame29，其余目标调用在frame11094。最终runtime module为9023 words，FNV64为 `dcd609ecefa41335`。

绑定批次total=6、captured=6。buffer binding5的offset=0、range=1392、dynamic_offset=54016，binding35为offset=0、range=32、dynamic_offset=1478656。image binding11/17为sampled usage4，binding23/29为storage usage8，格式分别为83/97。四项image_extent均65×37×1，mip/layer均base0 count1，metadata均1。实际direct dispatch为9×5×1。

.prev属于PID21627的旧11行格式会话。ZIP内engine.log.fatal的collection mtime为1794810774681，内容为较早的shader bundle错误。当前19行结论只使用PID25897会话，旧文件分别保留来源。结构化核对结果在 `archive-audit.json`。

独立查看 `report-view.png` 和 `exported-view.png`，前者显示诊断导出入口，后者明确显示“已导出10个文件，共476371字节”。结合总控保存的SAF截图、UI XML及ZIP实物，支持本次荣耀设备导出流程已完成。

## 结论与剩余验收

本次诊断补丁复核通过，未发现需要修正的阻塞问题。可用于后续采集目标API、最终descriptor和派发参数的同会话证据。荣耀当前路径的目标收据与真实导出归档已核对，G925原故障仍需9400/9500用户在故障后提供新报告，再按PID、时间和API边界分析。

Android构建与设备操作来自总控执行记录，本复核者独立执行的是上述两组宿主测试、源码审阅及归档/图像审证。总控device.json记录原73包已恢复，保护的127项私有存档、47项外部存档摘要检查通过。

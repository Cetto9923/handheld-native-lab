# Mali 目标管线诊断收据补齐

2026-11-18，任务 `CODEX-MALI-DIAGNOSTICS-20261118`，执行者 `/root/battle_performance_fix`。

## 输入与写入范围

独立工作树 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/codex-mali-diagnostics-20261118`，分支 `task/T-mali-diagnostics-20261118`，基线 `eb6b7ad9b73ed189ff34cf2901eee8bce805d15d`。只改 `xbg_api_breadcrumb.h`、`xbg_game.cpp`、`test_api_breadcrumb.py` 和本报告。日志、补丁及检查点存入该树 `.local/mali-diagnostics-20261118/`。共享源码、设备、信号链和协调state未修改。

原G925末次故障的warm/runtime并发假说已有反证，本候选补充后续同会话归因所缺收据。编译互斥锁未在本候选中实现。

## 具体改动

沿用原 `FFRS_API_BREADCRUMB_DIAGNOSTIC`，默认值0，目标仍为 `073ce354a4f65e2f`。保留原5类begin/end、PID/TID、frame和runtime最终module words/FNV64逻辑。每条session、begin/end、descriptor和dispatch行新增CLOCK_MONOTONIC纳秒字段，便于与已有故障或shader时序关联。

首次真实dispatch发出前，仍在原begin/end区间内写一条payload。普通及CPU fallback路线写最终xyz，GPU indirect只写 `gpu_indirect=1` 及传给vkCmdDispatchIndirect的buffer相对offset。新增诊断不读取GPU参数、不刷新或等待GPU，不导出地址或资源内容。若pipeline创建失败而没有真实dispatch，不抢占dispatch预算。

目标首次实际descriptor更新返回后发布一次批次。两处template更新和两处普通更新都接入，set-cache命中不消耗首次write预算。最多8个binding/array element，额外写total/captured显示截断。本目标canonical有6项，固定预算仍适用于其他合法形状。日志记录最终VkWriteDescriptorSet中的binding、element和实际descriptor type。

- buffer字段取最终VkDescriptorBufferInfo的offset/range。dynamic UBO另记实际dynamic_list offset，保持descriptor offset与dynamic offset各自含义。
- image字段通过最终VkImageView反查Image.views发布的创建键，记录format、实际Image.create.extent、mip/layer范围、view type、aspects和usage，支持filtered RG16 proxy。查找不到时只标metadata=0，不补推测值。
- 其他descriptor种类保留真实binding/type，并标kind=Other。本目标没有texel-buffer binding，未为此增加额外资源查询。

总行数上限调整为21，包含1条session、10条API begin/end、1条dispatch payload、1条descriptor批次头和最多8条descriptor。所有新增写入沿用原有mutex、首次原子领取和fflush，禁用时不创建日志，错误target不会写新记录。重启旋转与失败时保全前次收据的语义继续保留。诊断开启时目标路径有有限文件I/O开销。

## 实际验证

命令为 `python3 tests/native/test_api_breadcrumb.py`。组件以clang++ C++17、ASan/UBSan、Wall/Wextra/Werror编译并执行，退出0。

| 覆盖 | 结果 |
| --- | --- |
| 禁用、空target、错误target | 无新增记录，禁用无新文件 |
| 16线程竞争5类API与descriptor批次 | 一次性预算成立，总数21行 |
| monotonic与原FNV | 每行时间为正且按写出顺序不降，原little-endian words FNV精确匹配 |
| descriptor预算 | total12/captured8，无越界和额外条目 |
| buffer和image字段 | range/dynamic offset及format/extent/mip/layer逐项断言 |
| 生产image lookup lambda提取执行 | canonical、filtered proxy、未知view与禁用分支通过 |
| dispatch | direct输出xyz，GPU indirect输出offset且没有xyz |
| 不完整begin立即可读及重启 | fflush与.prev原字节保留通过 |
| rotation失败、文件打开失败 | 保留原日志，后续begin为0 |
| 源码接线 | 4个vkUpdate返回后才发布，两个实际image分支、实际buffer字段及dispatch参数检查通过 |
| Vulkan调用清单 | 与eb6b7ad完全一致 |
| API28整CPP syntax | 原engine target flags，诊断flag0与flag1均退出0 |
| git diff --check | 退出0 |

首次运行在Vulkan计数断言处发现既有测试仍预期 `vkCreatePipelineCache=1`。独立比较eb6b7ad与候选，二者调用清单完全相同，均为2，来自已集成的缓存重试。同步该预期为2后全组通过。未修改生产Vulkan调用来适配测试。

NDK语法命令复用已有 `ffrs_nvn_vulkan_engine.dir/flags.make` 中的真实引擎宏、include与flags，源路径替换到本独立树。完整命令、日志与退出码在syntax-plans.json及android-syntax-flag0/1.log。没有执行全量Android构建。

## 交接与验收边界

代码补丁为本树 `.local/mali-diagnostics-20261118/mali-diagnostics.patch`，摘要及源码摘要在checkpoint.json。本报告与三个源码文件在独立分支形成一个自有提交，供总控串行审查集成。

本次覆盖组件执行、生产查找逻辑执行、source接线契约和Android语法。完整APK、日志导出UI、实际driver调用、G925原机及各设备游戏流程均NOT_RUN。需要总控以原flag1构建内部包，再核对原机同PID收据和导出归档。原事故API、最终动态资源关系及崩溃原因继续等待新同会话证据。

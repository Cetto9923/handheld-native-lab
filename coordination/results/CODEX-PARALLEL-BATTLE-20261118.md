# 第四章战斗性能并行修复检查点

## 当前结果

已实现 indexed_graphics_ready 的注册纹理节点缓存复用候选。源码及宿主检查通过，Android API28 语法检查通过。完整 APK 链接、实际战斗性能对照和独立 QA 仍待执行。

## 输入与隔离

- 任务为 CODEX-PARALLEL-BATTLE-20261118，负责人 /root/battle_performance_fix。
- worktree 为 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/codex-parallel-battle-20261118`，分支 `task/codex-parallel-battle-20261118`，HEAD `273c240155f95db92e2f08050b61164aec401f29`。
- 基线为 root 冻结的生命周期候选源码。已有生命周期改动保留在本独立树，交回补丁只包含本任务修改的两个文件。
- 读取了根 AGENTS.md、AGENTS-HONOR.md、coordination/EXPERIENCE.md、DOT-ARSENAL-20261118.md 和已有独立分析。本任务没有设备输入、安装、推送或存档写入。

## 热点依据与改动

exact342a 的 PID2771/TID3239、BuildID26b6ff221df734793357d856ff5a6ac6bd224897 探索走动采样共有2326个有效 RHI 样本，其中23个落在 indexed_graphics_ready 的纹理注册表查找。该采样覆盖探索走动，实际战斗的 CPU 热点仍需新采样确认。

原 indexed 资格判断对每个纹理绑定执行 `table.find(id)`。同一引擎已有 `find_texture_record`，descriptor 和 integer 路径通过它复用注册节点。注册更新在原节点进行，std::map 插入保留节点地址，shutdown 清理注册表时清空 front cache。本候选将 indexed 入口接到该现有方法，继续使用原 pool 选择和 id 边界检查，继续执行全部 storage、pending report、writeback、alignment 和 image-memory 判定。

资格判断前的注释改为明确的资源约束。注册节点查找会更新指针 front cache 与计数器，GPU 资源、slot 和所有权均沿原路径处理。

已核对 query-end、bbox、readback retention、重复 VS/range 等既有结果。query-end 的旧 REVIEW 状态落后于最终双设备报告，确认该候选已做过对照且未证明收益后，本任务停止重复该方向。

## 实际验证

| 命令 | 结果 | 覆盖 |
| --- | --- | --- |
| `python3 tests/native/test_indexed_bbox_ready.py` | 退出0 | 生产完整函数20000状态、200000范围及全部失败字段对照，ASan/UBSan |
| `python3 -m unittest tests.device.test_render_policy` | 退出0，8项 | 既有注册节点缓存与渲染策略契约 |
| NDK26.1 API28 clang++，沿实际 CMake 编译参数 `-fsyntax-only xbg_game.cpp` | 退出0 | 当前候选 Android C++ 编译前端 |
| `git diff --check` | 退出0 | 修改格式 |

新增入口用例覆盖缺失节点后注册、原位更新、实际 pool 延迟创建、既有缓存关闭、shutdown 式缓存清理，并与冻结直接查表函数比较结果。既有缓存关闭时结果保持，命中计数也按现有方式处理。

实际命令和日志保存在 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/codex-parallel-battle-20261118/.local/battle-record-cache-20261118`。`android-syntax-plan.json` 保存完整 NDK 命令，`checkpoint.json` 保存源码摘要与 dirty 状态。当前候选未构建 APK，未取得真实 FPS 收益。

## 交回与恢复

补丁为 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/codex-parallel-battle-20261118/.local/battle-record-cache-20261118/indexed-record-cache.patch`，SHA256 `42c2975fb1d774485058b13c5eaea6483e23b505d0ed7b08ec383d80c936d6ba`。

root 可先执行 `git apply --check <上述补丁>`，再于自己的集成树应用。补丁只修改 `src/graphics/nvn_vulkan/engine/xbg_game.cpp` 与 `tests/native/test_indexed_bbox_ready.py`。本任务原基线副本在同一输出目录的 `baseline/`，恢复时只按补丁反向移除本任务变更，保全生命周期候选。

下一步由 root 统一设备窗口采集真实战斗 RHI 栈，并对候选进行完整 APK 构建、匹配敌人和场景的对照。天玑9400闪退、战斗低帧、长期表现与音频验收继续各自保持未完成。


## 第二轮实际攻击窗口归因与增量边界候选

本段使用 root 新采集的同一遭遇攻击过程窗口，PID9958 / RHI10265，5秒 `rhi-active.data`，BuildID `497ce1b6b962e156c7c803f70add57f90c4447a8` 与归档 DWARF SO 匹配。root 提供的采样前后截图均在战斗，连续12次 A 输入后敌方 HP 下降。命令选择窗口的 `rhi.data` 单独保留，本段数量没有合并两个窗口。

active 窗口共有2326个目标线程样本，丢失0。原始叶符号 pending_view 为116个，indexed_graphics_ready 与其 lambda 合计169个；readback_buffer 调用链中的 memset 为103个。这些是 on-CPU 采样归属，不能换算为可全部消除的帧时间。实际解析输出在 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/codex-parallel-battle-20261118/.local/battle-active-20261118/all-callers.json`，原证据在根 `.local/codex-dots-takeover-20261118/combat/`。

### 具体实现与维护边界

候选复用现有 `XbgIndexedBounds` 和实验开关 `bboxready`。默认值及初始化均为 false。每个 writeback/report 发布成功后，以常数次比较扩展对应保守包围界。indexed_graphics_ready 读取已维护边界。pending_view 继续先执行原 recording_slot 条件与副作用，再决定是否拒绝完全不相交查询。

- 4个 writeback 发布点与1个 report 发布点经生产 helper 维护。vector push 成功后更新边界，原聚合初始化与版本顺序继续保留。
- cpu_access 的 erase 只删元素，既有边界成为安全过近似。它可少省扫描，仍包含所有现存范围。
- slot 退休后的 subset swap 与报告前缀 erase 后，各自一次重建对应边界。
- resolve_pending 移出 writeback 后立即执行 clear helper，report 清空与 shutdown 同样重置对应边界。
- 无效 producer 使该侧边界 unusable，继续执行原扫描。无效 query 也继续沿原函数处理。
- report 优先、writeback 逆序、最新部分覆盖拒绝、对齐、owner pin、跨 slot viewed 登记及所有 GPU barrier/同步继续由原函数完成。

元数据维护在开关关闭时也执行，额外 CPU 成本尚待真实对照。当前交回是默认关闭的实验候选，真实收益与总体开销均未测，不能按旧 bbox 候选的 APK/开关记录推断本候选已通过。

### 已执行的证明与回归

| 检查 | 实际结果 |
| --- | --- |
| 生产 append/clear helper、pending_view 与 indexed_range_ready 交错400000查询 | ASan/UBSan退出0 |
| 完整 indexed_graphics_ready 20000状态与200000范围 | ASan/UBSan退出0 |
| 缺失 writeback 发布维护负对照 | 触发断言，退出-6 |
| 缺失 report 发布维护负对照 | 触发断言，退出-6 |
| 把拒绝放到 recording_slot 之前的负对照 | 触发断言，退出-6 |
| 原 indexed ready probe 和 bbox probe 契约 | 均退出0 |
| 原 uniform arena / indirect texel residency 契约 | 退出0 |
| 当前 xbg_game.cpp Android API28 语法检查 | 退出0 |
| 根工作区应用检查与 git diff --check | 均退出0 |

新增测试比较返回值、Buffer字段、版本、recording_slot次数、pending_views、跨slot状态、保留owner及viewed范围。原 pending_view 去掉新增 guard 后与冻结基线 SHA 一致。原 indexed_range_ready 函数 SHA 完整一致。还覆盖最新部分写、溢出/零长/UINT64_MAX边界、动态开关、retirement、move/clear 和 recording_slot 内新发布范围的时序。生产扫描计数用例确认不相交 query 省掉了实际 producer-loop step。

首次 pending_view fixture 没有在参考与候选两次调用之间恢复 producer 版本计数，recording_slot 注入发布会消费不同的测试版本，因此失败。修正夹具恢复版本计数后通过，失败日志保存在 pending-test.log，后续通过及负对照日志各自保留。

### 第二补丁与下一步

独立补丁为 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.worktrees/codex-parallel-battle-20261118/.local/battle-pending-bounds-20261118/incremental-pending-bounds.patch`，SHA256 `1c9a39440cab9e71fd73f7964b1f3727abec3803d1d90afe05d6b0edbe6014be`。它基于已交回的第一轮查表复用源码，修改 xbg_game.cpp、既有 indexed bbox 完整函数测试，并增加 test_pending_bounds.py。根已完成 `git apply --check`。

下一步由 root 统一构建资源与设备窗口。建议先审查所有维护点，再构建隔离 APK，显式启用 bboxready，在同一机械敌人遭遇内分别记录命令选择和持续攻击窗口的普通真实呈现帧。开关关闭的总体成本也需与原候选比较。完整 APK、实际收益、画面/声音/长期回归与独立 QA 仍待执行。

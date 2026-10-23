# T07-GUEST-CONTEXT-REVIEW 普通 owned-stack/quota 组件独立审查

冻结提交：`e846dc174edcb687c4e075288326c62ab10ac86e`；总控另报集成提交 `d9a1667`。结论：**普通主机组件契约 PASS，未发现需修复的真实功能缺陷**。只读审查四个冻结文件；本代理仅写 `functional-review/guest-context/`。

## 源码与预审要点

- `guest_context.c:34-49` 要求正的明确 quota/stack size，检查 host page 对齐、PTHREAD_STACK_MIN 与 guard 跨度；未读取 RAM/RSS 或静默选择/舍入预算。
- `51-99` 用同一 Context mutex 检查 charged+pending+requested，预扣 pending 后才容许资源创建；commit 只搬移账项，cancel/release 分别验证票据状态。ticket 由不透明句柄保存固定 owner，调用方负责串行使用每张 live ticket；没有跨 Context 的任意退款参数。
- `150-200` 在任何 mmap 前为精确 stack payload 预扣；先准备 invocation 元数据；整段 PROT_NONE 映射仅将 payload 设为 RW，pthread 明确使用该 stack，并关闭额外 implicit guard。descriptor 在 pthread_create 前发布，没有 callback 快速返回后被覆盖的完成状态。
- `133-148,201-215` 正常路径先完成 pthread_join，再真实 munmap，最后退票/清状态。清理不确定时保留映射和票据而非提前退款；join 失败时保留 ownership、charge 和 join_pending，拒绝复用/销毁。此失败路径仅做源码审查，没有注入失败 join/munmap。
- `217-241` 外部 live ticket 或未完成 native thread 阻止 destroy；可重试的 retained-mapping 清理只有 munmap 成功后才退款。所有用户/API 在最终 destroy 前必须静止。
- `memory_info_valid_fields` 初始化后始终为 **0**。native quota/stack 不被冒充成 SDK 七字段、ThreadType、heap、Query 或 TP 适配；配额测试值都是明确 synthetic fixture。

## 本代理独立执行

Apple Clang 21.0.0 / macOS arm64 / host page **16 KiB**；编译使用 `-std=c11 -O2 -g -Wall -Wextra -Werror -pedantic -pthread`。

| 契约 | 编译真实码 | 运行真实码 | 结果 |
|---|---:|---:|---|
| 原 production contract | 0 | 0 | 64 KiB 实际 SP/局部变量在 owned stack，RW payload/双 PROT_NONE guard，host TLS、join 后退款，8线程2 accepted/6 LIMIT，七字段 invalid |
| 原 recovery contract | 0 | 0 | 另覆盖一个受控 map 失败，mmap 前 pending 已预扣，失败后账本回滚并可再运行 |
| 独立补测（校正后） | 0 | 0 | 8线程全部停留 pending 阶段仍仅2成功；callback 返回后 pthread-key destructor 阻塞期间 stack 费用/active 保留，额外借1B为 LIMIT、再次 run 为 BUSY；native join 后才退清 |

`llvm-nm --defined-only` **exit 0**，production binary 无 `guest_context_test_*` 定义。测试后指定四个文件与冻结 SHA 的 `git diff --exit-code` **0**，编译快照摘要保持一致。未运行完整 build script，也未 cross-build 或执行 Android。

## 补测假设纠正记录

本代理最初额外要求 C `_Thread_local` marker 必须延续到 pthread-key destructor。该混合析构顺序不在组件公开契约内；本机诊断实际为 callback errors=0、destructor marker=0，而 destructor 阻塞前后 committed usage 都正确。

未修改作者实现或原契约。已保留最初补测源码、失败日志与诊断版本：未改补测确认运行 **exit 1**，加诊断后确认运行 **exit 1**；随后仅移除这项未承诺的 teardown-order 要求，保留原 main/callback TLS 检查和全部 join/charge 断言，校正补测 **exit 0**。首次驱动在保存 child code 前错误地假定输出是 JSON，驱动退出 1；该次 child code 未持久化，不作精确值主张。所有尝试均记录于 `manifest.json`。

## 总控提供的 RG477M 证据，单独归因

总控报告冻结版 production 小 contract 在真实 RG477M **exit 0 / no timeout**：64 KiB 实际 SP/双 guard/TLS，join 后 pending=charged=0，8线程2 accepted/6 LIMIT，七字段 valid mask=0，errors=0。证据入口：`ffrs-integration/.local/device/rg477m-session1/guest-context-execution.json`。这是总控执行，本代理没有使用 ADB 或独立复跑；设备 production 无测试 hook，`map_failure_rollback_cases=0`，不能声称设备失败注入通过。

## 限制

此组件只管理独立 native callback、stack 及逻辑票据。未与 loader、pages、heap 或 SDK 生命周期合并，不证明真实完整内存预算、QueryMemoryInfo、游戏/T02/完整移植通过。callback 的返回值不得指向即将释放的 stack；每个 live ticket 的操作须串行；取消运行调用和并发最终销毁不受支持。failed join 的保守 quarantine 保留资源，不能当成可强制清理的成功路径。

完整命令、cwd、输入/产物 SHA-256、真实退出码与日志见 `manifest.json`；预审设计快照与要点亦保留。当前授权范围无待修问题及剩余阻塞。

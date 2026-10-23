# 当前交接证据索引

这是安全收口时的当前索引，替代a277及更早索引顶部的历史“startup69/106仍阻塞”描述；旧命令/失败保留在Git与原始目录。单入口：[HANDOFF](../../coordination/HANDOFF.md)。所有`.local/`均相对`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`，其它作者证据在相邻工作树，不假定原交付复制了大日志。

## 最新可核验链

| 范围 | 精确现存证据 | 真实结果 |
|---|---|---|
| 当前代码 | integration与原交付a277；源码e305d1e；[工作树快照](../../coordination/WORKTREES.json) | 已集成434、cfc、80d修复、f60、6a与851诊断；仅本次交接文档待最终自引用Git核对 |
| 106/110/startup | `.local/device/rg477m-session1/primary-identity-v1-{plan,stage,execution}.json`；[device report](codex_guest_process_device_report.md) | 10项：组件/两原startup0，CRT515/133；两startup真实OS析构2轮/1调用/remaining0；无timeout/main未改 |
| HOST消费者 | `.local/device/rg477m-session1/tls-host-consumer-v1-{plan,stage,execution}.json` | 14项11×0/3预期134，无timeout/main未改；SCS/HOST/currentx18与ledger真实覆盖 |
| 最新原allocator+CRT | `.local/runtime/original-allocator-v2/manifest.json`；`.local/device/rg477m-session1/original-allocator-v2-{plan,stage,execution}.json` | allocator空/带参0/0、155.918208/170.973375ms；CRT515/+5920a34、raw133/162.899666ms，wrapper0不掩盖原码 |
| 最新Android CLI | `.local/runtime/original-allocator-v2/nso_owned_startup.android` | 839248B，SHA256`b9085e0669c36a7c968d80a721d0eb7d1dc521d268bdb9d0e8f4291a3090b6f7`；精确增量link argv与输入hash在manifest，未重跑全113构建 |
| 独立审查 | [primary](codex_primary_identity_review.md)、[HOST消费者](codex_tls_host_consumer_review.md)、[HOST边界](codex_native_host_callback_review.md)、[provider](codex_tls_provider_review.md)、[P2修复](codex_tls_provider_repair_review.md) | 各FINAL；reviewer Android NOT_RUN，root设备另归因；原fresh P2 -11保留，80d同fixture修复0；Darwin mincore测试误判校准保留 |
| CRT下一关 | [frontier](codex_crt_frontier_review.md)、[caller](codex_tls_caller_report.md)、[boot](codex_boot_order_reuse_report.md)、[thread exit](codex_tls_exit_hook_report.md) | 21字保真/104B运输已过；515真实DSO schema/逐线程ELFTLS/TP/五参Start尚未闭合 |
| 图形复用 | [LAS FINAL](codex_las_transfer_review.md)、`las-transfer-integrated-v2-*`设备日志 | MIT LAS typed记录→真实vkCmdCopyBuffer，RG0/113.854ms、64KiB+256B零差异；无shader/present/game |
| 设备/APK | [device](codex_device_report.md)、[Android](codex_android_report.md)、[阶段](codex_stage_status.md) | v6仅诊断APK451092B/SHA4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a；无完整游戏APK |
| 真实进程收口 | `.local/handoff-final/process-check.json` | ps只读0，命名本项目构建/测试匹配0；普通沙箱ps EPERM后获准只读核对成功；不外推外部代理/设备全局进程 |
| 原分支保存 | `.local/integration-approval/original-ff-a27742c-result.json`及最终新回执（如有） | a277实际safeff成功clean/no push；不重放历史已完成操作，最终文档提交实读Git |

## 原始材料与工具

- main125577976B SHA256`6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`；SDK SHA256`9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9`。路径`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/uncompressed/{main.bin,sdk.bin}`；仅引用，不复制/提交素材。
- 当前设备项目根`/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259`，main已匹配hash；ADB序列`RG477M01445122`。精确push/运行输入在上述stage/plan；先核是否已完成，不盲重放。
- 完整585基线构建：`.local/runtime/integrated-585e3e1/manifest.json`；增量f60 `../ffrs-primary-identity/.local/primary-identity-v1/freeze.json`；6a `../ffrs-tls-host-consumer/.local/tls-host-consumer-v1/{freeze.json,manifest.json,device-argv.json}`。工具链`../ffrs-android/.local/minimal-ndk/toolchain`，CRC/member hash已核而整个NDK归档未核。
- 精确复现优先读取manifest的argv；host `scripts/runtime/run_capture.py`保留真实退出码和timeout。原设备CLI诊断预算1GiB、logicalstack65536、resourcequota1MiB是明确端口策略，不是伪装Horizon原内存值。
- [REA/方法](codex_method_reuse_audit.md)、[成熟路线](codex_reuse_route_review.md)、[LAS范围](codex_las_implementation_review.md)、[malloc前置条件](codex_malloc_startup_prerequisite.md)、[module_tls范围](codex_module_tls_review.md)。不重复helper同故障/GP21运输/已通过探针；未证明的SDK字段不猜。
- 历史失败必须保留：地址0误判、tail掩盖139、候选重定位完整性误判、pre-init malloc139、原fresh登记-11、mincore harness -6、编译超时及旧QA平台中止。当前startup和CRT不同结果不能拼接为完整启动。

# T07-TLS-PROVIDER-REVIEW — cfc4df89

最终状态：**P2 已由 `80d4cfbf` 限定修复并独立复核闭合，见 [修复复核](repair/REVIEW.md)**。原冻结 `cfc4df8921eaf911662ee6512c770a0683e787db`（base `b19706c4cfc9fdd3246d85a12c880f922ae346d3`）仍保留一个独立实证 P2；原 cfc 报告与 -11 证据未改，其余限定审核未发现第二项需修问题。本人 Android 编译/执行均 NOT_RUN；总控设备结果另归因。所有写入仅位于本审查目录，未改源码或 Git。

## P2：未加载模块的有效 Process 注册会 SIGSEGV

`src/runtime/hle/guest_process.c:494` 在 `tls_primary_locked(p)` 状态检查之前，无条件调用 `nso_base(p->module)`。有效 Process 在 create 成功、尚未 load 时 module 为 NULL；`src/runtime/nso/nso_loader.c:386` 的 nso_base 直接读取 `m->image`。

独立最小复现 `fresh_registration_contract.c` 创建普通有效 Process，以真实自有宿主 alloc/free 函数地址调用注册 API。这里应拒绝非 primary/非模块回调并返回 `NSO_INVALID`；实际原始退出 **-11（SIGSEGV / shell139）**，无超时。没有载入任何 NSO 输入或执行 guest code。`logs/fresh-process-registration/` 保存完整命令和原始 stdout/stderr；runner 外层退出 1，没有把故障改成预期成功。

最小建议：先验证实际 primary/module_ready/模块非空，再取 module base 并验证 text 回调；拒绝路径保持 pair 不变。后续修复冻结 `80d4cfbf` 已用同一复现源码和 `NSO_INVALID` 预期独立复核为0；详见修复复核报告。原 cfc 失败保留于 `cfc-original-REVIEW.md`、`cfc-original-manifest.json` 和原日志。

## 冻结与限定审核

从 Git 独立快照 25 文件，其中 5 增量与 freeze 一致，20 依赖与已审 b197 逐字节相同；复用九个核过 SHA 的已审自有宿主对象，未重跑完整 owner QA。五个增量 SHA：

| 文件 | SHA-256 |
| --- | --- |
| guest_process.c | 742c166f8d954d416a673be899106fc8211aa5fe5eeedfca1f6a4fdd0254c762 |
| guest_process.h | b674bf7af5b31ca78390d0eda5b5d102592195e7e0c25627282d92c252f3d460 |
| guest_process_contract.c | 5dbe9e540ad3fbf0408b82bdbbec8dff890bde388b4ab97b8da574b148a5b58e |
| build_guest_process_contract.sh | ec9fbb64e614d294a020b9cdf5f2595ac7c42793daaa7b166101643236f33033 |
| codex_guest_process_report.md | eb00de6e49bcace3d4f5a65945b82aaa6d76a49d291b98cc0b6515e9d36f67c7 |

`guest_process.c:489–591` 的 pair 在同一锁下同时登记，要求 once/non-null/module text，不立即调用；已登记信息保留到真实模块释放。消费者限定 actual primary 和 prepared IDLE boundary，HOST/忙态拒绝保持 out 不变。Allocate 传 `(bytes,16)`，free 传 `(ptr,bytes)`；null free 不调用 guest，非空只接受本 owner 的精确 pointer/size。

metadata 在任何 guest 副作用之前分配，回调期间释放 Process 锁，因此真实 Query 重入可取锁。NULL allocation 返回 LIMIT 且不写 out；失败或无法确认结果保存 uncertain 记录并 quarantine，不自动重试回调。live/uncertain 记录在 destroy 的 pages/module/observer 变更之前阻止释放；释放正常记录只 free host metadata，没有把 opaque guest pointer 交给 host free。记录是逻辑 suballocation 身份，不重复作为物理 backing 计费。uncertain 回调分支仅完成源码审查，未执行该分支。

typed import 的 set 为 GP2→0、initialized 为 GP0→1；initialized 只表示 pair 已登记，不代表 ELF TLS 初始化。自有 ASM fixture 的实际模块地址/独立 buffers、Query 重入与 GP 参数对应。独立检查 normal ELF 和 SCS 测试 ELF：生产没有 `guest_process_test_tls_allocator`，测试产物包含该 hook；SCS 观察源码与编译元数据一致。独立反汇编已跟进 compiler outlined helper，确认 x1 release 参数经 x19 保存，没有把局部摘录误判为参数丢失。

仅消费作者已有静态事实索引：once/non-null、固定 alignment16、free-null 行为和 vtable 路由；未读取原 SDK/main。静态事实 SHA `76434bec4976fef0a48c94b1b5f6c6a95c0b445f1579d21fcb194c28a2223890`，不冒称本人重新验证原指令或成功 REA Evidence。

## 本人实际宿主执行

Clang21、`-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`，新 Process 与合同独立严格编译。27 条快照 Git 命令均 0；19 条编译/测试/离线检查为 **16×0、2×-6、1×-11**，无超时。

- 正常 once/module-owned 注册合同 0；null/repeat wrapper 的真实 failstop 各 -6（shell134）。
- provider 宿主合同 0，明确 UNSUPPORTED/native_calls=0，没有 raw fallback；普通组件合同 0。
- 新 `registration_lifecycle_contract.c` 编译/执行 0/0：真实 primary 正常注册、foreign thread 拒绝、prepared 前 out 保持、null free 不调用、Query 一线程/64KiB、登记 pair 保留至真实 destroy、primary 返回后消费者拒绝。
- 新 fresh 注册复现编译 0、执行 -11，作为上述 P2 保留。
- diff、编译器身份、ASM/ELF/outlined helper、输入及总控证据检查均 0。

一次辅助证据索引脚本错误地把 freeze 的 ABI **stdout** 摘要当作 run.json 摘要，断言退出1；确认三个实际 stdout 及其原始 run 元数据后均吻合。这是审查脚本假设错误，未触发编译或改变任何输入，记录于 manifest 的 auxiliary_inspection_failures。

## 总控 RG477M 证据（本人未执行）

核对 `.local/device/rg477m-session1/tls-provider-v2-{plan,stage,execution}.json`：freeze、manifest、17 个范围内源记录及依赖一致，六个 Android ELF 本地摘要吻合；实际 stage 的三个 ELF 与两个正向 fixture 摘要吻合。复制七个普通 case 的 stdout/stderr 并逐一核对 execution 摘要。info 与原 startup 仅取执行元数据；`test_loader.py` 只见清单摘要，内容未读、未执行。

| 总控实际 case | 原始退出 |
| --- | --- |
| normal / registration | 0 / 0 |
| registration-null / registration-repeat | 134 / 134，真实预期 failstop |
| provider / provider-scs-recovery | 0 / 0，各 6 次真实 provider 调用，Query 重入/x18；recovery 保留 metadata-before-side-effect/HOST 拒绝断言 |
| live-retained | 0，两个 allocation/code/pair 保留；cleanup_complete=false，不能当释放完成 |
| info | 0 |
| original-startup-empty / arguments | 133 / 133，到 GetCurrentThread106/+0x18d68，不能当 startup 通过 |

十 case 均无超时；main 前后摘要记录一致，本人未读取 main。普通 normal ELF 1039240 B/SHA `076a67e885bf59484239aef25ace62c9ac6199b04b0a1f85ea914ef753bce86a`；SCS recovery ELF 1050704 B/SHA `67314ac93e4e501a55a51022d7d01409c85aea04b6cb2cd3b4f76320a97590f7`；自有 allocator fixture 9216 B/SHA `e343000711ac3167dd6e6fdecdb37a9133727dbd8d991866b1e8ca8913780f05`。

作者 112 命令仅为来源证据，原始分布 98×0、7×1、5×-6、2×2，无超时，不算本人执行。HOST-phase 真正 ELF/TCB TLS 消费者仍显式 unsupported，ELF_TLS_initialized=false；完整 ELF TLS/CRT/游戏、旧停止流程、原 game/SDK 执行均未纳入本次审核。

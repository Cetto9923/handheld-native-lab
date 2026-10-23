# T07-STARTUP-PROFILE-REVIEW

冻结实现：`15e498c0985aee20c973632b6406e2b976628d81`。独立 host 普通组件主路径与受控清理恢复通过；发现一个 host 编译配置下的 strict resolver 缺口。总控同一冻结 Android normal contract 已失败，后续分项诊断已锁定 Bionic 实际可用栈不足 64KiB；修正尚待新冻结，不能标 Android/RG PASS。

## 具体发现：guard feature 关闭时 profile 可进入 extension（P2，host 配置边界）

`src/runtime/hle/guest_process.c:225` 无条件列出三个 `__cxa_guard_*` 名称，`:234` 调用通用 `nso_host_resolve`，并注释称每个名称都不会落入 extension。但 `src/runtime/hle/nso_host.c:26` 的真实 typed guard 分支仅在 Android 或 `NSO_WITH_CXX_GUARDS=1` 时编译；普通 host 编译关闭该宏时，这三个名称会走 `nso_host.c:54` 的 extension fallthrough。

独立合法 C probe `resolver_feature_contract.c` 只安装正确类型的普通 host guard provider 并解析名称，不调用 guest/NSO 代码：

- 原冻结指定构建配置（宏开启）对照：退出 **0**，`extension_calls=0`。
- 仅将同一冻结 `nso_host.c` 按宏关闭的普通 host 配置编译：构建退出 **0**，probe 退出 **1**，`extension_calls=1`、`profile_selected_extension_guard=true`。

Android 与作者当前 checkpoint 脚本均启用真实 typed guard，因此此发现不解释 RG 失败，也不宣称当前指定产物已经走过 extension。公开 profile 尚无拒绝该 host 配置的构建约束，故“never extension”不能对所有可编译配置成立。最小修正是明确拒绝无 typed guard 的 profile 配置，或从不带 extension 的 typed provider 获取这些地址；无需扩大导入范围。精确命令、两组对象及 stdout/真实退出码见 manifest 的 `resolver-feature-*` 项。

## 主路径审查与已独立执行证据

输入为上述 commit 的八个 scope 文件、同一 commit 的十个依赖 c/h 文件，以及作者正常正向 fixture 的独立副本。fixture 9216B、SHA256 `a8467dc70098b693ab2d0266ac9ec99c705b76a644da20fbc0e5b263cd237493`；未读取/执行旧异常 NSO QA 或 `test_loader.py`，未执行完整构建脚本（其中含可选原 main/Android 步骤）。全部源码及 fixture 测后摘要一致。冻结 Context/address_region 与 `e846dc1` 的四文件 `git diff --exit-code` 实际为 **0**。

严格编译为 AppleClang C11、`-O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`，链接真实 host libc++ runtime；**16 个编译/链接命令均 0**。实际总计 29 个独立命令：26 个 0、一个 -6、两个 1，完整 argv/cwd/log/实际码在 `manifest.json`，分布见 `summary.json`。非零结果如下分别保留，未称全部为 0。

| 普通功能执行 | 实际退出码 | 范围 |
|---|---:|---|
| frozen normal contract | 0 | 两次 native callback；七字段、padding、真实 SP/局部变量、VA、ThreadRecord 和信号恢复 |
| frozen recovery contract | 0 | 五阶段正常 syscall/限额失败；precharge、retained、retry、observer 一次 |
| invalid void Query | -6（shell 134） | `[query-invalid]` 明确 abort，非静默成功 |
| legacy CLI info | 0 | 正常 fixture 的旧入口信息路径 |
| profile CLI info | 0 | 显式 quota/stack 的正常 fixture 信息路径 |
| profile 构建不带 policy 选项 | 0 | 仍走旧默认 info 路径 |
| profile module quota=1 | 1 | 明确 NSO_LIMIT，拒绝 module mmap |
| 独立补充 contract | 0 | 下述正常边界与恢复检查 |
| guard macro 关闭的 resolver probe | 1 | 上述 P2 的真实可复现失败 |
| guard macro 开启的对照 | 0 | 当前指定配置不进入 extension |

独立补测 `supplement_contract.c` 验证：模块可装载但 quota 比 module+64KiB 少 1 时，主栈 reserve 返回 LIMIT、callback 未进入且 owner 可完整销毁；恰好相等时七字段有效且 `used==quota`。owner commit/cancel/release 回调的受控失败保留原 pending/charged ticket，真实 image munmap 后才取消/退款；重试不重复 munmap，observer 在 image 仍存活时只调用一次。另在已有自定义 altstack 和四个 signal handler/mask 的普通 host 线程验证 begin/end、重复 begin EBUSY、原 altstack/动作/mask 恢复。独立 fork 的无活动 profile Query 子进程被 SIGABRT 终止，父进程检查共享 output 的全部字节仍为 0xa5（含 padding）。生产 host normal binary 的 `nm -g` 未包含 `nso_loader_test_vm`。

## 冻结实现已闭合的预审事项

- **无效 Query：** `guest_process.c:124–159` 校验成功后才 memcpy `0x34`；`:165–167` 对 void ABI 失效显式诊断并 abort。两个预审要求中的失效返回歧义已闭合。
- **一致计量：** `guest_process.c:21` 私有 Context，`:124–148` 在 process 锁下核对 exact module ticket、Context 两个 ticket、active stack 描述、pthread 身份、VA backing/数量/总量及 quarantine。生命周期及 raw API quiescent 契约下，不存在由外来 ticket 偷换的公开合法路径。它依赖单 guest/外部 raw API 静止的嵌入契约，不是通用跨 client 所有权机制。
- **empty heap、字段与 ThreadRecord：** `:117–122` 真实一次状态转换为 READY_EMPTY，resolver 不提供 mutator；六个 u64 + `0x30` u32，`0x34..37` 不写，baseline 零值重算。`:244–267` 登记实际 pthread/独占 stack，run 等 Context join 完成后才清除 record；未把 record 当 SDK ThreadType 布局。
- **loader：** `nso_loader.c:125` 在 source mmap 前建立 descriptor，`:151–165` 同次验证 image extent，在 image mmap 前 reserve、成功后 commit。`:328–356` 各成功 munmap 才清 pointer，image 消失才退 ticket；source/sentinel 或 owner cleanup 失败保留可重试 handle，ready/closing 阻止部分释放后执行。observer 独立且先置空后一次调用，不作计量。legacy load/free 在无法交回 retained handle 时明确 fatal。
- **fault scope：** `nso_loader.c:441–483` 在执行线程保存/恢复 signal 动作与 altstack；`guest_process.c:253–268` 包围实际 callback，恢复失败返回非成功并保留 profile。诊断 handler 使用 stdio 是已明确的隔离实验限制，未当作可恢复、async-signal-safe 的一般异常设施。
- **输入与 policy：** full mapped image extent（含 holes/padding）及 owned stack 是逻辑 cap；host admin/guard VA 不冒称原机物理数据。RO MAP_PRIVATE 输入须在 load 期间保持未改写/未截断；没有声称 immutable snapshot。公开接口/报告均标 native policy，不把 Context 未知资源、原 SDK TLS/CRT 当作已测有效字段。

## 总控真实 RG 失败：已定位容量不足，修正待冻结，单独归因

总控报告其 frozen 15e Android normal contract **exit 1**，stdout 空，在 `guest_process_contract.c:202` 的 `value==&returned_token && !errors` 汇总断言失败。我没有设备操作，仅只读复制了总控明确指定的 `ffrs-integration/.local/device/rg477m-session1/owned-profile-contract.stderr`；原路径、SHA256 和副本在 `coordinator_device_observation`。stderr 可见一次 Query 的 native policy 为 total=1048576、used=102400、baseline=36864、stack=65536、count=1，以及两次 VA reserve/一次 free；这些日志不能证明所有断言通过，也不单独证明失败原因。

`exercise` 目前在字段/padding（32–34）、`pthread_getattr_np`（41）、reported stack size/SP/局部变量（49–50）、altstack（52）、VA（58/60/61）、wrapper 输出（66）仅递增同一个 errors。Android 路径把 `pthread_attr_getstack` 的报告范围恰为 65536 当作必要条件，这与 Context 实际映射 descriptor 是两个观察点；它是待总控/作者分项实测的具体平台假设，尚不能认定为根因。父代理随后提供了保留所有断言的分项诊断：唯一失败为 native usable=64752B / 0xfcf0，低于 supplied mapping=65536B，差784B；SP/local在reported范围，返回指针一致、altstack flags=0，其余检查由总控报告符合。原始分项 stderr 只读副本及摘要见 manifest.coordinator_device_diagnosis。修正尚未冻结，冻结15e RG组件验收保持 **FAIL / corrective implementation pending**。

## 边界

独立执行只覆盖自有正常 C/POSIX callbacks、正向 fixture 装载和 CLI info；未执行原 main/guest 函数、游戏、原 TP 写路径、设备/ADB、旧异常输入/安全 fuzz。未重跑作者的 33 步流水线，其原 main 的 133 不能算本评审执行或 0。legacy CLI 的 init/call/symbol 及所有游戏入口不在本次执行覆盖内。没有 SDK CRT/TLS 或游戏通过结论。

## 后续已收到的总控分项诊断

总控进一步使用保留原断言的分项版锁定唯一失败为 Bionic reported usable 64752B，比 supplied mapping 65536B 少 784B；其余原检查均符合。原 frozen 15e RG exit1 保持 FAIL。诊断只读副本及摘要见 manifest.coordinator_device_diagnosis；修正的预审条件见 `STACK_CORRECTION_PRE_REVIEW.md`。新实现尚未冻结，本评审未执行，不能记为已修复或通过。

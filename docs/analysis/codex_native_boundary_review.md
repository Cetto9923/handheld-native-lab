# T07-NATIVE-BOUNDARY-REVIEW — cleanup repair

修复冻结 `5c20b064f2866577ca1fdd903fa196dd842eb3a8`（parent `2fca9614b59332a3ef78a024fce2b379302492a4`）。原 P2 在本限定组件范围内闭合；没有发现新的具体功能缺陷。依据为独立源码/diff/严格交叉编译，加总控实际 RG normal、cleanup 及本评审额外 retained fixture 三个 exit0。设备执行均归因总控，本评审 Android 执行仍为 NOT_RUN；Mac 明确 UNSUPPORTED。

## 原缺陷与修复依据

旧 `2f:src/runtime/nso/native_boundary.c:112–113` 在 RX 设置失败后忽略 rollback munmap 失败，丢失仍存活的 mapping，binding/count 为零，scope 可关闭。总控曾实际执行本评审旧 cleanup fixture：exit1、47.067292ms、无超时，`live_mapping_not_returned=true`、scope_close_status0、4096B、thunk未执行，随后由 fixture 真实回收。原源码、报告和 `native-cleanup-old-*` 原始证据保留在父目录；新结果不覆盖这次失败。

新 `native_boundary.c:112–121` 在 protect 失败且 rollback 失败时返回 **NB_RETAINED=9**，保留真实 scope/mapping/mapping_bytes 并增 binding_count，entry 与 bti_guarded 保持零。没有发布可调用入口；scope 的既有 count/lifetime 保护继续有效。rollback 真正释放成功才返回 NB_PROTECT_FAILED 且不生成持有记录。

新 `native_boundary.c:133–144` 允许无 entry 的 retained binding 正常 unbind；仍先检查 live/dead owner 与 phase，munmap 失败不改 binding/count，真实成功后才减 count 并清记录。重复 unbind 不再次 munmap。既有 count 上限检查仍在分配前，未增加第二份 ownership ledger。`native_boundary.h:63–70` 明确调用方必须保留两个 owner 对象、不得发布 entry、需重试 unbind。caller-owned record 的零初始化、不可复制及控制/调用/销毁外部同步契约保持。

五文件全部从新 SHA 读取；只四文件变化，assembly 与旧冻结完全一致。本次未重做未变的整套 ABI 分析，也未把 repair 变成生产 loader/resolver 接入。

## 本评审独立执行

`build_review.py`：6 个 Android API28 严格编译、3 个链接、normal 符号检查、Mac 严格编译与明确 UNSUPPORTED 检查，全为真实 exit0。另 3 个输入/证据核对命令 exit0，共 15 个捕获命令。精确 argv/cwd、命令输入、stdout/stderr、退出码及 source/artifact SHA 均在 manifest 与 logs。

编译采用 C11、`-O2 -g -fPIE -ffixed-x18 -fno-omit-frame-pointer -Wall -Wextra -Werror -Wpedantic -mbranch-protection=bti`，normal contract 保留真实 Clang SCS。normal ELF 的 wrapper/failure-control 符号确实不存在；独立补测的符号替换仅在独立 fixture 产物。三个交叉 ELF 的 AArch64 PIE、16KiB LOAD 对齐、无 RWX LOAD、RELRO/nonexec stack 检查通过。实际使用的 111 个 partial NDK 成员 CRC/size 复核，未声称完整 archive SHA1 验证。

另保留一次评审工具输入核对的真实 exit1：首次误用 archive 前缀查询 toolchain-relative manifest key，在比较输入 bytes 前停止；改成实际记录的相对路径后复核 exit0。该失败不是被审代码失败，详见 reviewer_setup_corrections；未删除或混算为全0。输入核对结果先与 runner manifest 写入相互覆盖，随后改为独立验证结果文件并成功复核，旧成功日志也保留。

自有 `retained_contract.c` 采用有效 scope/signature，真实 mmap、受控 protect/unmap 失败，不调用 thunk。独立于作者新增 contract，它验证 rollback 成功无记录、双失败保留、失败重试逐字段保留、真实释放才清 count、重复 unbind 无额外释放；额外验证 owner 线程正常 pthread 退出并 join 后，retained scope/lifetime 仍在，其他正常 host 线程可真实释放并 close。补测不依赖 struct padding。

独立产物 `build/retained_contract.android`：37680B，SHA256 `5e4204bc4445da43bf9fda819cbdfb2a591694cc5f69d8c8a9d4e3c74fcc4e42`。本评审自己的 normal/author-cleanup 交叉产物为 52624B/34944B，debug 源路径不同，不冒称与总控使用的作者产物字节相同。

## 总控 RG 实际执行，单独归因

| Case | 实际退出码 | 时长 | 关键结果 |
| --- | --- | --- | --- |
| 新 normal | 0 | 156.280833ms | 原 normal 全 flags true，SCS/GP/FP/x18/嵌套/thread/dead owner/BTI 保留 |
| 作者新增 cleanup | 0 | 67.742708ms | rollback 成功、双失败 retained、NULL entry、失败重试、3次 munmap attempt/1次实际成功、count0 |
| 本评审独立 retained fixture | 0 | 40.296417ms | 上述基本清理，加 idle dead owner retained cleanup；thunk_executed=false |

三次均无 timeout，stderr 为空。normal 作者产物 52056B/SHA256 `228672de96da20ee2d9b8bd000c5e0104ee6428b9b17fb73dc94b227f99ce78a`；作者 cleanup 34560B/SHA256 `504d5308ddbb11270dbcc9a4f73f1b1535ba77d91dead96a4fdf55d5d01927e4`。本评审 fixture 的设备产物 SHA 与上述独立编译产物完全相同。

原始证据在 `ffrs-integration/.local/device/rg477m-session1/native-repair-{plan,stage,execution}.json`、`native-repair-{normal,cleanup}.stdout/.stderr`，以及 `native-retained-review-{plan,stage,execution}.json`、`native-retained-review.stdout/.stderr`。只读副本在 coordinator-evidence；五源码、artifact、stdout/stderr 摘要已经独立核对，精确路径/大小/SHA 在 manifest。作者 baseline regression 产物只出现在 plan，本报告未将其称为已设备运行；旧失败来自本评审旧 fixture 的总控实际 exit1。

## 限制

结论仅覆盖明确 register-only ABI 与正常返回/受控 POSIX cleanup。非 idle owner 退出的保留规则仍以源码核对为依据，不冒称本次额外设备测试。调用方仍负责发布/活跃调用/销毁同步与 stale entry 禁用；没有跨模块自动 quiesce、异常/longjmp/异步信号恢复或通用 aggregate/stack/variadic ABI 承诺。x8 运输不等于通用 aggregate 分类，21 参数 musl helper 不能直接绑定。生产无 loader/resolver 消费，无游戏/NSO/ADB/TP 写入或旧被中止工作流；组件结果不外推到后续 profile 或完整游戏。

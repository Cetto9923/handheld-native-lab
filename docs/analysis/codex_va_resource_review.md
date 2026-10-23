# T07-VA-RESOURCE-REVIEW

冻结 `3dbb0dcc81543b790c75510e9320981e429046f2` 的限定普通组件审查完成：**未确认需修的功能缺陷，独立宿主正常/预算耗尽/恢复/ownership/locale 合同通过。** 本人 Android 执行 **NOT_RUN**。总控同冻结的13个普通组件合同均0，但原始 malloc 诊断为 **139**、总控 runner 为 **1**、本轮 CRT **NOT_RUN**；因此不能报告整轮启动通过。

唯一自有写入目录为 `functional-review/va-resource/`。仓库、Git、其他报告及后续 WIP 均未修改；未执行 ADB、原 SDK/main/游戏、旧停止的异常 NSO/安全流程，未读 `test_loader.py`、未跑作者完整 builder。

## 冻结和依赖身份

本轮 source base 为 `3af578a3a9948dc4f5e05a67503ac3795e30e264`，12项增量及必要依赖共25个文件全部从 `git show 3dbb0dcc…:<path>` 保存。逐文件字节/SHA256、Git命令和真实退出码见 `final-manifest.json`。作者 freeze.json 的 SHA256 已独立核对为：

`1c2687b49c775733e38d70be4129eed60d049aabf6f3eeff584cda0b32d1e4db`

十二项增量及所有 component input 与 freeze、总控设备 plan 完全一致。native boundary **仍是5c字节**：C `81d92787eb1519069894af0498ac9f07c03a0e4e04c5eee67bd9e64dc1b28f83`、h `9d3dbef557ed41844a36bdf3101a17566905d2acd13bab6b1a51726f8d407f3a`、S `8b267cdb2c30bc74345f16e6235c62b11477ba270f90ea91285bb08b9f09815c`。OS TLS 为既有9c字节；本轮已更改的 Context/pages/region/Process 不继承旧组件结论。总控较新的7be组合和后续 ff964e9 代码/结果均不计入本轮。

只复用现存正向自产 fixture，逐个核字节/摘要，未生成异常二进制。两个新增9216B fixture：resource.nso SHA `227f205d7145720e5987c17a37aa0b8f1a64b772f05b8d5678d69529fba2fbec`；locale.nso SHA `5c446a8749ceaa01c54170ad0723d7d1039c050468389d78a330b7c4a2837981`。其他六个既有正向 fixture 来源/摘要同样记录。

## 源码核对

- `guest_context.c:67–174`：资源 arena 的实际 RW mmap 承载256B slab（32B header、最多224B payload）。活 node/ticket 分配、free chain、kind/size/owner、计数和 ticket 金额均在 Context mutex 下操作。启用 arena 后没有节点 malloc fallback；旧未启用资源 policy 的构造器仍走兼容路径，resource usage 明确 UNSUPPORTED/invalid，并不伪造资源 quota。旧路径的 `is_owned` 兼容接受不提供 arena 身份证明，本轮强身份结论只针对显式资源 arena。
- `guest_context.c:175–269,479–512`：正资源预算必须精确 host-page/256B 倍数；实际 mmap 失败释放控制对象且 out=NULL。票据占用真实 slab，元数据耗尽不能悄悄转宿主 malloc 或增加 backing pending/charged。destroy 拒绝活票据/节点/primary；真实 arena munmap 失败保留 Context/mapping、标 quarantine，usage.valid=false；重试真正成功后才清映射/释放控制对象。
- `guest_pages.c:74–154,281–357` 与 region allocator：同一 bounded allocator 服务 page region/allocation 和 lower VA reservation/interval。预留节点与 ledger 发布共用 page-owner 锁，避免并发快照看到未发布的 node。完整快照核对 arena kind计数与各 sole ledger、ticket owners/金额及 VA interval 数。锁顺序为 Process→pages→region→terminal Context，或 pages→Context；Context 不回调上层。raw allocator 安装/移除要求 ledger 空及外部 quiescence；没有增加第二份 accessible-interval ledger。
- `guest_pages_map/free/release`：预算不够或失败 rollback 时只归还已取得的节点/票据；VM protection/restore 不确定时保留待定票据/page allocation，backing_valid=false，不能把0 backing当可用信息。whole release 的真实 unmap 仍先于退款/归还节点；既有 whole-unmap fatal 边界未伪造为可恢复结果。
- `guest_process.c:271–342`：资源 getter 复用同一个 live Process/pages/Context 快照，要求完整 ownership 与资源 quota 身份有效。C API 失败不写 out，guest getter失败 fatal。返回是确切的 `(total, min(total, raw_occupied_slabs*256 + 0x80000))`，不是可用 RAM/RSS 或 Horizon page-table成本；用 remaining/margin 算法避免加法溢出。`__uint128_t` 只服务已知小端 AArch64 双GP返回，签名 gp_returns=2，不宣称通用 aggregate ABI。
- `guest_process.c:447–465,472–539`：四参 typed `strncasecmp_l` 丢弃 guest locale，64位 n 保留，返回 int32 精确差；Android 调真实 Bionic 非 `_l` API，不传 guest `locale_t` 或改 global locale。Mac 路径是显式确定性 ASCII 实现。现有 hash匹配的 locale 静态报告/事实及 Query返回ABI文本已复核来源摘要，仅读取既有文本/事实，未重读原SDK。公开 [AOSP android-14.0.0_r1 实现](https://raw.githubusercontent.com/aosp-mirror/platform_bionic/android-14.0.0_r1/libc/upstream-openbsd/lib/libc/string/strcasecmp.c) 使用 unsigned-byte ASCII映射/精确差值；[其 locale 包装](https://raw.githubusercontent.com/aosp-mirror/platform_bionic/android-14.0.0_r1/libc/bionic/strings_l.cpp) 转调非 `_l`。未把该tag冒充RG系统 libc 的精确 build commit。
- CLI：`--guest-resource-quota` 是显式 opt-in、无默认；仍要求 backing quota/stack，重复或不完整 policy 返回 usage。旧 constructor/config ABI 和未提供资源预算的加载/info路径保持；未把该 getter回退到 dlsym、常量0或宿主资源统计。

## 本人实际执行

在本目录运行 `python3 build_review.py` 和 `python3 build_supplement.py`；后者 locale 首版编译失败后只修自有测试声明，并以独立 `*-final` 命令完成。Apple clang21，C11、`-O2 -g -Wall -Wextra -Werror -pedantic -fno-omit-frame-pointer`，真实 guard/runtime 链接 `-lc++ -lpthread`。完整命令/cwd、原始 stdout/stderr、退出码/耗时/SHA及输入版本在 `logs/`、`final-manifest.json`。

共捕获 **49 条**构建/执行/检查命令：**44×0、2×1、2×2、1×-6**，无 timeout/pending。两个1分别是预期 guard-disabled 编译拒绝，以及 Reviewer 自有 locale 表的初版编译失败；两项2为CLI usage；-6为无有效 Query scope 的真实 SIGABRT（shell134）。不是49条全零或全部首次符合期望。

已完成：

| 独立执行 | 结果 |
|---|---|
| 冻结普通/argv/五项loader恢复合同 | 全部0；旧行为没有替换为虚假默认资源量。 |
| resource-owner | 0；真实预算耗尽-before-VM、节点 rollback/复用、8线程8000次节点分配、400次并发VA变化/快照，最后 used0。 |
| resource Process三预算 | 0；1MiB/2MiB/一个host页、三私有票据、真实VA/page节点增减、独立 backing quota、512KiB cap、失败输出保持。 |
| resource recovery | 0；arena mmap ENOMEM、活node拒绝destroy、munmap EBUSY后invalid及真实重试；protect+restore失败保留4 slabs/待定ticket，真实whole release后回零。 |
| locale | 冻结65536个u8对照0；新的独立 literal-table合同 **131072次**一字节/二字节对照0，另证 exact signed difference、高位字节不作Unicode折叠、NUL/长度截止、64位n和0长度不load。期望不依赖Darwin libc或冻结合同的 ascii_reference。 |
| 两个native fixture的Mac路径 | 外层合同0、native_calls=0，明确 UNSUPPORTED、无raw fallback；不算Android执行。 |
| CLI | legacy info、无资源默认、显式资源info均0；缺policy/重复资源选项各2。 |
| 离线生产产物 | `llvm-readelf` exit0确认 `strncasecmp@LIBC` 为未定义系统导入、没有系统 `_l` 导入；独立 `llvm-objdump` exit0看到成功返回前 `ldp x0,x1,[sp]`，与既有双GP桥匹配。只检查自有正常ELF，不执行Android。 |

新增 `allocator_ownership_contract.c` exit0：两个真实 arena 的节点全部落在各自实际 mmap 范围；跨Context/错误kind的身份查询拒绝；写满224B payload 不改其他payload。填满一个页的全部slab后，node/ticket都明确LIMIT且out=NULL，backing pending/charged/tickets仍0。释放唯一可用slab后可取得真实ticket，commit只有1B backing charge而slab charge仍256B；活资源拒绝unmap；归还后容量复用且清零，最终两arena真实unmap、blocks/charges全零。没有仅比较总量就声称 ownership 成立。

新增 `process_retained_contract.c` 最终exit0：**argv mmap ENOMEM＋arena munmap EBUSY**组合发生时，构造返回保留的Process、原错误ENOMEM/cleanup EBUSY；exclusive owner继续占用，两种Query输出都保持。下一次destroy真实munmap后才释放owner；随后资源Process可重建。再用旧constructor确认没有 arena默认/getter，资源API返回UNSUPPORTED且out保持。两arena合计3次unmap尝试、2次真释放。

保留的 Reviewer 问题：locale literal 的256B字符串初版声明为数组256，Clang因额外NUL terminator严格拒绝1；保存 `locale_literal_contract.initial.c` 与原失败日志，改为推导长度并断言257，映射/期望不变，最终编译和执行0。辅助符号解析曾因正则漏掉 `@LIBC` 后缀触发外层 assertion/1；实际readelf已0，按已有stdout修解析，没有重试工具或改生产。两者均不是冻结实现缺陷。

## 总控设备证据与真实失败边界

只读核对并复制 `ffrs-integration/.local/device/rg477m-session1/va-resource-v1-{plan,stage,execution}.json` 及13个组件stdout/stderr，见 `coordinator-evidence/`、`evidence-verification.json`。本人25份源码、全部组件依赖、8个正向fixture和5个Android产物哈希与plan/freeze一致；9个官方partial NDK library/header输入重核bytes/SHA/CRC，不声称完整NDK整包验证。5个正常/专用恢复ELF均离线检查AArch64 PIE、16KiB LOAD、NX、RELRO/EH。

其中正常合同936296B SHA `91d3ad61d27723f7495aa94882dd99541666d4648399d93609ca189e77890542`；CLI803896B SHA `72405ea9a9201579dfebc00b2162249e5ebd33166861f4f180c9b9a59ae91ba3`。其余三个专用恢复产物完整身份在manifest，不混为生产错误注入。

总控真实 RG477M：normal、arguments、arguments-native、os-tls、os-callback、resource、resource-native、locale-native、resource-owner、locale、resource-recovery、arguments-recovery、boundary-retained **13项均exit0、无timeout**；info亦0。resource-native在三预算下实际guest调用3次、验证双GP返回及x18；locale-native实际6次，普通locale覆盖65536对。resource recovery arena释放是2尝试/1真释放；专用hooks结果不冒充生产ELF故障覆盖。本人没有执行这些设备动作。

原始入口只读取总控execution元数据：startup-empty严格停67/+0x137a4、startup-nonempty停61/+0x1345c、nnMain停126，均 **133**；直接 pre-init malloc 为真实 **139**，并非strict133或组件PASS。总控runner在保存记录及核对main前后SHA一致后因unexpected139最终 **exit1**（父端报告；保存runner源码的终止assert也已核对），随后本轮 **CRT NOT_RUN**。这些失败不删除，也未据此猜测3db组件因果。后续对pre-init入口/新启动顺序的定位和 ff964e9 结果是另scope，未并入本快照结论。

## 限制

本人 Android 执行最终 **NOT_RUN**，未独立重做Android交叉构建，只核冻结产物/寄存器返回桥和总控设备证据。普通C借用/owner生命周期须遵守quiescence和调用方串行约束；不覆盖任意内部结构损坏、并发destroy、旧异常NSO或安全探测。arena只定义此port的ticket/page/VA-node预算，其他loader/pthread/TLS/控制对象仍是明确排除的host administration；旧未启用arena的兼容路径不具备本轮bounded身份保证。无SVC/host RAM/RSS等价主张，无完整CRT/ThreadType/ELF TLS或游戏启动通过。

本轮无工具拒绝、超时或挂起；限定组件审查无阻断。原始malloc139及CRT未运行是继续由总控处理的系统诊断边界。报告/精简manifest可归档，源码、fixture、ELF及原始文本保留本地。

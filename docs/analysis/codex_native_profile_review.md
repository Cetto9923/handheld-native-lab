# T07-NATIVE-PROFILE-REVIEW85

冻结 runtime **85a13c79378550fe850ffb5ff182cdbdfec177f8**，依赖 pages **4695e3c995993832ff8e274415f58b1cdb1e4684**、native_boundary **5c20b064f2866577ca1fdd903fa196dd842eb3a8**。本次限定功能审查完成，未发现新的具体缺陷；普通宿主契约、独立门禁补测与独立交叉编译完成，总控另在 RG 运行组件与独立清理顺序 fixture。本人设备执行 NOT_RUN，Mac actual guest 明确 UNSUPPORTED。

## 冻结与输入

只用 git show 读取85的8文件增量与必要冻结依赖，共23文件，逐文件 revision/size/SHA保存在 manifest.sources。没有读取作者随后 WIP，也没有将当前工作树 HEAD/clean 当冻结证据；作者 freeze.json 声明85 clean，本评审独立匹配冻结内容与总控输入摘要。

四个现存自有正向 fixture 从 `.local/native-profile-v2/` 复制，均9216B，复制前与作者freeze、之后与总控plan逐SHA匹配：contract、boundary、init-executor、readonly-import。未读取/执行 test_loader.py，未执行完整builder，也未构造畸形NSO、异常输入或复制旧被中止QA流程。readonly-import是已有普通只读配置的 unsupported/no-write 合同。本目录 fixture 前后摘要一致；借用输入在使用期间仍须由调用方保持不被并发改写，MAP_PRIVATE不等于不可变文件快照。

## 独立执行与真实退出码

42个捕获命令：**41个exit0、1个预期exit1**（guard宏关闭时的明确编译拒绝）。精确argv/cwd、原stdout/stderr、退出码、输入/产物摘要在manifest与logs；没有pending、uncertain、拒绝动作或未解释的失败。

| 普通执行 | 实际结果 |
| --- | --- |
| host normal | exit0；errors0、两次host callback，Query七字段/padding、实际栈与页归属、信号恢复 |
| host loader recovery | exit0；errors0、5个受控自有VM/ownership清理分支 |
| host boundary | exit0；actual guest executed_calls0、UNSUPPORTED、output不写、raw_fallback=false；ended-session设备结论为null |
| host retarget/init executor | exit0；retarget_errors0；ELF三个injected executor调用、wrapper一个，失败中止 |
| host readonly retarget | exit0；NSO_UNSUPPORTED、不写原slot |
| legacy/profile CLI info | 各exit0；只用本目录普通contract fixture |
| 独立lifecycle/gate supplement | exit0；foreign host thread call/init拒绝且call output不写；owned module旧raw call与raw init各自有子进程实际exit2 |
| Android独立cleanup-order交叉编译/链接/ELF检查 | 全exit0；本人执行NOT_RUN；实际RG另归因总控 |

严格C参数为 `-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`；host编译明确启用typed guard runtime并链接libc++。Android目标API28 AArch64，`-fPIE -ffixed-x18`；125个实际partial NDK依赖成员CRC/size复核，不宣称完整archive SHA1验证。独立Android测试ELF有16KiB LOAD对齐、无RWX LOAD、RELRO/nonexec stack、GNU_EH_FRAME，链接map的guard来自官方 `libc++abi.a(cxa_guard.cpp.o)`。本次没有重复未变5c组件的整套SCS/GP/FP QA。

## 源码与行为核对

**导入必须经已知签名和边界。** `guest_process.c:263–354` 的resolver闭集仍不进入extension/dlsym；typed guard三个地址直接引用真实runtime。新增signature表与实际C目标逐项核对，限制已知scalar GP参数/返回，未加入variadic/stack/aggregate。`exit`、`__cxa_finalize`因可调用guest callback而严格unresolved；atexit仅登记，卸载observer仅discard，不在native gateway中裸执行guest destructor。未将21参数CRT helper猜成此接口可支持。

lazy preparation只在 `guest_process_call:356` 通过current_process、RUNNING ThreadRecord、实际pthread身份检查后发生，scope属于执行primary而非load协调线程。全部binding成功后才一次retarget；任何bind/retarget失败都保留owner并quarantine。NB_RETAINED9 binding的entry为NULL，失败路径不会进入retarget或guest execution。`guest_x18`由每次packet返回保存，后续同primary call复用；真实host x18仍由未变5c assembly在C前捕获/切域。

**retarget只写记录过的普通import位置。** `nso_loader.c:397–428` 先验证所有target的已解析FUNCTION身份、非零目标和唯一symbol；每个记录site还需64位/零addend/对齐/原data可写页、不碰text、当前值等于expected且无import-write重叠。全部preflight通过才commit所有slot及metadata；不改变保护、不扫描未知内存。既有fixture实际验证重复请求拒绝、正常和幂等retarget、expected值被host改写后的RETAINED，以及只读位置明确unsupported且不写。其余不支持布局是静态核对，未增加异常二进制测试。

**guest call与init没有profile raw fallback。** load于 `guest_process.c:137` 标记executor-required；`nso_call`在 `nso_loader.c:447` 对此标记明确exit2。独立C补测以有效owned正向module验证：旧直接call、旧raw init API均在任何A64目标执行前exit2，而非借Mac能执行A64继续。legacy非profile模块原raw API保持独立兼容路径。

`guest_process_initialize:380` 标记单次init attempt，使用injected profile_executor；`nso_run_init_execute:453` 对DT_INIT及每个initarray entry都调用该executor，错误立刻返回，不走备用raw调用。wrapper policy仍只走DT_INIT，不第二次扫array。CLI仅接受已审查命名scalar entry与精确参数数目，未知数值offset/签名明确拒绝；Mac调用不会为继续运行回退raw。host injected executor验证的是walk/中止逻辑，不能冒称实际guest initializer执行完成。

**实际guest primary结束后不能换owner续跑。** `invoke_primary:417` 在scope创建后的primary返回时标记guest_session_ended；新的primary callback即使建立，其guest call仍在prepare前返回NSO_UNSUPPORTED。绑定保留至module真正释放，避免guest缓存thunk地址成为过早stale指针。总控正向boundary实跑两次同primary call与后续primary拒绝；Mac只证明unsupported门禁。独立foreign-thread补测也验证load线程以外的任意host thread不会误用当前primary。

**清理依实际释放推进。** `guest_process_destroy:450–488` 先释放private pages，随后typed nso_release(module)，observer仍在活image时执行一次；只有release完整成功后才遍历unbind/close，最后Context销毁并释放exclusive owner。module释放失败不会解绑thunk；unbind失败保存Process和该binding，已成功清零的前序binding可幂等跳过；module已为NULL时下一轮继续binding清理。没有第二份VA/ticket账本。typed retained、真实image释放及observer生命周期沿用既有typed release，不能以仅descriptor状态或总量代替真实释放。

## 独立清理顺序fixture：总控RG实际执行

自有 `cleanup_order_contract.c` 使用冻结positive boundary fixture，真实调用一次非游戏A64例程（经typed ReferSymbol import，结果0x91），随后正常primary join。loader已有测试hook仅控制image munmap，boundary测试产物仅重命名munmap；没有替换ABI实现或伪造释放。

1. 第一次destroy：image munmap受控EBUSY，Process/module保留，**thunk unbind attempt为0**。
2. 第二次destroy：真实image munmap成功，module=NULL；随后dead-owner thunk munmap受控EBUSY，Process/binding保留。
3. 第三次destroy：真实thunk munmap成功，Process清空；新exclusive owner可创建/销毁。

独立产物714848B，SHA256 `3b241e3f51c9b5cf289f5d56389a575095ebedc45289cc0b41f9c3fafbc5e267`。**总控RG实际exit0、99.205541ms、无timeout**：module2attempt/1real、thunk2attempt/1real、actual_guest_calls1，全部flags true。本人仅独立交叉编译与核证据，没有设备操作。源23文件、依赖SHA、产物、positive输入和stdout/stderr摘要全部匹配。

原始证据 `ffrs-integration/.local/device/rg477m-session1/profile-cleanup-review-{plan,stage,execution}.json`、`.stdout/.stderr`，只读副本在coordinator-evidence。这是85新profile的集成清理顺序补测；没有抹去旧2f问题/old exit1，也不重做或重开5c已闭合档案。

## 总控已有RG组件证据，单独归因

`native-profile-v2-contract-1..5` 五合同均实际exit0、无timeout：普通owner/Query/page，真实boundary两次调用与ended-session拒绝，retarget/init injected executor，只读retarget拒绝，以及bind rollback double-failure retained（3attempt/1real，guest_executed=false）。上述源码、依赖和fixture与本次快照逐SHA匹配；production normal/CLI ELF离线核对没有loader/boundary测试hook，EH frame header section/segment存在。

作者normal Android产物771408B，SHA256 `0f4d57dd3243893ef2f2cf4f61e80bb0638b7d2e72739b32dba55d740cb40e82`；CLI725256B/SHA `88004a43021e49662f0b5fe1268c0d9604f675197382347f2259c8a2599a27e7`；controlled cleanup779416B/SHA `5c6d581225f95c59e0eaf59d9ad7bd678d6b1b45c6bdac762e30afae60250140`。

总控同批原模块info0，另四个原始entry实际**133**，不是全部0或游戏启动PASS。本评审仅读取该execution metadata并保留来源，未读取/运行原game/SDK模块，未执行其原entry命令；原entry stdout/stderr没有作为本评审动态分析输入。普通fixture实际导入执行覆盖typed ReferSymbol，其他已知签名以源码核对为依据，不能把一次marker import外推成36个原导入全部ABI兼容。

## 剩余边界

本结论是独立宿主功能/交叉编译审查，加明确归因总控的实际RG正向组件结果；本人Android NOT_RUN。接口仅已知scalar register ABI、正常返回、同一实际primary生命周期；不支持unknown aggregate/stack/variadic、callback reentry、foreign unwind/longjmp或一般异步signal适配。diagnostic signal设施不等于上述支持。

调用方仍须排除并发mutation/teardown和stale entry调用，模块/回调/provider需要真实quiesce；non-idle dead owner继续retained。thunk页为明确的native管理开销，不混入guest logical quota；heap与Query沿用native port policy，不宣称原SVC物理测量。

CRT、SDK ThreadType/TP、ELF TLS完整退出生命周期、21参数helper、OS-TLS接线及游戏启动仍未由本单元证明。9c OS-TLS manager不在85源码中，没有把上一组件审查当作已接线通过。未执行ADB、原game/SDK、TP写入或旧异常QA，未改仓库/Git/配置；所有本次输出仅在functional-review/native-profile。

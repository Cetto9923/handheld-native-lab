# T07-OS-TLS-PROFILE-REVIEW

冻结 **28dc1a792946ae990690f11d71e89a60bf5d8b8b**。本次 consumer 源码与独立普通宿主审查完成，未发现需要修复的具体功能缺陷。独立 Android 目标编译额外检查超时，未计 PASS；本人 Android 执行 NOT_RUN。七个实际 RG 合同单独归因总控，9c组件QA没有代替本次接线验证。

最终证据以 **final-manifest.json** 为准，summary.json给出状态；manifest.json是执行中的工作记录。所有源码、输入、命令argv/cwd、原stdout/stderr、真实退出码/超时均保留。未修改仓库、Git、配置或其他代理文件。

## 冻结与执行

五个变化文件从28 git show取得；其余必要runtime源码从85a13c冻结，pages从4695e3c，native_boundary从5c20b064，OS-TLS从9c24ed35，共25文件。两份SDK静态报告也从OS-TLS9c取得，未读取其原SDK/NSO输入。未读取runtime下一phase WIP、test_loader.py，未运行完整builder、旧异常NSO/安全流程、原game/SDK、ADB或TP写入。

五个现存自有正向fixture逐SHA匹配作者freeze及总控plan并复制到本目录。新增 `os-tls-callback.nso` 为9216B，SHA256 `fe0c3532a6d7783624d094317ca07a5661cf4fb68ce262a0626d63688a37ac45`；其余四个fixture与85审查相同。只读配置的unsupported合同沿用现存普通fixture，没有构造异常二进制输入。

独立捕获命令33次：**30个exit0、1个预期编译拒绝1、1个预期SIGABRT(-6；shell等价134)、1个Android编译超时而无compiler退出码**。不能写成“33步全0”。严格宿主参数为C11、`-O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`，guard明确绑定真实typed runtime；normal与loader recovery使用冻结组件。

| 独立普通执行 | 实际结果 |
| --- | --- |
| normal / loader recovery | 各exit0，errors0；新record要求下原Query/栈/页/信号/ownership合同仍通过，recovery含5个受控分支 |
| 四个OS typed wrappers | exit0，APP16槽、0x1203且output不写、Free保留stale value、重新Allocate清零；两个实际primary各有独立record |
| boundary / callback fixture的Mac路径 | exit0，但actual guest及guest析构未执行；明确UNSUPPORTED，不能算native callback PASS |
| 非空value0 guest析构的Mac provider | 原始SIGABRT -6，failed round1/slot0/value0，provider=(FAILED,NSO_UNSUPPORTED)，没有成功cleanup日志 |
| retarget/init injected executor、readonly拒绝、legacy/profile info | 均exit0，仅普通正向fixture |
| 自有ordinary failure consumer合同 | exit0，详见下节；含一个真正SIGABRT子进程，未裸执行A64 guest代码 |

额外 `strict-android-process` 在30.032026秒超时，stdout/stderr为空，记录exit_code=null、timed_out=true，外层驱动exit1。没有可报告的compiler真实退出码；该object不作为验证产物，后续计划中的Android contract/OS-owner编译未运行。未重试该动作，也未以总控构建替代我的独立编译成功。

## 四个APP服务的签名与结果

依据冻结 `codex_tls_sdk_lifecycle_report.md` 的限定静态PC/ABI结论核对，并非本次重新读取或执行SDK：

| 服务/SDK PC | 已核对ABI | 本次consumer处理 |
| --- | --- | --- |
| AllocateTlsSlot /143b60 | x0=u32输出指针、x1=void(u64) callback身份；w0 Result | `uint32_t(uint32_t*,uint64_t)`；0成功，仅APP耗尽映射已证实0x1203且output不写 |
| FreeTlsSlot /143bf0 | w0=slot，void返回 | `void(uint32_t)`；不调用析构，不清value |
| GetTlsValue /143c10 | w0=slot，x0=u64 | `uint64_t(uint32_t)`；允许未分配槽的stale值 |
| SetTlsValue /143c30 | w0=slot，x1=u64，void返回 | `void(uint32_t,uint64_t)`；CLEANING期间可重入 |

`guest_process.c:279–352` 的四个精确mangled resolver与 `import_signature` 的GP参数/返回数一致。callback保持u64不透明身份，没有转换成host回调裸调用；非零身份须在本module ready/aligned text中。其余不支持/内部错误明确fatal，不伪造SDK Result。SDK/C-sdk allocation wrapper、slot16、GetCurrentThread/ThreadType、ELF TLS均未接入；没有按相似名称放开resolver或extension。

设备typed wrapper合同验证实际consumer结果；另外五个真实guest GOT imports（四个服务+Query）的正向native fixture验证边界运输。没有把9c直接API测试或普通C调用当成所有原SDK导入都兼容。

## 注册、析构与实际primary生命周期

`invoke_primary:470` 先建立当前Process和真实pthread ThreadRecord RUNNING，再注册独立OS record；Query除了既有owner/stack/ticket约束，还要求OS manager有效、未quarantine且恰有一条record。注册失败不进入用户callback，正常返回到Context join/回收路径；不会产生七字段Query假成功。

正常路径为：**register → 用户callback正常返回 → OS cleanup → unregister并清os_thread → fault-scope end → ThreadReturned/session-ended → Context join/栈释放和refund**。开始OS cleanup时current_process、RUNNING、实际栈和module/fault scope仍有效；原nb_call已经返回IDLE。

`invoke_os_destructor:448` 只通过同一 `guest_process_call` 提交一个已知u64参数，故沿5c assembly恢复host/guest x18及正常返回，没有naked raw fallback。9c manager在invoker外释放自己的mutex，guest析构可经typed imports重新Get/Set/Allocate/Free或Query。源码将cleanup完成放在ThreadReturned前；实际RG正向consumer验证A(0)、B(9)、A(5)，清当前槽发生在返回后，较早槽重设进入下一轮。日志为rounds3/invocations3/remaining0（含最后空扫描），Query同时观察RUNNING、1 thread、65536 stack、valid0x7f。

只有成功cleanup才能unregister；只释放逻辑record，不对opaque value进行host free。manager中的callback身份保留至Process destroy；真实guest session结束后，`guest_process_run`在创建下一primary/record前即拒绝，防止下一record第一轮再调用已结束native session的callback。该门禁比85的“新primary内再拒绝guest call”更早；root boundary合同已验证新的run拒绝。

## 自有普通错误/恢复补测

`ordinary_failure_contract.c` 仅在测试产物对OS组件calloc/free和Process cleanup/unregister做薄包装；所有正常实现仍使用冻结源。没有改变生产源码或provider返回结果。

1. **manager创建OOM：** 只对OS组件的一次正常calloc返回NULL；Process返回NSO_OWNER_ERROR、out=NULL，OS live allocation0，新exclusive owner可创建。
2. **record注册OOM：** Process/module已就绪时令下一OS calloc失败；run返回NSO_OWNER_ERROR，用户callback次数0，manager仍是唯一OS allocation；随后同一Process正常primary可运行，Query看到1条record/64KiB，最终destroy后OS allocations0。
3. **正常callback返回后的真实provider失败：** 自有子进程注册有效module callback身份，value为0；Mac实际provider返回UNSUPPORTED，9c真实cleanup进入quarantine。薄wrapper在Process failstop前观察manager+record仍为两个真实分配、registered/quarantined thread各1、事件round1/slot0/value0；公开primary-stack API仍成功（意味着ThreadRecord RUNNING、当前真实pthread匹配），局部变量仍位于实际65536逻辑栈，module entry仍ready。pipe只收到Q，未收到unregister标记U；之后原Process代码真正SIGABRT。没有返回Context或把失败记作cleanup完成。

这补充的是consumer自身的错误路径与owner次序，区别于9c组件的quarantine单测。仅在host执行；不宣称设备OOM/provider错误注入已通过，也不假装提供recoverable stack-retention API。正式策略是隔离进程failstop，owner保持到进程终止。

## 总控实际RG结果，单独归因

只读复制 `ffrs-integration/.local/device/rg477m-session1/os-tls-profile-v2-{plan,stage,execution}.json`、七个contract及info的stdout/stderr。25个源码/依赖、5个fixture、实际device artifacts和这些日志摘要已独立核对，副本和SHA在final-manifest/coordinator-evidence。

七合同均exit0、无timeout；info亦0。actual OS callback合同79.428000ms：A调用2次、B1次，firstA0/secondA5/valueB9、order0x121，Query的RUNNING/1thread/64KiB断言通过。producer/runtime normal Android产物824656B，SHA256 `7fa988417a9c3a147edf90d43734299bc78dd488dad10cb192d01471ff4130c3`；CLI759352B/SHA `b142eb52f0f17eb48d5f29f400181f576ae021eb82c67726e75bd4e1ca6c9973`；controlled cleanup832768B/SHA `ba358bd81b6335b908bc47ffd70c9033a996a89d411f7e7327a1af140a1d2cb5`。

同批四个原entry实际133，只读取execution metadata，未读取/执行原game/SDK及其entry输出。总控报告startup从Allocate59前进至GetHostArgc60/+13420、133；这只归因总控的原输入控制流定位，不能当完整CRT、BSS直接readback或游戏启动PASS。

## 历史失败与本次执行限制

**producer v1 link exit1保留。** 已复制的 `producer-history/v1/run.json` 明确returncode1/timed_outfalse，原command没有 `--image-base=0`。v2冻结builder加入该参数，fixture SHA与root真实结果匹配；总控另确认v2 54个匹配构建命令完成。这里没有把v1改成成功或独立重跑整个producer。

读取v1旧stderr原文件曾停在read_bytes/f.read；本人中止自己的只读session，确认退出130/KeyboardInterrupt，未再读该文件。旧run.json及空stdout已复制，stderr copy明确NOT_COPIED。随后lsof诊断完成0返回原reader PID；pgrep诊断返回3（环境sysmond service not found），未重试。没有平台安全拒绝，也没有挂起的该读取脚本。最终另存final-manifest，避免任何工作记录迟到覆盖。

另保留一次评审输入定位错误：runtime28没有SDK静态报告，初次git show返回128；之后从实际拥有报告的OS-TLS9c冻结获取。该128、上述130/3和Android编译timeout都属于评审动作的实际结果，未混成实现测试PASS或被抹掉。除未完成的可选独立Android编译外，没有阻止源码/宿主审查收口的项目动作。

## 边界

此次证明限定的普通primary返回路径；native consumer动态结果来自root，五轮上限源自固定9c实现，本次真实guest fixture只有上述三次析构，未声称设备上覆盖五轮所有重入组合。没有SDK ThreadType/TP、ELF析构链、SDK pool/C-wrapper、异常/取消/显式终止/一般信号适配或完整CRT承诺。外部调用仍需生命周期同步与quiescence；非正常provider不允许恢复后重跑callback。

本人无ADB/设备操作，独立Android执行NOT_RUN；额外Android编译超时未完成。此前native/OS组件报告保持不变，本次输出仅functional-review/os-tls-profile。

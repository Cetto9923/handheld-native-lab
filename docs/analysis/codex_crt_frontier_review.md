# T07-CRT-FRONTIER — 一页有界审核

**下一硬关是 515 的真实 DSO/逐线程 TLS 语义，不是运输。** 本轮只读 `851a84238a6a13bedb0485ed3b713fb231a5854b` 源码与已保存证据；无新逆向、外部请求、编译、游戏/SDK执行、TP写入或 ADB。逐项来源/摘要与只读检查的真实失败见 `manifest.json`。

**已知。** 当前 `native_boundary.h` 已有 `nb_stack13`、`nb_call_stack13` 和 version2/`NB_SIG_FIXED_GP21` 导入绑定：x0–x7 加 13×u64/104B，112B含对齐而非第22参。当前 `.S` gateway 已用160B、从原 incoming SP复制，故应直接复用7be运输和434修正，不能再造同一合同。既有7be报告记录总控真实双向GP21合同0；这是组件能力，不是515 consumer已接通。本人早先“需新增运输”的建议已撤回。

保存的21-word捕获全部 match；原SDK未初始化 gate实际返回2，诊断主动stop86，构造未续跑。所有来源槽经过RELR，原0/8变成B/B+8；不能空化或当立即数。caller还对返回u32==1作特殊分支，不能猜“返回0即兼容”。既有callee/TLS报告显示515不仅复制模板，还维护DSO/模块表、分配/锁和thread-pointer hooks。真正登记必须有实际模块/DSO身份、范围与生命周期、真实可用的allocator/同步/模块表及失败保留，不能只让Process登记pair或置一个成功标志。

**已知与推断的边界。** main模板544B初始化、总4121B、8对齐/16B未建模前缀有证据；当前 `module_tls.c/h` 只提供独立复制/清零/对齐，明确不保留嵌入指针指向的module、没有TCB字段/current getter/析构。它可复用作存储原语，不能把镜像称为guestTP。main有1489处内联MRS，含跨块、栈逃逸和动态偏移；只适配常见相邻模式或固定单primary地址不会覆盖后续线程。**推断：** 正式放行构造前，所执行路径的每次TP访问必须得到正确当前线程视图或严格停住；不要求先宣称所有1489处均可达。固定literal不能成为生产provider接口，亦不应改宿主硬件TP冒充guest布局。

SDK另有TPIDRRO+1f8的ThreadType和+1f0的ELF线程对象；opaque primary token不是这些结构。现有scope/import thunk保留创建线程的owner/lifetime，错误线程会拒绝；共享module GOT指向该thunk后，为新pthread另建scope也不会自动重绑它。因此当前成功的primary/HOST回调不证明CreateThread兼容。未来必须先有逐线程context/TLS、正确ThreadType及退出/join顺序、线程可分派且保留SCS/owner规则的导入路径。OS槽最多5轮与ELF LIFO drain不可合并；普通SDK线程的“OS析构→状态通知→hook/ELF drain”也不能用状态通知替代析构完成。

**仍未知。** 515完整记录schema/全部分支、原始初始线程的完整TCB/header、全部TP到达定义/别名，以及五参Start的x4职责与x0/x1协议尚未完整闭合；不能凭通用musl名称或另一游戏demo补齐。已保存boot报告支持SDK-init→startup→main-init及DT_INIT拥有array；本人未直接重读上游LAS main.c，故只引用该已核报告的决策，不宣称已采用其runtime/四参ABI。

**可编码优先级（仅两项）。**

1. **515具体consumer与事务记录：** 直接接现有v2 GP21；把21字保真归一为经owner验证的模块/DSO描述符（未命名字段仍opaque），实现已证实的登记、hook/模板关联与部分失败保留/注销。SDK↔main双向绑定及真实全局前置未满足时继续显式unsupported，不返回伪成功，不跳过DT_INIT/array。
2. **逐线程TLS provider接线：** 以`thread-generation + module/DSO-id`关联现有镜像与真实storage provider、析构链和module引用；先用自有两个真实线程验证隔离、退出后join再回收、失败保留。先选定并验证native直接TP访问的适配接口，再放行对应原路径；保留宿主TP，CreateThread及共享GOT多owner消费继续关闭，直到这些实际语义成立。复用已有module_tls、owner/page账、typed HOST回调；不重写libc/运输或另造全套框架。

**851 CLI简审。** `nso_call.c:61–86,167–178` 的`--startup-allocator`仅在owned、无initializer、无额外参时启用，显式调用startup后做48/64B两次alloc及逆序free，不进入normal SDK boot路径；失败标非0，free失败即停、不host free或盲重试，destroy未完成保留owner。未发现需修增量问题。总控最新通知6a HOST14case符合预期、组合重链allocator0/0、CRT仍515/133；本人未执行或独立重核该新组合，不能把isolated allocator成功或startup返回等同musl/CRT/游戏通过。

# 原生移植接力：startup与OS清理已过，完整CRT未过

最后事实核验：2026-10-23T14:27:23.978054+00:00。先读AGENTS.md，再实读Git/WIP与state.json；本文不代表后台持续运行。

## 当前结果与边界

- 目标：《最终幻想：共鸣》Switch→RG477M Android ARM64真实原生可玩；荣耀GT Pro等比例/必要留边。仍无标题、可操作场景、完整游戏APK或性能成绩，不报整体百分比。
- **最新实际RG/f60：** 原始nninitStartup空/非空均正常返回，随后真实OSslot0析构18c60完成（2轮/1调用/remaining0），最终退出0/0，无timeout、main哈希不变。未跳过值0析构。
- 原游戏48/64字节allocator原回调也已在startup后真实分配/按size释放，空/非空最终0/0；851的显式隔离合同不改正常SDK启动顺序。
- **完整CRT仍停515：** 新同版原DT_INIT包装严格133、`__nnmusl_init_dso` caller+5920a34；五参SDK Start/DSO/ELFTLS/TP、资源/着色器/present/可玩均未过。
- f60另6组件/identity/native零值析构/SCS/恢复/fresh回归与info均0；身份仅真实primary opaque PROT_NONE token+64bit nativeTID，**不是SDKThreadType/TCB布局**。typed同步HOSTcallback、GP21、真实TLSallocator IDLE消费者均已实现，Process HOST消费者已真实RG11×0+3预期134且独立FINAL。
- [真实执行报告](../docs/analysis/codex_guest_process_device_report.md) · [阶段验收](../docs/analysis/codex_stage_status.md) · [命令/哈希索引](../docs/analysis/codex_handoff_evidence_index.md)

## 仓库、分支和实际写者

- 总控：`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration` / task/codex-integration；本轮源码HEADe305d1ed155dbba7e6176d621f2834a074d3a022，最新文档提交以git实读。root唯一Git/ADB/协调写者。
- 原交付：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native` / work/rg477m-native；14:14UTC已成功ff至db0f9fce1e0b87a29f02fd97d81abcb73fabe2e9且clean、无push。该SHA是历史事件；最新交付以原树与任务树GitHEAD/`.local/integration-approval/`记录核实，不用历史SHA推断。safeff须核clean/期望HEAD，不覆盖成果。
- HOST消费者冻结`../ffrs-tls-host-consumer` /6a615e3，已集成e305；当前原游戏allocator回归仍0/0，CRT515仍133。当前所有作者/QA已交回，下一任务由root登记后才启动，不虚称持续后台。
- f60独立QA已FINAL，未发现需修组件缺陷；两代API/真实TID/region与真实munmap观察、失败保留通过。初版Macmincore harness-6已校准纠正并保留；reviewer Android NOT_RUN。所有worker当前idle；后续真实分工看state，不按旧消息猜活跃。
- ffrs-primary-identity/f60、ffrs-tls-provider/cfc、ffrs-tls-provider-fix/80d、ffrs-native-host-callback/434均clean冻结，不能覆盖。详细lease以state.json为准；占用非OS锁，不按时间抢占。

证据路径约定：本文及报告`.local/`均相对上述**总控绝对根**；`../ffrs-*`相对总控父目录。原交付只同步Git自有代码/文档，不复制大型原始证据；先核`.local`链接，不reset/clean/stash。

## 恢复顺序

1. 核Git/活跃writer/现有manifest。f60执行已完成：primary-identity-v1-{plan,stage,execution}.json；不要重放stage。源码f60与root83ad的src/tests/scripts差异0，复用hash匹配的增量产物，避免完整113步重编。
2. HOST消费者14RG+独立QA已过，现有GP21/104B运输也已过，不再造同契约。真正下一目标是完整CRT/DSO/guestTP生命周期，参考[前沿审核](../docs/analysis/codex_crt_frontier_review.md)。515不能return0/1假放行；本SDK Start五参/三callback及DT_INIT独占array顺序复用[boot报告](../docs/analysis/codex_boot_order_reuse_report.md)。
3. main1489处TPIDR_EL0内联访问、SDK自身TP/ThreadType与Horizon服务不可直接拿Bionic替代；不写hostTP、不猜默认结构。module_tls模板组件不等于已接通guestTP，边界/完整可行性仍待实证。
4. rootADB：`/Users/yuyan9923/Library/Android/sdk/platform-tools/adb -s RG477M01445122`；device15/host22秒（组件受控子进程30/40），保真实退出码。官方partialNDK逐成员hash/CRC已核，完整归档未验证。精确构建/运行以manifest argv为准。

## 复用与已否定路线

通用已验证子集：NSO/重定位、唯一页票据/VA资源、native x18/SCS/GP21/HOSTcallback、逻辑32槽OS TLS、owned argv/typedlibc/libm、生产VkBuffer与MIT LAS transfer。main/SDK白名单、偏移/模板属本游戏专用；不推断任意游戏可用。
LAS Beta4 fe66f31a…已采用MIT命令记录/typed transfer；实际GPU copy/clear零差异，仅transfer，无shader/present。REA Skill已实际读取；当前CLI函数切片前Swifthelper失败1，无functionEvidence，LLVM证据另记，不重试同一故障。
保留失败经验：mmap无MAP_FIXED不能推出必须0址；tail0曾掩盖139；最长RELA候选不是完整性；pre-init malloc139是原对象构造前访问，已定位不补vptr；leaf/musl早return2不是游戏；94MB必OOM旧猜测撤回。旧被拒安全/异常NSO和Codex UI流程不重试绕过。
用户授权并行、本地开发/提交/原分支ff及设备诊断；不push/发布/购买/root/刷机/清数据、获取新游戏/密钥、停止或消息其他工具。当前无待批/不确定设备动作，无持久goal工具；不伪称后台引擎。

**接手句：** 读AGENTS和本入口，实核HEAD/WIP/lease；原startup+OS析构现RG0，CRT515仍未过；复用已通过HOST消费者、现有GP21运输，继续真实515 DSO/TLS/TP，沿真实CRT/TP→图形→可玩推进，保留成果，不把诊断或合同当游戏。

# 当前接力证据索引

最后核验：2026-10-23T13:56:56.571016+00:00。只读整理，未重跑构建/游戏/设备；[单入口](../../coordination/HANDOFF.md)与[state](../../coordination/state.json)记录当前版本，Git历史及下列原始日志保留旧阶段，不拼接为同次PASS。

## 当前版本与门槛

| 范围 | 版本/入口 | 结果与限度 |
|---|---|---|
| integration | task/codex-integration / 92fd886e4338cb3b02a717dbaa991452489f7130 | 实读HEAD；LAS81/3db/ff964e9/b197/0708已集成，后续包含本文的SHA须实读 |
| 原交付 | work/rg477m-native / f9284ea44f10a0d3a838c554508a3e911635b49f | 本轮只读clean，明确许可后safeff成功，无push/当前审批block |
| 新组合构建 | [manifest](../../.local/runtime/integrated-92fd886/manifest.json) | 构建进程0；新组合RG原码0,0,0,0,133,133/no timeout，两个startup仍69，不是完整启动 |
| 最近b197 RG | [math-runtime-v1证据目录](../../.local/device/rg477m-session1/) / [device report](codex_guest_process_device_report.md) | logf真实FP合同0；原startup空/非空均69 SetMemoryAllocatorForThreadLocal，allocmain+13830/freemain+13870，两真实map1286144+1048576，133/no timeout；未完整startup/CRT |
| 当前开发 | state精确5file TLSprovider +6file HOSTcallback lease | 两编码实际进行，未freeze；tls_research计划已交回等freeze，functional_review idle，DOC只四文件 |

组合92新RG：[execution](../../.local/device/rg477m-session1/integrated-runtime-v1-execution.json)；systemlogf76.093ms/native87.782ms/normal86.732ms/info225.082ms均0，startup空182.680ms/非空194.006ms均133/69，两真实map1286144+1048576。freeze SHA`45f0222985aeee189525b98dc1a74267c0b7ab0ce42f37eaa63dadc072bd0383`；CLI807776B/SHA8300d0a19ef7d02ad8083d8bf3999dcb8d43cc237ad947e0a792a15bd7f37388，normal977456B/SHA13f7dbc08031c98ef7ab3ce22b898f2105511f63185daab3b8113d97ec802d6e。0708 caller诊断已核，不影响控制流。DOC四文件现暂停交回root。

## 组件与原始命令入口

所有RG计划/执行文件在总控`.local/device/rg477m-session1/`；各prefix的`plan/stage/execution`及stdout/stderr保留精确argv、timeout、原始exit、来源/工件hash。读取匹配版本，不重放旧命令。

| 范围 | 清理报告 / 原始prefix | 有效覆盖 |
|---|---|---|
| math b197 | [独立FINAL](codex_math_runtime_review.md)、[原review](../../../functional-review/math-runtime/REVIEW.md) | review SHA`7b8f4a0dfb36401fecf4ecdb0667ea066ca6fb1883646406995c33abbc96d6d2`；typed libm/FP增量，非CRT；root device `math-runtime-v1-*` |
| ff964e9 61/67 | [独立FINAL](codex_startup_next_review.md)、`startup-next-v1-*` | 成熟strcasecmp及实际capability，原startup曾到153；后续b197已到69，旧153不是当前门槛 |
| 3db VA/locale | [独立review](codex_va_resource_review.md)、`va-resource-v1-*` | 真management arena/usage和Bionic locale；历史malloc139保留，不作为正常路径继续重试 |
| LAS81 | [LAS FINAL](codex_las_transfer_review.md)、`las-transfer-integrated-v2-*` | 19命令11×0/5×2/2×3/1×4符合预期；集成组合真实GPUtransfer0/113.853958ms、64KiB+256B零差异，shader/present/gamefalse；非新92组合结果 |
| owned argv9dc | [review](codex_guest_arguments_review.md)、`guest-arguments-v1-usb-ready-*` | 独立53命令符合预期；RG六合同/info0，历史空66/非空63均133；原main未改 |
| native/OS/pages/stack | [boundary review](codex_native_boundary_review.md)、[OS TLS review](codex_os_tls_review.md)、[pages review](codex_guest_pages_review.md)、[capacity review](codex_native_stack_capacity_review.md)、[GP21 review](codex_native_stack13_review.md) | 声明ABI/票据/lifecycle子集；GP21完整独立C交叉编译20s timeout保留，28额外C编译30s timeout不算通过；root设备与reviewer区分 |
| boot与malloc | [boot](codex_boot_order_reuse_report.md)、[malloc前置条件](codex_malloc_startup_prerequisite.md) | 原初始化先后与已知pre-init故障来源；不造对象/默认值或强行pthread旁路 |
| 现有APK/输入/音频/leaf | [Android](codex_android_report.md)、[device](codex_device_report.md) | v6 451092B/SHA4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a；leaf各16875零差异仅单函数；音频headadvance不证明听感/游戏音频 |
| 方法/成熟源码复用 | [method](codex_method_reuse_audit.md)、[locale](codex_locale_compare_report.md)、[LAS reuse](codex_las_implementation_review.md)、[route](codex_reuse_route_review.md) | REA Skill实际读取但63函数helper编译1/no timeout/nofunctionEvidence；LLVM与Bionic另归因。LAS MIT pinnedfe66 API/记录语义复用，不继承CPUmemmove假GPU或GetInfo常量0 |

自有9216B `boundary.nso` SHA`0214c62fdc6ffb00093ad7bce051955b1003ff1ad73abd10deb53f110d82278f`不含游戏字节；`native-profile-v2-contract-2.stdout`真实RG两次native调用0、raw_fallback=false、ended_session_rejected=true，只证明声明loader/import/ABI子集。游戏专用main SHA`6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`与SDK SHA`9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9`及偏移须逐版本核对，不能当任意游戏支持。

## 历史失败、工具与恢复

- 原分支ff曾被自动审批拒绝且未执行：[拒绝记录](../../.local/integration-approval/original-ff-f9284ea-rejection.json)。用户明确允许后唯一重试成功f9284ea clean，当前无pending/blocked；不重放已完成动作。
- 15e/e95 usable64752≠65536的RG合同1保留；d4容量修复先hold callback、真实join/unmap/refund后有界补admin，第二次raw69632/admin4096/native68848/logical65536。5c修旧2f retained映射丢handle，旧fixture1与修复retry0分别保留，见对应报告。
- mmap(0)无MAP_FIXED仅hint；tail0曾掩盖139；最长RELA候选不证明完整性；leaf和musl早return2不是CRT；94MB必OOM无证据论据撤回。代码量/合同数/面板Hz不计游戏完成率。
- Google官方部分sysroot逐成员长度/CRC验证，AppleClang/RustLLD真实ELF；完整NDK归档NOT_VERIFIED。CMake configure0/build2为Darwin ar/ranlib不能索引ELF，minimal成功不能改称完整NDK成功。
- 平台中止的旧NSO安全/异常QA不重试；`cua.getApp('Codex')`被明确DENIED，无截图/AX，UI可见性UNKNOWN，不绕过。历史ps127和10:40恢复快照不当当前进程证明。
- 环境中断后先只读查HEAD/index/WIP/日志再决定恢复，不重放uncertain mutation。当前无待审批或不确定写入；root唯一Git/ADB，其它Agent不stage/commit、选设备或启动VM。授权并行本地开发/提交/原分支ff；不push、清数据、root或获取新游戏/密钥。

[AGENTS](../../AGENTS.md)约束不变；STATUS仅`python3 scripts/project_status.py --write`生成。精确后续构建argv复用当前builder manifest，root核工件hash后设备复测；文档快照不会自动刷新或打开持久目标引擎。

## 69→106 与HOST callback最新冻结

- TLSprovider `../ffrs-tls-provider` / task/T02-tls-provider / cfc4df8921eaf911662ee6512c770a0683e787db clean；freeze `.local/tls-provider-v2/freeze.json` SHA45bd3c2707f11ff91fe13b5fa1ba893fa3d191a799057f5abe60537907a1dc75，112步骤预期/无timeout。root `tls-provider-v2-{plan,stage,execution}.json` 十例，raw0,0,134,134,0,0,0,0,133,133；原startup到106/+18d68，是否entry返回后的TLS析构待组合flush确认。fresh API P2独立证据 `../functional-review/tls-provider/logs/fresh-process-registration/`，修复另树ffrs-tls-provider-fix，不污染冻结cfc。
- HOSTcallback `../ffrs-native-host-callback` /434c4d13efcfd0e2ea20188bede031d6353e89ce clean；freeze `.local/native-host-callback/candidate1/freeze.json`，五源bundle c3cd96fb49d88415b612cb3a8ca17daa1900f43fca372a486a72c0640e965ab9。root `native-host-callback-v1-{plan,stage,execution}.json` normal0/265.251ms、cleanup0/72.840ms，noTimeout/stderr空；normalELF129064/B070e788...18d1，cleanup37032/4be7c5e6...3e2，完整SHA见plan。root已集成7f9c42e，独立review进行中。
- 既有原入口返回日志可能被缓冲；root7628947仅在成功call后fflush，严格syntax0 `.local/host-verification/entry-return-evidence/result.json`，尚未组合RG运行。不把stdout无result误称入口未返回；不以晚期清理断点冒充完整游戏成功。


最新组合585源/full113build0与真实RG11case见 `tls-combined-v1-*`；wrapper结果已在环境恢复后完整取回0。两startup返回日志0x92后再stop106（voidx0非状态）。freezeSHA1ab04cb38fece44c184be5382e31d646f46e59c2468cd4ea881600f4cc4e3d4f。完整SHA/精确argv原码在plan/execution。
独立FINAL已归档：codex_native_host_callback_review.md SHAb2d6d291925067cb0f818c4e0e2e5d4e42eae285e06dd9766857ac53d013ad85；codex_tls_provider_review.md SHA4d4f41aa63865ec32976921846a07f49aca4cff6265a67dd140e451e642a9fee；codex_tls_provider_repair_review.md保持原samefixture复现和旧P2失败。Android执行一律root单独归因。当前唯一coding是ffrs-primary-identity/base585的106/110子集，其余已冻结。

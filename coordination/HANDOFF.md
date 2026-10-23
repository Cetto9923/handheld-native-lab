# 原生移植接力：已越TLS登记69，下一停点GetCurrentThread106

最后核验：2026-10-23T14:05:25.035213+00:00。先读`AGENTS.md`，再核Git与`state.json.active_writers`；本文不是后台刷新状态。

《最终幻想：共鸣》Switch版→RG477M Android ARM64真正原生可玩。荣耀GT Pro附加目标等比例缩放/必要留边，不拉伸裁UI。完整startup、CRT、标题、可操作场景均未通过，诊断APK不是游戏。无可靠完成百分比；无CreateGoal/UpdateGoal持久入口，仅当前回合按目标执行。

## 当前检查点

**最新组合真机（优先于历史）：** 源585e3e1，113步骤构建0/无timeout；RG11例原码0,0,0,134,134,0,0,0,0,133,133，wrapper0，main hash不变。空/非空原startup均实际返回（stdout已flush `result0x92`，void函数无返回状态含义），随后OSslot0原析构18c60在GetCurrentThread106/+18d68停133。未完成正常OS清理、全CRT/ELFTLS/五参SDK Start/图形标题与游戏。新身份接口106→110由runtime在隔离树编码，110为64bit真实TID。

cfc freshAPI P2已80d4cfb最小修复，并由同一独立复现-11→0闭合，原始失败保留。434 HOSTcallback原RG normal/cleanup0且独立FINAL无需修复，现已含在组合源码。没有待批或未确认设备操作；瞬时环境断线后只读取回结果，没有重放。

**路径约定：** 下列`.local/`原始证据均相对总控绝对根 `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`；`../ffrs-*`相对该树父目录。原交付只同步Git自有源码/文档，不复制大型原始证据。
- integration：`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`，`task/codex-integration`，本轮源码HEAD **585e3e1445a3ebfa68e6fad7d86487b2eaf34a55**。已集成LAS81、3db、ff964e9、b197、0708诊断、762返回flush及434原生HOST回调；包含本文档的后续提交SHA须用Git实读，不猜自指SHA。
- 原交付：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，`work/rg477m-native`，实读 **f9284ea44f10a0d3a838c554508a3e911635b49f / clean**。用户明确允许ff后唯一重试成功，无push、无待审批；新检查点由root串行保存/安全ff。
- 组合构建：`.local/runtime/integrated-92fd886/manifest.json`，构建进程exit0；新组合RG六例原码 **0,0,0,0,133,133**、均无timeout，两个原startup仍69，完整启动未过。`integrated-runtime-v1-{plan,stage,execution}.json`保留各case，caller_address/outside_module=true已核，未伪造guest offset。
- 最近已执行b197：真实logf FP合同0；原`nninitStartup`空/非空均到`SetMemoryAllocatorForThreadLocal`69，alloc=`main+0x13830`、free=`main+0x13870`，两真实映射1286144+1048576，严格exit133、无timeout。原模块hash保持，完整初始化未完成。

[启动真机报告](../docs/analysis/codex_guest_process_device_report.md) · [阶段状态](../docs/analysis/codex_stage_status.md) · [精确版本/命令/失败索引](../docs/analysis/codex_handoff_evidence_index.md)

## 实际写者与恢复点

| 写者 | 独立树/分支/范围 | 当前状态 |
|---|---|---|
| `/root/runtime` | `../ffrs-tls-provider` / `task/T02-tls-provider` / baseb197，五文件Process c/h、contract/builder/report | 旧树cfc/80d冻结；当前../ffrs-primary-identity /task/T02-primary-identity/base585e3e1五文件实现106/110；全ELFTLS未接通 |
| `/root/vulkan` | `../ffrs-native-host-callback` / `task/T02-native-host-callback` / base7be，六文件Boundary c/h/S、contract/builder/report | 434c4d1冻结/实际RG两合同0/集成7f9c42e，作者idle，独立FINAL通过（限定组件） |
| DOC | integration四文件：HANDOFF/state/生成STATUS/证据索引 | 四文件已暂停交回root串行checkpoint；无Git/设备操作 |

`tls_research`和`functional_review`已FINAL交回idle；`runtime`在隔离identity树编码，两者等待下一冻结才复测。冻结cfc/80d/434树不再写。root唯一ADB/Git/集成及其它报告写者。历史冻结树不覆盖；`.local`链接先核实目标，不reset/clean/stash或抢他人占用。

## 已有证据与复用边界

- **可复用的已声明子集**：NSO/重定位/记录导入重绑定、Context/stack/pages唯一票据、typed x18/SCS GP21/FP边界、32槽逻辑OS TLS、owned argv、真实VA管理arena及生产VkBuffer/LAS transfer。各自source freeze、独立合同与RG证据见索引；不推断任意游戏兼容。
- **本游戏专用**：main SHA`6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`、SDK白名单及nnMain/startup/69/CRT/worker栈写者偏移。自有9216B无游戏字节boundary fixture与leaf只证明各自子集，不算游戏启动。
- **仅研究或未实现**：完整ELF TLS/ThreadType/CRT、NVN shader/present/资源挂载及游戏输入音频存档性能。面板120Hz不是游戏FPS；荣耀平板未连接。
- LAS81独立FINAL且集成组合曾真实RG0：64KiB copy/256B clear零差异、31VkResults0/12destroy/3unmap。只有typed transfer，无shader/present/game。ff与math增量独立报告FINAL，Android归因root，不冒充独立设备QA。

## 下一步与命令入口

1. 新106/110身份子集完成→冻结→独立QA→root同RG复测原startup及真实slot0析构，继续已核验五参SDK Start/CRT路径。仅真实managed primary/TID、受保护opaque token，不猜ThreadType字段，不返回pthread_t。HOST callback已可用但Process HOST allocator消费者仍未绑定，按真实ELF消费者接通。
2. 复用已有[boot顺序](../docs/analysis/codex_boot_order_reuse_report.md)与[malloc前置条件](../docs/analysis/codex_malloc_startup_prerequisite.md)，不重跑已知pre-init malloc139，不补默认对象或写TP。
3. `python3 scripts/project_status.py`只读状态；DOC用`python3 scripts/project_status.py --write`生成STATUS。构建/运行精确argv、cwd、日志与exit以对应manifest和device plan为准，勿盲重放stage；ADB仅root。
4. 工具链为Google官方部分sysroot逐成员长度/CRC验证+AppleClang/RustLLD；**完整NDK归档NOT_VERIFIED**。CMake configure0/build2的Darwin ar/ranlib失败保留，不能声称完整NDK安装通过。

否定路线继续复用：mmap未MAP_FIXED不能推必须零址；tail0不能掩盖139；最长RELA候选不是完整性；leaf/musl早return2不是CRT；94MB必OOM旧猜测撤回。旧安全/异常QA及Codex UI被平台拒绝的流程不重试、不绕过。REA Skill使用与LLVM来源分开，63函数提取helper编译1、非timeout/无functionEvidence，不冒称成功反编译。

用户已授权并行开发、本地提交、原分支ff和设备诊断；不push/发布/购买/root/刷机/清数据，不获取新游戏、密钥或凭据，不停止/消息其他工具。当前无待审批或不确定写入。历史原分支ff拒绝及后续明确批准保留在索引。

**短接力：** 读AGENTS和本入口→实读HEAD/WIP/lease；585组合RG已明确原startup返回、清理停106，P2已独立闭合，434回调独立FINAL。runtime当前新identity树做106/110，root唯一Git/设备。先读已有日志，保留WIP，沿真实CRT/完整入口继续，不把合同/诊断画面算可玩。

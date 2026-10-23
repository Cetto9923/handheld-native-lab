# 原生移植接力：当前停在TLS分配器登记69

最后核验：2026-10-23T13:56:56.571016+00:00。先读`AGENTS.md`，再核Git与`state.json.active_writers`；本文不是后台刷新状态。

《最终幻想：共鸣》Switch版→RG477M Android ARM64真正原生可玩。荣耀GT Pro附加目标等比例缩放/必要留边，不拉伸裁UI。完整startup、CRT、标题、可操作场景均未通过，诊断APK不是游戏。无可靠完成百分比；无CreateGoal/UpdateGoal持久入口，仅当前回合按目标执行。

## 当前检查点

- integration：`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`，`task/codex-integration`，实读HEAD **92fd886e4338cb3b02a717dbaa991452489f7130**。已集成LAS81、3db、ff964e9、b197及0708诊断；包含本文档的后续提交SHA须用Git实读，不猜自指SHA。
- 原交付：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，`work/rg477m-native`，实读 **f9284ea44f10a0d3a838c554508a3e911635b49f / clean**。用户明确允许ff后唯一重试成功，无push、无待审批；新检查点由root串行保存/安全ff。
- 组合构建：`.local/runtime/integrated-92fd886/manifest.json`，构建进程exit0；新组合RG六例原码 **0,0,0,0,133,133**、均无timeout，两个原startup仍69，完整启动未过。`integrated-runtime-v1-{plan,stage,execution}.json`保留各case，caller_address/outside_module=true已核，未伪造guest offset。
- 最近已执行b197：真实logf FP合同0；原`nninitStartup`空/非空均到`SetMemoryAllocatorForThreadLocal`69，alloc=`main+0x13830`、free=`main+0x13870`，两真实映射1286144+1048576，严格exit133、无timeout。原模块hash保持，完整初始化未完成。

[启动真机报告](../docs/analysis/codex_guest_process_device_report.md) · [阶段状态](../docs/analysis/codex_stage_status.md) · [精确版本/命令/失败索引](../docs/analysis/codex_handoff_evidence_index.md)

## 实际写者与恢复点

| 写者 | 独立树/分支/范围 | 当前状态 |
|---|---|---|
| `/root/runtime` | `../ffrs-tls-provider` / `task/T02-tls-provider` / baseb197，五文件Process c/h、contract/builder/report | TLSallocator一次登记、IDLE typed alloc/free、live持有，build中；未freeze/未验收 |
| `/root/vulkan` | `../ffrs-native-host-callback` / `task/T02-native-host-callback` / base7be，六文件Boundary c/h/S、contract/builder/report | `nb_call_from_host`有界4层、真实host SCS/x18恢复；未freeze/未验收 |
| DOC | integration四文件：HANDOFF/state/生成STATUS/证据索引 | 四文件已暂停交回root串行checkpoint；无Git/设备操作 |

`/root/tls_research`仅完成预冻结QA计划，等待freeze，**没有正在测试**；`functional_review`idle。root唯一ADB/Git/集成及其它报告写者。历史冻结树不覆盖；`.local`链接先核实目标，不reset/clean/stash或抢他人占用。

## 已有证据与复用边界

- **可复用的已声明子集**：NSO/重定位/记录导入重绑定、Context/stack/pages唯一票据、typed x18/SCS GP21/FP边界、32槽逻辑OS TLS、owned argv、真实VA管理arena及生产VkBuffer/LAS transfer。各自source freeze、独立合同与RG证据见索引；不推断任意游戏兼容。
- **本游戏专用**：main SHA`6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`、SDK白名单及nnMain/startup/69/CRT/worker栈写者偏移。自有9216B无游戏字节boundary fixture与leaf只证明各自子集，不算游戏启动。
- **仅研究或未实现**：完整ELF TLS/ThreadType/CRT、NVN shader/present/资源挂载及游戏输入音频存档性能。面板120Hz不是游戏FPS；荣耀平板未连接。
- LAS81独立FINAL且集成组合曾真实RG0：64KiB copy/256B clear零差异、31VkResults0/12destroy/3unmap。只有typed transfer，无shader/present/game。ff与math增量独立报告FINAL，Android归因root，不冒充独立设备QA。

## 下一步与命令入口

1. 组合92同RG验证已结束且仍69；原码与旧版本分别保留。当前两编码完成→freeze→独立QA→root复测原startup69，继续真实CRT/标题/场景。
2. 复用已有[boot顺序](../docs/analysis/codex_boot_order_reuse_report.md)与[malloc前置条件](../docs/analysis/codex_malloc_startup_prerequisite.md)，不重跑已知pre-init malloc139，不补默认对象或写TP。
3. `python3 scripts/project_status.py`只读状态；DOC用`python3 scripts/project_status.py --write`生成STATUS。构建/运行精确argv、cwd、日志与exit以对应manifest和device plan为准，勿盲重放stage；ADB仅root。
4. 工具链为Google官方部分sysroot逐成员长度/CRC验证+AppleClang/RustLLD；**完整NDK归档NOT_VERIFIED**。CMake configure0/build2的Darwin ar/ranlib失败保留，不能声称完整NDK安装通过。

否定路线继续复用：mmap未MAP_FIXED不能推必须零址；tail0不能掩盖139；最长RELA候选不是完整性；leaf/musl早return2不是CRT；94MB必OOM旧猜测撤回。旧安全/异常QA及Codex UI被平台拒绝的流程不重试、不绕过。REA Skill使用与LLVM来源分开，63函数提取helper编译1、非timeout/无functionEvidence，不冒称成功反编译。

用户已授权并行开发、本地提交、原分支ff和设备诊断；不push/发布/购买/root/刷机/清数据，不获取新游戏、密钥或凭据，不停止/消息其他工具。当前无待审批或不确定写入。历史原分支ff拒绝及后续明确批准保留在索引。

**短接力：** 读AGENTS→本入口→实读HEAD/WIP/lease，当前原startup69而非旧66/63/153；92组合build0且RG0,0,0,0,133,133仍69，runtime TLSprovider5文件与native HOSTcallback6文件在开发，QA等freeze。root唯一Git/设备，保留所有WIP，沿真实启动继续，禁止以合同或诊断画面冒充可玩。

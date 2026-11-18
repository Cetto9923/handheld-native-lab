# Mali 管线编译串行化独立审查

任务 `CODEX-MALI-COMPILE-REVIEW-20261118`，执行者 `/root/battle_performance_fix`，2026-11-18。

## 输入与结论

审查源码为根 HEAD `eb6b7ad9b73ed189ff34cf2901eee8bce805d15d`。`xbg_game.cpp` SHA256 为 `a6f9f5e2bc5c57c50aa8cfe3fe5c3c5cdca38974f1101edd4912a3e88de55a57`，与该提交字节一致。只读源码和原始诊断包，仅新增本报告及 `.local/codex-mali-compile-review-20261118/` 中的审查脚本与索引。

限定 Arm/Mali 的管线创建域串行化具有实验依据。现有代码允许多个预热线程及运行时同时进入驱动，其他项目也报告过降低编译并发后存活时间延长。本机原事故的末次会话却在故障前已记录预热线程归零，故障API与TID也缺失，当前无法把串行化认作根因修复。建议保留为可关闭的独立诊断候选，继续用相同APK、相同缓存输入和同原机流程建立因果。

## 原始日志的会话边界与反证

`~/.cetto-arsenal-inputs/crash73-new/report.zip` SHA256 为 `a1c6f559111f52defdce905edaf7df5c6e8a2048687586892248e69a98e5abd4`。本轮验证 ZIP 内两份日志与旁边的 engine.txt/guest_boot.txt 逐字一致。engine.txt 完整收集，guest_boot.txt 按 collection.txt 采集前65536B与最后2097152B，中间缺口必须保留。

engine.txt 实际串接三次初始化，前两段结尾存在半行。按 completion_wait 初始化和 CODEX_FRAME 文件中的PID划分如下。

| engine.txt 行号 | PID | 对应退出 |
| --- | --- | --- |
| 1–1960 | 19877 | exit-history 为139 |
| 1961–2518 | 23917 | force-stop，status0 |
| 2519–4416 | 24477 | exit-history 为139 |

诊断采集PID31937属于导出进程。以下末次事故分析仅使用24477块。guest尾部四条独特记录与该块逐字匹配，包括memory reclaim的具体image数量和一条shader monotonic区间，检查脚本会拒绝重复或跨块匹配。日志索引保存为 audit.json。

| 末次会话证据 | 含义 |
| --- | --- |
| engine2575 | `background builds for skippable draws=0`，async路线关闭 |
| engine3097 / guest15286 | 首次回收时 `warm_workers=2`，此前预热确实活动 |
| engine3303 / guest16124 | level2回收时 `warm_workers=0 warm_ready=0` |
| engine4084 / guest20667 | 再次回收仍 `warm_workers=0 warm_ready=0` |
| engine4246 / guest21411 | 最后一次回收仍 `warm_workers=0 warm_ready=0` |
| engine3194起至4027多个PERF窗口 | `warm_built=199 warm_failed=0 warm_ms=11388` 固定 |
| guest21654–21656 | CS073ce module载入已返回成功，随后vendor SIGSEGV，缺故障API和TID |

当前实现的 trim_for_memory_pressure 将memory_constrained置true，worker及async_worker返回，async_request拒绝新请求。仅start将其恢复false。最后会话块中没有后续初始化或重启预热记录。active_workers覆盖worker整个生命周期，归零发生在退出其驱动调用之后。结合现有代码和同会话记录，应用层warm编译仍在末次故障处并发工作的解释缺乏支持。原73精确源码与驱动内部异步工作仍有取证边界，早期编译造成后效应的假说也需要额外证据。

最后pipeline-cache保存成功记录为engine3901，随后还有runtime创建与两个workers0回收点。现有保存日志无begin/TID，无法彻底排除cache saver在其他未记录时段与runtime驱动操作重叠，也没有末次故障时重叠的正证据。

## 当前生产并发与完整入口

xbg_game.cpp147的recursive engine mutex串行化普通渲染提交及大部分辅助创建。warm线程不持该锁。warm::mu只保护容器查找和发布，build_compute4236及build4274进入Vulkan时没有持该锁，因此它没有串行化管线编译。

warm::start4447启动 `min(3,max(1,hardware_concurrency()/3))` 个worker。启用async后最多再有一个async_worker，runtime仍可同步创建，应用层理论上可有5个管线创建调用在途。原G925末次会话观测到过2个warm worker且async关闭。budget_per_frame是每个worker自己的额度，nice和interval也不能保证API互斥。只把warm数量设1，仍允许warm/runtime以及async/runtime同时创建。

以下为src全部13处直接调用。当前游戏引擎9处，其余是独立renderer或诊断程序。清单由audit.py扫描保存，包含缓存重试的第二次调用。

| 文件及行号 | 路径 | 候选覆盖 |
| --- | --- | --- |
| xbg_game.cpp4236 | warm compute | 共享编译gate |
| xbg_game.cpp4274 | warm graphics | 同一gate |
| xbg_game.cpp4472、4479 | runtime graphics resilient首次及空cache重试 | 两次均经过gate |
| xbg_game.cpp4484、4491 | runtime compute resilient首次及空cache重试 | 两次均经过gate |
| xbg_game.cpp4615 | RG16 proxy compute | 同一gate |
| xbg_game.cpp6157 | bright compute | 同一gate |
| xbg_game.cpp7913 | RG16 GPU contract的sample compute | 同一gate，便于覆盖验证 |
| xbg_vulkan.cpp339 | xbg_vk_create的真实2D renderer graphics，空cache | 作为进程级编译域时也需同一gate |
| vulkan_shader_probe.c974、1032 | 独立shader probe compute/graphics | 独立诊断可执行程序，分别声明是否覆盖 |
| vulkan_graphics_bisect.c1336 | 独立bisect graphics | 同上 |

xbg_game.cpp的runtime lookup通过两个resilient helper创建，当前helper对任何失败且cache非空都重试。若加gate，本轮应保持该路径的返回值、部分handle处置和重试次数，错误重试策略另有独立审查范围。仅修改这两个helper会遗漏warm与辅助compute。VkShaderModule、layout、renderpass创建以及cache saver不属于仅管线创建gate的覆盖，当前目标module已成功创建这条事实可以用于下一轮定位。

## 锁范围与死锁审查

最小可审查实现为一把进程级编译mutex，按已初始化的实际properties.vendorID==0x13b5启用，必要的名称fallback明确记录。启用状态在warm启动前固定，停用需等待所有worker和runtime退出。Arm设备之间的适用性与driver版本效果逐机记录。

锁仅包围直接vkCreateGraphicsPipelines/vkCreateComputePipelines调用，驱动返回立即释放。锁内不做shader改写、warm::mu查找/发布、engine mutex获取、warm::record、文件日志、等待fence、join、CV等待或设备销毁。gfx创建持engine锁后等待编译gate，warm创建拿gate时不持warm::mu且释放后才发布，所以该限定范围没有发现反向锁边。现有allocation callbacks均nullptr。debug/validation回调及未来自定义allocator仍需遵守不反入此gate或engine等待的规则。

以下两种扩大范围会引入具体风险。

- runtime拿engine锁等待gate，warm若持gate再取engine锁，会形成ABBA。给warm整段build加锁再复用任何获取engine锁的代码，需重新审计。
- shutdown当前持engine锁调用cache_save_join和warm::shutdown。若shutdown持gate等待join，而worker/saver尚等待gate，会直接死锁。trim、join、queue idle与销毁均放在gate外。

若wrapper内部自动获取gate，调用者不要再次加同一非递归锁。graphics和compute必须共享一个实例，两个wrapper各自static mutex或头文件每TU一份static mutex均不满足互斥目标。gate应位于两个renderer能复用的拥有明确链接身份的位置。单个VkDevice mutex是否足够取决于驱动内部状态，进程级gate能覆盖同进程多个device的应用层创建并发。

cache saver的vkGetPipelineCacheData需要独立判断。当前cache创建flags=0。Vulkan只在cache显式设置EXTERNALLY_SYNCHRONIZED标志时要求这些create API由应用对cache外部同步，所以多线程共用当前cache本身不足以证明规范违规。若试验范围扩为cache操作域，先取gate完成两次vkGetPipelineCacheData再释放，磁盘I/O、日志和join继续放在外面，且报告需明确试验扩大了覆盖。[graphics创建规范](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateGraphicsPipelines.html) [compute创建规范](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateComputePipelines.html)

## 外部案例的适用边界

ScarletNX #2127原文为Poco X6 Pro的Mali-G615 MC6、r44p1，在六worker重建时早崩。2026-11-13评论称build2ec5e70设pipeline_threads=1后一次运行207秒、生成50条pipeline且无崩溃，作者同时要求two-worker实验区分偶然性。该单次结果支持开展并发假说实验。其shader、驱动及代码与本项目各有身份，当前未取得本G925相同结果。[ScarletNX #2127](https://github.com/scarletnx/ScarletNX/issues/2127)

MaliBridge #78为Immortalis-G925 MC12、r49p1-03bet0，两个固定compute shader在7次启动中触发驱动编译崩溃，帖子仍开放。它说明同家族驱动存在特定shader编译失败案例，未给出串行化修复对照。本机报告只提供driver数值0xc401000，不能据此直接赋予r49p1字符串身份。[MaliBridge #78](https://github.com/ASEbrahim/MaliBridge/issues/78)

上游本轮均可访问，只引用报告内容，没有复制实现。ScarletNX评论指向的build短revision为2ec5e70，未据短revision复建其代码或建立本项目行为等价。源码许可证不作为实现复用输入。

## 最小测试与交接

1. 将candidate置于独立源码，先用有状态假Vulkan同时阻塞graphics与compute，覆盖warm/runtime/proxy/bright及resilient第二次调用，启用时max_inflight必须1，停用负对照必须观测大于1。对厂商gate各跑Arm和非Arm，核对输出handle、错误码、调用数和重试顺序完全保持。
2. 并行启动warm创建与runtime，并分别触发trim和shutdown。用有界超时验证worker可完成、gate在发布warm::mu前释放、join时没有持gate。静态入口扫描必须拒绝新增未包装生产调用，整CPP按真正engine target flags语法验证。
3. 复用已有api-breadcrumb先取得原G925同session的故障API/TID及目标最终module身份。现有breadcrumb仅目标compute有warm/runtime pipeline收据，graphics不在其记录范围。若要证明全编译域inflight，应额外有明确预算的聚合计数或测试桩，不能从一对目标收据推断全域互斥。
4. 原G925同APK开关两臂，用相同隔离存档和同字节输入cache/recipe分别记录冷、暖，前后截图核实实际故障流程，保留正常cache输出而用下一轮新副本还原输入。每臂多次复跑，记录API与线程、inflight、warm worker状态、创建/等待时间及生存时间。停用臂能复现且启用臂持续通过才形成支持，单次不崩仍保留时长和概率边界。

本轮实际执行仅为只读审查、ZIP字节一致核验、会话分块和四条跨日志匹配、入口扫描。完整APK、GPU驱动执行及原机串行化对照均NOT_RUN。没有ADB操作，没有修改信号链或共享源码。audit.py、audit.json及source-metadata.json在本任务.local目录。恢复核验命令为 `python3 .local/codex-mali-compile-review-20261118/audit.py`，总控负责选择是否实现独立候选并更新状态。

# Codex 原生移植接力：尚未完成游戏启动

## 当前事实与授权

目标《最终幻想：共鸣 / FINAL FANTASY RESONANCE》，RG477M Android ARM64原生运行；荣耀平板GT Pro为附加目标，按源画幅等比例缩放/必要留边，不拉伸或裁切UI。用户已直接授权本地开发、并行子代理、本地提交，已连接掌机并两次亲自操作；不推远端或发布，不购买、刷机、root、清数据或读取凭据。不向Claude/Antigravity发消息、不停止他人任务，不reset/clean/stash他人成果。

用户说掌机页面“一串代码”后，实际截屏核对为正常诊断JSON，标题明确Game startup and rendering: NOT_RUN。不是崩溃，也不是游戏画面，无需用户手工输入代码。

## 分支 / 工作树

- 原交付：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，`work/rg477m-native`，本检查点已干净快进至 **253b824180667fa31ce8bdc08e94e95ebf7d4c91**。后续快进前再核实branch/HEAD/dirty，禁止覆盖新成果。
- 总控：`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`，`task/codex-integration`。基线HEAD d9a1667；原有coordination/device/context/stage报告WIP保留。用户已授权DOC-HANDOFF-LIVE暂代coordination唯一写者；总控仍唯一stage/commit/集成和设备操作者。active_writers以state.json为准，不按过期时间抢占。
- Runtime：`../ffrs-runtime`，`task/T02-codex-runtime`，HEAD e846dc174edcb687c4e075288326c62ab10ac86e；GuestContext冻结并经独立review通过。T02-OWNED-STARTUP-PROFILE正编码，已观察loader.c/h有WIP；完整独占范围见state，未冻结、不可覆盖。
- Android：`../ffrs-android`，`task/T03A-codex-android`，冻结15da4c2，v6音频及EH链接已集成fb7d870。
- Vulkan：`../ffrs-vulkan`，`task/T03B-vulkan-ops`，2ca5d35，源f7ac445已集成4695bca，报告集成c8874f9。
- 独立普通功能审查：`../functional-review/`；GuestContext FINAL PASS已释放旧lease，新startup-profile先只读设计预审、实现冻结前不执行。静态TLS/图形底座评估：`../tls-research/`，底座评估已完成并释放lease；只写各自证据。旧QA异常NSO流程被平台安全检查中止，不重试，其WIP保留，不冒充完整QA。

实际工作模型GPT-6.1-sol/xhigh。环境没有CreateGoal/UpdateGoal入口，未开启持久目标模式；本回合主动推进，不承诺后台自动运行。用户最新明确要求文档与开发/审查并行，10:38UTC实际runtime/doc两路running，QA预审与图形评估已交回；文档交此检查点后暂停供总控提交/ff，待通知恢复。lease不等于进程，重型构建仍串行。

各树.local独立，仅private uncompressed/exefs为原素材链接。保留原交付开发签名，不读取或复制私钥。原交接历史曾含敏感内容，当前文本已清理；未来上传需另做历史审查，当前禁止push。

## 最新实证（2026-10-23）

本检查点与可复制命令/旧失败否定路径先看 [接力证据索引](../docs/analysis/codex_handoff_evidence_index.md)；文档按事件更新，没有后台自动刷新。详见 [真机报告](../docs/analysis/codex_device_report.md)、[阶段验收](../docs/analysis/codex_stage_status.md)。所有原始真机证据在总控 `.local/device/rg477m-session1/`。

- 真实RG477M Android14/API34，MT6897/ARM64/nativebridge0/4KiB。窗口1280×960、内容1280×918、280dpi，当前120Hz是面板而非游戏帧率。
- canonical v6 APK已更新并冷启动195ms，旧事件前缀和leaf摘要保留。无申请权限，451092B，SHA256 **4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a**。路径原交付`.local/artifacts/android/ffrs-diagnostic-debug.apk`；clean sourcefb7d870，35构建步骤0、输入稳定。重新构建必须重新核对build-evidence及摘要。
- 原始main+0x694bd0 leaf44B，在Mac/ARM64 AVD/真实RG各16875比较0差异；仅单函数、无构造器，不是正式T02/full game。APK JNI只load/relocate125577976B主模块，651imports/4612RELA/1370974RELR，NULLresolver，未调用game entry。
- 同RG production HLE CLI info0；nnMain/malloc严格在QueryMemoryInfo +0x27a50/+0x1398c exit133；CRT __nnmusl_init_dso +0x5920a34/133；startup AllocateTlsSlot +0x133a0/133。无超时。不要用tail的0掩盖真实退出码，旧SIGSEGV139不是最新结论。
- C++guard/EH真实修复：a22d0dc含.eh_frame_hdr/PT_GNU_EH_FRAME，RG8线程80000调用/初始化1/abortwinner1/异常attempts2/errors0。未注册guest NSO unwind或完成CRT。
- Vulkan真实GPU操作exit0：64KiB buffer copy及8×8 RGBA clear/readback零差异、31VkResult0。Mali-G615 MC6/Vulkan1.3.247。没有shader、surface/present、descriptor indexing实际操作或NVN后端验收。
- owned-pages e112d65真机normal contract0：4KiB、8线程×32周期、32release竞争，mapped0/validtrue/errors0。62GiB只虚拟预留，实际只映射4096B。独立主机rollback/quarantine审查通过；真机未故障注入。组件未绑定SDK，mapped不是physical resident budget。
- 实体方向/B/左杆已记录；补测右Z/RZ均[-1,1]、L2/R2数字键8/10事件，模拟触发轴仍0。v5公开InputDevice枚举识别retrogame_joypad，旧raw-node误报修复。不代表游戏映射。
- v6显式音频0.25秒/PCM16peak327：实测48000Hz/256frames，扬声器route，12000frames写入/播放推进、246ms、stop/release正常，系统音量未改。用户“刚才没注意”，听感未确认，不记游戏音频PASS。
- fb7d870主机23集成检查均符合真实预期（覆盖exit3、normalexit23、_Exit24，其余0），原始输入前后hash一致。独立普通组件review见docs/analysis/codex_*_review.md，不能替代完整QA。

## 核心阻塞及下一步

1. **真正GuestContext和QueryMemoryInfo**：SDK七字段是total/used non-system memory、heap capacity、heap allocated、baseline、registered stack bytes和thread-object count，不是地址查询。不能把宿主MemAvailable/meminfo/pthread栈大小填成guest值。main映像port逻辑span4KiB=162869248B，文件size不是映像计量；NPDM主栈明确配置65536B，SystemResourceSize20MiB只是内核管理保留、不是guest总额；见codex_npdm_memory_report.md。e846dc1已实现必填quota/stack_size、precharge与真实专属pthread栈/guard，普通callback主机+RG验证通过（SP/guard/TLS/join后清账），不绑定Query或执行原始入口。GuestContext独立普通主机review FINAL PASS（production/recovery/校正补测编译0/运行0，非游戏验收）。现正实现封闭startup profile与loader owner事务：mmap前precharge、失败真实保留/重试、munmap成功后退款，避免现有void free忽略错误；新Query七字段与原nnMain门槛尚未冻结/验证。QA设计预审a5de2bd3要求invalid Query必须fatal且不写output、validity/ownership同一串行快照；已发作者。module/heap/thread未知保持invalid；62GiB虚拟reserve不算physical。设计来源在runtime.local/guest-context-plan/。
2. **TLS/CRT**：main模块TLS镜像initialized0x220/total0x1019/align8/prefix16，独立复制/清零/对齐正常，不代表header/指针生命周期兼容。SDK OS32槽与ELF destructor LIFO是不同机制；应用槽0–15、SDK31–17、16未知；OS最多5轮且callback后清值。ELF节点48B/align16，可重入且无固定轮数。main303e0释放依赖guest allocator虚表，不能hostfree。静态报告在docs/analysis/codex_tls_sdk_lifecycle_report.md。可空GLOBAL OBJECT hook安装99ba0→9ae40、fini分支99ea0清零已闭合；普通线程返回依次OS槽→结束状态/通知→hook→ELF drain（内部门控非零条件），异常及初始线程退出不外推，详见codex_tls_exit_hook_report.md。不要执行写TPIDR的原SDK helper、批量改MRS或强制CRTgate。
3. **真实游戏图形/资源**：NVN仅2/535查询入口，533未实现，59缺PFN类型；当前GPU探针不等于翻译后端。Strato GPU/Hades评估已结束：有真实代码但无直接NVN ABI桥，需图形负责人定生产接入边界；整游戏可行性仍未知。按启动真实调用链补接口，不能批量假桩或继续用新probe替代后端。合法本地素材保留，实际挂载/IoStore/着色器/呈现/标题/场景都未验收。
4. **设备余项**：听感、完整键轴/休眠、真实游戏输入音频存档；30FPS实际场景/45分钟未运行。荣耀平板未连接，官方profile3000×1920/8sGen3/144Hz仅规格，不是adb身份或GPU兼容证据。

## 恢复与构建

先只读核对工作树/HEAD/WIP/agents/进程；当前总控报告无ADB长命令、未决审批或不确定写入。文档代理的旧AVD PID只读ps被沙箱拒绝(exit127)，不宣称历史PID存活。exec连接中断时不要重放不确定写入。原有AVD多次随执行环境中断消失，启动前核对adb与进程名，不wipe、不重复实例。RG已连接时无需为了探针再开AVD。

```sh
python3 scripts/project_status.py
python3 scripts/verify_host.py --runtime-contracts --game-leaf --output .local/host-verification/NEW-CHECKPOINT
FFRS_USE_MINIMAL_TOOLCHAIN=1 scripts/device/build_diagnostic_apk.sh
FFRS_USE_MINIMAL_TOOLCHAIN=1 scripts/device/build_runtime_hle.sh
```

使用新日志目录并保存原始exit。部分Google官方r27d sysroot每成员长度/CRC验证，AppleClang21/RustLLD交叉链接；完整NDK归档校验NOT_VERIFIED，不能称完整官方工具链安装。CMake静态库曾被Darwin ar/ranlib阻塞，minimal链实际成功；不要将交叉configure0写成build通过。

设备操作用显式序列号（保存在本地证据），先身份核验，安装前记录包名/权限/hash/签名；只访问本项目应用/新shell诊断目录。当前shell目录`/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259`保留，不清理。adb exec-out远端错误可能仍return0，须解析JSON、长度和摘要。不要读取私钥、别的应用或系统凭据。

下一优先级是冻结并独立审查startup profile，让真实入口跨过Query后记录下一个caller/真实exit，再沿CRT/TLS和NVN生产路径推进。最近16提交用途粗分5启动链/5诊断/6分析，不是工时或完成率；停止新增外围probe。

> 短接力提示：先读本文件、state.json和证据索引，再只读核对各树HEAD/dirty及active_writers。保留所有WIP；只接明确移交的scope。总控唯一设备/集成/提交，DOC-HANDOFF-LIVE独占四文档；runtime profile未冻结、QA先预审。真实Query/CRT/TLS/NVN优先，不把APK、leaf或用例/行数当游戏成功；无push、root、清数据、密钥或新游戏下载。

主界面只读核验`cua.getApp('Codex')`被CUA安全规则DENIED，无截图/AX、不重试绕过，不阻开发且不是Git待审批。内部`/root/...`仅协作标识，未创建四个sidebar任务，UI可见性UNKNOWN。

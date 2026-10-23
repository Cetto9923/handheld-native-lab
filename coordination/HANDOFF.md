# Codex 原生移植接力：尚未完成游戏启动

## 当前事实与授权

目标《最终幻想：共鸣 / FINAL FANTASY RESONANCE》，RG477M Android ARM64原生运行；荣耀平板GT Pro为附加目标，按源画幅等比例缩放/必要留边，不拉伸或裁切UI。用户已直接授权本地开发、并行子代理、本地提交，已连接掌机并两次亲自操作；不推远端或发布，不购买、刷机、root、清数据或读取凭据。不向Claude/Antigravity发消息、不停止他人任务，不reset/clean/stash他人成果。

用户说掌机页面“一串代码”后，实际截屏核对为正常诊断JSON，标题明确Game startup and rendering: NOT_RUN。不是崩溃，也不是游戏画面，无需用户手工输入代码。

## 分支 / 工作树

- 原交付：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，`work/rg477m-native`，最后核验干净HEAD **2e444a16a1d80fe3ff1407e69eb5ba554490b528**（目标文档检查点2e444a1）。后续快进前再核实branch/HEAD/dirty，禁止覆盖新成果。
- 总控：`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration`，`task/codex-integration`。最后核验HEAD **999b5494c6f84d02758dc845ad727a8cf280212b**（known_at 2026-10-23T11:10:02.051068+00:00；三报告已提交，容量源码c5bbddb；后续文档commit会改变HEAD）；原有WIP已由总控提交，此前四文档检查点4e90347、三份root报告999b549均已提交；当前仅四文档事件WIP，文档代理现暂停供总控串行提交。收到恢复通知后继续维护；runtime/pages/QA继续。用户已授权DOC-HANDOFF-LIVE暂代coordination唯一写者；总控仍唯一stage/commit/集成和设备操作者。active_writers以state.json为准，不按过期时间抢占。
- Runtime：`../ffrs-runtime` / `task/T02-codex-runtime`，当前clean freeze **d4ded908096feef6fff2876ab2ffdaa9e48303b5**（最后核验2026-10-23T11:07:57.118071+00:00，9文件）；同RG容量/Query gate已过，已集成c5bbddb，独立新freeze QA进行中。作者等待typed pages API后只接profile wrappers/Query；此前15e/e95失败完整保留。
- Android：`../ffrs-android`，`task/T03A-codex-android`，冻结15da4c2，v6音频及EH链接已集成fb7d870。
- Vulkan：`../ffrs-vulkan`，`task/T03B-vulkan-ops`，新production VkBuffer冻结 **fa76ca979d711c68beebe9e6ae699914f8a1cdf0**、最后核验clean；旧源f7ac445已集成4695bca，新组件已集成34dd0c8且同RG contract/validator均0；仅transfer范围通过。
- Pages：`../ffrs-pages` / `task/T02-guest-pages`，base **d4ded908096feef6fff2876ab2ffdaa9e48303b5**；隔离树已实际创建并触发`/root/tls_research`编码，lease ACTIVE/RUNNING，精确四新文件见state。仅typed内部page owner/noresolver/noADB；runtime后续只接guest_process wrappers/Query，单一VA/page authority。
- 独立普通功能审查：`../functional-review/`；GuestContext FINAL PASS已释放旧lease，新startup-profile已接收freeze15e498c开始独立普通源码/host contract审查，旧15e已交回host路径通过/宏关闭P2/RG失败，等新freeze复核。fa76ca9 buffer_resource.c/h独立review已FINAL接受组件：精确8文件，40普通mock+2ownership+自编两种各7组PASS，ASan/UBSan无误；reviewer非新组件作者、未ADB，范围不含游戏图形。静态TLS/图形底座评估：`../tls-research/`，底座评估已完成并释放lease；只写各自证据。旧QA异常NSO流程被平台安全检查中止，不重试，其WIP保留，不冒充完整QA。

实际工作模型GPT-6.1-sol/xhigh。环境没有CreateGoal/UpdateGoal入口，未开启持久目标模式；本回合主动推进，不承诺后台自动运行。用户最新明确要求文档与开发/审查并行，第一检查点后文档已获准恢复；当前runtime d4ded9已clean并同RG通过容量/Query gate；typed pages隔离worker实际编码，独立QA复核新freeze，doc事件同步；GPU组件与独立review已通过其限定范围。lease不等于进程，重型构建仍串行。

各树.local独立，仅private uncompressed/exefs为原素材链接。保留原交付开发签名，不读取或复制私钥。原交接历史曾含敏感内容，当前文本已清理；未来上传需另做历史审查，当前禁止push。

## 最新实证（2026-10-23；版本与最后核验时间见证据索引）

本检查点与可复制命令/旧失败否定路径先看 [接力证据索引](../docs/analysis/codex_handoff_evidence_index.md)；文档按事件更新，没有后台自动刷新。详见 [真机报告](../docs/analysis/codex_device_report.md)、[新启动RG实证](../docs/analysis/codex_guest_process_device_report.md)、[阶段验收](../docs/analysis/codex_stage_status.md)；[旧15e独立review](../docs/analysis/codex_guest_process_review.md)与[VkBuffer FINAL review](../docs/analysis/codex_vulkan_buffer_review.md)版本分开。所有原始真机证据在总控 `.local/device/rg477m-session1/`。

- 真实RG477M Android14/API34，MT6897/ARM64/nativebridge0/4KiB。窗口1280×960、内容1280×918、280dpi，当前120Hz是面板而非游戏帧率。
- canonical v6 APK已更新并冷启动195ms，旧事件前缀和leaf摘要保留。无申请权限，451092B，SHA256 **4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a**。路径原交付`.local/artifacts/android/ffrs-diagnostic-debug.apk`；clean sourcefb7d870，35构建步骤0、输入稳定。重新构建必须重新核对build-evidence及摘要。
- 原始main+0x694bd0 leaf44B，在Mac/ARM64 AVD/真实RG各16875比较0差异；仅单函数、无构造器，不是正式T02/full game。APK JNI只load/relocate125577976B主模块，651imports/4612RELA/1370974RELR，NULLresolver，未调用game entry。
- **最新RG原始入口突破**：d4ded9的Context/contract/info真实0；原nnMain Query+0x27a50→AllocateMemoryPages71/+0x27f90/133，malloc→VAusage66/+0x144b8/133，均无timeout；原main前后hash保持。实测attempt1 usable64752暂不callback，attempt2 raw69632/admin4096/native68848/logical65536才ready。独立QA进行中，不是TLS/CRT或游戏通过。
- **保留旧失败**：15e startup contract649672B实际1/42.27ms，e95诊断-only663040B实际1/36.30ms，唯一pthread usable64752 vs65536差784B；原断言未删，这些不是d4的新结果。
- 历史同RG production HLE CLI info0；nnMain/malloc严格在QueryMemoryInfo +0x27a50/+0x1398c exit133；CRT __nnmusl_init_dso +0x5920a34/133；startup AllocateTlsSlot +0x133a0/133。无超时。不要用tail的0掩盖真实退出码，旧SIGSEGV139不是最新结论。
- C++guard/EH真实修复：a22d0dc含.eh_frame_hdr/PT_GNU_EH_FRAME，RG8线程80000调用/初始化1/abortwinner1/异常attempts2/errors0。未注册guest NSO unwind或完成CRT。
- 最新生产VkBuffer组件fa76ca9→34dd0c8确由原transfer driver使用，同RG ELF198584B/SHA2088ffd…3b9真实exit0/90.15ms/no timeout、validator0/PASS；64KiB copy/256B clear读回零差异、31VkResult0、12destroy/3unmap。source bundlea43b88d…26f；嵌入HEAD2ca5d35为提交前构建标记，须用bundle/freeze核对源码。不是新probe或NVN桥。
- 历史Vulkan真实GPU操作exit0：64KiB buffer copy及8×8 RGBA clear/readback零差异、31VkResult0。Mali-G615 MC6/Vulkan1.3.247。没有shader、surface/present、descriptor indexing实际操作或NVN后端验收。
- owned-pages e112d65真机normal contract0：4KiB、8线程×32周期、32release竞争，mapped0/validtrue/errors0。62GiB只虚拟预留，实际只映射4096B。独立主机rollback/quarantine审查通过；真机未故障注入。组件未绑定SDK，mapped不是physical resident budget。
- 实体方向/B/左杆已记录；补测右Z/RZ均[-1,1]、L2/R2数字键8/10事件，模拟触发轴仍0。v5公开InputDevice枚举识别retrogame_joypad，旧raw-node误报修复。不代表游戏映射。
- v6显式音频0.25秒/PCM16peak327：实测48000Hz/256frames，扬声器route，12000frames写入/播放推进、246ms、stop/release正常，系统音量未改。用户“刚才没注意”，听感未确认，不记游戏音频PASS。
- 最新sourcec5bbddb（运行时dirty仅四文档）主机23集成检查均符合真实预期（覆盖exit3、normalexit23、_Exit24，其余0），原始输入前后hash一致，见`.local/host-verification/checkpoint-c5bbddb/summary.json`；旧1997f27检查点另留，不能用旧结果覆盖后续源码。独立普通组件review见docs/analysis/codex_*_review.md，不能替代完整QA。

## 核心阻塞及下一步

1. **真实owned pages/VA usage→后续CRT/TLS**：容量修复d4ded9已clean冻结且同RG Context/contract/info0，原nnMain实证越typed Query到pages71/+0x27f90/133，malloc到GetVirtualAddressMemoryResourceUsage66/+0x144b8/133。RG按public pthread测量不足则withhold callback、join/真实munmap/refund后补有界admin，第二次native68848支持精确logical65536；quota仍64KiB、admin4096另列，未硬编码784。1GiB是明确port-policy非原SVC RAM；RG baseline162869248/used162934784与Mac16KiB数值分开。独立新freeze QA在做。下一步隔离pages worker实现typed单一owner/address_region，再由runtime接profile wrappers/Query并同RG复测；不能假桩跳过pages或直接声称TLS/CRT通过。旧15eRG1与e95容量差失败保留；详见[profile报告](../docs/analysis/codex_guest_process_report.md)和证据索引。
2. **TLS/CRT**：main模块TLS镜像initialized0x220/total0x1019/align8/prefix16，独立复制/清零/对齐正常，不代表header/指针生命周期兼容。SDK OS32槽与ELF destructor LIFO是不同机制；应用槽0–15、SDK31–17、16未知；OS最多5轮且callback后清值。ELF节点48B/align16，可重入且无固定轮数。main303e0释放依赖guest allocator虚表，不能hostfree。静态报告在docs/analysis/codex_tls_sdk_lifecycle_report.md。可空GLOBAL OBJECT hook安装99ba0→9ae40、fini分支99ea0清零已闭合；普通线程返回依次OS槽→结束状态/通知→hook→ELF drain（内部门控非零条件），异常及初始线程退出不外推，详见codex_tls_exit_hook_report.md。不要执行写TPIDR的原SDK helper、批量改MRS或强制CRTgate。
3. **真实游戏图形/资源**：NVN仅2/535查询入口，533未实现，59缺PFN类型；当前GPU探针不等于翻译后端。Strato GPU/Hades评估已结束：有真实代码但无直接NVN ABI桥，需图形负责人定生产接入边界；整游戏可行性仍未知。按启动真实调用链补接口，不能批量假桩或继续用新probe替代后端。合法本地素材保留，实际挂载/IoStore/着色器/呈现/标题/场景都未验收。
4. **设备余项**：听感、完整键轴/休眠、真实游戏输入音频存档；30FPS实际场景/45分钟未运行。荣耀平板未连接，官方profile3000×1920/8sGen3/144Hz仅规格，不是adb身份或GPU兼容证据。

## 恢复与构建

先只读核对工作树/HEAD/WIP/agents/进程；root c5bbddb主机回归session22690已结束0，23checks符合真实预期0/3/23/24、main前后hash一致，日志`.local/host-verification/checkpoint-c5bbddb`；不用重复起同一回归，新capacity独立QA仍待完成。当前总控报告无ADB长命令、未决审批或不确定写入。旧文档ps127保留为历史失败；总控10:40:12UTC新恢复快照中ps/adb均0、限定AVD/qemu/gradle/cmake/nso名字空、仅授权RG。该时间点快照不等于永久当前，接力须重查。exec连接中断时不要重放不确定写入。原有AVD多次随执行环境中断消失，启动前核对adb与进程名，不wipe、不重复实例。RG已连接时无需为了探针再开AVD。

```sh
python3 scripts/project_status.py
python3 scripts/verify_host.py --runtime-contracts --game-leaf --output .local/host-verification/NEW-CHECKPOINT
FFRS_USE_MINIMAL_TOOLCHAIN=1 scripts/device/build_diagnostic_apk.sh
FFRS_USE_MINIMAL_TOOLCHAIN=1 scripts/device/build_runtime_hle.sh
```

使用新日志目录并保存原始exit。部分Google官方r27d sysroot每成员长度/CRC验证，AppleClang21/RustLLD交叉链接；完整NDK归档校验NOT_VERIFIED，不能称完整官方工具链安装。CMake静态库曾被Darwin ar/ranlib阻塞，minimal链实际成功；不要将交叉configure0写成build通过。

设备操作用显式序列号（保存在本地证据），先身份核验，安装前记录包名/权限/hash/签名；只访问本项目应用/新shell诊断目录。当前shell目录`/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259`保留，不清理。adb exec-out远端错误可能仍return0，须解析JSON、长度和摘要。不要读取私钥、别的应用或系统凭据。

下一优先级是typed guest_pages组件真实分配/释放/配额事务→profile wrappers/Query→独立QA与同RG原始入口，再继续CRT/TLS与NVN生产路径。最近16提交用途粗分5启动链/5诊断/6分析只是历史用途分类，不是工时或完成率；停止新增外围probe。

> 短接力提示：先读本文件/state/证据索引并实时核对HEAD/dirty/lease，保留所有WIP。d4ded9已同RG Context/contract/info0、原nnMain Query→pages133/malloc→VAusage133；pages worker实际在独立树四新文件编码，runtime仅接后续wrapper，独立QA新freeze进行中。旧失败不能删；无游戏成功。总控唯一Git/设备，DOC独占四文档，无push/root/清数据/密钥或新游戏下载。

主界面只读核验`cua.getApp('Codex')`被CUA安全规则DENIED，无截图/AX、不重试绕过，不阻开发且不是Git待审批。内部`/root/...`仅协作标识，未创建四个sidebar任务，UI可见性UNKNOWN。

短目标：原始nnMain越过Query/TLS/CRT→真实图形/标题→可操作场景→游戏输入音频存档性能。持续循环“定位真实阻塞→最小功能修复→编译→真机复测→本地检查点→下一关”；各scope并行，少重复调研、无假桩，仅报实质突破/用户操作/真实阻塞。工具无CreateGoal/UpdateGoal/persistent入口，仅当前回合按目标持续执行，不声称持久参数或后台已开启。

# 可直接交给Antigravity

继续本机《最终幻想：共鸣 / FINAL FANTASY RESONANCE》Switch→Android ARM64原生移植。最终验收RG477M真实标题、可操作场景、输入/音频/存读档及稳定性；兼顾荣耀GT Pro平板等比例/必要留边。不要改成Mac、FC27、模拟器/串流/视频或空窗口，不用组件返回0冒充游戏，不报无依据百分比。

先在 `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration` 读 `AGENTS.md`、`coordination/HANDOFF.md`、`coordination/WORKTREES.json`、`coordination/state.json` 和 `docs/analysis/codex_handoff_evidence_index.md`。原交付路径 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`；任务分支 `task/codex-integration`。代码检查点a27742c，实际最新运行时代码e305d1e；交接文档后续提交实读Git。先只读核验两树HEAD/WIP、其它任务/进程和.local链接，不按旧聊天覆盖新成果。

Codex本轮已安全停止，所有实现/审查代理交回；没有已知构建或测试命令待收。新DSO树 `ffrs-dso-thread-owner`/task/T02-dso-thread-owner 与新TP树 `ffrs-native-tp-read`/task/T02-native-tp-read 仅创建在a277、clean，代码实现NOT_STARTED，未发布新getter ABI/方案；可以核验后转移lease。旧 `ffrs-qa` 的未跟踪scripts/qa及tests/acceptance/nso_contract.c要保留、不接续，其工作流被平台中止；外部Claude树与.local不动，不停止或消息其它工具。

最新真实结果：106/110已编译并实际RG通过；原nninitStartup空/非空均返回，真实OS slot0析构main+18c60完成（2轮/1调用/remaining0），最终0/0，无timeout/main hash不变。显式--startup-allocator在startup后调用原48/64字节alloc/free也0/0。6a HOST消费者14项11×0+3预期严格134及独立审查通过。完整原DT_INIT包装仍卡接口515 __nnmusl_init_dso，caller+0x5920a34，raw133/162.900ms；尚无完整CRT、nnMain、标题/可玩或游戏APK。完整命令/哈希/输出在证据索引；不重放旧stage或反复全量构建。

下一硬关是515真实DSO登记、module/thread-generation TLS owner/current getter、析构及透明native TP读取适配。先读codex_crt_frontier_review.md、codex_tls_caller_report.md、codex_boot_order_reuse_report.md、codex_tls_exit_hook_report.md。直接复用现有GP21/104B/160B gateway、434 nb_call_from_host、6a真实HOST分配器、module_tls镜像及Context/page配额票据。515有21机器字且全部RELR，原0/8实际为B/B+8；未知字段保持opaque，不能猜返回0/1放行。main16B零header不是完整TCB，opaque PROT_NONE身份不是SDK ThreadType；main1489处MRS含49复杂用途，固定单primary literal和写Bionic TP都不成立。CreateThread还需要共享GOT逐线程正确分派、SCS/owner、隔离及join/析构/失败保留。不要只补个scope或空桩。

实施上只补经证据证明的必要差异，先最小可集成真实代码，正向自有fixture、增量Android编译、root匹配哈希的RG复测、独立复核、本地检查点，随后推进实际CRT→图形→标题→可玩。最多3worker+总控，按不重叠文件/隔离worktree并行，派工前写state lease；总控单独Git/ADB，文档不阻塞编码。遇真正技术阻塞留下最小证据/原码/恢复步骤，不盲试或用报告数量替代进展。

成熟复用已落地：LASRecomp Beta4 MIT revision fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93，命令记录/typed transfer已接真实vkCmdCopyBuffer并实际RG零差异；尚无游戏NVN对象ABI、Maxwell着色器或present。不要采用CPU memmove假GPU、GetInfo硬编码0或把GLSL shaderc当Maxwell translator。REA技能 /Users/yuyan9923/.codex/skills/reverse-engineer-anything/SKILL.md 已实际读取；当前MCP0、CLI6.5在函数分析前Swifthelper编译失败，无functionEvidence，既有LLVM切片另归因，复用匹配输入缓存不重试同故障/做全局修复。

工具链在 /Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-android/.local/minimal-ndk/toolchain，官方partial NDK逐成员hash/CRC验证，整个归档未验证。现成AppleClang+RustLLD和对象依赖在manifest，避免不必要113步重编。ADB /Users/yuyan9923/Library/Android/sdk/platform-tools/adb，显式 -s RG477M01445122，先核型号/连接/工件hash。设备远端项目根 /data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259；既有main已校验，不盲推。RG Android14/ARM64/nativebridge0/Mali G615，模拟器和荣耀平板验收分别记，不因都是安卓就当兼容。v6 APK只是诊断JSON，用户无需为其当游戏操作。

保留失败经验：mmap未MAP_FIXED不能证明必须0地址，tail曾掩盖139，最长RELA候选不证明完整性，pre-init malloc139属于构造前访问不能手填vptr，musl早return2/leaf0非游戏，94MB必OOM旧猜测已撤回。旧平台中止QA及被拒Codex UI操作不重试绕过。所有游戏/生成代码/大型日志留.local，不提交素材、密钥、凭据。用户授权本地开发/提交/并行、设备诊断和原分支安全快进；保护WIP，不reset/clean/stash，不推远端/发布/购买/root/刷机/清数据/另取游戏或密钥。继续前先确认当前事实，不要求用户重复授权既有范围。

每个实质验证/提交更新唯一HANDOFF+state+证据索引，再用 python3 scripts/project_status.py --write 生成STATUS。不能伪称有后台持久目标引擎。先给简短实际接手状态和下一步，然后编码，不重复整套调研。

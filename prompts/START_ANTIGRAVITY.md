# Antigravity 完整启动提词

你是本项目总控和集成人，负责组织真实的多 Agent 协作，持续推进《最终幻想：共鸣 / FINAL FANTASY RESONANCE》在安柏尼克 RG477M 上的 Android ARM64 原生运行、分辨率适配和稳定帧率。Mac 仅为分析/构建主机。先不要铺开其他游戏或完整 Mac 版。

项目已创建：
`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`

请先打开该目录，完整阅读根 AGENTS.md。这是本仓库唯一工程规范；然后阅读 docs/PROJECT.md、docs/TOOLCHAIN.md、TASKS.md、coordination/state.json、coordination/HANDOFF.md。运行 `python3 scripts/project_status.py` 核实当前状态，不能仅依赖这段提词或历史聊天。现阶段只有项目骨架，没有游戏原生实现或真机 PASS。

我的验收目标：
- 游戏本体代码在 Android arm64-v8a 上原生执行，可采用静态重编译/逆向重建与平台 API 兼容层。
- 不接受 Ryujinx 等模拟器套壳、通用 CPU 解释器/JIT、串流、视频或空窗口替代。反编译出伪代码、编译成功、APK 安装成功和游戏可玩是不同阶段。
- RG477M 官方规格为天玑8300、Mali-G615 MC6、Android14、1280×960 4:3；RAM/ABI/Vulkan/驱动/手柄以用户实机探针为准。芯片理论性能或充足 Token 不证明一定快，也不证明只改分辨率就够。
- 首阶段目标稳定30 FPS，60 FPS 是后续目标；不能牺牲游戏逻辑速度。真机暖缓存目标平均≥29 FPS、P95帧间隔≤35ms、P99≤50ms，冷缓存/首次着色器编译单独报告，不能事后删除慢帧。按 PROJECT 完成代表场景、多次短测与45分钟持续测试；无法达标时保留数据并继续按瓶颈优化，不静默降低标准。
- 保留游戏原比例；若为16:9，输出1280×720居中于1280×960，上下各120黑边，不拉伸、不裁UI。内部渲染从960×540候选档验证，另测1280×720和854×480；按渲染器实际能力实现，输出、UI与内部渲染分别记录。先固定档，后考虑动态分辨率。

技能与工具：
1. 使用 reverse-engineer-anything / REA 技能。已安装入口为 `/Users/yuyan9923/.gemini/config/skills/reverse-engineer-anything/SKILL.md`，必要时用共享 `.agents/skills` 入口。完整读取技能，按目标读相关指南，使用当前 MCP 实际工具/schema。
2. REA负责包、代码、函数、资源和平台依赖证据；有完整源码的开源重编译器/运行时使用正常开发工具。安装 Skill 不是完成移植，不重复setup或更改全局权限。
3. 复用 `/Users/yuyan9923/.local/share/rea-toolchain/README.md` 中的 Ghidra/Switch Loader/hactool 等。不要假设 REA open_binary 直接支持 NSP/NSO。缺什么依赖再核实安装什么，不盲目重装。
4. 可研究 SuperMarioWonderRecomp、AC-Recomp 及相符项目，先核实来源、可获取revision、许可证和源码支持范围。其他游戏成功不代表本作支持，不绕过TitleID/版本校验伪造适配。

输入游戏位于：
`/Users/yuyan9923/Downloads/SwitchGame/[最终幻想：共鸣][FINAL FANTASY RESONANCE]`
本体候选版本 v1.0.0，TitleID 文件名线索 0100B8A026F14000；目录还有更新和DLC。T00核实内部元数据和完整哈希，不能仅信文件名。原 Mac移植交付 是模拟器成果，只读参考，原包/存档/旧成果保持原样。不要自动获取新的游戏、固件或密钥，不打印/上传密钥内容。

多 Agent 工作方式：
- 你是唯一总控及状态写者。根据真实可用能力派工，默认最多三个工作Agent并行；没有调度功能则使用本项目 WORKER.md 模板给实际独立任务，或串行执行并如实说明，不能假装多人。
- 角色按任务轮换：RE逆向/路线；CORE重编译与运行时；ANDROID/GPU掌机宿主、Mali与性能；QA独立复跑。QA与实现者分开，不由实现者单方面宣布验收完成。
- 每个工作Agent只领一张具体卡，记录输入、输出、依赖、实际分支/worktree、可写文件和验收。优先独立worktree；状态文件仅总控改。一个文件只有一个写者，跨模块集成由你处理。
- 从T00开始，出口后并行T01和T03A（宿主交叉构建）；T03B单独验证真机。再推进T02真实游戏代码样例、T04真实启动、T05可操作场景、T06分辨率/性能、T07持续QA。T04可在T02/T03A后做实现，真机标题验收仍须T03B通过。阶段依赖见 TASKS.md，不把所有实现同时开工。
- 并行研究可以，昂贵解包/分析/构建按宿主内存统一调度。同一输入复用证据，避免几个Agent重复读9GB包或同时跑完整编译。

Token、上下文和跨模型接力：
- 我可以投入很多Token，但不能接受额度或上下文耗尽后重头再来。每次关键修改、构建测试、阶段变化和交接，把状态写回项目；大日志留文件，聊天只报摘要与路径。
- coordination/state.json 保存任务/依赖/写入占用；HANDOFF.md 保存当前恢复点与下一步精确命令；每Agent交回独立报告；运行 `python3 scripts/project_status.py --write` 更新生成看板。
- 接近上下文上限时优先完成一个小检查点并交接，不启动无法收口的大改。无法读取余量时按里程碑主动保存。
- 长进程先记录命令/cwd/日志，启动后记录PID/session、存活检查和恢复办法。切任务前列出dirty/untracked，保留WIP，不reset/clean/stash。过期占用先核实原任务和进程，不能自动抢占。
- 自有源码和清理后的记录本地提交到任务分支；游戏派生代码、资源、密钥、APK、大型证据进.local/并记录路径/摘要/再生成命令，不提交上传。不强推、不动main/master，不混入其他人的改动。Git忽略不等于私有产物已备份。

现在请先复述原生目标、帧率/分辨率口径和接力机制，检查真实工具与多Agent能力，登记负责人并启动T00。不要只写方案就结束，基线完成后继续做最小真实代码样例。没有真机时继续可做的主机分析/构建/测试，把真机项标BLOCKED_DEVICE，不伪造PASS，也不把缺设备推导成移植不可能。每阶段给出实际产物、证据、失败和下一步；遇到确实无法跨越的阻塞，交付可复现失败和所缺条件，不擅自改交模拟器方案。

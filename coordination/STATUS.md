# 当前项目状态（生成文件）

状态事件时间：2026-10-23T08:36:49.519997+00:00
阶段：NATIVE_RUNTIME_AND_DIAGNOSTIC_APK_CHECKPOINT
总控：Codex — integration owner; GPT-6.1 xhigh workers; CreateGoal tool unavailable
更新方式：按事件更新，没有后台自动刷新。

| 任务 | 角色 | 状态 | 实际负责人 | 启动依赖 | 验收门禁 | 分支 |
|---|---|---|---|---|---|---|
| T00 | RE | passed | Antigravity (RE) | 无 | 无额外门禁 | work/rg477m-native |
| T00R | QA | passed | Agent C (QA, 独立复核) | T00 | 无额外门禁 | task/T00-qa-review |
| T01 | RE | passed | Agent A (RE) | T00 | 无额外门禁 | task/T01-route |
| T02-PREP | CORE | in_progress | Codex coordinator (integrated strict loader and true exit-code infrastructure) | T00 | 无额外门禁 | task/codex-integration |
| T02 | CORE | in_progress | Codex runtime worker | T01 | 无额外门禁 | task/T02-codex-runtime |
| T03A | ANDROID | passed | Codex Android worker | T00 | 无额外门禁 | task/T03A-codex-android |
| T03B | ANDROID | blocked | Codex coordinator — BLOCKED_DEVICE; no adb device | T03A | 无额外门禁 | 未创建 |
| T04-assets | RESOURCE | passed | Agent D (RESOURCE) | T00 | 无额外门禁 | task/T04-assets |
| T04-nvn | GRAPHICS | passed | Agent E (GRAPHICS) | T00 | 无额外门禁 | task/T04-nvn |
| T04 | CORE_ANDROID | pending | 未领取 | T02, T03A | T03B | 未创建 |
| T05 | CORE_GPU | pending | 未领取 | T04 | 无额外门禁 | 未创建 |
| T06 | GPU_PERF | pending | 未领取 | T05 | 无额外门禁 | 未创建 |
| T07 | QA | pending | 未领取 | T06 | 无额外门禁 | 未创建 |
| T07-PREP | QA | blocked | Codex independent QA (platform safety check interrupted; no further retries) | T00R | 无额外门禁 | task/T07-codex-qa |
| T04-NVN-RUNTIME | GRAPHICS | in_progress | Codex coordinator | T01 | 无额外门禁 | task/codex-integration |
| T07-FUNCTIONAL-REVIEW | QA | passed | Independent ordinary functional reviewer | T00R | 无额外门禁 | 未创建 |
| T02-TLS-ANALYSIS | RE | in_progress | Independent static TLS/main caller analyst | T00 | 无额外门禁 | 未创建 |
| T07-MODULE-TLS-REVIEW | QA | passed | Independent ordinary C module TLS reviewer | T00R | 无额外门禁 | 未创建 |
| T02-CXX-GUARD | CORE | in_progress | Runtime worker | T01 | 无额外门禁 | task/T02-codex-runtime |
| T03B-VM-DISPLAY-FIX | ANDROID | in_progress | Android worker; coordinator independently validates in AVD | T03A | 无额外门禁 | task/T03A-codex-android |

## 独占写入

- {"task": "T03B-VM-DISPLAY-FIX", "owner": "Codex Android worker", "paths": ["src/android/", "scripts/device/", "tests/device/", "docs/analysis/codex_android_report.md"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-android", "branch": "task/T03A-codex-android", "acquired_at": "2026-10-23T06:58:31.817820+00:00", "checkpoint": "e59c2fa", "objective": "Fix observed Android JNI display false 1280x960 fallback: measured 1080x2340; unknown CLI values unavailable, no fabricated audio capabilities"}
- {"task": "T04-NVN-RUNTIME", "owner": "Codex coordinator (graphics integration)", "paths": ["src/graphics/nvn/", "tests/graphics/", "scripts/graphics/", "docs/analysis/codex_graphics_report.md"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration", "branch": "task/codex-integration", "acquired_at": "2026-10-23T06:58:31.817820+00:00", "checkpoint": "d26d3f6"}
- {"task": "T02-TLS-ANALYSIS", "owner": "Static TLS analyst — bounded remaining49 read flows", "paths": ["tls-research/ (outputs only; repository read-only)"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/tls-research", "branch": null, "acquired_at": "2026-10-23T08:23:49.368800+00:00", "checkpoint": "caller-v4", "objective": "Classify remaining49 non-adjacent TP data flows without execution; identify header/dynamic access uncertainty"}
- {"task": "T02-CXX-GUARD", "owner": "Runtime worker — gpt-6.1-sol xhigh", "paths": ["src/runtime/nso/", "src/runtime/hle/", "tests/native/", "scripts/runtime/", "docs/analysis/codex_runtime_report.md"], "worktree": "/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime", "branch": "task/T02-codex-runtime", "acquired_at": "2026-10-23T08:30:50.891315+00:00", "checkpoint": "3124895", "objective": "Actual Android missing __cxa_guard_acquire: use verified compiler C++ ABI implementation, link/export or typed bridge, no fake guard success; preserve unsupported CRT/TLS"}

## 阻塞

- 完整 CRT 初始化仍在 __nnmusl_init_dso（caller +0x5920a34）严格 exit133。原SDK allocator gate实际0、DSO函数实际return2；后续helper会写TP寄存器，禁止绕过门控直接执行。Android单独main/malloc另有__cxa_guard_acquire缺失（正在复用真实C++ABI实现），Mac当前QueryMemoryInfo。
- RG477M与荣耀平板需等用户下班连接；真机GPU、标题、输入音频、游戏存读档、性能和稳定性仍NOT_RUN。Android14 ARM64 AVD已完成诊断/叶函数实跑，不替代真机。
- NVN 535 名字仅两个查询入口可解析，其余 533 缺失；没有 Vulkan 资源/队列/着色器/显示转换实现。Bundled header 对其中 59 名字缺 PFN 类型，不能猜 ABI。
- 既有 RomFS 流式工具和资源清单不等于游戏运行时挂载/IoStore 解压成功；尚未集成。保留合法本地素材，不获取新游戏或密钥。
- 独立 QA 代理被平台安全检查中止；停止重试，未取得完整独立验收。普通基线与未提交成果保留，不记 PASS。
- 完整 NDK 包校验未完成。当前真实 ARM64 构建使用 Google 官方逐项长度/CRC 已验证的部分 sysroot 与 AppleClang/RustLLD，不称完整官方 NDK 安装。
- 环境未提供 CreateGoal / UpdateGoal 入口，未开启持久目标引擎；当前回合按完整目标执行。

## 已验证

- Antigravity 交接基线 d26d3f6，接管时工作树干净、active_writers=[]；新开发使用独立分支/工作树，旧成果保留。
- 继承 T00/T00R 记录：内部元数据 mucha / FFRS，SQUARE ENIX，v1.0.0；ExeFS 与本地游戏包指纹保留。Codex 本回合不重新解密或读取密钥。
- 游戏引擎 UE 5.6.1 的既有直接字符串证据保留；sdk=SDK 20.5.17，subsdk0=movie，rtld=nnrtld.nss。
- 严格加载器实际解析当前 main.bin：651 个导入、4612 RELA、1370974 RELR；未知导入使用可定位 sentinel，不返回假成功；resolver callback 与正常有效输入契约已验证。
- main.bin SHA256 6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5；原始 leaf +0x694bd0 的 16875 组 Mac ARM64 真实执行与数学 oracle 零差异，前后输入哈希一致。不是完整游戏或正式 T02 PASS。
- Android-target libffrs_leaf.so 已实际生成：2176B，ELF64 AArch64 ET_DYN，原函数44B哈希8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9；无 DT_NEEDED。尚未设备执行。
- SDK 已证实的 ReferSymbol typed anchor、Allocate/FreeAddressRegion 的真实 PROT_NONE 预留已实现；4KiB/64KiB/2MiB/62GiB 预留、保护查询、释放通过，不触及实体页。
- 真实 POSIX Mutex 家族依据本地 SDK 字段 ABI 实现；8线程80000次 increments/overlap0、递归深度3、争用trylock、100次同址重建通过。Opaque owner token 不冒充完整 Switch ThreadType。
- 独立 guest-TLS PoC：8线程16槽、128000次值检查零差异且宿主 pthread key / TPIDRRO 保留。尚未绑定游戏 SDK TLS，不证明完整 TLS 兼容。
- Android T03A 包装范围通过：实际 AArch64 ELF/诊断CLI/JNI与 org.ffrs.diagnostic 签名 APK；DEX/Manifest/签名、16KiB LOAD、无 W+X/TEXTREL 静态检查通过。未安装或执行游戏。
- 诊断 UI 有硬件输入、生命周期、app-private 临时文件往返代码；无真机执行证据，不能替代游戏输入/音频/存档。正在增加可选的设备 leaf 对照入口。
- 主机普通功能测试、NVN typed registry 并发行为通过；NVN coverage 真实退出3、2/535解析明确表示无渲染完成。
- 部分已校验 Android 工具缓存已复制到原仓库新建 .local/minimal-ndk（3330文件，未覆盖），后续可在交付目录重建。
- e0024a0 集成真实退出回调注册/DSO筛选/重入/并发及明确单guest实验scope；注册不会提前执行，正常exit返回23运行一次，_Exit返回24不执行，卸载丢弃不会调用已卸载代码。真实nnMain当前QueryMemoryInfo，仍非完整启动。
- 独立普通功能审查完成（adc6e8e / 5e165d0）：6次严格Clang编译均0，9次运行符合0/23/24预期；未发现正常Mutex/独立TLS/地址预留/退出回调范围需修缺陷。没有重启被中止的NSO安全/异常输入流程，不证明完整QA或SDK兼容。
- 独立main caller/TLS静态研究：21参数全部RELR，候选TLS模板+0x7767108/初始化0x220/总长0x1019/对齐8；最高直接字节访问TP+0x1028，发现__cxa_thread_atexit实际调用。49处非相邻TP读取仍待核对；不证明可用宿主TP。
- Android14 ARM64 AVD pvz2/nativebridge0实际安装冷启动org.ffrs.diagnostic（c280e2a，422420B，SHA8ec9d704ae331f77074e67cc63ee56b398ff89d6f3bf148f8de1b3f9637bb07d）；原始leaf实际16875比较0差异，构造器未调用，scope仅单函数。
- Android严格HLE CLI真实加载main exit0；构造器/__nnmusl_init_dso133、nnMain/malloc/__cxa_guard_acquire133、startup/AllocateTlsSlot133，未超时；不同平台边界没有混写。
- 444115b独立module TLS镜像已真实复制/清零/对齐/释放；主机正常多线程与映射模板验证通过；独立纯C review 32968000检查无错。未绑定HLE、未写TP，复制的指针值不证明指向对象卸载后仍有效。
- AVD图形探针实测Apple M5/MoltenVK Vulkan1.3（虚拟GPU，不替代Mali/Adreno），发现固定1280x960/60Hz探针fallback错误，版本4真实WindowMetrics/密度/刷新模式修复已主机测试/交叉构建，尚待总控VM复验。

## 未验证

- 正式 T02 所要求的 Switch/可信参考行为对照与 Android 实际执行；完整启动与 CRT/TLS。
- 游戏运行时资源挂载与解压、NVN->Vulkan 渲染及 Switch 着色器转换。
- RG477M 真机能力、标题、可玩场景、真实输入/音频/存读档。
- 实际分辨率、30FPS帧呈现、45分钟稳定性和完整独立 QA。

具体恢复命令与未完成修改见 HANDOFF.md。

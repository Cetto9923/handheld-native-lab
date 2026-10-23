# 当前项目状态（生成文件）

状态事件时间：2026-10-23T13:40:00+08:00
阶段：ROUND1_ACTIVE_T01_T03A_T04ASSETS_T04NVN_T02LOADER
总控：Claude Code (Fable 5) — 唯一总控；2026-10-23 接管 Antigravity 交接，已核验现场并派工
更新方式：按事件更新，没有后台自动刷新。

| 任务 | 角色 | 状态 | 实际负责人 | 启动依赖 | 验收门禁 | 分支 |
|---|---|---|---|---|---|---|
| T00 | RE | passed | Antigravity (RE) | 无 | 无额外门禁 | work/rg477m-native |
| T00R | QA | passed | Agent C (QA, 独立复核) | T00 | 无额外门禁 | task/T00-qa-review |
| T01 | RE | in_progress | Agent A (RE) | T00 | 无额外门禁 | task/T01-route |
| T02-PREP | CORE | in_progress | 总控 (NSO 原生加载器 / 行为基线基础设施) | T00 | 无额外门禁 | claude/laughing-cray-3464b0 |
| T02 | CORE | pending | 未领取 | T01 | 无额外门禁 | 未创建 |
| T03A | ANDROID | in_progress | Agent B (ANDROID) | T00 | 无额外门禁 | task/T03A-android |
| T03B | ANDROID | pending | 未领取 | T03A | 无额外门禁 | 未创建 |
| T04-assets | RESOURCE | in_progress | Agent D (RESOURCE) | T00 | 无额外门禁 | task/T04-assets |
| T04-nvn | GRAPHICS | in_progress | Agent E (GRAPHICS) | T00 | 无额外门禁 | task/T04-nvn |
| T04 | CORE_ANDROID | pending | 未领取 | T02, T03A | T03B | 未创建 |
| T05 | CORE_GPU | pending | 未领取 | T04 | 无额外门禁 | 未创建 |
| T06 | GPU_PERF | pending | 未领取 | T05 | 无额外门禁 | 未创建 |
| T07 | QA | pending | 未领取 | T06 | 无额外门禁 | 未创建 |

## 独占写入

- {"task": "T01", "owner": "Agent A (RE)", "paths": ["docs/analysis/route.md", "docs/analysis/t01_*.md", "tools/", "evidence/t01/"], "worktree": ".claude/worktrees/t01-route", "branch": "task/T01-route", "acquired_at": "2026-10-23T12:39:00+08:00", "checkpoint": "baseline b26bf15"}
- {"task": "T03A", "owner": "Agent B (ANDROID)", "paths": ["src/android/", "scripts/device/", "tests/device/", "docs/analysis/t03a_build_report.md"], "worktree": ".claude/worktrees/t03a-android", "branch": "task/T03A-android", "acquired_at": "2026-10-23T12:39:00+08:00", "checkpoint": "baseline b26bf15"}
- {"task": "T04-assets", "owner": "Agent D (RESOURCE)", "paths": ["scripts/extract/", "docs/analysis/asset_inventory.md", "evidence/assets/"], "worktree": ".claude/worktrees/t04-assets", "branch": "task/T04-assets", "acquired_at": "2026-10-23T13:05:00+08:00", "checkpoint": "baseline a7ae15d"}
- {"task": "T04-nvn", "owner": "Agent E (GRAPHICS)", "paths": ["src/graphics/nvn/", "docs/analysis/nvn_surface.md", "evidence/nvn/"], "worktree": ".claude/worktrees/t04-nvn", "branch": "task/T04-nvn", "acquired_at": "2026-10-23T13:05:00+08:00", "checkpoint": "baseline a7ae15d"}
- {"task": "T02-PREP", "owner": "总控 (Claude Code) — NSO 原生加载器", "paths": ["src/runtime/nso/", "tests/native/", "evidence/coordinator/"], "worktree": ".claude/worktrees/laughing-cray-3464b0", "branch": "claude/laughing-cray-3464b0", "acquired_at": "2026-10-23T13:05:00+08:00", "checkpoint": "a7ae15d + 本轮 loader 提交"}

## 阻塞

- RG477M 真机尚未连接或验证；真机实测项保持 BLOCKED_DEVICE 门禁，不阻塞主机交叉构建。
- 宿主可用磁盘约 20 GiB；Android NDK + Ghidra 工程 + RomFS 提取属重型占用，必须串行并及时回收，禁止并发铺开多个大分析工程。
- 游戏资源侧缺口：Program NCA 的 RomFS（8.5 GB）未提取；ExeFS 提取所用 NCA 流式解密逻辑此前未入库（已由 T00R 复核确认），T04 启动链需要资源与模块加载器时这是硬前置。
- bindless/textureBuffer 支持度未验证：本作重度依赖 bindless 与 NVN 专有纹理格式 0x25A5A；Mali-G615 的 descriptor indexing 能力只能由 T03B 真机实测判定，是 30 FPS 目标的最高风险项。
- hactool 内置密钥集含损坏记录（Key ... must be 32 hex digits!）导致其 NCA 路径不可用；本作 rights-id 加密的 title key 需另行推导（已交 Agent D 复现）。

## 已验证

- 协作项目初始化与总控接管
- 游戏包完整性与全量哈希已校验 (本体/升级档/DLC)
- 内部元数据已从 NACP/NPDM 提取：Title 0100B8A026F14000, 内部项目名 mucha (FFRS), SQUARE ENIX, v1.0.0
- ExeFS 关键二进制定向提取完成 (.local/exefs/，含 main, main.npdm, sdk, subsdk0, rtld)
- 游戏引擎直接证据：Unreal Engine 5.4 (Shipping工件 FFRS-Switch-Shipping.nss, 项目代号 mucha, 含 EChaosVDAABBTreeNodeFlags, EnhancedInput, CommonUI, CRIWARE, Oodle, NVN)
- 逆向工具链实测：hactool 正常解析，Ghidra 12.1.4 + Switch Loader 成功导入识别 NSO0 (AARCH64:LE:64:v8A)
- 2026-10-23 接管复核：5 个 ExeFS 工件 SHA-256 与 evidence/t00_verification.json 逐项一致（轻量复核，未重算 9GB NSP）
- 2026-10-23 接管复核：adb devices 无设备；Android SDK 存在但缺 NDK/build-tools/platforms/cmake，T03A 需按路线安装
- 2026-10-23 接管复核：历史 Mac 交付为 Ryujinx 模拟器 + IL 补丁（非原生重写），仅作线索不继承其 PASS；从中确认本作重度依赖 bindless/textureBuffer、使用 NVN 专有格式 0x25A5A、原生 Switch 限帧 30
- 2026-10-23 独立复核（T00R）：ExeFS 五工件 SHA-256、三个 NSP 完整 SHA-256（含 9GB 本体）、NACP 元数据、PFS0 清单、NSO0 段表、build-id 对应均独立重算一致。
- 2026-10-23 引擎版本修订为 UE 5.6.1（直接证据：SDK MW+EpicGames+UnrealEngine-5_6_1，main.bin 偏移 108874700，全文唯一）；T00 的 UE 5.4 为推断且已被推翻。
- 2026-10-23 模块职责修订：sdk=Nintendo SDK 20.5.17 NVN 图形栈，subsdk0=movie，rtld=nnrtld.nss；游戏与 UE 代码只在 main。
- 2026-10-23 总控：main 模块 NSO 重定位表 4612 条（RELATIVE=0，.text 完全位置无关）、导入 651 符号（24 个 nn:: 命名空间+libc+CRT）、导出 8（含 nnMain @0x352F160）。
- 2026-10-23 总控：原生 NSO 加载器（src/runtime/nso/）在 macOS arm64 上完成映射 162MB 模块镜像、解析 651 导入、应用 4612 重定位，并首次原生执行了游戏二进制内的真实导出函数（模块偏移 0x1090，返回正常，build id 与 T00 一致）。

## 未验证

- 本游戏原生代码重编译/重建样例 (T02) — 前置基础设施(T02-PREP)已完成并可原生执行模块内函数，但尚未选定并验证目标函数
- Android ARM64 APK 宿主 (T03A)
- RG477M 实际 Vulkan/驱动/硬件能力 (T03B/BLOCKED_DEVICE)
- 真机画面/输入/音频
- 帧率与45分钟稳定性
- T01 技术路线判定（Agent A 产出中）
- 游戏资源清单与着色器字节码格式（Agent D 产出中）
- NVN API 面与 Vulkan 映射（Agent E 产出中）

具体恢复命令与未完成修改见 HANDOFF.md。

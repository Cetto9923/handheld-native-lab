# 当前项目状态（生成文件）

状态事件时间：2026-10-23T12:40:00+08:00
阶段：ROUND1_DISPATCHED_T01_T03A_T00R
总控：Claude Code (Fable 5) — 唯一总控；2026-10-23 接管 Antigravity 交接，已核验现场并派工
更新方式：按事件更新，没有后台自动刷新。

| 任务 | 角色 | 状态 | 实际负责人 | 启动依赖 | 验收门禁 | 分支 |
|---|---|---|---|---|---|---|
| T00 | RE | passed | Antigravity (RE) | 无 | 无额外门禁 | work/rg477m-native |
| T00R | QA | in_progress | Agent C (QA) | T00 | 无额外门禁 | task/T00-qa-review |
| T01 | RE | in_progress | Agent A (RE) | T00 | 无额外门禁 | task/T01-route |
| T02 | CORE | pending | 未领取 | T01 | 无额外门禁 | 未创建 |
| T03A | ANDROID | in_progress | Agent B (ANDROID) | T00 | 无额外门禁 | task/T03A-android |
| T03B | ANDROID | pending | 未领取 | T03A | 无额外门禁 | 未创建 |
| T04 | CORE_ANDROID | pending | 未领取 | T02, T03A | T03B | 未创建 |
| T05 | CORE_GPU | pending | 未领取 | T04 | 无额外门禁 | 未创建 |
| T06 | GPU_PERF | pending | 未领取 | T05 | 无额外门禁 | 未创建 |
| T07 | QA | pending | 未领取 | T06 | 无额外门禁 | 未创建 |

## 独占写入

- {"task": "T01", "owner": "Agent A (RE)", "paths": ["docs/analysis/route.md", "docs/analysis/t01_*.md", "tools/", "evidence/t01/"], "worktree": ".claude/worktrees/t01-route", "branch": "task/T01-route", "acquired_at": "2026-10-23T12:39:00+08:00", "checkpoint": "baseline b26bf15"}
- {"task": "T03A", "owner": "Agent B (ANDROID)", "paths": ["src/android/", "scripts/device/", "tests/device/", "docs/analysis/t03a_build_report.md"], "worktree": ".claude/worktrees/t03a-android", "branch": "task/T03A-android", "acquired_at": "2026-10-23T12:39:00+08:00", "checkpoint": "baseline b26bf15"}
- {"task": "T00R", "owner": "Agent C (QA, 只读复核)", "paths": ["coordination/reports/T00R-evidence-review.md", "tests/acceptance/", "docs/analysis/acceptance_matrix.md"], "worktree": ".claude/worktrees/t00-qa-review", "branch": "task/T00-qa-review", "acquired_at": "2026-10-23T12:39:00+08:00", "checkpoint": "baseline b26bf15"}

## 阻塞

- RG477M 真机尚未连接或验证；真机实测项保持 BLOCKED_DEVICE 门禁，不阻塞主机交叉构建。
- 宿主可用磁盘仅约 18 GiB；Android NDK + Ghidra 工程属重型占用，需串行并及时回收，禁止并发铺开多个大分析工程。
- 游戏资源侧缺口：Program NCA 的 RomFS（8.5 GB）未提取，ExeFS 提取所用 NCA 流式解密逻辑未入库（只有产物，没有可复现脚本）；T04 启动链需要资源与模块加载器时这是硬前置，且提取会进一步挤压磁盘。
- bindless/textureBuffer 支持度未验证：历史线索显示本作重度依赖 bindless 与 NVN 专有纹理格式 0x25A5A，在 MoltenVK 上即成为 GPU 侧瓶颈；Mali-G615 的 descriptor indexing 能力只能由 T03B 真机实测判定，是 30 FPS 目标的最高风险项。

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

## 未验证

- 本游戏原生代码重编译/重建样例 (T02)
- Android ARM64 APK 宿主 (T03A)
- RG477M 实际 Vulkan/驱动/硬件能力 (T03B/BLOCKED_DEVICE)
- 真机画面/输入/音频
- 帧率与45分钟稳定性
- T01 技术路线判定（Agent A 产出中）

具体恢复命令与未完成修改见 HANDOFF.md。

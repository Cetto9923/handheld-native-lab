# TLS provider fresh-lifecycle repair — 80d4cfbf

结论：**P2 CLOSED，限定修复 PASS**。冻结 `80d4cfbf3756f7744072914cda1c1b2d2653c6cb`，base `cfc4df8921eaf911662ee6512c770a0683e787db`。原 cfc 的 SIGSEGV 证据和原报告保持不变。本人 Android 编译/执行 NOT_RUN；此处不继承总控 cfc 设备结果为修复或 root 组合结果。

`guest_process_register_tls_allocator` 现在先检查实际 primary、module 非空和未登记 pair，随后才调用非空限定的 `nso_base`。未加载模块时保持原 typed `NSO_INVALID/EINVAL`，不改变回调或 allocation 状态。修复没有更改正常 pair ABI、consumer/锁/保留策略、owner 组件或测试预期。

独立从 Git 快照 25 文件：以下 4 个变化与作者 freeze 一致，另 21 个与 cfc 相同。九个已审自有依赖对象逐一核哈希后复用；未重跑完整 owner QA。

| 文件 | SHA-256 |
| --- | --- |
| guest_process.c | 9c455a4d6d901b58153f910788a016fcced5a8a7566ebd62e8fbcebf9e018a69 |
| guest_process_contract.c | df809d0c80932e2190052cf955f523a4fb0ece62870fcc75e06d35b408edd1d3 |
| build_guest_process_contract.sh | 0d6b043588d024d86812bbbb8e00bee86ebc74c7be5b73ae771e4fd2b6f523a1 |
| codex_guest_process_report.md | cdd412897ee23d81c3c368531f26c34f1ca1320f7f4403f5014d4005087d6ff4 |

本人使用完全相同的 `fresh_registration_contract.c`（SHA `258ac0c8d04517501d57d82dcdac4d9b2498be73ada009ea0e19ba2c6123a5df`）重新严格编译。函数地址、create-without-load 流程、`NSO_INVALID` 预期和后续 destroy/未登记断言均未改变。

| 实际命令 | 原始退出 |
| --- | --- |
| 新 Process 严格编译、冻结合同链接 | 0 / 0 |
| 同一 fresh 复现编译/运行 | 0 / **0**（原 cfc 为 **-11** / shell139） |
| 同一独立 registration lifecycle 编译/运行 | 0 / 0 |
| 作者新增 fresh 两阶段合同 | 0；先 create/reject/destroy，再 create/reject/load/register/run/destroy，含输出哨兵 |
| 原正常注册合同 | 0 |
| null / repeat failstop | -6 / -6，shell134，预期保留 |
| provider 宿主明确 unsupported / 普通合同 | 0 / 0 |
| frozen diff --check | 0 |
| 最终冻结/同一复现证据校验 | 0 |

27 条快照 Git 命令均 0；15 条独立编译/执行/校验命令为 **12×0、2×-6、1×1**，无超时。其中退出1仅来自本人第一次收口校验脚本：它枚举到自身当前 pending 记录后访问尚不存在的 `timed_out` 键而 KeyError。已保留原脚本 `verify_repair.initial.py` 和失败日志；修正为显式识别当前 pending 校验及保留已完成历史退出码，最终校验退出0。没有修改实现、合同输入或预期以通过。

所有真实功能合同符合预期。新 fresh 与独立生命周期仍只运行普通宿主 C/POSIX 组件；native fixture 在 Mac 明确不执行 guest ABI。原 cfc 总控的 Query 重入/SCS/live-retained 设备证据保留在上一级报告；本修复没有本人或已核验的总控 Android 执行记录。uncertain callback 分支仍仅源码审核，完整 ELF TLS、HOST 消费者、CRT/游戏及 root 后续组合不在通过范围。

作者18步增量记录仅为来源证据（16×0、2×-6），未冒充本人执行。输入来源、同一源码 SHA、原 cfc 失败与新成功的完整 argv/cwd/输出及真实退出码见 `final-manifest.json` 和 `repair-verification.json`。

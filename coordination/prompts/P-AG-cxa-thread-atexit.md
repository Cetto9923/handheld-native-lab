# 工单 P-AG-15｜Antigravity｜主路径：__cxa_thread_atexit 推进

你是实现 Agent。上一单 P-AG-14 已闭环：HEAD `a03b88b`（分支 `work/rg477m-native`）。真机已越过 posix_memalign/memalign/aligned_alloc/valloc 等对齐分配。新阻塞在线程退出回调登记。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`.local/logs/rg477m-boot-v30-posixmemalign.log`
- 对照已落地：`atexit` / cxx abi / TLS 相关 HLE
- 真机：`adb -s RG477M01445122`；不 root/刷机/清数据；不 push
- 禁止 reset/clean/stash 覆盖；成功后可提交检查点；密钥/.pak/.ucas 不进 git
- 并行：P-CC-3 正在物化 `.pak`/`.ucas` 到 `.local/romfs`——**不要**改素材抽取脚本抢写；可读 romfs，勿删大文件
- 验收不降级；勿动 ProtectRelro 桩除非硬需要

## 当前阻塞
- 符号：`__cxa_thread_atexit`（symbol index **130**，JMPREL 129，GOT≈`0x7772408`，caller≈`0x221e8`）
- exit **133**；日志 `.local/logs/rg477m-boot-v30-posixmemalign.log`
- 阶段：`T02_POSIX_MEMALIGN_PASSED_CXA_THREAD_ATEXIT_REACHED_ON_RG477M`
- 基线：按 Itanium/Android C++ ABI 登记线程局部析构；打可归因 `[hle]` 日志；紧随必现的 `__cxa_thread_atexit_impl` 等可一并最小落地

## 目标
1. 落地最小 `__cxa_thread_atexit` HLE；越过 import 130
2. native_boundary/guest_process 绑定；宿主契约；RG477M `--boot` 同前参数
3. 新日志建议 `rg477m-boot-v31-*.log` + exitcode；更新 HANDOFF/state；`python3 scripts/project_status.py --write`
4. 提交检查点；回报 HEAD/新阻塞/日志；做完停下；不要 ask_user

## 禁止
不声称标题/可玩；不空桩乱过；不下载未授权本体；不抢 P-CC-3 写的 `.local/romfs` 大文件

做完停下汇报，等项目经理下一派工。

# T02-BOOT-ORDER-REUSE — 2026-10-23

**采用 LAS 的模块初始化顺序和 DT_INIT 所有权规则；不能照搬其四参入口。本 SDK 是五参 Start，原样执行仍受 guest thread / TP / CRT 缺口限制。** 无源码、Git、设备操作或绕过补丁。

已读固定 [LAS runtime/main.c](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/runtime/main.c) 的174行及 [MIT 许可证](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/LICENSE)。L14–31、139–145 的生产路径为 SDK callback 在 startup 前、main callback 在建堆后；DT_INIT 存在则由它拥有 array，否则才 array fallback。只复用该决策，不复制 Cpu/guest_call 或不同版本的 ABI。

**本地指令独立确认：** SDK 导出 #1352 是 `_ZN2nn4init5StartEmmPFvvES2_S2_`（+0x110e90、96 B）；LAS 的四参符号不在本 SDK 符号表。x0/x1 原样传给 nnosInitialize；x2/x3/x4 保存后分别在 **110ec8 / 110edc / 110ee4** 无条件 BLR，无 null 检查。24条指令及原 JUMP_SLOT/dynsym 给出的完整顺序是：

```
libc0 → nnosInitialize(x0,x1) → x2() → libc1 → nninitInitializeSdkModule
→ 原 nninitStartup → libc2 → x3() → 原 nnMain
→ x4() → nninitFinalizeSdkModule → nnosQuickExit
```

所以 LAS 的前两个 callback 时机获本版本支持。**额外 x4 的 rtld provider / 完整职责未知**，不能给 null、任意 noop，或把 LAS 实际指 main-init 的变量名 fini_fn 当作它；x0/x1 完整 handle/args-region 协议也未恢复。

**不能直接原样启动的原因：** SDK DT_INIT +0x400 在 +4e0 调 `__nnmusl_init_dso`、+51c ProtectRelro、+544 循环 array（83项）；main 匹配旧切片也有 +5920a94 array BLR（1890项）。不要 DT_INIT 后再补调 array；状态分支细节留机器证据，不自命名返回码。main 的21参数/104 B栈运输、DSO/TLS 与 allocator gate 复用既有 [TLS 报告](../../tls-research/TLS_CALLER_REPORT.md)。新核实 `nnosInitialize +149474 → ThreadManagerC1 +1421c4`，连到 [既有 TP 证据](../../ffrs-integration/docs/analysis/codex_runtime_report.md) 的 +142214（经 TPIDRRO+1f8 写入）与 +147ff0（写 TPIDR_EL0）。正确顺序不会自动提供这些 guest ABI。

**下一决定：** 保留当前原 startup 建堆进度为诊断检查点；正常入口目标采用本版本五参原 Start。先完成真实主线程上下文/参数区、三项 typed callback provider、SDK↔main 绑定及各自 DT_INIT 的 DSO/TLS/构造生命周期，再接入口；不另建手工 startup-first 正常路径。x4 provider、libc1 私有 helper、完整 CRT/TP 适配仍是明确缺口，本单元到此停止，不把另一游戏成功当作本游戏已可启动。

私有证据 `.local/facts.json`、`resolved-plt.json`、`slice-*/`；`verify_boot.py` exit0，7小窗1336 B、14 clang/LLVM命令全0。SDK/main 前后摘要均匹配原始输入（完整SHA见机器表）。未执行 SDK/main/TP、未 ADB；已知 REA helper 故障未重试，本轮是 LLVM 静态证据，不是成功 REA Evidence。

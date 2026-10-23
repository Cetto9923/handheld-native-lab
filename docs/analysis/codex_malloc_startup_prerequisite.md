# T02-MALLOC-FAULT-PROVENANCE — 2026-10-23

**本现场是“独立调用 malloc 时，其依赖的原 startup 全局对象尚无 vtable”，不是已证明的 TLS / Bionic TP 故障。** 原对象写者已定位到 `nninitStartup` 后段的 `0x137f0 → 0xae770`。不应补默认对象或手填 vptr；正确顺序仍需父端在后续正常 startup 路径验证。

原始 main SHA256 前后均为 `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`。完整读取父端 `va-resource-v1-malloc.stderr`、stdout 和 execution.json 的 malloc case，三者摘要已相互核验；副本 / 来源见 `.local/parent-case.json`。该 case 是 `--symbol malloc 4096`，**没有 `--init-wrapper` / `--init-elf`**，真实 exit **139**、非超时；stdout 显示原入口 `0x132d0`、CRT/TLS not initialized。父端明确确认该 case 未先运行 nninitStartup。前面的两次 atexit、Query 和真实 reserve 成功日志，不能证明对象构造已完成。

## 动态观测与回算

日志 PC=`0x6ed1d2b05c`、module_offset=`0xb705c`，故 main base=`0x6ed1c74000`；LR=`0x6ed1cfa264` 回算为 **`0x86264`**，不是 `0x8c264`。现场 `x0=0x6ed9439210=base+0x77c5210` 非零，`x8=0`，fault address=`0x30`。帧中 `0xbe2a8`、`0xb70c4` 与下述真实调用的返回 PC 相符；不对 host ASLR 地址猜符号。

## 已确认的指令 / 数据依赖

| main 相对地址 | 事实 |
|---|---|
| `0x132d0/132d4` | 导出 malloc：`MOV w1,wzr; B 0xb7040`。 |
| `0xb7040/7048` | ADRP / LDR 通过 GOT `0x7767738` 取全局 owner 的地址。原 GOT 值=`0x7c3d9f0`，在 RELR bitmap 项 `0x5ec5510`（base `0x7767718`、bit5）中。现场 x21 也等于 base+`0x7c3d9f0`。 |
| `0xb7050/7054` | `LDR x0,[x8]` 读取 owner；为零才走 guard / lazy initialization 分支。 |
| `0xb7058/705c/7060` | `LDR x8,[x0]; LDR x3,[x8,#0x30]; BR x3`。本次第一条成功读出零，第二条访问 0x30 崩溃。零来自对象首 qword / 虚调用表指针，不是 x0 对象指针。 |
| `0xb70c0 → 0xbe270` | 首次 owner 初始化的调用，保存返回地址 `0xb70c4`。 |
| `0xbe284 → 0x13c64`，`0xbe294` | helper 返回固定地址 `0x77c5210`，随后将其写入 owner。`0x13c64` 只有 ADRP / ADD / RET，本身不构造对象。 |
| `0xbe2a4 → 0x86208` | 对应帧返回 `0xbe2a8`。被调函数 `0x86258` 设置 size=`0x40000`、w1=0，`0x86260 BL 0xb7040`，对应现场 LR=`0x86264`；再次进入虚调用时对象表指针仍为零。 |
| `0x352f150` | 导出 nninitStartup 的单条 B thunk，实际目标 `0x13370`。 |
| `0x137e4..137f0` | startup 准备 x0=`0x77c5210`、x1=0，真实调用 `0xae770`。 |
| `0xae798..ae7ac` | 构造函数取得表地址 `0x67def48`，`STR x8,[x0]` 写对象首 qword。 |
| `0xae7d8..ae7ec` | 同一函数再取得 `0x67de800`，`STR x8,[x19]` 再写首 qword；该表原始 `+0x30` 项=`0xb2910`。不能只跳过构造过程填此值。 |

对象 `0x77c5210` 位于零填充区域，原文件 data end=`0x77c29f8`；不能从原文件伪造读取它的运行时内容。零值结论来自真实 fault 的 load 和寄存器。GOT、全局 owner、固定全局对象与两条真实 store 已闭合；这条直接来源链没有 TP/MRS 读取，x18=0 对本次 fault 无因果证据。表/构造函数的 C++ 类型名未恢复，不杜撰名称。

## writer 前提与下一步最小边界

已取局部 startup 段：`0x137a0` 调 index67 IsVirtualAddressMemoryEnabled；返回 PC `0x137a4` 是 `ORN w8,w20,w0`，`0x137a8 TBNZ bit0 → 0x13820`，继续要求 w20 bit0=0 且返回 w0 bit0=1。随后 `0x137d0` 调 index68 AllocateAddressRegion，`0x137d4 CBNZ w0 → 0x13820`，继续要求真实结果0。`0x137e0` 是 index65 QueryMemoryInfo，返回后到 `0x137f0` 没有另一个分支。父端当前 index67 gate 正位于这些 writer 之前。

因此下一次正常集成应让原 startup 按真实条件前进，确认实际越过 `0x137f0`，并完成原构造流程后再评估 malloc。本次**没有证明完整构造能完成，也没有证明全程序不存在其他写者**；构造函数后续仍调用原接口和分配逻辑，未扩大研究。不能把这个 pre-init case 泛称“所有 CRT 是同一阻塞”，也不能借此宣称 ELF TLS、host TP 或后续 allocator 已兼容。

`.local/facts.json` 为机器结论；`.local/verify_fault.py` 只读复核输入 / 日志摘要、PC/寄存器、stub、调用边和 writer，真实 exit **0**。匹配输入的旧 inspection 复用；新取 10 个小窗口共 2832 B，20 个 clang / LLVM 命令全 exit **0**，每个窗口的 manifest / 原日志 / 逐 PC JSON / absolute.txt 均在 `.local/slice-*/`。原 LLVM 内部地址是切片相对值，归一化文件另标 `actual_address_operand` / branch target；初步探索的错误候选 LR `0x8c1d0` 窗口未用于结论。唯一一次冗余手写目标计算存在 Python syntax error（exit1），未用于证据，正确目标由原指令校验获得。

本角色未运行 main / SDK / TP 写入函数，未 ADB、修改运行时代码或 Git。继续采用已读 REA 技能的事实 / 推断边界；此前已确证 Swift helper 启动失败，本任务没有 doctor/setup 或重试引擎。本报告是 **LLVM 静态证据 + 父端既有真机日志**，不是新的成功 REA Evidence。

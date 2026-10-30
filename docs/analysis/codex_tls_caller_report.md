# main TLS caller 静态检查点

2026-10-23。只读原仓库；只在本报告所在目录写分析产物。未运行游戏、改 TPIDR、改 SDK 初始化标志或重启已被拒绝的 QA/异常输入探测。当前结论来自精确 NSO 元数据、原始指令静态切片和 runtime 代理的已保存 callee 证据；不是完整启动、线程兼容或设备验收。

## 已确认的调用与重定位

- main SHA-256：`6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`。读取前后相同。SDK callee 证据绑定 SHA-256：`9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9`。
- `DT_INIT=main+0x5920950`；`+0x5920a30` 的 BL 经 PLT `+0x5e699d0`、GOT `+0x7772f80` 对应导入 #515 `__nnmusl_init_dso`。回址 `+0x5920a34`。
- 共 21 个机器字参数：x0..x7 加 `[call_sp+0x0..0x60]` 的十三项。完整置换、GOT 源地址、RELR 项与值在 `.local/caller-v4/caller_tls.json`。**全部 21 个源槽都是 RELR 目标；入参为 B+addend。原文件中的 0 和 8 分别传成 B、B+8，不能当空指针或立即数 8。**
- a0=B；a3/a6/a20=B+`0x7767328`，与 `DT_DYNAMIC` 完全相同。MOD0 在 `0x5e6b058`，main 常规映射（含 BSS）结束于 `B+0x9b53000`；这不等于 TLS 块界限。
- a10/a11 对应 PLT GOT 半开区间 `[0x7771fe8,0x7773398)`：627 个 JMPREL 槽加三个头部机器字。a12/a13 为 `DT_RELA` 半开区间 `[0x5e6b098,0x5e82630)`；a16/a17 为 `DT_JMPREL` 半开区间 `[0x5e82630,0x5e860f8)`。a14/a15、a18/a19 各为 B/B；只能确认空区间候选，尚不命名格式。
- a1=B+`0x5e67aa4` 指向 text 内零填充地址，真实用途未知。调用结果写入 main+`0x77a2aec` 的 u32；恰为 1 时跳过后续工作，其他值会走后续路径，不能伪造成功让构造继续。

## TLS 模板：caller 与 callee 的交叉结果

| 项目 | 值 | 证据性质 |
|---|---:|---|
| 模板起点 a2 | `B+0x7767108` | caller 值，callee 写入 DSO 记录 +0 |
| 初始化末端 a3/a6 | `B+0x7767328` | caller 值，callee 计算 a3−a2 |
| 初始化长度 | `0x220`（544） | callee 写入记录 +8 |
| 总末端 a7 | `B+0x7768121` | linker 地址值，callee 计算 a7−a2 |
| 总长度 | `0x1019`（4121） | callee 写入记录 +0x10 |
| 对齐字段 | 8 | callee 取 max(a5−a2,a9−a6)，写入 +0x18 |
| 零填充尾部 | `0xdf9`（3577） | 总长度−初始化长度 |

SDK callee `+0x98e30` 内相对 `+0xb4/+0xbc/+0xcc` 的减法、`+0x1f4/+0x1f8` 长度计算及 `+0x208/+0x210` 存储支持这些字段用法；原始 C 类型和字段名尚未恢复。原先只看 a5 会错丢模板首 8 字节。初始化模板只有 11 个非零字节，SHA-256：`2236311146eecaa733a64cda6108fece94e6693159ba1d3add9bb3f0899834bf`。

总末端是 TLS linker 值，不能从主模块 `[0x7767328,0x7768121)` 继续复制文件数据充当 TLS 尾部；这里已是主模块普通 dynamic/GOT 等数据。模板应独立重建并按字段长度补零。

## 直接 TPIDR 使用与可实现边界

全 main text 的精确 opcode 计数为 1,489 个 `MRS TPIDR_EL0`、零个 `MRS TPIDRRO_EL0`。其中 1,440 处严格匹配 `MOVZ x0;MOVK x0;NOP;NOP;MRS`，可恢复 59 种 x0 常量，范围 `0x10..0x1028`。最常见 `0x250` 929 处、`0x238` 183 处、`0xde0` 101 处。余 49 处已列清单，未作完整寄存器/别名数据流证明。

- main+`0x17040` 读取 TP，`+0x17044` 读取 `[TP+0x240]` 一个字节。
- main+`0x34a0` 函数构造 `TP+0x230` 地址并调用局部构造代码；尾部 `+0x34e8` 经 PLT `+0x5e682e0` 进入导入 #130 `__cxa_thread_atexit`，同时传线程对象地址。因此除了存储，生命周期也有实际代码证据。
- main+`0xbb784` 先读 TP，稍后 `+0xbb79c` 写 `[TP+0x10]` 的 u32，说明 TP 可被提前保存在其他寄存器；简单相邻模式不覆盖所有使用。
- 六个 `TP+0x1028` 常量站点中，五个后续读取单字节（`+0x5209348/+0x52193c8/+0x5219624/+0x521a148/+0x521a354` 是 MRS 地址）；另一个 `+0x5209660` 位于返回该 TLS 字节地址的函数。最后一字节与 `0x10+0x1019=0x1029` 的排他界限吻合。

这些证据支持先实现独立 guest 模板描述符及逐线程模板复制/尾部清零，并验证最小块界限（候选 TP 头部 0x10，8 字节对齐后存储至少 0x1030）；**尚不支持把宿主 pthread、现有 16 槽 PoC 或 Android 的 TP 直接作为此布局**。main 动态符号表 660 项、无 STT_TLS，不能仅凭 dynsym 重建变量名/析构规则；静态模板可恢复存储初值与边界，却不解决 1,489 个内联 TP 读取和 SDK 的另一套线程结构。

下一最小步骤无需设备：完成 49 个非相邻 MRS 用例的有界静态数据流核对；为独立 guest 模板创建输入哈希/复制长度/尾部清零/多线程隔离契约，不绑定游戏导入、不写宿主 TP。真正接入仍需准确线程上下文获取路径、所有直接 TP 访问适配、C++ TLS 析构/DSO 生命周期；不能仅放行 SDK allocator gate。

## 复现与局限

运行 `python3 gather_tls_caller.py --input /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/uncompressed/main.bin --metadata .local/main-v1/inspection.json --out .local/caller-next`，out 必须新建。LLVM 只反汇编 caller 所属的精确合法代码切片；派生字节、对象及日志都留在本目录 `.local/`。完整命令记录见 `COMMAND_LOG.json` 和 `.local/caller-v4/commands.json`。

callee 交叉证据原路径：`../ffrs-runtime/.local/runtime/static-sdk-dso-v1/{inspection.json,slice-98e30.1.stdout.log}`。已读 runtime 独立保存的 `crt-init-v2/sdk-gate/stdout.log`，其 21 参捕获全部 match，原 SDK 未初始化 gate 返回 2 后主动停止；这是其他代理的运行证据，本任务未独立复跑。

研究工具的一次本地脚本首跑因只载入 DT_RELA、未包含 DT_JMPREL 而停止（caller-v1）；修正后 v2/v3/v4 均退出 0。早期人工消息误算过 BL 目的地址，已更正为本脚本从原始指令解出的 `0x5e699d0`。不存在完整初始化或设备 PASS 声明。

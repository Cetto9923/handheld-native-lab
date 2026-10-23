# T02-LOCALE-ABI-REA — 2026-10-23

**结论：这个 SDK 的 `strncasecmp_l` 忽略 locale；当前 `x3=0` 不需要任何 guest locale 对象。Android 最小复用是四参 typed wrapper 丢弃第四参，调用系统 Bionic 的非 `_l` `strncasecmp`。** 不把 guest 值转换为宿主 `locale_t`，也不修改全局 locale。此结论只覆盖下面列出的原始函数和正常有效字符串输入。

输入为原 SDK `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/uncompressed/sdk.bin`，前后 SHA256 都是 `9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9`。复用匹配摘要的 `ffrs-runtime/.local/runtime/static-sdk-dso-v1/inspection.json`（SHA256 `c7fb61c797b2e6a06860c834a767f6a27adad2e1e2c97461ac9877d767c20a59`）。未执行 SDK、main、游戏或设备操作。

## 已确认的静态事实

| 锚点（SDK 相对地址） | 原始事实 / 范围 |
|---|---|
| `0x59c7e0`，172 B / 43 条 | `strncasecmp_l` 弱函数别名与全局 `__strncasecmp_l` 同址同大小。完整体从未引用 `x3/w3`；没有 locale 结构读取。 |
| `0x59c7e0` → `0x59c884` | `CBZ x2` 在任何字符串 load 之前；`n=0` 返回 `w0=0`。 |
| `0x59c7f8`、`0x59c81c` 等 | `LDRB` / `LDURB` 使用无符号字节；`x0/x1` 是字符串指针，`x2` 是 64 位长度。按 n / NUL / 折叠后不等停止。 |
| `0x59c83c/84c/85c/86c` | 四个 BL 均到 `0x64f2f0`。该 PLT 的 ADRP/LDR/ADD/BR 定位 GOT `0xb715b8`；`DT_JMPREL` 项 `0x6b4cd8` 是 `R_AARCH64_JUMP_SLOT=1026`，符号索引 16130 → `tolower`，定义 `0x58dce0`，20 B。 |
| `0x58dce0`–`0x58dcf0` | 五条指令只读取 `w0`：`(uint32_t)(c-'A') < 26 ? c\|0x20 : c`。对来源 unsigned byte，仅 A–Z 折叠，0x80–0xff 不变。`tolower_l` 的 20 B 逐字节相同。 |
| `0x59c870` | `SUB w0,w19,w0`，返回折叠后两个字节的精确有符号差 `[-255,255]`，不限于 -1/0/1。 |
| `strncasecmp` `0x59c730`，172 B | 与 `_l` 的 39 个非 BL 指令逐字相同，另外四个 BL 也解析到同一 PLT。 |

因此 ABI 可表达为 `int32_t(const char *a, const char *b, uint64_t n, uintptr_t ignored_guest_locale)`。这是寄存器和指令的解释；没有恢复或依赖 guest `locale_t` 的真实 typedef。`x3=0` 在这个实现中是未消费值，不能推广到其他 `_l` 函数。父端的 main+`0x13498`、n=`0x1d`、x3=`0` 为父端真机观测，本角色没有重复执行。此静态解析不观察 live GOT / 符号替换。

## 成熟实现复用

首选 Bionic 非 `_l` API。已实际阅读 AOSP 官方镜像 **`android-14.0.0_r1`** 的 [OpenBSD `strcasecmp.c`](https://raw.githubusercontent.com/aosp-mirror/platform_bionic/android-14.0.0_r1/libc/upstream-openbsd/lib/libc/string/strcasecmp.c)：`strncasecmp` 用固定 256 项 ASCII 映射和 unsigned byte，保留高位字节，返回精确差值，并支持零长度。该文件标明 OpenBSD RCS **1.7 (2015-08-31)** 和 BSD-3-Clause 条款。[`strings_l.cpp`](https://raw.githubusercontent.com/aosp-mirror/platform_bionic/android-14.0.0_r1/libc/bionic/strings_l.cpp) 的 `_l` 包装也不使用 locale，转调非 `_l`，文件为 BSD-2-Clause。[构建清单](https://raw.githubusercontent.com/aosp-mirror/platform_bionic/android-14.0.0_r1/libc/Android.bp) 明确列入这两个生产源文件。调用系统 API 无需复制这一算法或引入新库。

作为交叉核对，musl 的 [`strncasecmp.c`](https://git.musl-libc.org/cgit/musl/tree/src/string/strncasecmp.c)（已显示 Git blob **`e0ef93c20de1a9e0a6b8f4a4a951a8e61a1a2973`**）具有相同 unsigned-byte / n / NUL / `tolower` 差值循环，`__strncasecmp_l` 也直接忽略 locale。此处是结构相符，**不是 SDK 源码归属证明**。本次没有完成 musl 固定发行版及许可证文件的在线核验，因此不建议本轮新增 vendor 副本。Mac fixture 应明确用 ASCII / u8 的期望值，不能把任意宿主 libc 的 locale 行为当作 Android 证据。

限制：AOSP tag 内容读取成功；将该 tag 解析成完整不可变 Git commit 的 API 请求因本地 DNS 失败，Gitiles tag 页面也不可访问，未虚构 commit。RG 当前系统 libc 的精确 build/revision 及实际调用结果由后续集成验证；本角色 Android **NOT_RUN**。下一步仅需 runtime 把已有导入的四参签名接入 Bionic 非 `_l`，在现有普通合同中覆盖 n=0、边界截断、NUL、ASCII 大小写、0x80/0xff 和精确正负差；不要扩充 locale 管理器。

## REA 实际动作与阻塞

已读取 reverse-engineer-anything **SKILL.md v42** 及 native/connection guides；实际 MCP 工具发现为 0。实际调用已安装公共 **REA CLI 6.5.0** `function` / **Ghidra 12.1.4**，目标是明确从合法本地 SDK 提取的 **172 B raw-binary slice**，ARM64，入口 VA=`0x59c7e0`；该切片不含外部调用依赖，不冒充完整 SDK 分析。

55 秒限时内实际 **6.957 s，exit 1，非超时**。Ghidra 启动前的 `ProcessRunTokenReader` Swift helper 编译失败：默认 `~/.cache/clang/ModuleCache` 写入遭 sandbox `Operation not permitted`，并报告 Swift 编译器 / SDK 版本不匹配。因此**未产生成功的 REA 函数 Evidence / snapshot**；未 doctor、安装、修改全局配置或盲重试。这是明确的工具执行失败，不是自动审批拒绝，也不是“分析完成”。上面的函数事实全部归因于 LLVM 反汇编 + 原始动态重定位读取。REA 完整 stdout JSON 为 `.local/rea-function/stdout.json`（9051 B），stderr 为 0 B，命令 / 环境 / PID / 限时 / 真实退出见同目录 `command.json` 和 `run.json`。

复现与私有证据：`.local/verify_slice.py` 只读校验原输入、四个切片、43 个逐 PC 指令、BL 目标、PLT 和重定位，真实 exit **0**；输出 `.local/verified-facts.json`，日志 `.local/verify-run.json` / `verify.stdout.log` / `verify.stderr.log`。四次自有 `.S`→object 和四次 LLVM disassembly 均 exit **0**，完整命令见 `.local/llvm-commands.json`。原始游戏切片、对象和反汇编全文全部留私有 `.local/`，不提交。无运行时代码改动；无需再次处理 Info16/17。

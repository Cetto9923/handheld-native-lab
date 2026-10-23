# T07-MATH-IMPORT-REVIEW — b19706c4

结论：限定 `logf` 增量 **PASS，无需修复的功能缺陷**。冻结为 `b19706c4cfc9fdd3246d85a12c880f922ae346d3`，基于已审 `ff964e9d479e288e2b695b5100836def780ca4df`。本人 Android 编译与执行均 **NOT_RUN**；设备结果单独归因总控。未读取或执行原 main/SDK/游戏、`test_loader.py`、旧停止的异常输入流程，未运行作者完整 builder，未操作 ADB、源码或 Git。

## 冻结与范围

从 Git 独立取得 25 个源文件，四个增量 SHA 与作者 freeze 一致，其余 21 个与已审 ff 快照逐字节一致。复用并核哈希的九个自有宿主依赖对象来自已审 3db/ff 构建；只重新严格编译新 Process 和合同，未重做 owner/cleanup 系统 QA。全部摘要、真实 argv/cwd、原始退出码、日志在 `final-manifest.json`。

| 增量文件 | SHA-256 |
| --- | --- |
| src/runtime/hle/guest_process.c | dc7bb53971e08de41a4bb2eddd31b3deb1663a6e3cb8e1c95c33f181e79dd1f7 |
| tests/native/guest_process_contract.c | a5629c8e49a2fcccb603356f21e37752ed84fc5ec22ecdfd6ebc7d522d62518e |
| scripts/runtime/build_guest_process_contract.sh | e12cfeaf5cf15aee9f2d860cf707597889d9c62573a170e96fea577845114ea3 |
| docs/analysis/codex_guest_process_report.md | 231bd92083fc0115fe5cd00624a411ce14ea90729473abfc60f2983e5e6e0e4d |

作者 freeze SHA-256 为 `f0ef1653d4996e1c8bfbd28723e004e262ee9b97b6bf9801e3863dc6bfb69175`。源码依赖为本冻结中与已审版本相同的 3db Context/pages/region、5c boundary、9c OS TLS，未混入下一阶段代码。

## 源码与 ABI 核对

- `guest_process.c:516` 的 `PURE(logf)` 只是返回 `<math.h>` 的真实 `&logf`，没有自制算法或 compiler pure 属性。未知 `log`/其他数学名称仍返回 0。
- `guest_process.c:553` 专门设置 register-only、FP 参数/返回各 1；其余聚合初始化成员为 0，故 GP 参数/返回均为 0。`prepare_boundary_locked` 仍按 typed signature 绑定并整体 retarget，没有原始地址执行回退。
- 合同 `guest_process_contract.c:731` 起包含十个正数/边界值和五个 IEEE 系统行为观察。`build_guest_process_contract.sh:458` 起的自有 fixture 以对象地址作为整数入口参数，实际 `LDR s0 → BLR x16 → STR s0` 传递浮点值；返回整数 0x91 是独立的 fixture 状态。输入布局、guest x18 和结果均有检查。
- 独立反汇编复制的自有 AArch64 对象，并核对已冻结正向 `logf.nso` 对应指令字；Android normal ELF 确有 `UND logf` 和 `DT_NEEDED libm.so`。未把 CLI 整数参数当浮点参数。
- 仅复核既有 `facts.json` 元数据（SHA `70665159637b16be7e58395950dcc596d7158ab739f65a94d3a9c4fd1717228b`）：SCVTF s0,w19、PLT/GOT/JMPREL 指向 symbol153 logf，返回 s0 后 FADD/FCVTZU；本人未重新读取原模块验证这些字节，也未扩展支持其他数学服务。

## 本人实际执行

27 条快照 Git 命令和 12 条独立编译、测试、离线检查命令均原始退出 `0`，无超时。Clang 21，`-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`；新增自有 FP 合同还用 `-fno-builtin-logf`。每条完整命令与 stdout/stderr 均已落盘。

| 执行内容 | 原始退出 / 覆盖 |
| --- | --- |
| 严格编译新 Process、链接冻结普通合同 | 0 / 0 |
| `contract --logf` | 0；10 正数/边界值、5 IEEE 观察 |
| `contract --logf-native fixtures/logf.nso` | 0；Mac 明确 UNSUPPORTED，native_calls=0，输出哨兵未伪造 |
| `contract fixtures/contract.nso` | 0；普通组件合同 |
| 自有 `fp_identity_contract.c` 编译/运行 | 0 / 0；解析地址等于系统 logf；18 个运行时位模式输入（含 1 附近、正常/次正常边界及 IEEE 类别），有限/无穷结果位一致，NaN 类别一致，输入和 rounding mode 不变，未知数学名拒绝 |
| diff --check、fixture 反汇编、Android ELF 检查、编译器身份、证据校验 | 全部 0 |

这证明本机绑定与组件行为；不是对整个 libm 的精度、所有浮点控制状态或 Android ABI 的本人执行认证。

## 总控 RG477M 证据（本人未执行）

核对 `ffrs-integration/.local/device/rg477m-session1/math-runtime-v1-{plan,stage,execution}.json`：17 条范围内源记录、7 条组件依赖摘要、两个 ELF 和两个自有 fixture 的本地及已 stage 摘要均匹配本冻结。`test_loader.py` 只见来源清单摘要，内容未读、未执行。三个组件的 stdout/stderr 另复制并核对 execution 所记哈希；info 与原 startup 仅消费执行元数据。

| 总控实际 case | 原始退出 / 结果 |
| --- | --- |
| logf-system-contract | 0；70.255 ms |
| logf-native | 0；62.918 ms，15 次真实 FP native 调用、errors=0 |
| normal | 0；78.736 ms |
| info | 0；151.093 ms |
| startup-empty / startup-nonempty | 133 / 133；到未支持 import69 `SetMemoryAllocatorForThreadLocal`，均无超时，不算启动通过 |

共 **3 个普通组件合同及 info 各 0，另 2 个定位实验各 133**。normal ELF 为 977552 B / `7e00f3407b9523def4d40ac8976d994c55e7e0561793be9cab64de520a432b94`；CLI 为 807712 B / `3203929acbd626a7e13df5ca4a1e4ceb1bee08fd1c0324c7700c1ab1eaaa89d3`；自有 logf fixture 为 9216 B / `fe758ca12a33e7c63db89dfd689034bc60dd09ac062be3052aacbe403eef3667`。总控 main 前后摘要记录一致，未由本人读取 main 文件。

旧 unresolved-import 诊断的 caller_offset 出现 unsigned underflow，不能当真实 guest offset；总控另修输出边界，该修复不在本冻结。既有直接 pre-init malloc139 仍保留在前期证据，未重跑或改称成功。作者 101 条命令仅作来源证据：原始分布为 89×0、7×1、3×-6、2×2，不冒充本人执行或全为 0。当前结论不外推完整 CRT、TLS 初始化、游戏启动或帧率。

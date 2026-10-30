# NSO 静态结构解析报告（总控直做证据）

- 产出角色：总控（Claude Code）— 为 T02 行为基线准备加载器基础设施
- 日期：2026-10-23
- 工具：`src/runtime/nso/nso_probe.c`（自研，clang -O2，Apple clang）
- 输入：`.local/uncompressed/main.bin`、`.local/uncompressed/sdk.bin`、`.local/uncompressed/subsdk0.bin`、`.local/exefs/main.npdm`
- 原始输出：`evidence/coordinator/nso_probe_main.txt`、`nso_probe_sdk.txt`、`nso_probe_subsdk0.txt`
- **性质**：静态字节观察。不执行目标代码，不产生运行时行为结论。

---

## 0. 一句话结论

本作 `main` 模块（125.6 MB，UE5.6.1）的**平台耦合面已被完全量化：651 个导入符号，横跨 24 个 nn:: 命名空间 + libc + CRT**。
这意味着"平台兼容层"不是"重写整个 Switch SDK"，而是一份**约 430 个 nn:: 函数的可枚举清单**；
真正的硬骨头不在游戏代码，而在 **NVN→Vulkan 的图形落地** 与 **UE5.6.1 引擎层本身**（RHI / IoStore 资源 / Vulkan 着色器）。

---

## 1. NSO0 模块头（`main`）

| 项 | 值 |
|---|---|
| 文件大小 | 125,577,976 B (0x77C2AF8) |
| flags | 0x38（三个段均未压缩；此文件是 hactool 的 unpacked NSO） |
| seg0 (.text) | file 0x100 / mem 0x00000000 / size 0x05E6A200 (99.0 MB) |
| seg1 (.rodata) | file 0x5E6B100 / mem 0x05E6B000 / size 0x00969B44 (9.87 MB) |
| seg2 (.rwdata) | file 0x67D5100 / mem 0x67D5000 / size 0x00FED9F8 (16.7 MB) |
| .bss | 0x02390608 (37.4 MB) |
| 模块镜像总大小 | 0x9B53000 (162,869,248 B) |
| build id (BE) | `736837AFD390442ED7C74A7DE730E71B`（与 T00 报告一致） |

### 1.1 BuildInfo / MOD0（.rodata 起始）

```
.rodata+0x00 : 01 00 00 00 | 58 00 00 00 | 8c 00 00 00 | 00 00 00 00
.rodata+0x10 : 'A' + 构建路径 "E:\workspace\mucha\mucha\Binaries\Switch\FFRS-Switch-Shipping.nss"
.rodata+0x58 : 'MOD0' + build_id[0x20] + 模块字段
```
构建路径是**直接证据**：项目代号 `mucha`，目标工件 `FFRS-Switch-Shipping.nss`。

---

## 2. 重定位表（自研扫描器定位，非硬编码）

| 项 | 值 |
|---|---|
| 位置 | file 0x5E6B198 = .rodata+0x98（扫描检出，4612 条连续 24 字节记录） |
| 条目数 | 4612 |
| 类型分布 | ABS64 3972 / JUMP_SLOT 627 / GLOB_DAT 13 / ABS32 0 / **RELATIVE 0** |
| 符号索引范围 | 1 .. 651 |
| 引用位置 | 全部落在 .rwdata（0x67D9278 … 0x7773390） |

**RELATIVE = 0 是一条关键事实**：该模块 `.text` 完全位置无关（ADRP/ADD 相对寻址），
所有绝对地址修补都通过"导入符号槽"完成。也就是说 **`.text` 可以被映射到任意基址而无需修补**，
平台层只需要为 651 个符号提供落点。

---

## 3. 动态符号表 / 字符串表（来自 NSO 头字段，非猜测）

NSO 头 0x90/0x98 两组 u64 为 `(size << 32) | offset`：

| 表 | offset（相对 .rodata 起点） | size | 文件偏移 | 条目 |
|---|---|---|---|---|
| dynstr | 0x5FE48 | 0x623A (25,146 B) | 0x5ECAF48 | — |
| dynsym | 0x5C068 | 0x3DE0 (15,840 B) | 0x5EC7168 | 660 |

- 基址 = **.rodata 起点**（由 dynstr 可打印率 97% 实测确认；探针会尝试多个候选基址并报告命中者）。
- dynsym 结构为 ELF64 Sym（24 字节：st_name u32 / st_info u8 / st_other u8 / st_shndx u16 / st_value u64 / st_size u64）。
- 分布：**st_shndx == 0（未定义 = 导入）651 条；已定义（本模块导出）8 条**。

### 3.1 `main` 的 8 个导出符号（含入口点）

| # | 名称 | 模块内地址 | 大小 |
|---|---|---|---|
| 652 | `nnMain` | **0x352F160** | 0x90 |
| 653 | `nninitStartup` | 0x352F150 | 0x4 |
| 654 | `__nnDetailNintendoSdkRuntimeObjectFile` | 0x1090 | 0x4 |
| 655 | `malloc` | 0x132D0 | 0x8 |
| 656 | `free` | 0x132E0 | 0xC |
| 657 | `calloc` | 0x132F0 | 0x44 |
| 658 | `realloc` | 0x13340 | 0x8 |
| 659 | `aligned_alloc` | 0x13350 | 0x10 |

> **`nnMain` @ 0x352F160 就是本作的入口点**（被 `sdk` 模块的 `nnMain` 调用）。
> 后续 T02/T04 应以此地址为启动锚点。

---

## 4. 导入面量化（651 个符号，完整清单见 `nso_imports_main.tsv`）

| 命名空间 | 导入数 | Android 对应物 |
|---|---|---|
| `libc/C++` | 209 | libc：math/string/stdio + C++ 运行时 |
| `nn::os` | 73 | pthread / 原子操作 / 时钟 |
| `nn C-API` | 69 | `nnos*` / `nnsocket*` / `nnssl*` / `nnResult*`（C 封装） |
| `nn::audio` | 52 | AAudio / OpenSL ES |
| `nn::fs` | 43 | AAssetManager / POSIX 文件 |
| `nn::socket` | 41 | BSD sockets |
| `nn::hid` | 31 | Android InputDevice / 手柄 |
| `nn::oe` | 25 | Activity 生命周期 / 焦点 / 性能模式 / 录制 |
| `nn::codec` | 24 | MediaCodec |
| `CRT` | 15 | `__cxa_*` / `__gxx_personality` |
| `nn::web` | 15 | WebView |
| `nn::time` | 7 | clock_gettime |
| `nn::vi` | 7 | ANativeWindow / Surface（**NVN 的落地层**） |
| `nn::nifm` | 6 | 网络接口 |
| `nn::swkbd` | 6 | 软键盘 |
| `nn::account` | 5 | 账号（本作用途待确认） |
| `nn::ro` | 4 | 模块注册 / RELRO |
| `nn::ssl` | 4 | TLS |
| `nn::diag` | 3 | 日志 |
| `nn::aoc` | 3 | DLC / AddOnContent |
| `nn::util` | 3 | UUID |
| `nn::ldn` | 2 | 局域网发现 |
| `nn::err` / `nn::mem` / `nn::crypto` / `nn::nsd` | 各 1 | 错误码 / 内存 / 加密 / DNS |
| **合计** | **651** | |

**关键观察**：导入面中**没有 NVN 符号**。NVN 不由游戏直接调用，
而是经 `nn::vi`（Display/Layer/Vsync）落到 **Nintendo SDK 模块 `sdk`** 内部。
游戏本身对图形 API 的依赖被 `nn::vi` 这 7 个函数完全收敛。

---

## 5. 模块图（三个 NSO 的导入/导出关系）

| 模块 | 大小 | 导入数 | 导出数 | 导入来自 |
|---|---|---|---|---|
| `main` | 125.6 MB | 651 | 8 | libc + nn::*（其中 nn::fs/os/vi/oe 等由 `sdk` 提供） |
| `sdk` | 12.1 MB | **8** | **23,033** | `free/malloc/calloc/realloc/aligned_alloc/nnMain/nninitStartup/__nnDetailNintendoSdkRuntimeObjectFile` |
| `subsdk0` | 5.6 MB | 663 | 9,447 | 主要来自 `sdk`（`nn::ro`/`nn::os`/`nn::sf::hipc`/`nn::lmem`/`nn::diag`） |

**推论（推断，非直接证据）**：Switch 平台耦合**集中在 12 MB 的 `sdk` 模块**（Nintendo SDK 20.5.17：nnSdk / NVN / libz），
125 MB 的 UE5.6.1 游戏代码对平台的直接依赖只有 651 个符号。
这改变了工作量估计的分布：兼容层的主要成本在 `sdk` 侧，而不是游戏侧。

### 5.1 构建标识串（直接证据）

| 文件 | 字符串 |
|---|---|
| `main.bin` | `SDK MW+EpicGames+UnrealEngine-5_6_1` |
| `main.bin` | `SDK MW+Nintendo+NintendoSDK_libcurl-20_5_17-Release` |
| `sdk.bin` | `SDK MW+Nintendo+NintendoSDK_NVN-20_5_17-Release`、`..._libz-...`、`NintendoSdk_BuildTarget-64Bit`、`..._nnSdk-...` |
| `subsdk0.bin` | `SDK MW+Nintendo+NintendoSDK_movie-20_5_17-Release` |

> **引擎版本 = UE 5.6.1（直接证据）**，T00 报告的"UE 5.4"是基于特征符号组合的**推断**，被本证据推翻。
> 另：`sdk`/`subsdk0` 是 **Nintendo SDK 组件**，不是 UE 引擎模块。

---

## 6. 内核服务面（来自 `main.npdm` 的 ACI）

ACI 列出 **49 个内核服务**：
`acc, aoc, apm, appletOE, audin, audout, audren, banana, bcat, bsd, bsdcfg, caps(su/u), csrng, ectx, friend,
fsp-srv, hid, htc, htc:tenv, htcs, hwopus, irs, ldn, ldr, mii, mm, mnpp, nfc(mf/user), nfp, ngc, ngct, nifm, nim, notif,
nsd, ntc, nvdrv, pcm, pctl, pl, prepo, set, sfdnsres, ssl, time, vi`

这些是 IPC 服务端点；`sdk` 通过 `nn::sf::hipc` 与它们通信。
在 Android 上，这些服务要么映射到系统 API，要么整体省略（网络/好友/商店类通常不需要）。

---

## 7. 证据边界与未决项

- 本报告全部为**静态观察**；没有任何目标代码被执行。
- **未决**：`hashes` 表位置未定位（NSO 头未给出该字段）。由于 dynstr/dynsym 已提供符号名，
  本项目的加载器不需要 hashes（真实 Switch 内核按 hash 解析，本项目按名字解析）。
- **未决**：`subsdk0` 的 663 个导入只做了归类，未逐项确认。
- **未决**：8 个导出中 `malloc/free/calloc/realloc/aligned_alloc` 是 `sdk` 反向依赖游戏实现的分配器
  （`sdk` 只导入这 8 个，其中包含 `nnMain`/`nninitStartup`）——这是**模块启动顺序**的直接证据：
  `main` 导出分配器与 `nnMain`，`sdk` 导入它们，说明 `sdk` 依赖 `main`（或反之），
  实际链接/加载顺序需在 T04 确认。

---

## 8. 复现命令

```sh
clang -O2 -Wall -Wextra -o /tmp/ffrs-build/nso_probe src/runtime/nso/nso_probe.c
/tmp/ffrs-build/nso_probe .local/uncompressed/main.bin --imports
```

（`.local/` 为私有工件目录，不入 Git；探针源码在 `src/runtime/nso/`，属总控写入范围。）

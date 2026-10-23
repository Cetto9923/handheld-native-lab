# NSO 原生加载器与「链接基址 = 0」发现（T02-PREP）

- 产出角色：总控（Claude Code）
- 日期：2026-10-23
- 代码：`src/runtime/nso/nso_loader.{c,h}`、`src/runtime/nso/nso_call.c`、`src/runtime/nso/nso_probe.c`
- 输入：`.local/uncompressed/main.bin`（125,577,976 B）
- 平台：macOS arm64（Apple clang，10 核 / 24 GB 宿主）
- 性质：**已在宿主上真实执行游戏二进制内的原生 AArch64 代码**（无解释器、无 JIT、无转译）；所有地址/结构结论均为静态字节观察。

---

## 1. 加载器做到了什么

`nso_load_file()` 一次调用完成：

| 步骤 | 结果 |
|---|---|
| 解析 NSO0 头 | 段表、build id、dynstr/dynsym extents 全部读出 |
| 映射模块镜像 | **单个连续映射，162,869,248 B**（text 0 / rodata 0x5E6B000 / rwdata 0x67D5000 / bss 0x77D29F8+0x2390608） |
| 拷贝三段 | 按模块内偏移落地，.bss 由匿名映射清零 |
| 导入解析 | **651 个未定义符号**按名字绑定：能 `dlsym` 到宿主 libc 的直接绑定（memset/memcpy/malloc 类、math、stdio），其余落到记录式 trap |
| 重定位 | **4612 条**（ABS64 3972 / JUMP_SLOT 627 / GLOB_DAT 13），逐条写入 |
| 保护位 | 数据段先 mprotect，**文本段最后** mprotect(R+X)，使跨段共享页归文本段所有 |

**已验证可执行**：
- build id 输出 `736837afd390442ed7c74a7de730e71b`，与 T00 报告**逐字节一致**。
- 调用模块自身导出函数（模块内偏移 `0x1090`，`__nnDetailNintendoSdkRuntimeObjectFile`）→ 正常返回 `0x0`，退出码 0。
- 调用导出函数 `malloc`（`0x132D0`）→ 进入模块内部实现后在 `0xB7050` 触发 SIGSEGV（见 §2）。
- 调用游戏入口 `nnMain`（`0x352F160`）→ 在模块内偏移 `0x27830` 触发 SIGSEGV（见 §2）。

> 这是本项目**第一次在非 Switch 宿主上原生执行本作游戏二进制里的真实代码**，也是 T02 行为基线的载体。

---

## 2. 核心发现：本作模块的**链接基址是 0**，且**没有 RELATIVE 重定位**

### 2.1 证据链

1. **重定位表里 RELATIVE = 0**。4612 条全部是导入型（ABS64 / JUMP_SLOT / GLOB_DAT），没有一条用于修补模块内部的绝对指针。
2. **.data 里的绝对指针没有对应重定位**。例如模块内偏移 `0x7767738`（文件 `0x7767838`）存放 `0x7C3D9F0`（一个 .bss 内部偏移），重定位表中**查无此 r_offset**；相邻槽位 `0x7779378 / 0x7C53500 / 0x7774144 / 0x7C05FE1` 同理。
3. **代码把这种值当绝对地址直接解引用**。`0xB7040` 处：
   ```
   0xB7040  adrp  x8, 0x7767000        ; PC 相对，得到模块内 .data 页
   0xB7044  movz  w2, #0x2a
   0xB7048  ldr   x8, [x8, #0x738]     ; 读到 0x7C3D9F0（未重定位的模块内偏移）
   0xB7050  ldr   x0, [x8, #0x0]       ; ← SIGSEGV：把模块内偏移当绝对地址解引用
   ```
   故障 PC = 模块内 `0xB7050`，故障地址 = `0x7C3D9F0`。
4. **`nnMain` 跳到绝对地址 `0x27830`**。调用游戏入口后 SIGSEGV，`pc == fa == 0x27830`：
   代码取出一个模块内偏移并 `br` 过去，**该偏移被当作绝对地址使用**。
   → 除非模块被映射在 **0**，否则该跳转必然落到未映射地址。
5. **.data 中约 74 万个 u64 落在模块自身地址范围内**（16 MB–152 MB 桶里密集分布，与 text/rodata/data/bss 四段布局一一对应）；导出函数 `nnMain` 的模块内偏移 `0x352F160` 就以原始偏移形式存放在 `.data+0x7767720`。这与「链接基址 0」一致，与「基址 B>0」不一致（后者会在 B 附近形成密集簇，实测没有）。

### 2.2 结论

> **本作的 ARM64 代码与数据假定模块被加载在虚拟地址 0。**
> Nintendo Switch 的加载器把游戏 NSO 映射在 0；因此该模块**完全不可重定位**，
> Nintendo 也不需要为它生成 RELATIVE 重定位（宿主上零成本）。

### 2.3 对宿主的直接后果

macOS 实测（`mmap` 探针）：

| 尝试 | 结果 |
|---|---|
| `mmap(NULL, ...)` | OK → `0x1010d4000`（用户态最低 `0x100000000`） |
| `mmap((void*)0, MAP_FIXED, ...)` | **请求被忽略**，返回 `0x1010d4000`（未落在 0） |
| `mmap((void*)0x1000, MAP_FIXED, ...)` | EINVAL |
| `mmap((void*)0x4000 / 0x10000, MAP_FIXED, ...)` | ENOMEM |

即 **macOS 无法把镜像映射到 0**。Linux/Android 同理（`vm.mmap_min_addr` 典型值 32768 / 65536，0 地址被拒）。

**因此「把原模块原样搬到 Android 跑」这条路（路线 P2）在普通宿主上被一个地址布局事实卡住。**

---

## 3. 这对路线的含义与候选出路

| 出路 | 说明 | 状态 |
|---|---|---|
| **A. 映射到 0** | 宿主不允许 | **已被实测排除**（macOS；Android 待实测，但 `mmap_min_addr` 使 0 不可映射） |
| **B. 重新基址（rebase）** | 把 .data/.rodata 里所有「模块内偏移形态的指针」加上 B | **可做，但缺一个可靠的指针集合**：NSO 不提供 RELATIVE 重定位，指针集合必须推断（候选约 74 万个 8 字节槽）。需要额外证据（例如 UE 自身反射/序列化元数据、vtable 布局特征）把误伤压到可接受 |
| **C. 静态重编译（P0/P1）** | 用 UE 源码重新编译，指针天然变成 PC 相对 | 取决于 **UE 5.6.1 源码可获取性**（Epic 私有仓库 + EULA），由 Agent A 核实中 |
| **D. 只做主机侧行为基线** | 映射到 0 不可行 ⇒ 退一步：在**允许低地址映射**的环境里取参考输出 | 备选：QEMU-user 风格的固定地址沙箱（属"模拟器"范畴，本项目不接受）——**不采用** |

**当前最合理判断**：路线判定必须把「**原模块不可重定位**」作为一等约束写进去。
- 若 UE 5.6.1 源码不可得（P1 被否），则 B 方案（带可靠指针集合的 rebase）成为 P2 路线唯一的存活形态，
  且其难点从「平台 API」转移到「二进制级指针重定位」。
- 若源码可得（P1 可行），则 rebase 的必要性消失。

> **这一条必须写进 T01 的路线判定**：它同时影响 P1 与 P2 的取舍，
> 且**不能**用「先跑起来再说」绕过——因为它的失败模式是运行中随机崩溃，而不是编译期报错。

---

## 4. 已知的下一步实验（按性价比排序）

1. **确认基址假设的第二条独立证据**：在 `main.bin` 中查找 Nintendo 的运行时 ModuleHeader 写入点
   （`main.npdm` 的 `text_offset = 0x1400000` 字段需权威解码；`.text` 首地址 `0x100` 处的前两条指令是
   `b 0x5E6B058` / `b 0x5E6B08C`，正指向 .rodata 的 MOD0 块与 `+0x8C` 字段——这很可能是模块元信息入口）。
2. **指针集合的可自动化提取**：尝试用 UE 自身的**反射/资产元数据**或 **vtable 形态**约束，把候选从 74 万降到可信集合。
3. **段内自洽性检查**：统计 .data 中「模块内偏移形态」的值落在各段的分布，与段的实际用途对齐，识别出明显不是指针的类别
   （例如恰好等于小整数的槽）。

---

## 5. 复现命令

```sh
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.claude/worktrees/laughing-cray-3464b0
clang -O2 -Wall -Wextra -o /tmp/ffrs-build/nso_probe src/runtime/nso/nso_probe.c
clang -O2 -Wall -Wextra -o /tmp/ffrs-build/nso_call  src/runtime/nso/nso_loader.c src/runtime/nso/nso_call.c -ldl

L=/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local
/tmp/ffrs-build/nso_probe $L/uncompressed/main.bin --imports
/tmp/ffrs-build/nso_call  $L/uncompressed/main.bin --info
/tmp/ffrs-build/nso_call  $L/uncompressed/main.bin --call 0x1090      # 正常返回
/tmp/ffrs-build/nso_call  $L/uncompressed/main.bin --call 0x132d0 4096 # SIGSEGV @0xB7050
/tmp/ffrs-build/nso_call  $L/uncompressed/main.bin --call 0x352f160    # SIGSEGV @0x27830
```

（`.local/` 为私有工件目录，不入 Git；加载器源码在 `src/runtime/nso/`，属总控写入范围。）

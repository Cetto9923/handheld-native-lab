# SDK 线程退出 hook：安装与普通返回顺序

本报告 supersedes `TLS_SDK_LIFECYCLE_REPORT.md` 中两处表述：**`nnmuslThreadPointerInitializer/Destroyer` 是 GLOBAL OBJECT 可空 hook，并非 ELF STB_WEAK**；普通新建线程返回路径的 OS 槽析构与 ELF drain 顺序现已闭合。其他异常/进程退出路径仍未证明。

全部为匹配原输入的普通 LLVM/元数据静态证据，非 REA Evidence；没有执行 SDK、写宿主 TP、修改其他工作树或操作设备。新增产物仅在 `.local/exit-hook-review-v1/`。机器结论 `exit-hook.json`，逐 PC 与原日志摘要在 `normalized/`，实际命令和失败记录在 `commands.json`。复用既有切片；新增 4 个限定切片合计 1280 字节。10 个证据窗口的 **790 条指令行**已逐字节核对原输入，包括重叠行。

原输入位于 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/uncompressed/`；前后哈希相同：

| 输入 | SHA256 |
|---|---|
| main.bin | `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5` |
| sdk.bin | `9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9` |

下述地址均为 SDK 模块相对 PC。

## 重定位与安装赋值

| 对象 | dynsym | st_info | GOT | RELA entry/type/addend |
|---|---:|---:|---:|---|
| Initializer `b88c38` | 4642 | `0x11` | `b68b70` | `671e38` / 1025 GLOB_DAT / 0 |
| Destroyer `b88c40` | 5566 | `0x11` | `b68b78` | `6734e8` / 1025 GLOB_DAT / 0 |

`st_info=0x11` 是 STB_GLOBAL + STT_OBJECT。符号是 8 字节 BSS 对象；可空调用由代码的 CBZ 证明，不能把“可空”称为 ELF weak binding。

未导出名称的安装函数结构范围 `[99930,99bb8)`，有来自 `__nnmusl_init_dso` 的三个直接调用点 `99660/996c0/997d8`。其 `99950/99954/99958` 读取并比较 `mem64(SDK_B+af0c68)` 与 input x0；相等分支进入 `999e8` 并最终安装，普通不等分支在 `999e4` 尾调 `9a080`。

Destroyer 的赋值链无中间调用：

```text
99b44 ADRP x12 -> SDK_B+b68000
99b78 LDR  x12,[x12+b78] -> GOT b68b78 中的对象地址
99b94 ADR  x11 -> SDK_B+9ae40
99ba0 STR  x11,[x12]     -> 安装 destroyer hook
```

Initializer 同段 `99b70` 生成 `9ad20`，`99b8c` 写入经 GOT `b68b70` 取到的对象。按本 SDK 符号定义解析 GOT、且不被独立覆盖的前提下，destroyer 对象 `b88c40` 因此保存 `SDK_B+9ae40`。

`__nnmusl_fini_dso@99bc0` 的一个可见清理分支在 `99e98/99e9c/99ea0` 经同一 GOT 将 destroyer 清零；initializer 在 `99e8c/99e90/99e94` 清零。这里没有把该分支外推为每次 fini 调用必经。

## 普通新建线程的 body 指针闭合

复用 `ThreadManager::CreateThread@141a68`：`141b6c` 的 ADR 将 `141c80` 放入 x2；`141b84` 经 PLT `64c1f0`（GOT `b6fd38`、JUMP_SLOT entry `6b0358`）调用导出 `ThreadManagerImplByHorizon::CreateThread@1480c4`。

该实现 `1480dc` 取 ThreadType 的 `+50/+58` 两字段，`148100` 求和，`148108` 将传入 body x2 存入：

```text
mem64(mem64(T+0x50) + mem64(T+0x58) - 0x20) = SDK_B+0x141c80
```

`1480f0` 生成外壳入口 `148420`，随后在 `148118` 发起创建调用。外壳 `148464/148468/14846c` 从相同字段与栈记录取回 body，`148470: BLR x8` 调用它。这闭合的是**成功的普通 SDK 创建/启动，且 ThreadType 字段和该栈记录未被另改**的路径，不是假定任意间接调用都指向此 body。

## 普通返回的直接顺序

| 次序 | PC | 行为 |
|---|---|---|
| 1 | `141cec` | body `141c80` 在状态 3 时调用用户入口，参数来自 T+60、函数来自 T+68 |
| 2 | `141d04` | 调 OS `InvokeTlsDestructors@144cf0`，采用前报告精确确认的最多 5 轮策略 |
| 3 | `141d18/141d1c/141d38` | 设置状态 4、广播并通知等待者 |
| 4 | `141d58` | 尾调 Leave，普通返回到外壳 `148474` |
| 5 | `148474..148484` | 经 GOT 加载当前 destroyer；`148480` 为零则跳过，否则 `148484: BLR x8` |
| 6 | `9ae68` | 已安装的 `9ae40` 在其 `9ae5c` 内部全局门控非零时调用 ELF LIFO drain `98b60`；后者在摘链/解锁后于 `98bcc` 调析构回调 |
| 7 | `148488` | hook 普通返回后调用 `1755bc`；该目标静态字节为 SVC #0xa、RET；外壳另有 fallback 自循环 `148490` |

所以，对这条已限定路径，可确认 **用户入口返回 → OS 槽析构 → 结束状态/通知 → 可空 hook → ELF drain → hook 后调用**。状态/通知发生在 ELF drain 之前；不能用状态 4 直接证明 ELF destructor 已完成。

另一个已知入口 `ThreadManager::CleanupThread@141970` 在 `141990` 调 OS 槽析构，其函数内没有 ELF drain/hook 调用。单凭它的尾返回，不能证明它的每个调用者都会接上 Horizon 外壳；本报告没有扫描这些未知调用者。

## 保留边界

这闭合安装赋值与普通新建线程返回顺序；没有证明动态实际执行、成功启动、全程不存在 hook/记录独立修改，或内部门控永远非零。异常、取消、显式终止、信号、进程退出及初始线程 teardown 不外推。若继续，下一条必要证据应是**被明确指定的初始线程或显式终止调用点**，核对它是否进入同一外壳；无需扩展槽 16、allocator 或无关全程序扫描。

本轮分析脚本初次因手写 LDP 指令字断言抄错而退出 1；以原输入/LLVM 字节纠正后退出 0，790 行核验通过。没有被拒绝、pending 或 uncertain 的动作。

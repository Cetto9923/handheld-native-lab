# 工单 P-MM-46｜MiniMax Code｜NVN event 族（11 名）—— guest 解析过、尚未调用

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `3517404`
> 影响：guest 在设备 transcript `.local/logs/p-mm-44-guest-transcript.log` 里**解析过**这 11 个名字（`-> trap slot`），**尚未调用**。sync 族（P-MM-45）之后它们很可能是下一个。

---

## 0. 目标（一句话）

实现 **NVN event 族的 11 个名字**。

**这一轮不产出画面。**（replay 仍只解码、不发 Vulkan 命令。）event 是 **GPU 与 CPU 之间的信号**，S1-c 之后帧与帧之间的同步会用到。

---

## 1. 名字清单（**guest 自己解析出来的**）

```
nvnEventBuilderSetDefaults    nvnEventBuilderSetStorage     nvnEventBuilderGetStorage
nvnEventBuilderGetMemoryPool  nvnEventBuilderGetMemoryOffset
nvnEventInitialize            nvnEventFinalize              nvnEventSignal
nvnEventGetValue              nvnEventGetMemoryPool         nvnEventGetMemoryOffset
```

来源：设备 transcript `.local/logs/p-mm-44-guest-transcript.log` 里 `nvnDeviceGetProcAddress: "<name>" -> trap slot` 的名字集合。**只有 guest 解析过的名字才在这张卡里**，不要自己加。

---

## 2. 语义来源

1. **签名**：`src/graphics/nvn/include/nvn/nvn.h`（vendored，别的游戏的 NVN 53.105 重建件，**签名未对本作证实**）。
2. **语义**：`.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`（同款游戏的参考宿主，MIT）：
   - `grep -n "nvnEvent" .local/refs/nxnative/dm/gfx/nvn/hle_nvn.c` 自己找，逐个**记下行号**。
   - 参考树里 event 的关键语义：**`nvnEventInitialize` 建一个事件；`nvnEventSignal` 把它置为已信号；`nvnEventGetValue` 读回这个值**。参考树有没有真的等？**自己看清楚并写下来**。
3. **本项目已有的形态模板**：`src/graphics/nvn_device/nvn_sampler.c`（对象族，15 名）、`src/graphics/nvn_device/nvn_memory_pool.c`（18 名）—— 形态照它们：**状态在宿主、按 guest 地址建键、绝不写 guest 对象内存（0xAA 哨兵）**。
4. **表要能长大**：`src/graphics/nvn_device/nvn_object_table.h` 是共享的、**随对象数增长**的表（`e8518f7` 引入）。本族照它写，**不要**再写一个定长小表。
5. **event 与 sync 的区别**要说清楚：sync 是 **fence/wait** 的载体，event 是 **signal/value** 的载体。参考树对两者的差别**自己查并写进报告**。

---

## 3. 写范围（**严格**）

- **你独占写**：`src/graphics/nvn_device/nvn_event.h`、`src/graphics/nvn_device/nvn_event.c`（新建）、
  `tests/native/nvn_event_contract.c`（新建，自带 main 与开关 `--nvn-event`）、
  `scripts/runtime/make_nvn_event_variants.py`、`scripts/runtime/selfproof_nvn_event.sh`、
  `coordination/reports/T-EVENT-family-2026-10-25.md`。
- **你只读、**不许改**：其它任何文件。**特别是 `nvn_device.c`（dispatch 的族块）、`nvn_queue.c`、`nvn_sync.*`、`scripts/runtime/build_guest_process_contract.sh`、`src/android/native/CMakeLists.txt`、`tests/native/guest_process_contract.c`** —— 这些是**协调者**的写权限，接线由协调者做。
- 分支：`task/P-MM-46-event-family`，worktree `.worktrees/p-mm-46`（自建）。**不许 push。**

---

## 4. 验收（**自己测出来，不是声称**）

1. **11 个名字**经 guest 自己的 `nvnDeviceGetProcAddress` 解析到、**每次解析到同一入口**、**互不重名**。
2. **状态语义**：Initialize 之后 builder 的值**快照**（改了 builder，对象仍返回旧值）；guest 对象**一个字节没被改**（0xAA 哨兵）。
   **`Signal` 之后 `GetValue` 必须读得到已信号** —— 这条要有**具体期望值**（参考树的值，或规格的值），不许写恒真断言。
3. **契约**：族契约 `tests/native/nvn_event_contract.c` 报错数为 0；构建脚本会**自动发现** `nvn_*_contract.c` 并**从文件名推导开关**（`nvn_event_contract.c` → `--nvn-event`）。**若契约因缺链接而失败，说明你少交了族名或协调者还没接线 —— 把这一点写在报告里。**
4. **三变体自证**（`scripts/runtime/selfproof_nvn_event.sh`，四个退出码由脚本自己判）：
   - **baseline → 0**
   - **A**：改坏一处**期望**（实现不动、闸门留着）→ **1**，stderr 有真 `[check-fail]`
   - **B**：同一处期望改坏 **+ 去掉最终闸门 + 硬编码错误数为 0** → **0**，但 **stdout 仍谎报 0** 且 stderr 留真 `[check-fail]`（**假绿要被量出来**）
   - **C**：契约不动、**只改桩**（让桩撒谎）→ **1**
   期望值必须**写死在契约里**，不许从实现的常量派生。
5. **NDK 交叉编译**：`--target=aarch64-linux-android34` + `-Wall -Wextra -Werror`，你的两个 .c 与契约 TU 都 exit 0。
6. **报告** `coordination/reports/T-EVENT-family-2026-10-25.md`：每个名字的**签名出处 + 参考树行号 + 实测行为**，逐项列出；并写清 event 与 sync 的差别。

---

## 5. 纪律（**上一轮踩过的坑**）

- **第一条工具调用就写文件**；先写最小可编译版本**立刻 commit**；预算过半还没 commit 就立刻 commit。**不许只交设计。**
- 契约里**不许恒真断言**，**不许硬编码 0 错误数**。
- 签名在 vendored 头里没有、或与参考树不一致 ⇒ **点名说出来**，按参考树走并写明理由。
- **不许**为了让契约变绿而改业务规则。

---

## 6. 恢复点

- 基线：`work/rg477m-native` @ `3517404`。
- 设备**不要碰**（协调者持锁）。**本卡不需要真机**。
- 交回时给出：分支、commit 列表、工作树状态、四个退出码、证据路径、**没有证明的东西**。

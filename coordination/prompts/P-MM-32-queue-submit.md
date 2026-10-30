# 工单 P-MM-32｜MiniMax Code｜队列提交/呈现族（5 名）—— replay 的接缝

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `12b46ab`
> 影响：**这是 guest 把录好的命令交给 GPU 的那一步**。参考树在这里**执行**命令包；本项目的 replay 也在这里被叫起。

---

## 0. 目标（一句话）

实现队列的 **5 个名字**，其中 **`nvnQueueSubmitCommands` 是 replay 的接缝**：guest 把命令包的 handle 交上来，**replay 在这里被叫起并读那个包**。

**这一轮不产出画面**（replay 目前只解码、不发 Vulkan 命令；那要等 P-MM-28 的呈现路径）。**但接缝必须先在。**

---

## 1. 名字清单（**guest 自己解析出来的**）

```
nvnQueueSubmitCommands      nvnQueuePresentTexture      nvnQueueFenceSync
nvnQueueWaitSync            nvnQueueBuilderGetMemory
```

来源：设备 transcript `.local/logs/p-mm-33-guest-transcript.log`。

---

## 2. 语义来源

1. **签名**：`src/graphics/nvn/include/nvn/nvn.h`（vendored，别的游戏的重建件，**签名未对本作证实**）。
2. **语义**：`.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`（同款游戏的参考宿主，MIT）：
   - **`nvnQueueSubmitCommands(q, n, h)` —— L1132**：参考树在这里**执行**命令包：`for (int i = 0; i < n; i++) { mtl_exec_rest(...); mtl_exec((const uint8_t *)h[i]); }`。
     ⇒ **`h[i]` 就是命令缓冲交出来的 handle**，而 handle 指向那个**不可变的命令包**。
   - **`nvnQueuePresentTexture(q, w, idx)` —— L1630**
   - `nvnQueueFenceSync(q, s, c, f)` —— **L631**：把 sync 标记为已 fence，并记下当时最后一个 fence
   - `nvnQueueWaitSync(q, s)` —— **L634**：**永远返回 1**；若该 sync 还没有 fence，参考树**只记一次**日志（*the sync object has no fence yet, the host does not wait*）
   - `nvnQueueBuilderGetMemory` —— 参考树里自己找（`grep -n "nvnQueueBuilderGetMemory" .local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`）
3. **本项目已有的**：
   - `src/graphics/nvn_device/nvn_queue.c`（29 个名字，**不含**本卡的 5 个）—— 形态模板照它。
   - `src/graphics/nvn_device/nvn_command_buffer.h`：**`nvn_command_buffer_get_package(handle)`** 给出那个不可变命令包。
   - `src/graphics/nvn_device/nvn_command_replay.h`：**`int nvn_command_replay_run(uint64_t handle)`** —— **replay 的公开入口，已经存在**。
   - `src/graphics/nvn_device/nvn_sync.*` / `nvn_event.*` **不存在**（sync 族和 event 族是下一张卡）⇒ **本卡的 `nvnQueueFenceSync` / `nvnQueueWaitSync` 不许依赖 sync 族**：sync 对象的状态**先放在本族自己的表里**（按 guest 地址建键），并**点名**说明它和后面的 sync 族要合并。

---

## 3. 接缝怎么接（**本卡的核心**）

`nvnQueueSubmitCommands(queue, n, handles)`：

1. 逐个 handle：
   - 用 `nvn_command_buffer_get_package(handle)` 取那个**不可变的命令包**；
   - **取不到 ⇒ 点名拒绝并计数**（不许静默跳过，也不许假装提交过）；
   - 取到了 ⇒ 调 **`nvn_command_replay_run(handle)`**，并把**返回的记录数 / 拒绝数**记进日志；
   - **不许**在这里自己解释命令字节（那是 replay 的活）。
2. **不许**在这一步"提交"任何东西给 Vulkan —— **replay 目前不发命令**。日志要写清楚「包已读，命令未提交」。
3. 计数：提交了几个包、读了多少条记录、拒绝了几个。

`nvnQueuePresentTexture(queue, window, index)`：
- **本轮不真的 present**（呈现路径在 P-MM-28 手里）。做的是：**记录请求**（哪个窗口、哪个 index），**点名**说「呈现路径还没接上」，**不许**假装 present 成功。

---

## 4. 状态放在宿主（**铁律**）

- 队列的提交状态、sync 的 fence 标记**全在宿主**，按 guest 地址建键。
- **绝不写 guest 对象内存**（契约用 **0xAA 哨兵**证明）。

---

## 5. 写范围（**只有这些**）

```
src/graphics/nvn_device/nvn_queue.h
src/graphics/nvn_device/nvn_queue.c
tests/native/nvn_queue_submit_contract.c
scripts/runtime/selfproof_nvn_queue_submit.sh
scripts/runtime/make_nvn_queue_submit_variants.py
coordination/reports/T-QUEUE-submit-2026-10-25.md
```

## 6. 不许改（总控保留写权限）

`src/graphics/nvn_device/nvn_device.c`、`nvn_command_buffer.*`、`nvn_command_replay.*`、
`nvn_present.*`、`nvn_backend_vulkan.c`、`nvn_window.*`、`nvn_texture.*`、`nvn_sampler*`、
`src/android/native/CMakeLists.txt`、`scripts/runtime/build_guest_process_contract.sh`、
`src/runtime/**`、`tests/native/guest_process_contract.c`

---

## 7. 族契约与自证

- 契约 TU 自带 `main` 和**自己的开关**，开关名 = 文件名去掉 `_contract` 再把 `_` 换成 `-` ⇒ **`--nvn-queue-submit`**。
- 开关不对 ⇒ `fprintf(stderr,"usage: ...")` + `return 2`（**不要** `return 0`）。
- 末行 stdout 是一行 JSON，里面有 **`nvn_queue_submit_errors`**；自带 `CHECK` / `expect_value` / `expect_condition`。
- **期望值必须独立于实现常量写死**；**禁止恒真断言**；**禁止把错误数硬编码成 0**；**禁止删测试改绿**。

**契约至少要证明**：
1. 五个名字都能通过 guest 自己的 `nvnDeviceGetProcAddress` 解析到，**每次解析到同一入口**，族表里**每个名字只出现一次**。
2. **接缝真的接上了**：录一个真实的命令缓冲 → 交出 handle → `nvnQueueSubmitCommands` 之后，**replay 读到了那个包**（用 `nvn_command_replay_get_stats()` 的包数/记录数验，**不是**用桩数）。
3. **未知 handle 被点名拒绝并计数**（不许静默跳过）。
4. **0xAA 哨兵**：队列对象、命令缓冲对象、handle 数组**都没被写**。
5. `nvnQueueWaitSync` **对没有 fence 的 sync 也返回 1**，并且**只记一次**日志（参考树的行为）。
6. `nvnQueuePresentTexture` **不报告成功**（呈现路径还没接上）。

**三变体自证**（脚本自己判四个退出码）：

| 变体 | 做法 | 期望 |
|---|---|---|
| baseline | 不改 | exit 0，stdout `errors:0` |
| **A** | 改坏一个期望值 | **exit 1**，stderr 有真 `[check-fail]` |
| **B** | 同处改坏 **+ 去掉最终 CHECK **+ 上报错误数**硬编码 0** | **exit 0**，而 stdout 仍谎报 `errors:0`、stderr 留真 `[check-fail]` ← **假绿要量出来** |
| **C** | 契约一字不改，只把桩改坏 | **exit 1** |

日志：`.local/logs/p-mm-32-queue-submit-selfproof.log`（**每次运行独立目录**）。

---

## 8. 验收

1. 五个名字解析、稳定、不重名。
2. **接缝接通并被契约验到**（replay 的包数/记录数变了，不是桩数）。
3. 未知 handle 点名拒绝并计数。
4. 0xAA 哨兵成立。
5. `nvnQueueWaitSync` 永远返回 1，未 fence 时只记一次。
6. `nvnQueuePresentTexture` 不谎报成功。
7. 三变体自证 A红 / B假绿 / C红，脚本 exit 0。
8. **NDK 交叉编译** `--target=aarch64-linux-android34` + `-Wall -Wextra -Werror` 过。
9. 报告 `coordination/reports/T-QUEUE-submit-2026-10-25.md`：每个名字的签名来源、参考树没有的名字**点名**、**没有**证明的东西。

---

## 9. 纪律

1. **第一条工具调用就是写文件**，不是搜索、不是推演。
2. **先写最小可编译版本，立刻 `git commit`**，再一项一项补。**宁可有五个能编译的提交，也不要一个想清楚了但没写出来的提交。**
3. 预算过半还没提交 ⇒ **立刻提交**当前能编译的状态。
4. 每次实验一个独立目录，原始日志不覆盖。

---

## 10. 禁止事项

- ❌ push；reset --hard / clean / stash
- ❌ 改第 6 节里的文件
- ❌ **自己解释命令字节**（那是 replay 的活）
- ❌ **假装 present 成功**
- ❌ 写 guest 对象内存（0xAA 哨兵会抓）
- ❌ 为通过测试硬编码结果 / 伪造数据 / 删断言
- ❌ 声称标题画面 / 可玩 / 有帧 / 帧率
- ❌ 碰真机（**本卡不需要设备**）

---

## 11. 完成后的动作

1. `git commit`（分支 `task/P-MM-32-queue-submit`，**不 push**），只放第 5 节的文件
2. 自证日志与 stdout/stderr 副本拷到主检出的 `.local/logs/` 与 `.local/selfproof/`
3. 报告写完就**停下来**，最终回复给我：分支、commit、每个验收项的实测结果（含四个退出码）、证据路径、**没有**证明的东西

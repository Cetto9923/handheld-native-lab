# 工单 P-MM-29｜MiniMax Code｜游戏自己的 fragment shader 被驱动拒收（找出真因并做出能被接受的版本）

> **状态**：正式派工
> 基线：`work/rg477m-native` @ `971df74`（含 P-MM-27 的管线崩溃根因）
> 影响：**游戏的 vertex shader 能建成管线，fragment shader 建不了** ⇒ 图形管线这一关过不去 ⇒ **第一帧出不来**。

---

## 0. 目标（一句话）

搞清楚**为什么设备的驱动拒收这个游戏的 fragment 模块**，并产出一个**在这台设备上实测能建成管线（`VK_SUCCESS`）的 fragment 模块**。

**这一轮不产出画面。** 产出的是「模块能被驱动接受」+「draw 被 GPU 消费」。

---

## 1. 已测到的事实（**别重做**）

P-MM-27（`coordination/reports/T-VULKAN-graphics-pipeline-crash-2026-10-25.md`）在设备 `RG477M01445122` 上测到：

| 事实 | 值 |
|---|---|
| 游戏的 **vertex** 模块 | 管线 **`VK_SUCCESS`** |
| 游戏的 **fragment** 模块 | 管线 **`VK_ERROR_INITIALIZATION_FAILED` (-3)** |
| 同一批模块用 `spirv-val --target-env vulkan1.1` | **全部通过** |
| compute 管线（去掉 `Float64` 声明后） | 4/4 建成并被 GPU 消费 |
| 图形管线的 SIGSEGV | **已定位为探针自己的缺陷**（stage 结构没初始化），`971df74` 已修 |

**P-MM-27 提出的三个猜想（是推断，不是测量）**：
1. fragment 用 `OpAccessChain` 访问 `v4float` 接口变量，**形态可疑**；
2. fragment 读 **Input location 0–1**，而 vertex **只写 location 0**；
3. `NoPerspective` 修饰不匹配。

**你的任务就是把「推断」变成「测量」**，并给出修法。

---

## 2. 仓库与设备

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- **自己开 worktree**：`.worktrees/p-mm-29`，分支 `task/P-MM-29-fragment-shader`
- 设备 `RG477M01445122`：**协调者现在占着设备**（真机重跑）。
  - 真机前先 `ls .local/agent-pm/`；**有锁就停下报告，不要抢**。没有锁才 `touch .local/agent-pm/BOOT_LOCK_MM_A`，**结束必须删掉**。
- 成功可 `git commit`；**不 push**

---

## 3. 一步一步来（**每步都要有实测数字**）

### 步骤 1：先排除「能力声明」这一层

游戏那三个 fragment 模块**声明了但没用** `Float64` 和 `FragmentShaderPixelInterlockEXT`（P-MM-23 实测）。
本项目已有工具 **`scripts/graphics/fix_spirv_capabilities.py`**（按模块自身证据修能力，模块真用到的能力不许删）。

```bash
python3 scripts/graphics/fix_spirv_capabilities.py --in <in>.spv --out <out>.spv \
  --add StorageImageWriteWithoutFormat --drop Float64 --drop FragmentShaderPixelInterlockEXT
```

**先量**：把三个 fragment 模块用这个工具修好，**再送进管线创建**。`VK_SUCCESS` 吗？
- 如果**成功** ⇒ 真因就是能力声明（和 compute 一样），**这一轮就结了**，把命令和退出码写进报告。
- 如果**仍失败** ⇒ 继续步骤 2。

### 步骤 2：把拒绝原因钉到**具体的 SPIR-V 指令**上

按顺序试，每次**只改一处**，每次都送设备：

| # | 改什么 | 怎么改 |
|---|---|---|
| a | `OpAccessChain` 访问接口变量 | 把对 `v4float` 接口变量的 `OpAccessChain` 换成直接 `OpLoad`（或删掉该访问链） |
| b | fragment 读 location 1 而 vertex 只写 0 | 给 vertex 补写 location 1（`OpDecorate ... Location 1` + 对应输出），**或**让 fragment 不读 location 1 |
| c | `NoPerspective` 不匹配 | 把两侧的插值修饰改成一致 |
| d | 其它 | 反汇编（`spirv-dis`）逐条看 fragment 的 `OpEntryPoint` / `OpDecorate` / 接口变量，找**别的**不匹配 |

**每一步都要留**：改前后的 `spirv-dis` 片段、管线创建的返回码、原始日志。

⚠️ **不要用"改到能过"为止就收工** —— 要说清楚**哪一处改动让拒绝消失**，以及**另外几处改动单独做时是什么结果**（分开测，不是叠着测）。

### 步骤 3：产出

- 一个**能被设备接受**的 fragment 模块（`.spv`）+ **生成它的脚本**（进仓库，**不要**只交一个二进制）。
- 该模块的 `vkCreateGraphicsPipelines` = `VK_SUCCESS`，且**一次 draw 被 GPU 消费**（`vkQueueSubmit` + `vkWaitForFences` 都 `VK_SUCCESS`）。
- 原始模块**一律不改**（修正版另存）。

---

## 4. 写范围（**只有这些**）

```
scripts/graphics/fix_spirv_*.py              （新工具，可加）
src/graphics/vulkan_probe/vulkan_shader_probe.c   （可改：加 fragment 变体的管线创建）
scripts/graphics/build_vulkan_shader_probe_android.sh （可改）
coordination/reports/T-SHADER-fragment-2026-10-25.md
```

证据在 `.local/shader-verify/`（**不提交**）：每次实验一个目录（命令、退出码、logcat、run.json）。

## 5. 不许改（总控保留写权限）

`src/graphics/nvn_device/**`、`src/android/native/CMakeLists.txt`、
`scripts/runtime/build_guest_process_contract.sh`、`src/runtime/**`、
`tests/native/guest_process_contract.c`、`scripts/graphics/fix_spirv_capabilities.py`（**已入库的总控文件，只读**；要改就写进报告让总控改）

---

## 6. 纪律（上一张卡栽在这里）

1. **第一条工具调用就是动手**，不是搜索、不是推演。
2. **先跑出第一个实测数字就提交**（哪怕只是"步骤 1 的结果是 -3"），**宁可有五个能复现的提交，也不要一个想清楚了但没跑出来的提交**。
3. 预算过半还没提交 ⇒ **立刻提交**当前状态。
4. 每次实验**一个独立目录**，原始日志不覆盖。

---

## 7. 验收

1. **步骤 1 的实测结果**（能力修正后 fragment 能不能建成），有退出码和日志。
2. **真因定位**：是能力声明，还是接口变量/location/插值，**每一项单独测过**（分开测，不是叠着测），并给出**让它消失的那一处改动**。
3. **一个能被设备接受的 fragment 模块**，以及**生成它的脚本**（进仓库）。
4. **该模块建成管线 `VK_SUCCESS`，且一次 draw 被 GPU 消费**（submit + wait 都 `VK_SUCCESS`）。
5. **原始模块没被改**。
6. **compute 路径没回归**（复跑原来的 compute 实验并贴结果）。
7. 报告 `coordination/reports/T-SHADER-fragment-2026-10-25.md`。

---

## 8. 禁止事项

- ❌ push；reset --hard / clean / stash
- ❌ 改第 5 节里的文件
- ❌ 声称「出帧 / 标题画面 / 可玩 / 游戏画面」
- ❌ 把「模块能建成管线」写成「游戏能跑」
- ❌ 改到能过就收工，**不说清楚是哪一处改动起的作用**
- ❌ 恒真断言 / 硬编码错误数 0
- ❌ 改原始模块
- ❌ 跑完不删 boot lock

---

## 9. 完成后的动作

1. `git commit`（分支 `task/P-MM-29-fragment-shader`，**不 push**），只放第 4 节的文件
2. **删掉** `.local/agent-pm/BOOT_LOCK_MM_A`
3. 报告写完就**停下来**，最终回复给我：分支、commit、步骤 1–3 的实测答案（含每次的返回码）、证据路径、**没有**证明的东西

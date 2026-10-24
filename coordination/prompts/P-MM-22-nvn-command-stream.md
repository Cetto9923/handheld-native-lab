# 工单 P-MM-22｜worker｜NVN 命令流：真实记录格式 + 不可变命令包 + 调用期数据拷贝

> **状态**：正式派工（总控：Codex 只读复核后的追加纠偏第 3 项）
> 基线：`work/rg477m-native` @ **`064634f`**（工作区不干净：命令缓冲族已接入但未提交，shader 身份工具已提交未接）
> 纠偏原文：`/Users/yuyan9923/Documents/Codex/2026-10-25/kiro-http-localhost-5476-chat-diagnosing/outputs/MiniMAX执行中复核-追加纠偏.md` §3
> 真机证据：`.local/logs/p-mm-22-guest-transcript.log`（57023 行；第一致命已是 `nvnSamplerBuilderSetDefaults`，命令缓冲族已越过）
> 前提：P-MM-17～19 已交付；帧数仍为 0；未达标题/可玩。

---

## 0. 仓库与环境（必须）

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- 设备 `adb -s RG477M01445122`；**本单不装包、不跑真机**（总控统一跑）；不 root / 不刷机 / 不清数据；**不 push**
- 先读：`AGENTS.md` → `coordination/HANDOFF.md` 顶部 → **本工单** → `coordination/reports/T-SHADER-identity-2026-10-25.md` 末节 → `src/graphics/nvn_device/nvn_command_buffer.{h,c}` → `src/graphics/nvn_device/nvn_command_buffer.generated.c`（生成器：`scripts/runtime/make_nvn_command_buffer_family.py`）
- **同款游戏的参考实现**（MIT）：`.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h`（命令记录格式与载荷布局）、`.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`（该游戏真实的 NVN 语义）、`.local/refs/nxnative/dm/games/0100606026F12000/`（就是本游戏）
- `.local/romfs/**` 只读；**不要调用 agy**；不 `git add .`；**不要 commit**（总控统一提交）

---

## 1. 问题（纠偏原文摘要）

现在的 `nvn_command_buffer.generated.c` 批量建了 156 个入口，都报告 `implemented`，但：

1. 它们只调用通用 `nvn_command_buffer_record`，把参数 **memcpy 到 `uint64_t args[8]`**；指针参数只存**指针值**，不拷数据。
2. `SetViewports` / `BindUniformBuffers` / `CallCommands` 等只保存数组指针；调用方改写／复用内存后，回放无法可靠还原原命令。
3. `nvn_command_buffer_end_recording` 只递增返回 handle，**没有 handle → 不可变命令包的关联**；同一 buffer 下次 `BeginRecording` / `Finalize` / 地址复用会覆盖已完成的内容。
4. 满容量／分配失败只增加 `dropped`/`refused` 计数，**不阻止调用继续**；recording 状态也没有强制检查；后续可能静默丢掉绘制。
5. 这是**第三套平行格式**（函数编号 + 原始参数），与上游 opcode/payload 没有转换，没有 replay consumer。
6. 头文件缺签名的 16 个走统一原始整数 wrapper，**不能据此标 production implemented**。

---

## 2. 本单要做的事（三件，按顺序）

### 2.1 真实记录格式（替代平行格式）

NVN 的命令流格式是**宿主内部**格式，不是跨宿主 ABI；但**本游戏**的格式是已知的，就在参考树里：

```c
.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h
    typedef struct { uint32_t op, size; } op_hdr;    /* 记录头，size 含头且 8 字节对齐 */
    enum { OP_END, OP_JUMP, OP_BEGIN, OP_CALL, OP_SIGNAL_EVENT, ..., OP_COUNT_ }  /* 约 55 个 opcode */
    p_set_rt / p_clear_color / p_clear_ds / p_viewports / p_scissor / p_bind_buf /
    p_bind_vbuf / p_bind_handles / p_bind_tex1 / ...
```

**要求**：录制器写出的记录必须能被上面这套布局读出来。

- 至少把**本游戏真实会用到**的 opcode 落地（不是全部 55 个）：从 `hle_nvn.c` 里看该游戏实际会走到的那些。
- 每个 opcode 的 payload 必须按 `nvn_obj.h` 的结构写，**字段顺序与宽度逐字对齐**（`FITS` 宏已校验过布局）。
- **不要自己另发明编号**。要么用 `nvn_obj.h` 的 `OP_*`，要么在代码里写清楚「本移植的编号与参考树的对应关系」，并附一张对照表。
- 记录写入**宿主侧的一段命令内存**（模拟 guest 通过 `AddCommandMemory` 交出的那块），不是写进 guest 对象。
- **handle = 这段命令内存里本次录制起始记录的地址**（参考树 `cmd_t.begin` 的做法），而不是一个自增计数器。

> 若发现 `nvn_obj.h` 的布局与本游戏真机行为矛盾（概率低，它是同款游戏），**以真机日志为准**，并把矛盾写进报告，不要自行调和。

### 2.2 不可变命令包（handle → 命令快照）

- `EndRecording` 把本次录制的记录**快照**成一份不可变的命令包，handle 指向它。
- 同一 buffer 的下一次 `BeginRecording` / `Finalize` / **地址复用**都**不得**覆盖已完成 handle 指向的包。
- 提供只读访问口，供 replay 消费：给 handle，返回该包的记录数组与总字节数。
- `CallCommands(cmd, count, handles[])`：**在调用时拷贝 handle 数组**（guest 随后会改写那块内存），并对每个 handle 做**登记表比对**校验；未知 handle **拒绝并如实记录**，不得解引用、不得静默跳过。

### 2.3 调用期数据拷贝（易失数据）

上一轮 worker 已把 62 个带指针参数的入口分成三类（固定大小对象 / 元素大小已知但个数来自邻近参数 / 大小未知）。**按那三类分别处理**：

| 类别 | 处理 |
|---|---|
| 固定大小对象（blend/channelMask/color/ms/poly/ds/vertexAttrib/vertexStream/program/texture/textureView/copyRegion/bufferRange/sync/event/memoryPool/texturePool/samplerPool/device） | **调用时拷贝该对象**（头部 sizeof），记录里存副本而不是指针 |
| 元素大小已知、个数来自邻近参数的数组（float/int/uint32/handle 数组、pointer 数组、viewportSwizzle 数组） | **调用时拷贝 count × element_size 字节**；count 与 element_size 必须写进记录 |
| 大小未知（`const char*`、`void*`、`NVNdrawTextureRegion*`、无个数的 float/int 数组） | **不解引用、不猜大小**：记录指针 + 标记「未解析指针」，replay 必须知道这件事 |

**约束**：
- **不能不分类型把所有 buffer 都提前复制**（GPU buffer 内容由 guest 自己管，命令内存不归我们），**也不能所有指针都延迟解引用**。
- buffer 内容的生命周期语义要正确：绑定的是**地址 + 偏移 + 长度**，不是内容。
- 拷贝要有上限（例如单条记录最多 4 KiB），超限**拒绝该条命令并记录**，不能截断后当作成功。

### 2.4 语义必须明确（不许静默）

- **recording 状态**：只有 `BeginRecording` 与 `EndRecording` 之间的命令才进命令流。状态外的调用要**明确计数并按名字记录**，不能悄悄混进命令流。
- **容量／溢出／分配失败**：命令写不进去时，**该次录制作废**——`EndRecording` **不得**交出有效 handle，必须返回失败并说明原因。**不许 dropped 之后继续返回成功。**
- **回调**：`SetMemoryCallback` / `SetMemoryCallbackData` 存的回调**不得**在 replay 时直接调用；命令内存不足时走本项目的 native boundary / guest TP 规则（见 `src/runtime/nso/native_boundary.{h,c}` 与 `src/runtime/hle/guest_tp.{h,c}`），**回调必须真的走 guest TP**，不要留空指针装饰。
- **未知 ABI**：那 16 个签名未知的入口，**不得**报告成普通 `implemented`。给族表加一个状态字段（例如 `signature_confirmed`），在 `nvnDeviceGetProcAddress` 的日志里区分「签名来自 vendored 头（未对本游戏证实）」与「签名未知，仅诊断记录」，让日志能说清。**但不要因此让 guest 立刻 trap**：guest 已经在调用它们了，trap 会把推进打断；正确做法是**如实标注 + 只读记录 + 计数**。

---

## 3. 写入范围（只有这些）

| 文件 | 动作 |
|---|---|
| `src/graphics/nvn_device/nvn_command_buffer.h` | 改 |
| `src/graphics/nvn_device/nvn_command_buffer.c` | 改 |
| `src/graphics/nvn_device/nvn_command_buffer.generated.c` | **由生成器重新生成**，不要手改 |
| `scripts/runtime/make_nvn_command_buffer_family.py` | 改（生成器） |

**禁止修改**：`nvn_device.c`（dispatch）、`src/android/native/CMakeLists.txt`、`tests/native/guest_process_contract.c`、`scripts/runtime/build_guest_process_contract.sh`、`scripts/runtime/selfproof_*`、`coordination/**`、`scripts/graphics/**`、其它任何 NVN 族文件。这些由总控接线。

如需新增文件（例如记录格式单独一个 `.c/.h`），先在报告里说明文件名与理由，等总控确认；**默认不新增文件**。

---

## 4. 自测（必做，三变体）

参考 `scripts/runtime/make_nvn_render_state_family.py` 与 `scripts/runtime/selfproof_nvn_render_state.sh` 的路子（**读，不要改**）。写一个自带 harness 放 `.local/`（不进 git），至少证明：

1. 写出的记录能被 `nvn_obj.h` 的布局读出来（用同样的结构体去解析，字段逐个相等）。
2. **handle → 命令包不可变**：录一次 → EndRecording 拿 handle → 同一 buffer 再录一次 → 第一个 handle 的内容不变。
3. **地址复用不覆盖**：用一个 buffer 地址再初始化一个 → 旧 handle 仍然指向旧内容。
4. **调用期拷贝**：录一条带数组的命令 → **改写 guest 的那块数组内存** → 记录里的数据不变。
5. **未知大小指针**不解引用，且记录里有「未解析」标记。
6. **溢出语义**：把命令内存上限压到很小 → 触发溢出 → `EndRecording` **不交出有效 handle**，并且有明确日志/计数。
7. **recording 状态**：在 `BeginRecording` 之外记录命令 → 不进命令流，且被计数。
8. **三变体**：A 改坏期望→红；B 去末门 + 硬编码 0→假绿；C 契约不改、只把桩改成撒谎→红。

把 harness 源码与**真实运行输出**贴进报告。

---

## 5. 验收

- `clang -std=c11 -Wall -Wextra -Werror -I src/graphics/nvn_device` 编译无警告。
- 8 项自测全过，三变体红/假绿/红。
- 报告写清楚：实现了哪些 opcode、哪些没实现（点名）、拷贝策略的边界、未知 ABI 的标注方式、**离「replay 能消费」还差什么**。
- **不要**声称出帧 / 可玩 / 标题。

---

## 6. 收尾

- 四个文件留在工作区，**不要 commit**。
- 报告写在最终回答里（纯文本，≤ 60 行）。
- 不装包、不跑真机。

---

## 7. 禁止

- 继续扩第三套平行格式（这正是要消除的东西）
- 万能成功桩、绕过 abort
- 写入 guest 对象内存
- 把「入口能 resolve」当成交付点
- 声称标题/可玩/游戏帧
- push / force / 动 main / 调 agy / 删 romfs / 改 dispatch / 改契约 / 改 CMake / `git add .` / `git commit` / `git reset` / `git clean` / `git stash`

做完停下汇报。

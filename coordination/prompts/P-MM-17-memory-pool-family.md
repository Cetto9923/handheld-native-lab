# 工单 P-MM-17｜MiniMax Code｜NVN 内存池家族（越过 `nvnMemoryPoolBuilderSetDefaults`）

> **状态**：正式派工（总控诊断：上一轮对话卡在「找头文件 / 找解密 NSO」，二者仓库里都已有，勿再空转搜索）
> 基线：`work/rg477m-native` @ **`6b40c89`**（clean；其上实现提交 `48bd1c4` 窗口族 + `02eb386` 队列族）
> 真机证据：`.local/logs/p-mm-17-guest-transcript.log` L59814
> ```
> [nvn] unimplemented NVN entry "nvnMemoryPoolBuilderSetDefaults" was called (trap slot 16, caller lr=0x7215453a38)
> ```
> 前置：P-MM-16 已交付 `nn::vi` + queue + window；帧数仍为 0；未达标题/可玩

---

## 0. 仓库与环境（必须）

- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 **`work/rg477m-native` only**
- 设备：`adb -s RG477M01445122`；不 root / 不刷机 / 不清数据；**不 push**
- 先读：`AGENTS.md` → `coordination/HANDOFF.md` 顶部 **P-MM-16** → 本工单 → `src/graphics/nvn_device/nvn_queue.{h,c}`（**形态模板**）→ `src/graphics/nvn/include/nvn/nvn.h`（签名来源）
- 产物：`.local/build-p-mm-17/`
- guest 日志：`.local/logs/p-mm-17b-guest-transcript.log`（或沿用 app 内 `guest_boot.log` 拉回后改名）
- `.local/romfs/**` 只读；`task/T04-nvn-firstframe` / `.claude/worktrees/*` 勿改
- **不要调用 agy**；成功可 `git commit`；**不 push**
- 真机 boot / 装包前 `touch .local/agent-pm/BOOT_LOCK_MM_A`，结束后删掉
- 验收不降级；**禁止声称标题/可玩/已出游戏帧**；做完停下写 HANDOFF

---

## 1. 你卡住的原因（读完立刻跳过搜索）

| 你在找的 | 实际位置 | 结论 |
|---|---|---|
| vendored NVN 头 | **`src/graphics/nvn/include/nvn/nvn.h`**（约 L1196–1212 起 `PFNNVNMEMORYPOOL*`） | **已有，禁止猜签名** |
| 解密后 NSO | **`.local/uncompressed/main.bin`**（120M，build_id `736837af…`） | **已有** |
| 反汇编工具 | `python3 scripts/runtime/inspect_nso.py` + `/Library/Developer/CommandLineTools/usr/bin/llvm-objdump` | **已有** |
| 调用点普查 | **`evidence/nvn/guest_nvn_code_references.tsv`**（本族 18 名全在） | **已有** |
| 名字清单 | **`evidence/nvn/main_nvn_ids.txt`** | **已有** |

**不要**再搜网、不要再问「有没有 NSO」。直接开写。

---

## 2. 本族 18 个名字（只做这些，不夹带 command buffer）

```
nvnMemoryPoolBuilderSetDevice
nvnMemoryPoolBuilderSetDefaults
nvnMemoryPoolBuilderSetStorage
nvnMemoryPoolBuilderSetFlags
nvnMemoryPoolBuilderGetDevice          ← 头文件 typedef 段缺失，见 §3
nvnMemoryPoolBuilderGetMemory          ← 头文件写成 void 返回，见 §3
nvnMemoryPoolBuilderGetSize
nvnMemoryPoolBuilderGetFlags
nvnMemoryPoolInitialize
nvnMemoryPoolSetDebugLabel
nvnMemoryPoolFinalize
nvnMemoryPoolMap
nvnMemoryPoolFlushMappedRange
nvnMemoryPoolInvalidateMappedRange
nvnMemoryPoolGetBufferAddress
nvnMemoryPoolMapVirtual
nvnMemoryPoolGetSize
nvnMemoryPoolGetFlags
```

guest 解析入口偏移（TSV）：`SetDefaults @ 0x142e48c` 等，全表见 `evidence/nvn/guest_nvn_code_references.tsv`。

---

## 3. 签名来源（头文件优先；冲突时 guest 调用点胜）

从 `src/graphics/nvn/include/nvn/nvn.h` 照抄（已核对）：

```c
void     nvnMemoryPoolBuilderSetDevice(NVNmemoryPoolBuilder*, NVNdevice*);
void     nvnMemoryPoolBuilderSetDefaults(NVNmemoryPoolBuilder*);
void     nvnMemoryPoolBuilderSetStorage(NVNmemoryPoolBuilder*, void*, size_t);
void     nvnMemoryPoolBuilderSetFlags(NVNmemoryPoolBuilder*, int);
/* GetMemory：头文件 PFN 写成 void 返回 —— 这是逆向头常见笔误。
   实现前必须用 inspect_nso 切片调用点确认 x0 是否带回 void*；
   若 guest 使用返回值，按 void* 实现并在 HANDOFF 记「头文件笔误」。 */
size_t   nvnMemoryPoolBuilderGetSize(const NVNmemoryPoolBuilder*);
NVNmemoryPoolFlags nvnMemoryPoolBuilderGetFlags(const NVNmemoryPoolBuilder*);
NVNboolean nvnMemoryPoolInitialize(NVNmemoryPool*, const NVNmemoryPoolBuilder*);
void     nvnMemoryPoolSetDebugLabel(NVNmemoryPool*, const char*);
void     nvnMemoryPoolFinalize(NVNmemoryPool*);
void*    nvnMemoryPoolMap(const NVNmemoryPool*);
void     nvnMemoryPoolFlushMappedRange(const NVNmemoryPool*, ptrdiff_t, size_t);
void     nvnMemoryPoolInvalidateMappedRange(const NVNmemoryPool*, ptrdiff_t, size_t);
NVNbufferAddress nvnMemoryPoolGetBufferAddress(const NVNmemoryPool*);
NVNboolean nvnMemoryPoolMapVirtual(NVNmemoryPool*, int, const NVNmappingRequest*);
size_t   nvnMemoryPoolGetSize(const NVNmemoryPool*);
NVNmemoryPoolFlags nvnMemoryPoolGetFlags(const NVNmemoryPool*);
```

**头文件缺的一条**（名字在 `main_nvn_ids.txt`，typedef 段没有）：

```c
NVNdevice* nvnMemoryPoolBuilderGetDevice(const NVNmemoryPoolBuilder*);
```

用 `inspect_nso.py .local/uncompressed/main.bin --out .local/scratch/p-mm-17-pool --slice 0x142e470:0xc0` 核对 `GetDevice`/`GetMemory` 的返回约定；**guest 胜于头文件**。

Flags 枚举（同头文件 L605+）：
`CPU_NO_ACCESS=0x1` / `CPU_UNCACHED=0x2` / `CPU_CACHED=0x4` / `GPU_NO_ACCESS=0x8` / `GPU_UNCACHED=0x10` / `GPU_CACHED=0x20` / `SHADER_CODE=0x40` / `COMPRESSIBLE=0x80` / `PHYSICAL=0x100` / `VIRTUAL=0x200`。

---

## 4. 实现方向（已拍板，照抄 queue/window 形态）

### 4.1 形态（与 `nvn_queue.c` / `nvn_window.c` 一致）

- **状态全在宿主**：guest 的 builder/pool 地址只作 key；**绝不写入 guest 对象内存**（SDK 布局未知）。
- 新文件：`src/graphics/nvn_device/nvn_memory_pool.h` + `nvn_memory_pool.c`
- 在 `nvn_device.c` 的 `GetProcAddress` 里挂上 `nvn_memory_pool_family_*`（紧挨 queue/window 注册）
- CMake / 契约构建接入：
  - `src/android/native/CMakeLists.txt`（已有 queue/window 两行旁加 `nvn_memory_pool.c`）
  - `scripts/runtime/build_guest_process_contract.sh` 的 `core_sources` 同样加一行

### 4.2 语义（最小可证伪，禁止假成功）

1. **Builder**：`SetDefaults` 清零 device/storage/size/flags；`SetDevice` / `SetStorage(ptr,size)` / `SetFlags` 往返；Get* 读回一致。
2. **Initialize**：
   - builder 未 `SetDevice` 或 device 未初始化 ⇒ **返回 false**，打 `[nvn]` 说明，**不写 pool**。
   - `SetStorage` 的 `ptr/size` 是 **guest 已分配的宿主机可见内存**（UE 自己 malloc/mmap 再交给 NVN）——本层 **记录这段范围**，用它作为 CPU 映射基址；**不要再 malloc 一份影子拷贝去「代替」guest 存储**（除非 storage 为 NULL 且 flags 要求运行时自备，那种路径本单若无真机触达证据则 **点名拒绝**，勿猜）。
3. **Vulkan 侧（仅 Android 真机后端）**：按 flags 选 `VkMemoryPropertyFlags`，`vkAllocateMemory` + 必要时 `vkMapMemory`；**没有真设备时 Initialize 必须失败**（与 queue 一致，宿主 macOS 走 none 后端诚实失败）。
4. **Map**：返回 CPU 指针（storage 基址或 `vkMapMemory` 结果）；未 Initialize 返 NULL。
5. **Flush/InvalidateMappedRange**：CPU_CACHED 路径做真实同步（至少 `vkFlushMappedMemoryRanges` / `vkInvalidateMappedMemoryRanges` 或等价）；其余 flags 可做 no-op 但必须打日志说明为何 no-op。
6. **GetBufferAddress**：返回本运行时稳定的 GPU 可见地址句柄（可用「pool key + offset 编码」或真实 `VkDeviceMemory` 绑定地址）；**同一 pool 反复调用必须稳定**；契约锁死稳定性。
7. **MapVirtual**：本单若无真机调用证据，**实现为返回 false + 点名日志**，勿伪造页表。
8. **Finalize**：释放 Vk 资源、清 host 表项；重复 Finalize 安全。
9. **未实现的其它 NVN 名**继续走原 trap（点名 `_exit(97)`），禁止万能成功桩。

### 4.3 日志

成功路径统一：
```
[nvn] nvnMemoryPoolInitialize: pool=%p builder=%p device=%p storage=%p size=%zu flags=0x%x -> true
```
失败必须说明原因（no builder / no device / no storage / backend refused …）。

---

## 5. 契约（必做，防假绿）

在 `tests/native/guest_process_contract.c` 增 `--nvn-memory-pool`：

1. 18 个名字均可 resolve 且同名稳定  
2. builder 往返：defaults / device / storage / flags / size  
3. **builder / pool 对象内存不被写入**（0xAA 哨兵，照 queue 契约）  
4. 无 device 时 Initialize → false  
5. 真设备时 Initialize → true，且 `Map` 非 NULL、`GetSize/GetFlags` 与 builder 一致、`GetBufferAddress` 稳定  
6. Finalize 后 Map 返 NULL  
7. 打印真实 `nvn_memory_pool_errors` + 末尾 `CHECK(!errors)`  
8. **三变体自证**（照 `scripts/runtime/selfproof_nvn_queue.sh`）：A 改坏→红；B 去末门+硬编码 0→假绿对照；C 后端谎报有内存→红  

`build_guest_process_contract.sh` 接入新用例与 manifest 字段。

---

## 6. 验收

### 6.1 宿主契约
```bash
cd /Users/yuyan9923/GitHub/ff-resonance-rg477m-native
sh scripts/runtime/build_guest_process_contract.sh \
  --out .local/build-p-mm-17 \
  --toolchain .local/minimal-ndk/toolchain \
  --resource-quota-bytes 16777216
```
期望：exit 0；`--nvn-memory-pool` PASS；自证三变体日志入库。

### 6.2 真机（app 同进程 guest，沿用 P-MM-15/16 装包路径）
- 设备 `RG477M01445122`
- 拉回 guest transcript → `.local/logs/p-mm-17b-guest-transcript.log`

**验收 A（必过）**
- 不再在 `nvnMemoryPoolBuilderSetDefaults` / `nvnMemoryPoolInitialize` 上作为第一致命停下
- 出现 `[nvn] nvnMemoryPoolInitialize: ... -> true`（或 guest 下一刀若未走到 Initialize，至少 SetDefaults/SetStorage/SetFlags 有成功日志且未 trap）
- 帧数仍可为 0（诚实记录）

**验收 B（能推则推）**
- 记录**新的**第一致命（下一个 trap 名 / 新 abort）写入 HANDOFF  
- `rg -n 'unimplemented NVN|MemoryPool|\\[fault\\]|guest-abort' .local/logs/p-mm-17b-guest-transcript.log | tail -40`

---

## 7. 收尾

1. `HANDOFF.md` 顶部写 **P-MM-17 闭环**：HEAD、是否越过 MemoryPoolBuilderSetDefaults、新阻塞一句话、契约 JSON、transcript 路径、改动文件清单  
2. `python3 scripts/project_status.py --write`  
3. `git add` 仅本单文件；commit：`feat(graphics): implement NVN memory pool family`  
4. 停下回报：HEAD / 是否越过 / 新阻塞 / 日志路径 / 改动清单  

---

## 8. 禁止

- 空转「找头文件 / 找 NSO」（§1 已定位）  
- 猜 ABI、万能成功桩、静默丢弃 SetStorage  
- 写入 guest builder/pool 对象内存  
- 夹带 command buffer / texture / sampler 家族  
- 声称标题/可玩/游戏帧  
- push / force / 动 main / 调 agy / 删 romfs  

做完停下汇报。

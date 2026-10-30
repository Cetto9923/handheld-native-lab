# T02 — 游戏真实代码原生样例（验收用例集）

任务来源：`TASKS.md#T02`、`docs/PROJECT.md` §原生定义。

**当前整体状态：NOT_RUN**（T01 路线尚未完成，选定样例未确定）。

> PROJECT.md §原生定义：「游戏派生代码以 Android NDK 编译为 arm64-v8a 原生代码并真实执行」。
> T02 的 PASS **只能**证明**被选中的那一个函数/子系统**。不得表述为"游戏已原生化"或"移植可行"。

---

## 重要前置事实（来自 T00R 复核，直接影响本组设计）

1. **引擎版本已修订**：T00 报告写「UE 5.4」，实测链接器模块标识串为
   `SDK MW+EpicGames+UnrealEngine-5_6_1`（`main.bin` 偏移 108,874,700）。
   本组所有涉及引擎内部结构/反射/符号的样例，必须以 **UE 5.6.1** 为基线，
   并在用例记录里写明所用引擎版本。
2. **模块职责已澄清**：`sdk` = Nintendo SDK 20.5.17 的 **NVN** 图形栈（+ libz / nnSdk / 64-bit build target）；
   `subsdk0` = Nintendo SDK 20.5.17 的 **movie**（多媒体/视频）；
   `rtld` = Nintendo 运行时加载器（`nnrtld.nss`）；**游戏与 UE 代码只在 `main`**。
   ⇒ **T02 的"游戏真实代码"样例只能从 `main` 中选**；
   `sdk`/`subsdk0` 属于平台库，是"要重实现或替换"的对象，不是"要重编译的游戏代码"。
   选样例时把 `sdk`/`subsdk0` 的函数当作 T02 样例 ⇒ 用例设计错误。

---

## 共享前置条件（NS 组）

| 编号 | 条件 | 校验方式 |
|---|---|---|
| NSPRE-1 | T01 已选定样例函数/子系统，并记录**原始模块 / 原始地址 / 原始 BuildId / 原始符号** | 样例卡 + `evidence/t02/<sample_id>/origin.json` |
| NSPRE-2 | 已建立**原始行为基线**：在参考环境（Switch 硬件或可信模拟器）上对同一输入记录输出 | `evidence/t02/<sample_id>/reference-input-output.json`；**基线缺失则 NS-003 必然 BLOCKED，不允许跳过** |
| NSPRE-3 | 原生实现已用 Android NDK 编译为 arm64-v8a，产出 `.so` | `readelf -h` → `Machine: AArch64`；`sha256` 记录 |
| NSPRE-4 | 已确认实现**不含**运行时通用 CPU 指令解释器/JIT | 依赖清单 + 符号表检查；见 NS-006 |
| NSPRE-5 | 已确认实现不含模拟器套壳/串流 | 无网络回传、无远程画面输入；见 NS-007 |

---

## NS-001 样例来源与原始身份绑定

- **前置条件**：NSPRE-1
- **步骤**：
  1. 记录样例所属模块名（必须是 `main` / `FFRS-Switch-Shipping.nss`）。
  2. 记录该模块的 NSO0 BuildId（`main` = `736837AFD390442ED7C74A7DE730E71B`）与 `.local/uncompressed/main.bin` 的 SHA-256。
  3. 记录样例函数在该模块内的**偏移地址**与反汇编/反编译锚点。
  4. 记录该函数的输入签名、寄存器/栈布局、返回值语义。
- **期望**：每一条样例都能回答"这段代码原本在哪、原本做什么"。
- **判定口径**：PASS 条件 = 每个样例都具备 `{origin_module, origin_build_id, origin_address, origin_symbol, input_signature, output_semantics}` 六项且非空。
- **证据产物**：`evidence/t02/<sample_id>/origin.json`（符合 `schema/acceptance-evidence.schema.json` 的 `build.source_module_refs`）
- **状态**：NOT_RUN

## NS-002 主机侧实现与行为对照（Mac ARM64）

- **前置条件**：NSPRE-1、NSPRE-2
- **步骤**：
  1. 用同一份输入向量驱动原生实现，收集输出。
  2. 与 NSPRE-2 的参考输出逐字段比对。
  3. 记录编译器、ABI、优化级别、构建命令。
- **期望**：输出与参考一致（浮点需给容差并写明容差来源）。
- **判定口径**：
  - PASS 条件 = 全部输入向量输出**逐位一致**（整数/位域）或**在写明的浮点容差内**（`rel ≤ 1e-6`，除非参考实现本身用更低精度）。
  - 任一不一致 ⇒ FAIL，且必须给出最小复现输入。
  - **主机 PASS 只证明主机平台**。记录里 `environment.platform` 必须写 `host-macos-arm64`。
- **证据产物**：`evidence/t02/<sample_id>/host-run/`（构建命令、退出码、stdout/stderr、输入向量、输出对照）
- **状态**：NOT_RUN

## NS-003 真机侧行为对照（RG477M）

- **前置条件**：NS-002 PASS、NSPRE-2、DEV-001 PASS
- **步骤**：
  1. 把同一原生库推送到 RG477M（`adb push`），用同一输入向量在真机上执行。
  2. 采集输出，与 NS-002 的主机输出**和** NSPRE-2 的参考输出三方比对。
- **期望**：三方一致。
- **判定口径**：PASS 条件 = 真机输出 == 主机输出 == 参考输出（同一容差口径）。任一不等 ⇒ FAIL。
- **状态**：BLOCKED_DEVICE（依赖 T03B 与设备）
- **证据产物**：`evidence/t02/<sample_id>/device-run/`

## NS-004 交叉构建产物身份（Android ARM64）

- **前置条件**：NSPRE-3
- **步骤**：
  1. 记录完整构建命令（含 NDK 版本、toolchain 文件、ABI、API level、优化参数）。
  2. `readelf -h` / `readelf -d` 检查产物：Machine、NEEDED 库、符号可见性。
  3. 记录 `.so` 的 SHA-256 与体积。
- **期望**：产物为 AArch64 共享库，依赖集合符合预期。
- **判定口径**：PASS 条件 = `Machine == AArch64` **且** `NEEDED` 列表中**不含**任何解释器/JIT 运行时（如 `libdvm.so`、`libv8.so`、`libmonodroid.so` 之类）**且** `.so` 的 SHA-256 已记录。
- **证据产物**：`evidence/t02/<sample_id>/build/ndk-build.log`、`readelf-*.txt`、`sha256.txt`
- **状态**：NOT_RUN

## NS-005 已加载模块与进程检查

- **前置条件**：NS-004 PASS、DEV-001 PASS
- **步骤**：
  1. 真机上运行后，用 `adb shell cat /proc/<pid>/maps` 列出所有已映射的 `.so`。
  2. 区分：探针/宿主库（自有）、系统库（`/system`、`/apex`）、第三方库、游戏派生库。
  3. 对每一项记录路径、大小、SHA-256（可读的）。
  4. 记录进程命令行与包名。
- **期望**：映射表可完整解释——没有来历不明的游戏二进制被整包加载。
- **判定口径**：
  - PASS 条件 = 存在**至少一个**本任务产出的游戏派生库被映射，且所有映射项都能归入上述四类之一。
  - 若出现"整包 NSP/NSO 被映射"或"出现无法解释的映射" ⇒ FAIL。
- **证据产物**：`evidence/t02/<sample_id>/device-run/proc-maps.txt`、`mapped-libs.json`
- **状态**：BLOCKED_DEVICE

## NS-006 排除运行时通用 CPU 解释器 / JIT

- **前置条件**：NS-004 PASS
- **步骤**：
  1. 列出产物与其依赖的全部符号（`nm -D` / `readelf --dyn-syms`）。
  2. 搜索解释/JIT 特征符号族（`interpreter`、`jit`、`compile`、`translate`、`opcode`、`dispatch` 等）。
  3. 检查是否链接了任何提供通用字节码执行的第三方运行时。
  4. 抽样验证：把一个"引擎函数"用 ARM64 原生指令重新实现后，其机器码应是 AArch64 指令而非数据驱动的解释循环。
- **期望**：无通用解释器。
- **判定口径**：
  - PASS 条件 = 依赖集合中不存在通用字节码执行运行时，**且** 抽样机器码为 AArch64 原生指令。
  - **仅凭"没有 JIT 这个词"不算通过**，必须有符号/依赖/机器码三类证据中的至少两类。
- **证据产物**：`evidence/t02/<sample_id>/build/dynsyms.txt`、`sample-disasm.txt`
- **状态**：NOT_RUN

## NS-007 排除模拟器套壳与串流

- **前置条件**：NS-005 PASS
- **步骤**：
  1. 运行期间抓取该进程的全部网络连接（`ss -tnp` 或 `adb shell netstat`），确认无对外画面/控制流。
  2. 检查进程是否读取主机上的 Switch 镜像/存档目录。
  3. 检查画面来源：是否为本地渲染目标（EGL/Vulkan swapchain）而非解码视频或远端帧。
- **期望**：无串流、无套壳。
- **判定口径**：PASS 条件 = 采集期内无出站连接指向非白名单端点，且画面来自本地渲染目标。任一不满足 ⇒ FAIL。
- **证据产物**：`evidence/t02/<sample_id>/device-run/netstat.txt`、`surface-source.txt`
- **状态**：BLOCKED_DEVICE

## NS-008 可重复性

- **前置条件**：NS-002 PASS
- **步骤**：用同一构建、同一输入向量连续执行 10 次，逐次比对输出。
- **期望**：10 次输出完全一致（无未初始化内存、无地址相关行为泄漏到结果）。
- **判定口径**：PASS 条件 = 10/10 逐位一致。
- **证据产物**：`evidence/t02/<sample_id>/repeat/`
- **状态**：NOT_RUN

---

## 组级出口条件

NS-001…NS-008 全部 PASS，且 **NS-003 至少有一条在真机上 PASS**，
T02 才可标记为通过。

T02 通过**只**证明被选中的那段代码。
不得据此声称：完整游戏可玩、标题画面可达、性能达标、帧率达标。

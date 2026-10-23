T02-NATIVE-HOST-CALLBACK：同步 owned HOST→GUEST 回调边界

新增 `nb_call_from_host(nb_scope*, void(*guest_entry)(void), nb_registers*)`。它只允许当前 owner 的真实 HOST phase2，复用已绑定 import；普通 `nb_call`/GP21 的同 scope HOST 调用仍为 NB_BUSY。没有临时 bind、裸 host cast、CPU/JIT、Process/OS/graphics 或游戏输入。本轮编译/静态检查完成，Android execution **NOT_RUN**，实际行为须由总控在 RG477M 执行冻结 ordinary normal/cleanup 合同确认。

scope 的既有 offset0..40 与总大小64B保持不变，原 reserved 两个u64现在是 `active_guest_x18@48`、`callback_depth@56`。这改变了内部字段的源名称与含义；原来直接使用 reserved 的客户端必须重新编译，文档本已禁止修改内部字段。新增 `NB_DEPTH_LIMIT=10`、`NB_MAX_CALLBACK_DEPTH=4`，`NB_RETAINED=9`及现有错误值不变。scope初始化仍清零，close/unbind 对非零 callback depth 也拒绝，不将未正常结束的 owner 假 reset。

gateway 在真实 GUEST→HOST 入口保存进入前的 active guest快照并写入当前 guest x18，随后才恢复 host x18/调用C。新回调汇编入口在任何 C/SCS prologue 前捕获当前 host x18（Hdeep），将它临时放入 scope.host_x18，使用实际 active_guest_x18（A）进入 callback；packet 的输入 guest_x18 被明确忽略。callback 可改 x18 为B并再次进入同scope已有host import，其 gateway 必须恢复 Hdeep。callback 返回后 packet记录B，而scope恢复原HOST phase、Houter、A、depth，host x18恢复调用新入口时的当前SCS深度。最终外层gateway恢复原guest的A。没有process-global host x18、TP写或Bionic TCB假设；TPIDR_EL0只用于既有owner只读核对。

回调入口纯汇编逐项核 null/alignment/magic/lifetime/owner/HOST phase/depth，再改变状态；拒绝不写 scope/packet。成功的272B帧保存x19–x29、完整q8–q15、SP/LR、当前host x18及外层字段。返回先保存真实GP/FP/x18结果，再恢复外层字段与HOST phase。固定depth4允许4层owned callback，第5次返回10；不支持无限递归、异步signal适配、foreign unwind、longjmp，异常离开后的active scope不会被当作正常完成。新入口只有既有register-only x0–x8/q0–q7契约，无variadic/aggregate/新增stackargs；GP21只能通过原明确104B接口。

v1 gateway私有帧从32B增为48B；GP21 gateway从144B增为160B以保存旧active guest快照。GP21读取原incoming SP的地址同步改为new SP+160，仍只复制13words/104B到callee SP+0..96，SP+104为8B padding，metadata从112开始，无参数/保存区重叠。nb_call原256B、nb_call_stack13原368B主入口帧保持原样。binding固定64B thunk、RX/BTI/cache与retained rollback流程未改变。

ordinary合同在原文件扩展，没有新probe：一条真实的 self-authored guest → 同scope预绑定import → SCS host C → 更深SCS helper → owned callback guest → 同一import链。每条有5次真实host进入、4次成功owned callback与1次depth refusal，重复3次。包seed为0xf00d+D，故意与真实A不同；guest读取A后改成0x80+D。host/helper分别验证Houter/Hdeep，拒绝与返回都对64B scope、208B packet做适用的完整字节比较。C目标使用真实7个scalar加指针参数、2个double及数据相关double返回；guest同时给出独立GP与FP寄存器结果。汇编guard逐项比x19–x29、q8–q15、x18，检查SP与正常LR返回，guest刻意clobber callee registers以核边界保存。现有GP21 typed21 C import中还加入owned callback seed leaf，检查v2 gateway实际guest快照及返回恢复。线程错误、无HOST活动、invalid/dead owner拒绝在普通已知状态下验证，不手改phase或用pthread_exit穿过guest栈制造通过。

构建命令：`NB_BUILD_DIR="$PWD/.local/native-host-callback/candidate1" bash scripts/runtime/build_native_boundary_contract.sh`。AppleClang21/API28/AArch64，normal合同真实 `-fsanitize=shadow-call-stack`，部件 `-ffixed-x18`，BTI。Host Mac compile/check exit0但明确输出UNSUPPORTED，不是native Android合同PASS。逐compile/link/ELF/symbol/disassembly命令的真实exit为0；ELF为16KiB LOAD/PIE/NX-stack/RELRO/NOW。既有官方partial NDK的实际输入hash/成员CRC复查，未称整包已校验。原normal GP/FP/SRET、GP21逐参/末字/栈canary/线程/lifetime/四个受控SIGTRAP合同保留；wrapped cleanup合同保留NB_RETAINED原double-failure/retry/idempotence路径，历史2f regression只构建，未申请再跑已知失败设备fixture。

`.local/native-host-callback/candidate1/manifest.json` 保存5个编译/构建源文件、输入与逐命令哈希、normal/cleanup ELF大小和SHA；报告本身不参与该编译源bundle。`checks/static.json`记录已执行的反汇编检查：callback_host/callback_deep有真实str x30,[x18],#8；新入口272B、v1 48B、v2 160B、incomingSP及第13word地址正确，边界无MSR。最终冻结commit与6文件精确清单另见同目录freeze.json。Root在独立设备目录校验ELF、chmod700后分别 `./native_boundary.android`、`./native_boundary_cleanup.android`（host端30秒有界）并保存真实exit/stdout/stderr；预期0尚不是实测0。

该组件为后续guest allocator/析构回调提供必要同步调用边界，不实现allocator、DTLS、__cxa_thread_atexit或SDK初始化，也不把之前7be GP21/81 LAS transfer的RG PASS当本次回调或CRT成功。新树独立于原5c/7be冻结树，集成与设备操作只由总控执行。


总控追加实测（作者冻结保持）：source434c4d13efcfd0e2ea20188bede031d6353e89ce，已同RG477M/Android14/ARM64执行normal raw0/265.250958ms、cleanup raw0/72.840083ms，两者noTimeout/stderr空。normal的owned_host_callback、same_scope_import_from_callback、callback_scs_deeper、callback_scope_packet_restored、callback_callee_gp_fp_sp、callback_actual_guest_x18_seed、gp21_host_callback均true，depth4/refusals3；原GP21与GP/FP/SRET/thread生命周期回归仍true。cleanup真实3munmap尝试/1释放/count0/idempotenttrue。源码/115输入/两ELF hash核对后新路径stage/readback/chmod700，未运行旧2f regression。独立QA已启动，尚未FINAL；root已集成7f9c42e，集成不是独立review通过。原始证据.local/device/rg477m-session1/native-host-callback-v1-{plan,stage,execution}.json及各stdout/stderr，运行run_native_host_callback_v1.py。不外推Process HOST消费者、完整CRT或游戏成功。

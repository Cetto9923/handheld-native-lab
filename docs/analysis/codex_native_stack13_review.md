# T07 固定 GP21 / stack13 独立审阅

冻结：`7beeb22294311cc3c896ec23ac23561fca9d6b6d`，五文件通过 `git show` 提取，原工作树只读。**已完成的静态/编译范围未发现可复现新增缺陷；不能将本轮称为独立完整 Android 构建或设备验收 PASS。** 完整 Android C 组件编译在20秒超时停止，未重试。独立严格 host 编译、host unsupported 执行、Android目标汇编与独立 freestanding C21 ABI对象均 exit0。

## 固定参数运输与保存

| 方向 | 独立机器码核对（本对象 .text 偏移） | 结论 |
|---|---|---|
| host→guest `nb_call_stack13` | `0x264` 减 SP368；`0x29c..2d0` 从显式 packet 读6对+最后1个u64，存 SP+0..+96；`0x2d4` 仅清 SP+104 padding；`0x308` 调目标。 | 参数104B、栈区112B；保存区从112开始，范围互不覆盖；368是16的倍数。输入 packet 不被写回。 |
| guest→host `nb_import_stack13_gateway` | `0x3e4` 减 SP144；`0x3f4` 求原 SP=`new SP+144`，`0x3f8..42c` 复制同样104B；`0x430` 清padding；`0x43c` 恢复当前scope的host x18后，`0x440` 才调用host。 | 没有误从已调整SP读取参数；参数区与112..143元数据分离；返回后`0x454`恢复guest x18，SP/LR成对恢复。 |

两条复制路径只用 x9/x10 scratch，未覆盖 x0..x8/q0..q7；入 guest 前从 packet 装载这些寄存器，返回后存回实际结果。host→guest 保存并恢复 x19..x29、q8..q15及host x18；gateway自身只改的callee-saved x29另行保存，typed host目标仍须符合AAPCS64。代码仅读 TPIDR_EL0作owner token，无MSR；不推断Bionic/guest TLS布局。

独立 `independent_gp21_fixture.c` 不复用作者的copy macro或checksum实现。严格 freestanding Android A64编译后的真实C caller将a8..a19放连续SP+0..+88，a20放SP+96；C callee读取同范围。caller实际有 `str x30,[x18],#8` / `ldr x30,[x18,#-8]!` SCS操作。此为**编译器ABI旁证，未运行**，不代替native boundary设备执行；未另建运行probe。

## 版本、scope与失败路径

- v1汇编前缀与直接父版逐字相同，见 `v1-prefix-check.json`。C的v1签名条件只是将原拒绝逻辑重组为同一布尔条件；v2另限定version2、flags恰4、stack bytes恰104、GP8/FP0、u64返回1，variadic/aggregate/unknown均拒绝。v2没有开放sret或FP签名推断。
- v2在任何guest x18加载前检查pointer/alignment、magic、lifetime、owner TP并claim phase；BUSY失败不改packet。导入gateway在恢复host x18/调用C之前检查存活、owner和GUEST phase。host x18取自当前ASM入口，支持已在SCS调用深度内的不同scope嵌套，不使用全局/线程初始x18替代。
- live binding阻止close；dead且非idle owner仍保留。v2仅改变选用gateway literal，没有改RX失败/munmap失败分支：rollback失败仍保留mapping/scope/count，entry为NULL；unbind只有真实munmap成功才减count并清owner。原父版retained合同已由独立审查接受，本轮未重新运行其Android路径或扩大生命周期承诺。
- 正常SP恢复、无foreign exception/longjmp/异步信号适配、packet与scope/registers分离及控制对象生命周期仍是显式前提。本轮不把栈访问fault扩成可恢复状态，不尝试非法执行。

## 实际验证与限制

`commands/*/{command.json,run.json,stdout.log,stderr.log}`保留原始命令/码；`manifest.json`保留五源文件前后hash及产物hash；`abi-check.json`记录14个copy行/范围和对象PC。

- strict host：`-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic`编译exit0，冻结普通contract执行exit0，输出明确`UNSUPPORTED non_android_a64 / Android NOT_RUN`。
- 完整Android C对象：使用既有partial NDK、目标API28、fixed-x18/BTI，在20.140491s超时，stdout/stderr均空，exit_code=null/timed_out=true；没有编译错误诊断，原因未确定，不能归因为代码缺陷。未重试；后续完整contract/cleanup编译和链接未执行。
- 不依赖该NDK头的**独立步骤**：冻结Android A64汇编编译exit0；自有freestanding typed C21对象（fixed-x18/BTI/SCS及严格告警）编译exit0；两个反汇编命令exit0。没有冒称它们补全上述C组件构建。
- 默认只读`ps`被sandbox拒绝`operation not permitted: ps`，exit1；没有升级或重试。Python timeout已kill/reap直接clang。总控随后单独获准核查，报告选定clang/cc1均无，证据为 `ffrs-integration/.local/host-verification/process-after-review-timeouts.json`；这属于总控证据，不是本审阅执行。

总控另报告同冻结版RG normal exit0（227.309792ms，含14种签名拒绝）、cleanup exit0（55.494125ms，3次unmap尝试/1次真实成功/count0），证据入口 `ffrs-integration/.local/device/rg477m-session1/native-stack13-v1-*`。本审阅 **Android设备NOT_RUN，无ADB**，未读取SDK/main/游戏；不得把这些数据当本人的独立设备结果。组件尚未集成、原musl仍在133边界的状态没有由本轮审阅改变。

可交付对象：`build/boundary-independent.o` SHA256 `7288baae9367bd9c648e219bf189b5272fc511fccbf9c0e86f1c678e1e94ef20`；独立ABI witness `build/gp21-freestanding.o` SHA256 `89c427feee83f31254ebd8bf42a9c0debf61425bcf98d66c08f7d7e667f18e6b`。后者仅供编译器布局检查，无新增设备运行要求。

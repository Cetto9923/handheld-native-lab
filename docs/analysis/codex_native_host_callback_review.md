# T07-NATIVE-HOST-CALLBACK-REVIEW — FINAL（限定范围）

**冻结 `434c4d13efcfd0e2ea20188bede031d6353e89ce` 在本次独立源码、普通 C/ASM、ABI 与离线构建审查中，未发现需修复缺陷。** 已独立核对 root 的同冻结版 RG normal/cleanup 成功证据；reviewer Android **NOT_RUN**、无 ADB。本结论不是游戏、Process、CRT/TLS 或完整启动验收。

## 冻结与独立输入

六文件从精确 commit 用 git-show 独立提取，匹配作者六文件摘要 `f21e3031ff6b3697e7d98f0ccd336cfeaa96ffdf65aa07f00a0c0ba4f3c40f55`；编译五源 bundle `c3cd96fb49d88415b612cb3a8ca17daa1900f43fca372a486a72c0640e965ab9` 也匹配。作者115项输入已逐项重新计算大小/摘要。新旧源分别保存在本目录快照及既有 7be 独立快照，完整命令/真实退出见 `frozen-434c4d13/commands/`，机器核对见 `frozen-434c4d13/verification.json`。

## 关键独立核对

| 项 | 证据与结论（PC 为本审查独立 assembly.o 的 .text 偏移） |
|---|---|
| HOST 准入 / 深度 | `0x4b0..4ec` 检查 magic、live、当前 TP owner 与 HOST=2；`0x4f8` 在原深度≥4时跳至返回10，先于 `0x50c` claim 与任何 scope/packet store。IDLE/GUEST 非HOST、wrong/dead owner拒绝无 packet/scope 写入。旧 nb_call 在HOST仍BUSY。 |
| 三份 x18 | `0x548` 保存当前 Hdeep 与 scope 中 Houter；`0x54c/550` 保存真实 active guest A及旧depth；`0x56c` 以 A入guest，未读取packet guest_x18 seed。`0x5b0` 将真实返回B写packet；`0x5c4..5dc` 恢复 Hdeep/Houter/A/depth/HOST。该区无C调用，TP仅读。 |
| 新 callback frame | SP减272，16B对齐；x19–x29/LR与q8–q15保存区0..223，控制元数据224..271，不覆盖。返回先记录 x0–x8/q0–q7/B，再恢复 host 保存值与SP/LR。BTI入口存在。 |
| 两种 import gateway | v1 frame48、GP21 frame160均保存/恢复上一 active guest；实际guest x18先入scope，恢复host x18后才调用C。GP21 `0x414` 的原incoming SP确为新SP+160；`0x418..44c`运输104B，`+104`仅padding，metadata从112开始，末参数+96无偏移错误。 |
| 真 SCS / 正常回入 | 独立编译的 callback_host、callback_deep 均有真实 `str x30,[x18],#8` / `ldr x30,[x18,#-8]!`。冻结合同通过正常HOST→guest→同binding import递归生成4个callback，第5次拒绝；区分packet seed、A/B、Houter/Hdeep，并逐64B比较scope。未手改phase/depth制造路径。 |
| 生命周期 | close/unbind 在phase或depth非零时保留；dead-owner仍先检查life，不能把未完成回调当正常返回。原真实munmap失败/RETAINED计数规则保留。本次非idle dead-owner保留为静态核对，未制造异常退出。 |

旧 `nb_call` 364 B与 `nb_call_stack13` 436 B机器码分别与旧7be独立对象逐字相同。新旧公共layout独立C程序输出一致：scope64/align8、旧字段offset0/8/16/24/32/40；packet208/align16、guest_x18=72、q=80；signature20、stack104、binding40。新字段48/56及status10/limit4在编译时另核。合法旧消费者没有scope膨胀；新API调用方须用新头，**新C/S必须成套**，不能把旧gateway与新callback实现混用。访问旧reserved字段的源码需调整，那些字段原本已声明为内部字段。

## 实际执行 / 构建归因

- 本人：严格 `-O2 -Wall -Wextra -Werror -Wpedantic` host构建和合同 exit0，输出明确 Mac/non_android_a64 **UNSUPPORTED**；新旧layout程序各exit0。Android C、ASM、SCS正常合同、cleanup合同独立交叉编译各0；normal与cleanup最终链接各0；三份反汇编及独立元数据/ELF/ABI核对各0，无超时。原partial NDK路径复用，无下载/安装。
- 本人第一次 cleanup 链接漏带冻结builder的 `--wrap=mprotect --wrap=munmap`，真实exit1（__real_*未解析）；原失败日志保留，按原builder补齐选项后exit0。未改被审源码、未重复编译，也未将此误记为组件缺陷。
- 本人独立产物为A64 ELF/16KiB PT_LOAD、无W+X、RELRO/非执行栈；normal无cleanup测试hook。仅离线构建，**未在Android执行**。没有新建额外设备probe；现有冻结合同已覆盖本次必要正常观察量。
- **root 实测**：已核对 plan→freeze摘要、stage设备RG477M/arm64-v8a、远端sha256、execution与两stdout/stderr摘要。normal产物129064 B（`b070e7883b6ac9a0c7571ed60c16adfa483ceeb4251d25331b526d76a94d18d1`）exit0 /265.250958ms，callback新字段全true、depth4/refusals3、原GP21回归保留。cleanup37032 B（`4be7c5e6b83d2ddcdba9a35f575a8975b56b847d6fd60e08b3b30bb2d81683e2`）exit0 /72.840083ms，3次munmap尝试、1次真实成功、最终count0；两例无超时、stderr空。入口为 `ffrs-integration/.local/device/rg477m-session1/native-host-callback-v1-{plan,stage,execution}.json`。这些运行属于root，不是本人独立设备执行。

无剩余阻塞或修改请求。接受边界是同步、正常返回、显式register-only HOST callback；没有新增stack参数/variadic/aggregate、foreign unwind/longjmp/异步信号支持，没有SDK/main/loader或TP布局适配。既有7be的旧C编译超时记录不被抹除；本冻结新C编译确实已完成。REA未调用，普通源码不需反编译引擎。

# T07-OS-TLS-REVIEW

冻结 `9c24ed35e683e0aa426ae2561dac670a8ead8ef1`，读取时 worktree HEAD 匹配且 clean。独立普通 C/POSIX 功能审查完成，未发现需要修复的具体缺陷。独立严格编译、normal/recovery 契约与必要补测均实际 exit0；此结论仅覆盖独立逻辑 OS-TLS 组件。

## 独立执行与输入

四文件均从冻结 SHA 用 git show 提取，编译只使用本目录 snapshot，不使用作者可变工作树。精确 argv/cwd、输入 SHA、stdout/stderr 和真实退出码在 manifest.json 与 logs；执行驱动为 build_review.py，补测为 ordinary_supplement.c。

| 独立执行 | 真实退出码 | 结果 |
| --- | --- | --- |
| normal contract | 0 | 1351 checks / errors0，8 native workers ×16 cycles |
| recovery contract | 0 | 1419 checks / errors0，FAILED 与 UNKNOWN provider 两例 |
| 自有普通补测 | 0 | 同槽释放/再分配后动态回调、零值 preflight、暂停 provider 期间读取/BUSY、其他 manager 进展 |

共11个捕获命令均0：版本检查、owner/normal/recovery 三次严格编译、两次链接、两个原 contract 执行、补测编译链接及执行、一次输入/证据完整性核对。没有失败、pending、uncertain 或被拒绝动作。严格参数为 `-std=c11 -O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic -pthread`，normal 与 recovery 共用同一未加 hook 的 production owner.o。未运行作者构建脚本，也未将作者自测当独立执行。

## 源码与 SDK 静态报告核对

只读取冻结的 `codex_tls_sdk_lifecycle_report.md` 与 `codex_tls_exit_hook_report.md`，记录各自 SHA，未读取报告指向的原 NSO/模块或其切片。报告用于核对普通槽语义，不冒称本评审重新验证 SDK 指令。

- `guest_os_tls.c:65–84`：APP 0→15，SDK 31→17，16 保留；独立 allocated 标记使 destructor identity0 仍占槽，语义对应 SDK 的非零 noop 哨兵。一个 manager 锁下先清所有登记记录的该槽值，最后才发布 u32 槽输出；耗尽与其他失败不写 allocation output。counts 分别报告 APP/SDK 使用量，没有把 SDK 槽混入 APP 数。
- `:86–119`：Free 只释放 callback 身份，不调用 destructor 或清 stale value；未分配槽仍可正常 Get/Set，随后同槽 Allocate 才对所有登记记录清零。slot16/越界采用明确组件状态返回，未声称这些防线等同 SDK 的 unchecked Get/Set。
- `:149–204`：第一轮对每个 allocated real destructor 调用，即便值0；后续只对非零值重读当前 callback 表，最多五轮。回调返回后清 CURRENT 值，而非先清或仅清旧快照；最后一轮可留下已经扫过槽的非零值。CLEANED 表示扫描完成，不能解释为全零或 guest object 已释放。原契约实际验证五轮后余值305仍可读并允许注销逻辑记录。
- `:174–194`：typed invoker 收到不透明 u64 destructor/value，未转换为 host function pointer。解 manager 锁后才调用，返回后再锁并清值。自有补测释放并重分配当前槽：旧 callback101 → later slot callback102 → 下一轮新 callback103，实测事件和值分别为 (101,11)、(102,22)、(103,66)；同槽回调内重设55/77被 post-return clear 覆盖，符合报告顺序。
- `:154–166`：NULL provider 且存在 real destructor 会在进入 CLEANING/清值前返回 NEEDS_INVOKER，之后可提供 provider 重试。自有补测验证值全0时也不能跳过 preflight；既有契约验证有值时保持全部值与 LIVE 状态，再正常重试成功。
- `:182–189`：provider FAILED/UNKNOWN 均保存 failed round/slot/destructor/input value/provider error，保留回调已修改后的当前值，不执行 post-return clear；thread 和 manager 同时 quarantine。既有 recovery 实测 failed input111 与 current333/444 分别保留，读取/count/report可用，后续 mutation/cleanup/unregister 被拒绝，destroy为BUSY。

这里的“recovery”含可安全重试的 NEEDS_INVOKER，以及不可重试、永久保留的 provider 不确定结果，不能将两者混为“失败后重新运行析构”。recovery 子进程实际保留两个 manager/两个 thread 直到进程退出，retained计数2；这是声明的 ownership 策略，不是“全部释放通过”。

## 并发与生命周期

`guest_os_tls.h:57–66` 明确 caller 负责 handle 有效期、API entry/注销同步和清理时 value user quiescence。身份匹配先查 manager 链再解引用输入 handle；自有补测以另一个 manager 中仍有效的记录验证 NOT_REGISTERED 且 Get output 不写。注销后的 stale pointer 或并发释放不是受支持输入。

CLEANING 与 manager.cleaning 保留当前逻辑 thread/manager，callback 期间注销当前 thread 为 BUSY，destroy 因登记记录/cleaning为BUSY；每个 manager 仅允许一个 cleanup。自有 condvar 同步补测在 provider 暂停时从另一 host thread 读取 Get/count/report，检查 CLEANING/invocations1、其他同 manager thread 的 cleanup 为BUSY且仍LIVE、当前注销与manager销毁为BUSY；同时另一个独立 manager 可完成 cleanup/unregister/destroy。没有在清理期间使用不受声明支持的无关并发 value writer。

原契约另有正常 8线程×16 注册/Set/Get/cleanup/unregister 周期，以及 provider 同线程 Get/Set/Allocate/Free/Register 重入、同/另一 record nested cleanup BUSY。cleanup完成后注销才释放本库逻辑 record，destroy要求没有登记记录及 caller排除并发API；opaque value/destructor 身份不代表本库拥有其 guest object/code。

## 总控真实 RG 证据，非本评审执行

只读复制 `ffrs-integration/.local/device/rg477m-session1/os-tls-v1-{plan,stage,execution}.json` 与 `os-tls-v1.stdout/.stderr`。已独立核对 HEAD、四源码与两份报告 SHA、实际输入 artifact 和 stdout/stderr 摘要，副本与 SHA 见 coordinator-evidence/manifest。

总控 RG normal contract 实际 **exit0、45.649208ms、无timeout、stderr空、1351 checks/errors0**。Android产物112864B，SHA256 `22d3a937d68e03718628ee06aa158b44ef8b4eacc7f1c8ea64d9c13a22d48cba`。该设备执行 recovery_cases0、retained0，只证明 normal 设备组件；没有把 host provider-failure 两例或本评审补测称为已设备执行。本评审 Android 执行 NOT_RUN。

## 覆盖边界

本单元是显式 manager/record 的32个u64逻辑值模型，实际可分配31槽；槽16所有者仍未知。没有隐式 current thread、SDK ThreadType/TP layout、SDK Result/Abort 映射、ELF TLS/destructor list 或 HLE/resolver 绑定。普通 SDK 返回路径的 OS槽→通知→hook→ELF drain 顺序来自静态报告，这个组件尚未接入真实线程退出路径，也没有实现 guest callback ABI/provider。

调用方必须维持 provider/guest code 的生命周期，并确保正常返回；异常/nonlocal exit、异步取消、未quiesce的并发 value writer 不在契约。未读取或运行 NSO/游戏、操作ADB/TP或旧异常QA流程；未修改仓库/Git/配置，全部输出位于 functional-review/os-tls。上一 native-boundary 档案未再修改。

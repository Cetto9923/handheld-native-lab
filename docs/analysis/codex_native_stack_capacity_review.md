# T07 startup capacity — independent ordinary functionality review

冻结：`d4ded908096feef6fff2876ab2ffdaa9e48303b5`。本次独立 host 普通组件审查与契约通过，未发现仍需修复的具体功能缺陷。原 host guard-extension P2 已闭合。总控在匹配该冻结的真实 RG 上报告 Context/profile/info 为 0；原 main 两条路径仍为明确 unsupported 133，不是 CRT/游戏通过。

所有新结论来自新冻结快照与重新执行，未复用旧版本的 QA 作为本版 PASS。范围为冻结九个 scope 文件及同 SHA 十一个依赖文件；逐文件 SHA、精确 argv/cwd、原始 stdout/stderr、产物 SHA 与真实码见 `manifest.json`。正向组件 fixture 9216B，SHA256 `a8467dc70098b693ab2d0266ac9ec99c705b76a644da20fbc0e5b263cd237493`。未读/运行旧异常 NSO QA、`test_loader.py` 或可变 WIP，未运行完整作者脚本及其可选原 main/Android 步骤。

## 独立结果与真实退出码

AppleClang C11，`-O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`，真实 host libc++ guard runtime。19 个严格编译/链接成功命令均 0；另有两个故意缺少 typed guard 能力的编译按约拒绝 1。

共 38 个独立命令：**33×0、1×-6、4×1**。这包含真实被拒绝/终止及一次评审启动参数错误，未把流水线全说成 0。完整分布见 `summary.json`。

| 执行 | 实际退出码 | 验证范围 |
|---|---:|---|
| 新 Context normal / recovery | 0 / 0 | 实际 64KiB SP/local、raw/native/logical、outer guards、host TLS、join、8线程配额2accepted/6LIMIT；recovery含map失败1、容量重建1、容量拒绝3 |
| 新 profile normal / recovery | 0 / 0 | 两次 callback、七字段/padding、VA归属、ThreadRecord、signal恢复；recovery含五个正常loader/限额阶段 |
| 无活动 profile 的 void Query | -6（shell134） | 精确 `[query-invalid]`，显式 abort |
| legacy info / profile info / profile默认旧路径info | 0 / 0 / 0 | 同一正常 fixture 的信息路径 |
| module quota不足 | 1 | NSO_LIMIT，module mmap前拒绝 |
| guard macro未定义 / 显式0 编译 | 1 / 1 | 缺 typed guard 支持时明确编译拒绝 |
| 跨TU guard契约 | 0 | profile TU宏开、nso_host TU宏关；三地址仍为真实runtime、extension0、abort→retry→release |
| 容量边界与raw释放补测 | 0 | 六个边界、exact logical quota、4页上限、完整raw retained/retry、线程destructor先完成 |
| profile补测正确argv | 0 | owner回调失败/retry、exact/不足stack quota、既有altstack/handler/mask恢复、共享output不写 |

自有 `profile-extra-contract` 首次启动漏传必需 fixture，入口 `argc==2` 检查即退出 **1**，尚未进入测试体。原命令/日志保留。只补齐 frozen `contract.nso` argv 后，在独立新日志项退出 0；源码与断言未改。该启动错误不归为产品缺陷，也不隐藏。

## 六项容量修正契约的闭合

1. **就绪来自公开实测。** `guest_context.c:144–203` 在实际 child 使用 pthread 公共属性取得 native range，再验证范围、容量、SP/local，失败不调用用户 callback。Context 的 descriptor 在 `:119` 要求已经有 native bytes，profile 的 ThreadRecord/Query 仅在该 callback 之后就绪。784B 只出现在正常测试 fixture 中，不是生产 sizing 常数。
2. **五类范围分明。** `guest_context.h:33–43` 分别保存 exact logical window、raw RW allocation、native usable、admin 与 outer guard 信息。逻辑窗口按 native usable 的 top 向下取请求大小（`:187–193`），完整包含在实测 RW 范围。两类契约继续检查真实 SP/local、native/raw containment、logical精确长度、字段/padding与VA，并改用 raw allocation 边界查询两个 guard；没有靠删除旧size断言通过。outer guard保护raw mapping，未冒称紧贴可能非页对齐的logical边界。
3. **增长有界、guest计量不变。** `:233–238`、`:302–314` 明确最多3次、最多4 host pages admin，溢出检查后对累计差额页舍入。每次都是 join → 完整 raw munmap → refund → 下一次 precharge。`guest_charge_reserve`仍先于 mmap，ticket始终只扣请求的logical payload。独立补测在quota恰等于logical时，差1B补1页、差page+1补2页、差4pages补4页均成功；差4pages+1、持续不足到第3次、属性查询EIO均不进入callback并清账。
4. **失败保留完整raw ownership。** `:205–220`、`:285–301`、`:322–347` 使用原始 allocation/guard 形成的完整映射 base/size清理；未成功不退 ticket，join不确定不强拆。独立测试仅在自有非生产对象中以编译宏将 munmap 显式转接到普通受控shim（`-Dmunmap=review_munmap`）：补admin后首次释放返回EBUSY，Context保留charged=65536/ticket1；线程pthread-key destructor已经完成。destroy用同一完整raw base/size重试一次成功，shim记录证实未用偏移logical地址释放。该shim与GUEST_CONTEXT_TESTING均不在生产产物。
5. **更强断言与host TLS保留。** 新 Context及profile契约将“reported size必须等于65536”替换为“reported容量至少logical + raw/native/logical关系 + exact SP/local窗口 + outer guards + admin上限”，保留原Query、padding、VA、返回指针、信号与配额断言。normal Context仍报告未知SDK七字段valid mask=0。未把原hostTLS替换为guestTLS。
6. **独立冻结与跨TU guard修正。** `guest_process.c:2–4` 无typed guard的host构建明确#error，`:16–20`和`:240–242`直接引用三真实typed compiler symbols，绕开通用HLE extension fallback。独立把nso_host.c按宏关闭构建、profile TU按宏开启构建后，三地址仍匹配真实runtime，extension_calls=0；正常acquire/abort/retry/release成立。`nm`见三强未定义runtime引用。旧15e P2因此在本冻结闭合，未只依赖单个构建脚本的宏一致性。

生产 host profile 的 `nm -g` 未包含 `guest_context_test_*`、`nso_loader_test_vm` 或 `review_munmap`。所有源码与fixture测后摘要一致。Query失效不写/显式fatal、一致private owner快照、empty manager/closed resolver、loader typed retained/retry/observer均使用本冻结重新编译并在normal/recovery及补测中验证；没有发现新的“假Query成功”路径。

## 历史失败不能抹去

只读 Git 历史确认连续三个检查点：

- `15e498c0985aee20c973632b6406e2b976628d81`：真实RG原contract exit1，汇总断言失败，保留FAIL。
- `e95e521c79bead1c6de4f64d5b01416f1f639ec1`：保留所有原断言的诊断版在RG仍exit1；定位native usable64752B / 0xfcf0，比提供mapping65536B少784B，其他分项及returned pointer符合。它是定位证据，不是PASS。
- `d4ded908096feef6fff2876ab2ffdaa9e48303b5`：本次容量修复；新host独立结果与新RG总控结果分别记录。

前两次原始/诊断stderr、结论仍在同级 `startup-profile/` 的报告和coordinator-evidence中。新通过没有追溯改写旧结果。

## 总控新RG证据，非本评审执行

本评审只读复制了总控明确给出的 `capacity-v1-plan.json`、`capacity-v1-execution.json` 与 Context/profile stdout/stderr，未连接设备。计划source_freeze=d4ded908；21项相关source SHA与本次git-show快照一致，Context/profile日志摘要与execution记录一致。计划中 `test_loader.py` 的摘要仅作为外部元数据，未读取该文件。副本/源路径/大小/SHA在manifest.coordinator_evidence。

总控实际RG（4KiB页）Context和profile contract均0，info0，无timeout。raw RW=69632、native usable=68848、exact logical=65536、admin=4096、attempts=2；首次64752B只探测、callback withheld。新契约报告guards、SP/local、TLS、七字段、padding、VA、returned pointer、join后清账均符合。生产设备无故障测试hook，设备controlled map/cleanup failure cases=0，不能称真机错误注入通过。

总控原nnMain路径越过Query(+0x27a50)后到AllocateMemoryPages index71/+0x27f90，exit133；malloc路径到VAusage index66/+0x144b8，exit133。上述两项属于总控证据；本评审没有执行原main，也未将133改称0或CRT/游戏完成。

## 执行边界

仅独立自有普通C/POSIX组件、正向fixture装载与CLI info；未执行原main/guest函数、游戏、TP写路径、ADB、旧异常输入/安全fuzz。未独立Android交叉构建或执行。单guest、外部raw API静止、生命周期由调用者串行及只读静止输入等限定继续有效；legacy init/call/symbol未在本轮实跑。本报告无剩余组件功能blocker；SDK CRT/TLS和新的page/VA-budget门槛仍未实现，不作一般SDK或游戏兼容结论。

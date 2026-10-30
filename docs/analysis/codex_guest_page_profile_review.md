# T07-PAGE-PROFILE-REVIEW — independent ordinary integration review

冻结为 profile四文件 `f24b826b5d118b2479a476ec61aba778df26e021` + 独立页组件 `4695e3c995993832ff8e274415f58b1cdb1e4684`。本次独立严格host正常/恢复及必要集成补测通过，未发现需修的具体功能缺陷。Query没有把backing_valid误当所有权/退款完成；SDK失败路径没有返回猜测的Result。此结论不是原main/SDK CRT/游戏启动通过。

## 精确输入与执行

f24是四文件增量，本身未含guest_pages.c/h。首次按单SHA提取该文件的git show退出128（源码提取阶段，未构建/运行），已在manifest.setup_correction保留；随后按总控指定4695提取两文件。其余依赖均取f24，逐文件source_revision/SHA已记录；4695与f24的Context/address_region四文件比较真实退出0。构建脚本也明确支持独立 `--pages-source-root`，未混用WIP或把依赖伪称属于f24。

正向fixture9216B，SHA256 `a8467dc70098b693ab2d0266ac9ec99c705b76a644da20fbc0e5b263cd237493`。未读/执行test_loader.py、旧异常NSO流程或作者完整脚本（其中有原main/Android步骤）。所有输入测后SHA一致。

AppleClang C11、`-O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`。18个严格成功编译/链接均0，缺typed guards的profile编译按约拒绝1。共32个捕获构建/检查/执行命令：**27×0、3×-6、2×1**，无timeout，精确argv/cwd/日志/产物SHA在manifest，分布在summary。另行记录上述源码查找128，不混入构建成功数。

| 独立普通host执行 | 实际退出码 | 覆盖 |
|---|---:|---|
| frozen normal / recovery | 0 / 0 | 两次真实native callback；Query加入实际页charge、exact free退款、remap zero、live页whole owner cleanup；五个loader受控恢复阶段 |
| 无活动profile的void Query | -6（shell134） | 明确query-invalid abort |
| profile info / legacy info / profile默认旧路径info | 0 / 0 / 0 | 正向fixture信息路径，legacy功能未扩展 |
| module quota不足 | 1 | 明确限额拒绝 |
| retained Query + partial destroy补测 | 0 | 两种真实保留ticket状态不写output、修复后有效；先清页后module失败并retry |
| SDK页quota失败 / partial free | -6 / -6（各shell134） | page-unsupported明确fatal，无猜测错误Result |
| 跨TU guard | 0 | HLE TU宏关、profile TU宏开，真实runtime三地址/extension0/abort-retry-release |

生产normal binary无测试Context wrapper、guest_pages_usage_real、loader VM hook或guest_context_test符号。

## Query消费与所有权

`guest_process.c:150–176` 从唯一page owner取得已锁定的pages+Context快照，再核对真实primary descriptor。除ledger/backing valid外还拒绝quarantine、pages.pending_bytes、Context pending、backing_bytes!=charged_bytes、ticket数不符、module extent不符和所有sum溢出。Context tickets必须恰为module+primary两个身份加page owner的per-allocation tickets，charged必须恰为module+logical stack+pages。module/primary在实际callback中不变；没有复制第二份VA/page ledger，也不靠总量相等推定来源。

独立 `profile_extra_contract.c` 以测试链接wrapper使用真实page owner/Context操作构建两个状态，未改生产源码/私有结构：

1. page map成功但commit受控失败、rollback成功、cancel暂失败：page usage实际返回OK，ledger_valid/backing_valid均true，backing0但pending=1页/ticket1。
2. page map/commit成功、真实exact VM free成功、release暂失败：同样实际usage OK/valid，backing0但charged=1页/ticket1。

在registered native callback内调用typed `guest_process_memory_info`，两次均NSO_INVALID，完整output（含padding）保持0xa5。通过真实ticket重试/region release修复后，Query恢复OK且字段回到原值。该测试证明本consumer没有把“VM数已知”误解为“生命周期完整”；测试wrapper不是新增可恢复SDK接口。SDK包装层遇到同类非OK仍按公开profile约定终止。

`guest_process.c:26` 仅有私有guest_pages authority；原va_record/region总量副本已删除。模块ticket仍由loader同次extent预扣/commit，primary由冻结Context拥有，page tickets由独立页组件拥有。嵌入契约仍要求单guest与外部raw API静止，生命周期由调用者串行。

## SDK包装与错误边界

`guest_process.c:221–254` 四个包装只把page owner完成的真实transition成功映射为0；任何非OK先保留quarantine/完整typed诊断，再打印 `no unproved SDK Result mapping` 并abort，不把native errno、内部enum或原raw region_result改造成SDK错误码。attempted标志仍随诊断保留。

独立真实子进程分别验证：module+stack已耗尽quota时页map失败；一个2页allocation被请求只free1页时失败。两者都SIGABRT(-6)，后继C语句未作为SDK返回继续执行；stderr明确page-owner-error/page-unsupported，而非合成错误Result或成功。有效disjoint map/exact free/remap zero由本冻结正常契约实际验证。

resolver只新增两条精确typed page入口，仍拒绝malloc/calloc/mmap/mprotect/pthread_create/nvn/SDK初始化等未支持导入。三cxa guards继续直接绑定真实compiler runtime，guard禁用编译拒绝与跨TU对照均重新执行，无extension回退。legacy CLI构建排除Context/process/pages依赖，普通info路径及profile无policy选项的旧info路径仍0；未执行legacy init/call/symbol。

## 部分cleanup和heap零值声明

`guest_process.c:347–366` 在primary完成join后，先destroy sole page owner，成功才清p.pages；之后typed module release，最后Context destroy。页/模块失败均保留process供retry，不丢ticket或重复owner。

独立补测保留一个真实live页，随后只让module image第一次munmap受控返回EBUSY：第一次destroy为RETAINED，page raw regions已实际清空、module ticket未退、image仍保留；不能再次run。第二destroy跳过已经清空的page owner，正确重试module，image真实释放之后模块ticket恰drop一次，新process/owner可重新建立，exit0。normal/recovery原契约同时保留loader observer一次及typed release恢复覆盖。

`guest_process.c:143–149` 的heap=0/blocks=0只声明一次初始化的READY_EMPTY **native heap manager**；Vamm page backing另有真实owner并计入used，不把页数写成heap block或原SDK heap状态。没有暴露heap allocator mutator，不称原SDK manager/CRT已经初始化。baseline在module+初始stack就绪时缓存，页map/free只改变used，六u64+一个u32宽度和padding保持原契约。

## 总控真实RG证据，非本评审执行

本评审仅只读保存总控指定 `page-profile-v1-plan.json`、`page-profile-v1-execution.json` 与contract stdout/stderr。计划分别确认f24和4695两冻结；17项f24相关source SHA与本次快照一致，page组件使用指定4695快照，contract日志SHA与execution一致。test_loader.py仅见于外部计划摘要元数据，未读取其文件。源/副本路径、大小及SHA见manifest.coordinator_evidence。

总控RG contract实际0/52.846834ms、info0、均无timeout；contract714024B，SHA256 `ad7d9995b3bcb3f5aff23c35cb9e51573bfd7f188c07647a7b0854bdf77f28c6`。生产device无failure hooks，cleanup_failure_cases=0；本评审host故障恢复不能称真机故障注入通过。设备contract显示actual raw/native/logical区分、map/free计量、recommit zero、两个callback live页whole cleanup及所有字段/padding符合。

总控原nnMain Query之后真实map65536，再到CreateThread index126/+0x352f1b8，exit133；malloc到VAusage index66/+0x144b8，exit133。上述是总控证据，本评审未读取/执行原main或实现CreateThread。按总控参数来源补充，BSS+0x77c51b8应由nninitStartup写入1MiB，并非DT_INIT array；当前原nnMain属于pre-init定位实验。本评审不把这些133/尚未完成的初始化写成CRT或游戏启动PASS。

## 执行边界

只覆盖自有普通C/POSIX、正向fixture装载、CLI info与受控组件失败；无ADB、原main/游戏、TP操作、旧异常输入/安全fuzz，也未读取Process后续WIP。quota是明示logical policy，不是RSS/物理RAM。页组件自身的独立审查另见同级guest-pages；本报告针对新profile消费，未直接沿用组件PASS作为集成PASS。本单元无剩余具体功能blocker。

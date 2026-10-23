# T07-GUEST-PAGES-REVIEW — independent ordinary component review

冻结 `4695e3c995993832ff8e274415f58b1cdb1e4684`。独立严格 host normal/recovery、身份/退款重试补测及正常八线程争用均通过；在公开的单 owner、raw API 静止及生命周期契约内，未发现需修的具体功能缺陷。此结论只覆盖独立 guest_pages 组件，不外推 Process/resolver 接入。

## 输入和独立执行

只读 git-show 保存四个新文件及同 SHA Context/address_region 四个依赖 c/h。依赖与 `d4ded908096feef6fff2876ab2ffdaa9e48303b5` 的 `git diff --exit-code` 实际为 **0**。所有快照测后 SHA 未变，逐文件/产物摘要与精确 argv/cwd/日志在 `manifest.json`。

AppleClang C11：`-O2 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic`。14 个编译/链接命令、4 次组件执行、compiler/nm 检查共 **20 个捕获命令全部真实退出 0**，无 timeout；见 `summary.json`。未运行作者整套脚本或 Android 交叉构建。

| 独立执行 | 实际码 | 输出与覆盖 |
|---|---:|---|
| frozen normal | 0 | 76 checks/errors0，真 RW/zero-fill、free/remap zero、exact/combined/partial/unknown free、quota、whole release、外部 Context ticket、destroy账清 |
| frozen recovery | 0 | 152 checks/errors0，6个正常受控失败阶段：部分grant失败后恢复NONE、commit失败rollback、quota无VM、grant+restore失败pending保留、free替换失败charged保留、commit+rollback失败pending保留 |
| identity_contract | 0 | 每个活跃ticket和exact span的真实释放顺序、外部实际RW资源共存、退款/cancel失败重试、retired whole release、overflow/output和quota无VM |
| concurrency_contract | 0 | 8线程共享owner、互异页：2accepted/6QUOTA；只读写成功拥有的RW页，两个exact free后账清 |

正常/recovery最终 pending/charged/tickets/raw regions 均0。生产 host binary 的 `nm -g` 无 raw VM hook、Context wrapper或 `guest_context_test_*` 测试符号。

## 身份与失败顺序的必要补测

自有 `identity_contract.c` 仅在独立测试对象中把已有 Context/region 公共函数编译重命名后接普通记录wrapper；生产源码未修改。wrapper记录活跃票据身份、exact address/bytes、reservation base，并在真实 exact VM free 或真实 whole unmap 返回成功后才把对应身份标为已释放；Context release/cancel检查该身份已无活资源。因此证明的是具体资源先释放、其ticket后退款，未凭总量为0冒充ownership。

- 外部 Context provider 先reserve/commit一个独立真实RW页并写入可核验内容；owner随后获得两个互异ticket。owner usage只计自身两个，Context计三个，释放页owner后外部内容/charge仍保留；外部真实munmap后才退款，最后Context可销毁。
- quota不足、combined free和uintptr范围溢出校验均没有VM调用/票据退款；无效reserve对有效owner的output归0，generation不变。检查使用普通C参数验证，不构造二进制输入。
- exact VM free成功但Context release受控返回EBUSY时保留原ticket/charge与身份；重试exact free只退ticket，不再调用VM。
- whole unmap成功而退款失败时保留retired reservation记录；新reserve返回BUSY、output0、无下层VM。按原base重试只退原ticket，不二次unmap，也不复用该base冒充新身份。
- commit失败后真实rollback成功，而cancel受控失败时保留pending身份；exact重试cancel成功且不再VM。上下文最终清账。

`concurrency_contract.c` 与正常生产对象链接，未带上述wrapper或failure hooks。各worker只在map成功后接触自己页；所有worker完成尝试后观察exact两个allocation/ticket和2页backing，释放后0。未对NONE/unknown页做故障读取。

## 源码结论与限定

- `guest_pages.c:75–104` 在全局owner锁下确认原始ledger为空，并建立唯一active owner；Context可有其他provider的票据，但必须存活到owner销毁。绕过本组件调用raw region/page API明确不受支持，未称通用跨client权限机制。
- `:44–55` 在VM前检查非零、4096和host-page对齐、uintptr溢出及reservation包含关系；`:141–144` 逐私有allocation身份拒绝重叠。`:193–215` 只匹配单一exact allocation，拒绝partial/combined/mixed/repeated free，无VM/退款副作用。
- `:148–179` 真VM之前reserve pending，成功VM之后才commit；commit失败做真实rollback，恢复可知NONE才取消票据。restore/rollback不确定使region quarantine，保留pending/charged与allocation，whole release前禁止继续map/free。
- `:201–215` exact backing释放后才drop对应ticket；`:224–243` whole reservation先真实unmap，再逐ticket退款。失败后retired元数据保留且reserve阻止base重用，重试不会重复unmap。既有raw whole munmap失败仍为fatal边界，未伪装可恢复；本次未触发该fatal路径。
- `:255–295` 验证私有identity非重复、区域/alloc disjointness、逐ticket求和，与全局raw ledger和Context capacity交叉核对。外部Context票据只作为Context余量，不拷入page owner用量。外部provider须按头文件要求串行，才能把它们一起当coherent快照。
- **消费方需保留的语义：** `backing_valid`是“accessible backing数量已知”，不是“生命周期所有ticket已结清”。退款失败但VM已释放时，它可为true而backing=0、pending/charged/ticket仍非零；此为诚实保账，不是零资源成功。未来Query/Process必须同时解释held tickets、pending/charged、backing及retained状态，不能只看一个valid标志。当前组件不提供SDK Query/resolver，无虚构SDK Result或errno映射。
- `guest_pages_result`的top status与attempted标志是使用入口；未尝试VM/region时嵌套结构零初始化不能充作SDK成功。普通create（非NULL Context/out）失败会清空out，reserve（非NULL owner/out）先归0；invalid参数时调用方仍须检查status。

锁顺序为owner→Context或owner→region，Context锁不跨真实VM回调；free/release/destroy要求实际内存用户已静止，destroy/API entry由调用方排除并发。仅组件API串行不等于自动停止外部内存使用。

## 总控真实RG证据，单独归因

本评审未执行设备命令，仅只读复制指定 `ffrs-integration/.local/device/rg477m-session1/guest-pages-v1-{plan,stage,execution}.json` 与 `.stdout/.stderr`。计划freeze与执行source_freeze均4695e3c；8项相关source SHA与独立快照一致，stdout/stderr SHA与execution一致。`run_capture.py`仅见于计划元数据，不在本次源码审查。源/副本路径、大小和SHA见manifest.coordinator_evidence。

总控RG normal生产组件：**exit0、39.740125ms、无timeout，76checks/errors0**，pending/charged/tickets/raw_regions最终全0。产物100784B，SHA256 `5cfde4d177e65211a93b4098ecbabea360f882d629af0db2cca2636091fbd172`。设备产物无failure hook，recovery_cases=0；不能称真实设备六个错误注入场景已通过。作者host和总控设备结果均未计入本评审20个独立命令。

## 未执行与剩余范围

没有ADB、NSO、游戏、原TP操作或旧异常/安全测试流程；没有读/运行Process接入WIP。配额是显式fixture逻辑限额，VA/backing不是RSS/原机物理RAM。新Process/resolver/SDK page Result接入必须另按自己的冻结验证，本组件PASS不覆盖它们。本单元无剩余功能blocker。

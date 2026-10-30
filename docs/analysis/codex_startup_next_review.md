# T07-STARTUP-NEXT-REVIEW

冻结 `ff964e9d479e288e2b695b5100836def780ca4df` 的五文件最小增量审查完成：**限定范围内未发现确认缺陷，独立宿主合同通过。** 本人 Android 执行 **NOT_RUN**；总控五个组件合同/info均0的设备证据单独归因。两组原startup仍是153/logf/+0xb3eac的严格 **133**，不算完整startup成功。

仅写本目录；仓库/Git、原3db报告和下一阶段代码未改。未操作ADB，未读/执行原SDK/main/游戏、`test_loader.py` 或旧停止流程，也未跑作者完整builder。

## 输入和增量

base为刚完成独立审查的 `3dbb0dcc81543b790c75510e9320981e429046f2`。25份源码/文档由精确 `git show ff964e9…:<path>` 保存，只有Process c/h、普通contract、builder、报告五项变化；另外20项与本人的3db快照逐字节相同。未重跑整个owner/allocator QA；复用本人3db严格编译的九个不变依赖object，复制后逐项记录bytes/SHA与原编译命令，新Process及新contract独立严格编译/链接。没有使用作者宿主二进制代替独立构建。

freeze.json摘要已核为 `6fc7d78db3edfdc02a8d2a843bc561c20e305f3b8da97e7ca47fb8fd966533a8`。native boundary仍是冻结5c字节，OS TLS为9c，pages/Context/region为3db；不继承root7be、新math或其他组合。

只复制本轮正常自产9216B fixture：

- strcasecmp.nso：`0f76d2220ab857358f8d63284941c350cbf0d264031f2ad16ca3c5b0730e62ef`
- va-enabled.nso：`639cd080e82577fd44ecad9433122957a5a3643b2a8b327a65ff3c24a9084074`
- 既有contract.nso：`a8467dc70098b693ab2d0266ac9ec99c705b76a644da20fbc0e5b263cd237493`

全部输入/产物摘要、命令/cwd、原始stdout/stderr/退出码及超时状态见 `final-manifest.json`、`logs/`。作者97条命令为作者证据，不计作本人执行。

## 核对结果

- `guest_process.c:337–356`：capability无条件先取完整live-primary/owner/ledger snapshot，然后才写out。有效但无显式resource policy返回canonical0；有有效非零resource total则canonical1，不以当前region数或剩余quota判断能力。snapshot失败不写out；guest无参wrapper失败fatal，不把无效/retained/quarantine答成false。新增page_snapshot只复制已有标量快照，没有新增owner、分配或第二账本。
- `guest_process.c:484–497`：`strcasecmp`两参、int32返回，Android真实调用Bionic非locale API；Mac确定性unsigned ASCII路径。NUL停止、A–Z折叠、0x80..ff保持，返回精确有符号差，未推广到Unicode或其他locale服务。
- resolver/signature表：strcasecmp为2GP参数/1GP返回，IsVirtualAddressMemoryEnabled为0参数/1GP返回；同既有native boundary流程消费，无dlsym/raw fallback。既有static facts文本摘要与freeze一致，只复核现存事实/方法记录，未重读原SDK或重启REA。其SDK依据为strcasecmp59bdb0及IsEnabled1448b0→145c6c的GetInfo16 success/nonzero total，仍明确为既有LLVM事实而非新REA Evidence。
- 只增加五文件范围的合同/正向fixture生成与报告；未改loader、owner、Context、region、CLI、边界实现或新math代码。生产ELF动态符号独立检查exit0：`strcasecmp@LIBC`为实际系统导入。Bionic实现/精确ASCII语义的公开来源已在3db审查核过，仍不宣称掌握RG系统libc的完整源commit。

## 本人执行和补测

`python3 build_review.py`、`python3 build_supplement.py`，Apple clang21/C11、`-O2 -g -Wall -Wextra -Werror -pedantic -fno-omit-frame-pointer`，链接真实guard/runtime。共 **16条**有界命令：**15×0、1×-6**；-6是事前要求的invalid-capability真实SIGABRT（shell134），包含`[va-capability-invalid]`，无timeout/pending/未预期失败。

- 新strcasecmp合同65536个u8对照exit0；capability两policy×五个真实VM状态（空owner、reserve、map、decommit、release后空owner）exit0。普通normal smoke0。两个native fixture的Mac分支正确UNSUPPORTED/native_calls0，不是本人的原生执行。
- 自有 `delta_contract.c` exit0：独立literal字节表的65536个两字节比较，长前缀/精确正负差/高位UTF8字节结果；不使用冻结合同ascii_reference或Darwin libc作为期望。两policy下真正注册的空owner分别canonical0/1；运行期间新pthread对同owner查询均INVALID且out不变，原primary随后仍能查询正确结果。启动前/返回后同样INVALID且不写out。
- 自有 `retained_capability_contract.c` exit0：在已有3db小型组合失败fixture中只增加新API断言。argv mmap ENOMEM＋arena munmap EBUSY返回保留Process时，新capability API严格INVALID、out=0x5a保持；重试真正unmap后owner释放并可重建。没有为本增量重新展开allocator故障矩阵。

## 总控RG证据，非本人执行

已离线核对 `ffrs-integration/.local/device/rg477m-session1/startup-next-v1-{plan,stage,execution}.json` 与五组件stdout/stderr，复制在 `coordinator-evidence/`。本人25份源码、依赖、三个fixture及设备实际使用的两ELF bytes/SHA与freeze/plan一致；24条staging命令均0，组件日志摘要与execution一致。作者自有C bridge反汇编run/输出摘要也匹配freeze，仅离线读取。

正常合同966232B SHA `21ac601bd9ba617194604350b0f80f777698bf94e0adf8ab7687c29b2e6d2f03`；CLI807440B SHA `06c22db0428ee2dc5c7a5614edba3949a9601f116e67eff9e534320f595c3797`。

总控实际RG：strcasecmp-native、va-enabled-native、normal、strcasecmp、va-enabled及info均exit0、无timeout。strcasecmp普通65536pairs，native6次调用；capability policy0/1×五个VM状态，native10次，canonical返回/x18断言通过。本人仅核其日志与哈希，Android执行保持NOT_RUN。

总控原startup-empty与nonempty均在153/logf/+0xb3eac严格exit133，源main前后摘要元数据一致。它们已越过61/67是本轮控制流证据；原constructor链的进一步解释属于父端定位，未由本角色读取/执行原main。旧3db直接pre-init malloc139、runner1、CRT NOT_RUN仍保留在原报告，未重跑或改写为成功。

## 未复测边界

复用3db已接受的owner/allocator/完整rollback审查，不重复其8线程/8000节点、全预算/VM故障矩阵或整套恢复；本增量仅做所列小型retained新API断言。本人未重做Android交叉编译、设备操作、原SDK解析或游戏启动。capability只描述配置好的native reserve/map/exact-free策略，不能外推完整Horizon VM、ThreadType/ELF TLS、未知ABI或完整CRT。无本轮工具拒绝、超时或挂起，限定增量审查无阻断。

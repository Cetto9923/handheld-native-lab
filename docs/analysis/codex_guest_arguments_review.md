# T07-GUEST-ARGUMENTS-REVIEW

冻结 `9dc8aa73c6e2d038c2b1c89efd898b65fbf0d1bb`：本轮普通源码审查和独立宿主合同通过，未发现需修的具体功能缺陷。结论限于 owned argv 组件及其 Process/CLI 消费路径。本人 Android 执行 **NOT_RUN**；总控随后提供的同冻结 RG477M 证据已独立离线核对，单独归因如下。

全部输出位于 `functional-review/guest-arguments/`。仓库、Git、其他报告和下阶段 WIP 均未修改；未执行 ADB、原游戏、SDK、TP 写入、旧异常 NSO/安全 QA，未读取 `test_loader.py`，未运行作者完整 builder。

## 输入与复现

六个增量文件由 `git show 9dc8aa73…:<path>` 保存，含 Process c/h、普通合同、builder、报告和 CLI；其余必要依赖逐项取既有冻结。共 25 份源码/文档输入，逐项 revision、bytes、完整 SHA256、Git 命令/退出码见 `final-manifest.json`。页 owner `4695e3c995993832ff8e274415f58b1cdb1e4684`、native boundary `5c20b064f2866577ca1fdd903fa196dd842eb3a8`、OS TLS `9c24ed35e683e0aa426ae2561dac670a8ead8ef1`；其他既有依赖取 `85a13c79378550fe850ffb5ff182cdbdfec177f8`。六个增量的摘要均与作者本轮 `freeze.json` 相同。

只复制现存正常自产 fixture，未生成异常二进制。尤其没有误用 28 的旧 OS fixture：

| 本轮 fixture | bytes | SHA256 |
|---|---:|---|
| arguments.nso | 9216 | d0aa9c9f744014aa93f01d9b9659e711e844adab1d72879b094e7253efc27409 |
| os-tls-callback.nso | 9216 | ec889611349d57808b800086ec387e8102fd658d29c08a20ee8611b38012bf15 |
| contract.nso | 9216 | a8467dc70098b693ab2d0266ac9ec99c705b76a644da20fbc0e5b263cd237493 |

其余既有 boundary/init-executor/readonly 正向 fixture 的来源和摘要同样已核对。本轮所有 snapshot/fixture 的最终摘要重核一致。作者 68 条构建命令属于作者自测，并非本人的执行数。

在本目录依次运行：

```sh
python3 build_review.py
python3 build_ownership.py
python3 build_cli_observer.py
python3 build_metadata_failure.py
```

各脚本使用自己的 `snapshot/`，不引用工作树可变源码。Apple clang 21.0.0，目标 `arm64-apple-darwin27.0.0`，C11、`-O2 -g -Wall -Wextra -Werror -pedantic`，实际 typed C++ guard/runtime 链接 `-lc++ -lpthread`。完整 argv、cwd、原始 stdout/stderr、真实退出码、耗时和 timeout 状态在 `logs/*/command.json` 与 `final-manifest.json`。当前最后一次 ownership 执行为 `independent-ownership-order-recovery-final`，保留前次同功能执行。

本轮已捕获 **53 条**构建、检查、执行命令：48 条 exit 0；guard 宏关闭编译按约拒绝 exit 1；无有效 Query scope 和不支持的非空 OS 析构 provider 各真实 SIGABRT，原始 returncode **-6**（shell 134）；两项缺少显式参数策略/参数值的 CLI usage 各 exit 2。53 条均符合事前期望，**无 timeout，无 pending**。这不表示把非零结果改成零。辅助只读日志结构探查另有一次 `KeyError: case`/exit 1：误把 stage 的命令记录当成 execution 的 case 记录，随后按正确结构读取成功；不涉及设备动作，原失败在 manifest 单列。

## 源码结论与独立证据

- `guest_process.c:178–226`：argc 被限制为有符号 int 范围，检查指针表/字符串总量与向上取整溢出；普通 C 借用输入的可读/不变期限明确为 create 返回前。先取得精确页票据，再真实 mmap，复制向量、字符串及 NULL 尾项后才 commit。空 argc 也真实拥有/计费一个页，旧 create 不借用宿主命令行。`arguments-storage` exit 0 覆盖空向量、三个字符串含空字符串、多页字符串、调用方修改输入及 guest 修改副本。
- `guest_process.c:256–297`：Query 要求 argv 的私有映射/票据/committed 状态，加上既有真实 module/primary 和 pages owner，核对 `3 + pages.ticket_count` 及所有 provider 精确字节和；溢出检查先于加法，pending/retained 不伪装成功。used/baseline 包含 argv 的实际页取整 extent，padding 保留，失败输出不写。不是原 SVC/RAM/RSS 测量。
- `guest_process.c:402–409`、resolver/signature 表及头文件新 API：两个真实 getter 无参数，返回实际缓存 int argc 与私有向量地址，签名按单寄存器返回接入既有 gateway，无 raw fallback。核对了既有小范围文本证据/plan-checkpoint：GetHostArgc SDK+0x143690 返回 w0；GetHostArgv +0x1436b0 返回 x0。只读取既有报告、摘要和 bounded 文本，未读/执行原 SDK。`static-abi/` 保存来源和摘要；这不是新的 REA 证明。
- `guest_process.c:631–680`：OS record 已清理/注销且 primary 已结束后才能 destroy；OS manager/pages/module/边界解绑的实际释放先于 argv unmap。argv 私有原始地址/extent 不取自 guest 可改写的 argv[i]。unmap 失败保留映射和票据，真实 unmap 成功后才 release/cancel 票据。失败保留 Process/exclusive owner，允许同句柄重试。
- `nso_call.c:112–159`：`--guest-arg` 的下一个值直接作为字面值，不受数字解析或 option 外观影响；选项组成完整 guest argv，调用方自行选择 argv0，默认空向量。显式 quota/stack 仍必需；metadata 在成功和 usage 分支释放。新的薄 observer 观察真实 create 的输入，再委托真实 create/加载/primary/info/destroy；三次 exit 0 分别证明空向量、单个空字符串，以及五个值 `ffrs`, `--info`, `space value`, 空字符串, `0x10` 原样传入。缺少值的 case 实际 exit 2。

新增 `ownership_contract.c` 只以薄观察包装器委托冻结 owner，VM 失败使用已有普通组件 hook；没有直接改私有结构或伪造账本。最终严格编译/执行 exit 0，实际证明：

1. 调用方两个字符串和输入向量在 create 返回后真实 free，随后 getter 仍读完整独立副本（本例真实两页）。commit 前已完成复制且 Context pending 等于 extent；commit 后转 charged。
2. 直接观察 argv/module 两个不同私有 ticket，再结合真实 owned primary、Context tickets=3 及 Query 精确字节；不是只比较总量便声称所有权成立。
3. guest 将 argv[0] 改指向外部常量存储、argv[1] 改为 NULL，修改字符串副本；argc 和向量基址仍稳定，清理从未 free 外部指针。
4. 真正的 OS cleanup/unregister 前后 argv 仍可读。此宿主 case 的 OS 析构表为空，不冒称执行了 guest 析构指令。
5. 第一次模块真实 unmap 操作受控返回 EBUSY，argv unmap 尝试数仍为 0，module/argv 两票据保留；下一次模块真正 unmap/退款完成后才进入 argv 释放。
6. 第一次 argv unmap 受控 EBUSY，已改写的向量和副本仍可读，精确 argv ticket/extent 仍 charged；再试真正 unmap 后才退款。模块 unmap **2 尝试/1 真释放**，argv 同为 **2/1**。真实退款完成后 Context pending/charged/tickets 全为 0，之后才 destroy。

新增 `metadata_failure_contract.c` 在 Process 自身 source pointer/length 元数据分配处受控 ENOMEM，其余 owner 使用真实 allocator。exit 0 证明失败 out=NULL、未 mmap、exclusive owner 回滚；随后非空和空参数 owner 均可真实创建/销毁。

冻结原有 normal、五项 loader cleanup recovery、argv precharge/quota-before-map/map-failure-cancel/unmap-retained retry、typed OS wrappers、retarget/init executor、readonly 显式 unsupported、legacy/profile CLI 也已独立运行，均为预期。生产宿主 CLI 的 `nm` 检查未见 argv/loader/boundary/Context fault-hook 符号。Mac 的 arguments-native 和 OS callback 路径明确 native_calls=0/UNSUPPORTED；其外层测试 exit 0 仅证明正确拒绝及无 raw fallback。

## 总控设备证据，非本人执行

开始时总控两次 identity read 均 exit 1/device not found、尚无新 push；这两次失败保留。之后用户确认 USB 重连，总控真实 RG477M 完成本轮，不以 28 的旧设备 PASS 继承。本人只离线核验：

`ffrs-integration/.local/device/rg477m-session1/guest-arguments-v1-usb-ready-{plan,stage,execution}.json` 与六组件 stdout/stderr 已复制到 `coordinator-evidence/`。`evidence-verification.json` 对照本人 25 份冻结输入、全部组件依赖、六 fixture 及四 Android ELF 本地字节/SHA；stage 44 条命令记录均为 0，六组件日志摘要与 execution.json 一致。

| 总控 RG477M case | 原始 exit | ms | 具体覆盖 |
|---|---:|---:|---|
| normal | 0 | 50.725 | 2 callbacks，Query 七字段/64KiB/恢复信号/页账；生产无故障 hook |
| arguments | 0 | 46.7765 | 3 storage cases，NULL tail/空页/多页 |
| arguments-native | 0 | 62.240041 | 3 次真实 getter fixture 调用，argc/vector/x18 断言，无 raw fallback |
| arguments-recovery | 0 | 60.010917 | 本轮专用 hook ELF，map rollback、argv unmap 2 尝试/1 真释放 |
| os-callback-arguments-lifetime | 0 | 48.16525 | A(0), B(9), A(5)，order=0x121；本轮保留断言校验析构内 argc2/同向量/字符串首字节 |
| boundary-retained | 0 | 43.349167 | 专用 hook ELF，3 unmap 尝试/1 真释放，guest_executed=false |

四个实际 Android 产物与本轮 freeze/plan 完全匹配：normal 853288B SHA `430dcb1feccb757997b29a0b06abcfad1de0e689979afeffc53c83fbc052dd86`；CLI 771864B SHA `ff26a2887a723301b076f0e390fcaad3aad82589daf9f82916cefa287b7cda96`；boundary cleanup 861992B SHA `bef567c61896d09ceeed823dede301fcad8326ff74bb6cd98bd23ba181f3320c`；argv recovery 867920B SHA `b62ab981f388603d25c465ec10acf09646b5fb1c850dd79b8aeb6df32b60f95e`。

总控 info exit 0；其五个原始入口只读取 execution 元数据，未读原始模块或执行它们：startup-empty 在 66/VAResourceUsage/+0x13744 严格 exit **133**；两个参数的 startup 在 63/strncasecmp_l/+0x13498 exit **133**；direct nnMain 126/CreateThread、malloc 66、CRT 515 均 **133**，全部无 timeout。这些不是游戏/完整 CRT 成功。

## 限制与交付

本人 Android 执行仍 NOT_RUN；本轮可选 Android 重复交叉编译按授权跳过，上轮真实 I/O/编译 timeout 已留在旧报告，未假称新编译通过。总控验证了新产物和正向 guest 析构/getter，不能把本人的宿主补测算设备补测，也不能把专用 hook ELF 的故障覆盖算生产错误注入。

输入必须是正常稳定可读的 C 向量/字符串，生命周期操作须调用方串行且原始 owner API quiescent；未测试并发改写输入、通用异常恢复或任意 SDK ABI。argv 是可变的 guest 内存；程序修改/移除 NULL 尾项后，其自身后续遍历义务不由 getter修复。模块卸载仍是现有 observer/discard 语义，未新增完整模块 finalizer、ELF TLS、SDK ThreadType、未知 CRT、跨模块线程或通用 aggregate/stack/variadic 调用支持。

没有本轮被拒绝或挂起的动作。无需功能修复；下一步设备/入口研究由总控另行定 scope。本文件及精简 manifest 可归档；源码快照、fixture、ELF 与 bounded 原始文本仍作为本地证据保存。

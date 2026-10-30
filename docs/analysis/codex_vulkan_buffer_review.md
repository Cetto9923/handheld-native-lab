# T07-VULKAN-BUFFER-REVIEW

结论：**在本次限定范围内未发现确认缺陷，可接受该组件提取。**
冻结：`fa76ca979d711c68beebe9e6ae699914f8a1cdf0`；总控集成 `34dd0c8`。
本目录 `snapshot/` 精确保存该提交变更的 8 个文件，逐文件 bytes/SHA256 见 `snapshot-manifest.json`。复核结束源工作树 HEAD 仍为上述 freeze，`git status --short` 为空。

审查人是旧 transfer 基线作者，**未参与新增 `buffer_resource.c/h` 或本次提取重构**。本次独立性仅指对两个新生产文件及其 driver 接入的独立审查/主机复测，不把旧基线称为独立审查对象。
未改源码、未操作 ADB/模拟器/真机、未读取 NSO/游戏、未做安全 fuzz、未增加 probe。所有输出仅在本目录。

## 核对结果

- `buffer_resource.c:47–94`：create 要求零/空 owner，busy owner 返回局部错误而不覆盖；每个成功句柄取得后才归入 owner；create/allocate/bind/map 任一失败均只回滚已取得的所有权，并返回独立 rollback 计数。失败 owner 可以重新 create。
- `buffer_resource.c:97–127`：整 allocation 从 offset 0 map，flush/invalidate 也为 offset 0 / `VK_WHOLE_SIZE`，覆盖非 atom 整倍数 allocation 的末端规则。coherent 仍发真实调用；batch 先检查资源有效和 device 相同，调用者提供 count 大小的 scratch。执行依赖和 GPU 完成继续由调用者负责。
- `buffer_resource.c:10–39`：`gpu_quiescent=false` 拒绝释放并保留句柄/映射；true 依次 unmap→destroy buffer→free memory，清除 ownership，第二次释放不再调用 Vulkan，统计归零。保留尺寸/type 元数据不会使已释放资源再次通过 mapped 检查。
- driver `execute/cleanup_buffer/cleanup`：只有 fence wait `VK_SUCCESS` 清 pending；wait `DEVICE_LOST`/timeout 均不假定完成、不做 invalidate/readback、不显式销毁在途资源，使用失败进程 teardown。正常路径及未提交的 rollback 明确传入 quiescence。未添加隐式无限等待。

## 独立执行证据

所有外部命令的 argv/cwd、规范化命令 SHA256、真实 exit 与 stdout/stderr hash 见 `command-index.json` 和 `commands/`。

| 本 Reviewer 执行 | 结果与证据 |
| --- | --- |
| 冻结快照的普通 host mock suite | **40 项 PASS**，ASan/UBSan；包括 31 个调用失败点、buffer/image 差异、timeout、wait-device-lost。`host-suite/summary.json`。未重复旧且未修改的 20 s watchdog。 |
| 作者新增 ownership contract 独立复跑 | coherent/noncoherent **2 组 PASS**；同目录 `resource-contract-*` 原始日志/exit。 |
| Reviewer 自编普通组件 contract | coherent/noncoherent **各 7 组 PASS**；额外逐项确认四个 create 错误的 observer/rollback 计数、失败后复用、busy owner/未静止保留、局部 batch 参数不发调用、释放幂等、无 host memory 回滚。`reviewer_contract.c` 与 `commands/reviewer-contract-*`；ASan/UBSan，最终 live objects / mapped memories 均 0。 |
| 原始 RG 记录只读核验 | actual exit 0，64 KiB + 256 B 零差异、31 VkResult 成功、destroy 12/unmap 3；匹配冻结 source bundle 和 ELF hash，并调用冻结版本的 result validator，exit 0。`review-evidence.json`。 |

主机测试是确定性 test double，不能证明真实 GPU cache/driver 行为；其中非一致性配置仅覆盖调用流程，没有独立的 cache 模型。

总控原始设备证据路径：
`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration/.local/device/rg477m-session1/vulkan-buffer-execution.json`。
其 `returncode=0`、elapsed 0.090150875 秒；设备执行者是总控 `/root`，本 Reviewer 的 device execution 为 **NOT_RUN**。
读取的 `.stdout` hash 与该记录一致，设备上实际执行的 198584 B ELF SHA256 为：
`2088ffd84187f8f868f6d892584a4d4164e09c6091bddc0f0531c948347ed3b9`。
本快照独立重算 driver + component + header bundle SHA256：
`a43b88df0741e61581f069d95e33342d6f1759deb58b60605665538cd540d26f`，与执行记录一致。

## 限制

无阻断项。未独立复跑设备、未运行 validation layer/真实 noncoherent memory、未做长期负载或游戏渲染验证；本结论只覆盖所列资源 ownership/rollback/同步范围/释放及 driver 接入。
不涉及并行 runtime 的 Bionic stack 容量修复。

语义复核使用官方 [VkMappedMemoryRange](https://docs.vulkan.org/refpages/latest/refpages/source/VkMappedMemoryRange.html) 与 [invalidate](https://docs.vulkan.org/refpages/latest/refpages/source/vkInvalidateMappedMemoryRanges.html)：整 allocation 映射允许末端到 allocation end，invalidate 需要此前的 GPU→host 依赖及等待。
当前 wait-device-lost 的保留策略比 Vulkan 销毁语义更保守；[Lost Device](https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html#devsandqueues-lost-device) 允许为生命周期判定将该返回值按成功处理，但它绝不证明读回内容有效。本次实现没有把 device loss 报为完成或成功数据，未据此提出缺陷。

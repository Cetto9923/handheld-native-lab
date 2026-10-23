# T07-LAS-TRANSFER-REVIEW

冻结 `81b1917adb261f80ce1780e2c16d8d31c536a693`：**限定范围内未发现确认的功能缺陷，普通宿主 ASan/UBSan 合同通过。** 结论覆盖五个新 LAS adapter 文件及现有 Vulkan transfer driver 的接入增量。本人 Android 执行 **NOT_RUN**；总控同冻结 RG477M 的实际成功结果已离线核对，单独归因。

所有自有输出仅在 `functional-review/las-transfer/`。未修改仓库、Git、其他报告；未操作 ADB、读取/执行原 SDK/main/游戏或旧停止的安全 QA。没有创建新的 GPU probe、shader 或渲染测试。

## 冻结与来源

`snapshot/` 由精确 `git show` 保存本提交十一项增量和必要冻结依赖/测试支持，共 18 个文件。逐项路径、revision、字节数/SHA256、Git 命令退出码见 `final-manifest.json`。12 个 production/interface/provenance 输入按作者同一规范化 JSON 方案独立重算 source bundle：

`21e4c62f30c481211ff09e8df8f89557215322f610552824e64dbd82079eed70`

已与作者 `final1/freeze.json`、构建证据、设备 plan/execution 一致。旧 `buffer_resource.c/h` 与本人既有 `fa76ca979d711c68beebe9e6ae699914f8a1cdf0` 审查快照字节完全相同，复用旧 ownership 审查，未重开其整个系统 QA。作者 58 条构建命令属于作者自测，不计入本人的命令数。

只读复用了 `graphics-reuse-integration/.local/upstream/` 的固定上游 `fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93`，没有重新获取大仓库。四个直接相关原文件 LICENSE、runtime/nvn/nvn.h、cmdbuf.c、vulkan/exec.c 的完整 bytes/SHA256/Git blob SHA1，以及相关选定片段 SHA 均与 UPSTREAM.json 一致；`upstream-range-verification.json` 保存结果。LICENSE 与保留的 MIT/Peiikai 归因字节完全一致。

`command_record.h` 的 CmdRec 和完整 opcode enum 是原文件的精确文本块；独立编译还确认 sizeof=80、COPY_BUFFER_TO_BUFFER=43、BARRIER=51、COUNT=60。cmdbuf.c 的 raw_rec/snapshot/Begin/End 与第406行 Copy 记录语义可追溯；上游 exec.c:3769–3773 的 copy 原为 renderer_flush 后 CPU memmove，本适配未复制该执行方式。上游原 HOST/SET_HOST/opaque offset、Cpu、OOM callback/chaining、默认静默跳过也未进入新 adapter。仅核上述相关 MIT 来源，不将它外推为完整上游 renderer/third_party 授权。固定 [上游 nvn.h](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/runtime/nvn/nvn.h) 可定位所提取格式。

## 源码结论

- `transfer_backend.c:35–63`：借用唯一现有 buffer owner，不复制或释放 owning struct。绑定检查同 device、真实非空 buffer/memory/mapping、HOST_VISIBLE、allocation/range/offset、声明的 transfer usage；拒绝重复 resource/buffer/memory 和逻辑地址重叠。实际创建 usage、外部同步和 owner 存活由调用方保证，注册期间不能从别的 API 销毁/改写它。
- `transfer_backend.c:84–130`：对最多64条固定80B记录先完整快照和预检，再发第一个 Vulkan 命令。END 必须在末尾，未知/JUMP/CALL 明确拒绝；copy flags、大小、范围、四字节对齐、同 buffer 重叠和总量溢出均检查。实际 Vk offset 是 binding.offset 加逻辑地址差。发出的是 `vkCmdCopyBuffer`，每次 copy 前有保守写→transfer 依赖，末尾有 transfer-write→host-read 依赖；后续 cache invalidate/完成等待仍由 owner 负责。[Khronos copy 的范围/usage/非重叠规则](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html) 与[同队列 barrier 依赖](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier.html) 支持此处的审查依据。
- `transfer_backend.c:132–159`：ENCODED 锁住绑定，接受真实 submit 结果并记住 fence；错 fence 不改变完成状态。TIMEOUT/NOT_READY 的可恢复不确定性只能用同一已接受 fence 的成功观察解除；提交失败或其他 wait 错误永久 quarantine，后来的 SUCCESS 不能伪造 idle。成功 wait 后还须 retire 才能 unbind。未提交取消依赖调用方已真实 reset/free command buffer 的明确证明，不自行构造提交/完成。
- `typed_transfer.c:24–79,96–163`：所有对象 key 只作 side-table 身份，未读写 opaque 字节。线程 token 不依赖可复用的 pthread ID；enter 不嵌套，错误 latch 后所有本地 handle 作废，clear_error 不恢复旧 token。附加 pool/span 有 offset/对齐/容量/别名检查；每次 append 为 END 预留空间，不支持的 callback/chaining/非零 typed barrier/copy flags 不假成功。Begin/Finalize/错误使旧 handle 失效；End 的全局单调 token 不作为 raw command-memory 地址使用。
- `typed_transfer.c:165–178`：八项 host target 使用现有冻结 nvn.h 的真实 C typedef 和 register-only signature 元数据。它们供正常 host ABI 调用；本次没有 runtime resolver、native gateway prebind 或 guest dispatch 消费，裸 target 不能直接交给 guest。不能据本轮推导实际游戏 opaque 布局/guest ABI 已兼容。
- driver `record_las_copy/execute/cleanup`：真实注册 source/destination 后通过 typed Begin/Copy/End→encode；`vkQueueSubmit`/`vkWaitForFences` 的实际 VkResult 交给 backend。只有成功完成+retire 才 invalidate/readback。submit/wait 不确定保留所有 Vulkan owner 并退出进程；未提交的 ENCODED rollback 先真实销毁 command pool，才 cancel→typed close→unbind→owner destroy。没有无界 DeviceWaitIdle。

## 本人独立执行

从本目录运行 `python3 build_review.py`、`python3 build_supplement.py`。所有外部构建/执行均有界，命令 argv/cwd、原始 stdout/stderr、退出码及摘要在 `logs/` 和 `final-manifest.json`。Apple clang21，C11、`-O1 -g -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer`。本轮 **19 条命令**均符合事前期望，真实退出码为 **11×0、5×2、2×3、1×4**，无 timeout/pending；没有将预期非零改写为0。

| 独立执行 | 真实结果与覆盖 |
|---|---|
| 冻结普通 adapter contract | exit0；ASan/UBSan，无诊断。typed→record→Vk观察、canary、不改 opaque、无CPU copy、整单拒绝、线程隔离/新线程token、容量/旧handle、错fence、timeout恢复、submit/device-loss quarantine。 |
| 新自有 `offset_lifecycle_contract.c` | 严格 ASan/UBSan 编译与执行均0。非零 binding offset32/64叠加记录偏移4/8，得到实际Vk offsets36/72；两个依赖copy都有barrier；同buffer相邻不重叠copy32→96合法；前copy有效而后CALL被拒绝时 Vk copy/barrier 均0。 |
| 同一自有合同的 typed/lifetime 补测 | pool offsets64/384及外侧canary保持；两个对象 token 在一次 typed 错误后都失效，clear_error 后仍不能用旧token，重录取得新token；错fence/timeout/retire前仍不能解绑。设备/pool/command opaque 字节及映射 source/destination 字节未改。这里只观察 Vk 调用，不模拟 GPU 数据复制。 |
| 聚焦现有 driver 增量 | coherent/noncoherent正常均0；end-command、submit、wait、retire后invalidate、wait-device-lost五项失败各2；wait-timeout为4；buffer/image mismatch各3。正常释放 live/mapped=0，不确定路径真实保持非零库存；保留路径 driver 关闭 LSan，未声称已释放。 |
| 既有 validator、ELF与输入复核 | exit0；独立重哈希110项实际 Android 编译/链接输入；NDK成员逐项CRC/bytes符合官方 partial manifest，不声称整包SHA1验证。正常 ELF 有两个adapter encoder及实际 vkCmdCopyBuffer import，无contract/mock符号；AArch64 PIE、16KiB LOAD、NX栈、RELRO/NOW、EH header检查通过。本人未重复交叉编译或执行Android。 |

driver 初版查找名 `invalidate_readback` 没有对应 stage，因此当时没有执行该项；随后按正常报告中真实的 stage31 `invalidate_readback_memory` 单独执行，原始 exit2、retired=true、清理库存0，日志完整保存。没有漏测后直接算通过。未重复未改的全31失败点/20秒watchdog/整套资源owner QA。

## 总控 RG477M 实证，非本人设备执行

只读复制并核对 `ffrs-integration/.local/device/rg477m-session1/las-transfer-v1-{plan,stage,execution,result,validator}.json` 及 `.stdout/.stderr`。`evidence-verification.json` 保存完整 hash 对照；execution 的 source commit/bundle、Android ELF bytes/SHA、stdout/stderr SHA 均一致。六条 staging 记录 exit0。本人还运行冻结 validator 核验捕获的 JSON 与独立记录的真实 exit，再额外断言所有 LAS 新字段，exit0。

实际产物288744B，SHA256：
`8ac793ff4bc978ecad1e1d837195e102ec9a7f28118b97fed38f38694430ef26`

执行者为总控 `/root`，真实 RG477M/Mali-G615 MC6：**exit0，100.1925ms，无timeout，stderr空**；64KiB copy与8×8 RGBA/256B clear均0差异，31个VkResult全成功，12 destroy/3 unmap。`typed_record_path=true`、`backend_copy_count=1`、`backend_copy_bytes=65536`、`gpu_completion_retired=true`，`opaque_layout_written=false`、`shader_or_present=false`。这是本轮新增 LAS 消费路径的设备证据，不继承旧 buffer probe 的设备 PASS。

## 支持边界

没有本轮被拒绝、超时或挂起的动作，无需修复的确认缺陷。宿主测试使用确定性 test double，非 GPU/cache/driver 实证；noncoherent 分支仅验证调用流程，未独立测真实非一致性缓存。非零offset/多copy补测未由本人在GPU执行。

调用方仍须保证所有借用 owner/handle/memory 活着、记录 span 可读稳定、外部同步正确、command buffer 正在同 device/queue family 上录制且在 render pass 外。此组件没有资源分配器、完整 NVN pool/builder、QueueSubmitCommands export、跨线程接管、native gateway 消费、未知 opaque布局、shader/texture renderer/present、完整游戏图形兼容。19条命令的局部成功及总控单次真实transfer均不能外推到这些边界。

本人 device execution 最终仍 **NOT_RUN**。报告/精简 manifest 可归档，原始源码快照、upstream、ELF和设备日志保留为本地证据。

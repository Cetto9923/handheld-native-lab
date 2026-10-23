T04-LAS-TRANSFER：LAS 固定记录协议接入现有 Vulkan transfer driver

已新增可消费的生产组件，并将现有 driver 的 64 KiB copy 改为 typed CommandBuffer 入口 → LAS CmdRec → 已登记 buffer_resource → 实际 vkCmdCopyBuffer。driver 继续持有真实 queue/fence、执行原 64 KiB 与 8×8 RGBA 256B 比较；没有新增 probe、三角形、surface、shader 或 present。本轮 Android execution 为 **NOT_RUN**，待总控使用冻结 ELF 在 RG477M 实测；宿主 test double PASS 不代表 GPU/本游戏图形成功。

固定上游为 [LASRecomp Beta4 fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93](https://github.com/peiikai/LASRecomp/tree/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93)。`third_party/lasrecomp/LICENSE` 保留原 MIT/Peiikai 归因，`UPSTREAM.json` 保留六文件原字节 SHA256、Git blob SHA1、提取范围及片段哈希、修改边界。CmdRec/原 opcode 枚举及 cmdbuf.c raw_rec/record/Begin/End/Copy 参数记录语义用于适配；没有复制整个运行时或模拟器。VMA、SDL3、shaderc、Maxwell、游戏 hack 与生成的 game 代码均未引入，主仓 MIT 没有被外推为 third_party 授权。上游原 buffer-copy case 使用 CPU memmove；本组件明确补上 Vulkan copy，没有复制 CPU fallback、ClearBuffer 缺 case 或 default 静默跳过。

生产范围为 `src/graphics/nvn_las/` 的五文件。`command_record.h` 保留 80B 布局与原 opcode 数值。后端接受连续最多64条、size固定80的 BARRIER/COPY_BUFFER_TO_BUFFER/END 列表，先快照并验证整单，再发任何 Vk command。非零 flags、未知 opcode/JUMP/CALL、未对齐、越界/溢出、同 buffer 重叠 copy 均明确拒绝。复制前采用 core Vulkan1.0 保守 memory barrier（包括前序命令写与 host write），复制后建立 host read dependency；连续 copies 也序列化。没有启用或宣称使用 descriptor indexing、timeline、dynamic rendering 等可选功能。

buffer 映射借用原 `FfrsVkBufferResource`，不复制 owning struct 或另设 allocator。绑定核对 device、HOST_VISIBLE 映射、allocation/range、实际调用方提供的 transfer usage；拒绝重复 buffer/memory 与地址区间冲突。原 owner 的 rollback、整块 coherent/noncoherent flush/invalidate、显式 gpu_quiescent 释放语义保持原样。调用方在注册期间必须保留 owner 与同步，不得绕过 registry 去销毁资源。组件不把 guest address 当 VkDeviceAddress；driver 的 source/destination 映射地址通过注册表解析为真实 VkBuffer/offset。

状态为 IDLE → ENCODED → PENDING → COMPLETE → retire/IDLE。ENCODED 后绑定全部保留；取消必须由 owner 确认 command buffer 已实际 reset/free。后端观察 driver 的真实 queue/fence VkResult，不自行伪造完成；wait 还必须匹配同一个 fence。TIMEOUT 可以对同一已接受提交的 fence 重试；submit failure 或 DEVICE_LOST/其他错误永久 quarantine，即使随后传入 SUCCESS 也不退款、清 owner 或假 idle。driver 对这些路径保留进程内所有 Vulkan owner，最终由 CLI 进程退出回收，没有无界 DeviceWaitIdle。正常状态仍显式释放12对象、unmap3次。

typed adapter 使用线程所属 side table，key 只作不透明身份，绝不读写 LAS 的 HOST offset0、builder 固定偏移或本游戏 opaque 字节。它显式登记已拥有的 command-memory pool span；本轮没有实现 pool/buffer builder、NVN资源分配或完整 QueueSubmitCommands。8个 host target 是 Initialize、Finalize、AddCommandMemory、BeginRecording、EndRecording、CopyBufferToBuffer、Barrier、IsRecording，使用现有 nvn.h 的真实 C 原型与 v1 register-only signature 元数据。它们只允许正常 host x18，enter/leave 不支持嵌套，线程身份使用不复用的单调 token。线程退出后的上下文不会在新线程重新激活，也没有跨线程自动接管/清理能力。

command handles 为单调 side-table token，区别于 LAS 的原 raw record address；Begin/Finalize/error 使旧 handle 失效。连续 command-memory span 不支持 chaining 或 guest OOM callback；容量不足明确 latch error，不回调假桩。void typed 入口的失败 latch 到 context 并使本地 handles 无效，encode 在显式清错误/重录之前拒绝它们。不满足业务覆盖的 API 不在导出清单中，不返回恒真成功。

动态绑定仍只是静态 host-target + signature 元数据，未修改 Process/resolver/global dispatch，也没有在 active scope 中 bind。后续 runtime 必须在 scope idle/phase0 预绑对应线程 native_boundary gateway；裸 host target 不可直接交 guest，Bootstrap phase2 的临时 bind 会 BUSY。当前 global dispatch 的跨线程 thunk 路由，以及本游戏是否直接访问/复制 opaque 对象，均尚未闭合。这些限制阻止自动对本游戏启用本组件，但不阻止已消费生产路径的开发与真实 GPU 合同验证。原 5c/7be 冻结树保持不动。

实际本地验证：

- 新普通合同在 ASan/UBSan 下 exit0：typed→record→Vk调用、源/目标偏移与字节数、opaque/command-memory canary、整单预检先于命令、无CPU copy、错误容量、失效handle、同线程重入拒绝、线程隔离、timeout恢复、错fence拒绝、submit/device-loss quarantine。
- 原 buffer owner coherent/noncoherent 两个合同 exit0。
- 现有 driver 的41个宿主场景符合各自真实预期退出码：正常/coherent与noncoherent、31个逐 VkResult 失败、timeout、device lost、两种故意 readback 差异、无device/queue/host memory，以及watchdog124。mock正常64KiB/256B均0差异、31个VkResult0、12 destroy/3 unmap；mock只是测试替身。故意 quarantine 场景以真实 live/mapped 计数检查保留，LSan 对这批driver测试关闭，未声称这些对象已正常释放。
- Android API28/AArch64 C11 交叉编译/链接与ELF检查通过：16 KiB LOAD、PIE、NX栈、RELRO/NOW、eh-frame-hdr，无RWX段。normal ELF不含host mock wrapper/contract入口。只读使用既有官方 partial NDK，110项实际输入逐项 hash，NDK成员另逐项 CRC 检查，未宣称整包已验证。

精确构建命令：`FFRS_LAS_BUILD_DIR="$PWD/.local/las-transfer/final1" bash scripts/graphics/build_las_transfer_contract.sh`。产物/证据位于本树 `.local/las-transfer/final1/`，58条命令的 argv/env哈希、真实exit、stdout/stderr、12项源码身份、全部编译输入均在 `build-evidence.json` 与 logs。旧通用 Vulkan 构建/host-suite 脚本没有改动；本次应使用新脚本，它链接 adapter 并按新12文件身份冻结，不能用旧三文件 source_bundle 代表新产物。

冻结 Android ELF：`vulkan_transfer_probe`，288744B，SHA256 `8ac793ff4bc978ecad1e1d837195e102ec9a7f28118b97fed38f38694430ef26`。源码 bundle SHA256 `21e4c62f30c481211ff09e8df8f89557215322f610552824e64dbd82079eed70`。构建时 HEAD 为 `245dfa222f22da6998819d4e0773bde9d8e6ae71` 加本任务工作内容，最终本地提交与 frozen source 的一致性另存 `.local/las-transfer/final1/freeze.json`；不会把旧base HEAD写作新源码提交。报告本身不参与ELF源码身份，避免报告记录产物hash的循环。

总控设备执行：将上述已校验 ELF 放入独立设备目录、chmod700，执行 `./vulkan_transfer_probe`，host端30秒有界，保存真实exit/stdout/stderr。成功时沿用原 validator，再核 `las_transfer.typed_record_path=true`、backend_copy_count=1、backend_copy_bytes=65536、gpu_completion_retired=true。此命令尚未由本 worker 执行，不能把现有 buffer-transfer 或 native ABI 的历史 RG PASS 当作新路径实证。得到0差异也仅证明本条 transfer 组件路径，不是 shader/present、CRT 或游戏画面验收。

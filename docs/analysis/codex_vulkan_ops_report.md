# T03B-VULKAN-OPERATIONS — transfer 操作探针交付

日期：2026-10-23。工作树：`/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-vulkan`。
分支：`task/T03B-vulkan-ops`。源码冻结提交：`f7ac445e9ad065a186795f05dbd42bca6a233207`。

**结果：Android ARM64 构建 PASS；总控在 RG477M 上执行的 buffer/image transfer readback PASS。**
本 Worker 只读核验总控原始设备记录，没有执行 ADB、模拟器或真机操作；本报告不构成另一名设备操作者独立复跑。
没有游戏素材、游戏代码或 shader 参与本次执行，不代表游戏渲染后端、NVN 映射或 FPS 验收完成。

## 实现和范围

新增独立 C11 CLI：`src/graphics/vulkan_probe/vulkan_transfer_probe.c`。
只请求 Vulkan 1.0 core，不启用任何 device extension 或 optional feature。

1. 创建真实 instance、physical/logical device、graphics queue。
2. 创建三个 host-visible buffer 和各自 memory，映射完整 allocation；写入固定 pattern 和读回毒值。
3. 对三个 allocation 都调用 `vkFlushMappedMemoryRanges`；选择 coherent memory 也照实调用，不假设缓存一致。
4. 创建 8×8、optimal tiling、RGBA8 UNORM image，实际分配/绑定 memory。
5. 一个 command buffer 记录 host→transfer barrier、65,536 B `vkCmdCopyBuffer`、image layout transition、magenta clear、transfer→transfer barrier、`vkCmdCopyImageToBuffer`、transfer→host barrier。
6. 实际 `vkQueueSubmit`，使用 fence 有界等待 5,000,000,000 ns；完成后 invalidate 两个读回 allocation，再逐字节比较 65,536 B pattern 和 256 B RGBA 数据。
7. 正常及提交前失败路径显式逆序清理；完整成功释放 12 个 owned objects、unmap 3 个 allocation。

所有 31 个返回 `VkResult` 的调用都逐项记录 stage、数值和名称。void 命令仅代表已记录，其执行证据另外由 submit、fence completion 和精确 readback 字段表达。
任何非 `VK_SUCCESS`、unsupported memory/queue 或数据差异均失败，没有替换实现或假通过分支。

| 能力 | query/create | 本探针实际 use |
| --- | --- | --- |
| Vulkan instance / device / graphics queue | 是 | 是 |
| Host-visible buffer / allocation / map | 是 | CPU 写入、GPU copy、CPU readback |
| RGBA8 optimal image | 查询支持并创建 | transition / clear / copy readback |
| Core feature query | 是，实测 robustBufferAccess 支持 | optional features 未启用 |
| Descriptor indexing | 未查询；其他能力探针的查询不归此项 | 否 |
| Timeline semaphore | 未启用 | 否 |
| Shader / pipeline / render pass / draw / surface / present | 未实现 | 否 |

## 构建与产物归属

Android CLI 精确产物：

```text
/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-vulkan/.local/vulkan-ops/20261023T094422Z/vulkan_transfer_probe
bytes: 174856
SHA256: 6e66aced9f0db6e67db754a2122be75febd51173d005515a831f9c60c36869b8
frozen C SHA256: 2e11d78de212d9b1a6d974ba9a68a88474cf42506865a7d318d852950c457403
```

该 ELF 在源码提交前构建，内嵌 `git_head_at_build=8b817b6ac5c310df435ce38a9e2ab260e7cead15`，当时新增源码为 untracked。
构建前已复制 C 源与构建/产物校验脚本到 `source_freeze/`；内嵌 source SHA256 与上述冻结提交内 C 源一致。
不把其 build HEAD 冒称为后来的源码提交，也不为了改变这个元数据重新生成/重跑已验证的 ELF。

构建命令：`bash scripts/graphics/build_vulkan_transfer_android.sh`。
工具链只在本工作树 `.local/minimal-ndk/` 建立 `toolchain` 和 `validated-members.json` 两个只读使用的 symlink，指向总控指定的 `ffrs-android/.local/minimal-ndk/` 对应项；未共享整个 `.local`、未修改 cache。
使用 Apple Clang 21.0.0、Google NDK r27d partial sysroot、Rust stable 随附 LLD，`DYLD_LIBRARY_PATH` 只作用于 linker 子进程。
NDK 来自官方 `https://dl.google.com/android/repository/android-ndk-r27d-darwin.zip` 的逐 member CRC/size 验证提取；**整包 SHA1 未校验，不称完整已验证 NDK**。

编译目标 `aarch64-linux-android28`。链接仅需 C runtime 和 Android Vulkan stub，无 C++ ABI/static runtime 依赖。
ELF 独立解析检查通过：ELF64 little endian、AArch64、ET_DYN/PIE、`/system/bin/linker64`、4 个 LOAD 的 `p_align=16384` 且文件/虚拟地址模 16 KiB 同余、无 W+X LOAD、GNU_STACK 非执行、GNU_RELRO、BIND_NOW、GNU_EH_FRAME。
动态依赖仅 `libc.so` 与 `libvulkan.so`；NDK `libvulkan.so` 是链接 stub，设备执行使用系统 Vulkan loader/driver。

构建证据目录：`.local/vulkan-ops/20261023T094422Z/`。
其 `logs/*.command / *.log / *.exitcode` 保留实际编译、链接、ELF 和 evidence 检查，退出码全部 0。
`build-evidence.json` SHA256：`2728f80868691418c559d8c2f8ce48df61ff932c39a0246ebcc01f765a4ba3c3`。
其中记录 96 项实际输入的 SHA256；95 项为 NDK member 并重新核对 CRC/size，另 1 项为冻结 C 源。

## RG477M 实测：总控执行，Worker 只读复核

原始执行记录：

```text
/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-integration/.local/device/rg477m-session1/vulkan-execution.json
SHA256: 26fca9089ce633a31c4b1592861dd41387ea69e7993049f4aa59920d4796d86b
started_utc: 2026-10-23T09:46:55.824575+00:00
exit: 0
timed_out: false
stderr: empty
```

总控设备上下文：实际 RG477M，Android 14/API34/ARM64、native bridge 0、4 KiB 页。
本次 probe 自报物理 GPU `Mali-G615 MC6`，vendor ID 5045，device ID 3097694256，API version 4206839（1.3.247），driver version 184553472；queue family 0、flags 7。

| 检查 | 实测值 |
| --- | --- |
| `commands_recorded / queue_submitted / fence_completed / readback_compared` | 全部 true |
| Buffer 比较 | 65,536 B，0 mismatch |
| Image 比较 | 8×8 RGBA8，256 B，期望 `[255,0,255,255]`，0 mismatch |
| VkResult | 31/31 `VK_SUCCESS`，0 error |
| Fence wait elapsed | 485,231 ns；仅为本次等待记录，不是 FPS/稳定性能指标 |
| 清理 | `explicit_reverse_release`，destroy 12 / unmap 3 |
| Optional features / descriptor indexing / timeline semaphore | 均未使用 |

原始 wrapper 的 stdout 为完整 JSON 字符串，Worker 已复制到本工作树 `.local/vulkan-device-review/execution-parent-record.json`，并提取 `probe-stdout.json`。
提取 JSON SHA256：`8cd792023c28c15480b547155c3f6b25ed11a35f1ab0437f2fb8aba919b28815`。
`provenance.json` 明确记录 executor 为 parent `/root`，`worker_device_execution=false`。
`scripts/graphics/validate_vulkan_result.py` 对真实 exit=0、source SHA、全部阶段、精确 bytes、零差异、feature 声明和清理计数复核 PASS。

供总控复跑的命令模板（本 Worker 未执行）：

```sh
adb -s "$SERIAL" push \
  /Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-vulkan/.local/vulkan-ops/20261023T094422Z/vulkan_transfer_probe \
  "$REMOTE_PROBE"
adb -s "$SERIAL" shell chmod 755 "$REMOTE_PROBE"
adb -s "$SERIAL" shell "$REMOTE_PROBE" > "$RUN_JSON" 2> "$RUN_STDERR"
RUN_EXIT=$?
printf '%s\n' "$RUN_EXIT" > "$RUN_EXIT_FILE"
python3 scripts/graphics/validate_vulkan_result.py "$RUN_JSON" \
  --exit-code "$RUN_EXIT" \
  --source-sha256 2e11d78de212d9b1a6d974ba9a68a88474cf42506865a7d318d852950c457403
```

总控首次实际 target 是隔离临时目录中的 `vulkan_probe-6e66aced9f0d`，精确完整 target 见原始 wrapper `plan.target`；这里不更改其已有设备执行安排。

## 主机自测与失败清理

`tests/graphics/vulkan_mock_driver.c` 是显式标注的 HOST TEST DOUBLE，通过 CPU 拷贝模拟 driver，仅测试控制流/JSON/资源生命周期。
它不参与 Android 构建，所有主机 summary 的 `android_execution` / `gpu_execution` 保持 `NOT_RUN`。

首次测试命令：

```sh
python3 tests/graphics/test_vulkan_transfer_probe.py \
  --toolchain .local/minimal-ndk/toolchain \
  --output .local/vulkan-host-tests-20261023T0950Z --watchdog
```

实际同一份 C 源经 ASan/UBSan 编译，40 项 PASS：正常数据、非一致性 memory 配置、31 个 VkResult 调用逐项注入 `VK_ERROR_OUT_OF_HOST_MEMORY`、fence timeout、buffer/image 各 1 B corruption、无设备/无 graphics queue/无 host-visible memory、20 秒进程 watchdog。
成功、提交前失败、比较失败、invalidate 失败均释放全部 owned objects/mappings；fence completion 不确定的两类注入严格保留 `process_teardown_inflight_uncertain` 失败，不伪造显式资源释放。
每项完整 stdout、stderr、真实 exit 均在上述目录。summary SHA256：`a41befc0489fc5f0130dd855279be08aed130bab36c09a546962557049604859`。

补充 flush/invalidate 调用计数断言后，当前测试脚本在 `.local/vulkan-host-tests-final/` 再跑普通 39 项 PASS，确认 coherent/noncoherent 都 flush 3 次、invalidate 1 次；未重复无变化的 20 秒 watchdog。
其 summary SHA256：`0177d0d399c7fda2f93dc3d14693318b87fd8d9eb9817649812045f0e0f5a805`。

`tests/graphics/test_vulkan_validators.py` 另外 17 项 PASS：接受完整实测 JSON/实际 ELF，拒绝缺阶段、失败 VkResult、未 submit、未 compare、错误 bytes/差异、源 SHA 错误、feature 假声明、缺清理、exit 不符，以及错误架构、4 KiB LOAD 对齐、W+X LOAD、可执行栈和缺 RELRO。
结果 `.local/vulkan-validator-tests.json`，SHA256：`7f38451e4194a053cff17cac31510e3fa935d0049fd1ad7e3679d5490d8ef321`。
`git diff --check` 通过。

## 失败边界、未覆盖项和恢复点

- Fence timeout 请求为 5 s；Khronos 允许实现按 timeout 精度调整。独立 20 s watchdog 避免驱动调用/清理无期限挂住。
- 已 submit 但 completion 不确定时，不调用无限 `vkDeviceWaitIdle`，也不销毁仍可能在用的 GPU handles；报告 FAIL 并由进程退出回收。watchdog 输出最小 JSON 并返回 124，不声称完整阶段或精确释放。
- 本次真实设备选择的 memory flags=7，属于 coherent memory；真实 noncoherent 硬件路径未实测，仅主机替身覆盖调用流程。
- 没有 Vulkan validation layers 运行证据、device loss 真机故障注入、压力/长稳测试、性能统计、WSI、shader、pipeline、draw、游戏场景或 NVN integration。本项 PASS 不满足这些后续门禁。
- 没有修改 `src/android`、runtime、现有 NVN 或 coordination；没有 push、下载软件、读取凭据或游戏素材。总控负责状态/集成。
- 本 Worker 普通 `git add` 首次因 worktree Git metadata 在工作区外而被沙箱阻止；随后针对同一已授权提交的 escalation 获准成功。没有自动审批拒绝或未知提交结果。

恢复点：源码提交 `f7ac445e9ad065a186795f05dbd42bca6a233207` 加本报告提交；已有有效 ELF 与真机证据无需因文档/格式变动重新跑 GPU。
下一步由总控串行 cherry-pick 本分支的两个自有提交，并按任务门禁安排独立 QA 或后续 NVN→Vulkan 真实调用实现。

参考 API 语义：[Khronos vkWaitForFences](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html)（timeout 与 finite wait），[Khronos Synchronization and Cache Control](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)（fence、pipeline barrier 和 host/device 依赖）。

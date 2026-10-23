# T04-VULKAN-BUFFER-RESOURCE

已把现有 transfer 驱动中的 VkBuffer 所有权提取为两文件 C11 组件：
`src/graphics/vulkan/buffer_resource.{c,h}`。三个实际 buffer 的创建、同步与释放均已改用它；原有 buffer/image copy、barrier、submit、fence 与逐字节比较继续保留。
基线为隔离分支 `task/T03B-vulkan-ops` 的 clean `2ca5d3587e43006bd0073ef4cfcad7f710f12eb8`。

## 接口与实际边界

| 导出函数 | 责任 |
|---|---|
| `ffrs_vk_buffer_create` | 独占 VkBuffer、专用 HOST_VISIBLE allocation（优先 coherent）、bind offset 0、整 allocation 持久 map；返回 typed status / 失败 operation / VkResult。失败时只回收已成功取得的 handles，并返回真实回滚计数。 |
| `ffrs_vk_buffer_flush` | 对整个 allocation 调真实 `vkFlushMappedMemoryRanges`，coherent/noncoherent 均走同一有效调用。 |
| `ffrs_vk_buffer_invalidate` | 使用调用者提供的 scratch 对同 device 的资源构造整 allocation range，批量真实 invalidate。驱动保留原来一次调用、两个 readback allocation 的契约。 |
| `ffrs_vk_buffer_destroy` | 调用者明确传 `gpu_quiescent=true` 才 unmap/destroy/free；false 保持资源，不等待 GPU。成功释放可重复调用，清所有权而保留诊断元数据。 |

输出结构必须先零初始化；拥有资源时不可复制或重复 create。typed local 错误不冒充 Vulkan 调用，`vk_result` 仅在 OK/VULKAN_ERROR 下有意义。creation observer 只观察真实 VkResult，不改变执行结果。
整 allocation、offset 0 / VK_WHOLE_SIZE 的同步范围覆盖 nonCoherentAtomSize 的末端规则；组件不声称 flush/invalidate 代替执行依赖。
调用者继续负责 host 访问排他、barrier、queue ownership、提交与 fence 完成。驱动仅在 wait=VK_SUCCESS 时确认 quiescence；timeout/device-lost/其他不确定等待均失败并保留在途 handles，由 CLI 退出回收。没有调用无界 DeviceWaitIdle。

组件使用现有 Vulkan core 入口、NULL allocation callbacks，不拥有 device/queue，不做 suballocation、device-local-only buffer、NVN ABI/opaque object、shader、pipeline 或 present。没有改 NVN dispatch/header，也不依赖启动 profile。

## 验证与冻结

**Android ARM64 编译/ELF检查 PASS；本版本设备执行 NOT_RUN，交由总控操作 RG477M。**
复用原 partial NDK、Apple Clang 21、既有 LLD；未下载/安装/clone。partial NDK 整包 SHA1 未验证，98 项实际输入按原 member CRC/size 与 SHA256 跟踪；未降低 ELF/validator 检查。

- `.local/vulkan-buffer/host-v1/`：原 40 项（含 watchdog）全部通过。
- `.local/vulkan-buffer/host-final/`：原 40 项保持，再加 wait-device-lost，合计 **41 项**；另 coherent/noncoherent **2 组组件所有权契约**通过。ASan/UBSan 开启；确认未静止拒绝释放、busy owner 不被覆盖、释放幂等、局部错误不发 Vulkan 调用；现有逐调用失败矩阵确认部分创建全部回滚。成功仍为 31 个 VkResult、flush 3 / invalidate 1、destroy 12 / unmap 3。
- `.local/vulkan-buffer/validator-final.json`：原 **17 项 validator 测试**全部通过；使用新实际 Android ELF 与明确标为 host test double 的完整结果，未把它作为设备结果。
- `git diff --check` 通过。每项 stdout/stderr/原始 exit 与实际命令保留在上述目录及 `.local/vulkan-buffer/run.json`。

精确交接产物：

```text
.local/vulkan-buffer/android-v1/vulkan_transfer_probe
198584 bytes
ELF SHA256: 2088ffd84187f8f868f6d892584a4d4164e09c6091bddc0f0531c948347ed3b9
embedded source bundle SHA256:
a43b88df0741e61581f069d95e33342d6f1759deb58b60605665538cd540d26f
```

`source_sha256` 从旧单 C hash 扩为 driver C + component C + public header 的确定性 bundle；`build-evidence.json` 明确记录 `canonical_path_sha256_json_v1` 和三个逐文件 hash。这样 component/header 变化会改变设备结果的 source 身份。现有 result validator 不变，校验此产物时使用上面的 bundle hash。

构建冻结发生在本地提交前，内嵌 build HEAD 为上述基线，并记录当时 dirty 状态；不把它冒称后续提交。`source_freeze/` 与本任务提交内三个生产文件逐 hash 一致。编译导出接口见 `android-v1/logs/buffer-symbols.log`；两个 object 均实际参与 Android 链接。ELF 为 AArch64 PIE、16 KiB LOAD 对齐、RELRO/BIND_NOW、非执行栈、无 W+X，依赖仅 libc.so/libvulkan.so。

重建（新目录，避免覆盖原始证据）：

```sh
FFRS_VK_BUILD_DIR="$PWD/.local/vulkan-buffer/android-next" \
  bash scripts/graphics/build_vulkan_transfer_android.sh
```

总控捕获实际 exit/stdout 后，可用：

```sh
python3 scripts/graphics/validate_vulkan_result.py "$RUN_JSON" \
  --exit-code "$RUN_EXIT" \
  --source-sha256 a43b88df0741e61581f069d95e33342d6f1759deb58b60605665538cd540d26f
```

本 Worker 未操作 ADB、运行游戏或做新的图形 demo。旧 transfer 的真机 PASS 不继承为本 ELF PASS；真实 noncoherent 硬件路径、NVN 桥、游戏画面和性能仍未验证。本次无拒绝或 uncertain 的构建/测试动作，local commit 权限状态以最终交接记录为准。

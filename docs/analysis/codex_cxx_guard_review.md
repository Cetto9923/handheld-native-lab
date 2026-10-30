# C++ guard 普通 ABI 功能独立审查

主机验证基线：`14ed86aa0587857b30be45181af742aa430824a4`；修复复核冻结：`a22d0dc13b92655c846d307489760064926c11ee`。源码快照与测试均独立放在 `functional-review/cxx-guard/`，未运行完整构建脚本、游戏、NSO、TP、VM 或 Android 可执行文件。

## P1 构建缺陷已修复；独立离线检查与总控正常 contract 证据闭环

位置：`scripts/runtime/build_cxx_guard.py:121-128`。手动 `ld.lld` 链接参数未传入 `--eh-frame-hdr`。

对总控指定的正常 C++ contract ELF 做只读检查：

- 原路径：`ffrs-runtime/.local/runtime/cxx-guard-v3/guard_contract.android`。
- SHA-256：`eca1fba733de8870a915a50de1db69142e017abd3d8f159122a0fb895d04f4d9`，与指定值一致。
- `llvm-readelf --program-headers --sections` **exit 0**：`.eh_frame` 存在（大小 `0x8f34`），但没有 `.eh_frame_hdr` 或 `PT_GNU_EH_FRAME`。
- `llvm-readelf --symbols` **exit 0**：`_Unwind_RaiseException`、`__gxx_personality_v0`、`__cxa_throw` 均有定义。不能仅以“链接了 libunwind”认定正常异常路径可用。
- 对应 link map 确认 guard 来自 `libc++abi.a(cxa_guard.cpp.o)`，并实际纳入 `libunwind.a`。

总控另行报告该 Android contract 因 uncaught `int` 退出 134；本审查没有执行 Android 或独立复现此退出。旧冻结缺失 EH header 与静态 unwind 无法发现调用帧的问题一致；修复与复核结果见下节。

## 已完成的普通主机验证

- `nso_host.c:7-14,26-29` 强引用真实 typed `__cxa_guard_acquire/release/abort`，未自行模拟 guard 状态。NDK AArch64 声明为 `uint64_t *`；本地 NDK r27d 的 cxxabi/config/libc++abi/libunwind 文件大小与 CRC 均匹配已有来源 manifest，已另存 SHA-256。本审查未下载或独立重验整个 NDK ZIP。
- Apple Clang 21.0.0：四个 C 依赖使用 `-std=c11 -O2 -Wall -Wextra -Werror -pedantic -pthread -DNSO_WITH_CXX_GUARDS=1`，全部 **exit 0**；C++17 contract 严格链接 **exit 0**。
- 原正常 `cxx_guard_contract.cpp` 运行 **exit 0 / errors 0**：8 线程共 80,000 次局部 static 访问，仅构造 1 次；abort 后仅 1 个竞争者构造；首次抛出 `int` 后正确捕获，重试总尝试 2 次。
- 独立 provider 检查构建/运行均 **exit 0**：三 guard 函数的 `dladdr` provider 均为 `/usr/lib/libc++abi.dylib`；`llvm-nm` 确认 resolver object 存在三项强 undefined 引用。

## 限制

Guard 只处理同一有效 8 字节 guard 的初始化同步，不替代对象/模块所有权或析构注册。跨模块必须保持 guard 存储与一致 runtime provider 的生命周期；构造中、等待中或仍有使用者时不得卸载/释放 guard 所在映射。调用方需 join/quiesce 所有使用线程，库没有自动卸载屏障。Host 结果不证明 Android unwind、SDK CRT、游戏或真机兼容。

命令、cwd、真实退出码、源码/产物摘要及完整日志见 `manifest.json`。未重复旧主机测试。

## 修复冻结复核

`a22d0dc13b92655c846d307489760064926c11ee` 的 diff 只涉及构建脚本、原 contract 的阶段输出与报告。typed guard 绑定没有变化；throw/catch 与重试断言完整保留，没有降低测试要求。

- 手动 LLD 参数已增加 `--eh-frame-hdr`，构建后也检查 `.eh_frame_hdr` 与 `GNU_EH_FRAME`。
- 指定新产物：`ffrs-runtime/.local/runtime/cxx-guard-v4/guard_contract.android`，**542,984 字节**，SHA-256 **`0b627417417bf09595e7b869f0501efd26087a2144c0aa15a53fc02c77bec14c`**，大小/摘要均匹配。
- 对新产物的 `llvm-readelf --program-headers --sections` **exit 0**：`.eh_frame_hdr` 存在，大小 `0x1654`；相应 `GNU_EH_FRAME` 段存在且覆盖该 section；`.eh_frame` 仍存在。
- 本审查至此未发现其他需修改的普通主机 guard ABI 功能问题。缺失 EH 索引的构建缺陷已由新冻结产物静态复核。

## 总控提供的 Android contract 证据（非本审查执行）

总控报告：同一 ARM64 Android AVD 上，`a22d0dc` 的完整未删减 `guard_contract.android` **exit 0**，8 线程、80,000 次访问、构造 1 次、abort 竞争后构造 1 次、异常重试尝试 2 次、errors 0。总控证据入口：`ffrs-integration/.local/emulator/pvz2-arm64-session3/guard-eh-execution.json`。本审查未运行 VM/Android，也未独立复跑这份 Android 证据，不能将它计为自己的执行。

范围结论：独立 **host typed guard ABI / 正常构造及竞争重试 PASS**；独立 **修复 ELF EH 元数据静态检查 PASS**；Android 正常 contract 由总控报告通过。所发现构建缺陷已有闭环，当前授权审查范围无待修代码缺陷或执行阻塞。以上均不证明真实 RG477M、完整 SDK CRT、游戏或 T02 通过。


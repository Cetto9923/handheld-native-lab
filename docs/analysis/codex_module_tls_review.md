# T07-MODULE-TLS-REVIEW 普通 C 功能审查

结论：在冻结提交 `3124895f06ea3f589c3334a61abb91ca4eb7232d` 的正常有效模板契约内，未发现需要修复的功能缺陷。仓库只读，所有源码快照、构建、日志均在 `functional-review/module-tls/`。

- `module_tls.c:12-32`：跨度与前置填充按幂次对齐计算；`35-48`：独立分配并整体清零，payload 按要求对齐，再同步复制初始化字节；`52-57`：view/存储归 image 所有，释放不执行回调。
- 严格编译：Apple Clang 21.0.0，macOS arm64，`-std=c11 -Wall -Wextra -Werror -pedantic -pthread`。原 `module_tls_contract.c` 与独立普通 C 补测均编译 **exit 0**；补测使用 `-O2`。
- 独立普通 C 补测运行 **exit 0 / errors 0**：8 类有效模板，覆盖空模板、零初始化、完整初始化、尾部清零、对齐 padding、非整齐 prefix、1–4096 字节对齐；源覆盖并释放后两个副本仍正确，修改/释放其中一个不影响另一个；8 线程镜像地址范围独立，完成 **32,968,000** 次 payload 比对，源字节保持不变。
- 原契约测试运行 **NOT_RUN**：`module_tls_contract.c:16-23` 在 AArch64 显式读取 TP 寄存器，与本审查“不操作 TP 寄存器”范围冲突；原测试未改动，仅编译。独立补测无 TP 寄存器读取或写入。

精确 argv、cwd、源码 SHA-256、产物摘要、stdout 和真实退出码见 `manifest.json` 与其引用日志；补测源码为 `ordinary_module_tls_contract.c`。本轮无执行拒绝，无未解决功能阻塞。

限制：前缀（目标形状为 16 字节）仅是明确未建模的零填充 header；该库未绑定 HLE/CRT。本审查不证明 SDK 生命周期、真实 TLS/ThreadPointer、NSO/module-main fixture、游戏/T02、Android 或 RG477M 兼容；未读取或运行 `test_module_tls.py`，未接触游戏输入、NSO 解析或旧中止 QA 流程。分配失败与无效参数路径不在本轮正常功能测试范围。

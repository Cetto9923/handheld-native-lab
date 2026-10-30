# T07-OWNED-PAGE-REVIEW 普通 POSIX 功能独立审查

冻结源码：`e112d65898433192912646fb0b6a56f881560246`（总控另报已集成为 `7d6c09f`）。结论：**授权的独立主机普通 C/POSIX 契约 PASS，未发现需修复的功能缺陷**。本轮仓库只读，所有源码快照、测试、构建与日志仅写 `functional-review/owned-pages/`。

## 源码审查要点

- `address_region.c:146-181`：map 先检查 host-page 对齐、真实 owned interior 与重叠，只有 RW 变更成功后发布区间/计数；失败恢复整段 PROT_NONE，恢复失败则 quarantine。
- `184-239`：free 先证明整个范围由连续相邻区间覆盖；中间拆分所需节点在破坏性替换前分配。成功后可保留前后片段、跨相邻节点裁剪，并减去准确长度。替换失败不猜数据存活，保守 quarantine。
- `120-143,150-180,188-238`：map/free/whole release 的系统调用与账本变更由同一 mutex 串行。whole release 释放原完整映射，覆盖 guards、padding、拆分/重映射的区间。
- `242-263`：任何 quarantine 令全局 `mapped_guest_bytes_valid=0` 且 mapped 数值为 0/unknown；整区释放该 reservation 后，可恢复剩余健康区间的精确计数。mapped 计数表示可访问 guest 跨度，不是 resident bytes 或物理预算。
- `address_region.h:16-18`：quarantine 内容/权限不确定，只允许 whole release；本轮任何测试均未读写 quarantined 或当前 PROT_NONE 的范围。

## 本代理实际执行

Apple Clang 21.0.0 / macOS arm64，host page **16,384 字节**。统一严格参数：`-std=c11 -O2 -g -Wall -Wextra -Werror -pedantic -pthread`。

| 独立检查 | 编译真实码 | 运行真实码 | 结果 |
|---|---:|---:|---|
| 原 production contract，无 test hook | 0 | 0 | 8×32 cycles、32 release races，最终 mapped=0、ledger valid、errors=0 |
| 原 recovery contract，仅测试构建启用 hook | 0 | 0 | 3 个受控失败：部分 protect 失败后恢复、恢复失败 quarantine、replacement 失败 quarantine；最终账本有效为空 |
| 补充 `independent_owned_pages.c` | 0 | 0 | 跨 3 个相邻区间的中间裁剪，两端数据保持、重映射清零；健康/隔离区间共存时计数未知，隔离整区释放后恢复准确计数 |

生产 host binary 的 `llvm-nm --defined-only` **exit 0**，确认没有 `nso_host_owned_pages_test_vm` 定义。原 contract 的 62-GiB 操作只保留虚拟地址，并只 materialize 一页（本机 16 KiB）；没有物理预算或常驻页数 PASS 声明。

未运行完整 `test_owned_pages.py`；已只读审查其 host/recovery/Android 编译与检查流程，实际 host 命令直接使用冻结快照并输出到本代理目录。完整 argv、cwd、真实退出码、源码/产物 SHA-256 与输出均见 `manifest.json`。

## 总控提供的设备证据，单独归因

总控报告真实 RG477M 的 production contract **exit 0**：host page 4096、8×32 cycles、32 release races、mapped=0、ledger valid、errors=0；62-GiB 虚拟 reservation 只 materialize 4096 字节。证据入口：`ffrs-integration/.local/device/rg477m-session1/owned-pages-execution.json`。这是总控执行结果，本代理没有使用 ADB、运行 Android 或复跑设备证据。设备构建没有故障 hook，`controlled_vm_failure_cases=0`，不能声称设备受控失败测试通过。

## 限制

锁保证 VM 操作和账本串行，不自动保护调用者的普通内存读写。map 返回后仍可能发生另一线程 whole release；实际使用者必须自行同步，并在释放/卸载前 join/quiesce。quarantine 不可凭旧计数或肉眼可读性继续使用，只能在用户静止后整区释放。主机受控 hook 测试不等于真实系统资源耗尽或设备失败恢复验证。

本轮未运行游戏、NSO、TP、畸形二进制/旧中止 QA、安全测试、VM 或 ADB；不证明 SDK Result/physical-memory 预算语义、游戏、T02 或完整移植通过。当前授权范围无待修问题及剩余阻塞。

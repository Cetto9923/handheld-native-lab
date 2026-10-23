# 当前可行性判断与最小复现

结论：原生加载 + 平台兼容层仍是待验证路线，尚无证据保证完整游戏能启动或达到目标帧率。已证明原始游戏的一段无平台依赖函数能在 Mac ARM64 原生执行，且可生成 Android ARM64 ELF；这排除了“所有代码都必须在地址0运行”的旧推测，但没有解决平台 ABI、TLS、图形或资源兼容问题。

## 最小复现

输入为本地 `.local/uncompressed/main.bin`，SHA256 `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`。不要重新解密、下载素材或读取密钥。下列脚本编译本任务自己的实验宿主并逐命令保存 stdout、stderr、超时状态和真实退出码：

```sh
python3 scripts/runtime/run_experiments.py --host-hle --out .local/runtime/NEW-CHECKPOINT
```

已集成 e0024a0 的结果见 `.local/runtime/integration-exit-registry/`：

| 实验 | 结果与可复现边界 |
|---|---|
| 模块信息 | exit0，651 导入、4,612 RELA、1,370,974 RELR。未解析导入不等于已实现。 |
| 原始 leaf +0x694bd0 | exit0，16,875组与数学参考零差异。 |
| 单独 nnMain | exit133，`QueryMemoryInfo`，caller +0x27a50；此前 ReferSymbol、地址预留、Mutex初始化和真实延迟 atexit 注册已执行。 |
| 构造器序列 | exit133，`__nnmusl_init_dso`，caller +0x5920a34。 |
| startup | exit133，`nn::os::AllocateTlsSlot`。 |
| malloc | exit133，`nn::os::QueryMemoryInfo`。 |

各入口分开调用是定位手段，不是完整平台启动。必须先补 CRT/模块上下文并运行构造器序列，再评估正常入口行为；不能把 nnMain 单独深入几次就算启动成功。

## 无设备仍可推进

1. 按本地 SDK 的真实调用点核验退出回调和 DSO 模块上下文，实现有类型的注册、LIFO、DSO筛选、重入和卸载清理；不提前执行回调，也不以空函数替代 `__nnmusl_init_dso`。
2. 查清 SDK 的 CRT 初始化参数、模块/TLS表与调用顺序，再实现有证据支撑的最小模块上下文。既有 guest-TLS 容器只证明线程隔离，尚未与游戏绑定。
3. 核验 `QueryMemoryInfo` 每个字段及计数语义，建立与真实宿主/guest内存管理一致的数据来源，不能猜一个RAM常量让游戏继续。
4. 为已实现的正常调用补独立功能复跑；被平台中止的旧 NSO 异常输入/安全测试不重启，不冒充完整独立 QA。
5. 完成 Android 端单函数比较入口、静态构建与可复现日志。设备不在时将实际执行保留为 NOT_RUN。

图形实现仍是另一条大路径。535个动态名字中目前只有两个查询入口，缺533项。按游戏真正访问的初始化/资源/队列/着色器调用链推进；接口数量不是工时或完成度，不平均批量填桩。无GPU设备时可做类型、格式、命令及着色器工具研究和主机构建，不能据此宣布渲染成功。

## 关键风险

- 同为AArch64不代表线程寄存器和结构体兼容。游戏有直接 TPIDR_EL0 读取；已反汇编的 +0x17040 紧接索引字节读取。SDK的 TPIDRRO_EL0 → ThreadType → TLS 槽与宿主 pthread 布局不同，不能改写宿主线程寄存器或把宿主指针冒充完整SDK线程结构。
- 游戏构造器请求真实 `__nnmusl_init_dso`。本地 SDK 实现规模约0x9d8字节、涉及超过8参数和状态路由；直接返回0没有初始化这些状态。
- NVN/Vulkan后端、Switch着色器和资源实际使用尚未通过；“无需JIT”不意味着图形或OS兼容成本消失。
- 用户明确当前不便连接掌机，继续主机开发。RG477M 的能力、画面、输入音频保存、30FPS与持续运行只能等实际设备验证，不能用主机/空场景替代。

现阶段无法给出可靠的完工日期或证明完整路线可行。可以继续上述具体主机工作；没有理由因单个主机检查点通过而结束，也没有理由把尚未验证的最难部分计为完成。

公开资料互证：[Switchbrew 的 Thread Local Region 原始研究](https://switchbrew.org/wiki/Thread_Local_Region) 记载 SDK 线程有独立 TLR/ThreadPointer 路径。该页也含较新固件字段，只作 TLS 不等同宿主布局的互证；不能用其字段替代本地 SDK 20.5 的反汇编，普通 Linux musl 源码也不自动具有 `__nnmusl_init_dso` ABI。

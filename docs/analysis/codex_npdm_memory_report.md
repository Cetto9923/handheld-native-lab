# NPDM 的内存配置实证

仅解析已有合法本地 `main.npdm`，没有执行、签名校验、改写输入或绑定加载器。脚本为 `scripts/runtime/inspect_npdm_memory.py`；原始 JSON 保留在 `.local/runtime/npdm-memory/result.json`。

输入 1620B，SHA256 `a39b66ed07bcb7b2730a9bbd37ec9afe968dd28d6e3d5f4d7794c3528c1b3f95`；脚本前后读取摘要相同。

| 原输入位置 | 值 | 限定解释 |
|---|---:|---|
| META+0x0c | 0x37 | AArch64、address-space值3；虚拟地址空间不是RAM预算 |
| META+0x0e / 0x0f | 44 / 0 | 初始线程优先级/CPU配置，不直接改宿主调度 |
| META+0x14 | 0x1400000（20971520B） | SystemResourceSize，内核内存管理资源保留，**不是总guest内存** |
| META+0x1c | 0x10000（65536B） | 原程序主线程栈配置，可作为明确的栈映射输入；并不证明宿主已经建立该栈 |
| ACID+0x20c | 3，MemoryRegion值0 | Application内存区域类别，不给出可用RAM字节数 |
| ACI KernelCapability | ProgramType值1 | Application类别，不决定总配额 |

格式依据 [Switchbrew NPDM 固定版本15259](https://switchbrew.org/w/index.php?title=NPDM&oldid=15259)。这是公开研究文档；本解析不声称Nintendo官方格式认证。上述字段能解决主栈大小的静态来源，不能解决 `QueryMemoryInfo` 的 total/used non-system memory。20MiB不能冒充总配额，地址空间和Android剩余RAM也不能。

复现命令（实际退出0）：

```sh
python3 scripts/runtime/inspect_npdm_memory.py .local/exefs/main.npdm \
  --output .local/runtime/npdm-memory/result.json
```

下一步是按明确配置创建、守护并计量独占原生栈，另行定义可执行的端口配额策略与module/page/heap/thread生命周期。七字段未闭合时保持接口缺失诊断，不返回伪造成功。当前不存在需要用户批准的动作或不确定执行结果。

# 原生移植阶段验收：尚未进入游戏

按最终可玩目标没有可靠总量，撤回10%～15%估计。组件合同、诊断页面、构建成功均不替代游戏启动。

| 阶段 | 已核验 | 当前缺口 |
|---|---|---|
| 原始入口 | f60实际RG：原nninitStartup空/非空均正常返回，真实OSslot0析构18c60完成（2轮/1调用/remaining0），最终0/0，无timeout | 原DT_INIT同版仍515/133；完整五参SDK Start/CRT/nnMain、资源/图形未过，不能把startup实验退出0等同游戏启动 |
| CRT/TLS/线程 | x18/SCS/GP21与新增同步HOSTcallback实际RG0且独立FINAL；TLSallocator一次登记、IDLE真实6次alloc/free/Query重入及retained生命周期过；fresh API P2同一复现修复闭合 | 完整ELF TLS/ThreadType/TP/alias/CRT515未实现；HOST allocator消费者已RG/独立审查通过；DSO/ELF消费者仍未绑定，SDK五参Start未完整执行；旧directnnMain126不是新组合完整启动测试 |
| 资源 | 既有合法本地素材/哈希、清单和流式工具 | 游戏实际挂载/解压/读取尚未通过 |
| Android APK | canonical v6诊断包451092B，实际RG安装/启动/JNI/诊断显示 | 不是完整游戏APK；无标题或可玩流程 |
| 图形 | 真实RG Mali/Vulkan buffer copy/image clear及生产VkBuffer组件已验证；LAS MIT命令记录/typed适配81b已由既有driver消费并实际RG0/100.1925ms，64KiB+256B回读零差异 | 新组件独立QA FINAL，组合构建及新ELF同RG0；真正NVN对象ABI、着色器、资源/队列/present、标题仍未完成 |
| 输入/音频/保存 | RG方向/双杆、数字L2R2事件；短音频实际写入及播放头推进 | 游戏映射/音频/存档NOT_RUN，听感未确认、模拟扳机未证 |
| 真机/平板/性能 | RG Android14/ARM64/nativebridge0/4KiB，物理1280×960/内容1280×918 | 游戏30FPS/45分钟NOT_RUN；荣耀GT Pro未连接，保画幅/必要留边 |

最新实际命令、产物哈希和原始码：[device report](codex_guest_process_device_report.md)。9dc独立宿主审查：[review](codex_guest_arguments_review.md)，Android归因总控；旧7be独立完整C编译超时作为历史保留：[review](codex_native_stack13_review.md)；434新完整C/ASM/SCS离线构建及同RG GP21回归已通过，见[新review](codex_native_host_callback_review.md)。历史证据保留在各报告及[索引](codex_handoff_evidence_index.md)。

下一关按真实启动链推进：原startup已返回，OS析构106/110已过→已核验五参SDK Start及CRT/DSO/ELFTLS→真实图形/title。旧malloc139已由真实构造前置条件解释，不重跑或手填vptr。LAS源GetInfo16/17硬编码0已排除，不能复用其假统计。源码/图形/新ABI静态分析并行，单一交接见[HANDOFF](../../coordination/HANDOFF.md)。

# 原生移植阶段验收：尚未进入游戏

按最终可玩目标没有可靠总量，撤回10%～15%估计。组件合同、诊断页面、构建成功均不替代游戏启动。

| 阶段 | 已核验 | 当前缺口 |
|---|---|---|
| 原始入口 | 未改main映射/重定位；9dc在RG越过OS槽59和参数60/61，空argv到66/+13744、非空到63/+13498，原始退出133，无超时 | VA management真实资源预算/占用，strncasecmp_l locale语义；完整启动未返回 |
| CRT/TLS/线程 | x18/SCS边界、逻辑OS TLS生命周期及真实guest回调、owned argv与页账合同通过；GP21运输单独RG0 | CRT515/133；完整ELF TLS/ThreadType/alias栈，单独nnMain仍126/133，不能用默认栈或pthread假放行 |
| 资源 | 既有合法本地素材/哈希、清单和流式工具 | 游戏实际挂载/解压/读取尚未通过 |
| Android APK | canonical v6诊断包451092B，实际RG安装/启动/JNI/诊断显示 | 不是完整游戏APK；无标题或可玩流程 |
| 图形 | 真实RG Mali/Vulkan buffer copy/image clear及生产VkBuffer组件已验证；LAS MIT命令记录/typed适配实际编码并由既有driver消费，最终冻结进行中 | LAS本轮Android未实测；真正NVN对象ABI、着色器、资源/队列/present、标题仍未完成 |
| 输入/音频/保存 | RG方向/双杆、数字L2R2事件；短音频实际写入及播放头推进 | 游戏映射/音频/存档NOT_RUN，听感未确认、模拟扳机未证 |
| 真机/平板/性能 | RG Android14/ARM64/nativebridge0/4KiB，物理1280×960/内容1280×918 | 游戏30FPS/45分钟NOT_RUN；荣耀GT Pro未连接，保画幅/必要留边 |

最新实际命令、产物哈希和原始码：[device report](codex_guest_process_device_report.md)。9dc独立宿主审查：[review](codex_guest_arguments_review.md)，Android归因总控；GP21独立完整C编译超时限制：[review](codex_native_stack13_review.md)。历史证据保留在各报告及[索引](codex_handoff_evidence_index.md)。

下一关按真实启动链推进：66真实有界management allocator→63可证libc语义→继续原始启动/CRT→图形/title。LAS源GetInfo16/17硬编码0已排除，不能复用其假统计。源码/图形/新ABI静态分析并行，单一交接见[HANDOFF](../../coordination/HANDOFF.md)。

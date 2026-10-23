# 原生移植阶段验收（2026-10-23 真机检查点）

目标是 RG477M 上完整原生游戏启动、标题、可操作场景、游戏输入、音频、存读档及持续帧率。当前仍未进入可玩阶段；诊断页上的 JSON 是预期结果文本，不是游戏画面。

| 阶段 | 当前真实证据 | 尚缺验收 |
|---|---|---|
| 加载器 / 真正入口 | main 映射与 651 导入、4612 RELA、1370974 RELR；原始 44B leaf 在 Mac、Android ARM64 AVD、RG477M 各 16875 比较零差异。新RG profile f24实际info0、nnMain越过Query与真实65536B页映射，到CreateThread126/+0x352f1b8退出133；malloc到VAusage66/+0x144b8退出133，均无timeout。 | 完整CRT/TLS/原生线程。当前同构建DT_INIT仍在__nnmusl_init_dso515/+0x5920a34退出133；nninitStartup仍AllocateTlsSlot59。零线程栈参数已追到BSS77c51b8：原nninitStartup在TLS槽分配之后写默认1MiB，可由-MainThreadStackSize=参数覆盖；应运行真实写者，不能用默认值直接放行CreateThread。单独nnMain是pre-init定位实验。 |
| 资源 | 合法既有素材、清单与流式工具保留；APK JNI 已实际加载125577976B主模块。 | 运行时文件系统挂载、资源解压和实际游戏资源使用。 |
| Android APK | canonical v6、451092B，SHA256 4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a；clean fb7d870 源码、35构建步骤0，签名/DEX/JNI/16KiB LOAD/EH索引检查通过。RG 更新安装0、冷启动195ms，旧事件与leaf保留。 | 仍是诊断宿主，APK不执行完整游戏。工具链是逐成员CRC验证的部分官方NDK sysroot，完整NDK归档校验未完成。 |
| GPU / 标题 | RG Mali-G615 MC6/Vulkan1.3.247；实际64KiB buffer copy及8×8 RGBA image clear/readback零差异，31 VkResult均0。新可复用production VkBuffer组件fa76→34dd由同driver实际消费、RG及独立host审查通过。 | 着色器、surface/present、descriptor indexing等真实所需操作、NVN转换和标题。NVN仅2/535查询名字，533缺失；不平均铺假桩。 |
| 内存 / C++ ABI | RG owned-pages正常契约0，8线程×32周期、32释放竞争、4KiB清零重映射、账本归零；主机另独立审查rollback/quarantine通过。RG C++ guard 8线程80000调用、初始化1、异常重试2、errors0。 | 新唯一page owner4695→d8已同RG普通契约0/76checks/errors0；SDK Allocate/FreeMemoryPages已f24接入，Query统一module+logical stack+真实pagecharges并保padding，失败保账且无猜SDKResult。栈先withhold容量64752B再有界补4096admin，native68848B包含精确64KiB logical窗，RG契约及独立d4审查通过。62GiB仍仅虚拟预留，1GiB是明确port-policy，heap只初始化empty。完整SDK ThreadType/TLS/CRT、x18域切换集成及guest unwind未完成；新profile独立复核中。 |
| 可玩场景 | NOT_RUN。 | 完整启动、标题、场景、操作流程与正常退出。 |
| 输入 / 音频 / 保存 | 用户实际操作：方向/B/左杆；补测右杆Z/RZ均[-1,1]、L2/R2按键事件。公开InputDevice识别retrogame_joypad。显式0.25秒音频探针实际48000Hz、12000写入及播放推进、扬声器路由、stop/release正常。 | 游戏映射未实现；扳机模拟轴本次仍0，只有数字键证据。人回复提示音“刚才没注意”，听感未确认。游戏音频、游戏存读档、休眠恢复NOT_RUN。临时文件不是游戏存档。 |
| 真机 / 性能 | RG已连接并执行上述诊断；Android14/API34/ARM64/nativebridge0/4KiB。实测窗口1280×960、内容1280×918、280dpi。 | 当前120Hz是面板刷新率；游戏30FPS/45分钟稳定性NOT_RUN。荣耀平板未接入；按实际内容区等比例留边要求保留。 |

可玩目标粗估仍约10%～15%，仅定性工程判断，不按代码量、任务卡或工时计算。平台诊断进展不能显著降低完整启动、图形转换及可玩性的最高风险。

主机集成回归：c5bbddb的23项检查全部符合真实预期退出码（NVN覆盖3、normal exit23、_Exit24，其余0），见 `.local/host-verification/checkpoint-c5bbddb/summary.json`。普通独立组件审查已执行；旧异常NSO安全流程被平台中止，未重试，不冒充完整独立QA。

最新启动证据见 [profile device report](codex_guest_process_device_report.md)与[page owner report](codex_guest_pages_report.md)；历史设备证据见 [codex_device_report.md](codex_device_report.md)，可复制恢复信息见 [HANDOFF](../../coordination/HANDOFF.md)。

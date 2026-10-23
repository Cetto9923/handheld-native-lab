# RG477M 当前真机诊断检查点

真机已由用户连接并完成ADB授权；用户也亲自按键/拨杆。以下不是游戏标题或可玩验收。

| 范围 | 实际结果 | 限制 |
|---|---|---|
| 身份 | Anbernic RG477M，Android14/API34，MT6897，arm64-v8a，native bridge0，4KiB页 | 荣耀平板尚未接入 |
| APK | canonical v4 `org.ffrs.diagnostic`，426516B，SHA256 `bb8bca99af247c52dd7f2890b81f3c2aeabc88574a7dca98c6625c23ee3bb646`；无申请权限，新安装0，冷启动219ms | Source aa8a623，诊断宿主，未包含完整游戏 |
| 显示 | Activity窗口1280x960，内容1280x918，280dpi；60/90/120Hz模式，当前120.000008Hz | 面板刷新率不是游戏帧率；未来游戏按源画幅等比例fit/留边，不强制拉伸 |
| GPU查询 | Mali-G615 MC6，Vulkan1.3.247，ARM r44p1；descriptor indexing、dynamic rendering等查询可用 | 仅查询不证明功能实际操作；独立GPU操作探针正在实现 |
| 原始叶函数 | AndroidAArch64实际执行16875组，0差异，无构造器 | 仅main+0x694bd0原始44B，整游戏NOT_RUN |
| C++ ABI | 真机8线程80000次，构造1次、abort竞争winner1、异常重试2，errors0，exit0 | 独立host compiler-runtime契约，不代表guest NSO异常展开或SDK线程兼容 |
| 原生CLI | 生产链CLI info0；nnMain和malloc在QueryMemoryInfo +0x27a50/+0x1398c exit133；CRT __nnmusl_init_dso +0x5920a34/133；startup AllocateTlsSlot +0x133a0/133 | 严格阻塞诊断，不是完整启动 |
| APK内JNI加载 | 授权main.nso 125577976B，651导入、4612 RELA、1370974 RELR，LOADED_DIAGNOSTIC_ONLY | NULL resolver；不调用imports/constructors/game entry |
| 手动物理输入 | 基线后45个key、47个motion、pause/destroy；方向、B、BACK、左X/Y[-1,1]实测 | 右杆未观察非零；B/返回使Activity退出。raw-node能力探针误报空列表，正改公开InputDevice API；不是硬件无手柄 |
| 音频/存档 | app-private临时文件往返/生命周期有证据；AudioTrack未测 | 不等于游戏音频/存读档；休眠未测 |

原始报告位于协调工作树 `.local/device/rg477m-session1/`：`identity-before-install.json`、`install-plan.json`、`install-launch.json`、`diagnostic-v4.json`、`leaf-stage-production.json`、`leaf-result.json`、`leaf-result.png`、`manual-events-summary.json`、`runtime-plan.json`、`runtime-execution.json`、`diagnostic-module-loaded.json`。所有复制使用新目录或独占创建；没有卸载、清数据、root、SELinux改动、刷机或其他应用数据读取。

CLI source8b817b6：558000B，SHA256 `5cf82020353e061f3c2cef4fd2dd4cb417dbb3eb883460ecd688bb1b1ff89853`；独立guard sourcea22d0dc：542984B，SHA256 `0b627417417bf09595e7b869f0501efd26087a2144c0aa15a53fc02c77bec14c`。主模块SHA256 `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`。标准生产工具链仍是CRC验证的部分官方sysroot+AppleClang/RustLLD，完整NDK包未校验。

当前T03B保持in_progress：GPU实际操作、完整手柄键轴、音频和休眠等尚未完成；正式游戏启动/标题/可玩/T02兼容范围/性能仍未通过。

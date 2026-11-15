# 24候选的绘制定位与跨设备回归

## 输入及构建

开发分支task/T-codex-recovery-20261114，输入HEAD为4de88c8。本轮源码改动包括有界绘制清单、索引绘制诊断顶点计数、实际采样器能力接线，以及可选内部渲染比例启动参数。游戏逻辑时间保持原路径。

首个GPU诊断APK为1.0.24-preview，SHA256为acc3a1543f5be555d2cd8eeb0694c8589758711dfbb6c1c79a60341570461197。101项宿主检查、Android构建退出0，41份源码前后摘要一致。APK与构建计划保存在.local/codex-recovery-20261114/gpu-draw24/build及apk-history。

随后清理了用户已确认正常的按钮大小问题。曾构建但未安装的多指编辑候选d19e94保留为历史，相关TouchPad修改已精确撤回，按钮编辑原有实现保留。当前干净render-probe候选SHA256为4c6183b2131451fb40638d78d6b002fad705409ac470ef4bf5c165fbe0828005，3683399字节，build-render-probe中101项宿主检查及Android构建退出0。

首次render入口构建被既有启动接线契约拦住，原因是构造参数有意增加后，测试仍匹配原调用签名。已更新精确调用断言，保留seed先于两类native启动的顺序检查，并增加直接抽取生产Java方法执行的161个参数边界用例。原失败及后续成功日志全部保留。

## 当前战斗热点

RG477M01445122的首24包PID10789，用户亲自操作并反馈各种技能均卡。root同步记录90帧现成pass-live及首帧绘制清单。此诊断沿原精确计时路径，影响resident快路径，性能结果限定采样路径。

完整当次stderr在device-rg/user-skills/guest.log，关联CODEX_FRAME的10789文件。旧engine.log达到64MiB且已有轮转备份，文件写入已停止。旧文件未混入本轮归因，后续重启前已把满文件改名留存，让新进程重新记录。

采样帧19053至19142，共10620项GPASS。首帧517条GPASS_DRAW完整，按frame、slot、ring和pass关联，包含canonical shader、实际绘制参数、depth、blend、写通道、viewport与scissor。绘制清单只有元数据，未声称取得逐draw GPU耗时。

主要1024×576、7附件、format122通道90帧均为144draw，三个30帧块的GPU耗时中位为15.065、15.1515、14.819ms。首帧5.349ms原值保留，不能凭该低值归因频率或shader。末帧该单通道14.147ms，该slot全部通道累计32.430ms，二者分别记录。

首帧主通道23个shader组合中，几何量最大的组合为840fbd52427393a0→ac23ed01af69f24c，25draw、加权149217个顶点。随后7a4358322ae60f0e→968ba01f6a71a0b3为15draw、75261。完整分布及原始行在draw-review/first-sample-groups.csv。

复核发现原GPASS indexed verts累计漏乘实际instances。原主通道368304对应元数据加权379134，另一个8draw实例通道原45对应加权4785。当前候选仅修正该诊断计数，实际draw参数保持。历史报告原值与重算值并列。

draw-review/review.md最终SHA256为d92821b0ec71e215e53486b1568effcc8ed6c3cedb8c693a51fa5ef2c6e429f8。历史SPIR-V只作为分析线索，三份模块LDC折叠均0，未证明与24最终模块逐字相同，未据此改写特定shader。

## 跨设备实际接线

本项目后端来源与改动记录见src/graphics/nvn_vulkan/README.md。此前已接入部分DroidPlay精度和子组修正。本轮逐项复核报告在compat-review/review.md，SHA256为51a3347553cc883df8889015d48465db8323c28965802fb6ac5f413bc8de14ec。

发现正常启动未启用samplerMirrorClampToEdge且adapter配置一直为0。已补物理能力查询、core1.2或KHR扩展启用、成功创建后的能力记录、getter及adapter配置传递。只依据实际能力启用，不按GPU名称推断。规则核对Vulkan VUID-VkSamplerCreateInfo-addressModeU-01079。[Vulkan采样器规则](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html)

扩展现成layer_output_gpu_contract，以生产backend创建逻辑设备并真实创建mirror-clamp VkSampler。RG的Mali-G615 MC6及荣耀平板Adreno735均通过，同时分层渲染的4608像素、非零base-layer等原契约通过。组件未调用完整guest或descriptor memo，不代替完整游戏验收。旧Vulkan1.1扩展路径尚无对应设备实测，也没有该缺口与用户手机闪退的因果证据。

## 荣耀平板

设备A42BBB5109000333，旧包1.0.9，安装前无游戏PID。旧APK、4份存档及touch-pad.cfg均已备份，覆盖安装干净24候选后存档tar逐字相同。

首次从荣耀桌面freeform窗口启动出现ANR，PID13624，系统原因为Application does not have a focused window。原始lastanr、logcat和进程退出信息已采集。进程随后以ANR结束，旧guest_boot.log时间为11月13日，不作为本次启动日志。正在使用单次fullscreen窗口重试，并继续定位首次窗口建立的阻塞原因。该设备24完整游戏回归尚未通过。

荣耀历史测试包括1.0.6分场景长帧与早期Dark/Limit/Boss有限流程，各自失败与覆盖范围见原报告。本轮组件通过与旧版本游戏通过分别记载。

## 内部渲染比例试验

GameWindowNative和guest process已有真实argv及GetHostArgc/GetHostArgv链路。当前新增可选render_percentage整数Intent，仅接受UE文档范围10至100，生成r.ScreenPercentage启动命令。未提供参数时继续传空数组。主机执行161个边界用例通过，游戏Shipping版本是否接受该命令仍须以真实附件尺寸验证。[UE分辨率比例](https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-reference-for-unreal-engine)

用户明确确认保存并同意覆盖RG，新候选已安装，存档前后逐字校验通过。正在先记录默认参数的同机位数据，再进行比例对照。参数入口或APK安装成功不代表分辨率改变，也不代表卡顿已解决。

当前卡顿、荣耀首次ANR和全场景性能目标继续开放。未发布新的GitHub Release。

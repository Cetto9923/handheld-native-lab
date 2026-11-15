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

首次从荣耀桌面freeform窗口启动出现ANR，PID13624，系统原因为Application does not have a focused window。原始lastanr、logcat和进程退出信息已采集。进程随后以ANR结束，旧guest_boot.log时间为11月13日，不作为本次启动日志。单次fullscreen参数重试的PID15801进入共鸣标题，实际任务仍为freeform，因此不能归因为切换fullscreen已经修复。随后用户在平板前台运行空之轨迹，共鸣处于后台。用户明确本轮先不测平板，后续操作已停止。该设备24完整游戏回归尚未通过。

荣耀历史测试包括1.0.6分场景长帧与早期Dark/Limit/Boss有限流程，各自失败与覆盖范围见原报告。本轮组件通过与旧版本游戏通过分别记载。

## 内部渲染比例试验

GameWindowNative和guest process已有真实argv及GetHostArgc/GetHostArgv链路。当前新增可选render_percentage整数Intent，仅接受UE文档范围10至100，生成r.ScreenPercentage启动命令。未提供参数时继续传空数组。主机执行161个边界用例通过，游戏Shipping版本是否接受该命令仍须以真实附件尺寸验证。[UE分辨率比例](https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-reference-for-unreal-engine)

用户明确确认保存并同意覆盖RG，新候选已安装，存档前后逐字校验通过。完成同机位普通30秒A/B及独立附件采样，日志、脚本与CSV在render-percent目录。

| 条件 | PID | 普通FPS | P95/P99毫秒 | 超50毫秒 | GPU中位毫秒 | 主通道尺寸 |
| --- | --- | --- | --- | --- | --- | --- |
| 默认无参数，4c6183包 | 3205 | 30.0001 | 36.670/44.211 | 4/903 | 24.960 | 768×436 |
| 未加引号比例60，同包 | 14159 | 29.9997 | 35.354/44.135 | 2/903 | 25.455 | 768×436 |
| 已加引号比例40，47f7fe包 | 17163 | 29.9989 | 34.410/43.217 | 4/904 | 25.590 | 768×436 |

输出均1280×720，普通窗口draw约354、dispatch约53至54。CSV取已标记的完整区间，PERF窗口排除边界两段。尺寸采样有单独pass profiler标记，普通性能测量期间未抓帧。上述静止世界地图达到当前30限帧，不能覆盖移动、技能、室内或剧情表现。

比例60与原世界地图接近，单独这一组不能判定设置生效。随后按UE命令格式修正引号，再用40作较大差距对照，确认guest_argc2而主通道尺寸仍未改变。此入口记为NO_OBSERVED_EFFECT，实际命令消费或后续覆盖原因未知。已保全实验源码patch、APK及日志，再精确恢复4de88c8的GameActivity和配套seed接线测试，原APK预热及后台启动断言保留。最终候选移除无效果的Intent入口。[UE命令参数格式](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-command-line-arguments-reference)

11月5日荣耀已有UECommandLine.txt重定向、ini/ExecCmds/DPCVars的阴性历史，见state.honor_native_1080_cmdline_20261105。本次沿不同guest argv入口仍未量到改变，后续比例适配须找到实际游戏分辨率策略再继续。

普通A窗口记录bc_native0、bc_decoded1134、live_images791.2MB、bc_images389.3MB、bc_payload72.4MB。这组为分配量与累计建图计数，无法直接换算每帧显存带宽或FPS。BOTW固定源码textures.c的BC1转ASTC方案与本项目当前解压路径不同，先在现成pass-live下补最多64行的格式、storage/GPU写入用途及内存汇总，核对实际可覆盖纹理。当前未启用有损转码或削减纹理内容。

最终默认启动候选ffrs-1.0.24-preview-compat-census.apk已构建，SHA256为f7a2c5a2e61aded66c80cd7e10af32d3f060f3b6e948ea01e12d577280b79bd2，3683399字节。build-final24中101项宿主检查及Android构建退出0，41份源输入前后摘要一致。已覆盖安装RG并回读APK摘要相同，存档tar前后逐字相同，启动PID23308。

PID23308已正常读取02并回到第三章世界地图。guest_argc0确认，present3630的纹理汇总为2581个实时图像、30组、无截断，实际存活的解压BC图像968个。旧PERF的1134是累计创建计数，不能称为当前存活纹理数。device-rg-final/texture-summary.json保留解析与原始行，下面列实时分配量。

| 原始格式 | 实时图像数 | GPU图像MiB | BC原数据MiB | 已有GPU写入图像数 |
| --- | --- | --- | --- | --- |
| BC5_UNORM，0x4b，RG32_FLOAT | 201 | 137.658 | 17.447 | 26 |
| BC1两类，0x41/0x45 | 324 | 61.928 | 8.171 | 35 |
| BC3两类，0x44/0x48 | 287 | 105.504 | 26.011 | 37 |
| BC7两类，0x4d/0x4e | 33 | 75.584 | 18.505 | 0 |

这份分布说明BC1覆盖只是解压图像的一部分。不能将BOTW以BC1为主的收益直接代入本游戏。固定BOTW的bc1_astc.h另明确插值误差及透明像素RGB差异，本工程还区分BC1_RGB的selector3为不透明黑、RGBA为透明黑。移植编码器须保留该区别，并处理已经发生GPU写入的纹理、线性/sRGB视图、上传和读回路线。当前没有复制编码器或启用新编码。

BC5路径因RG16_UNORM缺线性过滤而采用RG32_FLOAT，当前137.658MiB分配是可核的适配成本。该纹理是否被最慢战斗draw读取及读取频率尚未关联，不能把分配量当带宽热点的因果结论。后续优先补读取关联和保持精度的表示方案验证。

最终默认包的普通world02回归为frame5794至6697，903个间隔，29.9999FPS，P95/P99为34.921/44.146ms，最大64.188ms，超50ms共3帧。13个完整PERF窗口GPU中位25.26ms、提交CPU11.61ms、draw354.2、dispatch53.4。结果仍为当前静止机位30限帧，未见可宣称的性能改善。截图、CSV、普通guest日志及summary在render-percent/D-final-default。用户随后可直接操作战斗，root在用户测试期间只采集。

当前卡顿、荣耀首次ANR和全场景性能目标继续开放。新包为兼容与诊断候选，本轮尚未取得战斗帧率改善。未发布新的GitHub Release。

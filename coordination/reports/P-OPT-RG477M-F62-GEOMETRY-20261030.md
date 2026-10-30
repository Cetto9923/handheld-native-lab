# F62 GPU几何输入与整数纹理读取核对

默认关闭的原GPU读回诊断扩大到已经观察到的 VS b0284572b5f180f2 binding18/24/30/36/42/48 与storage12。每绑定≤64KiB、共享8MiB预算不变；独立buffer、现有命令队列复制、真实fence/invalidate；storage使用实际descriptor offset/range并先检查界限。无正常帧行为/音频/材质更改。storage12日志vk98只是原始uint字节表示，不代表storage descriptor本身有VkBufferView格式。

RG477M01445122，隔离包SHA256 `43188234bcbdade1fb0f3a09dc818fc2c7809f934514e4eb54a6c590ad5d7212`；构建62869 exit0，安装SHA核对。trial11035 exit0，PID31654/frame5126/group108/END148groups、42,609,176bytes。两次F62 query都SUCCESS/available1/samples0。storage12为16字节全零；binding18完整32768字节，R32_UINT(98)，内容2441种uint值；b30完整200字节R32_SFLOAT、b36完整1904字节RGBA8_SNORM、b42完整1904字节RG16_SNORM、b48完整952字节RGBA8_UNORM；b24仅2字节不足4字节复制界限，明确SKIP，未捏造数据。

VS原SPV binding18为floatBuffer，实际R32_UINT不匹配；当前运行时integer-rewrite事实表不包含VS该项。Khronos VUID-vkCmdDraw-format-07753要求一致。两次绘制的实际索引分别3/4与0/1，GPU源数据这四项恰好均0，因此不能仅凭类型不匹配解释本轮缺失。

临时单VS shader override仅将该绑定改为uint fetch，再bitcast回原float4寄存器，不修改原计算/输出/深度/剔除。原与候选spirv-val Vulkan1.3均0；候选SHA256 `48698f79ee7130499ffbbb42ae06f193adcd2490ef503b02258221696ea44805`。trial25066 exit0/PID4211，runtime与warm都确认载入72445words候选，但F62两次query仍0，未恢复身体护罩。此更改未纳入生产实现；启用文件已移出override目录并验证不存在，重新启动正常shader实际加载后日志无override，restore-normal/VERIFIED.json已确认；app停止。

另外读到上游同款游戏粒子网格塌缩案例：[nxnative8e0916b shader文档](https://github.com/seedds/nxnative/blob/8e0916bf04f6fb8542c52ed01b166a8796158ca0/docs/reference/shaders.md#bindless-reads-whose-handle-phi-also-carries-other-values)，描述phi混合句柄被删读取、scale0，记录3197个shader修复及未解决26次读取。它是源码线索，不证明当前shader已中该问题；本机旧reference是7a029f6，现已在本刀.local独立取8e0916b参考，仅准备单key翻译对照，未替换游戏资源或批量shader。MIT核对，无外部源码并入本仓库。

本地原件与可恢复命令在 .local/rg477m-opt-30hz-game-mode/audio-paced-transport-20261030/{fx-geometry-inputs,shield-f62-geometry17,fx-typed-fetch,shield-f62-typed17,retranslate}/。APK/SPV/GPU字节/录像不入Git。新护罩/战斗效果修复未通过；30Hz运动与系统音频证据另报告。所有常设硬门继续保留，未push、未声称可玩。

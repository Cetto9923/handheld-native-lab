# 两名法师的身体护罩恢复；30Hz与倒地闪烁仍需收口

## 结果和范围

RG477M01445122 实际录到两名法师身体周围的蓝紫色半透明蜂窝罩。只替换隔离包的一份生成着色器 `9528f530e1c95ce5.xgs`，原游戏代码、容器、存档和主包未改；没有改深度、剔除、透明度或战斗时间。此前 F62、红色覆盖、纹理整型读取和重译 b028 的实验没有恢复护罩，已清理，不能冒充这次修复。

证据根目录为 `.local/rg477m-opt-30hz-game-mode/audio-paced-transport-20261030/`。决定性证据：`shield-9528-upstream17/story-25.png`、60秒原始 `story.mp4`，以及正常路径复测 `shield-clean-motion-cg-repeat/cg-start.png`、`cg-08.png`。护罩按剧情先后消失。证明的是这段身体护罩恢复，不是全部特效正常或完整可玩。

## 公开来源、输入与复现

- [nxnative 着色器说明](https://github.com/seedds/nxnative/blob/8e0916bf04f6fb8542c52ed01b166a8796158ca0/docs/reference/shaders.md#bindless-reads-whose-handle-phi-also-carries-other-values)，revision `8e0916bf04f6fb8542c52ed01b166a8796158ca0`，MIT。其混合 Phi 的 bindless 读取修复说明了粒子属性被清零、网格塌缩的公开案例；其兼容性和帧率不继承为本机 PASS。
- 使用既有版本的原始 IoStore 着色器输入，控制键 `9528f530e1c95ce5`，内容键 `70ce858441ca8223`，FS 配对 `5fe1d12402b04134`。复用已有 shader index，没有重新扫描全库；输入容器 SHA/长度在 `retranslate/input-index.json`。实际构建命令和退出码在 `retranslate/other-build-plan.json`、`other-build-exit.json`。
- `scripts/graphics/prepare_rg477m_effect_xlate.py` 固定上述 revision，拒绝改动过的参考源码，复用既有 Vulkan 非常量 offset fallback，保留上游 `1 << 20` 查询界限。本候选没有增加项目 guarded-Phi/Boolean 扩展。脚本准备的 source.patch 和全部输入文件哈希与实际使用的 translator-src 一致，复现清单在 `shield-candidate/reproduce-tool-source/manifest.json`。
- 原 XGS 276104 bytes、15 bindings，SHA256 `b570fe2901a7bcc50c74b1a0f37ce814054da1434a4c1d5d5b56d3c872a4fd27`；候选 287208 bytes、16 bindings，SHA256 `7d20ab511ded85d5d1bb43f3d0ab43c2eaa5ac4b6caf786c5d65eec9a6db125d`。恢复一项源数据读取及对应 binding；SPIR-V Vulkan1.1 校验通过，实际载入 71601 words。
- 工具 SDK 为私有 .NET10.0.403；Oodle 解码辅助工具来自 [ooz](https://github.com/uilman/ooz/tree/9c67c6e2f892c462689592a28908576fe1a6a511)，GPL3，仅本地分析 CLI，未链接 APK。许可证、源码补丁、编译器、摘要和工具下载校验均在 retranslate 各 manifest。原始游戏/生成 shader/SPIR-V/APK 不进 Git。

## 部署、缓存与恢复点

包 `org.ffrs.perfisolated`，APK SHA256 `43188234bcbdade1fb0f3a09dc818fc2c7809f934514e4eb54a6c590ad5d7212`；外部 shader9528 SHA 为上述 `7d20ab…`。必须同时报告 APK 与外部 shader，APK 哈希不能单独界定此次修复。

`shield-candidate/DEPLOY.json` 保留原 shader、VkPipelineCache 和 pipeline list 的设备备份路径与 SHA。旧列表561项中只归档3项旧9528描述符布局，保留558项其他记录的原字节，补入2项由该设备真正录到的新9528记录；候选列表 SHA `d6d649c0149b54d47ed517a1dd23c55e2762132e5a443a8f9fbee2bb12f08f58`。不沿用旧布局假预热。回滚时仅在停止隔离进程后按 DEPLOY.json 恢复这三个文件并逐项核 SHA。安装会话2317 exit0，候选现保留；`org.ffrs.gamewindow` 与其存档未变。

## 正常路径30Hz自测

session26765 exit0。GPU probe、PCM dump、录屏、系统音频采集关闭，仅已有 buffered frame CSV 与5秒遥测。目标30、fence_spin0、paced transport1、legacy_waitall0、async0、depth-aspect1；NONE 实际 NDK枚举值10、原6150帧缓冲等音频救回点保留。启动预热560条成功、失败0。首次2898因标题视觉检查门超时退出，无有效测量，失败保留。

真实场景20轮上/下各3秒，所有 CSV 行边界内间隔保留：124.1366077秒，3720间隔，平均29.966986Hz；p50/p95/p99 为33.334808/33.846489/34.602116ms，最大169.756308ms；>50ms两次，>100ms一次，draw/image_writer不推进均0。后面三段30秒各约30Hz；不得删去第一段的长帧。

后续护罩剧情至下一战斗引入对话59.458903773秒、1740间隔，平均29.263910Hz；p50/p95/p99为33.347077/34.622308/50.133543ms，最大724.101615ms；>50ms18次、>100ms5次，无 draw/image_writer 不推进。范围内保留一次截图开销及所有转场，不能宣称全流程稳30。该区间跨下一战斗画面/资源加载，不是单一持续护罩镜头；详细边界在 cg-bounds.json。

cpu_mode只读为2，沿用此前用户游戏模式及UI核对，没有新UI档位截图，不改系统档、不用狂暴数字。遥测全程 current HAL CPU50.376–85.129C、skin37.625–46.674C，GPU cooling最大0；GPU频率EACCES未提权。物理1280×960、输出1280×720，内部场景 viewport1024×576，内部原生720P仍未通过。无新FATAL/RG16FATAL文本；已看场景建筑、护罩及转场正常，不扩张为全部画面验收。

## 新反馈：倒地后人物闪烁

用户明确是两名法师破盾、倒地后仍闪几下。复用同shader原始视频，保留 `fallen-mage-flicker-review/` 的连续画面定位和29.3/29.4秒帧：枪击光效结束后人物局部仍有暗条纹变化，疑似渲染异常，不能仅凭视频判为正常受击。角色相关 draw 在旧抓帧 group87（VS18a37643faef3594 / FSb5e9ec0ba0af7aaa）已可定位，原因未证实。

新 audio/video150s控制器24597的 title/field图片实际已经是场景/后续对话，PHASE-INVALID.json 显式作废其阶段声明，已停止输入和录制；不能当作请求的破盾闪烁或新音频验收。继续做准确阶段的单独录制/绑定证据。

先查[KHRONOS图像诊断边界](https://github.khronos.org/Vulkan-Site/tutorial/latest/AI_Assisted_Vulkan/04_multimodal_ai/04_expectations_limits.html)、[深度规范](https://docs.vulkan.org/guide/latest/depth.html)、[Epic时序AA说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine)及[Meta时间抖动技术说明](https://developers.meta.com/vr/blog/tech-note-shader-snippets-for-efficient-2d-dithering/)。它们仅给出深度/同步/透明抖动等待验证假设，不是本游戏原因。没有加全局 depth bias、关阴影、砍透明或去掉受击效果。

## 未通过的硬门

常设音频清单完整保留在 RESULT：蓝牙A2DP野外/战斗各≥2分钟耳听、真实DISCONNECTED重开流、物理AV同步、战斗特效时的音频仍待验收。之前系统录音不替代这些门，新shader下尚未取得有效全段录音。xruns不等于听感；不砍缓冲、不默认LOW_LATENCY。数据路径实际可达沿用前检，三候选/Intent/setup未改，负例setup未新跑。倒地闪烁、全特效、战斗稳定30和独立QA仍开放。本次仅自测；不push，不声称可玩。

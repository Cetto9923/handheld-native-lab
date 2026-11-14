# 第三章录屏与世界地图异常

## 输入和运行范围

本轮由用户反馈第三章最新录屏。设备为 RG477M01445122，Mali-G615 MC6，应用 org.ffrs.gamewindow，PID32442。已安装1.0.21-preview，生产源码780ebc67d4b9cfe41c7df35bc0e1a7280e0796a1，APK SHA256为07f5f0368183b7fb9228c9f744fd835bf623c05fd0e86a1ecb71e13f7ec1419e。该包的开场女主闪烁曾由用户确认消失，世界地图本轮为FAIL。

最新原片为screen-20261114-234124.mp4，39.807322秒，28,672,963字节，SHA256为d37ddc1c4eb32fb2c046eee4a8f6c2c18ca8c1da5df41ef7b265534c950c73a1。文件及命令、原生日志、shader输入和采集结果保存在.local/codex-recovery-20261114/chapter3-map/。

此前119.606522秒开场片为screen-20261114-231601.mp4，SHA256为0999ddd4d29c9c1803c7a9752fb0c81cf1660f5cdce4a65898bebe69aa1f4e69。该片只覆盖开场影片，不能作为世界地图的正常基线。开场声音中断与真实场景帧率任务继续进行，记录见CODEX-SCENE-FPS-20261114.md。

## 录屏与原生呈现证据

最新视频前段为第三章水都、随后进入LOADING，约29秒出现世界地图。31.509067秒至31.575933秒相邻编码帧间，地图前景出现巨大绿色斜面，遮住城镇基座和树林。33.855967秒至33.922378秒又恢复。中央600×300区域至少一通道变化超过32的像素比例分别为90.51%与91.89%。远景及文字相对稳定。人物浅绿轮廓可能属于遮挡提示，当前没有单独认定为角色透明度故障。

录屏从首黑帧17.952889秒到首地图帧28.831733秒，共10.878844秒。24.9295秒至28.8317秒有3.902233秒PTS空缺，媒体样本不足以直接量化应用真实加载耗时。

原生present56520的最终1280×720图也含巨大斜面。随后单帧capture56974结束于179events，约64MiB预算。HDR color0在group90至91首次出现该斜面，后处理继续继承。取证后gfx-codex-probe已恢复0。capture会改变部分缓存及clear快路的准入，故同时保留普通present证据。

## 已确认的顶点数值接口缺口

运行时group35深度预绘制使用VS a4554215780cb394。group91正式颜色绘制使用VS11320c7ea662abd3与FS77a69261b6e511cc，两个draw分别为342与589实例，count均96。group92使用VS00758c2888655e0b及同一FS。原XGS成员从设备读取并逐字节比对本机archive，输入摘要及偏移见shaders/extract-plan.json与a455独立provenance。

实际pipeline学习表确认a455及11320的location0与1均为VkFormat41，即R8G8B8A8_UINT。两路stride为4，分别按顶点和实例推进。SPIR-V接口却声明float4。00758也有相同数值类缺口。这违反Vulkan顶点输入的数值类匹配要求。[Vulkan规范](https://docs.vulkan.org/spec/latest/chapters/fxvertex.html#fxvertex-input-extraction)

两个双输入VS均有8个分量AccessChain与float Load，随后将原始寄存器位型bitcast到uint并转成数值坐标。现有integer-buffer variant只修texel buffer接口，真实vertex attribute仍直接沿format(a.fmt)进入pipeline。当前input_classes仅为未绑定属性选择常量零格式。

上述不匹配是已确认代码缺口。巨大斜面的因果验证仍需修正后复测同机位。没有将FTZ、纹理空数据或单独深度比较认定为已证根因。

## 缓存文件权限恢复

设备vk-pipeline-list.bin原为shell所有、mode644，905,028字节。实际应用UID的test -w与仅打开append均返回权限拒绝，导致学习表无法追加。原文件已保留本地及设备带时间戳备份。由run-as创建同字节stage，经SHA256一致核对后原子替换，应用UID可写测试返回0。随后正常运行自动追加到1,094,487字节，原905,028字节为完整前缀。

此操作只恢复生成缓存的属主，详细命令见pipeline-owner-repair.json。尚未测得这项恢复对帧率或冷加载卡顿的收益。

## 修复候选与验收状态

1.0.22候选按真实attribute格式数值类生成VS接口variant，保留原VkFormat、stride、实例推进与draw参数。接口变量、派生Input指针及Load使用匹配类型，Load后bitcast恢复原类型和原result ID，后续游戏运算保持原路径。类编号复用0 FLOAT、1 SINT、2 UINT。

变体identity在已有integer与typed recipe之后追加vin1。普通绘制、深度预绘制、warm loader和持久化recipe解析同步接入。每份变体从当前最终模块派生，包含原有NoContraction与typed处理。包含vin1的warm模块跳过pre-optimizer复用表，完整pipeline identity仍参与预热复用。

组合identity的实现者自测、独立ASan/UBSan复跑及原integer/typed真实SPV验证已通过。接口helper的ASan/UBSan、28份SPV验证及NDK26/API28对象编译通过。真实三个VS分别重写8、8、4条Load，独立请求location0也通过，恢复原Load及AccessChain类型后的函数体逐字一致。

22候选的99项宿主检查与Android构建均退出0，32份构建输入前后SHA256相同。APK为.local/codex-recovery-20261114/apk-history/ffrs-1.0.22-preview-vertex-input.apk，3,617,788字节，SHA256为2e4c354ed1aa8f2d6bc0b281069c84e3f931922e339e3d10b83a16459faf4774。native SHA256为cbd2264d54106c6b94df654ef605ea13346418bd11413962e29f006f124bfd10，三个LOAD段均16KiB对齐。构建与复核记录见vertex-input-build/。

构建完成时22尚未安装，等待用户保存后继续。随后用户明确回复“已保存，现在安装”，安装及地图复验记录见下节。开场女主的22同包回归仍为NOT_RUN。

## 性能取证边界

当前静止地图的CPU-only采样为present56162至56164共3帧。wall累计101.509923ms，计时范围累计64.232845ms，范围外37.277078ms。1059draw、150dispatch、26submission、0新shader。other累计31.540449ms、descriptor14.526092ms、draw5.469464ms、snapshot5.347725ms、pipeline1.305087ms、present1.219462ms，fence0。pipeline缓存150次命中、0miss。累计数据须按3帧解释，静机位证据不能外推走动、战斗或完整第三章性能。

独立视频审计、shader数据流审查与identity QA分别保存在map-video-analysis/、map-terrain-review/及其qa-vertex-identity/下。它们使用root保存的同构建输入，独立真机执行为NOT_RUN。

## 22安装与世界地图同包对照

本轮用户确认保存后，root于2026-11-15覆盖安装22。安装前保全21 APK、engine/guest日志和存档tar，安装后重新拉取APK核对SHA256为2e4c354ed1aa8f2d6bc0b281069c84e3f931922e339e3d10b83a16459faf4774。存档tar前后SHA256均为86d8a9f5f9a77604ff5d6ebde5e716169f2ac804b15306d26e72b17f26a93ae8。新PID为18193，dumpsys确认versionCode22及1.0.22-preview。

Continue最初读入第1章slot05，root随后通过游戏原有读取菜单选择第3章slot01，Lv25、游戏时间7:38:52、水都奥尔德里昂。再从旅店出门，沿运河向东至圆形广场，从南桥进入世界地图。输入命令及阶段截图保存在vertex-input-build/device-actions.json及同目录。原生存档文件未由取证脚本改写。

本轮日志确认三个实际地形VS均成功生成接口变体。

| VS | 最终identity |
| --- | --- |
| a4554215780cb394 | a4554215780cb394:ib1:6_98:vin1:0_2,1_2 |
| 11320c7ea662abd3 | 11320c7ea662abd3:ib1:6_98:vin1:0_2,1_2 |
| 00758c2888655e0b | 00758c2888655e0b:ib1:6_98:vin1:0_2 |

地图静机位保持相同角色与镜头位置，在同APK、同PID18193内用现有gfx-live-ab进行新路径、旧路径、新路径三阶段采样。每阶段两批各3个普通原生present，共18个1280×720 RGBA8帧，同时采集Android截图、当次PID帧CSV与原生日志。前轮frame-dump目录已用带时间戳目录名保全。采集结束gfx-live-ab已恢复空值，新接口保持开启。

| 阶段 | 原生present | 画面结果 |
| --- | --- | --- |
| 新接口第一轮 | 13290–13292及13440–13442 | 城镇周边地面、道路、河岸及树林的空间关系完整 |
| 旧路径，LIVE-AB在13620生效 | 13710–13712及13860–13862 | 地面大面积缺失，露出水面，城堡下出现黑色矩形，右上出现巨大斜面 |
| 新接口第二轮，LIVE-AB在14040恢复 | 14130–14132及14280–14282 | 地面、道路、河岸及树林恢复，与第一轮状态一致 |

此结果支持顶点数值接口缺口是该机位世界地图地形异常的成因，新接口在本次同机位对照通过。21原片镜头角度与本次初始地图镜头存在差异，本结论以22同进程对照为依据。完整游戏、世界地图其它位置、开场女主、战斗特效与其它设备各自保留验证状态。

设备执行与采集由root完成。perf_cpu及lookup_review只读复核保全输入，属于独立离线审证据，未独立操作真机。全部原始帧、同名PNG、命令计划和摘要索引在.local/codex-recovery-20261114/vertex-input-build/map22/。evidence-manifest.json记录48份阶段文件，共171,877,761字节。framedump的同步读回会引入长帧，性能核对保留这部分影响及稳定窗口的各自范围。

独立审阅将18个bin按header重新解码，与同名PNG的RGB逐像素核对一致。新路径12帧完整，旧路径6帧均显示上述结构异常。固定前景ROI为x250至1000、y420至650。绿色像素比例在新路径第一轮为84.11%至85.58%、旧路径为19.71%至22.99%、新路径第二轮为80.93%至83.41%。同一区域蓝色比例在新路径低于0.03%、旧路径为61.56%至61.91%。统计规则和输入摘要保存在device-qa/，颜色统计作为空间结构观察的佐证。

同PID18193的三阶段CSV与日志逐步追加，on1及off文件分别是on2文件的完整字节前缀。两批采图间各取60个呈现间隔，on1帧13321至13380为30.002FPS、off13741至13800为29.999FPS、on2 14161至14220为30.003FPS。三阶段尾部各60个间隔依次为30.009、30.030、29.999FPS。正常窗口仍保留on1帧13518的53.050ms与off帧13746的58.176ms长帧，dump及其后继帧的42至70ms同步读回影响单列。当前证据只支持该静机位约30FPS，尚无帧率收益结论。

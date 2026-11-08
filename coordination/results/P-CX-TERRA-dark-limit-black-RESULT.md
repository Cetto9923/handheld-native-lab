# 剧情色块第一次轨迹未命中 — 2026-11-08 12:12 +08:00

用户确认剧情画面方块而非正常战斗；用户后观察画面似乎修好但帧率低，尚未构成该缺陷修复证据。首个 gfxreconstruct 单帧 .local/shield-save-isolation-20261108/resume-contract/scene.gfxr 513883697B SHA256 2b9f82400b8ffa188340c42d885fb8851146a24d3b6985ade29148703b95f9fd；全部838308个压缩块解码、EOF和一帧边界完整，frame15250；同包1d30真机原样重放 screenshot replayed-scene.png 显示战斗时间轴教程而非用户指定剧情镜头，明确 NOT_HIT，不用于首错。原因是观察截图后触发过晚，下一次将同步脚本在自动前进到剧情后立刻触发；不能在暂停/错相位取得截图冒充目标帧。已有用户确认图 advance3.png 和视频保留。此前普通包经 SHA4a91、设置四 null/triggerfalse 恢复；当前同包1d30 replay正运行，后续需再次恢复。第一次 install -r 重放包被版本降级拒绝，按既有方案 -r -d 后成功，未删数据。

# 开场剧情矩形色块复现、隔离存档可继续 — 2026-11-08 11:05 +08:00

用户明确：进入首次战斗前的剧情画面出现多处半透明颜色不一致方框，顶部和底部均有；当前战斗画面正常。用户亲自确认 .local/shield-save-isolation-20261108/advance3.png（SHA256 23d2215821f84b06e6d87998883f72b27c29fd3cb34d882e0098ae105d4b0695）就是缺陷画面；连续录像 opening-first-full.h264 SHA256 909a435222c6cbab4ee5203db518ab574aff9489aa88cac56e182d40db373a64。不得按 PASS 或护盾施法验收；首错未定位。

新隔离调试路径 first 实机写入 settings，正常 save 的 ls 仍304368B/09:35（不等于字节校验）。第二版选择仅 debug 且显式允许的严格格式 shield-regression-[0-9]+ app私有目录，并拒绝符号链接/越界，合成测试PASS；APK .local/shield-save-isolation-20261108/resume-contract/apk/shield-resume-debug.apk SHA256 153b7e5ce8d040179b631bc4926200cbfd5b15397c464a084c020d3d44f4cb18，install-r 后 log 实际 root shield-regression-1761428033271459419，Continue 确实回到剧情前道路，用户不必反复新建。当前平板 PID22306，此诊断APK，剧情前道路，marker0；与交付包 native 库哈希不同，不能直接冒充交付包验收。下一步复用既有 gfxreconstruct 层/重放工具，在可重复回到的剧情镜头抓实际 GPU 状态并定位色块，禁止错相位暂停。完成后恢复普通4a91、四层null等；60目标尚未达、Honor400Pro未测；音频 WAIVED_BY_USER_NOT_TESTED。

# 开场护盾回归：隔离新存档准备 — 2026-11-08 10:38 +08:00

用户确认护盾只在开场动画见过。新增仅显式调试参数 isolated_regression_save 启用的空 app 私有存档根，普通启动返回原 saveRoot；非调试构建或创建失败时中止启动，不回退原存档。合成合同通过：默认 null/managed 根不变、非 debug 拒绝、两次隔离根不同、模拟原存档字节不变、创建失败中止。真机还未测试，护盾仍 NOT_RUN；不复制真实存档。构建 .local/shield-save-isolation-20261108/apk/shield-isolation-debug.apk SHA256 43ecbd8df39cdfc7abe9f90c640069489acb5031159a453236d79842ad4f4ee9，plan/build-logs 同目录；替换前原存档仅可 stat 为 304368B/09:35，不声称内容已有备份或字节未变。下一步 install -r 并以隔离参数启动核实实际根，再观看开场；结束恢复普通 4a91。音频 WAIVED_BY_USER_NOT_TESTED，60Hz 与 400Pro 未验证保持原状态。

# 存档只读结果纠正 — 2026-11-08T10:25:21.651576+08:00

纠正上一条“GameSaveData0 102B”：实机ls显示该文件实际304368B、mode0600、时间2026-11-08 09:35；102B是adb exec-out cat的权限拒绝消息，而该adb命令本地退出码仍为0。应用run-as对该外部文件test -r也返回1，不能将错误文本当存档字节、不能判断章节或是否可施护盾。本轮仅查元数据/尝试只读，没有写入、复制或移动存档。

较早2026-11-02记录同路径304144B，此次大小/时间不同；缺可读取的旧内容和有效备份，不能据此确定发生过何种游戏操作或数据丢失，也不能声称已有可恢复副本。需保护现有文件，先只读核查app进程能否安全使用独立saveRoot及已有合同测试，未经证明不得写原路径或重置数据。护盾魔法回归仍NOT_RUN；音频验收用户已免除，不是音频PASS。原始命令结果与纠正见goal-completion-audit-20261108/save-read-correction.json。

# 后续验收核对：现有存档位置与护盾场景 — 2026-11-08T10:20:57.607684+08:00

上一轮TXT及音频免验是用户直接要求的有效推进；图形捕获/普通视觉结果保持。此次只读确认：六段普通回归录像SHA与原索引一致，Dark旧实测APK和当前普通4a91的13个运行时条目逐字节一致；这属于证据完整性核对，不冒充新视觉测试。音频验收已由用户免除，状态WAIVED_BY_USER_NOT_TESTED，不再作为阻塞。

护盾魔法在已测试序章招式中不可用，仍NOT_RUN；先前防御动作不能替代。为避免触碰现有存档，只读检查真实设备：managed路径files/saves不存在；源码JNI的legacy回退实际是应用外部files/codex-local-save，该目录存在GameSaveData0（此前误认102B，实际304368B；见顶部纠正）和设置文件。此前102B系权限错误消息；仅从文件名/长度无法判定章节或是否可施护盾，未写入、替换或复制存档，也未操作其他包。managed缺目录不等于存档丢失：legacy回退由guest_boot_jni.cpp:221–229显式设置。

现有JNI nativeStartInstalledGuest可接收独立saveRoot，现有tests/native/local_save_contract.c覆盖本地存档根；当前GameActivity只在managed模式传入。下一步先只读核查现有合同测试及可复用的隔离启动机制，判断能否使用新的独立存档副本复现护盾场景且不覆盖原存档、不改变数据自动发现；没有证明前不做任何存档写操作。若已有存档仍在序章，则需要推进至可施护盾的场景或获得该场景，不能将NOT_RUN写PASS。60fps目标仍未达，400Pro实机未测；持续目标保持active。

# TXT安装说明已重写；音频验收按用户要求免除 — 2026-11-08T10:13:27.443201+08:00

用户最新指令“音频 验收不需要了，手册重写下 用TXT，不要提旧版本，言简意赅说清楚这数据包怎么放和怎么安装和使用就行”已执行。音频状态为WAIVED_BY_USER_NOT_TESTED：未做人耳测试的事实保留，音频验收不再作为本次交付门槛，不重复索要耳机/听测。常设音频源码保护不变，未将免验冒写成音频PASS。

正式说明为coordination/docs/HONOR-400PRO-INSTALL.txt，交付文件.local/honor-delivery-20261107/release/安装说明.txt（UTF-8，1106B，SHAaff0eb86fd4a268525705e6d40f2d8d44aefc0154ade9452d3b9925d968d559f）。只说明电脑解压ZIP、FFRS放手机内部存储根目录、安装APK/授予文件权限、标题选择与PAD/手柄使用；无版本历史、调试或验收段落。已有MD副本同步为相同简短内容，避免入口显示过时说明；APK/17GB数据ZIP未改、未重打。

此前持续目标离线完成审计确认6段普通视觉回归录像SHA仍一致、Dark与最终普通APK的13个非签名runtime条目实际逐字节一致；不冒充新视觉/音频实测。审计.local/honor-device-profiles/flame-20261104/dark-shader-retranslate-20261107/postbaseline-20261108/full-native-capture/half-fact-fix/goal-completion-audit-20261108/artifact-and-gates.json中的“需人耳”阻塞已被本次用户免验指令解除。捕获闭合e381fcd及旧tag保留；60帧仍未达、400Pro未实测等事实不因免音频而改变。本次仅文档/元数据改动，没有操作平板、安装或改音频/渲染代码。

# Honor 修后双pair捕获／原始重放闭合，已恢复普通1.0.2 — 2026-11-08T09:56:56.016545+08:00

**CAPTURE_COMPLETE=YES**，限指定暗魔法9836与独立Limit19659各首个pre/color draw：实际GPU输入、实际下发四module/每对、全部绑定可达mip/layer/aspect、color/depth/stencil前后附件与实际pipeline/dynamic/descriptor/lifecycle状态已具备；每target单独重放未改API/shader，Dark174/174独立COPY逐一一致（pre71/color103），Limit126/126独立COPY逐一一致（pre22/color115，137语义比较含11别名）。missing0/IO0/完整END，native全部COPY文件名集合与比较集合严格相等。Limit同帧共每pair4次matching，按原生f.seen只采首个；另6实例保留全API，不冒称native覆盖。D24仅定义24bits、VB可达前缀/未用bound尾部、allocationpadding范围均明示；VK_NOT_READY返回码差异保留，不能从字节相同推成返回码全一致。

实际身份：Dark API1159530/1160967、GEQUAL/EQUAL、43个buffer binding/offset/range/type逐项相符，原trace559294482B/SHA37ef9e5e3918b521adcff0f52d6ead159d75f2f9581cb31b65a5643b22f2cc3b；B42本帧256x256/9mip完整BC4块每端43704B，不套旧512/10mip。Limit API2175722/2176830在StateEnd2174897后、均GEQUAL、36个buffer绑定逐项相符，trace744085458B/SHAa516d8a1eefa28df63ff1691558182083edaf6fe6066466dd2337f4664da18d3，frame19659..60。两份API精确EOF、所有压缩块完整ZSTD解码；四实际module各与native准确bytehash相同。完整moduleSHA、资源/世代矩阵、日志、复验命令见清理后索引coordination/experiments/honor-capture-20261108/postfix-closure.json及postfix-stream-20261108.json；私有根为half-fact-fix/fixed-stream-20261108/heap-readback/active（Limit在limit-purple）。未跨帧拼UBO/texel/mesh。

**VIEW_VALIDITY=FIXED，VIEW_VALIDITY_FIXED=YES**，限8已观测half误标事实/其实际类型及当前R32explicit range审计：Dark29696/Limit39263次创建均0坏range、0实际submitted坏引用；既有非零/负零/边界16case合约与8实际源类型证据保留。不扩range，不全局删UInt表，不外推全VUID/全游戏。历史非法trace原样保留，不拿view合规冒认黑效根因。**FIRST_WRONG**仍为先前各自正式Maxwell重译证明的VS丢失buffer读取→Position；Boss另为CS共享归约双计数。此次未找到新游戏GPU首错；新增的是诊断page_guard映射直接交fwrite的内核I/O失败，源码febedab在既有fence/invalidate后先CPU复制普通heap，174/126真机原生读回和重放现已验证，默认marker0渲染/声音语义不变。

代码／构建：普通运行时代码6a90ed1ed30bedc2c368cde64eed18d6a2e32106，捕获诊断代码febedab8859153ef07064f68d2ae28fbfa36f96d，Dark证据1ec04b9。临时captureAPK537cce8cbf0dbd7944a29d3c44edb8971d794fe60e3896f8f35682c241bcc430、原61abd层及既有1d30重放包已复用；没有重建/下载工具、没有删旧trace/数据/存档。所有失败/负控制保留：6248 watchdog134/truncated无END；13313直接mappedIO74错；8485/9748菜单和17676/38166/10761早期时相NOT_HIT；错误蓝阶段夹逼推断由原普通录像纠正后4.0sec命中。初export目录缺失、Python3.9file_digest、DarwinSIGBUS测试覆盖、恰2匹配draw假设错误均保留原日志并修正；均非额度错误，没有虚构quota耗尽。

普通视觉沿用精确4a91最终1.0.2回归及13runtime条目相同的Dark样本：**VISUAL_DARK=PASS_SCOPED，VISUAL_LIMIT=PASS_SCOPED，VISUAL_BOSS=PASS_SCOPED**；guard/fire/summon范围PASS，shield魔法NOT_RUN（防御不替代）；首战闪烁只在186编码帧范围NOT_REPRODUCED。当前诊断截图不充普通视觉或独立QA。**AUDIO=NOT_RUN_USER_EXPLICIT**；AAudio NONE实际NDKenum10/fence0/burst*2/TIMEOUT重试/设备变化重开保持；A2DP野外及战斗各>=2分钟人耳连续、连接/断开恢复均NOT_RUN，xruns不当PASS。本刀无抬帧改动。60未达（此前32.66594 distinct新帧/s），400Pro/独立QA/全游戏NOT_RUN；整体未验收通过，持续总目标不标complete。

恢复已核：只用adb -s A42BBB5109000333与org.ffrs.gamewindow，install-r保留数据恢复普通FFRS-Honor-1.0.2.apk（3749266B/SHA4a91b71cf13689029b13156ab831b9387890ff8443cf171657f6a9bb42cc1002）；实际base.apk SHA完全一致。PID13049、GameActivity实际标题、PADhidden截图ordinary-final.png已查看；marker0/triggerfalse/四app-onlylayer设置均null，先前capture_file/frames3/unassisted与空compression/timestamp/asset属性全部还原。场景停在标题，不再操作平板；同包数据/全部旧证据保留，未碰RG/其他包/kill-server/卸载/清数据。

交付仍为.local/honor-delivery-20261107/release/中的普通1.0.2 APK、同一16918705179B数据ZIP（SHA01a338c52b79869c04f486eb1d42ce926fa6935d28d19371ca0a7c6cd10411af，40547文件含11修复XGS）、安装说明md/txt；数据全CRC/逐文件SHA验证复用，不重复打17GB包。源码手册coordination/docs/HONOR-400PRO-INSTALL.md，BASELINE-HONOR-20261107.md顶部已补当前1.0.2/捕获状态，旧包/tag不移动。此次停刀前只提交自有复验脚本/清理索引/报告；不推送。后续最小工作是剩余外部验收和有证据的60性能工作，不能把本捕获闭合外推为60/音频/400Pro通过。

# Limit修后同帧捕获已命中；独立重放准备 — 2026-11-08T09:42:32.714742+08:00

已按普通录像真实紫球阶段4.0sec触发，同537c/PID20608，无新构建/下载/安装。native19659首次pre+color126/126GPU读回，missing0/IO0/pairMask3/完整END14840740B，strictauditPASS。API744085458B/SHAa516d8a1eefa28df63ff1691558182083edaf6fe6066466dd2337f4664da18d3、2181766blocks精确EOF/frame19659..60。实际draw2175722/2176830在StateEnd2174897之后，pipeline1248/2421，四module与native字节hash匹配；36个buffer stage/binding/element/offset/range/type逐项一致。实际preGEQUAL/write1，colorGEQUAL/write0/stencil1；39263次view创建0坏R32range/0submitted坏引用。

本帧同module共8个matchingdraw（每pair4次）；旧discovery恰2断言失败原log保留。已按native full_draw_begin的f.seen只捕每pair首次规则选择第一pre/第一color，其余6次完整保留在API，未冒称native覆盖；新的discovery选择由全部36个绑定进一步核对，不是放宽错误输入检查。无跨帧拼装。native附件本次实际部分480x272，随实际格式/尺寸审计，不套用旧native17938的bound或像素数；没有本刀主动降分辨率。

Dark9836原始GPU与未改API单目标重放174/174已闭合commit1ec04b9。Limit目前同帧native/API/view合规通过，原始GPU/replay输出仍PENDING，CAPTURE_COMPLETE仍NO。原FIRST_WRONG Position/sharedsum/诊断IO根因、普通4a91范围视觉PASS保持；AUDIO用户NOT_RUN/60未达/400Pro/独立QA/护盾魔法NOT_RUN。当前复用1d30准备单目标重放、restore先前properties/layers，744MB推送前设备3.4GiB足够留1GiB；原数据/旧捕获保留。结束须restore普通4a91/marker0/triggerfalse/四layernull/PADhidden，并更新RESULT/HANDOFF/state。

# Limit时相纠正／不合并错帧 — 2026-11-08T09:08:30.339570+08:00

暗魔法修后9836原生/原API单目标重放174/174一致（证据commit1ec04b9）。Limit实际1.7sec中点样本10761依然NOT_HIT：662281519B/SHA8d296894b46a2ebd0fe46cab44fb15b25067f3b9143b426082237d68e928107e、完整1377395blocks、frame10761..62、nativeEND/selected0。前一663381656B样本被低光提醒挡住施法，仍在菜单，独立负控制；未当有效phase实验。

纠正前文“0.9早/2.6晚”的推断：复查已验普通limit-v3.mp4的limit-sheet-0.jpg，蓝圆环4s/蓝柱4.5..5s在紫球5.5..7.5s之前；1.7新截图也正是初始蓝环。不是两端夹逼，先前推断不成立。下一次唯一变量改为确认返回后4.0sec，对应录像约6s紫球；同537c/原layer/FIFO，新目录active/limit-purple，普通NewGame前同包amstart-S冷启；无新工具/源码构建、无新下载或删旧件。若该明确时相仍无pair，不能继续凭蓝色猜时机或冒称Limit修后CAPTURE_COMPLETE。

CAPTURE_COMPLETE总体仍NO；DarkYES有界，Limit当前postfix缺目标pair。VIEW/FIRST_WRONG/普通视觉旧结果、AUDIO用户NOT_RUN/60未达/400ProNOT_RUN不变。最终必须恢复普通4a91/marker0/triggerfalse/四layernull/先前props/PADhidden，更新RESULT/HANDOFF/state后停刀；持续总目标不标complete。

# Honor 修后暗魔法同帧重放闭合；Limit有界中点捕获 — 2026-11-08T08:51:35.609121+08:00

Dark9836/537c实际native174份GPU输入/附件已与原始559294482B trace逐项一致：pre-only71/71、color-only103/103，missing0；174个native COPY文件一一覆盖，无跨帧拼装。API1159530/1160967、四实际module、43个buffer binding offset/range/type与资源世代及正常GEQUAL/EQUAL均匹配。29696个view创建无R32非法range、无实际submitted坏view；仅此VUID范围，非全VUID。CAPTURE_DARK=YES_BOUNDED_SAME_FRAME；VK_NOT_READY返回码差异原样保留，D24定义24bits、VB可达15540/完整bound15552范围明确。第一次replay因遗漏创建dump目录在WriteBuffer前失败No suchfile，原log保留；唯一变量新建自有目录后同工具/trace/target重跑成功。

Limit17676(2.6sec)与38166(0.9sec)两份完整trace均在StateEnd后实际pair0，nativeEND/selected0，明确NOT_HIT。最新38166 trace778852750B/SHAa64a4ece6fa3d0fbcaece8acf3668475c5dc7df87d0594e03260c979450558d2、2691206blocks/exactEOF、42082view创建0坏R32/0live，不能拿它补旧17938输入。当前唯一有界中点实验1.7sec，复用537c原工具，PID9927实际标题已看，正常NewGame路线；新目录active/limit-midpoint，FIFO capture-limit-midpoint-20261108。之后不重复盲试。

当前CAPTURE_COMPLETE=NO只缺修后Limit实际pair+同帧GPU/replay；Dark新帧已闭合。既有FIRST_WRONG Position/sharedsum、普通4a91视觉scopedPASS不变；AUDIO用户NOT_RUN，60未达/400Pro/独立QA/护盾魔法NOT_RUN。无工具下载重建、无quota错误；hostPython3.9无file_digest小错已保留并改流式SHA。装包/全局settings只限org.ffrs.gamewindow，数据保留。

下一步中点捕获按pair+END判据；成功则实际索引/世代/UBO范围/模块/合法view并独立每target1d30重放；无命中不拼帧，准确留缺项。最后restore普通4a91、marker0/triggerfalse/四layernull/先前props/PADhidden，再更新既有RESULT/HANDOFF/state并停刀。整体持续目标未完。

# Honor protected capture I/O fixed; paired closure in progress - 2026-11-08T08:15:41.615738+08:00

Post3272 capture: unassisted/ZSTD/FIFO PID6699 Dark6248 aborted with exact guest GameThread timed out waiting for RenderThread after120secs, exit134. API2080603698B is truncated; native lacksEND. page_guard same245d PID8407 Dark13313 API complete735773659B/2frames, actual GEQUAL/EQUAL/modules/25+31bindings,57948viewcreates0badR32/0live; native186queued112written74IO FAIL. All original failures retained, not quota or new graphics-root claims.

Application-only diagnostic fix febedab stages mapped readback into ordinary heap after existing fence/invalidate before fwrite. Host kernelEFAULT14 negative and guardedCPUcopy/write262144B positivePASS (initial DarwinSIGBUS138 test error retained/fixed). No capture/replay tool rebuild/download. App build0/base115ab27e plus original61abd layer repacked537cce8c,14nonsigning entries equal to base; marker0 renderer/audio unchanged, delivery4a91/tag unchanged.

ActiveDark PID23495/frame9836 strictnative174/174PASS (127image+47buffer),85117552B, missing0/IO0/pairMask3/END. API559294482B SHA37ef9e5e3918b521adcff0f52d6ead159d75f2f9581cb31b65a5643b22f2cc3b exactEOF/9836..37; actual1159530/1160967 afterStateEnd,modulehash exact,43distinct buffer stage/binding/offset/range/type rows. Actualpipe874 GEQUAL/write1,2393 EQUAL/write0/stencil1.29696viewcreates0badR32/0live, compressedblocks decoded. GPU original-vs-replay stillpending. Data under half-fact-fix/fixed-stream-20261108/heap-readback/active; no input splicing with8691/17938/23325.

Heap8485 menu sample NOT_HIT, no cast, preserved523060832B. IndependentLimit17676 API705304466B SHAa25c959ab2211f86045768a7c0d71c651f50e51e8abea401ca7dabe8ed203130 complete but Eselected0/pairMask0, laterbluephase NOT_HIT. Finite retry active/limit-retry uses E0.9sec vs2.6sec after secondconfirm, same537c. PID23495 exited SIGNALED9 during return-title, cause not established; exitinfo/guestlog preserved. Erroneous next navigation sent before launcher screenshot checked ended without game claim. Own GameActivity restarted PID12962 and actualtitle observed; normal route screenshots/inputs in limit-retry/route-pid12962. Systemlow-light reminders repeatedly consumed inputs, only transient dismissals; no settings changed. Retry APIreader/trigger not armed yet.

CAPTURE_COMPLETE=NO: newDark native/API/views checked but pertarget originalGPU/replay comparisonpending; newLimitNOT_HIT/retrypending. VIEW_VALIDITY=FIXED scoped eightobservedhalf facts/actualDark R32viewcreations, no allVUID/fullgame claim. FIRST_WRONG priorVSPosition/sharedsum roots retained; FIRST_WRONG_CAPTURE_IO protectedmapped pages->kernelIO, diagnosticfix tested. Prior ordinary4a91 scopedDark/Limit/Boss/guard/fire/summon visualPASS retained. AUDIO userNOTRUN; NONE10/burst2/retry/reopen/fence0 unchanged.60unmet/400Pro/shieldmagic/independentQA NOT_RUN.

Next: sameAPK earlierLimitEretry, nativepair must exist and lie inside APIrange. Restore saved app-layer settings/properties from fixed-stream run-plan, reuse1d30 install-r-d, one target per replay, compare defined GPU bytes/TBO offsets/D24bits/reachableVBprefix. Leave>=1GiB device margin (last3.9GiB; Dark559MB,exports50-100MB/run). Restore exactordinary4a91/marker0/triggerfalse/layersnull/PADhidden. Update originalRESULT/HANDOFF/state andstop; overallgoal remains open for audio/60/400Pro.

# Postfix full-API stream watchdog failure; finite alternative in progress — 2026-11-08T07:09:02.230521+08:00

Same6a90 source/245d APK, original61abd layer; no rebuild/download. Actual unassisted+ZSTD+FIFO Dark frame6248 attempted, guest120sec RenderThread watchdog abort134. Strict API framing and native audit FAIL (truncated finalblock/no frameend/no nativeEND); reader0 is not capture success. Raw failure/message/framing/SHA/pull/audit preserved in fixed-stream-20261108; no partial input is spliced into old frames. CAPTURE_COMPLETE=NO, VIEW_VALIDITY remainsFIXED only eight observed facts/target23; FIRST_WRONG existing Position/sharedsum conclusions unchanged. Existing ordinary visual scopedPASS retained, AUDIO userNOTRUN/60unmet/400ProNOTRUN. Finite single-variable page_guard actual-run experiment PID8407 in subdirpageguard; no game timeout or sync fix. Must restoreordinary4a91 and prior properties/settings after controlled work.

# Honor actual FIFO/ZSTD capture and existing replay control passed - 2026-11-08T06:44:45.204454+08:00

Same source6a90ed1, no renderer/game/tool rebuild. Final ordinary4a91 plus original61abd layer only was repacked as245d521c1e0ef6f5a4665d585b3907ec084dfd70e6c1393c8f11aa09aa2d6a91 (71812586B); all14 original nonsigning entries including manifest are byte-identical. Installed-r with data preserved, PID32410 reached an observed stable title. App-only layer settings, unassisted memory mode, ZSTD/timestampfalse/trigger_frames1, fresh internal FIFO filename; ordinary probe marker0. The standalone half helper route was not used because it has no explicit layer enable; no helper rebuild or global debug.vulkan.layers hack.

Actual frame19203 was streamed directly to host:426316667B, SHAca14a19504f98918ff81a9d5e8c6211ac7cdc0311d9034a47941325f5be2331b,18002blocks/exactEOF, complete state and one end-frame. All16115 compressed blocks decoded as ZSTD (15437API+678metadata),16138169110B uncompressed, max33554432B/block. Parent wait45sec expired just before actual Finished recording at06:29:50.118 (~45.5sec); reader PID68594 was absent on recheck. No restart/recapture, no invented reader exit0; exactEOF independently proved. Initial audit's128-byte header scan failed on variable-length image-init metadata (actual payload offsets up to144); corrected to unique self-described complete ZSTD frame at exact block end, API original-size field still strict. Both original errors retained; neither is quota exhaustion or a new GPU failure.

Unmodified stream file pushed once with ample device margin, reused same1d30 replay APK install-r-d (temporary debug3->1, no uninstall/data clear). All prior layer settings/properties restored first. PID1965 replayed frame19203, log finished loading state and one frame, wrote3341263B screenshot_frame_1.png SHA495a5f9c6f774f3f426c3348b3cba7c11d15cd072ee1f78b67bacd6f9ff1dab8; actual title PNG viewed. No shader replacement, sync workaround, source or trace rewrite. This proves the transport/compression/replay control, not Dark/Limit GPU equality: title contains neither target pair, so CAPTURE_COMPLETE remainsNO for the effects.

Exact ordinary1.0.2/4a91 installed-r back, startup verified PID4491 at title with PADhidden. Actual base.apk SHA matches4a91, marker0/triggerfalse, four layer settings null, compression/timestamp properties restored empty. Original FIFO nodes/trace/probes/game data retained, only owned package used. Local control-result/framing/zstd-audit/replay logs and actual restored screenshot in`.local/honor-device-profiles/flame-20261104/dark-shader-retranslate-20261107/postbaseline-20261108/full-native-capture/half-fact-fix/stream-control`; clean index/scripts incoordination/experiments/honor-capture-20261108/stream-control.json. Current code baseline/tag/delivery unchanged; ordinary Limit/Boss/guard/fire/summon regressions from6b0c12f remain scopedPASS, Dark equivalent-runtime scopedPASS retained; shield magic/AUDIO userNOTRUN,60 unmet/400Pro/fullgame/independentQA NOT_RUN. No overall completion claim.

Next concrete action is the effect-stream-next.json plan: unique fresh FIFO, same245d capture APK, ZSTD/unassisted/two frames, original D/E same-request native pair, no pause/ALWAYS/emptyFS/range expansion. Native frame must actually lie in streamed API range; check newartifact size before device push and leave>=1GiB margin. Reuse existing index/readback auditor plus installed libzstd; one target per replay avoids old CB-tail truncation. Inspect actual new shader/UBO/TBO offsets/ranges/generations/views and original outputs. Do not mix old8691/17938 or23325 inputs into the new frame, do not remove unobserved ninthfe033 based on original source Float32 alone, do not rebuild/download tools or delete old evidence. This is the remaining postfix full-API gap, not repetition of the already-closed original captures.

# Honor 1.0.2 普通回归补齐：BOSS／防御／爆炎／召唤 — 2026-11-08T06:03:37.110106+08:00

上一轮分类PROGRESS：真实源码6a90ed1、证据30a1a20及交付检查点已经落盘。本轮不装包、不重建工具、不重复下载或抓大型trace，复用同一最终普通APK `4a91b71cf13689029b13156ab831b9387890ff8443cf171657f6a9bb42cc1002`、PID26073、正常游戏路线，新增最终1.0.2本身的Limit40s、BOSS入场110s、防御40s、爆炎40s及巴哈姆特40s录像。代码/签名/数据ZIP均未变，不把新录像当新的性能测试。

结果：Limit实际蓝白冰晶9..10.5s、碎冰/雷电/火尾/光束完整，64样本0..31.5s自测PASS；BOSS实际落地约67.5..69s，66..71s共251个编码帧逐帧检查，棕色碎石/尘雾与角色正常，无旧贯穿画面黑条，55..79.5s另2Hz查看；Defend真实执行后受击195、HP5199→5004，无黑块；爆炎3.25..4.5s橙金火焰、Total5775，随后敌方光柱与角色/场景保持；召唤蓝白入场/紫碎片/太空龙体/光束和第一次协战均正常。均仅实现者有限视觉自测，未冒称独立QA/全游戏。原Dark27fa普通130s scopedPASS及13个runtime内容与最终4a91逐字节一致证据沿用，不再重做已经闭合的施法实验。

首战短闪补查：复用既有Dark普通录像72..79s，全部186个编码帧已查看（先前只有2Hz）；所见为方块消隐→黑场加载→战斗单向淡入，无反复明暗或巨大几何，该次NOT_REPRODUCED，不能外推所有首次入场。护盾魔法仍NOT_RUN：当前序章四招暗/爆炎/冰/雷，下4次回首项，未找到护盾；Defend不得替代护盾魔法。额外只读核对未观测fe033源B24确Float32/scalar0/slot132，但没有实际producer格式/range/use现场，未据此删第9项或全局删表。

完整索引与SHA：`coordination/experiments/honor-capture-20261108/visual-regression-1.0.2.json`；原录像/输入命令/计划/所有逐帧图在`.local/honor-device-profiles/flame-20261104/dark-shader-retranslate-20261107/postbaseline-20261108/full-native-capture/half-fact-fix/regression-1.0.2/`。final-move-v3录像只推进对话，明确NOT_HIT，真正召唤以summon-v3录像为准。当前现场PID26073普通BOSS战斗菜单与巴哈姆特同屏，PAD隐藏、marker0、triggerfalse、四layer值null，设备余4.3GiB；截图与实际查询postflight.json/png。没有清理或移动任何旧probe/数据/存档，没有RG或其他包操作。

CAPTURE_COMPLETE=NO（修后独立整帧API/原始重放尚未做，历史坏view不被隐藏）；NATIVE_DATA_COMPLETE=YES限此前指定两对；VIEW_VALIDITY=FIXED限8已观测half及目标23；FIRST_WRONG既有三处GPU根因不变，当前回归未找到新首错。VISUAL_DARK=PASS沿用等价runtime、VISUAL_LIMIT=PASS最终1.0.2，VISUAL_BOSS/guard/fire/summon=PASS限上述窗口。AUDIO=NOT_RUN，用户明确“暂时不能，音频记未验收”，A2DP野外/战斗各≥2分钟主观连续、蓝牙连接/断开恢复均未做；NONE(enum10)/burst*2/TIMEOUT重试/设备变更重开/fence0均未改，不能以机器日志顶替人耳。60仍未达（此前32.66594新帧/s），本轮无pacing/hint/affinity改动；400Pro、完整游戏、独立QA未测，持续目标不标complete。

下一步先离线核对既有capture writer是否有可复用流式/更小原始捕获路径，以免4.3GiB余量下盲抓12.5GB以上trace；不重建导出器、不删除旧证据、不因材料缺失伪称全CAPTURE_COMPLETE。人耳A2DP门仍需外部条件，遵循用户无需重复询问授权的指令。交付APK/ZIP/手册与旧本地标签保持不变，补充回归索引进入后续证据提交，不移动旧tag。

Stream feasibility 2026-11-08T06:12:12.576774+08:00: old raw block offsets show Dark state snapshot627MB and frame payloads3.605/4.674/3.602GB, Limit snapshot900MB and frame payloads8.382/6.640/6.635GB. Most bytes are per-frame unassisted writes, not the initial snapshot. Existing61abd layer68068840B contains ZSTD symbols (presence is not execution proof). In a new owned internal app directory, a1MiB zero-data FIFO run-as/head/cat smoke passed exact size/SHA and both exits0; no package/layer setting/game data changed. FIFO node retained, both helper processes finished. Actual layer-to-FIFO and compressed replay remain NOT_RUN. Host free33GiB/device4.3GiB. Next reuse existing half GPU helper and original layer for a small actual streaming control before any game capture. No tool rebuild/download; CAPTURE_COMPLETE unchanged.


# Honor 捕获闭合、half 类型与小窗启动修正已提交；1.0.2 成品 — 2026-11-08T05:26:04.613396+08:00

代码提交 `6a90ed1ed30bedc2c368cde64eed18d6a2e32106`（本分支work/honor-gtpro-30hz，仅7个自有源/测试文件）；捕获能力提交仍1b8b3f6。最终普通APK`4a91b71cf13689029b13156ab831b9387890ff8443cf171657f6a9bb42cc1002`，3749266B，versionCode3/1.0.2-honor，保留数据install-r已成功、实际新PID26073已在同小窗冷启进入标题；13个非签名内容条目与实测27fa逐字节相同，只有版本清单变化。未push/切树/reset/clean/stash，未碰RG或其他包。原214fd794/honor-graphics-20261107基线、2a4旧包保留。最终安装身份/前台核对已完成（2026-11-08T05:32:19.713660+08:00）：base.apk SHA与交付4a91完全一致，versionCode3、PID26073普通野外，PAD隐藏；gfx-codex-probe=0，debug.gfxrecon.capture_android_trigger=false，四app-only layer设置均null，实际硬件768dp/tablet/inline1。原始核对位于half-fact-fix/final-device-state.json、final-guest-external.log及final-field-pad-hidden.png。证据文档与复验脚本随本地检查点标签`honor-graphics-20261108-view-facts`保存，旧标签不移动；保持同线程唯一writer。

本轮原生数据/重放结果：旧正常Dark8691的190个独立GPU读回全部闭合（pre78/78，唯一变量color-only112/112）；Limit17938独立126个读回也闭合（pre22/22、color115/115，含11条别名语义引用）。实际四pipeline/八module和GPU UBO/TBO按binding/offset/range/代数关联（Dark43、Limit36个buffer绑定逐项一致），完整可达BC4块/mip及color/depth/stencil附件前后保留。旧双目标导出的14项附件差异由先前目标CB尾部被截断导致；color-only原trace复放全部一致，不再把合法遮挡少2455像素当新shader错误。VK_NOT_READY警告保留，数据一致性不能夸成所有返回码一致。

VUID风险限域修：只移除26ae/8b27/9528的B18和be4e/431a/1e22/b028/6018的B24误标UInt事实，保留每个shader其它raw绑定及未观测fe033 B24。现有归档按loose优先读取，8个真实源SPIR-V均Float32、metadata scalar0；源slot实际分别129/129/130/132/132/131/131/132，不统一假设129。设备实际archive索引及8个选中entry/loose override只读分段SHA与输入一致，无重复shader下载/工具重建。旧事实负控制exit4；新8个host重写模块保留half Float和其它UInt、spirv-val全0；复用已有Honor GPU half合约16/16非零/负零/边界PASS。

新27fa native23325命中Dark7771052/7771111、submit194927/194928、slotGeneration218287/218288，count6960；190/190写出、missing0/IO0、END86895216B，8个修正half在同帧全部观测native76/scalar0/range2，未扩渲染view。4个实际target module与旧8691逐字节相同，全部spirv-val0；这8个外围view修正不能冒认原黑块根因。新帧只做native捕获，未与旧帧UBO/mesh拼接，也未冒称新帧已做完整未改API重放。

小窗启动附带正确性修正：同小窗580dp，候选与旧2a4都会被旧档位逻辑选handheld_mid/inline0，在libllvm-qgl+0x476434退出。关闭warm、新cache文件两负控制仍同故障，已撤销所有临时cache源码/marker；未人为清理或移动缓存，应用正常缓存写入照常。用物理显示尺寸/稳定密度识别硬件档位、显式override保留，现为768dp/tablet/inline1，原cache与warm启用，重复冷启/实际场景正常。Java边界/手机/未知值/override测试PASS。

普通视觉：`.local/honor-device-profiles/flame-20261104/dark-shader-retranslate-20261107/postbaseline-20261108/full-native-capture/half-fact-fix/dark-verified-ordinary/dark-verified-ordinary.mp4`130秒/47185154B，在72..109.5秒2Hz实际检查完整施法86..92秒，88..89.5秒蓝白核+紫红圈，96秒回野外；此前只录野外/标题的录像均明确NOT_HIT，不充验收。`limit-ordinary.mp4`70秒/26907872B，0..39.5秒2Hz检查蓝白冰晶7.5..8.5秒、碎冰、雷电、火尾和终光束；两片全程marker0、无layer。VISUAL_DARK=PASS、VISUAL_LIMIT=PASS，均限实现者教程自测。普通背景火/角色/遮挡在上述范围正常；新包Boss/shield魔法/独立QA/全游戏/400Pro未重跑，旧2a4 Boss有限PASS保留，首战短闪未作全覆盖闭合。

**验收字段**：NATIVE_DATA_COMPLETE=YES（指定两对的完整GPU数据、实际shader与前后附件/原始输出核对均具备）；CAPTURE_COMPLETE=NO（严格全范围：历史完整trace仍含外围非法view，修正后新帧的独立全API/重放未运行；不隐去UB后宣称整帧语义合规）。VIEW_VALIDITY=FIXED、VIEW_VALIDITY_FIXED=YES，仅本刀8个已观测half及目标23个texel绑定；全游戏/未观测类型NOT_RUN，历史坏view保留原样。FIRST_WRONG=原Dark/Limit分别VS丢失buffer读→Position；Boss另为64lane共享归约双计数；不是ALWAYS/空FS/删prepass或全局waitIdle修复。AUDIO=NOT_RUN（用户明确暂不能听），NONE(enum10)/fence0/burst缓冲/timeout重试/设备变更重开未改。60目标**未达到**：本轮暖野外最后30秒32.66594 distinct新帧/s，不用屏幕90Hz、目标60或统计相等充PASS；跨场景/温度不做性能归因。整体验收未通过，持续目标不标complete。

私有安装交付位于`.local/honor-delivery-20261107/release/`：新增FFRS-Honor-1.0.2.apk及sha、1.0.2-build-manifest.json，安装说明md/txt已同步；旧1.0.1 APK及交付索引副本保留。数据仍同一FFRS-data.zip（16918705179B/SHA01a338c52b79869c04f486eb1d42ce926fa6935d28d19371ca0a7c6cd10411af，40547文件/11修复件不变），复用此前全CRC/逐文件SHA核验，不重复打17GB包。源码安装手册coordination/docs/HONOR-400PRO-INSTALL.md。不得将此图形检查点称作稳定60/音频/400Pro或完整游戏通过版。

复现实验脚本/清理后索引已保存coordination/experiments/honor-capture-20261108；实际全部构建、失败driver负控制、被拒绝的错相位录像、源SHA、模块/读回清单在上述full-native-capture/half-fact-fix。下步若继续60，复用该图形恢复点，从真实帧CPU/GPU等待/调度有证据的瓶颈推进，不重复已闭合shader试验或重建捕获工具；新大trace前检查设备空间（约4GiB），不得删旧probe/游戏数据。无实际额度错误，不虚构耗尽。

# Honor 同 draw 两对数据闭合；外围 half 类型和小窗档位修正验证中 — 2026-11-08T04:19:28.784513+08:00

Dark native8691共190/190独立GPU读回闭合；Limit native17938共126/126独立GPU读回闭合（单目标pre22/22、color115/115语义比较，11条是别名引用）。原trace未改，device/host SHA分别9ea6d528…efd0与bf9bd036…c7b0；实际四pipeline/八module、全部UBO/TBO offset/range、可达BC4完整块和D/S附件前后已同帧关联。Limit新增36个buffer binding逐项一致，Dark43；同buffer不同offset/range不去重。Dark联合导出的14附件差异经唯一变量color-only复放消失，属于导出截断干扰，非新游戏首错。证据和复现脚本索引`coordination/experiments/honor-capture-20261108/`；所有原始大件保留在postbaseline-20261108/full-native-capture。VK_NOT_READY警告继续如实保留，不把数据一致说成返回码全一致。

目标23个texel binding实际type/format/range均合法；全trace仍发现Dark16、Limit14个R32+range2创建，Dark135条实际submitted命令活跃引用、Limit0。原生PROBE_TEXEL_NATIVE定位另外8个旧half误标事实（26ae/8b27/9528 B18，be4e/431a/1e22/b028/6018 B24），仅移除这8行误标，保留raw索引/查表绑定及未观测fe033 B24。旧代码真实SPIRV负控制exit4，新代码8个编译shader保留half Float/其它UInt、fetch数量不变、spirv-val全0；复用已有16case真机half合约全PASS（含非零/负零/边界）。不扩range、不重建捕获工具，不冒认黑效根因。

候选ef028启动2次和已知旧2a4对照均在libllvm-qgl+0x476434退出。关闭warm仍同故障；新cache namespace仍同故障，两个假设否定，临时cache源码改动及新建marker已移除，旧cache未清/移动。实际日志共同点是SM8635被freeform窗口580dp误判handheld_mid、inline0。改为物理显示像素+稳定密度识别硬件档位，显式override保留；Java边界/手机/未知值测试PASS。候选27fac9d8e13d681d96d72ffbef64f9f8dc1bcee5bbd369b8989e902cbb66e796实际在相同窗口580dp选tablet768dp、inline1、原cache20711064B/warm829正常开启，并稳定进入标题页；已只最大化本包task1025，PID15485，继续普通新游戏Dark/完整Limit复验和原生类型观测。该APK未提升为交付基线。

CAPTURE_COMPLETE=NO（两对NATIVE_DATA_COMPLETE=YES，资源/输出缺项已清；全场景非法view修正尚在真机验收）；VIEW_VALIDITY=NOT_FIXED全范围，具体目标合法、8外围修正待复验。FIRST_WRONG原Dark/Limit VS读取丢失→Position、Boss共享归约双计数证据不变，未把view合规冒充黑色根因。VISUAL_DARK/LIMIT/BOSS上一普通2a4范围PASS保留，本候选NOT_RUN；AUDIO依用户NOT_RUN（NONE enum10、fence0、burst缓冲、timeout重试/变更重开未动）；独立QA/400Pro/60帧未完成。唯一writer继续，无实际额度错误，不虚构quota耗尽。

# Honor Dark同draw闭合；定位到双目标导出截断干扰 — 2026-11-08T03:26:33.976778+08:00

新native8691实际190份GPU读回已全部按同帧actualAPI和GPU导出核对。TBO并非缺GPU文件：现有1d30导出器只漏JSON descriptor条目，整VkBuffer的rp_0.bin实际存在。13段TBO（含两端B18有效2B）按actual view bufferId+offset+range各自切片，全部与nativeGPU原读回字节相同，未按同buffer指针去重。初联合pre+color导出的190项中176相同、14附件项不同，差异是color前已存在；pre前后三平面完全相同，color前depth竟逐像素等于pre刚结束depth，缺Native之后16768个角色/场景深度写入。

唯一变量改为只请求color1047825（同原始trace、同1d30、无shader/工具/原trace修改），112/112项（含7段TBO、完整mip/附件before-after）原运行与重放全部一致，missing0；pre另78项原已全部一致，合计Dark190份GPU读回闭合。D24仅比较定义的24bits，allocation tails剔除；VB导出15540B对应实际indexed可达前缀，native完整bound15552B的末12B未被此次索引读取，非伪称全bound回放读出。原生pre覆盖388882，实际color写386427，合法遮挡少2455；联合导出错误增到388882，不能把这2455差异或覆盖不同重新归成shader/depth bug。该受控结果定位到现有dump请求截断先前目标CB后续工作；同CB最后requested draw之后的命令不应再作为下一个目标的原场景输入。后续每次只导一个目标，禁止把联合导出前后附件当原运行金标准。VK_NOT_READY警告仍单列保留，不声称全修。

实际pipeline创建字节独立解出Dark840 pre GEQUAL/write1，2394 color EQUAL/write0/stencil1；Limit1245 pre GEQUAL/write1、2419 color GEQUAL/write0/stencil1，与native正常状态一致，非当前源码猜测。四个pipeline的actual module bytehash与native精确pCode一致，见actual-pipeline-depth-states.json。新Limit22.556GB已device/host SHA一致bf9bd03611b90323e7ce0bb0a15fdac959538e86e3956bb2445894b912edc7b0，2308406 blocks精确EOF，17938..17940，actual目标2296771/2297991在StateEnd2294227之后，排除了旧恢复CB错相位。Limit pre-only已22/22读回全相同/missing0，color-only原trace重放进行中session51575启动记录已保存。

CAPTURE_COMPLETE总体仍NO，仅剩Limit color原始输出对照及汇总/恢复普通包；Dark原生全部GPU输入+实际shader/state+完整附件和未改trace单目标重放已闭合。全VIEW_VALIDITY=NOT_FIXED，目标B18实际R16+range2合法，不外推全游戏；FIRST_WRONG原游戏三处GPU根因不变，新增FIRST_WRONG_REPLAY记录导出干扰，不冒认新游戏bug。VISUAL_DARK/LIMIT/BOSS普通2a4有限PASS沿用，AUDIO依用户NOT_RUN、60仍未达/400Pro未测。源码capture能力本地commit1b8b3f6，当前设备临时1d30 NativeActivity重放，四layer设置null/triggerfalse/marker0，结束须restore2a4/PADhidden。唯一writer继续。

# Honor Dark/Limit native原始捕获两端已齐；转原trace一致性验收 — 2026-11-08T02:55:36.028988+08:00

同捕获APK769d1d28/PID29937的Limit frame17938独立命中pre draw6671711/submit154037/slotGeneration172078与color6671726/154038/172079，count1800。pre/color均实际NVN7(GEQUAL)，color不写深度；正常FS、非ALWAYS。126/126GPU读回（86 image子资源+40 buffer切片），无缺项/IO，binary85982740B，132bin+trace共133文件，完整END；host audit PASS、四实际module均spirv-val0。preVS7a04879d7cdb4febfd5fd9b4d61c944fe6990ea86886e0c0cc4c3a9b517a703b，colorVS85eb0efe0491f1878bf9f6600f6d79ca4b626a39fa8fd8a7da93d4d958f80e0e，FS原始hash/index见limit-native-key-evidence.json。B18两pass实际R16_SFLOAT76/range2。截图limit-during.png是活跃紫色阶段，非暂停。新Limit trace22556150669B，02:44:40 Finished recording，deviceSHAbf9bd03611b90323e7ce0bb0a15fdac959538e86e3956bb2445894b912edc7b0；唯一一次pull进行中（session20909/PID27108），transfer-plan/pull.log落盘。设备剩5.1GiB，禁再抓大trace/不删旧文件；现有两帧已足以继续。

Dark12.5GB新trace已设备/host SHA一致9ea6d528ce031d7e4afaaa72d2e067ff76ac7ab516baeac7b9943f4b60dbefd0，1057625 blocks精确EOF，frame8690..8692；native8691在其中第二帧。复用旧index/parser只改输入，1039modules/829pipelines保留。actual pre API1046335（pipeline840/VS803/FS805）与color1047825（pipeline2394/VS2388/FS2390），count6960/instance1，与native唯一目标一致；4module全部精确bytehash一致，缓存有同字节重复module也明确保留。两draw43个buffer binding的effective offset/range/format与native逐项匹配，未按buffer指针去重，实际view仍R16 range2。

已临时保留数据install-r-d原1d30b246bbce2455c74f7d6faeea516e5aefe5fbce59231bb8d217315ed780a9重放APK（版本2→1，仅本包debuggable诊断，非卸载/清数据），app-only四layer设置恢复此前null、triggerfalse。第一次裸draw编号文件被现有导出器报Bad --dump-resources parameter:1046335并SIGABRT1575/1901；原始logcat/exit-info已保存，不能冒认游戏崩溃或额度错误。现复用旧target_indices.py从实际API流生成BeginCommandBuffer/RenderPass/QueueSubmit定位，启动v2重放（session1586）；未改工具或原捕获。安装source与失败/恢复命令全部full-native-capture目录。完成后须恢复普通2a4/marker0/PADhidden。

CAPTURE_COMPLETE仍NO：Dark+Limit native raw完整性各PASS，Dark actualAPI module/buffer定位通过，原始/replay定义区域逐项一致性未完成，Limit API索引待新trace完成传输。全VIEW_VALIDITY仍NOT_FIXED，不把目标B18合法化外推整帧；FIRST_WRONG已有三处GPU根因不变；普通2a4 VISUAL_DARK/LIMIT/BOSS及guard/fire/Bahamut有限PASS记录保留，本新诊断不代替普通视觉/遮挡验收。AUDIO依用户NOT_RUN，常设NONE(enum10)/burst缓冲/2026-11-08T02:55:36.028988+08:00OUT重试/变更重开/fence0均未改；60不达、400Pro/独立QA未验。代码诊断能力将本地checkpoint提交，不改既有graphics214fd79/tag/交付2a4/dataZIP。唯一writer继续。

# Honor 暗魔法原运行完整读回已通过，API/重放闭合进行中 — 2026-11-08T02:40:14.030200+08:00

同APK769d1d28/PID29937，正常NewGame→首战暗魔法、无暂停，D与原61abd GFXR三帧trigger同请求。native frame8691命中pre全局draw2269459/submit71759/slotGeneration80482与color2269518/71760/80483；count6960/instances1。实际记录pre GEQUAL(NVN7)、color EQUAL(NVN3)，无ALWAYS等覆写。GPU原格式读回190/190（143图像子资源+47 buffer切片），缺项0/IO0，196bin+capture.txt共197文件；binary总86895216B，完整FULL_BEGIN/FULL_END/END。严格auditor对实际文件、尺寸/SHA、每binding有效offset/range、全部mip/layer/aspect、VB/IB、模块和附件前后覆盖给native_readback_audit=PASS；这不等于全CAPTURE_COMPLETE。

同帧B42 Vk139 BC4确为10mip0..9，每pass174776B原压缩块（实际512×512基面，非host契约512×256示例），无缩样；B18实际R16_SFLOAT76/range2两端0000。诊断copy4在已证32MiB VkBuffer对象内，最终文件只存有效2字节，渲染view未扩大。同帧四个最终module words全部spirv-val Vulkan1.1 exit0；preVS5937617587b19ce3fa7a093a0de249906fc5586f6ed1ae5db7c612ff85365dbc、colorVSf7f5e743abd002a9a3853bb8610483499393b358f8b92b6006a679e85a0a29a7，其余FS与逐文件SHA见dark-native-key-evidence.json/dark-native-audit.json。截图during-capture.png已看为活跃蓝白核/紫红外圈。首个主机audit Python3.9缺hashlib.file_digest原始AttributeError已保留，改流式SHA后重跑通过；无额度错误。

新原运行trace12508482890B，设备SHA9ea6d528ce031d7e4afaaa72d2e067ff76ac7ab516baeac7b9943f4b60dbefd0，layer日志02:34:35 Finished recording；正在唯一一次pull至full-native-capture/original-dark.gfxr，session58781/hostpullPID19892，transfer-plan/pull.log已落盘，未完成前不解析成正式证据。新capture/auditor代码未再改游戏行为/音频/11XGS。下一步同进程正常第二战Thunder→Limit教程，E独立抓球体；然后复用原1d30重放包，核实际API模块/资源世代/输出，不重建导出器、不重复下载旧trace。

CAPTURE_COMPLETE=NO（Dark native读回已PASS，实际API对应/未扰动replay定义区域一致性仍待验；Limit native待捕），全VIEW_VALIDITY=NOT_FIXED；FIRST_WRONG既有三处GPU结论不变；VISUAL_DARK/LIMIT此前普通2a4有限PASS，不把本诊断截图代替普通验收；AUDIO依用户NOT_RUN，常设NONE(enum10)/burst缓冲/2026-11-08T02:40:14.030200+08:00OUT重试/变更重开/fence0未改。60/400Pro/独立QA仍未过，普通2a4恢复点保留。唯一writer继续。

# Honor 同draw完整捕获实现已构建，开始验证 — 2026-11-08T02:28:57.037073+08:00

沿原xbg_probe扩展Honor显式D/E，首个精确暗魔法或Limit球体pre/color各一次、最多2draw/256MiB。捕获最终实际UBO/SSBO/TBO的stage/binding/element/offset/range，实际VB/IB/indirect、全部已绑定image view可达mip/layer/aspect（BC1–7块布局，D/S分面），attachment before/after；独立GPU readback所有者跨slot保持，只在present安全点fence/invalidate/落盘。默认marker0不录额外GPUcopy，D/E明确清所有ALWAYS/cull/空FS/prepass写禁用。保留目标8module最终vkCreateShaderModule实参words并记录pipeline/动态/布局状态；实际pipeline创建与资源世代仍须同帧GFXR校核，不以当前effective key替代实际cache命中证据。渲染view range未扩大；诊断copy padding必须在证明的VkBuffer对象范围内，allocation_size不是buffer上界。未知格式/样本/资源、缺pair或I/O错显式FULL_MISSING/完整性否。

新源xbg_full_capture.inc/xbg_capture_layout.h及既有cpp/probe限域接入，host raw布局/溢出/小范围padding契约PASS、git diff --check通过；完整普通游戏构建两次exit0（第二次仅补attachmentCount/正确日志），最终base56e62bfe12d22cdc982f0cb00d842d18150011f7d671ad46b0ad26f9a9ed8982。同base14非签名条目逐字保持，只加入原61abd21138b02fae5c1eabef36c9cbd79c2b8737acd0b007f9cf129e4e16e28a捕获layer，APK769d1d28c0d36b984a94541e51f846541f99bddc52a9fe4f26b7e0e05429a5df/71812586B；未重建或下载导出器。build命令/源SHA/日志/exit/prepare索引在dark-shader-retranslate-20261107/postbaseline-20261108/full-native-capture/。严格落盘auditor scripts/graphics/audit_honor_full_capture.py已实现，尚未真机通过。先做未命中负控，再活跃Dark/Limit定向帧，与既有重放包做定义区域对照；不能只靠文件都有称完整。

装前实时PID24343全屏普通野外已截图确认，用户最新无限设备授权；install-plan记录769d入场，仅org.ffrs.gamewindow install-r/冷启、app-only layer、3帧触发false。device ONLY A42BBB5109000333，数据/存档/11XGS/音频常设门不变，restore普通2a4已保留。CAPTURE_COMPLETE=NO，VIEW_VALIDITY=NOT_FIXED全范围，FIRST_WRONG已有GPU定位不变，VISUAL_DARK/LIMIT/BOSS既有有限PASS未外推新构建；AUDIO依用户NOT_RUN，NONE(enum10)/burst缓冲/2026-11-08T02:28:57.037073+08:00OUT重试/变更重开/fence0未改。60未达、400Pro实机/独立QA未验。唯一writer继续，下一步完成capture负控/活跃帧，不能提前打整刀PASS。

# Honor ADPF负对照结束；原包/生产源码已恢复 — 2026-11-08T01:48:43.369145+08:00

同一候选65f3d6018e2f3435c66651f5eb23ae50d6635284586c9e0bd5088b8edf879876（3736978B）已正常构建/保留数据install-r。实际官方API session创建成功，RHIThread TID23002一线程组、target16666666ns，report无错误。但同场每段约45秒、无录屏/输入的关→开→关分别51.917712 / 52.033621 / 52.129284独立新帧/s，无重复/失败/跳过；没有可辨识的该提示入口收益，不作为修复。取时只涵盖firstsubmit到pace前，未含之后GPU等待，故只否定本挂接方式，不能外推所有ADPF实现。host生命周期与实际NDK33签名通过；实际app仍API28可选dlsym，无必需新API导入。首构建probe声明顺序错误已修，首次API28声明检查因33可用性失败也保留原始错误，非额度错误。

恢复原普通2a4c811d88eb0271fd1c5039bc9a98eaac2c5d95f95499f8f22787b1a1108201后，同Continue野外45.706070138秒、1741独立新帧，38.069342fps，p50 27.681927ms/p95 31.425937/p99 33.917761/max53.803697，无丢弃卡顿；仍不达60。恢复包实际/data/app/base.apk SHA已核对，PID24343，GameActivity实场全屏/PAD隐藏，无后台输入/录屏脚本。gfx-performance-hint已改名保留为gfx-performance-hint-aba-20261108（内容0），CPUprofile旧marker也仅备份，probe0/target60/defer0/fence0。未改11XGS、数据自动发现/存档/其它包/全局显示或电源设置；冷启因系统freeform旧bounds，仅操作本应用Task1013和自己caption恢复全屏。

候选关闭状态约52与原包35–38的跨进程差异未解释：观察到充电图标变化/后来AC+USBtrue，候选结束prime频率2803200、原包RHI在big核且prime480000；这些是调度/条件线索，未证明因果，不把不同窗口生命周期/CPU落点夸成代码提速。两包draw约407.2/dispatch93一致，GPU约13ms，CPU提交候选约10ms/原包约16ms。CSV按mod32分组显示诊断采样帧平均额外约1.5ms/32帧，只是小开销，不虚称是35→60主因；未再为此堆APK。

生产CPP只反向应用本刀自有ADPF patch，未reset/clean/stash或覆盖WIP；新header/test移入自有源码档案coordination/experiments/honor-adpf-20261108/，含README、可独立重跑的host契约及拒绝的integration patch。恢复后27个原source-build-manifest记录摘要全部匹配，git无生产src变更。APK65与完整原始源码/失败/CSV/日志仍在上述adpf-trial目录，不重打graphics tag/不替换已交付APK/dataZIP。图形基线commit214fd79、tag honor-graphics-20261107保持。

CAPTURE_COMPLETE=NO、VIEW_VALIDITY=NOT_FIXED全范围、FIRST_WRONG此前三处GPU结论不变；VISUAL_DARK/LIMIT/BOSS有限自测PASS，新增guard/fire/Bahamut有限PASS，shield魔法/最初短闪全闭合/独立QA/400Pro实测仍未完成。AUDIO依用户NOT_RUN；验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md，NONE(enum10)/fence0/burst缓冲/TIMEOUT重试/变更重开均未改，不能整刀PASS。下一项回到原运行同draw完整附件缺口，已有22附件88文件紧密行/分配尾审计结果可复用，不重建导出器或重下载旧大捕获。唯一writer继续。

# Honor 稳定实场仍35.22fps；已有CPU计时完成并恢复普通状态 — 2026-11-08T01:09:42.038814+08:00

同普通APK2a4/source214fd79、marker0/target60/defer0/fence0，正常Continue同一野外、PAD隐藏。新45.996758629秒无录屏/无输入稳定窗口1621独立新帧，0重复/失败/跳过，35.219873fps；p50 28.20849ms/p95 31.279583/p99 32.945052/max37.122084。全部卡顿保留，结果cpu-profile/stable-result.json及原CSV/bootlog，不能用初进入场景/带计时器窗口约50fps替代暖场结论。

已有CPU计时6002..6031共30帧完成，描述符4.180031ms/帧、snapshot1.255060、draw1.357583、other2.636567；elapsed时钟不等于线程CPU利用率。CreateThread+SetThreadName把25124对应RHIThread，单次ps lifetime84.3%仅作为候选线索，不作瞬时利用率。run-as taskset只读查询实际Permission denied，未设置affinity或绕权限。拟以Android官方PerformanceHint的有开关候选继续区分稳态调度与工作量成本，尚未实现/验证，不能预报提升。

PID17815实际正常启动；第一次pidof1只是异步启动检查过早，不冒认崩溃。系统窗口模式已通过FFRS自身暴露的最大化按钮恢复全屏，未改其他包/全局模式，也没有证据断言另一writer抢占。CPU触发marker已改名保留为gfx-cpu-profile-measured-20261108，当前不再启用，PAD隐藏。无新install，基线APK/数据ZIP/tag不变；新增脚本仅离线分析，不进APK。

本阶段22附件/88文件raw布局审计和防御/爆炎/召唤有限自测见下一条。CAPTURE_COMPLETE=NO、全VIEW_VALIDITY=NOT_FIXED、既有FIRST_WRONG结论及Dark/Limit/Boss有限PASS不变；shield魔法/独立QA/400Pro实机仍NOT_RUN。AUDIO依用户NOT_RUN，常设NONE(enum10)/fence0/缓冲/timeout重试/重开流保持，整刀未PASS，60仍开放。唯一writer继续，没有原始额度错误。

# Honor 追加实战回归与实际导出器离线审计 — 2026-11-08T00:59:52.768136+08:00

源码基线214fd794fb436de14155e8954908685ae2120c21/tag honor-graphics-20261107与普通APK2a4c811d88eb0271fd1c5039bc9a98eaac2c5d95f95499f8f22787b1a1108201不变；本阶段未装包/改生产渲染/声音/存档，唯一writer继续。新证据在 `.local/honor-device-profiles/flame-20261104/dark-shader-retranslate-20261107/postbaseline-20261108/`。

同普通marker0/target60/defer0实战新增有限自测：Defend确认后受击197、HP5199→5002；独立爆炎橙金火焰与5718伤害；巴哈姆特召唤蓝白光束/紫色碎片/第一次协同攻击可见，未见旧大黑条。各录像大小/SHA/检查时段在boss-battle/visual-result.json。防御动作不等于护盾魔法（当前菜单没有后者，仍NOT_RUN），召唤无Switch原版独立对照，不作全战/全游戏PASS。首150秒boss-battle.mp4仅对话/入战，实际防御在shield.mp4；followup/final-move两片仅到后续对话/召唤教程，真正召唤在summon.mp4，避免错报相位。最初入第一幕短闪仍未全闭合。

离线直接解出现有replay-ubo.apk（SHA1d30b246...、40,732,155B）的libgfxrecon-replay.so（167,629,824B，SHA20698afc9264251ec00699fe826b57c19187c4b6cbdd447eff7a7d6498e0cccf），没有重建/下载捕获工具。实际ARM64 CopyImageBuffer0x1c0277c/780将rowLength/imageHeight置0后push区域，核实紧密行；GetImageResourceSizesOptimal使用临时image分配尺寸作为子资源步距，额外文件尾不是像素。actual DumpImageToFile0x15fd888/894/898已有raw-stencil条件，区别于上游无条件skip；先前“现用导出器漏stencil”的临时推断已被二进制和各组8个真实stencil文件否定，不能沿用。

新离线只读脚本scripts/graphics/audit_gfxr_attachment_prefix.py对同trace原VS重放original-gpu与双方重译both-full-gpu核22个附件身份、88个文件，排除分配尾部。color1816568从0写出恢复5259个像素单元，color1818253从0恢复145165；前者HDR有效区域NaN0/Inf0/max11704。1384×780×4逻辑长度4318080，文件4444160的126080尾字节不参与比较。D24只按存储单元统计，未把未用bit或未初始化源区域当数值深度证明。raw-layout-audit/attachment-prefixes.json、binary-layout-contract.json、实际反汇编/命令与失败的symbol-only首尝试均保留。原始捕获未改，两个对照均是重放输出，不能冒充原运行独立附件。

CAPTURE_COMPLETE=NO（原运行独立before/after、全别名/写世代和VK_NOT_READY差异仍未闭合；本次关闭的是这些文件的raw行距/尾部疑点）。VIEW_VALIDITY=NOT_FIXED全范围；FIRST_WRONG保留此前Dark/Limit丢失buffer读取→VS Position，Boss精确CS共享归约重复计数证据，不引入新根因。VISUAL_DARK/LIMIT/BOSS原有限PASS保留；GUARD/FIRE/SUMMON本新增范围有限PASS，shield魔法/独立QA/400Pro实机NOT_RUN。AUDIO用户明确NOT_RUN；NONE(enum10)、fence0、burst缓冲、TIMEOUT重试/变更重开未动，不报整刀PASS。

60仍未达标（前次暖场35.27487）；本阶段用现有CPU计时入口6000/30，仅同APK冷启/Continue，不跳同步或重建工具。首次立即pidof返回1并不代表崩溃，后来PID17815活着；冷启进入系统窗口模式被其他窗口盖住，amstart仅投递existing task未改模式，已只点击FFRS自己暴露的最大化按钮恢复全屏，未操作其他包。标题Continue已实际查看并正常确认、PAD隐藏；有效实场计时结果仍待读取，不用遮盖/加载/菜单窗作FPS。

# Honor 图形修复基线已提交，APK/数据/手册已制作 — 2026-11-08T00:06:40.658359+08:00

代码基线已实际本地提交 `214fd794fb436de14155e8954908685ae2120c21`（64个显式文件，包含已认领的Honor dirty依赖与本刀修复）；本地annotated tag `honor-graphics-20261107` 指向同提交。未push、未切分支/新树、未reset/clean/stash、未碰RG工作树/设备。源码检查通过；原Markdown双空格换行保持，未为消除格式检查改写任务提词。安装APK仍精确为2a4c811d88eb0271fd1c5039bc9a98eaac2c5d95f95499f8f22787b1a1108201，提交前后所有实际build输入源码SHA保持一致。

私有可安装交付：`.local/honor-delivery-20261107/release/FFRS-Honor-1.0.1.apk`、`FFRS-data.zip`（16918705179字节，SHA01a338c52b79869c04f486eb1d42ce926fa6935d28d19371ca0a7c6cd10411af）、`安装说明.md`、清单/校验文件/licenses。ZIP40547文件CRC及逐文件SHA全部回读，40536设备原件摘要匹配+11修复件一致；不含存档/密钥/诊断缓存。本树staging可用于重建，勿自动清理正在使用的产物。手册建议电脑解压后将整个FFRS复制手机内部存储根目录，避免FFRS/FFRS双层；直接覆盖安装保留数据。

最终同APK普通marker0/target60/defer0三项视觉有限自测PASS：Dark40秒、Limit70秒完整施法/结算/野外，Boss110秒全出场+67-75秒10Hz检查；证据final-trial/visual-result.json及三录像，实际安装base.apk SHA一致、最终触控PAD隐藏截图已查看。设备唯一A42BBB5109000333，org.ffrs.gamewindow GameActivity/PID28429留在Boss对话；无后台输入/捕获脚本。原FS、DarkEQUAL及各Limit实际EQUAL/GEQUAL、预通道均保留。

验收字段：CAPTURE_COMPLETE=NO（原运行独立完整附件/别名世代等既有缺项仍保留，不能将范围内GPU首错证明夸成全量独立捕获）；VIEW_VALIDITY=NOT_FIXED全范围（具体目标合法类型事实已校核，不盲扩range）。FIRST_WRONG=Dark/Limit VS丢失buffer读取导致Position错误；Boss精确CS64-lane共享归约重复计数，详见前述GPU数值与FIRST-WRONG文件。VISUAL_DARK=PASS、VISUAL_LIMIT=PASS、VISUAL_BOSS=PASS，仅本流程实现者自测；shield/原开场短闪闭合/全游戏/独立QA未完成。AUDIO=NOT_RUN（用户明确暂不能听），AAudio NONE(enum10)、fence0/缓冲/超时重试/设备重开保持。Honor400Pro=NOT_RUN，配置支持不能替代手机验收。

**整体验收未通过，60目标仍开放**：暖实场35.27487独立新帧/s，defer2负对照34.966且事件0，已恢复默认。后续不要重复下载/重建捕获工具、重做已闭合图形试验或把标题/刷新率当60；从CPU/GPU串行等待和真实场景受控性能定位继续，保留本tag及私有成品作为恢复点。现有所有者仍是本线程唯一writer，不允许另起writer抢树。没有实际额度错误，不虚构额度耗尽。

# Honor 最终普通包三项视觉自测通过；基线与私有交付收口 — 2026-11-08T00:00:27.124604+08:00

最终普通2a4/实际安装base.apk SHA已逐字核对，PID28429，probe0/target60/defer0/fence0/无capturelayer。final-trial/dark.mp4（40秒15,297,090B）与limit.mp4（70秒26,109,131B）完整选单→施法→结算→野外；实际检查Dark5-15s每0.5秒（2Hz），Limit0-34s2Hz：蓝白暗核/紫红外圈、蓝冰晶、雷电、金白火尾爆发、最终光束恢复。final-trial/boss-final/boss.mp4（110秒33,183,386B）+67-75s10Hz完整落地（实际68.2-70.2s），棕石/尘正常、旧满屏长黑条无，角色/Boss/背景火/对话完整。VISUAL_DARK/LIMIT/BOSS=PASS仅本教程/出场自测，非独立QA或整游戏。最终三录像SHA及身份在final-trial/visual-result.json。

实际日志六新VS均integer rewrite1、compaction patched1、ae83752000words；没有NVN spv override/PROBE_DIAG/PROBE_BEGIN，原FS/深度/预通道保留。触控PAD已恢复隐藏（最终截图待随检查记录），11 naturalXGS仍保留，未部署任何replay remap/终端探针。AUDIO按用户NOT_RUN，NONE enum10/1443burst/2886capacity和fence0日志一致，缓冲/timeout重试/设备重开源未改。shield最终版本NOT_RUN，最初开场短闪未独立闭合，不能冒称所有闪烁均解决。

本地数据交付已全核：ZIP16918705179B/SHA01a338c52b79869c04f486eb1d42ce926fa6935d28d19371ca0a7c6cd10411af、40547文件；40536设备原件SHA与包完全相同+11修复件一致，ZIP全CRC/SHA回读通过。APK/数据/安装说明/licenses在.local/honor-delivery-20261107/release；400Pro SM8650/750目标60支持已实现，手机实测NOT_RUN。CAPTURE_COMPLETE=NO（原运行独立完整附件/别名世代、部分rawpitch定义区域及VK_NOT_READY差异仍未全闭合；同帧目标GPU输入/实际shader和Position、CS首错证明保留），VIEW_VALIDITY全NOT_FIXED，具体目标类型合规不能外推全游戏。

60目标未达标，普通暖野外35.27487 distinct frames/s；defer2控制34.966且事件0已否定并归档/恢复default0。图形基线将保存当前认领source/WIP及自有测试，代码47文件已显式暂存，无游戏/APK/key/.local。BASELINE-HONOR-20261107.md/本地tag拟honor-graphics-20261107，不push。完成提交后补真实commit/tag；60性能仍开放，不能给整刀PASS。

# Honor Boss出场复验通过；defer2提速被否定并恢复 — 2026-11-07T23:39:32.752091+08:00

普通1f90/60目标Boss同路线110秒录像34,264,479B，实际检查55-95sec1Hz与69-73sec10Hz完整落地扬尘，旧长黑条/斜条均无，棕石/尘、角色/Boss/火焰/对话完整。VISUAL_BOSS=PASS仅本出场自测，非整战/独立QA；visual-result.json保留SHA。runtime compaction patched1，ae83752000words/UInt1。

已有defer-events=2受控负实验（2a4新APK仅增加750精确hist/interface守卫、735行为不变）：同Continue/同野外44.6146s，34.966fps，对照1f90/defer0=35.275fps，且实际deferred events=0，此路径未使用，否定为提速方向；未修改source default。原来无marker，已将试验文件改名保留为gfx-defer-events-rejected-20261107并冷启恢复默认defer0。不能按标题60fps外推实场。

最新普通候选2a4c811d88eb0271fd1c5039bc9a98eaac2c5d95f95499f8f22787b1a1108201（3732882B）buildexit0/install-r，当前PID28429，开始final-trial全教程Dark/Limit视觉复验。新手机750只复用exact-module+selectedprofile guards（inline/hist/interface），仍不外推735depthreuse特殊门；400Pro实机仍NOT_RUN。dataZIP完整已校验；本地baseline/commit仍待本最终候选视觉复验完成，60仍未达标，audio依用户NOT_RUN。

# Honor 数据包与原件全量摘要一致；交付候选60实测35.27fps — 2026-11-07T23:27:48.150458+08:00

数据ZIP已完成全量回读：40547文件、16911082585B payload、16918705179B ZIP，SHA01a338c52b79869c04f486eb1d42ce926fa6935d28d19371ca0a7c6cd10411af。所有40536原始main/sdk/romfs/archive文件SHA与平板只读原件完全相同，额外11修复XGS逐一匹配已部署摘要；成品ZIP CRC/每文件SHA通过。初次find -exec +实际报Argument list too long/exit1，保存原错误与3864有效摘要，改按32文件argv只补剩余36672，成功，无重复整包下载。归档73495条目排序/连续offset/extent/EOF检查通过；不据此称所有shader语义PASS。

普通候选1f90已冷启，包名/数据/存档路径不变；原gfx-shader-archive marker改名保留，未给marker仍正常进入标题与同Continue野外。旧主shaders存在，所以这只验证自动选择与普通启动，不能谎称已经强制所有shader走archive。设备PID15649/marker0/target60/defer0/fence0、无capturelayer。暖野外44.224117s，1561独立工作帧、0重复/失败/跳过，35.274870fps，p50 28.133646ms/p95 31.326302/p99 33.349584/max37.351667；仍非60。已开始110秒同路线普通Boss出场录像（portable-trial/boss-baseline/plan.json，60目标，录像仅视觉不是干净性能）。

APK+数据+手册+licenses已在私有.local/honor-delivery-20261107/release形成可审阅候选；源码本地baseline/commit/tag尚未执行，60/音频/400Pro未通过，不能称完整交付PASS。暗魔法与完整Limit此前2c13普通marker0有限PASS。继续该录像和既有defer2受控性能实验，不改时钟/跳draw/删同步冒充60。

# Honor 60目标实测32.98fps；可移植候选构建/数据复制中 — 2026-11-07T23:15:11.032989+08:00

普通2c13/marker0/target60暖野外45.481859s，无screenrecord/输入：1501成功呈现、1501独立writer帧、0重复/失败/跳过，32.980182新帧/s；p50 30.100989ms、p95 33.010521、p99 35.358124、max47.361302，>100ms=0。fps60-field/result.json；初次/proc/uptime窗口与CLOCK_MONOTONIC不一致已明确INVALID_CLOCK_WINDOW，不使用其零行结果。当前60目标仍未达标，日志CPU约17.6ms、GPU16.1ms且约10提交/等待，继续有据性能对照，不能将刷新率或配置当PASS。

交付候选1f90c079784f3bdad1fe26b6e6750aae793b8b0e540604de754e4411595b46d0，3732882B，versionCode2/1.0.1-honor，构建exit0，install-r保留数据已成功。新增SM8650手机profile target60/1080字段+opaque/docked选择；helper inline仅增加Adreno750精确名，735深度特殊门未外推；400Pro仍NOT_RUN。JNI无marker时自动选旁边非空正规shaders.hnla，显式0/1优先且原loose overrides优先，现有数据发现/存档路径不变。三profile host环境用例、Java选择、archive选择host真实文件边界测试均通过；不代表新手机实测。

.local/honor-delivery-20261107/copy-plan.json/session88019只读复制自本平板既有FFRS main/sdk/romfs/archive和11修复XGS，未读取RG/旧禁区、未带存档/缓存/密钥。当前应用停在安装后的停止状态，等待复制完成再启动/回归；目标60marker已保留。新私有ZIP64打包脚本会核对最终ZIP CRC和全部SHA。手册coordination/docs/HONOR-400PRO-INSTALL.md已起草，尚未打baseline/commit/tag，数据ZIP尚未完成，不能报交付完成。

CAPTURE_COMPLETE=NO/全VIEW_VALIDITY=NOT_FIXED维持此前缺口；本教程暗魔法及完整Limit有限自测PASS（2c13，原FS/实际EQUAL或GEQUAL，不借探针），Boss9656有限PASS待1f90复验。AUDIO按用户NOT_RUN，AAudio NONE/fencespin0/缓冲/timeout重试与重开流未改。无原始额度错误。

# Honor 普通包暗魔法/Limit 完整施法有限自测通过 — 2026-11-07T23:03:25.613553+08:00

普通2c13包/marker0/30对照已完成暗魔法及70秒Limit完整施法。实际查看0-34秒0.5秒间隔联系表，蓝冰晶、雷电、火焰、最终光束/结算正常，15.5-18秒黑星恢复金白爆发。VISUAL_DARK=PASS、VISUAL_LIMIT=PASS仅本教程结构颜色自测；独立QA/AUDIO仍NOT_RUN。证据limit-red-tail-20261107-2130/ordinary-trial/visual-result.json和两录像。继续60实际帧率与Boss回归、400Pro可移植交付。

# Honor Limit 火尾首错定位：两端 VS 失读，普通候选待全流程复验 — 2026-11-07T22:46:49.178007+08:00

同frame10983的合法类型旧VS与原始Maxwell正式重译控制，draw1816568/VS85e1/module1385，vertex0/instance0直接GPU终端Position探针得到 x/y/w 从(83.359802,332.965332,3137.695801)变为(-184.474213,2768.721680,3489.950684)。两次marker C0DEF00D均有效，binding10有效range恰16字节；只写x/y/w+marker，Z未测。原FS保留，写入与VS输入/VB/IB/C0不重叠；探针FS输入受扰动，绝不作为普通画面验收。唯一诊断trace副本仅B10 stage16->17，host/device期望SHA d2fb1d9497526fe7e7b63e030be906b7a24185923850069261710c2dd31120b7一致，原捕获d45df785...未改。完整FIRST-WRONG-LIMIT-TAIL.json。

三对原Maxwell/control/content hash核对、C1与GPU一致：pre704f01d7...、color36091cc0...。36个texel有效范围和86项按binding/offset/range的UBO/SSBO核对，三pair VB/IB逐文件相同。类型only六VS无导出重放逐像素不变；仅重译三个color仍不变；仅重译pre形成更大的黑爆发；两端全部重译后恢复强亮金白爆发（原FS/EQUAL、预通道保留）。color1816568/1818253附件原本不变，现发生真实字节变化；第一组480x272逻辑前缀5259变化、HDR有限最大11704/8488/5872。其他raw图像pitch/精确像素坐标未全部审计，不能将前缀统计夸称完整像素证明。静态fetch数量color额外9/pre12个slot128浮点读取；非运行次数。完整普通Limit仍待验收，强亮是否符合完整过渡尚待录像。

源码只新增6个精确key的B12 R32_UINT事实，B18保留R16_SFLOAT/range2，不盲扩range；natural六XGS与原五个覆盖件将一起部署，绝不部署replay remap/位置探针。新普通APK2c13a8ea318c2086399ec29aec8d6d9a203749b2da0caa8965cbe729bf35d9fb（3,732,882字节）build exit0，deploy_tail_candidate.py保留数据install-r正在执行，六key旧warm记录有备份且仅过滤这六key，drivercache保留。marker0/30仅因果对照；60最终目标未验收。AUDIO依用户NOT_RUN，NONE/0us源未改。CAPTURE_COMPLETE仍NO/全VIEW_VALIDITY NOT_FIXED；Dark/Boss有限自测PASS；Limit普通包仍不能PASS。之后全流程视觉→60实测→baseline/commit/荣耀400Pro APK数据手册。

# Honor Limit 黑尾同帧捕获与新候选负控制 — 2026-11-07T22:13:04.885110+08:00

新一帧活跃火焰收尾捕获（frame10983，6,846,997,428字节，SHA d45df78508e08a7cf989928e647f3c554849d4c2563a5ba4cab3463beb25c92b）设备/主机一致，1,819,727 blocks精确EOF；捕获原库61abd，captureAPK b4082284... 与普通9656的14个非签名条目逐字相同，仅加入原layer。Finished recording已确认。phase1.png/no-export replay再次显示黑色星状条片，非暂停错相位。目录 dark-shader-retranslate-20261107/limit-red-tail-20261107-2130。

候选opaque pair已核实际 pre GEQUAL/write、color EQUAL/no-write，不能套原Limit632c GEQUAL。4draw导出283文件；24项texel有效range及57项UBO/SSBO按binding+offset+range核对；两pair本帧native-slot输入完全相同，C1=36091cc0...。两color原输出六附件均0，B12uint-only不改变结果。三opaqueVS85e1/55703/92bc使用原始Maxwell和原translator重译成功，但同条件无导出重放黑尾仍在（像素变化数0）；尚非已定位首错/修复。带导出新VS截图失去黑尾时也失去金色条纹/模糊，不能当通过，原FS/深度未改。候选未进入应用源表或设备正式shaders。

继续本帧稍晚translucent draw a6f544/cc083附件定位，不根据opaque颜色零直接冒认根因。CAPTURE_COMPLETE=NO（原运行独立完整输出/世代缺口仍在），VIEW_VALIDITY全范围NOT_FIXED，FIRST_WRONG尾迹NOT_FOUND，VISUAL_LIMIT=FAIL；此前普通Dark/Boss有限自测PASS保留。AUDIO用户NOT_RUN；60、baseline/commit/APK/data/manual待后续完成。设备当前既有replayAPK1d30，普通9656恢复包和五XGS保留。

# Honor Limit 全流程复验仍有黑色尾迹；下一次仅捕获该相位 — 2026-11-07T21:30:52.985529+08:00

当前普通9656包70秒完整Limit录像已保存（24,503,016字节，SHA0dccb6615055dd1cc289029ab615f43d55473fd44b6c700ec3bd89cfee0eb226），覆盖施法、冰晶、雷电、火焰、最终光束、敌人结算与野外返回。已查看0–32秒2Hz联系表：蓝冰晶和主火焰恢复，约15.5–18秒火焰收尾仍有黑色条片，VISUAL_LIMIT=FAIL。不能把compaction修复对铁巨人有效外推为Limit通过。暗魔法/铁巨人有限自测PASS保留。

下一步复用既有捕获layer，只把同一普通9656APK加入原61abd库；不重建/下载工具。新证据目录 limit-red-tail-20261107-2130，目标为Limit第一次确认后约14秒的活跃黑尾相位，一帧捕获。原15GB boss trace和导出全部保留。普通9656恢复包、五XGS原样保留；安装-r/正常NewGame UI继续，禁存档文件修改/清数据。

CAPTURE_COMPLETE=NO；FIRST_WRONG_LIMIT尾迹尚未定位；完整VIEW_VALIDITY未闭合；AUDIO按用户NOT_RUN；60Hz/提交/基线/换机交付仍待完成。唯一writer继续。

# Honor 暗魔法普通包复验通过；Limit 全流程复验中 — 2026-11-07T21:22:29.322692+08:00

普通包 APK `9656b203...`、五个已核对的 XGS、marker0、30fps 对照条件不变。`ordinary-candidate/dark.mp4`（40秒，14,921,729字节，SHA记录在 dark-visual-result.json）完整覆盖选单、暗魔法施放、三名敌人结算、回到野外。已查看1Hz联系表：蓝白亮核与紫红外圈恢复；VISUAL_DARK=PASS（本教程结构/颜色自测范围），独立QA未运行。原FS/EQUAL、预通道保留。

铁巨人出场已在同一普通包通过前述有限自测。下一步仍是 Limit 全阶段到结算，随后60Hz实测和荣耀400Pro交付。音频主观验收依用户要求 NOT_RUN，不能报整刀PASS。当前普通NewGame教程第二场流程，未修改/恢复存档文件。

# Honor ordinary marker0 Iron Giant entry visual self-test PASS - 2026-11-07T21:07:40.386226+08:00

APK9656... +5verifiedXGS includingae154173..., PID7300 atstart. Actual ordinaryGameActivity/noVulkanlayer/probe0/30target, sourcecompactionruntimepatched1; aeactual52000words/UINTrewrite1. SameContinue+scriptedroute movie109.952756sec/30437670B, ordinary-candidate/boss-visual-result.json SHA. Actual bossentry75-82s reviewed10Hz contact +68-92s2Hz contact and impactframe; originalmassiveblackvertical/diagonalbarsabsent, brownrockdebris restored, boss/flames/player/dialogueintact. VISUAL_BOSS=PASS for this full entry self-test only; independentQA/wholebossbattle notclaimed. NoNaNclip/countcap/capturelayer/ALWAYS/FSreplacement inordinaryAPK.

Stilltarget30forcausalcomparison; userfinal60goalunverifiedandpending. Nextsameordinarybuild fullLimitallphasesandDark/shield/occlusion; mayuseordinaryNewGameUI aspriorauthorizedtutorial capture, no savefileediting/data clear. Currentstateordinarybossdialogue +touchPADshown(originallyhidden), fiveprimaryoverrides retained. FinalrestorePADhiddenlater. No localcommit/tag/package/data/manual yet.

Audio source stillAAudioStreamBuilder_setPerformanceMode(AAUDIO_PERFORMANCE_MODE_NONE), fencespinruntime0us, burst1443/buffer2886. ActualNDK26headerAAUDIO_PERFORMANCE_MODE_NONE=10 (AAudio.h290); rawlogperformance10thereforematchesNONE. Standingfile numericexample performance0/10 contradictsNDKenum; do notforceinvalid0ormislabelasLOW_LATENCY. Noaudiochange; subjectiveA2DPfield/battle/reconnect NOT_RUN perexplicituser. Fullstandinggateunpassed, nooverallPASS.

CAPTURE_COMPLETE=NO aspriororiginal-run/generationgap; VIEW_VALIDITYfullscopeNOT_FIXED (targetAe/CS factslegal); FIRST_WRONG_BOSSconfirmedCSsharedsum; VISUAL_DARKhistoricaltutoriallimitedPASS, VISUAL_LIMITpriorFAIL/newbuildpending; AUDIOuserNOT_RUN. Solewriter/HEAD032e5f6/dirtypreserved, source2existingfiles+newheader/test thismilestone; goalactive.

# Honor first wrong GPU output found and source candidate built - 2026-11-07T21:02:04.860145+08:00

FIRST_WRONG_BOSS=CS b673e10f3da2451c shared workgroup reduction. Direct GPU probe retains all original arithmetic/control, only writes diagnostic counters in unusedtail words1024..1215 after existingbarrier. Single-group example dispatch474320 actualGPU C3 max7, lane values sum7, two32-lane warp sums7/0, butsharedvalue14 readbyall64threads. Baselinewithoutprobe downstreamcounter14same, so not NaN/fragment/depthfix. First16one-particleworkgroups similarlylaneSum1/warp1,0/shared2. Two-group rock dispatch474299 diagnostic tailwaswrittenbybothgroups, so per-group arrays thereareRACED/NOT_VALID; do notsumits64tailrecords aswhole100particles. GPUC3 recoveredcorrectlybyreversible same-frame UBOalias B48offset745472/368B; exact99compressedbytesrestoredafterread, originaltrace untouched. Fullproof FIRST-WRONG-BOSS-COMPACTION.json.

Controlled repair: only replace sharedatomic with eachwarp leader storingits partial in existingshared[rawSubgroupLane>>5], retainoriginalbarrier, finalread partial0+partial1. Existinguint[4] hasroom; bothleaderswriteincludingzero, noinitializationgap. Oldatomicreturndeadlocalonly. NOinstancecap/discard/ALWAYS/prepassremoval/emptyFS/waitIdlefix. GPUcounter17/indirectrock132->100; all19counter/indirectrecords corrected; particlefloat,preFloat,aliveindices eachbit-identical (SHA40d067.../99b11d...). Predepth29675->9542pixels bbox263,349-1095,467, twohugevertical/diagonalbarsgone (shared-sum-pre-depth-change.png actuallyviewed). ColorVSae837 retranslation separatelyneededtofixblackdebris; type-onlyB12wasinsufficient.

Newproductionadapter src/graphics/nvn_vulkan/engine/xbg_compaction_sum.h checks exact32684words/bound7554/local64x1x1/shareduint[4]/atomic/load/unusedreturn, rejectsunfamiliar/already-patched modules. RunsAFTERwarp32 in BOTHwarm/runtime, onlyQualcommvendor5143/subgroup64/exactkeyb673; noRGtreechange. tests/device/test_compaction_sum.cpp fixturetest recoversoriginalbyteidenticallybystrippingonlytwoedits, checksalready-patched/wrongbound/changedatomic rejection/no-mutation; hosttestexit0, generated130848B C++SPIRV spirv-val1.1exit0. Controlled3frame5752-5754 replayusesexactC++output+newaeVS+SINTsimulationviewfix, finishes3/3; phase1/phase3 actuallyviewed, solidsolidbarsgone/brownstonesrestored, somefirst-framehistoryghostsremain; ordinarycoldcleanroute pending, nofullvisualPASS.

SourcealsoaddsONLYae837B12UINT andcb997B29fetch/B47storageSINT nativeviewfacts. Actualcb997typecontrol3fetches/3stores keptcounter/float/alivebytesidentical, so legaltypingnotclaimedrootcause. ExistingDark/Limit4XGS retained. NewaeXGS1541730... naturalmetadata, NOT replayremappedSPV deployed. Originalb673 sourceXGS kept; noCSretranslationdeployment. Hostreproducedoldpipeline integer+helperinline+warp32 byte-identicaltocapturedactual22142 (compaction-prepare-result.json); newCSretranslationonlyaddsatomicbounds anddidnotchangeGPUcounter, rejectedasfix. NaNclip/integerShuffle/finitePhi notsourcefixes.

Ordinary candidate buildexit0, APK9656b203780bd7f1835bb86b3cb41b1f069298fa0b983d1205b9ad152db16f24,3732882B, apk/honor-compaction-debris.apk; plan/log/exit+sourcefixturesSHA retained. install-r preservingdata completed, newaeprimaryinstalled/sha verifiedfivebundles; stalegraphicswarmrecordsonlyae removed withbackup, allothersbyte-identical, drivercacheunchanged. Deployscript thenstoppedonmissingoptional gfx-fence-spin-us marker (default0, nofile, not audiofailure); fixedreadguardandresumedonlyremainingSHAchecks+GameActivitylaunch (session41114), NOTreinstalled. Currentnooverlayinputyet; awaitingtitle screenshot. Markerprobe0,target30causal; GPUdebug4null/capturepropsempty. SourceAAudioNONE/fencespin0unchanged. Normalcandidate nowinstalled, replaypackagealreadyrestoredtoordinarycandidate.

SameHEAD032e5f6/dirtypreserved, onlynewappsource3existingfiles/newheader+test; noactualcommits/tags/push. CAPTURE_COMPLETE=NO (independentoriginal-runfulloutput/remaininggeneration andcomputeUBOsallcoverage stillopen; oneC3closed); VIEW_VALIDITYfullscopeNOT_FIXED, targetnewfactslegal; FIRST_WRONGconfirmedasabove; VISUAL_DARKlimitedhistoricalPASS, VISUAL_BOSS/LIMITordinarycandidatePENDING(previousFAIL), AUDIOuserNOT_RUN. ContinueordinaryfullbossentrythenLimit/Dark/shield/fire/occlusion; final60realFPSandHonor400Probaseline/commit/APK/data/manualpending.

# Honor boss debris color repaired in replay; finite stretched instances and upstream SINT mismatch isolated - 2026-11-07T20:36:25.633571+08:00

Original authorized one translator restored exact.NET10.0.403 SDK SHA5125893c66b... (sdk-recovery-result.json), current private SDK path back; originaltranslator/bin executable retained. Experimental finite-Phi change had NO effect: original-buffer-facts,finite-no-facts,finite-buffer-facts ae837 XGS byte-identical SHA1541730bb1c759f78ca31028d4c202296abbb9622c8a6590027f0a9ee6b4480b. Reverted private BindlessElimination.cs to beforecandidate, archivedpatch/bin-finite as rejectedcontrol; no capture/replay tools rebuilt/downloaded. 085a pre stillrejectsunrepresenteddynamic32arrays, notstripped/deployed.

Exact own-device Maxwell ae837 group5430 recovered with12MBTOC+3.6MBUCASranges, existingOodle/index; old6GBcontainer not recopied. New ae837 XGS hasC1 exactlymatching same-draw GPU256B SHA182720ba..., native B12UINT typing plus replay-only B48floatalias->oldB24 andUBO54+shift mappedback. Shader288c6bf0... /originalFS833/GEQUAL writes29675 HDRpixels, matchingprecoverageincludingstretchedbars; all5nontrivial Gbuffers restored, depthunchanged. debris-retranslated-gpu-audit.json andhdr-comparison actuallyviewed. No formaldeployment yet; colorrepair is partial, blackflash notclosed.

TerminalGPUPositions: old B12-legal VS and newVS both132validmarkers,109finite,23NaNinstances107-129. Newchanges109finitepositions/restoresdebriscolor but NaNsunchanged. Vertex1newprobe identifies finiteextremeinstances130/131: (1977.917,323449.4375,10,9088.3018) / (106361.336,222034.2969,10,3469.1445), NDCy35.59/64.00. Vertex0ofthosewasordinary; vertex0alone insufficient. Sameframeall144meshverticesfinite/bounded. DiagnosticterminalNaNclipping pre832+newcolor831 DOES NOT removebars; nan-clip-phase1.png viewed, rejected asfix. Do not claim23NaNsasacceptedrootcause or applyglobalguard. Newfinite-Phi andNaNclipcontrols neverappsource.

TrackedGPUlineage: indirectpacketcopy474629 fromarena8564[3203328..] to576462; exactdraw340 packet144indices/132instances. Countslot17=132 incounter8564[3186944..] copied474612 from2072832. Aliveindices copied474601 from2040064. Producer overlap matrix: b673 dispatch474320 writesaliveindices B47SINT andcounterB53UINT; cb997 dispatch473949 writes particlefloat8564[422144..] atB41. All75livecompute module/resourceindices established; subsetmatrices live-compute-resource-matrices.json. C5 fields100 maybestride, NOT provenvalidparticlecount; do not cap132to100orhideinstances.

Crucial realtypefault: actualsimulationmodule575848/keycb997 nativeB29 R32_SINT99/range4096 andB47storage R32_SINT99/range4096, butbothSPIR-Vfloat. B17atomicUINT andB41floatalreadycorrect. Limitedactual-module control ONLYfetch29scalarSINT+store47SINT (3fetches+3stores), nootheroriginalcode/dispatch/depth/FS changes; candidate123012B SHAin simulation-sint/manifest.json, spirv-val1.1 pass. Reusesexistingreplay/capture, smallhostadapter calls existingrewrites. Running singlepredraw474865 export via run-simulation-sint.py session29296/log, nextpull/check indirectcount,predepthstripsandGPUinputs. Norootcausal/visualPASSclaimedbeforeoutcome. New rawMaxwellCSb673/cb997 recoveredonly128KBadditionalranges, bothretranslatebuilt with exact64x1x1/shared16/352 originalfacts; notGPUtested/deployed.

Computeexportdiscovery: oldreplaycompute path does NOT exporttexeloutputs and omitsdynamicoffsets forUBObufferfiles. Dispatch474620 outputonly3files, c3 reported16bytesarebufferstart garbage, NOTactualparams at801280. Do NOT consume thoseasGPUC3input orclaimCAPTURE_COMPLETE. Drawpath GPUbackingsandslicedUBOs remainvalidated; laterdraw8564 backing provescounter/particlebytesaftercompute. NeedpropercomputeC3 readback ifrequired, useexistingtools/limitedaliasdiagnostic orrawflushedcontextwithexplicitlimits; do notsilentlymix.

No newappsourcechanges this milestone, sameHEAD032e5f6/dirty. Device replay1d30, restoreordinaryea2f+hiddenoverlaypending. CAPTURE_COMPLETE=NO; VIEW_VALIDITYfullscopeNOT_FIXED(newsimulationSINTfault); FIRST_WRONG_BOSS: VS colorpathpartialrepair, finiteinstance130/131stretchupstreamcauseopen. VISUAL_DARKlimitedPASS; VISUAL_LIMIT/BOSS FAIL; AUDIO userNOTRUN. Final60/localcommit/baseline/APK/data/manualHonor400Propending. Goalactive, notcomplete.

# Honor Iron Giant debris draw isolated; actual color Position contains NaN - 2026-11-07T20:12:58.552353+08:00

Same writer/HEAD032e5f6/dirty preserved. Actual firstframe5752 pre474865 module832/834 writes29675 depth pixels, including the two long vertical/diagonal streaks absent before it (474865-depth-change.png). Its paired color475432 module831/833 shares exactly144indices/132instances and byte-identical1728BVB +288BIB. Native depth for BOTH is GEQUAL (6), prewrite1/colorwrite0; this new boss pair is NOT Dark EQUAL. Original color6attachments all0 changes. Smoke475828 sees the existing streak depth; bodyfe033 location9 hypothesis remains rejected.

Same-frame complete texel backing recovered from existing GPU export, no additional capture needed: pre/color B12 32768B bothSHA99b11d9a..., B18 2B both0000 SHA96a296..., B24 32768B bothSHA40d067e6...; all8192 B24 floats finite. Precise descriptorbuffer/offset/range/nativeformat in debris-pre/color-resource-matrix.json and debris-texel-gpu-validation.json. B12 native98 while original color831 float declaration is real type mismatch; B12-only legal control keepsall6attachments0. Format compliance is insufficient, no rootcause claim. Actual085a+existing12/24 rewrite equalscapturedmodule832 byteforbyte; actual6018+18/24/30 equalsmodule554; ae837 rawXGS equalsmodule831, keyidentity nowconfirmed notwordcountonly.

Added diagnostic terminal Position readback ONLY to B12-legal color831; 4 compatible setlayouts B10SSBO stage16->17 only in new traceclone, all other logical bytes identical. New clone15032076460B expectedhost=device SHAa18177e9c63fec998304deddd29fb6bb27944b1cdeddcec693133b6b66395011; original trace47e6 untouched. Position AFTER-draw output132/132 valid markers,109 finite,23 NaN instances107-129, vertex0. debris-color-position-result.json and exactSSBO binary/hash. Before-draw firstread is explicitlyINVALID_INPUT_ONLY, not GPUoutput; correctedafterrun recordsvalid132. This localizes invalid color output to VS Position before raster/FS/depth, but exact source arithmetic and earlierpre Position still open; do not claim completedfix.

ExistingSDK10.0.403 was ALSO removed byMiniMAX cleanup, initial build FileNotFoundError exactpath in translator-finite-build.log/plan. User says probablydeleted; applying prior explicit permission to prepare this one translator, recovering ONLY exactsameSDK URL/SHA512 in sdk-recovery-plan/log, session75326. No capture/replay tool rebuild/re-download. Existing translator/bin preserved; one privatefinitec2-Phi source candidate +newbin-finite planned, not runtimefix. New ae837 rawMaxwell group5430 recovered via existingTOC/index/Oodle fromown-device read-only: only12MBTOC+3.6MBUCAS byte ranges, no6GBcontainer copy. Originalpre Maxwell job/code reused; debris-retranslate-jobs/input.json. Source candidate finitephi preserves runtimehandle comparisons/readcoordinates/results; GPU validation pending. No appsource/performance/audio/save edits.

CAPTURE_COMPLETE=NO (original-run output independence, remaininggeneration/texturevalid-row audits open); VIEW_VALIDITY fullscopeNOT_FIXED; FIRST_WRONG_BOSS=color VS Position invalid inlegal B12control, earlierpredepth streak drawidentified butscalarcauseunproven. VISUAL_DARK limitedPASS; VISUAL_LIMIT/BOSS FAIL; AUDIO userNOT_RUN. Device remainsreplay1d30, normalrestoreea2f+hiddenoverlay pending. Final60/localcommit/baseline/APK/data/manual Honor400Pro pending; do notshipfailedcandidateorcallfullPASS.

# Honor Iron Giant bad phase captured and replay-verified - 2026-11-07T19:50:44.023466+08:00

Same sole writer / HEAD032e5f6 / dirty preserved. New boss trace completed exact finite3frames5752-5754, 15,032,074,879bytes; device=host SHA47e6a5592a6845c9f9dadc560156a8e4b414b760b8406639ff54cbebfb3d5358 (boss-trace-sha.json). Existing replayAPK1d30 reused, 3/3frames replay completed; phase1.png/phase3.png actually viewed and reproduce huge black landing streaks, no pause or wrong-phase substitution. Raw index977 actual shader modules /829 pipelines /633live draws (212firstframe), restoration-end473223 excluded. New trace is independent of removed earlier20GB trace, not a re-download.

First same-frame export now targets live draw475828 VSmodule554 candidate6018 /FS1501 candidatecdea, CB2481 begin475197 RP475822-475830 submit476316. Shader key mapping by runtime wordcount is provisional until actual bytes checked; no pairing/rootcause assumed. Existing exporter only supports first target per CB, so single-target run. Next inspect before/after attachments and complete resource matrix, locate first wrong GPU output. Location9 default experiment remains rejected/reverted.

User confirmed MiniMAX disk cleanup removed local T/data/romfs and original earlier trace around18:36. Preserve surviving19GB clone, all GPU exports/index/translator; no repeated setup or old trace download. Restore only needed translation inputs if new shader required. Device currently replayAPK1d30, normal restoreea2f and originallyhidden overlay still pending. No current source edit. CAPTURE_COMPLETE=NO (original-run per-draw independence/generations and new target coverage open), VIEW_VALIDITY fullscopeNOT_FIXED, FIRST_WRONG_BOSS=NOT_FOUND, VISUAL_DARK limitedPASS, VISUAL_LIMIT/BOSS=FAIL, AUDIO=NOT_RUN explicit user. Final60/commit/APK/data/manual Honor400Pro pending.

# Honor boss black-line flicker reproduced; location9 hypothesis rejected - 2026-11-07T19:23:46.698892+08:00

Ordinary marker0/30Hz baselineea2f (no capture layer) combined110srecord+Continue+scriptedroute succeeds: actual IronGiant first appearance captured, with long black triangles/lines during landing dust around76-81s. boss-route-baseline.mp4 /boss-baseline-entry-10hz.png independently reproduce reported blackshadow flicker. Repeated same route with ordinary candidate992ff: logs prove warm/runtime initialized patch1 and actual VS79255words; STILL same blacklines (boss-route-candidate.mp4 /boss-candidate-entry-10hz.png). Thus definedloc9 is INSUFFICIENT, NOT the accepted rootcause/fix. Exact video manifests boss-initialization-negative-result.json. Staticdialogue/no-flicker was not sufficient; actual entry A/B is decisive. No capture-layer-only attribution: both ordinary packages reproduce.

Reverted ONLY own new location9 initialization change in xbg_boss_interface.h/xbg_game.cpp/tests/device/test_boss_interface.cpp, preserving prior declaration-only workaround and all earlier dirty changes. Failed candidate sources archived defined-candidate-source/; APK992ff retained as rejected experiment, not delivery. No integerfact/Dark/Limit bundle revert. Source/final60/audio unrelated unchanged.

Now reused exact existing capture APK17faf (baseordinaryea2f plus original61abd layer, no rebuild/download), installed-r preservingdata; GPUdebug4 app-only enabled, unassisted, capturefile gfxr-boss-flash-20261107.gfxr, finite3frames, initialtriggerfalse. New capture_boss_route.py uses same combined recording/gameplay process; settrigger at north input+11.7sec to target impact dust/blacklines, without pause. Running session88136, boss-route-capture.log/plan, recorderhostPID and per-step timestamps disk. Do not start competing writer/input, redo setup or trigger14.2sLimit task here. Resume this exact handle/log first; newlyneededtrace only, original20GBpurple trace untouched. Once complete validate activebadphase, actualdraw/resources/module/attachments, then firstGPUerror for particle draw; do not assume creature VSfe033 caused lines or equate bodylighting with impactparticles.

Device currently capture17faf, ordinaryea2f restoration pending; four fixedXGS remain and all data/saves retained. Touch overlay temporarilyshown, originallyhidden; restorehidden atfinalhandoff. FIRST_WRONG_FLASH=NOT_FOUND; CAPTURE_COMPLETE=NO; VISUAL_LIMIT residualFAIL; VISUAL_DARK limitedPASS; AUDIO userNOT_RUN. 60Hz finalgoal remainsunverified, diagnostic30 forcausalcontrol only; afterfixbaseline/codecommit/APK/data/manual Honor400Pro pending. Goalactive, notcomplete.

# Honor boss defined-output source candidate built, ordinary A/B pending - 2026-11-07T19:06:14.558517+08:00

Source candidate (not yet device-accepted): xbg_boss_interface.h now initializes the exact missing output9 to(0,0,0,1), matching pinned local Ryujinx Translator.cs182-315 NextInputAttributesComponents policy. Original module guard78860words/types/IDs and HonorAdreno735/key gate unchanged. All original guest instructions preserved; xbg_game.cpp warm/runtime call same helper. Updated actual-module test_boss_interface.cpp rejects malformed/already-patched input and strips only added interface/default/store to recover original byte-identically; hosttest exit0 and patched Vulkan1.1 spirv-val exit0. This is NOT a proven flash root cause or visualfix. Broader phone750gate behavior not changed/verified.

Ordinary candidate APK992ffdedb4aa07591b8211dcbb8abc8707d26b2629c34b029a2edb341e4deff7 built exit0; build-boss-plan/log/exit, test-boss-initialized, boss-source-defined.spv in limit-tail-capture-20261107. Not installed yet. Existing VS/FS/draw/depth/FS color retained; no zeroFS/remap9to8/ALWAYS/audio/performance edits.

GameUI loaded existingAutoSave chapter1battlefieldLv1 Time16:37 (no savefile overwrite/edit); confirms checkpoint afterLimit and beforeIronGiant. Route now established: from loadedfield up9/right12, WAIT zone loading, then new Lcorridor right9/up12 reachesIronGiant dialogue. Previous up12/left9 during loading reversed transition; do not repeat those wrong route steps. First40s transition recording ended before subsequent input began due orchestration delay, shows only field, NOT bossappearance evidence. Need launch recorder and route in SAME live Python process to cover event; do not claimpaused/staticdialogue or priorroutevideos reproduce flash. Source-defined diagnostic and32-bitdefault evidence remain separate.

To remove capture-layer interference from A/B, restoring ordinary old-output APKea2f with4XGS retained, install-r and app-onlyGPU4settingsdelete /capturepropsclear /triggerfalse now running session50081, log ordinary-boss-baseline-install.log. Next waitactualhandle, inspecttitle, then combined110srecord+Continue+knownroute foroldbaseline. Candidate992ff then same ordinaryroute; requiresvisualproof. Current target30 forcausaltest; final usertarget60 stillpending actual60/audio gates. AUDIO userNOT_RUN, Limitresidualblacktail/orbsFAIL, darklimitedPASS, bossflashFIRST_WRONG_NOT_FOUND. No baseline/codecommit/packageyet; Honor400Pro deliverablesstillpending.

# Honor user expands scope: Iron Giant entry flicker +60Hz target - 2026-11-07T18:48:37.456968+08:00

Latest direct user request adds black-shadow flicker when current Iron Giant appears and confirms final target60Hz. This supersedes prior task's no60 optimization scope for eventual delivery; current diagnostic remains30, no60PASS claimed or audio buffer reduction. After graphics completion user also requests baseline/code commit/APK/data/manual for Honor400Pro, not yet fulfilled. Audio remains explicit user NOT_RUN.

Actual current PID17487, tail-capture APK17faf (same ordinaryea2f runtime plus reused capture layer), current Iron Giant cutscene was paused SYSTEM MENU; triggerfalse/frames1. No background journey processes live (hostps checked); previous newgame helper already exited0. User-controlled scene replaces obsolete tutorial macro phase: do not resume that macro or trigger14.2s from this unrelated scene. No capture triggered on paused menu. user-boss-current.png /user-boss-guest.log preserve live identity; actual VSfe033/FS5ad1 pair and runtime missing-output patch1, VS79245words with89integer rewrites.

Offline boss-interface-audit.json proves original VS has output8,10 but no9; FS loads input9.xyz in a conditional branch. Existing xbg_boss_interface.h declares location9 without any store. This is a concrete undefined interface, a hypothesis for flicker, NOT proven root cause. Pinned local Ryujinx Translator.cs182-315 initializes NextInputAttributesComponents to0/0/0/1; a diagnostic original-VS+defined-location9 variant assembled/spirv-val Vulkan1.1 exit0 (boss-defined-location9.spv /plan.json), not deployed, GPU/visual NOT_RUN, no source edit. Do not equate declaration orvalidation with flicker fix, or remap9to8 without guest evidence.

First35s boss recording stayed paused because instantaneous keyevent97 was not consumed by guest. Secondrecord boss-flash2.mp4 (5945140B) after temporarily showing touch controls and250msB resumed actual cutscene/dialogue, confirmed screenshotboss-unpause-attempt2.png. This does not recreate initial appearance; no flicker PASS/FAIL inferred yet. Overlay originallyhidden, nowshown temporarily; restorehidden when leavingtask. Read-only load-list inspection now in progress to locate current checkpoint for repeat entry, no save selection/overwrite/deletion. Capture layer still enabled app-only, ordinaryea2f restore ready; this stage not final device restoration.

FIRST_WRONG_FLASH=NOT_FOUND; FIRST_WRONG_SHARDS=VS Position as prior; VISUAL_DARK limitedPASS, VISUAL_LIMIT stillFAIL residualtail/orbs, CAPTURE_COMPLETE=NO, fullVIEW_VALIDITY=NOT_FIXED, AUDIO=NOT_RUN. No baseline/commit/package issued yet.

# Honor ordinary blue ice restored; residual Limit black particles remain - 2026-11-07T18:32:50.668188+08:00

Ordinary ea2f APK actual base SHA checked, D1/DB559 natural bundles deployed, marker0 and GPU4settingsnull. Runtime loads D1 words55049/DB55963527, integer rewrites1 each. 35s dark nativeMP4 (13249494B); exact manifest ordinary-trial/visual-result.json: tutorial complete cast blue-whitecore/purpleredouter, all3enemies settle, self-test PASS limited structure/color. 45s Limit nativeMP4 (14163754B) begins before A but input delayed to~25s; blue ice at~33s matches Switch05 texture/shape, gold flames restored, later lightning/beam rendered. Final beam nearEOF; do NOT call video cast-to-settlement complete, later screenshot after-limit.png proves field return. Still black round spots at ice-to-lightning transition and black particles during fire outro (limit-contact-2.png), so VISUAL_LIMIT remains FAIL. No early-purple replay substituted for later-ice actual visual. No source edit since2B12facts.

FIRST_WRONG_SHARDS remains VS Position; new static fetch audit confirms originalD1 B18/24 fetch17/20 vs new26/26 plus6 extra native slot128float, DB55925/28 vs34/34 plus6slot128float (static counts, not runtime execution). Red75GPUresources equal and2453HDR causal evidence as prior. Existing20GBearlypurple trace cannot identify later residual particles; preparing one later red-smoke capture using SAME layer/tool, latest ordinary ea2f asbase byte-identical non-signing entries. Capture APK17faf5a8fd0d07a961f32daeef3055566a483b3e85c175ef548ab92ff54c2988, library61abd... reused. New evidence dir limit-tail-capture-20261107/, single-frame trigger, target~14.2s after Limit A based on real movie, not13s provisional plan. Restore ordinaryea2f ready; preserve4shaderoverrides/all data. Host50Gi/device83Gi free before new trace, finiteoneframe.

Entry flicker: entry2.mp4 49.953s, dense10Hz11-16s window shows one clean fade into first soldier story, no repeated flashes observed in this one pass; NOT_REPRODUCED limited window, NOT_FIXED/NOT_EXCLUDED overall. Boss/shield/completeocclusion/independentQA still not accepted. AUDIO NOT_RUN by user; runtime performance10 is NDK NONE enum, source audio_bridge.c208 sets NONE, fence spin0 logged; no subjectivePASS or standing-gate completion.

New user instruction recorded: AFTER completing fix, baseline +code commit +APK/data package/install manual, target Honor400Pro. This is pending graphics completion, not current baseline approval. Preserve dirty, no push; only current Honor tree/device. Target phone not tested; exact model already supplied, do not ask again.

# Honor ordinary shard/trail candidate deployed - 2026-11-07T18:04:12.910481+08:00

Same sole writer/HEAD032e5f6; preserving all dirty/data. New red independent draw3495303 normal EQUAL, VSdb559/module878 +original FS1287, has75 descriptor resources with identical GPU valid bytes between B12-only and full-VS tests. B12-only outputs0; full retranslation outputs2453 HDR pixels exactly bbox801,392-875,560, matching separately captured pre3495030 coverage. Viewed red-retranslated-hdr/hdr-comparison.png: gold flame restored, all other attachments unchanged. C1 exact match to this draw GPU. First scalar for this red VS not measured; VS path causal only, distinct from D1 Position result. Evidence red-controlled-comparison.json / red-resource-matrix.json and both GPU dirs.

Source facts now add ONLY native R32_UINT B12 for D1 andDB559. Existing half removals unchanged; B18 stays R16_SFLOAT/range2. New ordinary APK ea2fdd5261e05660bc229cf89c10b44655a143db7fc2a04f631f542daf69c1c0 built exit0 (build-final-plan/log/exit), install-r succeeded preserving data; device base.apk SHA exact. Natural-layout D1 XGS62d51057373b7d52ee418b7906bc144ffa0f3e90daffcf78f3dea3c7322a5c86 and DB559 XGS7e0723610e7b6d146ccf85541fa2cb734b1230ed4a1ee0ce2192dd969d2cb6a7 deployed/sha verified; both primary previously absent. Remapped diagnosticSPV not deployed. New warm list backup/filter removes exactly20 records for these2keys, preserves816 other records byte-identically; driver cache unchanged. Primarydark920bd... andspheref357... remain.

ordinary-trial/DEPLOY-PLAN.json/deploy.log/warm-list-plan.json record restore. APK now ordinary, PID14075; marker0, GPU4settingsnull, capture3propsempty/triggerfalse; title/NewGameNormal reached. am start-W returned timeout after10sec but process/title proven healthy (do not call launch completed within10sec). Full Limit ordinary visual next, still VISUAL_LIMIT=FAIL pending retest, AUDIO=NOT_RUN per user. FIRST_WRONG_SHARDS=VS Position; CAPTURE_COMPLETE=NO, full scope VIEW_VALIDITY NOT_FIXED as earlier unresolved variants; entryflash/regression/independentQA open. No performance/audio/source save changes.

# Honor Limit shards first wrong = VS Position - 2026-11-07T17:59:11.162453+08:00

Same sole writer / dirty tree / HEAD032e5f6. Actual normal GEQUAL draw3495377 (d1d6 VS1042, original FS1289), frame37722, exact paired pre3494979 input. Direct GPU terminal Position probes retain full VS arithmetic, vertex0 for88 instances, valid markers88/88 and finite88/88 in both runs. Typed-original has15 distinct positions, ALL88 outside xy clip; first4 identical (-232.10005,-8536.890625,200,4425.26416), NDCy approximately-1.929126. New retranslation differs88/88,82 distinct positions,74 inside xy clip; first position (81.51373,1222.4945,200,4697.08154). Earlier commentary saying all88 identical was corrected: only first4 identical;15distinct overall. Evidence FIRST-WRONG-LIMIT-SHARDS.json + two position-*-gpu dirs and logs. This establishes Position as first wrong stage before coverage/FS/depth; no fragment/depth workaround. Existing full-VS/original-FS control restores3090 blue-white HDR pixels. Actual original shader/C1 and same-draw resource matrix retained.

Probe first attempt failed before draw at pipeline67844 with VK_ERROR_UNKNOWN: VS1042 is shared by additional pipeline layouts. Corrected diagnostic trace clone adds VS visibility only to existing SSBO10 in4 relevant set layouts1036/1285/1858/12919901, each decompressed/readback-verified. Original20GB capture unchanged. SSBO writes252160..253920, disjoint from captured vertex/index/C0/C1. Nonempty diagnostic FS and same probe for both runs; probes not normal visual acceptance. Query VK_NOT_READY replay-vs-capture warnings retained.

New source change xbg_spirv_stencil.h adds ONLY d1d6/B12 integer fact (native98/range32768; B18 remains76/range2). Ordinary APK build exit0, ice-capture/apk/honor-limit-ice-validity.apk SHA49dc3b698dfbeb45cde7cb513658a72ce09a96396e240019d3e8eec4ae291d28; not yet installed. D1 natural-layout XGS62d510... ready, no remapped diagnostic deployed. Red trail VSdb559/module878 independently translated with existing tools/index; actual same-frame B12 uint98/B18 half76/B24float100 mapped; typed-only test running. No root-cause or visual success claimed for it yet. Replay APK1d30 still installed; ordinary restoration/deployment pending.

CAPTURE_COMPLETE=NO (original-run per-draw attachment/generation independence and later ice-climax capture remain missing); VIEW_VALIDITY full scope NOT_FIXED, target D1 legal controls complete; FIRST_WRONG_LIMIT_SHARDS=VS Position. VISUAL_DARK=limited tutorial structure/color self-test PASS; VISUAL_LIMIT=FAIL ordinary package, controlled shards restored only. AUDIO=NOT_RUN per user, full standing checklist open; entry flash/other trails/occlusion/shield/fire/boss/independent QA remain open. No performance/audio/data/save changes.

# Honor Limit blue-white shard restoration in controlled replay - 2026-11-07T17:38:30.414700+08:00

Sole writer, HEAD032e5f6, all dirty preserved; no new game source edit this milestone. New 3-frame trace has exact EOF (3504314 blocks), 20258792101 bytes SHA b45629a0beeac836c4fc9c5b3068e1662f34d2dd87a71650b6442d0ce7d4ba0c, frame37722-37724. It is purple charge, NOT later blue-ice climax. Replayed 3/3 frames; black trails persist. APK1d30 replay currently installed; ordinary restore0545 remains available, GPU4 settings null, triggerfalse, no source/data/save deletion. Work/evidence: dark-shader-retranslate-20261107/ice-capture/.

Actual deployed new632c VS2482 SHA85eb0e... is byte-identical to candidate rewriting12/24, original17ab FS2508. Its sphere pair pre3495015/color3495333 has identical VB/IB and index1800 x2. Pre963 depth pixels, color956 HDR pixels at same bbox840,632-874,666: this particular sphere is working. Do not conflate other black shards with it.

Residual blue shard target independently identified as color VS d1d6ceeb3a7b2c18/module1042 SHA9b9bb59a..., FS106540616ca2eaca/module1289, draw3495377 normal GEQUAL/no-write. Correct pre is draw3494979 VS085a/module905 +FS1271; both share indirect buffer12922858 offset60, 144indices x88, exact same VB1728B SHA b62d13... and IB288B SHA8daaa4.... Pre3495 depth pixels bbox615,47-821,299; original color all0. Earlier provisional e243/3495030 is NOT its paired pre (6indices x32); retained as separate negative control. Original 6-target export only consumed first entry because same-CB entries are not all supported by current exporter. All further runs use single target or distinct CB targets; no tools rebuilt.

Recorded resource matrix trail-resource-matrix.json confirms B12 R32_UINT format98/range32768 while actual1042 image type was float; B18 R16_SFLOAT/range2 legal, B24 R32_SFLOAT/range13600. B12-only typed shader e562... preserves all attachments0, so type compliance alone is insufficient. Existing translator+index reused for exactly d1d6/e243: d1d6 builds, e243 strictly rejects dynamic-array32 (not stripped/deployed). New d1d6 XGS62d510... C1 exactly matches this draw GPU256B; replay-only extra float buffer B42 aliases original B24 (same source7 slot128, correct nativefloat view), UBObindings shifted back. Actual-module candidate0e8144... with B12 integer fetch and original FS/GEQUAL writes3090 HDR pixels bbox615,73-805,299; black fragments visibly become blue-white shards in trail-retranslated-hdr/hdr-comparison.png. Remaining attachments0 changes. New trace / same-draw data only.

FIRST_WRONG_LIMIT narrowed to VS path (controlled full retranslation), Position vs varying still being directly measured; no claim yet of specific first scalar or ordinary-game PASS. CAPTURE_COMPLETE=NO (original-run per-draw attachment independence/remaining generations, later blue climax not captured); VIEW_VALIDITY=NOT_FIXED for full Limit scope (observed new B12 type mismatch, B18 legal); VISUAL_LIMIT=FAIL ordinary candidate, REPLAY_BLUE_SHARDS_RESTORED only. VISUAL_DARK=limited tutorial structure/color self-test PASS; AUDIO=NOT_RUN per user; entry flicker/remaining red trails/occlusion/shield/fire/boss/independent QA still open. Device remains isolated, only own-package replay; next legal Position probe preparation reuses existing SSBO10 with explicitly limited layout visibility.

# Honor Limit ice-stage black polygons reproduced - 2026-11-07T15:54:48.018235+08:00

Same sole writer, HEAD032e5f6 and dirty tree preserved. Capture APK d364..., marker0, both new color XGS unchanged. Actual Chaos Limit tutorial video shows large pure-black polygons during blue ice and black trails in the following red fire stage. VISUAL_LIMIT=FAIL independently reproduced. Evidence: dark-shader-retranslate-20261107/ice-capture/limit-full.h264, limit-full.mp4, limit-contact.png, limit-visual-reproduction.json. MP4 is a 25fps frame-order reference, not wall-clock timing acceptance. GPU trigger NOT_TRIGGERED: the brief ice stage passed before screenshot review; no paused capture claimed. Next attempt uses video-derived timed trigger. Currently walking toward Iron Giant; capture settings remain app-only, trigger=false.

CAPTURE_COMPLETE=NO (new ice phase not yet GPU-captured; earlier original-run per-draw attachments/generation gaps remain). VIEW_VALIDITY dark native half confirmed legal, remaining Limit variants not all audited. FIRST_WRONG_LIMIT=NOT_FOUND; VISUAL_DARK=limited tutorial structure/color self-test PASS; AUDIO=NOT_RUN as user cannot listen now. Entry flicker cause unproven. No source/tool/performance/audio/save-file modifications or independent QA/full PASS.

# Honor碎冰定向捕获已启动；旧Limit输入9+26项补齐 — 2026-11-07T15:17:45.144706+08:00

用户明确残留在蓝色冰晶/碎冰段，旧f16817截图仅紫色背景早期，不能用于其验收。复用既有GFXR c081f1b库，只给普通0545...加入原库且核所有原非签名entry字节一致，新诊断APK d364f4a26669397aed35196fdf573702cac57bbf2560ba9b4c30b379032cb46e，install-r保留数据，PID16124。capture layer已实际Loaded，unassisted、trigger1帧、trigger=false；marker0。两个color XGS920bd.../f357...保留，原runtime代码未新改。当前在New Game默认普通难度流程开场影片，计划到Limit蓝冰段才触发；不是暂停错相位抓帧。恢复普通APK0545...的路径和原四GPU设置/props在ice-capture/install-plan.json。无需重建或重复下载工具。

离线最小raw metadata解码先与既有normal JSON的4类API逐字段核对，通过后仅解析本身已有Limit trace的template/view/bind。limit-recorded-resource-matrix.json补43条完整descriptor身份；9/9 texel ranges逐项关联该draw GPU backing文件，所有inbounds；pre/color12/18/24有效字节完全一致，虽然backing buffer/offset各异。26/26 UBO/SSBO独立切片与该draw offset/range/buffer/size一致、26个唯一文件。证据limit-texel-gpu-validation.json/limit-ubo-gpu-validation.json；旧“9texel联表缺失”现已关闭。CAPTURE_COMPLETE仍NO，原运行逐draw完整输出独立对照/部分世代及报告碎冰新相位尚未获得，不用元数据闭合替代视觉。

已离线准备Limit新VS remap到旧descriptor layout的replay候选a80b...，仅spirv-val通过，尚未GPU运行，nativeFormat前提/两B18合法view控制仍需做；不作为正式修复部署。VISUAL_LIMIT=FAIL用户实测，FIRST_WRONG_LIMIT未定；VISUAL_DARK仅教程结构颜色自测PASS，AUDIO明确NOT_RUN。新剧情瞬时闪烁用户反馈保留为未定因回归。

# Honor用户反馈：Limit仍有黑色部分，切入剧情短暂闪烁 — 2026-11-07T14:44:59.751729+08:00

用户已反馈Limit候选后仍有部分黑色特效，VISUAL_LIMIT=FAIL，不得把单个VS重译/局部改善算完整修复。当前PID17237、APK0545b7c...不变，Limit已部署XGSf357...；运行日志实载632c color words55353/rewrites27，匹配新bundle重写后长度（完整module字节仍须dump）。同日志出现多对FS，不能只盯17ab单pair或假设暗魔法根因已覆盖全部Limit。user-limit-still-black-guest.log保留。

另：用户重新开档进入第一幕战斗剧情时闪烁几下自行恢复，未由写手复现，FIRST_WRONG_FLASH=NOT_FOUND。after-user-act1-flash-guest.log/截图、user-current-feedback.json记录现场。用户当时在操作，写手停止输入/重启，仅只读与离线推进，避免并发争抢。AUDIO=NOT_RUN，用户明确暂不能听蓝牙；此门不可报PASS。

暗魔法f42543 native view13行核到：B18两端native R16_SFLOAT/vk76/2字节，shader float；新增B54 native R32_SFLOAT/vk100/float，不加盲目UINT表项。normal EQUAL color group99/pre GEQUAL group79；dark-native-view-audit.json索引1331文件。CAPTURE_COMPLETE历史缺口保留；此前“Limit未部署”已过时。继续Limit同draw资源联表/VS首错独立审计，不再重复暗魔法六load实验。

# Honor暗魔法普通marker0结构颜色已恢复 — 2026-11-07T14:23:15.667879+08:00

唯一writer/HEAD032e5f6，APK0545b7c...+dark color XGS920bd097...实际运行。device-trial/dark-candidate-marker0-full.mp4完整22秒、普通marker0，无ALWAYS/空FS/删prepass；已实际查看视频接触表第二行的蓝白亮核、紫红外圈，原大纯黑主体被正确结构替代。普通角色/火焰/铁巨人显示未见回退。VISUAL_DARK=PASS(本次教程战斗结构颜色自测)，完整遮挡/独立QA/AUDIO仍NOT_RUN，不能报整刀PASS。先前短视频因父进程结束提前截断，不作完整证据，22秒新视频以session2709 exit0完成。

Limit颜色XGS仍未部署，正在同一战斗独立验证旧Limit基线；VIEW_VALIDITY动态native格式仍待活动draw核验，CAPTURE_COMPLETE旧缺口保留。普通图已足以推进，不再重做暗魔法六load反事实。详细manifest ordinary-dark-visual.json。

# Honor颜色候选进入普通运行，half16项GPU检查通过 — 2026-11-07T14:10:44.632420+08:00

原始四VS已从平板现有容器只读复制/提取，一次5919组index完成且无group/job失败；输入设备/主机SHA一致（utoc ceb45c...，ucas695868...）。原始code/C1/job和Maxwell TLD.B字段索引在work/ffrs_xgs_uwitcx3m及maxwell-texture-load-audit.json。固定译器恢复color暗魔法9单handle+9双handle读取组，Limit9单+6双handle组；color2份Vulkan1.1校验通过，pre2份含runtime数组被现有reflect严格拒绝，未部署、不删数组。

已新增真实Vulkan组件test_dark_half_fetch.cpp/dark_half_fetch.comp，Honor Adreno735 native GPU16项通过：range2 R16_SFLOAT的0/1/负零/最大有限/最小subnormal/最小normal/正负inf，及R16_UINT原位对照；1.0结果分别3f800000与00003c00，不再以零证明语义。源表仅去掉dark/Limit四VS的binding18 integer事实，保持其原float采样/native视图，其余整型事实原字保留；xbg_game仅opt-in probe补nativeFormat元数据。APK0545b7c...构建exit0，实际install-r；未改音频/帧率/同步。

只部署dark color新XGS920bd097...（264500B，C1/顶点输入与旧bundle逐字相同，新增binding54及相应source映射）；Limit颜色候选f357...暂未部署。原设备两个primary覆盖均不存在，原cache/list已备份device-trial/before-*；只过滤warm-list中两个color key的旧layout，其余811条原字保留，原driver cache不改。冷启17237，warm811成功0失败，Continue进原存档野外。marker0真实场景施法/音频门待执行，不能把准备/组件/安装当画面PASS。精确部署/回退计划device-trial/DEPLOY-PLAN.json及warm-list-plan.json。

# Honor单译器已准备，正在恢复允许范围的原始输入 — 2026-11-07T13:55:23.132764+08:00

用户明确“允许，你抓紧吧”已解除单译器准备阻塞，goal恢复active；不再等待路径/重复问设备权限。仅本Honor树.local内准备nxnative8e091私有译器（既有Vulkan offset fallback，无新增Phi扩展），build exit0；Microsoft官方.NET10.0.403 SDK230759804B SHA5125893c66b...与发布元数据全值一致，源码/许可证/命令在dark-shader-retranslate-20261107/。Ooz9c67c核心decoder+x86CLI本地构建exit0，GPL3+不进APK；辅助用途不是第二运行时。

主机.local/romfs既有链接现在找不到请求的utoc，未进入RG确认/修链接。已改为只读复制平板现有FFRS容器到本树新data/romfs，copy-device-input.py/log/plan记录源、尺寸与设备/主机SHA比较；工具session需继续poll。不重复拉取既有GFXR captures，设备包/数据/marker不变。译器首次运行SDK日志报告创建未信任ASP.NET development certificate；该首次运行副作用未预先隔离，后续已设置私有DOTNET_CLI_HOME/NUGET_PACKAGES，不读/导出证书或修改信任，全局配置不再变更。

下一步保留原始提取中间物，用一次index扫描只重译四个目标VS（dark pre/color、Limit pre/color），确认丢失读取/实际绑定，再普通marker0验证。既有GPU因果结果仍有效，未声称正式修复或视觉/音频PASS。

# Honor 暗魔法首错落在VS丢失读取，受控原FS重放恢复颜色 — 2026-11-07T13:21:18.473917+08:00

**未交付正式修复，不报整刀PASS。** 持续设备授权有效，不再询问窗口。保持唯一writer、原dirty树、HEAD032e5f68172c101cb2b6e44ee06f02974dab2da8。当前已恢复普通normal-capture.apk8c0a6ccf...（install-r，数据/存档保留），marker0、GPU debug四项null、capture trigger unset、无游戏PID；候选shader未安装到游戏覆盖目录。

| 字段 | 本轮结论 |
| --- | --- |
| CAPTURE_COMPLETE | NO；dark/Limit 的 replay GPU导出已实际完成，但原运行逐draw全附件独立一致性/部分资源世代和下列Limit view映射未闭合 |
| VIEW_VALIDITY / VIEW_VALIDITY_FIXED | NOT_FIXED / NO；两个R32_UINT/range2仅在隔离副本改为R16_UINT/range2，生产half非零/负零语义未修 |
| FIRST_WRONG_DARK | VS粒子位置/缩放读取被0替代，造成Position收缩；六次读取恢复具有同帧GPU因果证据。Maxwell原始指令与译器级根因尚待核对 |
| FIRST_WRONG_LIMIT | NOT_FOUND；独立GEQUAL pair已导出，不能套用暗魔法因果结论 |
| VISUAL_DARK | FAIL（普通包仍是未修基线）；受控重放HDR恢复蓝色亮核/紫红外圈仅为REPLAY_STRUCTURE_OBSERVED，非marker0实际施法验收 |
| VISUAL_LIMIT / AUDIO | NOT_RUN / NOT_RUN；没有独立QA、遮挡/shield/fire/boss回归或音频整刀PASS |

## 暗魔法决定性证据

证据根仍为 `.local/honor-device-profiles/flame-20261104/dark-normal-capture-20261107/`（下称P），未混旧帧。帧8918 live pre1383759 / color1384398，GEQUAL/write、正常EQUAL/no-write不变。P/FIRST-WRONG-DARK.json与FIRST-WRONG-EVIDENCE-INDEX.json给出机器可读结果、尺寸和hash。

1. 实际VS867完整原计算在末尾读Position，2590/2590记录有效；pre VS868以其自己的同帧C1在shadow pipeline执行，同样2590有效。顶点输入pipeline格式/stride/topology、vertex/index、六texel范围、七个非C1 VS UBO、十BC4 mip有效块一致；各自C1逐字匹配原XGS。pre不是原pre draw直接插桩，保留shadow限制。完整Position中color仅十几像素，pre展开大片。
2. 逐scalar GPU trace：color2266/3229、pre3915/5545实际执行记录；只跑首triangle/instance0，两个最终vertex0 Position均与各自完整draw逐位一致。1194个同输入表达式节点GPU结果相同，C1动态读取按各自payload身份区分；没有把不同C1当驱动错。
3. 第一个有非零源对照的丢失字段是color粒子Position.z：actual asm line7880 `OpStore %2110 %float_0`，后续%2234为0；同帧binding24 index4有效字为0x43012b17=129.16831970214844，pre确实读取它。缩放三项同样丢为0，pre读取binding24 index20/22/24=4/5.5/5.5。原XGS内已有这些0，并非本轮GPU格式改写才产生。
4. 先只恢复三scale读取（数据驱动base/stride，未硬编码4/5.5值）：GPU网格重新展开但仍偏移；再恢复三position读取，**2590/2590四分量Position与pre shadow逐位相同，最大绝对差全部0**。原其他算术/分支保留，不换VS整份算法，不动depth或prepass。
5. 取消所有Position/SSBO插桩，保留六读取恢复，使用**原FS870**与正常EQUAL、原附件/混合状态重放：HDR写出305417像素，bbox169,113–927,631；其余color及depth/stencil变化0。P/color-six-load-original-fs/gpu/108文件、134337035B，gpu-audit.json完整尺寸/hash/有效区；hdr-comparison.png是同曝光映射的中间HDR前后图，已实际查看蓝色亮核与紫红外圈。不能当普通游戏最终图/参考逐像素匹配/音频PASS。

实际原VS SHA161df822093faebfafea89b18699514530c40dbc5dc179dc8b9d945d9e0915a9；六读取无插桩候选VS SHAf95a83a7ddb422c3c632d900f72cd7bdeb5c56b70c77777d68686a571626ab29；原FS SHAc9de616a2536b41e2f3e83b68c65cf983da9e1da8a600f07943baf2cc25c952f。所有实验复用现有replay1d30...，未重建/下载工具。P/color/pre-vertex-trace各有records/scalar-trace/gpu-annotated、GPU文件与invocation/log；三角形子集改动在draw-subset.json。

可审查复现脚本 `scripts/graphics/probe_honor_dark_lost_reads.py` 是本轮唯一新增Git范围代码，只生成诊断SPIR-V、不安装，严格核实际输入SHA、拒绝覆盖及工作树.local之外的生成物。命令：

```sh
python3 scripts/graphics/probe_honor_dark_lost_reads.py --shader .local/honor-device-profiles/flame-20261104/dark-normal-capture-20261107/actual-shaders/sh867 --out .local/honor-device-profiles/flame-20261104/dark-normal-capture-20261107/reproduced-six-load
```

本次exit0，输出逐字等于GPU实测候选f95a...，spirv-val exit0；错FS输入负控制exit2且无输出目录。重跑要换新out，保留已测产物。不是通用shader修复：仅覆盖当前float-buffer分支，half路径、其他丢失读取和原Maxwell语义尚未审计，故未部署成默认补丁。

## Limit独立消费结果与缺口

复用f16817完整已有trace，live pre draw2568508/cmd2627/begin2568127/submit2568702（pipeline1288，VS908/FS1287，GEQUAL/write）；color2568775/cmd77/begin2568714/submit2569368（pipeline1295，VS1071/FS1294，GEQUAL/no-write）。1800indices×2instances，两draw顶点4260B/索引3600B逐字相同。pre改depth/color2027像素，bbox405,499–455,549；color六color和depth/stencil均无变化。**不能据计数定因。**

P/limit-live-gpu/122文件、222101441B；limit-live-gpu-audit.json含完整文件hash、101资源行与有效压缩块长度/前后附件，run-limit-live-device.py/indices/invocation/日志可恢复。本次新的23项VS覆盖矩阵limit-resource-coverage-matrix.json明确：7+7 UBO切片文件有，3+6 texel views的底层全buffer虽由FFRS_DUMP导出，尚未将每binding view/offset/range关联到文件；不能因dr.json没列texel就宣称它们不存在/不访问，实际VS有57/42次ImageFetch。sampler/世代等还需同帧metadata联表。原运行独立逐draw全附件对照也未闭合，CAPTURE_COMPLETE_LIMIT=NO。

实际pre FS1287完整8740B SHA b90acafd55c462dbc4a2a9dcaf11dddf6e469aa119354df0b4c109d2d28cec17，已保留；源key0066ad2ea935247d还未逐字匹配，不能凭历史pair强套。与本地bdce...仅一常量不同（word361，int18 vs9），这仍不构成身份匹配。VS/FS接口均有对应输出，无简单缺Location结论。VISUAL_LIMIT仍NOT_RUN。

## 正式修复所需输入与当前停点

GPU证据已足以把暗魔法主线从depth猜测推进到具体丢失读取。下一步应核原始Maxwell对应读取并重译目标shader，验证半精度分支及未知variant，随后限域修view语义、普通marker0暗魔法/Limit和回归/音频；不要把本次六load反事实直接冒充译器修复。

本树按文件名检索未找到可运行dmsh_xlate/dotnet/uepak/Oodle输入工具；既有retranslate命令唯一记录的路径均在用户禁止触碰的RG/codex-black-screen目录。此前要求“不重建工具/不重复下载”仍有效，因此没有擅自读取该禁区、下载或重建译器。已异步请求**允许范围内既有译器/原始shader输入产物路径，或明确允许在Honor树准备这一个译器**；这不是设备窗口问题，不重复问设备使用权限。等待此输入时不把目标标complete；保留唯一writer。

## 恢复、自测与音频硬门

13:18设备复核：install-r恢复8c0a6ccf2c454313675a6de7d95fa50596cb47d5e67bce02487895b1ca46a83d，设备base.apk hash相同。P/restore-after-gpu-position-20261107/有旧replay APK SHA、安装计划、install.log、verified-state.json。marker实读0、四GPU设置null、capture trigger空/unset、无org.ffrs.gamewindow PID；未安装候选覆盖。未清数据/卸载/迁移/改名数据目录/碰其它包；无kill-server/RG命令。未改游戏默认渲染源码，xbg_game.cpp仍SHA5bec39879497c95d31424df8da15460973cfa639dab14daa63f3a6100cf58e5b，所有既有dirty保留。

验收必须完整包含 `coordination/prompts/_STANDING-audio-acceptance.md`。本轮未抬帧/改缓冲/改音频：NONE与spin0只继承源码/既有包证据，**新候选实际音频NOT_RUN**。NDK NONE枚举10，与旧清单文字performance0存在记录差异，不能把10自动认作LOW_LATENCY。逐项：A2DP野外≥2min NOT_RUN；战斗≥2min NOT_RUN；运行中蓝牙连接/断开自动重开 NOT_RUN；TIMEOUT重试/不永久停worker未新运行验证；缓冲不减薄未改；xruns不作主观PASS；包名/SHA已核；没有音频改善/回归或整刀PASS。独立QA、遮挡/shield/fire/boss均NOT_RUN。

公开来源见 `coordination/results/P-CX-TERRA-dark-limit-OSS-NOTES.md`；上游混合Phi删除读取案例仅作机制线索，不能代替本机因果和客体指令核对。git diff --check exit0。本刀当前状态为未完成/等待允许的译器输入，不是task_complete。

# Honor GPU Position分歧已实测，追踪首个算术差异 — 2026-11-07T12:52:40.072758+08:00

actual color VS867原计算+末尾Position读出，2590/2590有效marker、finite；pre VS868完整原算术以同帧byte-identical输入在color pipeline做shadow执行（只将C1 offset177152→176640匹配pre自己C1），2590/2590有效marker。两端vertex/index、6texel有效范围、7非C1 UBO、10BC4 mip有效块逐字相同；两C1均精确等于各自原XGS payload。shadow不是原pre draw直接读回，保留此限制。

pre投影范围instance0 x[-.559,.164],y[-.893,.389]，color仅x[-.201,-.181],y[-.405,-.369]；vertex0 clip pre[-491.12192,-778.442,10,2585.0173] vscolor[-576.26685,-1161.5807,10,3001.2268]。pre范围与大片depth改写相容。此为GPU Position阶段分歧证据，尚未以可信客体语义证明是哪边错误；FIRST_WRONG仍NOT_FOUND，不把不同本身冒认为根因。

证据dark-normal-capture-20261107/position-probe及pre-position-probe/：manifest、shadow-input-proof、positions-uint32.bin、positions-summary、gpu/各33项。正在使用原VS每个scalar store插入受控atomic trace，只追vertex0/instance0、其余写独立sink；非空FS不读SSBO。离线spirv-val已通过，记录3229/5545项；下一实验将相应三处indirect draw限域改为第一triangle以避免sink大量竞争，保持其它输入/状态，GPU最终Position须与完整draw负控制一致。未重建工具/新增游戏改动，唯一writer/持续授权有效。CAPTURE_COMPLETE=NO、VIEW_VALIDITY=NOT_FIXED、VISUAL_DARK=FAIL、VISUAL_LIMIT/AUDIO=NOT_RUN。

# Honor 合法短view隔离及Position探针启动 — 2026-11-07T12:36:37.766844+08:00

在用户持续空闲授权内继续，无新增游戏源码/导出器构建。普通EQUAL基线183项/new30 GPU切片已验证。离线副本仅修改createBufferView blocks7156/7193（view25816/25962）两个literal字节，98 R32_UINT→74 R16_UINT；range2、offset20224/2048、buffer/输入/shader均不变，new live两侧GPU有效两字节0000。同capture已有4条R16_UINT view创建VK_SUCCESS，offset满足已查alignment64。`view-isolation-patch.json`含精确文件偏移与args前后hash。此为零值帧的合法性隔离，不是half非零解码生产修复，生产VIEW_VALIDITY仍NOT_FIXED。

r16uint-gpu/重放183项已拉取，与normal-live-gpu/22项附件前后有效字节全相同；r16uint-attachment-comparison.json。故仅此合法化没有改变本帧输出；不得外推所有输入/UB原因。pre/FS和color/FS的FragDepth依赖slice保留原kill/control，离线spirv-val通过；实际GPU depth常量（pre b40/b46 vscolorb64/b70）对应范围字节完全相同，PixelDepthOffset[20].w=0，viewMode[31].w=0。不是FS深度GPU输出已一致，仍需位置/覆盖实证。

已准备并启动actual color VS867的临时GPU Position写出：原VS全部算术/位置写出保留，在main return前用OpAtomicExchange写4个Position原始uint32位和marker到既有SSBO10独立区，共1295×2×20=51800字节。来源实际VS SHA161df8...，probe VSc70e0bec...。device创建实际启用vertexPipelineStoresAndAtomics=true；只把两精确兼容layout865/2576768的binding10 stageFlags16→17。重放副本保持API block数量/indices不变，必要layout块仅转为非压缩存储。VS/FS替换通过现有--replace-shaders，不重建工具。

probe FS非空，写六个color且仅读SSBO尾字[65532,65536)，不读取Position写区；与VS无重叠读写。debug SSBO绝对区[253440,305240)与vertex[648192,663732)、index[663808,677728)不重叠；写入前原cmd1384388已有ALL_COMMANDS→ALL_COMMANDS/MEMORY_WRITE→MEMORY_READ|WRITE barrier。after-only资源导出用于取得VS写出后的SSBO；不拿探针颜色当视觉修复。position-probe/manifest.json、layout-patch.json、shaders/args/推送日志完整。实际GPU结果待返回。

# Honor 持续设备授权与正常帧GPU导出 — 2026-11-07T12:15:03.983962+08:00（Asia/Shanghai）

用户最新明确「我的设备现在就是空闲状态，你随时可以调试，不需要问我」：覆盖先前4分钟窗口和待答阻塞。后续在本任务内可持续使用A42BBB5109000333、同包install-r/冷启/重放/战斗捕获，不再逐轮申请；仅发现真实并发占用/断开/新冲突再停。仍禁止清数据、卸载、碰FC/RG/其它包、kill-server。goal先前blocked的设备条件已由用户解除，当前按用户恢复工作。

已复用replay-ubo.apk SHA1d30...，corrected live pair1383759/1384398成功重放导出，原输出gfxr-normal-live-gpu-20261107-1144，本地normal-live-gpu/183项；重放32.62秒，raw lognormal-live-replay-device.log。新f8918自己的30/30 GPU UBO/SSBO切片经validate_live_gpu.py独立对照该帧descriptor矩阵，buffer/effective offset/range/file size及30唯一文件均相符；normal-live-ubo-gpu-validation.csv/json、normal-live-gpu-manifest.json。旧179项未混入新帧。

实际GPU附件：pre color319161像素变化、depth319158变化，bbox(169,113)–(927,631)；color draw六个color+depth+stencil附件都0像素变化，正常EQUAL。均在1384×780有效紧密区域比较，非staging填充；normal-live-attachment-diff.json。仅说明没有附件差异，不等于无fragment或depth fail已证。查询VK_NOT_READY差异仍存在。下一步先限域隔离非法binding18并按Position/覆盖/FragDepth/depth/颜色逐阶段对照；不把ALWAYS当修复。CAPTURE_COMPLETE仍NO（原运行独立逐draw输出与view语义未闭合）、FIRST_WRONG=NOT_FOUND；未视觉或音频PASS。

# Honor 设备窗口阻塞审计 — 2026-11-07 12:01:07 +0800（Asia/Shanghai）

目标仍未完成。本次属于无新增GPU证据的阻塞复核，不称进展或后台等待：当前无本任务gfxrecon/replay/导出进程；正确实时pair索引、现有replay APK1d30...与恢复包8c0...均在且哈希重新核对一致，分支/HEAD未变。下一项必要实验是读取新EQUAL帧1383759/1384398的实际GPU输入/前后附件，未获新窗口前不能执行；不能以旧179项跨帧补齐、静态shader推断或再次核对已通过项目代替。

同一阻塞已连续跨三次goal turn：第一次已修正索引并发出4分钟独占问题，第二次只补实可离线完成的Limit模块身份但窗口仍待答，本次重核仍无答复/设备占用确认，且没有可继续仲裁首错的离线GPU执行路径。先前15分钟和8分钟授权均已结束；自动continue不延长设备窗口。为避免无进展重复，将goal标记blocked（不是complete/用户paused）。保留唯一writer路径占用和所有dirty。

解除动作：用户确认Honor A42BBB5109000333现在可独占约4分钟。复用现有replay-ubo.apk install-r、执行normal-live-draw-indices.txt对应live pair及run-normal-live-device.py、拉取导出、恢复normal-capture.apk；不重抓、不重建工具、不清数据、不碰其他包。现有问题已发，不重复弹问。

最后一次设备动作的恢复记录仍为11:40:27普通包8c0...，marker0、四项GPU设置null、triggerfalse、无游戏PID，后核APK SHA一致；本次未下adb命令，不能把历史现场当作当前空闲证明。FIRST_WRONG=NOT_FOUND、CAPTURE_COMPLETE=NO、VIEW_VALIDITY=NOT_FIXED、VISUAL_DARK=FAIL、VISUAL_LIMIT/AUDIO=NOT_RUN；所有视觉/遮挡及音频门完成前不报修好。

# Honor 离线模块身份闭合 — 2026-11-07 11:59:21 +0800（Asia/Shanghai）

本 goal turn 是证据进展：只读复用已有 `.local/test_multi_rewrite`，没有重建工具、没有平板操作。两个输入分别在独立子目录运行，原 `.local/test_out.spv` 未覆盖。`offline-type-audit/limit-vs-identity.json` 记录命令/cwd/退出码与完整hash。

- Limit color VS632c207422d773c5.xgs 经binding12/18/24既有重写，完整输出与实际sh1071逐字节相同：SHA28a35c6cff1a2db4746638678c7140f3ae788f6841a843272452104421c9d37f。
- Limit pre VS085afbc955a0744f.xgs同样与实际sh908逐字节相同：SHA7be4b0f6de99a3983c3e313835a694099c3d28f27ea9c4fb8a65f5115076dc3f。
- 因此此前“VS只作debug-name候选”的不确定性对这两个模块已闭合。Limit live color2568775的VS/FS身份与GEQUAL/no-write同时有依据；仍没有证明预通道FS0066...、完整GPU输入/输出或视觉PASS。
- 源码null_texture对Buffer维度显式使用65536字节静态零资源；该fallback本身不解释真实捕获range2的view。不能把2字节资源当作引擎空缓冲并擅自读取其邻接字节。R16_UINT与R16_SFLOAT对3c00/8000等非零和负零的数值语义不同，未据零值把替代格式宣布正确。

下一项唯一必要的设备操作仍是既有corrected live pair1383759/1384398重放导出，4分钟独占请求尚未答复。当前不操作平板；上次已恢复8c0普通包。CAPTURE_COMPLETE=NO、VIEW_VALIDITY=NOT_FIXED、FIRST_WRONG=NOT_FOUND；VISUAL_DARK=FAIL、VISUAL_LIMIT/AUDIO=NOT_RUN；没有新的渲染修复或验收结论。

# Honor 本地解析检查点 — 2026-11-07 11:53 Asia/Shanghai

继续离线消费新捕获，未在11:40:27恢复普通包后再操作平板。待答的4分钟窗口仅用于已修正的实时暗魔法pair GPU导出；不需要重抓或重建工具。

- 暗魔法f8918：StateEnd1381591；live pre1383759/cmd551/submit1384156、color1384398/cmd608/submit1385791，分别GEQUAL/write和EQUAL/no-write。新旧同帧各自pre/color viewport与scissor完全相同：viewport(0,778,1383,-778,z0..1)、scissor1384×780，不能用简单的两pass视口翻转差异解释。没有混用两帧输入。
- Limit f16817：StateEnd2566771；live color draw2568775/cmd77/pipeline1295/submit2569368实际GREATER_OR_EQUAL、depthWrite=false。FS sh1294与已有17ab021c45c63390.spv逐字节相同，SHA c96a5c7a7aef61298741743b97a0aff476019025433b9265697ba5bc4477c36c；VS sh1071 SHA28a35c6cff1a2db4746638678c7140f3ae788f6841a843272452104421c9d37f，其完整OpName/MemberName序列与632c207422d773c5源相同（只作VS候选身份，尚未逐字复现全部rewrite）。这是独立Limit颜色draw GEQUAL证据，不是视觉修复或prepair完整输入验收。见limit-color-targets.json、limit-audit.log和limit-actual-shaders/。
- 早先空导出针对1379909/1380385，录制在快照内，因此虽恢复后有提交也不具备当前导出器所需实时记录时点；不是整个新capture缺目标。raw容器边界已核，后一对可用。先前“1帧不够”的临时猜测撤回；不要为此重抓。
- CAPTURE_COMPLETE=NO（新实时pair GPU slices/附件尚待导出、原运行独立逐draw输出仍缺）；VIEW_VALIDITY=NOT_FIXED；FIRST_WRONG=NOT_FOUND。VISUAL_DARK=FAIL（本次仍复现大黑块，完整marker0门未通过）；VISUAL_LIMIT=NOT_RUN；AUDIO=NOT_RUN；其它回归/独立QA NOT_RUN。正常EQUAL和独立Limit GEQUAL的状态恢复证据，不等同特效PASS。

# Honor 正常路径捕获与重放边界复核 — 2026-11-07 11:48 Asia/Shanghai

CAPTURE_COMPLETE=NO；VIEW_VALIDITY=NOT_FIXED；FIRST_WRONG=NOT_FOUND；VISUAL_DARK=FAIL（本次仍复现大片黑色几何，非完整 marker0 验收）；VISUAL_LIMIT=NOT_RUN（仅前段截图，未完整结构验收）；AUDIO=NOT_RUN。没有黑块修复。分支 work/honor-gtpro-30hz、HEAD032e5f68172c101cb2b6e44ee06f02974dab2da8，既有 dirty 全保留，无提交/push。

## 已补齐和已纠正

- 消费 `dark-limit-restore-20261107/device-20261107-0957/` 的179项导出，30/30段 UBO/SSBO replay GPU 切片已验证；本轮生成逐 draw/stage/set/binding/element/effective offset/range/文件/hash 矩阵 `dark-normal-capture-20261107/ubo-resource-matrix.csv`。不再称这30段缺失。它们属于旧 f15768 ALWAYS 捕获，不与新帧拼装。
- 重新按真实格式的4B像素（stencil1B）比较22项附件紧密有效前缀，全部与旧同帧重放一致，且每一有效长度均不超过新旧文件长度。旧 `attachment-comparison.json` 部分颜色格式用了过大的 valid_bytes；修订为 `attachment-comparison-bounded.json`，保留原证据。`audit_inputs.py` exit0。
- 普通源码仍为移除无条件 ALWAYS 后 SHA5bec39879497c95d31424df8da15460973cfa639dab14daa63f3a6100cf58e5b，未再改渲染源。复用0957已构建库 SHA5f0b8ffe42ab3f111713e0b8a7565889cbd32e2167366eb188f6eadbf998b5f6，准备 normal-capture.apk SHA8c0a6ccf2c454313675a6de7d95fa50596cb47d5e67bce02487895b1ca46a83d。与旧 capture APK 除签名外仅 libffrs_game_window.so 变更，原 manifest/java/捕获层保持；`package-provenance.json`。本轮未重建导出器、未重复下载旧大捕获。

## 两次设备窗口与新捕获

用户分别授权15分钟（10:39:46–10:54:46）和8分钟（11:33:47–11:41:47）。设备仅 A42BBB5109000333，包仅 org.ffrs.gamewindow，保留数据 install-r。首次进入战斗，按教学先雷魔法，再暗魔法、Limit；未暂停错相位抓帧。10:54:21先释放；第二窗复用旧 replay-ubo.apk 导出，11:40:27已恢复 normal-capture.apk，后核设备 APK SHA仍8c0...，四项 GPU 设置null、marker0、capture trigger false、无游戏PID。没有清数据/卸载/改存档/碰FC或RG。

新文件都在 `.local/honor-device-profiles/flame-20261104/dark-normal-capture-20261107/`，设备原件保留：

| 捕获 | frame | bytes | SHA256 |
|---|---:|---:|---|
| gfxr-normal-20261107-1040_trim_trigger_20261107T104702.gfxr |8918|4194966621|4dd69d6957e40286c43db3b806fe7c00c4428096ca84789930e30330a1756aab|
| gfxr-normal-20261107-1040_trim_trigger_20261107T105303.gfxr（Limit）|16817|5740429343|7d755c1631042b7c5367be1c3ab39848be99e72a3fc41c7f83e08311b1570b40|

均完整到文件 EOF/frame END；每文件实际1个GFXR frame，不以属性capture_trigger_frames=2冒认2帧。`audit_markers.py`只解析容器边界、不重建GPU字节。新暗魔法StateEnd block1381591，真正实时录制pair为pre draw1383759/cmd551/submit1384156，color draw1384398/cmd608/submit1385791：pipeline871为GEQUAL/write，pipeline872为EQUAL/no-write+stencil；四个实际模块sh868/sh869及sh867/sh870与旧模块字节hash分别完全相同。故新普通路径EQUAL已获实际录制及提交证据，仍有黑块；不能称移除ALWAYS就是修复。

## 本次导出未命中的精确原因和下一步

第一次错误选择 pre1379909/color1380385，它们虽在StateEnd之后被提交，但录制动作本身在StateEnd之前，属于恢复快照中的CB。现有资源导出器未命中，目录0项，replay正常Done；日志 `normal-replay-device.log`。这是目标选择错误，不是新捕获没有可用pair，也不是需要重抓。临时“只有1帧所以不够”的解释已撤回，`container-markers.json`证明后一对1383759/1384398在实际录制区。`normal-target-draws-preliminary.json`为未作state阶段过滤的中间产物，以 `normal-target-draws.json` 的phase/dump_eligible为准。

已准备 `normal-live-draw-indices.txt` 和 `run-normal-live-device.py`，精确CB/RenderPass/submit均已从同一trace抽取：
`BeginCommandBuffer=1382183,Draw=1383759,BeginRenderPass=1383753,EndRenderPass=1383761,QueueSubmit=1384156`
`BeginCommandBuffer=1384168,Draw=1384398,BeginRenderPass=1384392,EndRenderPass=1384416,QueueSubmit=1385791`

下一步只需当前设备窗口授权后，复用已有 replay-ubo.apk install-r、运行该脚本、验证并拉取对应GPU导出、恢复8c0普通包，不重建工具、不重新战斗。4分钟窗口请求待用户答复。旧30切片已完成；新8918切片尚未导出，不能用旧179项填充。

## 首错、view、Limit和音频边界

- `normal-resource-matrix.json` 为新捕获元数据，不能冒充GPU字节；binding18 R32_UINT/range2仍在。未盲扩range，未冒认view根因。VUID00929来源见OSS-NOTES。
- 两个旧实际VS仅保留Position写出的离线分析变体 spirv-val通过，依赖binding6/12/18/24/30/36/42/54/60/66/72/78/84；不是GPU Position读回，未用于游戏。binding0/48在此静态Position切片被消去；不证明动态分支是否执行。
- FIRST_WRONG仍未定位：缺新实时pair的GPU附件/输入执行对照与Position→coverage/discard→FragDepth/depth→color/blend逐阶段输出。旧f15768颜色419 samples纠正及binding18同帧0000结论不变，不外推共因。
- Limit新frame16817已拉取且完整EOF，pipeline/实际shader独立离线解析中；GEQUAL尚未从这次目标draw独立确认，不套用暗魔法结论。截图 `limit-before-trigger.png`/`limit-after-trigger.png`只含前段，不能代表蓝冰晶阶段完整验收。
- `gfx-codex-probe=4`请求未产生该进程探针END/目录，不能声称本次原始最终图采样与重放一致；旧f15768的9216点一致仅旧帧有效。
- 音频常设门逐项仍未完成：A2DP野外/战斗各≥2分钟耳听、运行中蓝牙断开重连重开流、TIMEOUT/DISCONNECTED恢复、缓冲/桌面包身份等未完整验收。源码仍设置AAUDIO_PERFORMANCE_MODE_NONE；新日志performance=10是平台枚举值，不能自行将其认作LOW_LATENCY（常设文本performance=0与平台日志口径需区分）。未抬帧/改音频。遮挡/shield/fire/boss与独立QA均NOT_RUN。

# Honor 暗魔法 / Limit 离线恢复 — 2026-11-07

## 状态

`FIRST_WRONG=NOT_FOUND`，`CAPTURE_COMPLETE=NO`，`VIEW_VALIDITY=NOT_FIXED`，`VISUAL_DARK=NOT_RUN`、`VISUAL_LIMIT=NOT_RUN`、`AUDIO=NOT_RUN`。本轮未操作平板、未安装/冷启、未下载或重建现有捕获工具。HEAD `032e5f68172c101cb2b6e44ee06f02974dab2da8`、分支 `work/honor-gtpro-30hz`，认领既有 dirty，无提交/push。

## 正常深度状态

从 `src/graphics/nvn_vulkan/engine/xbg_game.cpp` 普通路径移除原有按 shader 列表强制 `VK_COMPARE_OP_ALWAYS` 的代码，并移除仅服务该覆盖的 memo 禁用条件，保留显式 opt-in 的 probe 诊断分支。移除前文件 SHA `598bd64252b1b250a15810407e20613650ffe3871a5699371beb11ef98955f04` 备份 `xbg_game-before.cpp`；独立原覆盖片段 `disabled-always-original.inc`；当前文件 SHA 在当前工作树以 `shasum -a 256` 为准。正常路径沿用 `VkCompareOp(state.ds.df-1)`：旧 f23465 暗魔法 color 对应 state `depthFunc=3`、NVN EQUAL，即 Vulkan EQUAL；旧 f17444 Limit pair color state `depthFunc=7`、NVN GEQUAL，即 Vulkan GREATER_OR_EQUAL。没有把这两次不同旧帧混成同帧，也没有硬编码特定 shader 的深度值。`pipe_memo_fill` 把包括 ds 在内的 state 片段放入 memo key，静态核对不同深度状态不应命中相同 memo。

f15768 的实际 color draw2806351 使用 pipeline2487 ALWAYS；已捕获但当时未绑定的 pipeline876 EQUAL 与2487 的 VkGraphicsPipelineCreateInfo 递归比较仅 `depthCompareOp` 一项不同。该比较仅证明当帧存在合适的 EQUAL pipeline 资源，可用于未来重放反事实实验；不能证明改变绑定后视觉正确或独立 Limit GEQUAL 已在 GPU 运行。

## UBO/SSBO 捕获修订

隔离 GFXReconstruct 源 `.local/honor-device-profiles/flame-20261104/dark-limit-capture-20261106/gfxreconstruct-c081f1b/framework/decode/`：

- `vulkan_replay_dump_resources_draw_calls.cpp` 对 shader stage / set / binding / array element 的每个 buffer descriptor 记录实际有效 offset/range，分别 GPU 读回；不因 buffer 指针相同或前一 draw 使用过就遗漏不同范围。针对 VK_WHOLE_SIZE 和越界进行检查，异常返回错误；texel 全 backing 数据仍沿用原诊断路径。
- `vulkan_replay_dump_resources_delegate.h/.cpp` 为每个 draw descriptor slice 提供包含 draw/stage/set/binding/element/offset/size 的唯一文件名，JSON 同时写入 effectiveOffset/effectiveRange，消除原来多个 binding 引向单个480字节文件的错误。
- 发现并修正独立的偏移排序错误：原 `BindDescriptorSets` 遍历 `std::unordered_map` 消费动态 offsets；现按 binding 升序、array element 顺序处理，并对数量不足/整数溢出报错。这里不同于旧 UBO 按 buffer 指针去重问题，未修正会使逐绑定文件仍读错位置。
- 变更前第三方源 `draw-calls-before.cpp`、`delegate-before.cpp`、`delegate-before.h` 保存在本目录；原上游版本另保存在旧 capture 目录。没有修改既有 `gfxr` 或旧 dump。

从 f15768 实际两次 `vkUpdateDescriptorSetWithTemplate`、模板4739094/4739156及 `vkCmdBindDescriptorSets` 抽取并按规范顺序计算 `ubo-descriptors.csv`，含14项 pre 与16项 color（后者包括1个静态 storage buffer），30个 stage/binding 标识唯一。示例 pre vertex binding84 effectiveOffset1986048/range480，vertex binding78 offset1952256/range6048；color fragment binding10 storage offset253696/range65536。**这是 descriptor 地址清单，不是这30段 GPU 字节**；仍不能从旧480/256B错误导出中拼造完整数据。CSV SHA `a81f4c5a5a042c8d17a453c85fa58125a34a912bb924650253ceaa591519a512`。

## 离线验证与下一步

- Android NDK26 `clang++ -fsyntax-only` 对修改后的 `xbg_game.cpp` 退出0（原有5条 warning）及隔离 replay 两个 .cpp 退出0；日志 `xbg-syntax.log`、`syntax-check-final.log`。首次语法检查发现局部名字错误，修正后复验通过，初始错误保留 `syntax-check.log`。没有产生新可执行文件或重建工具。
- `verify_offline.py` exit0：核对 NVN enum 到 Vulkan 的静态数值映射、正常路径无无条件 override、memo 包含 ds、pipeline876/2487差异、30项 identity 唯一以及代表性捕获 offset/range。它是静态/捕获元数据核对，不是 GPU 执行测试。
- 未在新源码上运行真实 draw；现有 f15768 是实际 ALWAYS、binding18 R32_UINT/range2 非法，不能视为 EQUAL 修复前后对照。旧 pre/color 覆盖差异、query数和输出无法证明 POSITION、discard、depth 或 color 哪个是首错。
- 要补齐真实 GPU 可见 UBO 并继续首错定位，需先构建经修订的隔离 replay（前一轮用户明确禁止重建工具，本轮未自行解除）、准备同包保留数据测试，再在获得用户设备操作授权后 install-r/重放；真实 EQUAL/GEQUAL 验证及 marker0 视觉、音频常设门需另做真机验证。用户本轮明确暂不操作平板，因此只报告，不触碰设备。最后一次已知设备状态为前次交接的 diagnostic APK / marker0 / 无进程，本轮未复核。


# Honor f15768 离线核查

## 关键纠正：本帧并非 EQUAL 对照

原始 f15768 trace 的 draw2805615 绑定 pipeline872（depth GREATER_OR_EQUAL，写深度）；draw2806351 绑定 pipeline2487（depth ALWAYS，depthWrite=false，stencil=true）。两个 pipeline 的动态状态只有 VIEWPORT/SCISSOR，所以 color depth compare 不是动态替换。见 `../dark-limit-capture-20261106/paired-target-calls.jsonl` 与 `paired-draw-near.jsonl`。当前既有 dirty `src/graphics/nvn_vulkan/engine/xbg_game.cpp:3934` 限定若干 spell/Limit shader 且 depth test 开启/不写深度时，无条件把 compare 改为 ALWAYS；apk `dark-gpu18-20261106.apk` 的 native library 也含日志字符串 `NVN spell/limit color pass depth EQUAL->ALWAYS override`，与抓到的 pipeline 一致。此代码和 APK 均为接手前既有状态，本次未改。捕获中真实 GPU 的 ALWAYS 是已证事实；本帧 guest 输入值是否为 EQUAL 仍未被该 trace 单独证明，且该覆盖不是可接受修复。此 capture 不能用作 EQUAL 原始路径的可信对照，也不能说明这个覆盖就是黑块首因。

## BC4 有效块

根据现有 replay 源 `graphics/vulkan_resources_util.cpp` 的 `GetImageResourceSizesOptimal` / `CopyImageBuffer` 和 `decode/vulkan_replay_dump_resources_common.cpp`，BC4 raw staging 按各 mip 内存要求预留，copy 设置 bufferRowLength=bufferImageHeight=0，图像每个 mip 的前缀按 4×4、每块8字节解释；更大的 allocation 尾部不是纹理有效块。f15768 两个 draw 的 vertex binding42 均为 image1011076，512²、10 mip、view1015207、identity swizzle、mip0..9/layer0。根据实际 descriptor，sampler10715：linear min/mag、nearest mip、U/V/W repeat、LOD 0..FLT_MAX。有效块依次 131072,32768,8192,2048,512,128,32,8,8,8 字节；两次 replay 的全部10级前缀及 SHA 均一致，见 `bc4-matrix.txt`（含整文件和有效前缀哈希）。`scaleFailed` 不说明这些 mip 缩放失败：本次 scale=1.0，源代码只给 `scaling_supported[i]`（按 aspect）赋值，delegate 却按每个 mip 文件索引读取；后续布尔项未经同样赋值。该判断只描述工具输出布局，不能取代原运行 GPU 逐 mip 对照。

## 生命周期和覆盖边界

capture 恢复状态中的 image1011076 在 block1260 创建，block2571 为 memory1011077 分配184320字节，block5138 以偏移0绑定，block33836 创建 view1015207；两个 draw 的 descriptor update 分别在2805608/2806344，绑定该 view 和 sampler10715。预通道 submit2806053 发出 semaphore16601，颜色 submit2807840 等待它。常规 Vulkan 过滤结果未发现该 image/memory 的销毁、重新绑定或直接图像写命令；这不排除 state-snapshot 初始化块、描述符驱动的写入、其他别名或未覆盖操作，不能声称资源世代完整证明。原始数据来源和 alias 审计仍需继续，见 `generation-filtered.jsonl` 与 `trace-filtered.jsonl`。

实际两 shader binding18 对应 view24419（buffer24359，offset2048）及 view36856（buffer36780，offset20224），两者 vkCreateBufferView 为 R32_UINT/range2，违反 range multiple 规则；同帧重放 GPU backing 每侧对应有效2字节均0000。仅证明本帧有效字节相同，不证明其他值或类型语义，不能盲扩读取范围。见 `paired-draw-near.jsonl`、`trace-filtered.jsonl` 和 `paired-dump.log`。

更重要的是 UBO dump 覆盖不完整：f15768 pre 有10个动态 UBO 描述符引用 buffer2823 的**同一个480字节文件**，color 有11个引用 buffer2675 的**同一个480字节文件**；每侧 buffer2589 又有4个描述符引用同一个256字节文件。输入描述符范围包含544、4096、6048等，不可能从这些文件覆盖完整值。代码 `vulkan_replay_dump_resources_draw_calls.cpp` 的 `DumpImmutableDescriptors` 以 `VulkanBufferInfo*` 去重并保存首次遇到的 offset/range，delegate 的文件名只包含 buffer ID/submit/command/rp，JSON 把同一文件指给多个绑定；不能把152项当全部 UBO。需要能按每个真实 dynamic offset/range，或整个 backing buffer，于同一 draw 前重新读取后交叉校验。用户本轮禁止操作平板和重建工具，因此当前本地这152项无法补齐缺失 GPU 值。

## VK_NOT_READY 与输出

离线 trace 中 131 个 `vkGetQueryPoolResults` 的 flags 均 `0x1`（64-bit，不含 WAIT），recorded 都返回 VK_SUCCESS。插桩 `paired-dump.log` 有33条 replay VK_NOT_READY / captured VK_SUCCESS；replay `OverrideGetQueryPoolResults` 对 SUCCESS→NOT_READY 最多立即重试16次后返回，无显式等待。结果与异步时序差异相容，但日志不含每条警告对应 pool/index，不能证明33条具体原因，也不能从 warning 推断样本数为0。两目标 query：pre pool24379/query3 仅覆盖目标 draw；color pool36747/query18 从2806346到2806362，除目标2806351还跨后续三个 pipeline314 draw，因此它不代表单独 color draw。此离线 trace 的131条读取中没有这两个 query pool 的 GetQueryPoolResults，不能恢复其两目标样本数。

`attachment-diff.txt` 为 f15768 限域 replay before/after 紧密有效区比较：pre color/depth 各52312个像素变化，bbox(341,0)–(771,162)；color attachment0 有606像素变化，bbox(552,733)–(577,761)，其他 color/depth/stencil 未变；pre 后与 color 前同 depth image2967 有效区逐字节相同。仅为插桩 replay 的附件变化，不等同覆盖率/全部片段结果；两 shader 和任务本可覆盖不同，且 color 是 ALWAYS，故这些数字不能判断第一个错误 GPU 输出。POSITION / coverage-discard / FragDepth-depth fail / color-blend 仍缺可靠同 draw 输入、原图附件比较和 Switch 客体语义。FIRST_WRONG=NOT_FOUND、CAPTURE_COMPLETE=NO、VIEW_VALIDITY=NOT_FIXED，Limit/视觉/音频 NOT_RUN。

## 后续最小工作

先处理当前源中的 ALWAYS 强制路径（保留和记录旧 dirty WIP，不能把 ALWAYS 当修复），建立真实 EQUAL/Limit GEQUAL 状态；修复或替换 replay 的 buffer 范围去重，逐 binding/offset/range 保存 UBO，并补对应目标 draw 的原运行完整 color/depth/stencil 前后对照；随后按 Position→coverage/discard→FragDepth/depth→FS color/blend 受控定位首个错误输出。本轮只做离线读取、trace 转换、数据分析与报告，没有改游戏源码、重建工具、下载或对平板下命令。


# f15768 同 draw 资源导出补记 — 2026-11-07 02:50 Asia/Shanghai

隔离 replay `replay-paired-probe.apk` 成功对 f15768 的 draw2805615 / 2806351 做 before+after、vertex/index、全部子资源和 raw stencil 资源导出：`gfxr-paired-draw-20261107/` 共152项，文件大小与 SHA `manifest-paired-draw.txt`，日志 `paired-dump.log`。两个 draw 前参数均 6960 indices ×2 instances；index 13920 bytes SHA `dadada60422d48b2405e144edaf1b05039bf4725f6f98b83fab08e790048833a`，vertex 15540 bytes SHA `9a0c77061e6d045eafd1e4deaccb1b68ad9a832bb3c537ebd974ff5733d57028`。与原运行**最终图采样**一致的同一 frame 对应，但导出值仍是重放结果，并非原运行同 draw 全附件独立对照。插桩重放出现 `vkGetQueryPoolResults` 重放 VK_NOT_READY / 录制 VK_SUCCESS，仍须解释；不得将任何阶段判作首错。

设备现场：同包保留数据恢复安装诊断 APK SHA `55b241b1c6b391f7a9d5060ac1f61594b94920d2aae2db517b7a031ef29ce6cd` 成功；`gfx-codex-probe=0`，四项 app-target GPU debug settings 为 null，capture trigger false，capture 配置已清空；当前无 `org.ffrs.gamewindow` PID。原始设备 trace 和 app 文件保留。恢复安装不等于普通路径视觉或音频验收。

# Capture 本刀 — 2026-11-07 02:46 Asia/Shanghai（未完成，历史保留）

**CAPTURE_COMPLETE=NO**（暗魔法部分捕获；Limit 未捕获）；**VIEW_VALIDITY=NOT_FIXED，VIEW_VALIDITY_FIXED=NOT_FIXED；FIRST_WRONG=NOT_FOUND；VISUAL_DARK=NOT_RUN；VISUAL_LIMIT=NOT_RUN；AUDIO=NOT_RUN。** 没有交付黑块修复。独立 QA、遮挡、shield、fire、boss 回归均 NOT_RUN。

工作树 HEAD `032e5f68172c101cb2b6e44ee06f02974dab2da8`、`work/honor-gtpro-30hz`，继承全部 dirty/untracked。本刀未改游戏源码；自有变更为本刀 `.local/honor-device-profiles/flame-20261104/dark-limit-capture-20261106/` 的隔离 GFXReconstruct 源、构建、脚本和证据，以及 RESULT/HANDOFF/state/生成 STATUS、`.local/honor-device-profiles/OSS-NOTES.md`。未 commit/push。仅 `adb -s A42BBB5109000333`、同包 `org.ffrs.gamewindow` 保留数据 `install -r`，未清数据或卸载。

同帧暗魔法捕获 `gfxr-paired-20261107_trim_trigger_20261107T021830.gfxr`，8580643727 bytes，SHA256 `71a32d7a4397ec289582ff6bdeeea2df023392714be680341100ffb32430325a`，捕获完成实际2帧 15768–15769。f15768 pre pipeline872 / VS sh869 SHA `98a757a3847059a215a3c0e4fb6a4a8fd01126339d2c74590b7bce8805e5a824`、FS sh871 SHA `4aeb46118d54f664ad5d5cb116a83c15cda1fa25637d3d61f57cd8f059e56f0f`；color pipeline2487 / VS sh870 SHA `161df822093faebfafea89b18699514530c40dbc5dc179dc8b9d945d9e0915a9`、FS sh875 SHA `c9de616a2536b41e2f3e83b68c65cf983da9e1da8a600f07943baf2cc25c952f`。目标 pair pre draw2805615 / cmd767 / submit2806053；color draw2806351 / cmd835 / submit2807840，后者等待前者信号；索引与原始命令见 `paired-target-calls.jsonl`、`paired-draw-near.jsonl`、`paired-draw-indices.txt`。

原应用 marker4 同帧同步 GPU 读回最终图：`codex-probe-24968/capture.txt` 有 END，1920×1080 RGBA 的 128×72 采样 36864 字节 SHA `ac2ddf6f06ed6e385e584946efea9c07bfb308fef011b30ef2cb31c4f800a237`。原始录制的 vkCmdCopyImageToBuffer block2808871 / cmd2545 / submit2808980；隔离 replay 等该 submit 原有 fence 后读取对应 GPU buffer7317，完整图 8294400 字节 SHA `de3ec66926ba035d3887f74e6d8385edcb32d79d98e08f4f5908997d422cdd27`，按原坐标采 9216 点，**36864/36864 字节一致**。见 `paired-probe-replay.bin`、`replay-paired-probe.log`、`paired-original-sample.png`。该证据仅覆盖最终图采样，读回会扰动，不证明完整图或同 draw 附件逐字节一致，也不消除非法 view 造成的未定义行为。

| 同帧资源 | 已有证据 | 缺项 |
|---|---|---|
| draw/shader | f15768 两实际模块和目标 pair；f23783 GPU replay draw 前 indirect/顶点/索引 | f15768 两 draw 已导出 replay 资源，引用范围待审；资源重用世代和同步仍须证明 |
| UBO/texel | f23783 两侧 replay 的 6 个 texel view、backing buffer、offset/range，以及 descriptor/UBO dump | 原运行 draw 前 GPU 字节独立对照、所有动态范围、f15768 动态范围和 view 合规仍需核实；f23783 R32/range2 非法 |
| 纹理 | GFXR trace 和 replay image/mip 导出；binding42 BC4 10 mip | 导出大小来自 optimal-image 内存要求，拷贝使用紧密行列，尾部可含填充；有效块、所有可访问 mip/view/sampler 与内容需独立核对 |
| color/depth/stencil | f23783 replay 的前后 raw，其中 stencil 来自隔离 replay 补丁 | 原运行同 draw 完整附件前后独立对照、重放分割与同步有效性未闭合 |
| 最终输出 | f15768 原运行和 replay 最终图 9216 点完全一致 | 同 draw Position、coverage/discard、FragDepth/depth fail、color/blend 与可信参考尚缺，首错不能判定 |
| Limit | 旧候选 pair + GEQUAL | 本刀未独立捕获和验收，不能假设同根因 |

f23783 `gfxr-20261106_trim_trigger_20261107T001546.gfxr` SHA `bc228e2937c64fa5f653918edf954526baa361cb50ccf7ef71405e8fa6aa137e`，实际 draw3315360/3316109。隔离 replay 修复 indirect 参数副本在 before 边界读得过早、补 texel view/backing 关联、开启 raw stencil；目标 draw 前 indirect 均 6960 indices × 2 instances、index 13920 bytes SHA `dadada60422d48b2405e144edaf1b05039bf4725f6f98b83fab08e790048833a`、vertex 15540 bytes SHA `9a0c77061e6d045eafd1e4deaccb1b68ad9a832bb3c537ebd974ff5733d57028`。数据是 replay GPU 值，不冒充原始 GPU 独立快照；`gfxr-draw-before-vb-20261107/`，size/hash `manifest-extended-sha256.txt`。

f23783 binding18 views6262904/5922581 实际 R32_UINT/range2 违反 VUID 00929，offset17664/2816；有效两字节 `0000` 仅证该帧两侧相同。Adreno735 设备查询支持 R16_SFLOAT uniform texel，alignment64，两 offset 对齐。仅 replay 限定这两 view 改 R16、替换两 shader 为只重写 binding12/24 保留 binding18 float 的变体；最终两 replay 截图逐像素一致 SHA `0af5ab1e5b3647e1e960e3cc624ff8d047bfb0cb264c180fb16821d9f4c904f4`。原非法 view 的 UB、非零/负零/边界及动态 descriptor 尚未闭合，不能当正式修复或根因；旧 R16 也黑，不能盲扩 range。旧 f23465 pre749147 / color419 均有 samples，不是 color 恒0。

GFXReconstruct 官方 Android `vulkan-sdk-1.4.309.0` c081f1b、MIT-style，参考 `.local/honor-device-profiles/OSS-NOTES.md`。原 diagnostic APK SHA `55b241b1c6b391f7a9d5060ac1f61594b94920d2aae2db517b7a031ef29ce6cd`；capture layer 同包 APK SHA `e0d5babdf6db5a10286b00e843f2fcb69b6f6014500a1ea75a52cd90f2fb5573`；最终样本字节对照 replay APK SHA `d9c402666a1d098ee916be09e4bef925a364880765b428bee5aaf9006535a98f`。`paired-info.log`、`paired-extract.log`、`paired-original.log`、`replay-paired-probe.log`、`paired-draw-indices.txt`、`filter-paired*.py` 保留。Capture 使用 unassisted memory tracking，producer/consumer 同步和资源世代还需证明；GFXR 改动仅限 `.local/.../gfxreconstruct-c081f1b`，原文件存 `*-upstream.cpp`。

最小下一步：核实 f15768 两 draw 的资源导出与 BC4 有效块；增加原运行同 draw 完整 color/depth/stencil 前后读回并与同一重放比较，安全解决 R32/range2 的类型语义与边界，再按 Position → coverage/discard → FragDepth/depth → color/blend 定位具体首错；Limit 独立抓取验证。Switch 照片用于结构和颜色，不逐像素比对。

音频常设门逐项状态均 NOT_RUN：默认 AAudio PERFORMANCE_MODE_NONE/真实日志、A2DP 野外≥2分钟和战斗≥2分钟各自耳听、蓝牙连接断开自动重开、fence_spin_us=0 运行确认、TIMEOUT 重试、DISCONNECTED 恢复、缓冲厚度、听感与 xruns 综合、桌面图标对应包。包名及 APK SHA 如上。没有音频主观验收、不称 PASS；本刀未抬帧或改音频。

# 授权设备后续核查 — 2026-11-06

用户确认 Honor 已连接且空闲授权。本次未安装/冷启/输入：设备只读复核 PID28053、marker0；没有捕获层可用的证据。主机 PATH、`/Applications`、Android SDK 的有限路径检查未发现 RenderDoc/gfxreconstruct/AGI；设备 `/data/local/debug/vulkan` 与 `/data/local/tmp` 的相应名称无输出，`ro.debuggable=0`，GPU debug layer 全局设置为空，现有 manifest 标记 app debuggable。上述检查不证明无法安装外部工具，也不证明完整捕获不可实现。

离线限定变体：针对暗魔法两 VS 仅对 binding12/24 使用现有 uint 重写，保留 binding18 原 float 声明。pre 变体 `5937617587b19ce3fa7a093a0de249906fc5586f6ed1ae5db7c612ff85365dbc`、color `ec82f5040fcc81e78b8041bb8c1e88e339a91886b8fc105e29a26b6325d2eccf`；`spirv-val --target-env vulkan1.1` 均 exit0。color 反汇编确认 binding18 保留 float Buffer，12/24 发生整数重写；测试和产物 `.local/honor-device-profiles/flame-20261104/dark-next-20261106/rewrite-12-24.cpp` 与同目录 SPV。这只证明 shader 结构合法，并未验证设备 format features、实际 view 绑定、所有动态输入、实际 Vulkan 输出或视觉，也不是已安装修复。现有捕获中小 binding18 原路径 28 条是 vk76/2，当前重写路径 2 条 vk98/2；该样本不证明其他场景或 null descriptor 恒为 R16。shader 在加载时固定、资源绑定在后，简单全局删两项而不保护其他绑定不能作为有完整语义证明的修复。

**VIEW_VALIDITY_FIXED: NOT_FIXED。** 缺口：需要为实际 binding18 建立来源格式、范围与 shader module 的逐 draw 一致性，包括数据/生命周期/同步和有无非 R16 动态情况的安全处理；且同帧完整 GPU 资源与输出捕获能力尚未建立。需要在项目隔离目录准备支持 Android Vulkan 的帧捕获或限定探针，进行实际 R16 格式特性/边界、正反输入、shader/descriptor 及 GPU 输出验证，再做正常路径修复；不能以离线 spirv-val 替代真机，也不把孤立变体安装算验收。首错、暗魔法、Limit、遮挡、音频、独立 QA 均 NOT_RUN。

# NEXT 离线复核 — 2026-11-06

HEAD `032e5f68172c101cb2b6e44ee06f02974dab2da8`；本 NEXT 无运行时代码修改、无构建 APK 或安装。自己的新增物仅 `.local/honor-device-profiles/flame-20261104/dark-next-20261106/` 的离线重写工具/产物，及 RESULT/HANDOFF/state/生成 STATUS。原 dirty/untracked 保留。实时只读检查：Honor `A42BBB5109000333` device、包 `org.ffrs.gamewindow` 前台 PID28053、marker0；未施加输入。

## 同帧资源覆盖矩阵

| 资源 | f23465 g80 pre / g96 color | 可见性与缺项 |
|---|---|---|
| draw/instance | 各 6960 indices、2 instances、baseInstance0；pipeline 部分状态及 query 记录 | 日志不含完整可重放 pipeline、资源世代或提交依赖 |
| VS/FS UBO | g80 14 个、g96 15 个快照，分属本帧 | 已拥有 uniform/support CPU 快照；是否覆盖 shader 全部必要读取及与 GPU 一致须验证 |
| texel | 两组各 6 项：12/18/24/30/36/48，已完成 GPU copy/fence，18 有效两字节 0000 | 该帧两侧相同不证明其他读取/格式语义正确；probe 数据属插桩路径 |
| mesh/index | 两组各 15552/13920 字节 | CPU mapped prepared buffer，GPU 可见性未证；需对应 draw 前 GPU 复制 |
| binding42 | 两组同一 serial 1371297、vk139 BC4、10 mip，view 记录 | 旧采样不是完整 BC4 块，缺全部可访问 mip 原始块及 sampler/同步状态 |
| 其他纹理与 color/depth/stencil | 记录部分 view 和 128×72 缩样 | 缺 draw 前后完整关联附件、完整可采样纹理与 stencil；不能重放/逐像素判断 |
| shader | 原始四套 SPIR-V 已存；见 dark-depth-pair | 下文为基于当前源码的离线重建，不是实际运行 module 的 dump；驱动实际输入还须验证 |
| 输出 | 精确 query g80 749147/g96 419；有抽样图 | 无每顶点 Position/关键 varying、FS kill/FragDepth 或逐像素 color/blend；不能判首错 |

旧 f9558、f10564、f8115 与本帧互为独立证据，未拼接。

## binding18 语义与合法性

原始 VS SPV SHA pre `a336f142adf5f246f7495e3a37a699da5269c889294d90e32c5b30edc013e09c`，color `c281eb188e3cb918da123f5cfc8c5ea8b0bd2032e8032eb0be2fb7ec4f6cb4b1`。原始 color 的 binding18 是 float Buffer，示例 fetch 后 extract float，再 OpBitcast int（原 `.spvasm` 3503–3518、3564–3574）。本次调用现有 `xbg_spirv_ffrs_stencil` 按当前表重写，pre 97、color 51 处，`spirv-val --target-env vulkan1.1` 两者 exit0；重建 color binding18 是 uint Buffer、uint fetch 后 bitcast，SHA `161df822093faebfafea89b18699514530c40dbc5dc179dc8b9d945d9e0915a9`；pre SHA `98a757a3847059a215a3c0e4fb6a4a8fd01126339d2c74590b7bce8805e5a824`。这是离线重建，不声称与设备实际 module 字节一致。设备 f23465 probe 记录 resident、vk98、range2；代码 `texel_acquire` 以 fmt、range 建 view，memo key 含 base/bytes/format，VK_FORMAT_R32_UINT 4字节块配 range2 违反 [VUID 00929](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferViewCreateInfo.html)。捕获 4 字节是独立诊断 copy，渲染 view 仍为2字节；offset alignment、设备 format feature 和实际 module 未完整核验。

反控制：half `3c00` 经 R16_SFLOAT 转 float32 是 `3f800000`，零填充 R32_UINT 为 `00003c00`；负零 `8000` 分别是 `80000000` 和 `00008000`。`0000` 只是碰巧相同。简单填充/换格式不能证明语义。原始翻译是否忠于 Maxwell、越界读取与动态 descriptor 如何处理尚未验证，不能安全将表项直接删除或一律半浮点转换。**VIEW_VALIDITY_FIXED: NOT_FIXED**；相关实际格式/范围/采样语义先需闭合。旧 f10935 为 R16 仍黑，不能将新风险认定为旧症状首因。

## 最小活体捕获计划与状态

在已授权 Honor 上，只触发一次或必要的有限重试、确认目标两 shader 对同一活动帧出现且 END；在 draw 前以 GPU copy/fence 保存顶点/索引、texel 与全部有界读取及同步世代；对 binding42 保存所有实际可达 BC4 mip 原始块、view/sampler，保存全部参与的纹理和 draw 前后完整 color/depth/stencil，记录 pipeline、索引/instance 和实际创建 shader SPIR-V/descriptor（格式、range、offset、feature 与 alignment）。现有 opt-in probe 不支持这份完整清单；须先实现或找到真实 GPU 帧捕获工具并对其完整性做负控制。不能凭现有缩样称可重放。随后先验证 Position/覆盖，再依序判 discard、FragDepth/depth、color/blend，且与可信客体语义对照；两 pass 差异不自动是错误。当前没有该能力的经过验证的捕获实现，因此未再制作孤立探针包。

首个错误 GPU 输出、暗魔法视觉、Limit 独立定位/视觉、遮挡/shield/fire/boss、音频及独立 QA 均 NOT_RUN；未有修复前后同条件对照，整刀未通过。音频按 `_STANDING-audio-acceptance.md` 全项 NOT_RUN。参考照仅结构/颜色。下一步唯一阻塞是无法获得同一 draw 的完整 GPU 输入与实际 shader/附件用于可信重放；须先补齐上述完整性核验的捕获能力，再修正/验证非法 view 的真实数值语义，不能由仅两字节为零推断正确补丁。

# 2026-11-06 更新：Honor 同帧 GPU binding18

用户授权设备调试并提供 Switch 原版照片。诊断 APK SHA256 `55b241b1c6b391f7a9d5060ac1f61594b94920d2aae2db517b7a031ef29ce6cd`，覆盖安装成功，无清数据；Continue 返回教程战斗。PID28053 施放暗魔法，marker6 后恢复0。`.local/honor-device-profiles/flame-20261104/codex-probe-28053-f23465/` 完整 END：g80 预通道 749147 samples、g96 颜色 419（VK_SUCCESS、available1），各 binding18 GPU 拷贝有效两字节均 `00 00`，只证明该帧读取的两个值相同。编译日志 `dark-gpu18-20261106-build.log` exit0；诊断源码为 opt-in `xbg_probe.inc`，当前 dirty，`git diff --check` exit0。原有 dirty WIP 保留。

**新风险线索，非已证根因**：该次捕获 binding18 报告 VkFormat98（R32_UINT）、描述符范围2字节；resident 创建路径 `texel_acquire` 使用该格式和范围直接创建 view。依 [Khronos VkBufferViewCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferViewCreateInfo.html) VUID 00929，显式范围须是格式块大小的整数倍，2 不符合 R32 的4字节块。原始着色器是 float Buffer，当前 WIP 对两 shader binding18 的整数重写影响实际 SPIR-V，需要单独核对；并且旧 f10935 使用格式76（R16_SFLOAT）时黑块已经存在，不能把新风险当作历史根因或直接宣称已修复。还需同帧输出检查。

用户照片转换只读副本 `.local/honor-device-profiles/flame-20261104/switch-reference-20261106/01.jpg`–`05.jpg`：前两张暗魔法蓝白亮核、紫红外圈；后三张有 Skip 提示的大招阶段，含紫色背景、红蓝光柱与蓝色冰晶。拍屏反光/曝光与非严格同帧限制逐像素对照。原 HEIC 未修改。binding42 vk139 是 BC4 压缩格式，旧按普通像素的 probe sample 不能作为纹理完整或可靠重放内容。

# P-CX-TERRA-dark-limit-black — BLOCKED / 2026-11-06

工作树 `work/honor-gtpro-30hz`，HEAD `032e5f68172c101cb2b6e44ee06f02974dab2da8`，原有大量 dirty/untracked 已保留。本次未改源码、未构建、未操作设备；无新 APK、无修复前后同条件对照。游戏版本未重新核实；既有运行及 APK 信息以 HANDOFF 对应记录为准。

## 离线结论

- 现有同帧捕获不能作为完整可重放输入：`codex-probe-31699-f9558` 包含成对 UBO；`codex-probe-3882-f10564` 包含成对 GPU texel 12/24/30/36/48；`codex-probe-1031-f8115` 的顶点/索引是 CPU 映射快照。它们是不同帧，不能拼成同一时刻；binding18 的 2 字节未获 GPU 可见证明，binding42 图像内容/视图也未完整保存。采样附件不是完整重放附件。缺口是**同一有效帧的完整 GPU 可见重放状态及输出**，因而不能判断第一个错误输出。
- 四个原始 shader 在 `.local/honor-device-profiles/flame-20261104/dark-depth-pair/`。静态检查：预通道 FS `c0adc062e4b8e8fa.spvasm` 有条件 `OpKill`（第 1610 行），颜色 FS `b7dd0f48aea00196.spvasm` 未出现 `OpKill` / `OpTerminateInvocation` / `OpDemoteToHelperInvocation`，两者都有 `FragDepth` 写入。这不证明预通道丢弃错误、两 pass 覆盖必须相同，也不证明颜色深度值错误。
- 先前 f10564 pre 1036971/color 0；f8115 pre 648976/color 0，均为现有记录，非本次新测试。禁用 depth/stencil 的旧探针仍覆盖少且可能扰动，不足以唯一归因。Limit 的另一 shader 对及 GEQUAL 未独立定性。

## 唯一阻塞与最小下一步

设备是否空闲未知。需协调后，在 Honor `adb -s A42BBB5109000333` 上一次活体暗魔法同相位捕获，确保目标两对 shader 同帧出现；以 opt-in 诊断完整记录 GPU 可见几何/索引、全部 UBO/texel（包括对齐读回 binding18）、binding42 图像及视图/采样器、相关纹理和深度附件、pipeline/draw 状态与资源同步世代，或使用能保留这些资源的真实 GPU 帧捕获。不要暂停后假定效果 draw 存在。若捕获仍缺资源，须明确缺项；之后才能在相同输入下比较 Position、FragDepth、discard 和逐像素覆盖。此步骤可能需构建并 `install -r`、冷启及重新进入战斗，保留 app 数据和存档；先与设备占用方协调，不在本次执行。

## 验收

暗魔法、Limit、遮挡、shield/fire/boss、Switch 对照及独立 QA：NOT_RUN；音频常设门（AAudio NONE、蓝牙野外/战斗各 ≥2 分钟、切换重开等）NOT_RUN。没有声称修复或性能达标。

## OSS-NOTES

- Khronos Vulkan fragment operations: https://docs.vulkan.org/spec/latest/chapters/fragops.html — depth replacement、depth test 与 sample counting 的顺序用于设计逐项定位；计数为零本身不区分上游覆盖和深度失败。
- Khronos depth guide: https://docs.vulkan.org/guide/latest/depth.html — 显式深度输出与 compare 语义，不能仅凭 EQUAL→ALWAYS 作为修复。
- 所给 Ryujinx PR https://github.com/Ryujinx/Ryujinx/pull/3553 与 commit https://github.com/Ryujinx/Ryujinx/commit/ad47bd2 当前返回 404，未独立核验 RGBX blend 结论；不据此改代码。

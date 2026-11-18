# 兵工厂卡顿：接手与实测检查点

尚未修复或发布。用户允许RG调试，荣耀已分配统一游戏库。

输入ZIP SHA97b18209e99b7fe55fea4a7fddfc4044fd7f36f549fc1efed0979719761f56ea，视频SHAe7488b8e68acd25826809597a8737cd04f93f092bf311c7dcd285b7c20f44f2c。原始与哈希保留在/Users/yuyan9923/.cetto-arsenal-inputs/。导出设备OnePlus PMS110。视频编码30fps不是游戏帧率。

RG原67的PID24992成功Continue进入第4章兵工厂存档点。30.179秒静止窗口901独立呈现，29.82fps，P99 40.38ms、max44.22ms，0帧超过100ms。短移动6.49秒27.73fps但窗口含截图，不当作正式移动性能验收。CPU6帧中descriptor54.18ms、snapshot26.49ms、other29.90ms；普通窗口GPU约27.5ms。CPU/GPU不同计时区间不可直接相加。旧analyze_stalls.py的submit_cpu_ms英文注释不是当前计时语义，不能直接归为vkQueueSubmit驱动卡顿。

原RG67 APK与12个私有文件已归档逐SHA核对，四文件玩家副本以应用UID放在shield-regression-20261118095001。升级后原12文件未变。首次安装72工作目录根候选发现SHA2e3ebda4非最终版，未作性能验收即停止，纠正为主发布目录最终72 SHA0fb453ad73e8d18944a2ccb2d1039ad6635b6f94b6b9998d51c5fa7920588371并实机回读一致。最终72 PID31689进入同场景，进行无截图移动窗口。

未改源码，既有移植经验指南WIP保留。最终根因、OnePlus复现、战斗、音频蓝牙、修复候选及新发布全部未完成。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 2026-11-18 后续探索，尚未验收
最终72 PID31689同存档移动到随机遇敌，31.1388秒25.05fps，9次>100ms；frame4064间隔1259.9ms，其中新管线创建909.2ms、31新shader80.6ms。证实该次长帧主要贡献之一为首次编译，不等于全部持续卡顿根因。另一随机敌组PID2692仍有404ms编译长帧，敌组不同不能A/B。
私有候选仅APK预热资产修改：旧1750配方扩为1898（148增量），1971681字节，SHA c7a1d89d498e013d9a8407268892f913193626fcb83e0e91fa5c416e702dd287。shader索引核对无缺项；native/Java字节不变。Android及host测试exit0；APK SHA90e031dd38d8921d765ccb0292da31d56ed32137d27a0205dabdd10c4aed1164。没有发版/上传。
RG原运行缓存保留为vk-pipeline-{list,cache}.bin.arsenal-original；冷A生成物另存.arsenal-coldA，均未删除。现候选PID6973，原正式存档未动，仍使用独立导入存档。
冷A生产版移动随机遇敌20.335秒17.70fps、max1944.6ms。冷B候选首个19.196秒移动窗口28.13fps、max67.97ms，但未遇敌且启动预热等待更久：这不是受控同场景对照，不能据此称修复！后续需相同预热阶段/相同敌组对比，并完成全回归与音频验收。
统一库目前只有SO模块更新不能包含该资产，需受控签名资产manifest及退出后激活/回滚支持；不得将本机候选说成线上热更新。

### 候选未过关，恢复生产72
PID6973预热最终1898成功/0失败，累计warm_ms257639。移动第二窗口28.829秒27.06fps、max52.23ms，但进入战斗在窗口末附近，不能当战斗验收。随后真实连续攻击21.901秒21.92fps、max329.77ms、5次>100ms；frame10510中pipeline_create255.7ms+shader23.9ms。继续攻击28.248秒23.36fps、max105.28ms。预热资产补充只缓解已覆盖管线的首次编译，不能修复全部持续负载；不发布该候选。
RG测试结束读取HAL CPU87.252°C、SKIN50.494/status2、SoC88.495/status3，而缓存ThermalStatus0不一致，已停止游戏冷却；没有改散热或频率设置。候选缓存另存.arsenal-coldB，原缓存复制恢复且SHA复核。恢复主发布目录生产72，不启动长时间空跑。
只读审查：vertex约17.62MiB/帧、index0.96MiB/帧是snapshot主要复制来源；已有46/47完整字节比较vcache负收益历史，不重复。正在审查不可变descriptor结构计划，不能跳过动态内容、revision、barrier或提交快照；尚未改源码。

## 用户OnePlus诊断改变优先级
Library上传FFRS-report-20261118-094949.zip（本地SHA496bfdb52cae172de2e1a3ef6026d2c2cd4aeed7db409213e040ece9be12b2d9）仅读取。metadata：2026-11-18 09:49:56+08、APK71、OnePlus PMS110/SM8850、Android17/API37，导出时可用内存约2.81GB且low_memory=false。设备profile标签phone_sd8g3/60/1920x1080是配置而非实际帧率或内部画质验收。
日志GPU明确Adreno840，driver0x8034a035；guest头BuildID736837afd390442ed7c74a7de730e71b对应旧1.0.0，RG当前b7f965…是1.0.1，同存档不等于同数据输入。
当前engine.log采样内160次相同graphics pipeline创建-13，缓存失败后VK_NULL_HANDLE重试仍失败。实际VS17c6ba1e659fec3a/FSab0a6325b8496fa2；pstat字符串另含compute45d6f0410c6e92a1只是状态列表，不能误称compute加入graphics stage。末60帧日志attempt FPS5.38、submit_cpu159.49ms/GPU26.07ms；不是独立呈现CSV验收。代码成功后才记pipeline_create计数，故失败反复编译时create_ms=0不能排除编译阻塞。
旧engine.log.1还包含VS5a97a7e1a99cce45/FS350ac5272e645d62（769次）和VS3bce5482c09be3fb/FSc9c4f100629aeee7（25次）。engine.log.fatal修改时间2026-11-15 01:13，与本次导出不同，不能认定当前崩溃。日志都有head/tail截断，缺中段及失败管线recipe，不能凭-13确认SPIRV或驱动根因。
父级确认无其他原FF writer后登记diagnostic scope：仅增加独立有界失败recipe日志及创建耗时，加入报告allowlist；不把失败塞入成功warm列表、不缓存跳绘制、不修改渲染语义。诊断候选构建中，shader静态检查并行。RG原12私有文件再次逐SHA确认全相等。

### 诊断候选完成（不是修复发布）
新增warm::record_failure独立pipeline-failures.log：最多8个唯一管线、512KiB文件，O_NOFOLLOW/普通文件校验；只写模块身份+portable recipe，不写游戏数据/存档，不放进正常预热列表，不缓存失败或跳过新的调用。失败现有日志补failed_create_ms，仍执行原创建/重试逻辑。DiagnosticReport白名单包含该日志。
.local/arsenal-diagnostic-20261118/FFRS-1.0.72-arsenal-diagnostic.apk SHA e3ca01816f93773ec272c2df214b0aea95f49edeae324199989ff5bd8715391b。Android0、host passed=true且659源码输入构建前后相同。新增生产提取record_failure测试8项ASan/UBSan通过（重复/8条上限/文件512KiB/未知模块/溢出/invalid/符号链接/配方往返），报告测试4项通过并实际断言新白名单导出。不是完整游戏验收。
RG PID19868覆盖安装全文SHA回读匹配，独立用户存档Continue进入第4章兵工厂并短距离移动正常，完成烟测后停游戏、恢复正式72。没有操作荣耀。没有上传社区，没有称该版本修好了840卡顿。
本地归档含6个失败VS/FS：原始SPIRV全部spirv-val Vulkan1.3通过。按日志启用的通用stencil/inline/quad/precise链离线应用后仍全通过；三VS仅分别增加700/747/680个precise约束，inline/quad没有改动。此离线链未覆盖实际运行时vertex/input/typed-format变体或真实失败管线状态，不等于完整driver验证。故仍需840的精确失败配方或该设备验证，不能凭通用SPIRV通过就归咎驱动。

诊断交付校验：Library导出完成，APK3781916B；签名与正式71/72相同，证书SHA256 d11967a31fcef92f3d6b44adf9d9678c8d2b07bf8f3246d51a791617ed61f36e。RG已实测adb install -r从72覆盖到诊断72成功、安装路径回读APK SHA=e3ca0181，读独立现有存档进入兵工厂及移动成功。随后正式72再次覆盖恢复，回读0fb453ad且无运行PID。用户OnePlus71只验证发行APK签名相同，尚未实际覆盖安装，因此不能声称其设备安装通过。不卸载、不清数据、不改存档。

### 荣耀重现及接口候选（进行中）
经库负责人交还荣耀，原72 PID18275隔离存档94001 Continue直接进入同兵工厂，实时画面4–5FPS且17c6/ab0持续graphics pipeline -13重试。Adreno735 driver0x802fa02e，不同于玩家840但已实际复现同错误。随后诊断72 PID18901同场景每次失败创建约66–69ms，持续每帧重复。完整日志保存在~/.cetto-arsenal-inputs/honor-arsenal/，只读查询完整会话边界后使用。日志记录recipe成功，但ADB shell/run-as直接读取受到设备存储权限限制，尚未拿到该文件，不以错误文本代替配方。
离线六模块发现三FS实际读取VS没有写出的varying：17c6/ab0 loc7.xyz；5a97/350a loc7.xyz；3bce/c9c4 loc4.xyz。原模块及通用改写链各自spirv-val通过不代表链接接口完整。来源Translator.cs固定revision8e0916bf04f6fb8542c52ed01b166a8796158ca0中的EmitOutputsInitialization对下阶段缺失输出初始化xyz0/w1，窄候选依此只补三个精确长度/全字节hash匹配VS，不删片段输入，不负缓存跳绘制。仅Qualcomm运行/warm两路径一致调用。ASan/UBSan两项含真实三模块spirv-val与原指令逐字保留检查通过。
候选.local/arsenal-interface-20261118/FFRS-1.0.72-arsenal-interface.apk SHA a07942f77d31ea35ff75e58df943f01e01ef8693471e863abf72b04d351d4137，Android构建0，host运行中。尚未完成实机场景验收，不能宣称修复或发布。此候选没有改种子资产，未覆盖其他项目。当前荣耀验证候选；RG已交库负责人短UI测试。
荣耀候选PID19759正式帧CSV窗口：13.4997秒左右移动301独立呈现，22.22FPS、P99 94.74ms/max183.66ms、3次>100；随后战斗连续A输入20.8179秒14.41FPS、max492.22ms、16次>100。不是最终达标，仍存在CPU/GPU持续负载和首次新管线卡顿。六帧detail CPU探针first6812–6817中descriptor119.49ms/总measured285.17ms，诊断本身开销不可当性能验收。日志轮转，主log+log.1已同时拉回。原17c6/ab0每帧失败已停止，但只这一修复不能称整个副本解决。APK a079候选host完成exit0、输入SHA不变。荣耀已停止游戏并交库负责人短窗口；电量48%、未充电、温度30°C，后续<=20%停止高负载。新增水之都需求另作同场景两设备对照，未预设根因。

## 追加：水都基线与细分 CPU 诊断（2026-11-18）
- 已从既有 memory-survival63 私有备份恢复第三章水都 Slot01 到独立 `shield-regression-20261118104001`，未覆盖正式存档；荣耀生产72 PID21619 实测旅馆外水面花朵路径出现严重低帧。
- 当前会话日志包含 VS5a97a7e1a99cce45 / FS350ac5272e645d62 及 FS8b3b271f21559280 的管线创建 -13。前者已在窄接口修复证据内；仍须候选同场景对照，不能宣布水都修好。
- 已整合 diagnostic-only texel 子计时（alias/pending/readback/view lookup/create），仅现有 gfx-cpu-profile 诊断窗口启用，正常路径不计时；无缓存、同步或生命周期优化。独立 ASan/UBSan 用例通过；Android 构建通过，host验证进行中。候选在 `.local/arsenal-texel-probe-20261118/`，未发布。
- 荣耀实读51%，AC/USB powered true/status2，电池32°C，确认正在充电。

### 水都候选初步结果（PID24213）
- 新诊断候选 Android/host 均0；APK SHA c9bc2bf5e63c64435a0fe42888938aa67baf3f4a7d3532c5799e9658ca11dc06。
- 水都生产72 PID21619本次日志639次5a97/350a和10次5a97/8b3b graphics -13；候选PID24213当前会话0次此失败。
- 水都旅馆外水面花朵来回移动正常无诊断窗口：10.51691411s，独立渲染36.1323FPS，P50 26.6822/P95 36.3107/P99 39.8642/max45.4664ms，无>100ms。证据 `~/.cetto-arsenal-inputs/honor-water/clean-candidate.json` 与同名clock起止、candidate24213.csv。
- 首次flowers窗口被CPUprofile7412–7417污染，不作性能验收。该诊断窗口descriptor32.45ms/6帧，texel_view_lookup仅0.124ms/6帧，当前水都没有为其增加frontcache的证据。
- 以上不是全城、战斗、稳定锁30或840原机验证。水都baseline短窗口tailCSV未刷新导致一行，已明确不用于数值A/B比率。

- 水都同路线锁30补测10.49997859s，30.00006FPS，P99 36.6892/max43.6882ms，无>100ms。已恢复marker0并切回隔离兵工厂继续，不含完整音频验收。

### 兵工厂细分 CPU 窗口 PID25490
- 同候选正常短路线9.556s，31.394FPS，P99 52.409/max76.774ms，无>100ms；位置/路线与此前22.22窗口不同，不能据此称进一步优化，战斗仍未过门槛。
- 六帧2882–2887 descriptor102.27ms：image_lookup残余32.83、texel_readback14.64、remaining32.61、view_lookup0.926（仅0.154ms/帧）。不实施缺乏收益依据的frontcache。
- 只读复核发现graphics pending_view/texel_acquire仍未分出、readback需拆cpu_access/分配，正在独立副本准备下一最小诊断patch，不跳过同步。
- 荣耀55%仍充电、电池34°C；thermalstatus1/GPU报告92°C，已停止原FF冷却，不继续高负载。RG交还后准备回归时offline，有限同端点重连中。

### 温度纠错与独立验收要求
上段“GPU92°C”读到了 thermalservice Cached temperatures，撤回作为实时温度证据。已保存的 thermal25490-end.txt 的 Current temperatures from HAL 实为GPU49.3–52.8°C、CPU最高73.2°C、skin38.861°C/status1、电池34°C。停止后当前HAL GPU34–35°C/status0。后续严格分Current HAL/缓存，记录充电；以匹配冷起点的旧/候选、不锁/锁30短场景对照调查发热，不预设充电根因，不以缓存峰值夸大。CPU频率可读，GPU频率接口无可读值则标缺测，不变更权限。

### 二次细分计时与后续约束
候选2 SHA6d2d016be2249f95692694dbb0c9a477ef9efa521f4cc3b11f2256f327b32507、Android/host0，实际源码checkpoint6af7a82。Honor PID27308隔离兵工厂2702–2707六帧：descriptor55.39ms，image_lookup11.22、readback9.93、resident_pending1.02、resident_acquire3.50、CPU_access0.023、view_lookup0.625、remaining15.64。该次位置/温度/CPU频率与此前不同，不作优化收益比率；新增诊断无渲染行为修改。Honor测试后停止，等待独立热对照，RG5038 offline。文件 `honor-arsenal/engine-probe2.log`。
用户新增分辨率：初核有 `operation_mode` intent→oe-operation-mode→HLE GetOperationMode，可能借游戏手持/底座档自行选择内部尺寸，待真实附件尺寸证据。`gfx-render-policy`仅Surface展示1920x1080，不可冒充内部缩放。已与统一库沟通不猜参数、不改当前默认、不与锁帧绑定；需重启、UI/触控/画质实测。

### 实际内部尺寸取证
在同一隔离兵工厂存档位置，分别完整重启 `operation_mode=0` 与恢复默认1，仅开启一帧 gfx-pass-live 诊断，不将它计入性能验收。
- 低档 PID28072/frame2433：多项主几何/深度/Gbuffer pass768×436，最终合成/UI1280×720。
- 默认mode1/frame1923：主几何/深度/Gbuffer1040×584，动态policy日志存在80→60%自适应；两档都不是固定1080内部渲染。
- 证据低档 low-mode-engine/guest.log、默认 default-mode-engine/guest.log，截图保留/tmp/honor-low-factory.png、/tmp/honor-default-factory.png。已恢复oe-operation-mode=1、gfx-render-policy=1、gfx-target-fps=0并停止游戏。
- 已告统一库用“标准/较低画质”且要求重新启动，不以屏幕/合成分辨率冒充内部尺寸。不宣称低档完整双机视觉/性能/温度通过。

### 低画质短探索对照（PID29645/30138）
同隔离兵工厂档、同左右各4秒摇杆路径、probe2 APK、uncapped：标准成功独立帧33.8497FPS/P9948.343/max50.473ms；低档33.5449/P9949.193/max50.731ms，均无>100ms。标准CSV在窗口尾未完全flush（实际记录7.799s，clock窗口8.324s），低档8.317s；因此仅探索，不能作精确性能倍率或验收。未见降低分辨率带来明确收益，继续优先CPU归因。Current HAL skin标准35.378→35.591℃、低档35.942→36.622℃，status0，电池31–32℃，充电状态相同；短样本/非平衡不作温升胜负。证据~/.cetto-arsenal-inputs/resolution-ab/。测试结束停原FF、oe-operation-mode恢复1；RG仍offline。复读只改自有记录，保留外部pitfalls文档WIP。已请descriptor_review独立只读复核readback/remaining路径，未委托源码改写。

### Readback清零成本实际定位
新diagnostic-only APK a7ac814027ca74280ba81479a9d6556afcb2e34922ee7317c2d554c98c991367（.local/arsenal-readback-probe-20261118），Android/host均0；追加细分计时不改变渲染。独立提取48组合及真实timing header root-null/nested/early-return ASan/UBSan通过，记录.local/arsenal-fullcopy-review/。荣耀PID31090同兵工厂独立档，frame3602–3607 descriptor52.9485ms，readback_zero7.9435ms/62,798,824B，copy0.5224ms/6,275,048B，alloc0.1747/guard0.1351/flush0.1419ms，guarded_bytes0/noncoherent_flushes0。日志engine-readback31090.log当前最后窗口，勿混前面旧probe数据。新证据支持验证仅随后GPU全量覆盖的两条pending-view copy路径，尚未取消任何清零、未称性能提升。原FF已停止；69%正在充电、电池30℃（启动前实读）。行为候选必须保持guard分支、原完整copy/barrier及存活期，不泛化到部分shader输出。

### 输入纠错与fullcopy候选（尚未验收）
用户指出滑屏时角色未移动。撤回本轮resolution-ab与fullcopy-ab off1/on1的“移动”描述：仅有输入命令和起止位置相似，不足证明真实移动，不能用作移动性能/优化收益验收。已停止全部swipe，改input keyevent --longpress22，连续3次后紫色恢复点明显从中央移至左边、角色走到空地（/tmp/ff-direction-right2.png），确认方向键有效。后续仅21/22固定方向键。
fullcopy2 APK a730217de510bd3bea40d36e5788c3e4e67da491e9b26ede30313989b514df90，Android/host0；default-off两完整GPU复制分支候选，guard/未知bound/alignment/alias/indirect CPU读取/probe全回旧路径。独立ASan32组合、原uniform-arena3项及两调用点完整copy/barrier静态检查通过。第一版fc07未安装：审查发现indirect_input隐性CPU读而进一步收窄；host测试helper位置影响fixture编译，重定位helper修复，不削弱测试。
当前荣耀PID744隔离兵工厂正在方向键A/B，未称性能提高。最初swipe on1掉至21FPS，off后同样约21，未归因候选；需实际固定路线/温频记录。Current HAL skin36.52/status0，GPU50.9–52.1、电池33℃，无热保护；按用户要求正常温度不例行暂停冷却。

### 真实方向键同路线A/B（PID744，不锁帧）
用户肉眼确认方向键已移动。固定Right×4/Left×4三轮（Android21/22，每次--longpress），候选a730同会话：on2 13.0332s/20.6395FPS/P99 57.162/max97.710ms；off3 13.1011s/19.9220FPS/P99 61.266/max68.293ms，两组均0>100ms。skin36.891/36.838℃、电池33℃、status0，表明仅小幅收益，远未30，不能发布。此前4.3s短窗off18.81/19.56、on20.03同样只作初步。后续向右探路到地图边缘已按用户提示停止、Left返回楼梯下可通行区；顶墙探索不在上述计时窗口，不计移动验收。
开优化六帧7712–7717 readback_zero仅1.321MB/0.1208ms（约400个fullcopy/帧命中），说明目标分支确实跳过重复清零；非同帧场景不能用总descriptor76.21ms对此前52.95做收益比。frame CSV/thermal/marker窗口见~/.cetto-arsenal-inputs/fullcopy-ab/；engine744-final.log与final744.csv完整收尾。实验开关恢复0，采样完成停止原FF以开展代码分析，不是例行热保护暂停；保留原存档。
后续瓶颈：同窗legacy_wait阻塞17–27ms/帧约2calls，不能把waits≈9.8当真正阻塞次数；sync-submit0、completion1、legacywaitall0确认，不改同步规避。Android现有XTRACE全空，仅OHOS有标签，委托独立审查准备API23 ATrace最小诊断，不开放不安全async翻译，不重试无效poolreset。

### Android时间轴与RHI采样
集成Android API23+ ATrace begin/end诊断（counter仍no-op，未使用API29符号），非采集时也有库enabled检查开销，不宣称零开销；不改变锁/等待。新增自包含tests/native/test_readback_fullcopy.py，实际提取当前实现，ASan/UBSan48+32组合、调用点负对照通过。APK379e8296b8f544c36a4ecede34e08381e046965819b2a386669c76494ce0ad70（.local/arsenal-atrace-20261118），Android/host0。
Honor PID6308独立厂区，Right×3/Left×3两轮，15秒Perfetto无error stats：主GameThread6502 legacy-lock1656次668.246ms、legacy-relock844次4288.134ms、legacy-wait844次6849.226ms。relock中Sleeping4028.444ms，RHI6509在整个traceCPU11265ms。独立复核relock与RHI Running重叠99.2%，1ms周期重试形状明确；不能把5.08ms平均relock当可全部省掉的空闲或认定互斥公平性问题。没有owner/return标签，waker字段NULL，后续不得跳过发布/改同步凑帧。
RHI6509另5秒simpleperf task-clock500Hz，2368samples、0lost：memmove6.46%、memset5.19%、indexed_range_ready4.65%、descriptors_build4.43%、pending_view4.10%、indexed_graphics_ready3.21%。原始trace/CSV在~/.cetto-arsenal-inputs/atrace/，simpleperf在同级simpleperf/；只本地诊断，无上传。未改kptr_restrict等安全配置。
采样完成停原FF、确认无PID，交荣耀给统一库10分钟低负载验收；当前80%、AC/USB供电true但status3（未在充电），电池31℃，未改电源设置。RG仍未发现。未发布，未宣称30FPS/完整特效音频通过。

### Indexed-ready bounded diagnostic integration
64eb27a baseline; independent review found indexed_range_ready reads pending reports/writebacks under existing engine mutex and indexed_graphics_ready does not mutate those lists; pin_indexed_input is outside and may append, so cross-call caching is unsafe. Integrated shadow counters only: same-function exact (address, bytes, alignment) duplicates, VS stream repeats, scan steps; original checks still run. 128 keys bounded stack, saturation excluded from exact denominators. Disabled stack/branch overhead is not claimed zero. Production-predicate differential ASan/UBSan 10,000 cases pass; pin-like append/fresh-call, alignment and overflow cases pass. No optimization enabled. Android/host build running `.local/arsenal-indexed-20261118/run_build.py`, tools session97092, plan/logs durable. Honor currently borrowed by port_rules for library low-load tests; no FF device input until returned. No new performance acceptance/release.

### Indexed-ready actual Honor evidence: reject dedup hypothesis
Diagnostic APK6abb51364be9e0f09260e7df19a42c1559169ea2682db88b61989fc20f1f196b installed over72; Honor10262 isolated factory94001, mode1/fps0/rbfullcopy0. Three Right longpress22 inputs visibly move actor away from purple recovery circle (private indexed/before.png and after.png). Six-frame CPU profile reports1014calls,15114requests,15054unique,60duplicates,0overflow;2132358report scan steps and5516230writeback steps; duplicate scan steps11460+23470; VS1014requests/0repeats. Exact duplicates0.397%, duplicate scan fraction0.457%: reject general/VS dedup as meaningful target, leave candidate unmerged. Diagnostic-window FPS is not acceptance. Broader cost remains many interval scans, needs new evidence before indexed structure changes.
First host verify failed performance-hint-contract: stats declaration was inserted between namespace performance_hint and timing_start, contaminating that test's extracted adapter with unknown type. Androidbuild passed. Moved declaration before namespace (no runtime semantic change), both original performance-hint tests and indexed10kcases pass. Full Android/host rerun plan persisted; first failure .local/arsenal-indexed-20261118/first-performance-hint-contract.stderr. OriginalFF force-stopped after sample. No release/30fps acceptance.

Declaration-only correction rebuild finished: Android0/host0, APK5a553763de62e1bb4302705057e9d6d6c985dfec2245a2a623fa6435348db326, build-plan source hashes unchanged across rerun. Installed diagnostic used for above10262sample remains6abb (same runtime logic), not falsely claim5a55 device-tested. CurrentHEAD0b9aef2; no unrelated WIP staged. Honor now port_rules066UI window, originalFF stopped.

### BBox shadow diagnostic (no fast path enabled)
Integrated independent bounded probe after state eligibility gate. Each eligible graphics-ready call profiles report and writeback half-open aggregate address bounds, build entry count, outside query count, and only actual original-scan steps that would have been avoidable. All original report-first and newest-writeback scan decisions still execute. Invalid pending intervals mark envelope unusable; overflow/empty/half-open semantics follow actual xbg_writeback_policy.h. Original predicate equivalence210000 differential queries plus lifecycle/short-circuit cases and ADPF extraction tests pass ASan/UBSan. Android/host command saved .local/arsenal-bbox-20261118/run_build.py session62636. Honor returned by library066; no game launched until candidate build checks complete. This measures feasibility, not performance improvement.

### Honor BBox feasibility sample
Android/host0 f25c5067e9463c5672270a6c7965838fc5be6829705f0669b725daa3db7f5bfc installed, PID12455, factory isolated94001, mode1/fps0/rbfullcopy0. DirectionRight22×3 visibly moves away from recoverycircle (bbox/scene and after screenshots). Bounded6frames:978eligiblecalls14586queries, noinvalid/empty/unusable envelope; reportbuild118133/writebackbuild370414 entries, originalscan1860521/5160672; outside10765/5348, actualavoidable1288820/2025688. Potential3314508/7021193=47.2%scan steps, envelopebuild488547=6.96% oforiginalscan. These are comparisons, NOT elapsed savings/FPS. Supports default-off candidate local-per-call range rejection, not global stale cache. Original predicates still all executed in this APK. Independentworker preparing candidate, no source behavior change yet. OriginalFF stopped after sampling; no release.

### BBox readiness candidate default off
Integrated reviewed patch56166aa4 as opt-in bboxready=1 (live-set), default/init false and existing AB off fallback. XbgIndexedBounds is independent of diagnostics, rebuilt after eligibility gate per function call; valid query outside a conservative half-open list envelope skips only that list. Invalid envelopes fall back; overlapping ranges retain report-first/newest-writeback order, containment/alignment and all synchronization unchanged. Complete graphics function20kstate/failure differential and200kactual-policyrange cases pass, plus existing shadow/readycases. No cross-call pointers/cache. Buildplan .local/arsenal-bbox-opt-20261118/run_build.py session47082, same native/assets/flags, source hash locked. Candidate is not yet performance-tested or released. Planned actual route3Right/3Left×4 and A/B/B/A uncapped, rbfullcopy remains0 to isolate variable; screenshots outside timed window, no diagnostic profile during acceptance samples.

### BBox actual directional A/B: no stable improvement
Android/host0 dbdb94d16338e7e7fa431a6fd79162f4f782bb987ac6c0587bad647d06243323, independent four negative controls rejected incorrect outside/invalid/latest/validity variants. InstalledHonor originalFF isolated94001; mode1/renderpolicy1/fps0, rbfullcopy0. Route Right22×3/Left21×3 repeated4, screenshots outside timedwindows, noCPUprofile. PID13663 off1 12.947s21.4718FPS P9989.186/max156.263ms; on1 13.070s20.2761FPS P9959.675/max62.235. Reopened sameisolatedsave PID15565 reverseorder on4 13.055s21.0645FPS P9969.980/max88.197; off3 12.952s20.7684FPS P9967.405/max119.975. Actualpresentmarker confirms flag switches before cleanwindows. Skin33.147/33.272 then34.997/normal0, no thermal excuse. Endscreens remain exploration but small return-position drift; short runs not stringent sustained acceptance. No consistent FPSgain, keepdefaultoff and endflag0; notrelease.
Excluded: on2 had concurrent14MBengine-logpull (20.56FPS retained but contaminated); on3 mixed randomencounter battle (19.26FPS,long438ms not exploration); off2 started whilebattle appeared and was terminated, no validwindow. This is explicit evidence rejection, not cherry-picking a stable claim. Screens show actualDpad motion earlier, no swipes/joysticks. Afterfinalsave/readlogs originalFF stopped, device75%powerconnectedstatus3battery30°C, library can borrow.
Evidence~/.cetto-arsenal-inputs/bbox-ab/{window JSON,CSV,screens,thermal,engine13663/15565-final.log}. Next larger CPUcost review uses existing simpleperf rhi6308-callgraph.txt (no new sampling), descriptors_build children29.65%/uniform_snapshot_cached6.29/readback6.12; all inclusive overlap, do not sum.

### Existing PERF timer normalization correction (not FPS correction)
Independent read-only review found old12455 bounded first2462–last2467 CPU_TIMING totals reused by subsequent PERF: parts_ms_per_frame timed=0 descriptor95.22 equals six-frame descriptor_ns95223268, and desc_ns_per_frame build106.40 is cumulative period amount, not106ms per gameplay frame. TimingRoot includes bounded timing_active but perf_timed_frames counts only ordinary sampled frames; tf fallback1 causes false per-frame labels. Treat these two bounded/mixed PERF summaries as unnormalized/unavailable, not frame estimates. Authoritative bounded CPU_TIMING first/last/frames and separate game-frame CSV remain valid. No published FPS improvement relied on the bad denominator. Preparing minimal truthful labels plus readback-site bytes, not another cache. Current movement-window UBO~0.57–0.58MiB/frame and front3152–3245 hits; laststatic0.60MiB/front3384 not incorrectly substituted for movement. Other inclusive CPU percentages must not be summed.

### Readback source counters + truthful timer labels
Reviewed site diagnostic integrated: five fixed sites, existing bounded descriptor root only, actual memcpy/memset bytes plus separate guard/full-GPU-copy bytes and requested-size buckets. No additional clocks or rendering changes. 480 differential/gating cases plus fullcopy original48/candidate32 and negative contracts pass; uniform/ADPF tests5pass. Normalization now marks parts unavailable for bounded/mixed windows and refreshes baseline, desc_ms_window_total not fakeperframe; pure ordinary uses real sampled denominator. Storage union (timed-only pages) also uses window_total when not normalizable; all-frame snapshot count/f unaffected. Build .local/arsenal-readback-sites-20261118/run_build.py session70836 source hashes locked.

GPU-source feasibility: shell tracefs/kgsl sysfs read denied; no permissions changed. Perfetto3s stock gfx/sync+FrameTimeline through stdin config succeeded (direct/data/local/tmp config read denied, no chmod workaround). Private atrace/gpu-feasibility.pftrace563239B contains sched_switch/waking/print only,53ftrace setup errors, compositor GPU-completion counters but no game GPU execution track. This does not prove all vendorGPUevents unavailable; independent review inspecting actual setup errors. Do not equate FrameTimeline/retirement gpu_ms/guest wait counts with GPU critical path.

### Readback actual source sample20924
Readback sites APK0f2952 Android/host0 installed Honor20924; bounded10862-10867: texel pending63.06MB zero vs storage pending1.275MB, no expanded fullcopy justified. Truthful timer labels verified; no FPS acceptance. Honor stopped80%connectedstatus3/30C, library069 window. Next explicit kgsl active-game feasibility; no release.
Evidence private readback-sites/engine20924.log, actual factory screenshot before bounded profile. Six-frame storageCPU21calls1.376MB, texelCPU72calls8.534MB; pending texel2376calls63.062MB vs storage42calls1.275MB. Probe collected only diagnostic interval, not FPS measurement. GPU feasibility53 errors parsed:7failed dma_fence/sde and46unknown, no kgsl/adreno requested item; cannot generalize to all GPU trace support. CPU reviewer preparing explicit active-game short config.

Frame-index correction: current READBACK_SITE lines161722-161726 belong immediately to CPU_TIMING at161678 first10862,last10867 (209.486ms wall). Earlier1652-1657 appears at126774 from older append session and must not be joined. Initial short checkpoint frame reference corrected; source byte totals unchanged and no FPS inference made.

### KGSL explicit actual-game feasibility
KGSL actual5s PID23334 factory3Right3Left completed; trace5,741,394B. Fiveexplicit kgsl events allUNKNOWN_TO_SETUP; noAdreno tracks. No permission changes. OriginalFFstopped79%connectedstatus3/29C. Independent CPU trace audit + defaultoff slot-only raw existingquery candidate underway; no newquery/wait/syncchanges, not release.
Trace private atrace/kgsl-active-20261118.pftrace, config/script/errors/tracks at.local/arsenal-cpu-review/kgsl-*. Uses file-output+pull to avoid stdout corruption; retained remote /data/misc/perfetto-traces/ff-kgsl-20261118-122930.pftrace. Loaded factory screenshot verified before capture, D-pad21/22 only. Event-specific unavailable, not proof allvendor GPU instrumentation impossible. Next candidate must preserve existingquerycount/flags/aggregate acceptance, serial reuse/covers/cross-clock/incomplete retirement guards per.local/arsenal-gpu-raw-review/REVIEW.md.

### Bounded raw query candidate
GPUraw candidate2659b941 Android/host0; fixed POD4096, defaultoff init marker, existing query once with originalacceptance, no newwait/query/idle. ASan state/negativecontrols and fullcopy/site regressionpass. Installed Honor marker3000/120/4096, isolatedfactory94001; session95925 launching. Need verify map/threads/exportmtime and native raw evidence before conclusions.
Build .local/arsenal-gpu-raw-20261118/run_build.py session52549 finishedAndroid0/host0 sourcehashes unchanged; test_gpu_raw productionheader ASan+4negativecontrols; old readback80/site480tests passed. CPU clock steady_clock, GPU domain uncalibrated, never add/overlay blindly. Capture output only afterwindow/naturalretirement; abruptkillcanlosecapture.

### Candidate first-device interruption (not acceptance)
PID25764 candidate2659 exited status139 at12:36:17, guest signal11/fa0x9c/NSOoffset0x142a0e0, aroundframe2797 before marker start3000. No raw output. Library owner confirmed no inputs afterhandover, so not library switch; app exit exposed underlying library. Post-crash direction-only command briefly reached library; no confirm/button A, excluded sample. Future input loops verify expected PID each event. Candidate prewindow logic returnsInvalid before rows/clocks, but static/Slot layout/timing changes remain possible triggers, not exonerated by guestPC. Independent exactBuildID disassembly pending.
Mac disconnected during off-marker restart; initial assumption commanddidnotrun corrected afterrecovery: PID26671 existed with0/0/0 at title. Continuedit rather than duplicated. Same3R3Lx6 plus8sec endshot remainedfactoryalive; single off-run not proof ofrootcause. Current retry marker2500/60/4096 starts earlier to obtain bounded rows, still no release. Private gpu-raw guest/engine/exit/screens/identity/csv preservefailedandcontrol evidence.

### Raw slot capture27676 complete
Raw27676 actualfactory success first2500/end2560 558rows,0dropped/pending/partial. CPU submitmean0.0468ms; GPUspanunion979.6ms GPUdomainonly, noFPSclaim. Marker restoredoff, originalFFstopped75%connectedstatus3/30C. cpu27676trace+PID/TIDreceipt collectedlaternotrawsamewindow. Previous25764guestnull still unresolved; off26671 and enabled27676 short runs didnotreproduce, notexoneration.
Private gpu-raw/raw27676.log170475B; identity27676/threads27676-before.txt,frames27676.csv,cpu27676.pftrace4372614B. Rawtimestamp48bits52.0833321ns;558validqueries nofailure/wrap; submitp95.109/max.485ms; queryspanmean1.756/p959.948/max15.683ms; observedretiremean11.558ms notGPUcompletion. Diagnosticstartup inheritedAndroidfloatingwindow, maximized beforelater screenshot; do notuseasstrictperformanceA/B. Mid-route screenshot provesactualwalking. Source0f corrected compile2659 candidate, nooptimizationpromoted. Independentanalyzer12synthetictests passed; per-domainonly/noALUutilizationclaim.

### Raw/CPU independent interpretation
RAW27676-REVIEW confirms all558commands=1,HUDtag0 notusableframeidentity. Retireentry→queryobservation median0.000677ms, so11.56ms submit→retire delay is mostly deferredhostobservation, not queryblocking.83spans>5ms contribute817.9ms of GPUdomain spans, stillnotALUbusy. Later separateCPUtrace4.921s with explicitPID27676→TID27794 receipt: RHI Running4.533s92.1%; relock1.792s99.56%overlapsRHI,wait1.815s86.34%; noloss. Differentwindowsnotcombined. True vkQueueSubmit mean0.0468ms; PERF submit_cpu_ms wraps execute atxbg_game_submit andisnotdriverAPIcost. Next prioritize currentdescriptor/snapshot preparation, verifyhotspots beforebehaviorpatch; nofencespin/no syncskip.

### Current-candidate RHI sample28718
PID28718/RHI28835 verified ps-T; actualfactory screenshot thenDpad3Right3Left duringtask-clock500Hz5s. simpleperf2236samples0lost, data2441678B. Private simpleperf/rhi28718*. Kernel-symbol restriction warning retained; no securitysettingschanged. Currentself memmove7.07%,memset5.14%,indexedchecklambda5.10%,ioctl4.52%,descriptors_build3.67%;inclusive descriptor28.49%. Readback child25.43%ofdescriptor, Arenaalloc30.25%ofreadback withmake/vkAllocateMemory dominant. This new-entrance shortsample may includegrowth: need distinguish warmup from repeatedtrim. Android4MiB standardchunk,readbackkeep1,trimdestroysoversized. Independentreview requested boundedreuse/memo/lifetime constraints beforeanyretentionchange. Candidate retains GPUraw off, FFstoppedaftercapture, library070ownsHonor.

### Arena lifecycle diagnostic build
Arena lifecycle diagnostic integrated, actual Arena mock differential/guard/keep0/multislot deltas pass; existing fixture updated with realArenaLife+allocation_size, not weakened. Android21560bbf built0, hostsession91952 underway. Honor070returned, next6frame early/late factorywindows. No retention behavior change.
Counters classify actualchunk equality4MiB vs nonstandard, successmake/trim bytes+allocation bytes, protectedkeeps/keep0 destroys. timing_start captures per-slot3arena baseline, reportaggregates deltas withregressioninvalid. Doesnotproveexactsize reuse; early/latewindows distinguishgrowth. No extra clocks/waits or retention. Buildplan.local/arsenal-arena-20261118, independentpatchSHA b9d943fe55bd74e78a3deee54de6401cee181a58fec0f40c135d333242043c09.

### Standard arena churn observed
First arena sample PID31514 frames2552–2557 confirms standard4MiB readback make12/trim12/protected12, oversize0. Second live profile unsupported sameprocess (one-shot) and foreground Settings, discarded. FF stillalive; stopped safely, library070 shortwindow. Preparing defaultoff keep2 vs1 with guard/pressure unchanged and actualFPS AB; no release.
Host+Android arena21560 both0. Evidence private arena-life/engine31514.log, .local/arsenal-arena-20261118/windows.json. Measurementplan corrected: bounded profile once per process; use later-only capture next startup. Settings foreground cause unknown, not crash (PID31514 alive, exitinfo no newer exit).

### Stability prioritized, two separate incidents
Priority changed to user-reported stability. keep2 APK0218b016 Android+host0, installed defaultkeep1/titleonly then stopped; performanceAB NOT_RUN. IncidentA Xiaomi58 report+wood-room screenshot user identifies chapter3 watercity room; no save bytes and no explicit crash in retained log. IncidentB vivoMT6991/72 SIGSEGV at vendorMali+0x1eda09c, separate from25764 guest-null. Readonly parallel shader/API lifetime reviews; bounded defaultoff mobile breadcrumbs candidate requested. No release.
New attachments preserved privately crash-new-review; sanitized report .local/arsenal-cpu-review/NEW-CRASH-ATTACHMENTS.md. User correction: not factorysecondfloor. Existing waterfixture is mixed previous saves, not proven same room. keep2 implementation6c432de; latest Honor stopped, originalsave intact.

### Minimal stability fix and actual diagnostic export
Stability candidate5db5ceb1 Android+host0, Honor6612 chapter3 donor isolated132001 loads room and walks outdoors with no observed crash. Actual in-game-menu diagnostic ZIP542093B exported and read back, contains11 breadcrumb lines withmatchingPID6612 and all5 targetAPI begin/end pairs successful. NoMali-device verification. Normal diagnosticdefaultoff restored, FFstopped. Preparing explicit auto-on -mali-diag test build, not officialrelease.
Commits503c99e warm-layout ownership fix and7e4c4f1 breadcrumbs. Private chapter3-intake/source.zip SHA b503aadebf88cd7b2dbe002ab97f53ed597c3fab54b2ec3722e412346e390462, donor non-crashing notincidentoriginal; byteverified4-file isolated copy. EarlierScreenshot is firstchapterwoodroom, notsameasthirdchapterstonehouse. File actual-export6612.zip confirmsnewallowlistedlog; nofailuresignature duringthisshortcontrol.

### Dedicated automatic player diagnostic validated
Automatic labelled diagnostic01a3c602 Android+host0, version1.0.72-mali-diag/code72, same signing digestd11967a3. Device marker0 yet auto session7984 header/11API rows verified; thirdchapterroom/exit route no crash. Actual menu ZIP545002B metadata.txt+api-breadcrumbs matchingPID7984 verified. Uploaded APK/readme Library receipts local. Not formalrelease, MT6991original crash remainsunverified. FFstopped78%chargingstatus2/31C; normalbuild ignoresstale marker.
Source23c26c4; .local/arsenal-mali-diag-20261118/build-plan.json and library-receipt.json. Parent hasLibraryIDs; no community upload. Export initially reader expectedmetadata.json, corrected actualmetadata.txt without alteringarchive. NewplayerZIP needed to locateMaliAPI; first11rowslimit maymisslateroccurrences. Continue safeperformancework independently while feedbackpending.

### Readback keep2 first actual A/B, thermal-confounded
keep-stable562d9dad Android+host0. Actual Dpad3R3Lx6 off1PID9600:28.134s24.312FPS,p95 54.296/p99 71.920ms; on1PID11316:28.679s21.828FPS,p95 53.755/p99 73.666. Bothendfactory screenshots, no battle. HOWEVER thermalstatusoff0→on1skin37C, so no causalregression/gain claim. offPSS3073692KiB/on3130864KiB (~57MiBextra). Boundedlateprofile off5282 standardmake12/trim12, on6182make6/trim6,oversize0. Behaviormechanismworked, benefitunproven. Restoredkeep1,FFstopped; onlyresumeoppositeordershortABwhenactualthermalnormal, notfixedcooldown. No release.
Evidence privatekeep-ab/off1,on1 and .local/arsenal-keep-stable-20261118/*arena.json. on title inheritedfreeform, maximizedandverifiedbeforeContinue; notmeasuredinwindow. CPUprofileafterperformance, onceperprocess; offlogpullmayoverlapprofiletail, onlyallocationcountsusednotwalltiming. ShaderarraynarrowingNOTintegrated: independentcheckerterminatorcounterexample; review safetyinterrupted, no retry; offlineprototypeDO_NOT_INTEGRATE.


### Readback keep2 reverse-order control closed, not promoted
Readback keep2 reverse-order control completed: on2 22.842 FPS/p95 52.378ms (thermal0→0), off2 23.312 FPS/p95 52.333ms (thermal0→1). No reproducible gain, keep1 restored; no promotion/release. FF stopped and Honor returned to library071 UI. Offline exact-BuildID RHI self-line attribution next; Mali diagnostic awaiting original-phone feedback.
Same normal562d APK, isolated94001, fps0/mode1, Dpad3R3Lx6. on2 28.193s/p99 68.146/max146.201ms; off2 28.397s/p99 55.804/max66.225ms. Both end factory, no battle; title inherited OS freeform on off2, maximized/visually verified before Continue. Thermal again differs, so no causal slowdown claim either. Memory-after off2 invalid: stop was issued during post-window collection after image, meminfo reports no process; FPS script completed guarded input and captured frame window before stop. Retain raw evidence and exclude off2 post-PSS. Next do existing RHI exact-build line attribution, not another retention A/B. Honor77%/32C/AC+USB true/status3, thermal1; returned to port_rules for low-load071 UI. No formal release/community/module update.


### Offline attribution and stability gates
Existing rhi28718 descriptor self samples recovered exactly82/2236 (58 PCs), historical BuildID e7bb2023405360a37ff73f462b3168392c733424. Only matching old SO is stripped; no DWARF available, addr2line all??:0. No guessed line mapping/new cache/repeated sampling. Report .local/arsenal-descriptor-lines/REVIEW.md. Current normal562d unstripped SO separately preserved and checked against APK BuildID b0d02298be91b0baab3c0ac235fb4c61e1cdc769 at .local/arsenal-keep-stable-20261118/symbols/receipt.json; NOT historical match. Next builds must retain exact unstripped candidate before shared build directory is overwritten.
Warm layout publication, bounded API breadcrumb and explicit diagnostic build-mode tests rerun: all ASan/UBSan/contracts pass. Current diagnostic01a3 remains dedicated, not formal crash fix. Next device window chapter3 scene transitions; independent warm/compute object-lifetime review continues offline.


### Preserve prior diagnostic session across restart
Diagnostic previous-session rotation implemented. Android1ce83fcb built0; initial host failed only old fixture entry count14 after adding2logs, corrected16 and focused4tests pass; full host rerun exit0. Installed same-signature/code72 on Honor, readback SHA matches. PID18185 thirdchapter isolated runtime validation underway. No formal release.
Rotation preserves an incomplete begin on restart; on rename failure diagnostics disable rather than truncate prior evidence. Normal build still disabled. Native ASan/UBSan plus ZIP allowlist byte tests pass. source-retest.json documents sole post-build change to fixture count, all production hashes remain same. Actual reboot/export validation pending.


### Diagnostic restart/export validated
Retained-session diagnostic1ce83fcb Android0/full host retest0, same signing/code72 installed+readbackSHA. Honor18185 chapter3 room→exterior succeeds; controlled restart18614 preserves previous11 lines byte-exact. Actual menu export517286B contains previous18185/current18614 (3lines) and correct version. Uploaded new APK+README Library; parent hasIDs. Not Mali original-device fix, no formal/community release. Honor FF stopped74%charging status2/30C.
Production sourcebefeba8; device-receipt.json and library-receipt.json under.local/arsenal-mali-diag-retain-20261118. Force-stop used intentionally after captured session for retention verification, not a reproduced crash. Keep candidate default1; no performance promotion. Independent warm review finds ownership fix correct, no further proven runtime UAF; conditional shutdown wait-idle failure/backend finalize risk requires separate coordinated design and has NOT been called currentMali root cause. No shader narrowing. Python os.setxattr unavailable on host, used xattr command successfully instead; upload itself succeeded.


### Parallel stabilization reviews, no invented root cause
Compute create-info/entrypoint/owned module/layout review found no additional provable invalid parameter or lifetime bug (.local/arsenal-compute-ci-review/REVIEW.md). MT6991 driver failure still lacks original-machine per-API evidence; user requested own testing first, no external retest request. Separate shutdown failure/full-device-finalize lifetime candidate assigned to water_inventory in.local only, original source remains sole writer here.
Offline existing RHI copy caller analysis (.local/arsenal-copy-callers): readback memset95/2236=4.25% wholeRHI; snapshot draw memcpy40=1.79%, retire29=1.30%, texel22=.98%, readback15=.67%,uniform14=.63%. Not current candidate measurements or justification to remove required copies. No further genericcache/zero-elision promoted. Honor stopped, last76%charging/29C.


### Current-candidate memory reclaim regression
Honor PID21627 retained-diagnostic1ce83fcb chapter3 room→exterior survived9 logged memory reclaims in last initialization segment, including one level2 hint2 injected trim. Second repeated am trim emitted AMS exception (not counted). Process stayed21627 until deliberate stop. Warm admission/reclaim independent11550 SC-interleaving model found no race; no lock patch. Original MT6991 driver fault not reproduced and RG45589 times out: actual device/API boundary remains a substantive blocker, no speculative shader/sync change.
Private evidence trim21627/engine.log and last-session-memory.txt, screenshotout.png. Firstnaturallevel1 had1746ready/1worker; injectedlevel2 RSS2403528704→2160746496, all9calls retainoriginal log. Only last initialize memory segment used; no old .1 log mixed. Initial quick tail/grep missed the injection; complete session shows hint2, corrected here. Not FPS/audio/fullscene acceptance. Honor stopped and lent briefly to library IPC owner.


### Scoped initialization cleanup hardening
Init+cleanup dual-failure terminal guard implemented in adapter; existing fatal callback records init/cleanup errors and exits70 instead of returning to unsafe guest continuation. Success/early-init/cleanrollback unchanged. Independent review passed;8 extracted production-branch cases ASan/UBSan incl NULL,size,already-ready and exact old-code negative pass. Normal candidate342a363d Android/host0, unstripped symbols preserved. Device regression BLOCKED both offline: Honor old33621/43141 refused; RG45589 timeout; Bonjour/mDNS empty. No formal release, no Mali root-cause claim.
Build.local/arsenal-init-cleanup-20261118; source hashes preserved, sole test refinement after build recorded. This explicitly selects terminal process policy for this double-error branch, NOT graceful lifecycle recovery/global DeviceFinalize fix. No new APIs/guest ABI/graphics output changes. Independent .local/water-inventory-review-20261118/init-double-failure-review.md. Two-device discovery repeated after user turned wireless on; no server restart/security change. Need new endpoints or broadcast to continue normal-path real-scene validation.


### USB reconnect and normal-candidate scene regression
Honor USB reconnected A42BBB5109000333. Normal candidate342a363d installed, device APK fullSHA matched. PID29729 thirdchapter donor132001 room→outside via longpress Dpad21x4/20x3 observed, survived until deliberate stop. Short normal-path regression only; no Mali-root-cause/FPS/audio acceptance. 73% charging status2/26C. Brief library helper window, factory actual-movement regression next.
Private images init-cleanup29729/ff-out.png and ff-out2.png. Initial non-longpress batch did not exit room; corrected actual held Dpad route successfully exits. Do not count first batch as successful movement route.


### Normal-candidate factory movement regression
Normal342a Honor PID31635 isolatedfactory20261118094001 uncapped/mode1/keep1 Dpad3Rx3Lx6:27.0398s distinct21.8937FPS,p95 56.5759/p99 76.2009/max179.8834ms. Before/after scene confirms factory and displacement, no observed crash, deliberately stopped aftercapture. Not30FPS/fullcombat/audio acceptance; no perf improvement claimed from failure-only guard.
Private init-cleanup-ab/normal342a CSV/window/screens/thermal. Actual thermal values in captured files; no repeated keep2 experiment. RG now discovered10.12.80.90:34115, library low-load helper window before FF dual-device.


### RG normal-candidate factory comparison
RG mt6897 normal342a installed SHA verified, isolatedfactory20261118095001 PID2451 survived host disconnect and resumed. Same3R3Lx6 input sample28.9304s gives28.4476FPS,p95 38.4622/p99 40.9788/max45.8594ms,thermal0→0; deliberate stop aftercapture. Honor21.8937FPS/p9556.5759. Supports observed RG smoother in this scene, NOT strict chip AB: warmup longer afterdisconnect, endposition differs, actualdynamicresolution not matched. Both below target, no Mali9400/9500 fix claim.
Private init-cleanup-ab/rg342a.* and before/after captures. Host disconnect did not change PID2451. Needed existing clock helper transfer fromHonor toRG; missingkeep marker means compileddefault1, no marker change. RG handed to library helper; Honor to Ocarina isolated test. Original saves not replaced.


### Exact-build RHI source attribution
Exact normal342a RHI profile collected on Honor during6 held Dpad inputs in factory:2359samples/lost0, PID2771/TID3239 selects2326; separate same-nameTID4204 has33 and excluded. Matching26b6ff22 debugSO allows true source attribution. Indexed report/writeback overlap scans106 samples,pending_view overlap61; sort2 only. Independent review examining redundant query validity hoist, no optimization written yet. OriginalFF stopped; Honor Ocarina/RG library windows.
Raw private rhi-exact342a/rhi342a.data; .local/arsenal-current-lines/result342a-expanded. First automatedContinue was too early: confirmedtitlethenpressedagain, factory screenshotconfirmed beforeprofile. No title samples used.


### Minimal query-end candidate built and actual codegen verified
Query-end minimal candidate06426cc4 Android+host0; actual APK and independent debug SO BuildID caf273fc50dc98537cf5fdf45bf38166701c1c1a match. Actual indexed lambda now omits invariant query-valid branch while producer-range mvn/sub/cmp/b.hs remains at3ef8a4..b0. Predicate4million and actual-function20kstates/200kranges ASan+UBSan equivalence PASS. NOT installed, awaiting device windows for baseline/candidate AB; no FPS gain claim.
Only indexed_range_ready two overlap predicates specialized after original guard; no global helper change/cache/sync alteration. Build.local/arsenal-query-end-20261118, exact disassembly .local/arsenal-query-end-review/actual-candidate-disassembly.txt. Baseline342a retained for AB.


### Query specialization dualdevice AB closed without promotion
Query specialization tested, NOT promoted: RG query29.1208FPS/p9539.001 vs baseline29.0194/p9539.184; Honor query29.3686/p9550.9227 vs baseline31.3336/p9547.9611. Allthermal0→0, no observed crash. Short routes endpositions differ and DRS/cache not fully matched: no causal regression or gain claim. Existingbaseline342a restored on bothdevices, source indexed function restored exactly to savedprechange. No release. Honor79%charging29C, FF12684deliberately stopped.
Evidence private init-cleanup-ab/{rgquery1,rgbase2,honorquery1,honorbase2}; isolatedsaves95001/94001,uncapped/mode1/keep1,3R3Lx6. This also demonstrates initial Honor21.89 is not a stable invariant chip ranking; revised baseline31.33 changes that comparison. Do not claim permanently fasterRG. Historicalcandidate20e5f2d/06426 retained for reproducibility, not activeoptimization.


### Authorized standalone local test delivery
User authorized local standalone test delivery. Copied verified342a to /Users/yuyan9923/GitHub/游戏发布包/02-最终幻想_共鸣_FFRS/待验证/1.0.72-稳定性测试-20261118/FFRS-1.0.72-stability-test-20261118.apk,3798300B,SHA342a363d5958e6838b8f18b65307e0b72752c1dfa47e2536259df0aaf1878fee. Samecode72/signerd11967a3,scopedHonor/RG installs verified. Notes explicitly not original9400/9500 crashfix,notstable30/60/fixed1080; no query/keep2 promotion. Originalpublic72 SHA0fb453ad preserved. No community/online module release.
Chinese notes+SHA+verificationJSON copied/readbackverified. Internalversion unchanged deliberately reuse identical testedbytes; filename/hash distinguish. Latest fixedscene setup PID14513 deliberately stopped for delivery; no new performance evidence from that setup.


### Actual version73 standalone test delivery
User requested actual73: manifest code73/name1.0.73 built Android0/host0; APK7f3e71552aee32c3b0bbb2cd765d50f6be3960d7ae0af511ba23a660210184c4 same signer d11967a3. ZIP comparison with tested342a differs ONLY AndroidManifest.xml, native/assets/classes identical. Honor and RG install-r success, installedversion+fullSHA match, no uninstall/data clear. Delivered separate 待验证/1.0.73-稳定性测试-20261118/FFRS-1.0.73-stability-test-20261118.apk +notes/SHA/JSON, old72 preserved. No Quark/community upload.
Prior scoped gameplay evidence transfers only because all runtime entries byte-identical; no new fullgame acceptance. MT6991 originalcrash and stableFPS open. Highercode means avoid blinddowngrade/uninstall; Chinese notes updated. First publicationPython stdin encoding error made nofiles, retriedwithcodingUTF8success.


### Player73 dungeon combat feedback
User supplied screenshot libfile_ca2d6fdb7bc88191addd670b7dc80545: player says overall improved to around58FPS, but combat20plus; location inside dungeon, outside test not yet provided. This is chat testimony, not game frame capture/device evidence; no model/settings/oldbuild controlled comparison known. Record as 1.0.73 user feedback, no stable58 or causal improvement/regression claim. Published73 bytes unchanged; next target isolated dungeon combat reproduction.
Private image user73-feedback/image.png, SHA 989b5f6d887b89d2d2ed1ec0cad366871251bed67f32f1001b48367587784844; no identifying chat screenshot in Git.


### Confirmed73 MT6991 recurrence
New user9400 report160005 confirms actual1.0.73/code73/APK7f3e7155 on vivoV2405A/MT6991 Android16. Two exit139 records; retained fault SIGSEGVfa0x8/vendorMaliPC+1eda09c/LR+1eda024, same signature as72; lasttarget073ce3549023words/frame1174. Therefore73 did NOT resolve this originaldevice fault. Lastshader adjacency still notAPIcausality. Screenshottitle visible, notfailurebeforeanylaunch. No perAPIbreadcrumbs in ordinary73 report. Performance work paused for priority stability; publishedbytes unchanged.
Private crash73-new/report.zip SHAa1c6f559111f52defdce905edaf7df5c6e8a2048687586892248e69a98e5abd4; read-onlysafeZIP, no save. Two139PIDs19877/24477; otherforce-stop/prestart exits distinct. Userconfirmscombatperformanceissueisfourthchapterdungeon. FF16617deliberatelystopped, no newcombatFPS measurement.


### Native crash evidence preservation prototype
MT6991 target descriptors independent review found no proven invalid binding/lifetime. New concrete observability defect: custom native handler _exit139 suppresses native crash classification. Honor isolated shell subprocess test exit mode exited139 vs restore-and-return mode SIGSEGV11 with originalPC/fa8 and3frame Android tombstone. No originalAPK/source behavior change yet; independent review of bounded Android synchronous-fault forwarding requested. Published73 unchanged; not crash-root-cause fix.
Evidence .local/arsenal-native-crash-chain/{probe.c,device-test.txt,crash-buffer.txt}; AOSP debuggerd source current main read-only reference. No security/SELinux change; log hint ignored. No game process started or saves touched. Honor79%plugged/status3/26C. Production integration remains pending correctness review and app-context export verification.

Production-forwarding caution: independent review found previous_actions/owns_actions are thread-local while sigaction is process-wide; a driver/RHI fault cannot safely use zero-initialized worker TLS as saved debuggerd. si_code>0 also does not guarantee retriggerable instruction (TRAP/asynchronous MTE). Review tool blocked further signal-chain work; no production integration attempted. Isolated probe is evidence only, not approved general solution. Target descriptor review (.local/arsenal-cache-concurrency-review/DESCRIPTORS.md) found no proven count/null/offset violation; no speculative patch. Honor lent to OOT10min, RG available to library; no FF input/build active.

### 73 reclaim follow-up
Read-only target feature and reclaim lifetime audit .local/arsenal73-reclaim-audit/REVIEW.md. Corrected interpretation: reclaim images=0 is reclaimed count, not resident count; no inconsistency with MEMORY_COUNTS image_bytes248383872. Existing production memory-policy/reclaim harness8tests ASan/UBSan passed6.245s exit0. No proven new stale-handle fault or shader capability mismatch; no speculative source change,73unchanged. Still need originalfaultAPI/native stack for targeted fix.

### Fourthchapter combat route resumption, NOT a combat measurement
73 Honor PID22651 then deliberately restarted PID24315, isolated94001/mode1/fps0/keep1. Actual Dpad route crossed lava pipe platforms and western stairs/scaffold, screenshots combat73/route1..15 and west1..6. No enemy battle triggered in this window; no attack-window metrics produced and HUD17–40 is not reported as measured combatFPS. One inspected in-game options screen stayed Normal difficulty; no options changed. Existing cheat-plan read has only header/BuildID/base, no patches. West scaffold lever interacted once in isolated run; publishedAPK/originalsave untouched. Deliberately stopped24315 and returnedHonor to library5minUI;74%charging31C. Engine navigation log retained privately; next task remains deterministic encounter reproduction, not assumed random-encounter combat.
Repro western route fromContinue:12left,1right+6up,6right,1left+8down,10left→lever. Narrow pipe forward/up attempts encountered geometry boundaries and stopped; they are excluded from movement-performance samples. No passive paused performance test, no new source/build.

### 73 actual fourthchapter combat reproduced
HonorPID26943,73/uncapped/mode1/keep1,isolated94001; continuous3Left3Rightx15 triggers two mechanical enemies. Earlier sparse navigation did not trigger, not a save/encounter defect. Actual A12 attack window battle73_a8.900357809s,13.2579distinctFPS,p95135.675/p99169.203/max246.264ms,thermal0. Before/after showbattle, one enemy defeated duringwindow. Existingcache preserved, not certifiedcoldcache. Lastinitialization ONLY frames3644–3762 has119stallrows:shader731.5ms,pipelinecreate27.3ms;96rows bothzero stillgapmedian62.9ms/GPU48.94/CPUsegment44.47. Bothnewshadercost and sustainedCPU/GPUload; segmentsnotadditive. Initialallhistoryparse484rows was discarded, correctedlastinit119rows saved in battle73_a-stall-index.json.
Secondbattle73_b8.971s13.376FPS endedVictoryscreen, EXCLUDED as purecombatwarmAB. No optimization/regressionclaimvsotherenemy/device. Private evidence combat73 CSV/windows/screens/thermal/logs; title/mapnavigationexcluded. Deliberately stopped26943,74%charging32C. NextpriorityAnka runtime/device-lifetime comparison;73bytesunchanged,fullaudio gateunverified.

### Anka mechanism applicability
Read-only exactAnka b2c0f4cfc37a8c75fad1d36f3e912ef0820416e4 comparison .local/arsenal-anka-adaptation-review/REVIEW.md: capability-gated creation, cache identity/returned-error fallback, internally synchronizedcache and process-lifetime resources. No copiedGPL implementation/no broadvendoroverride/no proven9400workaround. Invalid-cachefallback is a separate potential resilience change, NOT justified as thisframe1174SIGSEGVfix; must preserve device-loss/OOM/error semantics. IndependentFF lifecycle/featurechain audit continues.

### Conservative pipeline-cache retry candidate
Official vkCreatePipelineCache reference lists OOM/UNKNOWN/validation errors, not a generic invalid-cache error. Independently implemented one empty-cache retry ONLY for UNKNOWN with nonempty disk data; every other error and failed retry remains fatal. Output reset prevents undefined failed-create output being treated as owned. No signal handling, device-loss retry, null-cache-success or cache-file deletion. This is resilience only, not evidence of MT6991 frame1174 root cause.
Actual production function compiled/executed with fake driver: 11 success/error/empty/retry cases pass ASan+UBSan. Android build launched session63199, .local/arsenal-cache-retry-20261118/run.py + build-plan.json/android.log/exit-code recoverable. No installation yet, published73 bytes unchanged.
Reference: https://docs.vulkan.org/refpages/latest/refpages/source/vkCreatePipelineCache.html . Anka architecture comparison only, no GPL source copied. Device-lifetime independent review finds general Finalize teardown gap but no Finalize in supplied retained logs; PPSSPP feature-chain/fixed-swapchain-array examples do not match FF current implementation. Evidence .local/arsenal-device-lifetime-review/REVIEW.md.
Android cache candidate built successfully SHA5a5262f8d07ca302b425be82635ac5af114c2c3c0c538e77ea77647e30d88f33. Honor install-r passed; PID29923 Continue isolated94001 reached visible fourthchapter factory, directional short input survived. Screenshot/log retained .local/arsenal-cache-retry-20261118. This verifies ordinary successful-cache path only, no real device UNKNOWN was induced. No performance/audio/fullcombat acceptance. Deliberately stopped and restored immutable original73 install-r success; no save replacement/data clear. Additional fault branch Android integration remains unverified; host production-helper injection is separate.

### V1 pipeline-cache header validation gap
Official VkPipelineCacheHeaderVersionOne VUID04967 requires headerSize32 (and08990 within payload); existing loader checked version/vendor/device/UUID but not size. Added exact32 guard, preserving cache files and successful valid path. Actual production loader extracted into ASan/UBSan test covers valid cache, missing/truncated data, six wrong sizes, wrong version/vendor/device/UUID. All pass. This is damaged-input resilience, not evidence target9400 cache was corrupt.
Reference https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCacheHeaderVersionOne.html . Android candidate build .local/arsenal-cache-header-20261118/run.py session96730, separate from prior candidate and immutable published73. Two independent read-only reviews continue resource lifecycle and compute-barrier visibility. No blocked signal work resumed.
Header candidate Android build0, Honor install-r/isolated94001 Continue normal path PID30954 reached factory after loading; screenshot.local/arsenal-cache-header-20261118/factory.png. Deliberately stopped, restored73same-signature and installedSHA verified. No corrupted real cache injected/deleted. No FPS acceptance inferred from screenshot; combat remains earlier13.26FPS.
Independent compute barrier review found render-pass external dependency covers attachment accesses, so no speculative extra barrier. General surface finalize concurrency needs production-linked deterministic test, delegated descriptor_review; target incident no positive causal evidence.

### Deterministic window lifetime interleaving confirmed, integration not yet safe
Re-ran source-extracted present_state_of/WindowFinalize test in.local/arsenal-lifetime-followup/test_window_interleaving.py under ASan/UBSan: old admitted present can resume after same-generation backend teardown; new admission after close correctly rejected. Actual73 async0 requires a second guest thread for this overlap; supplied crash has no such proof.
Independent closure review: a common admission lock held through present enqueue and finalize drain can order existing queued presentations without worker acquiring that lock. But integration MUST include WindowInitialize (currently creates backend under window lock), mtl_present direct xbg entry, shared queue engine serialization, and stale surface handles after automatic rebuild. Do not replace stale-handle guard with NULL unconditional destruction. A lock solely around WindowFinalize or device-idle is incomplete. Design narrowing in progress; no production lifecycle patch/false crash fix claim.

### Standalone lifecycle protocol component
Independent implementation xbg_surface_lifecycle.h is intentionally NOT included by production. Actual-header deterministic ASan/UBSan contract test passed and independently rerun: nested admission, close-first rejection, old queued producer completion, stale owner refusal, new generation, failed setup closed/unretired requiring cleanup, exception lock release. Logical token distinct from Android generation. Full C ABI/backend/entrypoint integration draft delegated in.local/arsenal-lifetime-followup/proposal, no production lifespan fix claim yet.
RG cache-header candidate e220f4c8 installed-r, isolated95001 PID9835 reached visible fourthchapter factory. Original game data/save not replaced. Normal cache path only; no actual corrupt disk injected. Background/foreground short roundtrip pending final capture; restore73 before release.
RG header candidate PID9835 completed short direction input, Home→same Activity foreground roundtrip; samePID resumed visible factory.97%status3/30C. Captures rg-factory.png/rg-resume.png and rg-engine.log archived in candidate folder. Deliberately stopped; original73 install-r and installedSHA7f3e7155 verified. This expands normal-cache smoke acrossAdreno735/MaliG615, NOT9400G925 evidence, no fullcombat/audio acceptance. Devices returned to library.

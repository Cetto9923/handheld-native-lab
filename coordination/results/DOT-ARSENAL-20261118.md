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

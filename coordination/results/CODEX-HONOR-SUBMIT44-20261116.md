# CODEX-HONOR-SUBMIT44-20261116

## 输入与诊断

43源4a52571/记录ebccd39，APK e92965c5，Honor A42BBB5109000333 PID7643。42纯移动trace c4854f7e复核RHI25942在5.902619秒内CPU运行5320.17ms，睡眠509.45ms，Runnable24.56ms。43先采1b1f11a5遇敌混合，排除纯探索验收。逃走后重新采43纯移动833b63e1db6c61a07b0073564d648570aedca9dfe6293aef059930631340993b，前后截图均神殿内部桥段探索，3轮南北非零轴且最终归零。RHI7839运行5494.24ms，trace约5.91秒。栈含展开错误，inclusive项重叠，不能相加作exclusive占比。描述符构建、malloc和模板更新高频。普通FPS与Perfetto窗口分别处理。

原始证据.local/honor-submit44-20261116/，两个43 trace及lib43.symbols.so均保留。43内存快照PSS4622223KiB/RSS4733872KiB，属已运行较久探索阶段，后续候选需同路径与时长比较。

## 候选

Arena::destroy原先对所有VkBuffer销毁均增加全局memo_epoch，导致所有16槽惰性清空描述符缓存。44追踪缓存构建实际绑定的uniform/storage/C1/support/driver和驻留uniform-texel Buffer所引用槽。销毁时使这些槽失效并移除handle记录，进入槽的memo查找前正常unpin，front generation同步变化。无引用临时buffer不触发全槽重建。记录可能含已清缓存的保守引用，最多造成额外失效，不会漏掉实际引用。动态uniform命中仍刷新且核buffer身份，存储快照、texel generation、Image注册revision/identity及sampler全局epoch保留。现有8192 pinned上限及Arena内存回收规则保持。所有GPU销毁时机沿既有fence退役路径。

复用gfx-live-ab=memobuffer，保留原全buffer全局epoch作为同进程对照，两个分组都追踪引用以支持安全切换。PERF增加累计destroyed/referenced/slot_drops/tracked，采样不新增文件或UI。shader字节、NoContraction/FragDepth、guest时钟、音频配置不变。

生产函数提取编译ASan/UBSan覆盖无引用释放、跨槽引用含slot15、重复引用、handle复用、global sampler epoch、原路AB和8193边界及1000次释放循环，test_render_policy共6项通过。实际场景效果仍待验证。

与Khronos描述符缓存及动态offset建议核对，本次按现有实现缩小缓存失效范围，未复制示例代码。资料 https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html 。外部示例的帧率不作为本游戏性能证据。

## 待完成

宿主/Android全量构建、存档保全安装、同进程旧新旧普通移动及火焰群攻技能、显示/内存/冷暖与声音回归。用户补充最卡是当前位置遇敌后第一位主角的四字火焰群攻，名称待从实机菜单确认。发现PAD隐藏与位置变化后根停止输入，等待输入归属回复。独立QA NOT_RUN。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 构建检查点

源c27ad9c首次宿主检查112/113，arena-memory-policy旧fixture仅提供memo_epoch，生产Arena改为调用memo_forget_buffer后提取片段缺依赖。已更新该fixture验证资源释放前逐个调用失效钩子，所有既有guard、回收数量、Android/Mac分配量和16槽GC断言保留。实际slot引用与global对照语义另由本轮生产函数提取测试覆盖。组件合计9项ASan/UBSan通过。首次完整日志保留build44/，下一构建build44b/。

44b源dc032b3，113宿主/Android0/79输入前后一致，APK684a9e772466da57f777b727e743f9a551ed3912de087fe6f75dc01ec9fe39ec/3720407B。安装回读摘要和9文件SHA及正式源metadata通过，PID20331复用461，memobuffer原路启动。用户再次明确交还输入。43临近遇敌触发窗口74460..74580/22.93FPS混入战斗菜单，不作纯移动证据；当前战斗只出现拉斯韦尔，未取得雷因火焰技能基线，后续在44原路控制采集。

后续源复核发现新引用表关机清空前必须先显式清除所有槽的memo。原实现依赖buffer销毁的全局epoch延迟作废，shader/layout/pool销毁后不应保留缓存。已在destroy_slots现有16槽循环内先memo_unpin_all再texel_evict，保留栅栏等待顺序。测试增加实际源码shutdown全槽清空先于texel回收和descriptor pool销毁校验，组件通过。该修改尚未包含在684a9e77中，44c另构建后再安装。

## 44c构建与44b实测续记

44c源20e4ef6，113宿主检查及Android构建退出0，79输入前后一致，APK4574ccc5346482b90e891c5adfad863d0e7abe305c4e5869c89cd0c5de7e01f1/3720407B。尚未安装。设备仍44b/684a9e77/PID20331，调试输入由用户再次明确交回root，PAD显示、短按后全部松开。

同进程普通探索移动A1旧全局失效20760..20820，60间隔18.3261FPS/P9985.41ms，B1新22980..23040，60间隔26.1573FPS/P9959.05ms。A2遇敌导致不足两个CSV刷新边界，无有效FPS，原始数据保留并排除纯探索。战斗后A3旧57420..57540，120间隔24.2781FPS/P9957.60/max63.27ms，前后均神殿内部断桥探索、非零轴最终归零。A返回明显高于首轮A，18→26不能全归候选，继续成对复测。新路memo misses约225..308/帧，旧约648/帧，说明缓存机制生效，FPS单项收益待证。

主角实际技能已从菜单确认是雷因“消失突袭”，MP78，说明为对全体敌人火属性物理攻击。44原路同战斗3只Lv1钢蝠、两位角色均在，雷因HP6369/6372。43只出现拉斯韦尔的原因未确定，不据此诊断阵亡，本轮未使用回复或金手指。

fire-A1-global为旧memobuffer、shader_reuse0、无CPU/GPU探针及中途截图/录屏的实际首次技能释放。确认A输入monotonic140003.022s，后8秒39149..39268共120帧，15.0581FPS/P99285.82/max319.78ms，56帧超过50ms、15超过100ms、present错误0。此窗口含技能与胜利转场，不作纯技能全程FPS。39200帧319.8ms，其中shader加载261.8ms、submit CPU299.41ms、GPU23.53ms、pipeline创建0。首次技能存在明确同步shader加载开销。旧其它技能暖态shader0的负荷结论仍限定原窗口。

运行时6a530aaf1b6615a3/cf90d03fc8ba73b8等实际compute模块改写约51..106ms，日志同时证实这些key本进程已在warm加载过。启动现成gfx-shader-reuse=0使预热输出未保留，runtime仍重复改写。下一步沿已有模式2逐字比较warm/runtime输出，确认后再测模式1，保留64MiB压缩缓存预算及exact-input拒绝机制。先重复原路同技能暖态，避免将首次预热收益归给descriptor候选。冷暖/完整画面及A2DP常设音频验收尚未通过，真实稳定30与独立QA仍NOT_RUN。

## 暖态与预热输出校验

44c/4574ccc5已安装回读，9文件SHA及正式源metadata保持。原路暖态消失突袭8秒18.3459FPS/max126.69/9超100，2敌非同首轮3敌。mode2新进程27168同3钢蝠完成451次warm/runtime逐字比较，0输出不等/0输入不等，5个冷加载热点均equal1。正在同44c重启mode1验证复用收益，memobuffer维持旧路。稳30/显示长回归及A2DP未通过。
模式2保留正常优化并额外解压比对，首次技能窗口14252..14384/133帧16.579FPS/max299.99，只作校验诊断，不作复用性能收益。缓存仍64MiB上限，retained1436/denied351/peak67107854，部分typed变体被预算拒绝后正常回退。证据fire-A2-global-warm、fire-reuse2-compare及reuse2/。A3旧24.2781、B2新27.0464、A4旧25.8995，均内部同桥短移动；单项收益弱于首轮差值，仍需独立重复。

## 模式1加载收益与45候选

44c模式1 PID29319首次消失突袭命中5个compute模块，rewrite2.23/2.26/5.01/3.80/8.32ms，旧0模式约51..106ms；451次模式2比较全等。首次窗口2钢蝠14871..14999/129帧16.1533FPS/max173.37，仍有63超50/16超100，不能称稳定30，敌群数量不同不作整窗因果比较。现live-ab none新描述符路，准备暖技能60帧CPU诊断。45源码将已验证Adreno profile缺省复用1，保持显式marker/env优先、64MiB、exact-input验证及失败回退，7项组件通过，准备全量构建。
同帧仍有shader77.4ms和shader0/CPU约100ms/GPU约66ms，复用只覆盖部分加载成本。普通技能fire-reuse1-first，后续诊断单列。设备本轮2个43 trace已逐一核对主机和设备SHA后清理设备副本，共10899586B，完整trace及符号继续保留主机。

## 45构建安装与进一步负荷证据

45源9c85fa0/APK70db6d98d51e7aa81a00ee8144f4e58e92883ff82ce8303f07ae97128ea15781/3720407B，113宿主及Android退出0，79输入一致，安装回读与9存档SHA及正式源metadata通过。归档原gfx-shader-reuse标记以验默认，启动日志明确默认reuse1、warm1935，PID5377复用461。紧随am start的pidof过早退出1，随后同次启动5377正常已记录。当前探索入口，准备进入神殿继续测。
44c/29319新描述符路暖技能CPU诊断39152..39211共60帧/3.662714秒，67776draw/5369dispatch。CPU计时measured2.937488秒，descriptor exclusive1.103757秒、其中ImageLookup387.112ms，remaining491.449ms；snapshot262.592ms、draw390.359ms、fence316.918ms。部分范围含等待，不能全算CPU运行。DESC coverage OK，但诊断额外2881578次时钟读取，不能将该窗FPS作普通验收。此窗新shader1及pipeline miss1，pipeline最大217.876ms，保留新编译长帧，不能称完全暖缓存。B3-scoped-reuse1原始28.4402FPS/180间隔的后截图为遇敌黑场，排除纯探索移动验收。

## 45普通移动、GPU分段与暖技能CPU栈

当前45/70db6d98/PID5377/default shader reuse1/新descriptor路径。B45-moving1前后探索非零轴12600..12660/60间隔23.4315FPS/P9972.75。后续trace45-encounter-walk前后探索、轴3轮，53580..53880/300间隔29.1226FPS/P9957.94/max61.0/8超50，仍未稳30。

技能GPASS8帧23463..23470，场景1384x780，末UI1920x1080，pass+compute约24.2..31.7ms，属于改变批次的诊断。五个已校验compute模块在45默认配置均reused1，rewrite2.28..5.03ms。该次首次技能仍出现271.8ms长帧，其中shader140.1ms，多份顶点模块和变体载入聚集。复用覆盖了部分加载开销，其它分析和运行时负荷继续存在。

第二次消失突袭6秒Perfetto为95a09106334d11aed0f320ccacb9bae9075b1e47393081ef447d7cfeba069478。匹配符号BuildID为f16061d9db8428ab1daa7139064734a9032a28b4，RHI5679运行5202.26ms，样本窗口5.9301秒。

Perfetto将MONOTONIC归一到BOOTTIME，clock_snapshot给出146158443482074-145839938495302=318504986772ns偏移。实际A145840270000000ns落于trace开始约0.332秒。原辅助脚本将6秒采样后不足8秒的CSV写为8秒窗口，已保留action-metrics.unbounded-before-correction.json，改为强制检查CSV尾覆盖。当前仅3秒请求覆盖68371..68437/67帧/2.943741秒，22.4204FPS/max78.14ms，限定CPU诊断窗口。其它五份14秒采集的8秒窗口复验均覆盖完整。

RHI inclusive采样约431条，422条含INVALID_ELF展开问题。已解析部分中descriptors_build153、descriptors123、memcpy62、texel_acquire34、snapshot33、stream_buffer29，inclusive存在重叠。匹配地址转为十六进制后addr2line定位memcpy主要来自draw5448顶点snapshot及5449索引snapshot。普通日志每帧vertex约15..16.4MiB、index约1.4MiB。host桥只有返回0的dirty_since stub，dirty_tracked/protect/now缺失，现有跨帧vcache所需的内容版本前提仍有缺口。后续研究有界完整内容比较的静态顶点/索引复用，或先证明精确fetch范围再缩小snapshot。继续保留CPU访问、GPU pending覆盖和不可变提交快照。

LevelUp详情短按A多次未离开，最后1秒A正常回探索。一次在详情页发送的方向只作导航记录，该动作排除探索采样。profile-skill45脚本、trace、符号和CSV均已保全，设备上该45临时trace仍待主机验证后清理。当前战斗施法后，输入已UP。完整显示、A2DP、跨设备及稳定30目标继续开放。

## 46顶点索引复用候选

45暖技能Perfetto显示RHI运行5.20/5.93秒，vertex/index memcpy是热点之一。宿主缺dirty_tracked/protect/now，旧vcache默认关且无法复用。

46保留现有vcache框架，缺真实tracker时每次全字节比较CPU镜像。镜像和GPU拷贝来自同一次捕获，变化后分配新GPU副本。快速变化仍回退每提交snapshot，600槽后重试。GPU有效数据和CPU镜像合计64MiB，arena对齐与未退役提交另计。真实tracker路径保留，池删除释放镜像，关机队列空闲且退完槽后释放vcache。

默认继续关闭，使用既有live vcache开关对照。生产提取ASan/UBSan覆盖首尾变化、虚假dirty_since、真实tracker、冷却、池删除/地址复用、预算flush、旧GPU副本保留、1000次释放及关机顺序，与已有组件共11项通过。Android构建及实际收益待测。

验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md，A2DP、显示、稳定30仍开放。

46源b7ff0e3 clean，114项宿主全部通过，Android退出0，80项源码输入前后SHA一致。APK b4455701c8235a0557b9112661c1b0b9b88a63414ce461d79530cb06cf618fc0，3720407B。安装回读SHA及私有副本461全部9文件SHA、正式源metadata一致，证据build46/install46。launch46有界轮询PID15697，仍使用461，vcache0/texrecord1、新描述符路径及45默认shader复用1。当前启动预热中，尚未取得46性能结果。

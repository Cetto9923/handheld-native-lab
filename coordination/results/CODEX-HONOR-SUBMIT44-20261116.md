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

## 46初测与47冷却修正

A46-control-moving1为神殿内部桥面真实移动，前后探索，4560..4800/240间隔24.4858FPS/P99103.78/max112.58ms，25超50、3超100。B46-content-moving1中途遇敌，5400..5580原始18.8415FPS只作混合诊断，排除探索对比。

46开启后部分vertex/index复制由snapshot转到vcache，后续战斗静止菜单窗口仍fill约5.6MiB/帧、compare约8.08MiB/帧，累计flush38且volatile0。说明复制计数的迁移不足以证明减少总复制或FPS收益。一个游戏帧包含多次slot提交，缺tracker的完整内容比较不适合按8次提交判断逐帧变化，预算flush也会忘记动态范围。

47仅对内容比较路径按present差判断8帧内变化，真实tracker仍按原slot策略。预算flush保留未过期动态范围冷却记录，map最多16384项，满表仍可服务既有条目，新范围回退正常snapshot。新增多提交单帧、flush后保留冷却、满表上界回归，生产提取9种运行情形和既有组件共11项通过。默认仍关，待完整构建及同进程对照。

45临时trace已核对主机与设备SHA95a09106后仅删除设备文件5127903B，完整主机trace/符号保留，清理索引profile-skill45/device-cleanup.json。

46关闭缓存后的首次消失突袭为2敌Lv1钢蝠及妖藤，12862..13014/153帧、7.96527秒，19.0828FPS/P99151.50/max207.34ms，57超50、9超100，无present错误。此为首次技能及可能胜利转场8秒窗口，未做纯效果FPS结论。

47源f9ead5d clean，114宿主/Android退出0，80项源码SHA一致。APK faa7b9003bd7c150256127488decf3b1d0fcaea54d744e46aa0ef5ce162fe484，3720407B，安装回读及私有461全部9文件SHA、正式源metadata通过。launch47 PID18107，默认vcache0，准备内部移动同进程对照。

## 47缓存负收益与撤回

47的A47-control-moving1为内部探索6480..6540/60间隔23.9400FPS/max57.95。B47-content-moving1结束已遇敌黑场，7320..7380原始19.7285排除探索对比。首次消失突袭同2敌Lv1钢蝠及妖藤，11782..11914/133帧7.951秒，16.6017FPS/P99188.79/max197.20，64超50、14超100。它与46的19.0828跨进程首次窗口不能单独定因，但没有改善证据。

进一步使用moving-clock.py，日志改为monotonic，真实非零轴开始至UP归零作为边界，等待CSV缓冲尾覆盖完整窗口，再计算其中全部连续呈现间隔。没有删减区间内长帧。两段前后均为同一内部断桥探索，角色移动可见。

B47-content-moving2开启缓存24853..24914/61间隔，实际轴2.565秒、呈现2.5258秒，24.1508FPS/max58.82/7超50。A47-control-moving2关闭26504..26570/66间隔，轴2.583秒、呈现2.5498秒，25.8844FPS/max50.69/1超50。限定短窗口，未稳30，候选无收益。已live关闭，源码撤回46/47的全内容比较缓存及专属测试，全部实验APK与补丁提交保留备查。

45 trace中RHI CPU分布为0/1/2约57ms、3核704ms、4核1403ms、5核1290ms、6核1749ms，7核0。47当前RHI18426允许0-6，主线程允许0-7，vip cpuset0-7。sysfs实际拓扑0-2最高2016000、3-6为2803200、7为3014400kHz。run-as查询其它线程taskset被SELinux权限拒绝，没有修改亲和性。不能据此将丢帧直接归因到核分配。

设备API36，dumpsys performance_hint明确Hint Session Support true、preferred16666666ns。下一候选使用官方ADPF Performance Hint按真实渲染工作周期报告，保留系统调度和热管理，默认先关。参考https://source.android.com/docs/core/perf/performance-hint-api与https://developer.android.com/ndk/reference/group/a-performance-hint，2026-11-16实际获取，无第三方代码复制。旧NDK26.1的C接口为API33，因此动态解析并在不支持时回退。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 48官方工作时长提示候选

新增默认关闭的既有live-set项adpf。Android侧动态解析4个API33 C接口，缺符号、服务或session拒绝时仅停用提示。首次提交开始同一帧工作计时，后续提交不重置，在呈现前、原帧率等待及cfg.present之前上报MONOTONIC实际间隔，目标33333333ns。渲染线程更换时释放旧session再建新session。上报失败关闭session且停用至显式关闭或重启，shutdown停完异步线程后先释放session，覆盖未初始化及设备错误返回。其它平台为空操作，30Hz目标候选仍需实机证实。

组件生产提取涵盖默认关闭、非Android、库/符号缺失、manager/session拒绝、每帧多提交、呈现后idle排除、非正时钟、线程更换、上报失败、1000次启停/renderer重启与资源计数。8项组件通过。两次初始夹具编译分别因非Android分支未使用test和RTLD常量而Werror失败，均已修正，原始失败日志保留。完整宿主/Android及实机收益待测。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

首轮48源577fc01宿主114项通过，Android编译退出2，没有生成APK。即使是decltype取API33函数类型，NDK对minSdk28仍报unavailable。已改为等价函数指针声明，保持动态解析与minSdk28。新增真实NDK交叉编译，分别以API28编译生产适配器、API33静态比对4个函数指针与SDK原型全等，连同组件9项通过。原build48失败日志保留，后续build48b全量重跑。

48b源9f06fba clean，114宿主全通过、Android0，80项输入SHA一致，动态符号表无强依赖APerformanceHint接口。APK49321c68aaefaadc51fd4ca3e186077da1e359464a969fec381893fe3229367a，3720407B。install48回读APK及私有461全部9文件SHA、正式源metadata通过。launch48 PID24538，texrecord1/vcache0/adpf0，原正式存档与APK历史保留。尚待API实际session及同机FPS验证。

## 48系统提示接收及短移动对照

ADPF service确认24538的session绑定TID24719，实际comm为RHIThread，target33333333ns、AllowedByProcState true、ForcePaused false。日志前3次上报37.17/28.21/31.22ms，之后count300与600仍上报，未见接口错误。关闭后服务中24538的session已移除。

同进程桥面两轮非零轴、前后探索。A48-hintoff-moving1为5305..5357/52间隔、轴2.592秒、呈现2.48769秒，20.9029FPS/max84.31ms/12超50。B48-hinton-moving1为7627..7690/63间隔，轴2.603秒、呈现2.53921秒，24.8109FPS/max69.01/4超50。回关A48-hintoff-moving2为8337..8395/58间隔，轴2.581秒、呈现2.50651秒，23.1397FPS/max69.22/10超50。末图仍为桥面探索，遇敌圈已红。均无present错误。短暖窗仍未稳30，不能把初始20.9到24.8全归API，尚待重复、技能和完整音频验收。当前API关闭、输入UP。

45原CPU trace的cpufreq与RHI运行片段交集约5.00秒，3-6核加权约2.50/2.66/2.65/2.65GHz，记录45-rhi-frequency.sql/csv，初始频率缺失片段未补值。48启动时thermalservice状态1，完整温度快照launch48/thermal-before.txt保留。

下一最小假设为OP_BIND_UBO/SSBO无条件递增buf_gen。fill_driver的graphics c0只发布SSBO范围及base_instance，compute c0还发布UBO。应先逐字节验证graphics UBO变化和完全相同范围重绑对c0输出的影响，保留compute UBO与SSBO变化、base_instance、snapshot失效及全部真实绑定状态。API调度继续保留为默认关闭的候选。

## 49驱动块版本候选

OP_BIND_UBO/SSBO继续原样写入每个真实绑定。仅当driver c0实际使用的表范围变化时递增buf_gen，即SSBO范围变化或compute UBO范围变化。graphics UBO变化及完全相同地址/长度重绑可沿用相同driver块。原slot_serial及base_instance缓存键保留。通过既有live-ab drivergen可恢复原每绑定递增，none使用候选，增加PERF driver_gen skipped计数。没有改变uniform复制长度、snapshot版本、shader或图像元数据。

生产提取整个绑定case、完整fill_driver及真实base_instance/cache helper，ASan/UBSan验证4096字节驱动块在graphics UBO变化时全等、compute UBO及SSBO变化时更新、64位地址、大小变化、16槽全部graphics UBO、完全相同重绑、slot/base_instance变化和原路控制。连同系统提示及既有组件10项通过。FPS/显示/音频收益待实机。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

49源4b0fa32 clean，114宿主及Android0，80项输入SHA一致。APK6c8a4b7ec367d7c9b360001f1a12e33e3f8c75dbb7fac5bb2019e4b0d2107b7b，3720407B，install49回读APK及私有461九文件SHA、正式源metadata通过。launch49以drivergen原路、adpf0/vcache0/texrecord1启动，复用461，准备神殿内部真实移动对照。48再次开启ADPF后RHI亲和性仍0-6，状态快照B48-hinton2保留，未将其表述为已启用prime核。48后续0.8秒前进仍探索，尚未测其首次技能。

## 49普通移动与首次火焰群攻

PID27718同一内部桥面，全部moving-clock真实非零轴、前后探索。原A1为11437..11487/50间隔19.6768FPS/max89.22ms/19超50。新B1为13319..13383/64间隔25.5260FPS/max65.46/2超50。回原A2为16006..16068/62间隔24.4505FPS/max70.29/8超50。新B2为19830..19895/65间隔25.3060FPS/max68.95/5超50。每段轴2.55..2.63秒，源4b0fa32，adpf0。较近的A2/B1/B2差距有限，不能把初始19.7到25.5全归新逻辑。PERF确认每帧约4134..4383次driver_gen更新被跳过。真实稳30仍开放。

继续前进2.5秒正常遇敌，雷因消失突袭，2个Lv1钢蝠，输入前MP421。普通14秒采集覆盖完整8秒动作窗口，34459..34618/160帧7.9652秒，19.9618FPS/P99149.63/max215.03，46超50、12超100，无present错误。动作包含施法及可能胜利转场。frame34477 shader131.9ms、submitCPU206.11/GPU18.08；frame34483 shader0、submitCPU102.37/GPU67.00、sync_wb10。既有显示完整验收及A2DP仍未执行，结算截图角色可见，仅作局部观察。

49初始化日志已核对legacy_waitall0、completion_wait1，快速等待路径已生效，不能重复按旧默认推断锁等待。源中已有partial_overlay_check及模式5双输入GPU对CPU字节比较，历史2026-11-05的目标shader3b6302a58b86a34a binding47与可写SSBO17双输入游戏比较仍NOT_RUN，先续证据。当前helper仅启动时读标记且每输入最多8次，拟复用同一标记增加运行时启停，防止名额被标题或别的场景消耗。该阶段继续执行原同步，性能采样单列诊断。

只读检索Mesa Android构建文档和bylaws/libadrenotools原仓库，后者BSD-2-Clause、Android9+Arm64，提供应用内自定义驱动加载。此方向仍是研究，尚无本游戏及Adreno735实测，GitLab设备源码页面本次访问失败；没有取得固定revision或集成代码。资料为https://docs.mesa3d.org/android.html及https://github.com/bylaws/libadrenotools，不采用其它游戏或容器演示的FPS作为本游戏证据。

## 50复用既有双输入校验的运行时启停

复用gfx-partial-overlay-check，同一0..5解析器供启动和既有每30次呈现轮询使用，延续已验证Honor735范围。缺失、EOF及无效值返回0。可在技能确认前设5，结束后设0，既有每输入8次上限与原CPU同步路径继续用于诊断。此改动用于选择采样阶段，未实现异步GPU输入消费优化。

生产提取读文件、关闭与状态切换代码，覆盖其它设备不轮询、0..5、重复请求、缺失、EOF及非法值，连同已有组件12项ASan/UBSan/真实NDK检查通过。完整构建、目标技能双输入比较待执行。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

50源7a6607c clean，114宿主/Android0，80项输入SHA一致。APK1805321eca4426d33cf86b29621954f1cb079e0a238fad0c977ab27cbaa85915，3724503B。install50回读APK、私有461九文件SHA及正式源metadata通过。launch50复用461，none新driver、adpf0/vcache0/texrecord1、overlay0，接下来到技能确认时切5。49与此前全部APK及符号保留。

## 50实际技能与旧热点边界

50 PID2107消失突袭首次普通8秒171帧21.3589FPS/max159.42ms，45超50、6超100，含可能胜利转场。mode5已确认为20880，实际动作20986..21156，目标3b部分合并未触发，双输入比较NOT_OBSERVED。随后mode1随第二次技能启用27510，仍无overlay queued/result，不能据此证明字节相等或将旧LIMIT热点归给火焰技能。两次均已切回0。第三次暖技能Perfetto5fa8690c已保全，仍描述符/复制热点。probe启动时不存在，enabled0，临时写3不会启用，此次仅CPU trace有效。已创建0并重启同50、复用461，PID8384，准备在真正enabled1时复用marker3采同步输入/生产槽。未改shader或启用异步GPU合并。

完整原始证据fire50-overlay5、fire50-overlay1、profile-skill50及launch50-probe落在.local/honor-submit44-20261116/。蓝牙主观、整包显示及独立QA仍NOT_RUN，真实稳定30未达成。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 51间接计算调度候选

51候选扩展indirect_dispatch_inputs_resident。仅既有Honor范围及indtexel未禁用时接受buffer texture，要求CPU干净或最新完整当前槽GPU范围、4字节对齐，拒绝未知/空/越界/非buffer纹理、部分或外槽GPU写、报告覆盖及有副作用的诊断。精确可写texel别名沿原共享storage，拒绝其它可写交叠和uniform写别名，SSBO包含关系按现有descriptor复用核对。原UBO/SSBO路径与所有shader/typed/integer语义继续保留。生产提取ASan/UBSan覆盖分支通过，整包及真实性能/输出待测。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 51构建和安装

51源20c6c98 clean，host/summary.json为115项全部通过，Android退出0，81项源输入SHA前后相等。APK23298026ae6abfa7e9d991fd5f3b619a8282915292c52b90ed2d3b25d53a966e，3724503B。安装回读APK及461全部9文件SHA、正式源metadata通过。PID11169复用461，以indtexel原准入路径启动，adpf0/vcache0/texrecord1，probe0及overlay0。接下来同进程原/新/原/新实际消失突袭对照，冷暖分别记录。所有APK保留，Main只读。

预备51构建计划从50复制时source_commit字段暂为旧值，独立source51.json记录真实20c6c98，构建结束已同步修正plan/artifact.json，宿主summary的source_head同为20c6c98。没有修改在编译的输入。主机81项快照包含新增测试。

## 51首轮旧新与剩余回退

51同PID11169原A1为两Lv2变异鼠，普通8秒148帧18.4813FPS/max224.19ms/54超50/15超100，最大sync_wb10。新B1为Lv1钢蝠及妖藤，165帧20.6644FPS/max107.40/57超50/5超100，最大sync_wb5。敌种和冷暖不同，未归因为稳定帧率收益。B2新路两Lv2变异鼠采用probe3与录屏，单列显示/回退诊断，仍有924b0b5d1854f387 CPU调度读回，d56a对应原回退记录此窗未见，不能只按缺失日志证明全部调用通过。现在准备扩展已有probe3的一次输入清单，含32条上界的纹理地址、范围、slot、类型及当前驻留，保留同步路径，生产提取探针及准入联合ASan/UBSan两种数组规模通过。

probe3及录屏窗口排除普通FPS，显示尚待逐帧查看。真实稳30、冷暖全场景、A2DP及独立QA仍未达成。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 51暖原对照及52剩余回退输入

51暖原A2两Lv1钢蝠，44235..44396/162帧20.2719FPS/max135.56ms/54超50/8超100，全部54条stall的shader_ms为0，最大sync_wb恢复10。B1新20.6644与A2原20.2719相差有限且敌种不同，尚无稳定FPS收益。B2为probe3+屏录诊断19.30，排除普通FPS；32725起剩余计算回退皆924b，另有op26间接draw回退，均沿原CPU同步。录屏86acf83c/13556708B已保全，抽看04/06/08有目标确认、红色火环、角色和伤害数字，不能替代完整显示验收。设备本轮自建mp4已匹配host SHA后删除。PSS从B1前4697315KiB到A2后5009675KiB，时间/工作量及缓存不同，尚未判定泄漏。

52源3a3cc65 clean，115宿主/Android0，81源输入SHA一致，APK a2bbf957fa04a740b53d01ed274100ba7d128fbbc8f4679aec330a04465244fd，3724503B。安装回读及私有461九文件SHA/正式源metadata通过，PID16304，none新准入、adpf0/vcache0/probe0/overlay0，拟本次火焰技能前开启probe3以列出924b纹理范围。新52仅补有界诊断和版本。

所有APK、符号和私有存档原件保留。真正稳定30及完整冷暖、音频、显示和独立QA仍未达成。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 52实际短输入与53最小修正

52 PID16304 frame8039的924b回退输入已经实测。参数12/生产60字节同slot7；storage B11/B23为相同32字节、B17/B47相同52字节，readonly B29为12、B41为8192，均为完整当前槽GPU范围。唯一短输入B35为CPU干净2字节，无待GPU写覆盖。53仅撤去CPU干净texel的全局4字节地址/长度限制，GPU输入仍要求完整当前槽、源offset4对齐及copy长度4对齐，报告/别名/未知回退保留。生产indirect_cpu_clean、原准入及完整probe函数联合提取，新增CPU干净2字节、GPU两字节拒绝、未对齐guest地址与真实device-buffer offset分别验证，常规和32行上界两进程ASan/UBSan通过。暂未得到53实机性能。

shader、uniform范围、typed/integer、游戏时间与音频配置继续原样。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 53构建及装机

53源39c5bf9 clean，115宿主/Android0，81项源输入SHA一致。APK59a37bdfe2c2ab721a4b8a752ef989d9c9d188b9c8b3dfe980259aebb4cdbf0b，3724503B。安装回读及461九文件SHA、正式源metadata通过。PID19273复用461，以indtexel原输入准入启动，adpf0/vcache0/texrecord1/probe0/overlay0。下一次到技能确认前再选择新路，不把冷暖差归帧率收益。需要特别准确区分两个已有范围：partial_overlay_available按tablet+vendor5143+Adreno735，honor_inline_enabled按tablet+Adreno并服从gfx-honor-inline，53间接texel候选复用的是后者。新准入尚仅在本台735实测，不能声称其它Adreno/Mali已通过。

当前仅按候选验证，FPS/完整画面/冷暖全场景/蓝牙音频及独立QA未通过。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 53首次新路

53 PID19273的首个普通新路技能，两Lv1钢蝠，9296..9431/136帧17.0041FPS/max266.51ms/48超50/21超100，全部8秒原样保留。计算调度CPU fallback已为0，最大sync_wb1，最慢帧shader131.7ms、submitCPU254.61/GPU57.99。虽移除了计算参数读回，当前冷窗没有FPS改善证据，接下来同进程暖原/暖新复核，不能把等待次数改善称为稳30。仍可能有op26间接draw读回，原因待进一步按调用链验证，不能仅从indirect_template_batch不支持GPU参数就断言是新引入。当前已经切indtexel原路径，角色探索中，等待下一战斗。自建旧PID2107/8384/11169/16304帧CSV逐个匹配host摘要后移除，共7311200B；首轮run-as外部目录Permission denied无删除，shell同路径成功。备份与恢复命令retired-frame-csv/plan.json。

真正稳定30、完整显示、冷暖全场景、A2DP与独立QA均未完成。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 53首对暖原新

53同PID暖A1原与B1新均两Lv2变异鼠、输入前相同HP6368/372、仅MP不同，热状态均1，两个完整8秒窗口保留。A1 22906..23084/179帧22.3038FPS/max116.96ms/46超50/4超100，最大sync_wb10，stall shader_ms全0；B1 34490..34672/183帧22.7719FPS/max119.52ms/40超50/6超100，最大sync_wb1，stall shader_ms全0。已确认计算参数读回减少，FPS差约0.47且尾部仍长，不能当稳定收益或稳30。继续第二对原/新窗口。当前恢复indtexel原路径，准备退出结算。

再次确认既有DRS已接通guest_process.c的CVar控制，当前日志含mode2/min60/max60或80/budget33.333，1080P输出与内部尺寸分别验。不能由DeviceProfile的档位选择或grep未命中dynamic字段推断DRS缺失。新的普通窗尚未逐帧核实际3D尺寸，继续保留该比较限制。

## 53第二对暖原新

53第二对普通暖窗同为两Lv1钢蝠，A2原71067..71246/180帧22.4465FPS/max141.88ms/38超50/3超100；B2新93134..93322/189帧23.5755FPS/max105.93ms/29超50/3超100。A2有1条stall含shader，原始行继续保留；B2 stall shader全0。最大sync_wb分别10与1。两组同敌人A1/B1及A2/B2均有小幅正向差，分别约0.47与1.13FPS，仍非稳30且未逐帧记录内部DRS尺寸，不称充分收益验收。53后PSS5114631/RSS5227040KiB，需结合缓存时长继续看，未判定泄漏。当前MP31，需要通过游戏UI重新读档恢复测试输入后，再作暖新CPU/GPU诊断。

只读重新解析50匹配符号的record_open_owned 17个inclusive样本，8个返回点928在record_write_locked调用后、6个939在unlock、2个919在buffer_find、1个917在lock。没有把17个全部归为线性查表，暂不实施MRU猜测。write-tracking旧实现未接宿主hooks且版本按last_use+2选用，当前16槽，不能直接打开或扩到vertex；需实际生命周期和VM改映射审查后再考虑。

蓝牙主观、完整显示、其余场景稳30和独立QA继续待验。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

## 53暖技能CPU复核及后续候选

53暖技能6秒Perfetto已保全9f6897e3，符号BuildID d7878b63与trace映射一致。RHI运行5350.21/5930.14ms，descriptors_build139、texel_acquire43、hash_memory20 inclusive样本，存在上层INVALID_ELF不作独占比例。hash来源15条驻留texel、5条图像；源代码已有4lane整段hash。CSV已到120000上限，不能计算此次普通FPS。trace BOOTTIME与输入MONOTONIC相差318.504986771秒，以clock_snapshot对齐。领取有界写区间校验候选，仅在同一原本可复用提交内跳过可证无重叠的重复hash，保留整段回退；实际收益待A/B。

匹配PC解析53-descriptor-sites.txt。28个描述符调用点位于4807 texel_acquire，20个位于5066驱动更新。15个hash样本来自驻留texel，5个来自图像。只是inclusive取样，不能相加作为CPU百分比。小写入造成全局epoch增长和大表粗失效为源码事实，实际重复量和收益尚待新计数与同机对照。无改着色器及精度。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

### 54范围校验候选

54有界texel写区间校验实现完成。仅既有Honor/Adreno范围，gfx-live-ab=texelrange保留原路。同提交且完整64条以内无重叠才复用，提交reset、漏历史、重叠、异常范围全量hash。原pending writeback/report/event检查保持先执行。生产函数提取ASan/UBSan测试覆盖变化尾字节、在用旧副本、slot退休刷新、reset、64/65写、环首重叠、原路/非Honor、超过64居民、跨槽/格式和无效范围，与既有组件共5项通过。实际收益及完整构建尚待验证。
没有跨提交假定CPU内容不变，也没有开启写保护跟踪。新增range_hits/hash_mb接在原texel_resident计数行，未增加诊断开关。

### 54构建安装检查点

54源033a4ce clean，116host与Android0，82份源码输入前后SHA一致。APK bb758a851297398d3c8961f7ed87d703465263c421ad9d77d1352f32f8e76fb4/3728599B，install54回读APK及全部9份私有存档SHA、正式源metadata通过。新PID8516复用461；texrecord1/vcache0/adpf0，gfx-live-ab=texelrange先走原粗校验，indtexel新路保持。待标题Continue、神殿移动及同技能冷暖对照。音频与全显示回归未通过。

### 54神殿移动及首次技能

54 PID8516已进入神殿内部桥面。A1原路持续移动5.98秒27.253FPS/max71.85ms，B1新路中途遇敌25.839只作混合诊断并排除移动对比。击败后B2新路同桥面持续移动5.945秒28.930FPS/max47.52ms/0超50，随后A2原路5.882秒28.392FPS/max86.69ms/7超50，均无present错误且前后截图仍探索。短窗收益证据仍不足，不称稳30。首次新路消失突袭两Lv1钢蝠8秒17.496FPS/max246.05ms/19超100，存在多次shader冷加载，原始长帧全保留。range_hits约0.8..1.7/帧，hash约2.6..4.4MiB/帧，实际收益继续暖技能对照。
53 hash调用来源已用53-hash-sites.csv逐项求和校正，15个来自texel_acquire、5个来自get_image_slow，总数20。此前手工17/3计数撤回，范围校验的候选性质保持。

### 54暖技能与GPU检查点

54同PID8516暖技能普通窗已补。A1原范围校验为Lv1钢蝠+妖藤，41756..41901/146帧18.2178FPS/max121.76ms/61超50/14超100。B1新范围校验为三Lv1钢蝠，57533..57691/159帧19.9415FPS/max107.32ms/57超50/4超100。两窗stall shader全0，无present错误。敌群不同，不作因果加速比。命中仅约1次/帧，尚无充分范围校验收益证据，已在设备LIVE-AB=texelrange恢复原校验；代码54仍为未验收候选。GPU诊断8帧19263..19270所有query完整且无跳槽，完整8帧的1384x780主7附件通道按帧合计3.129..11.935ms，其中19266两段合计10.434ms；CS073ce354完整8帧为2.405..6.293ms。该诊断强制CPU间接参数回退，不作为普通FPS或新间接路的预算证明。当前结果屏，root持有输入，私有461复用，继续30FPS目标。

两次测试结束后thermalservice状态均0，属于结束时快照，不能证明测试全程无调频。54首次冷火焰shader日志有reuse0和reuse1混合，缓存retained1437、denied_budget356、peak67108465B已触及64MiB上限。现有XbgPackedShaderWords已用zlib Z_BEST_SPEED无损压缩，take_rewritten逐字输入验证并取走删除。不能把所有reuse0归给预算，也不要重复实现已有压缩或直接放大内存预算。须先用已有模式2预算身份日志核对未覆盖与预算拒绝各自的具体key。

下一项先审查正常CPU读回是否可将重复SHADER_WRITE到HOST_READ屏障合并到提交边界。保留所有GPU依赖、semaphore chain、slot fence和非coherent失效，逐一审计early mapped read再实现。此为候选推断，尚未修改同步代码。Khronos Guide固定a195f15615721921629b894ccbd3c788558305dd，本文SPDX CC-BY-4.0，源文件实取53224B/SHA a035d27f30ce62dc5382402f986e0e36a349fba97d53e9ebdd2a2d03be8d39e4，本地references/有取证索引。官方示例包含shader到host内存依赖、提交、fence等待和mapped invalidate。[固定同步示例](https://github.com/KhronosGroup/Vulkan-Guide/blob/a195f15615721921629b894ccbd3c788558305dd/chapters/synchronization_examples.adoc)

当前本地分支列表无main，git branch --merged main实际返回malformed object name main。尚未证实Main吸收，不删除当前或其它worktree分支。没有推送。所有APK历史保留。稳定30、完整场景运动显示、A2DP及独立QA仍未完成。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

GPU耗时范围已按gpu-pass.json完整8帧校正，前版转录只覆盖排序靠前记录，其范围撤回。19266主7附件通道分为2.128与8.306ms两段，两段均计入。所有8帧及效果收尾完整保留，未按快慢删帧。

### 55静态格式判断候选接管

只读审计发现cpu_access why3/8在descriptor准备期间直接invalidate并读pending mapped buffer，未在该分支等待fence。一般resolve_slot_writebacks/resolve_pending/capture路径有退休或gpu_sync，但此例外尚未解决，不能盲合并shader到host屏障。改做不可变typed-storage元数据短路候选，保留所有运行时格式检查。54范围候选尚无充分收益，领取自有源码撤回和新的生产谓词回归。
现有Shader.module_words还为有vertex inputs的stage0保留，typed_storage_variant因此会为没有可变storage格式的顶点模块每次锁全局诊断set并扫描bindings。53真实采样有TypedStorageSelection及其set分配调用点。只缓存模块/绑定自身的静态资格，不能缓存实际绑定VkFormat。

### 55静态资格短路实现

55撤回54范围journal及其专属测试，54APK和原始证据保留。新候选仅在原Honor/Adreno范围缓存Shader不可变的typed-storage资格，首次仍执行原诊断，eligible写入绑定每次继续按当前VkFormat选择原variant。gfx-live-ab=typedstatic回原路。新增生产字段/谓词/函数前缀提取ASan/UBSan覆盖320组合、运行中格式变化、后置加载、256项混合表、独立variant事实及非Honor/旧AB，与既有组件共5项通过。Shader字节、GPU同步、slot、音频均未改变。待116项全集/Android/真实性能对照。
CPU读回屏障候选继续只读审计，why3/8分支本身不等待的源码事实已记下，调用链覆盖和实际可达性仍待验证。

### 55安装检查点

55源563b403 clean，116host/Android0，82份源码前后SHA一致，APK912e88b654cc93f8ba36849c8d5eb5bce807f46f38ea93b6c306f215b10db10c/3728599B。install55回读APK、全部9私有存档SHA与正式源metadata一致。新PID16144复用461，gfx-live-ab=typedstatic先原资格检查；其它既有默认路径保留。待标题Continue、内部移动及真实技能A/B。另发现program bridge缓存命中前每次复制6份0x880控制段且program_find线性查表，沿53匹配符号再定位后拟复用现有render-state索引hint方案，尚未改这些文件。

### 程序元数据热点精确核对与56预备范围

53匹配BuildID的53-program-sites.csv/txt已生成。program_find原生PC 3158552共7个样本落在nvn_program.c第90行顺序查表，bridge13个inclusive返回点在info_of调用后，另3个在bridge entry_find。完整info有memset及memcpy等原生点位，不把bridge与子调用计数相加。现有program状态为可realloc的nvn_object_table，拟复用render-state的索引hint并按in_use/完整地址核验。桥接缓存命中仍先复制6份0x880控制段，拟先读锁内serial再命中不可变快照。初始化和SetShaders在同锁内更新serial，Finalize清in_use。
55 APK源563b403保持不变并正在神殿读档实测；56准备在独立native文件，当前已将旧41的桥接写占用转移root。尚无56构建或性能结论。

### 55实际采样与56程序元数据候选

55 PID16144普通A1桥面移动15425..15598/173间隔29.1612FPS/max61.79ms。B1中途遇敌17.9336FPS/max713.74ms，仅作混合诊断。静态资格短路实际探索约698..705次/帧，战斗约1072..1373次/帧。首次新路消失突袭为两Lv2变异鼠，38636..38755/120帧14.9745FPS/max270.39ms，61超50、24超100，含shader最高134.8ms，尚未证明普通FPS收益。

55预热1934成功/3失败，与54相同。开始时系统控制中心覆盖，折叠后10:09:28才出现16144的guest_start，先前残留guest_boot不用于本次启动结论。crash buffer中的SNK旧进程也排除。55匹配符号已保存在build55/lib55.symbols.so，后续构建会覆盖.local/build，须继续使用这份匹配产物。

56源码2c4bbae已提交。程序对象加1024个数字索引提示，按完整地址、in_use和表边界核验，受原锁保护。桥接先读同锁内serial，命中不可变快照时跳过完整info复制，miss保持六阶段完整控制字节。现有gfx-live-ab=progcache驱动原路径，原子mode在LIVE-AB确认前更新。相关ASan/UBSan组件共7项通过，覆盖4096容量、realloc失败、冲突、释放/同址重建、表重建、4读1写、模式切换、10000次缓存命中只有首次full copy、查询中替换与Finalize、旧快照持有及无宿主钩子。56全集、Android和实机尚待验证。

设备仍运行55 APK源563b403，私有461复用，root持有输入。当前最后画面为施放消失突袭后的结果屏，所有注入已UP。全场景稳定30、运动显示与A2DP仍待验收，验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。
### 55暖火焰与56构建检查点

55同PID16144的B3移动窗口77520..77658实际中途遇敌，23.1761FPS/max452.87ms保留为混合诊断，撤销其作为纯移动候选的资格。前后截图已核对，结束在两Lv2变异鼠战斗菜单。随后正常选择雷因消失突袭，全体目标确认后实际A触发，暖态普通8秒92773..92931共159帧，19.8658FPS/max131.46ms/P99 130.07ms，49超50、10超100，present错误0。完整49条stall、输入单调时钟、前后截图和同次engine增量保存在fire55-B1-new-warm，包含施法及可能的胜利转场，不宣称达到30。

56源6f24913 clean构建完成，117项宿主与Android退出0，87份源码输入前后SHA一致。APK344d7c30bf917cd62c942c9212549dfb76f7a43d41e965c0b0a330e8d3d3885a，3728599B，历史包与符号均保留在build56。三个原生入口用普通nm核对为本地已链接符号，nm -D未列出符合隐藏符号策略。开始安装56，继续复用私有461并逐项校验9文件及正式源metadata。真实性能和全部音频/显示验收仍未完成。

### 56安装、移动及火焰对照

56已安装并回读APK摘要，全部9份私有存档逐个SHA及正式源metadata保持。PID22998复用461。启动明确program fast lookup=0、LIVE-AB=progcache present0。实际none切换在present4200确认native1，同进程后续0/1均紧邻对应LIVE-AB。原精度、同步、fence spin0与分辨率策略保持。

纯探索运动两对短窗为B1新7816..7984/168间隔28.1425FPS/max57.65ms，对A2旧8707..8877/170间隔28.2455FPS/max71.08ms。A3旧15033..15206/173间隔28.9244FPS/max55.72ms，对B3新16378..16553/175间隔28.8987FPS/max88.55ms。各窗约6秒，均有同PID非零方向与最终零轴、前后同探索截图，0present错误。两对没有确认的帧率收益。A1和B2结束在遇敌过渡，混合原始数据保留并排除纯移动对比。

首次新路技能为Lv1钢蝠+妖藤，5123..5269/147帧18.3756FPS/max275.89ms，57超50、12超100。暖态B1新路两Lv1钢蝠12197..12363/167帧20.7801FPS/max112.50ms，51超50、3超100。暖态A1旧路两Lv2变异鼠21155..21304/150帧18.7088FPS/max118.43ms，62超50、11超100。全为普通实际A后完整8秒窗，含技能及可能胜利转场，0present错误。敌群不同不能作为因果加速比，稳定30未达。暖技能仍复制约10..16MiB/帧顶点，约1.4..1.5MiB/帧索引，作为下一项具体调用栈和读取范围核对的输入。

复用现有vulkan_subgroup_probe源码在本机重新查询，ROD2-W69/SM8635/Adreno735，子组默认64、可控范围64..128，required compute32不支持。因此拒绝请求物理32这一候选，没有修改设备feature或shader重写。首次动态链接因缺libc++_shared失败，静态C++运行库重建后capability-only errors0，未创建逻辑设备。证据subgroup56，保留失败与恢复记录。

音频数字口径校正。本次NDK AAudio.h明确AAUDIO_PERFORMANCE_MODE_NONE=10、LOW_LATENCY=12，生产audio_bridge.c默认使用NONE，本次requested_performance=10/performance=10符合该默认。常设音频文档中“10即低延迟、0即NONE”的数字说明与实际NDK不符，此数字判断撤回。仍按枚举语义及常设完整A2DP流程验收，人耳持续播放与连接切换仍NOT_RUN。没有修改音频源码或缓冲。

继续同包暖技能调用栈定位，匹配build56/lib56.symbols.so。程序缓存目前仍为未证明帧率收益的候选。root持有输入，正式存档、Main与APK历史保持，未推送。

### 56匹配敌群复核与新调用栈

同PID22998补齐A2旧程序路的两Lv1钢蝠暖技能，32668..32835/168帧20.9509FPS/max116.87ms，56超50、5超100，shader stall全部0。同敌群B1新程序路为20.7801FPS/max112.50ms，仍未证明程序缓存的整体帧率收益。B1包含一次8ms shader加载，保留该长帧。实际A后8秒与原始数据保留，没有把不同敌群的A1/B1作加速比。

第五场两Lv2变异鼠用新程序路采6秒暖技能Perfetto，trace769ef05be757301038c24a6e593709fb816369af6fa2ac529a0747cd17f33b90，本地和远端摘要相同。符号ecadb4a9e3bc11e36bd125846b87120b69380715244c3b10b347d901186ce2fc，BuildID49fa7f647345cd40a7eac805ef5ada568f5b3845与trace映射一致。5.9335秒trace内RHI Running5021.05ms，descriptors_build147个inclusive样本，stream_buffer memcpy返回点23、descriptor template update返回点23、texel pending/acquire返回点20、snapshot stream返回点19、recording_slot reset池返回点14。不能将父子计数相加。上层INVALID_ELF383条继续保留，不作完整游戏CPU占比。旧程序info/find热点当前仅bridge4个inclusive点，整体帧率结论仍按上述普通窗口判断。

下一项先复用仓库既有poolreset异步命令池重置。已审查job先等本slot fence、retire先finish、复用前finish、上传drain及destroy_slots先清job的顺序。图像和shader语义不改。原日志配置async command pool reset=0，marker实际不存在。首个备份脚本把缺标记误当异常，停止于只读阶段，22998仍活，未重启。补显式存在性检查并保留原失败记录后，归档该PID完整日志/CSV与9文件私有存档，再以原56同APK重启为27957，复用461，正式源metadata留档。启动确认async command pool reset=1、LIVE-AB=poolreset present0，先测原同步reset臂。该配置仍是实验，待同进程poolreset/none普通运动与技能A/B后决定保留或恢复。证据restart56-pool-retry1。

### 56命令池实验与57描述符候选

PID27957同56包。A1原同步池运动5746..5886/140间隔23.2525FPS/max106.59ms。B1异步窗口遇敌，保留混合22.9287并排除纯运动比较。战后柱旁机位B2异步25004..25136/132间隔22.1560FPS/max99.58ms，随后A2同步26335..26471/136间隔22.7878FPS/max109.92ms，前后均探索，实际非零轴及最终零轴有记录。较早28..29FPS属于上方断桥机位，不能与此处直接归为版本加速或退化。异步每帧约9.8个job、约0.7ms finish等待，尚无稳定收益，当前LIVE-AB=poolreset走同步路径。

首次异步战斗的菜单脚本提前完成了施法，施放时钟与8秒采样缺失，整场仅作预热。已保留截图并改用较短点按、逐步确认招式及全体目标，菜单辅助脚本增加同PID输入日志。随后三Lv1钢蝠同步暖技能37628..37809/182帧，22.6671FPS/max125.29ms/P99 121.27ms，44超50、2超100、0present错误。异步暖技能完整对照仍待补。

随后平板前台切到com.hihonor.controlcenter/ControlHomeActivity，27957仍在后台。已发操作归属问题，继续宿主代码和构建工作。下一次设备操作先确认前台和输入归属。当前磁盘gfx-async-pool-reset=1、当前进程LIVE-AB=poolreset。取得操作权后恢复启动配置或继续同包实验时须一并核对这两项。

57候选减少新分配storage输出的描述符缓存成本。当前实际setcache多数窗口hits=0，而56原生点位5090保留缓存插入分配调用。只在两个实际readback_buffer分配位置建立fresh事实，该新arena偏移进入本次buffer descriptor或对应texel view的完整键。槽退休先清set_cache，再复用arena偏移。因此该键在当前槽内只能首次出现。既有Honor/Adreno配置沿原allocate_set、模板创建/失败回退、完整descriptor payload和退休路径，跳过此类键的构造、查找及插入。现有gfx-live-ab=setfresh回原缓存路径，fresh_skips接入现有PERF行并随窗口重置。

生产描述符最终处理段抽取的ASan/UBSan测试已通过，覆盖Honor/原路、fresh/普通、两次分配及普通缓存命中、buffer/image/view完整有效载荷、模板失败plain回退、空writes和槽退休偏移复用。恢复旧缓存插入的负控实际失败。分配事实测试核对两个真实分配点及别名路径。与原native程序和typed组件共7项通过。57全量构建与设备收益尚待验证，设备仍为56。

本轮调度线索复核。27957的RHIThread28095允许0..6，78个其它线程允许0..7，top-app/vip cpuset均0..7。48已记录同样的RHI范围并实测ADPF，属于既有事实。当前cached readback实际已1，Snapshot使用SnapArena批量分配。物理32子组、重复实现snapshot allocator、再次开启readback-cached均缺少新的适用前提。尚未修改CPU亲和性。Android官方调度资料推荐以ADPF向系统提供工作量和期限，当前此路径仍按48实测结论处理。两份在线资料本次可获取，未取得不可变源码revision，仅作指导性引用。[线程调度](https://developer.android.com/android-performance-analyzer/analyze/thread-sched)，[Performance Hint API](https://source.android.com/docs/core/perf/performance-hint-api)。

### 57录制代际保护修订

首轮57源be754b8 clean，118项宿主与Android0，88份输入SHA前后一致。APK49225d8e638f1e69ce45f0554426e430950a5c99d43ed01555a1b22839e1da8d/3728599B保存在build57，尚未安装。

源码复核发现描述符准备期间CPU同步可能提交并切换录制槽。因此撤回“仅凭fresh布尔量即可认定当前缓存键首次出现”的完整性论证。新分配位置同时记录slot_serial，准入增加当前槽仍recording、分配序号等于当前slot_serial两个条件。槽切换及准备后已结束录制时沿原缓存路径处理。新增生产最终处理段测试覆盖这两种边界，含返回普通缓存命中和完整payload，7项关联ASan/UBSan通过，旧缓存插入负控仍实际失败。57b将重跑完整构建，首轮57保留备查。

设备仍为56/PID27957，输入归属问题待用户回答。当前进行宿主构建，验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。全场景稳定30、57实机收益、运动显示及A2DP完整流程均待验证。

### 57b构建安装检查点

57b源066e8a7 clean，118项宿主与Android0，88份构建输入前后SHA一致。APK5e16b299077128d86af1d8ebdfbe6d6f32cc798a44832fef03bb05d2e17d63cc/3728599B，包内真实native ELF与匹配符号的BuildID均de0543e1d06cac765673d4b764696cc0d4050224。build57b保留APK、符号及构建日志，首轮57同时留存备查。

按用户本轮“已松开手柄，你操作”及既有直接安装授权继续推进。前台变化问题作为状态核对，已通过消息说明恢复《共鸣》测试。install57b回读APK并逐一校验9份私有文件SHA、正式源metadata，全部相同。同步池实验由本轮创建的1标记已逐字归档并记录恢复命令，恢复实验前缺失状态。新PID5498复用461，启动明确async command pool reset=0、LIVE-AB=setfresh present0，先运行原描述符缓存路径。shader/显示/音频回归与真实帧率收益继续验证。


### 57b普通移动与暖技能复测

同APK5e16b299、源066e8a7、PID5498。B2新路桥中段23094..23235/141间隔23.9398FPS/max71.42ms，随后A2原路23662..23819/157间隔26.9689FPS/max64.09ms。两窗前后探索、方向输入与最终归零均可核对。B4新26798..26953为25.9974FPS/max61.44ms，同段状态仍有波动，尚未确认移动收益。A3和B5结束进入遇敌流程，原始数据单列。seek-C1前后为战果成长页，29.8263只属该页面，明确排除探索移动。seek-C4被系统暗光提醒截获，无game_input，断言停止，保留原始失败和截图。

首次B0新招式15519..15667/149帧18.5847FPS/max189.68ms，51超50、10超100。暖A1原路三Lv1钢蝠25465..25625/161帧20.0625FPS/max128.29ms，52超50、10超100。暖B1新路两Lv1钢蝠29223..29403/181帧22.5438FPS/max113.90ms，46超50、2超100。暖A2原路两Lv1钢蝠37059..37211/153帧19.0797FPS/max122.03ms，61超50、12超100。完整实际确认键后8秒，均0present错误。同敌群本次新路数值更高，仍须反向重复，不能从一次结果宣称稳定收益。B1动作内46条长帧全部shader_ms与pipeline_create_ms为0，长帧子集submit_cpu中位43.79ms、GPU中位22.67ms，子集不代表全动作平均。

准备核对独立uniform上传arena候选。真实暖态desc memo存在每帧约70..145次dynbuf不匹配，当前4MiB混合stream同时容纳大块顶点/索引与小UBO，动态UBO虽偏移可变，VkBuffer变化仍使缓存失效。Khronos Vulkan-Samples固定b4fb432b80234e54cf7be7411f20b65494a5b0f5、Apache-2.0，descriptor_management样例建议少量每帧buffer配动态偏移。README10804B/SHA e61bef62d82c001e55d34cfbb62c625e3307ee945a33f21b08f2cc5edbfeabc5已实际获取。此为候选适用依据，性能尚待实现和同包对照。现有stream与readback本机均实际type6/flags0xf，因此再开HOST_CACHED没有新的设备前提。

58若实施，必须沿原slot fence退休、snapshot内容与范围、非coherent flush、buffer引用失效、trim和shutdown处理。额外UBO池计入96MiB提交阈值与内存统计，限制闲置保留。新增tests/device/test_uniform_arena.py由root领取。用户松手授权持续有效，root输入，正式存档与APK历史保留。音频完整A2DP及全场景稳定30仍待验证。

### 58独立常量池实现检查点

58候选由root在既有write scope内实现。每slot增加独立uniform arena，沿原stream_buffer的读取保护、完整复制、尾部清零与flush，guest uniform、driver block和UI偏移副本三个入口接入。Honor准入与既有gfx-live-ab=uboarena原混合池对照，普通顶点、索引、storage及GPU resident读取路径保持原有入口。新池在原slot fence完成后reset，下一次recording前trim，闲置只留一个4MiB块。memo_forget_buffer随原Arena::destroy执行。提交阈值、峰值统计、内存画像及shutdown均计入新池。

生产上传函数提取契约验证2个slot隔离、顶点增长不改变常量buffer、完整复制/清零/flush、旧路及非Honor路、越界拒绝。恢复混合分配的真实负控失败。退休次序、提交预算及三个入口静态核对通过。与现有Arena及57描述符契约合计9项ASan/UBSan通过。完整宿主和Android构建、真机生命周期及性能均待下一阶段验证。

57设备仍PID5498、gfx-live-ab=setfresh。相同两钢蝠的B1新22.54与A2原19.08保留为单次对照，58两臂均保留57新路以隔离常量池变量。当前设备战果页，所有触控已UP。正式存档、Main和APK历史保持，音频完整流程仍NOT_RUN。

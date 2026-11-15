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

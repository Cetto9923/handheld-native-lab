# P-CX-50 只读同步归因与单一候选

本轮只读生产源码与已冻结的9条定时摘录，不重读512MB日志，不碰设备/用户游戏，不构建/改同步策略。

## 协作事实
xbg_game.cpp登记owner=/root/command_lifetime，task=T02-RG16-CANONICAL-PROXY，write_state=ACTIVE；并非登记为MiniMax bd1392e4/P-MM-49。没有证据可将它视作用户已杀会话，未借P-CX-50授权转移该owner。指纹前后稳定：4c8b15648bc9d7453b989a6672669a59ab68faf0ec04a7eaba1883776cb4b398；登记本身不证明活跃进程，稳定也不证明已退出。只读审计不受此阻挡，生产文件保持不动。

## 197.6次提交/等待如何拆开（引擎内部计数，非游戏FPS）
索引final-existing-timing-excerpt.txt最后60次引擎呈现窗口（原日志3871724）：submits=197.60、waits=197.60每呈现；同段submit_by gpu_sync187.80+other8.80+present1.00，解释了总提交归属，不是新测试。

源码perf打印943..950验证：sync_wb/sync_q是该60窗口原始总量，submits/waits已除以frames。因此7080写回resolve/60=118.00，4215 query-only resolve/60=70.25，sync_rb=0。这188.25次resolve与gpu_sync路径占主要提交相符，但调用次数和真正产生Vk提交/等待不保证一一对应。机器可复核数据 .local/preflight-dependency/existing-window-attribution.json。

同窗口submit_cpu367.57ms/引擎呈现、GPU累计162.53ms/引擎呈现、gpu_sync阻塞259.33ms（188.30调用）以及legacy wait约35.69ms；分项timed2的fence250.56ms是另一个抽样作用域。CPU提交包含等待，GPU工作可能重叠，不能相加为总耗时或宣称各自独占占比。最贵已测等待在gpu_sync，真实GPU工作成本也显著，单凭它不能判为驱动空转。

## 依赖链（事实）
1. draw3101/dispatch3328/copy_texture_buffer3465等入口preflight_cpu_reads1605，无条件检查read_preflight开关；本次日志确认开关1。
2. resolve_pending1582在没有待写回/报告或重入时返回；否则gpu_sync1584→submit_slots(true)1009，真实Vk提交/槽fence完成后才处理Buffer.invalidate、memcpy、报告、ready event。这不是提交一个空buffer替代工作。
3. writable storage descriptor由SPIR-V storage_writes2742决定，重叠别名范围合并2747..2748；真正written范围才进入out.written2750..2753。draw/dispatch后defer_writeback2421保留Buffer与slot，让下一次host可见性边界可以完成结果。故“所有storage buffer无条件写回”这个候选也不成立。
4. cpu_access1606仅在访问与pending范围重叠时resolve；CPU写标记invalidate_cpu_range/写入路径1530用why4。本窗口why_access60且why4=60，即约每引擎呈现1次可见重叠CPU写，不是197次都来自已证实的CPU重叠访问。其余保守preflight缺少按入口细分的动态数据，不能推测具体draw/upload各占多少。
5. ReportCounter3988..4003记录真实Vk timestamp/occlusion相关查询，PendingReport带addr/type/epoch/slot/query pool+index，计数器阶段TOP/BOTTOM由type决定，追加至pending_reports。结果尚未知时不能假写CPU时间或提前signal。
6. copy_texture_buffer3465也先preflight，随后3481访问目标纹理范围；to_buffer路径的显式gpu_sync readback有专用计数。本窗口sync_rb0，不能把197次都叫“每次纹理上传强制读回”；待writeback GPU→CPU结果另记sync_wb，不等于sync_rb。

## 单一候选（条件未证实，故未改）
仅评估query-only preflight：pending_writebacks为空、待报告结果尚不可见，而后续CPU读取集合与所有报告目的地址不相交，且当前不跨guest fence/event完成、条件渲染计数读或其它host-visible边界时，可研究将报告解析延后到真实依赖边界。候选针对约70.25次query-only resolve/引擎呈现，不碰118次writeback，不跳上传/barrier，不取消查询或真实fence。

这仍不是安全可提交diff：当前preflight发生在取得descriptor/image/slot引用之前，cpu_access可触发提交与槽退役；有些快照读why1/2对report故意例外，CPU实际读取集合未完全审计。简单从resolve_pending排除pending_reports/关闭预检，会破坏可见性或在持有易失引用时触发退役，离线反向测试也已证明不能删wait。外层/内层重复preflight已被空pending守卫消除，不是新可删等待。

## 最小下一可验证观测点
不新开性能benchmark，先由生产文件owner/总控处理精确路径写入登记；可加入只计数审计：preflight调用点、pending_writebacks/reports数量、报告所属slot、当前slot/pending fence状态、下一CPU读取地址集合与报告16字节范围的相交/未知标记（未知保留等待），分别计empty/reentrant/required/可审计候选。不能只统计总calls后宣称提速。离线契约需新增：disjoint/overlap/query别名、TOP/BOTTOM顺序、occlusion epoch、CPU读之前写回、事件/fence发布顺序、同slot池释放与租约、在持有引用前解析。全部覆盖前保留当前同步。

现无已证冗余GPUwait可删除；最贵当前工作为待writeback/query解析引发的gpu_sync串行等待。只读归因已完成，生产修复SKIPPED。新的运行验证会替换当前用户进程，必须交父协调后才做。本轮不改进程、不承诺后台监测。历史run59 E/INCONCLUSIVE、同APK59窗口未复现而非修复、严重卡顿/显示异常/角色自由控制未验保持。

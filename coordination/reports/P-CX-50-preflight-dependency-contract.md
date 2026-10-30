# P-CX-50 preflight资源依赖契约（离线阶段）

范围：ONLY codex-black-screen @0444c81+WIP。未碰运行进程/设备、未build APK、未改生产引擎/同步开关、无push/FPS/readback/RG16专项。

协作检查：xbg_game.cpp仍登记T02-RG16-CANONICAL-PROXY ACTIVE，旧checkpoint滞后于run59；该登记不证明写者现在活跃，但本轮没有接管其生产文件。开始/结束源码SHA均4c8b15648bc9d7453b989a6672669a59ab68faf0ec04a7eaba1883776cb4b398，无检测到并发改动。保留原WIP。

新增可复现交付：tests/native/preflight_dependency_contract.cpp，scripts/runtime/check_preflight_dependencies.py。运行python3 scripts/runtime/check_preflight_dependencies.py，直接从当前xbg_game.cpp提取resolve_pending/preflight_cpu_reads/cpu_access三个函数（不复制实现后自测），包含真实xbg_writeback_policy.h半开区间规则，用独立mock buffer/fence/query/事件夹具编译。源文件hash/fixturehash/编译器/退出码在.local/preflight-dependency/manifest.json。

依赖账本：
- pending_writebacks持有CPU目的地址/长度、源mapped Buffer及slot归属；CPU读取/覆盖重叠范围前，必须确保所属GPU工作完成，再invalidate映射、memcpy、invalidate快照。GPU完成不能由CPU计数伪造。
- pending_reports持有counter地址及GPU query/epoch顺序；写回依赖真实timestamp/occlusion结果与对应slot完成，必须在guest sync完成发布前可见。
- resolve_pending设置重入守卫后gpu_sync→submit_slots(true)，随后按列表写回/报告，再发布ready events；槽/资源/查询生存期不能因减少等待提前结束。
- cpu_access校验范围、半开重叠才触发resolve；边界相邻不相交不应wait。报告读why1/2有现有“一帧快照”例外，本契约只描述既有行为，不认可它适用于任意CPU读。
- 全局preflight无待写回/报告时直接返回；已清空pending的重复调用不提交、不等待；重入gpu_sync中的preflight也返回。

结果：baseline exit0，Apple clang21/C++17 -O2 -Wall -Wextra -Werror。覆盖空/重复、单query、混合writeback+query、重入、边界相邻/单字节重叠、报告16字节重叠、溢出/零长度、错误开关与快照例外的描述性测试。Counterfactual只在生成的隔离函数中删除gpu_sync，exit-6，write_report的completed断言失败。原生产源码未改。该断言证明测试能抓到“跳过等待就写结果”的错误；完成状态是mock，不代表GPU实际已运行，也不建立真实VkFence与所有权的完整证明。

已排除的一刀：外层execute_draw_record与内层draw各调一次preflight，并不等于两次GPUwait；第一次清空pending，第二次现有守卫已避免wait。不能靠删重复函数调用宣称消除197次submit/wait。独立队列工作尚未完成而pending存在时也不能整段跳过resolve_pending。

尚未证实：现有约197次提交/等待中有多少由可安全延后/已完成依赖产生。已有GPU累计耗时、CPU提交及fence统计互相重叠，不等于独立CPU/GPU时间；本轮没有新增时间测量、不能报提速。当前路径的已知重复调用已经幂等，未找到有真实调用证据且安全可删除的GPU等待，生产修复SKIPPED，未冒充优化成功。

下一刀最小方案（先交父协调）：核对并释放/转移xbg_game.cpp当前写入占用；只增加分调用点/原因的preflight审计（empty/reentrant、report/writeback、所属slot是否已完成、CPU访问是否重叠），保留同步原样，用独立fixture覆盖query顺序/slot租约/事件发布/CPU可见性，然后再评估某个已完成且无未解析结果分支。不要直接关gfx-read-preflight或从别游戏借why=a0证据。若需要新APK验证，先由父协调当前用户游戏进程的停止/替换影响；本轮未强停、未替换。

P-CX-50现状不变：同APK59 30分钟未复现历史TIMEOUT（E/INCONCLUSIVE，不等于修复）；正式开场已入，严重卡顿/显示异常、角色自由控制未验。需要的是局部同步依赖审计，不必扩大到FPS目标/readback/架构重写。

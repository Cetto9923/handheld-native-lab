# 窗口生命周期与纹理查找优化组合独立审查

2026-11-18，执行者 /root/mali_crash_fix。

## 输入和工作区

审查基于总控冻结的 `lifecycle-existing.patch`、两个指定未跟踪文件、暖布局校验提交 `03c1216`、性能代理 `indexed-record-cache.patch`。隔离工作区为 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/worktrees/codex-mali-20261118`，分支 `task/codex-parallel-mali-20261118`。审查、反例和通知修复均在该工作区执行。

## 已确认的问题

### 生命周期候选改变了第二个合法窗口的初始化结果

`XbgSurfaceLifecycle::reopen` 通过单个 `state_.retired` 限制全局只有一个 guest window。候选 `nvn_window.c` 418–436 行将所有初始化交给该对象，因此首窗口存在时第二窗口返回 0。

基线 `273c240` 的 `tests/native/window_acquire_contract.c` 69–75 行在首窗口仍存活时，验证第二个无平台 Surface 的窗口初始化成功并可获取纹理。候选测试 83–97 行新增先拒绝第二窗口、显式结束第一窗口、再创建第二窗口的顺序。该修改改变了原验收输入和期望。

独立编译并执行旧生产 `nvn_window.c` 与冻结候选，使用完全相同的测试后端、相同的两个软件窗口和无 Surface 条件。旧代码输出 `first=1 second=1`，候选输出 `first=1 second=0`。原始日志为 `.local/lifecycle-review-20261118/two-windows-{old,candidate}.log`，索引为 `two-windows.json`。两个二进制均 exit0，ASan/UBSan 启用。

现有 Vulkan backend 只保留一套 surface、swapchain 和 present-path 数据，旧多窗口物理资源管理也有边界。更完整的方案需要分别维护每个 guest window 的生命周期，以及共享或独立的物理资源归属，保留第二窗口成功的行为。初始化、Finalize、同 guest 指针复用、平台 Surface 自动重建和旧句柄退休都需贯通该映射。直接取消单 owner 检查会使现有 cleanup token 继续承担含混的资源归属，当前证据不足以把该改法作为完整修复。

总控已据此把完整生命周期候选移至隔离区。此审查支持继续保留原两窗口行为，并将资源归属方案留待单独设计和验证。

### 异步 completion 通知可以晚于栈上 CV 的销毁

冻结 `surface_serialized` 的 worker 在 `done_mutex` 下写入 `done=true`，释放 mutex 后调用栈上 `done_cv.notify_one()`。当 worker 已写入 done、调用线程才从 `gfx_enqueue` 返回时，调用线程可直接通过谓词并返回，随后 worker 对已结束生命周期的 CV 发起通知。

实际函数的定向测试在 notification 触碰底层 CV 前暂停 worker，强制 enqueue 在该点返回。旧函数在 CV 销毁后通知的断言处 SIGABRT，修复后 exit0。测试 seam 在检测到对象已销毁时只记录调度违规，避免故意调用已销毁的系统同步对象。该反例覆盖具体调度次序。[C++ 条件变量](https://eel.is/c++draft/thread.condition.condvar) [C++ 对象生命周期](https://eel.is/c++draft/basic.life)

最小修复把 `notify_one()` 放入同一个 `done_mutex` 临界区，确保调用线程能够观察 done 时通知已经完成。补丁为 `.local/lifecycle-review-20261118/notify-order.patch`，SHA256 `0c4493b8720268ee9066f5260ce319a983e9c886c7c27f0c046449598ee92178`。独立测试为 `tests/native/test_surface_serialized_notify.py`。

触发条件为异步 `gfx_enqueue`。当前 Android 初始化硬置 `gfx_async=false`，此发现对应隔离候选的异步协议覆盖。G925 原事故归因继续开放。

引用的 C++ draft 本轮成功读取，仓库 HEAD 为 `65890fab817ca1dfe7898f481c4fd088bbe2bbce`，可在固定 revision 对照 `source/threads.tex` 和 `source/basic.tex`。测试与修复独立实现。[固定规范源码](https://github.com/cplusplus/draft/tree/65890fab817ca1dfe7898f481c4fd088bbe2bbce/source) [许可证](https://github.com/cplusplus/draft/blob/65890fab817ca1dfe7898f481c4fd088bbe2bbce/LICENSE)

## 其余检查范围

- producer 先进入 admission，随后读取 window 快照、桥接纹理、提交 present 并记录 fence。初始化和结束也从 admission 进入，等待 renderer 时已释放 window-state mutex。
- 执行回调持有 engine mutex，worker present 和 backend 回调未再次获取 admission。JNI Surface 发布只进入 native-window holder 锁，未发现该路径反向等待 engine/admission 的锁环。
- failed init 保留未退休 token，claim 失败的 cleanup 跳过其他 owner。owner/generation 匹配后才解析当前物理句柄，已有 raw stale-handle 检查保留。实际 backend 提取测试中的 owner、代际、自动重建和 idle 失败保全用例均通过。
- 实际 `surface_serialized` 同步、FIFO、worker-inline 和原异常传回的旧提取测试通过。新增通知调度反例补足其栈上同步对象寿命覆盖。
- none backend 原本在 DeviceInitialize 返回失败，当前审查未发现其新增实际成功路径回归。现有受控 mock 已补入 ownership 回调，该测试适配本身不能证明全部历史 backend 契约。
- 平台 Surface generation 与 guest token 分开，Home/Surface 重建继续使用原 guest owner。总控已给出荣耀同 PID 实机证据。本审查的执行范围为宿主源函数和组件测试。
- 性能两文件补丁复用已有 `find_texture_record`。注册表节点到 shutdown 才清空，登记更新原节点，现有 engine mutex 保护该调用。独立复跑组合 `test_indexed_bbox_ready.py`，20,000 组状态和 200,000 个实际范围策略查询通过 ASan/UBSan。原始日志为 `performance-combined.log`。

## 范围说明与恢复点

暖布局提交 `03c1216` 只覆盖报告列出的 binding/type/stage 形状与 push range 校验。layout flags、descriptor aggregate limits、per-stage resource totals 等仍沿现有实现，其完整校验保留为后续工作。

本次审查未执行 Android APK 构建或设备输入。工作区仍保留总控生命周期基线与性能代理补丁的未提交改动，通知修复也只在隔离工作区。完整索引为 `.local/lifecycle-review-20261118/CHECKPOINT.json`。后续可独立复跑通知测试及传入 `xbg_game.before-notify.cpp` 的旧路径负对照，随后将通知补丁应用到被隔离的生命周期候选。

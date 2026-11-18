# 天玑崩溃复核与暖布局配方加固

2026-11-18，任务 CODEX-PARALLEL-MALI-20261118，执行者 /root/mali_crash_fix。

## 接手基线

- 工作区 `/Users/yuyan9923/GitHub/ff-resonance-honor-gtpro/.local/worktrees/codex-mali-20261118`，分支 `task/codex-parallel-mali-20261118`，起始 HEAD `273c240155f95db92e2f08050b61164aec401f29`。
- 已套用总控冻结的生命周期候选，输入为 `.local/codex-dots-takeover-20261118/lifecycle-existing.patch` 及两个指定未跟踪源码文件。既有候选继续保留在本工作区未提交区。
- 本次新增改动限定为 `xbg_game.cpp` 的 `warm::make_layout`、一个定向宿主测试和本报告。原始游戏、发布包、存档、设备状态均由总控保管。

## 原事故证据

直接复核 `~/.cetto-arsenal-inputs/crash73-new/guest_boot.txt` 和 `engine.txt`。原设备为 vivo V2405A、MT6991、Mali-G925-Immortalis MC12，实际版本 1.0.73。末段在 frame1174 载入 `073ce354a4f65e2f` compute 模块后记录 SIGSEGV，故障地址 0x8，vendor PC 相对偏移 0x1eda09c、LR 相对偏移 0x1eda024。当前证据没有给出该故障所处 Vulkan API 或故障线程身份。

已复用现有 compute create-info、目标六个 descriptor binding、缓存并发、设备销毁和内存回收审查。目标 metadata 的两个 UBO、两个 combined sampler 和两个 storage image，以及本机 ample descriptor limits，均未提供新的原事故成因证据。原事故归因继续开放。

## 本次确认的独立缺口

`warm::make_layout` 会把磁盘暖管线配方还原为 Vulkan descriptor/pipeline layout。原实现检查了字数、binding 数量、set 数量和 range 数量，结构完整的非法布局仍能抵达两个 Vulkan 创建调用。实际生产函数的驱动桩执行证明，重复 binding、错误 descriptor/stage 值及非法 push constant 范围可以被原路径接受。

新增校验在两个创建调用前执行，覆盖重复 binding、当前逻辑设备未启用的 extension descriptor/stage 形状、input attachment stage 范围、push constant 对齐与大小、同一 stage 重复 range，以及按当前设备 `maxPushConstantsSize` 检查范围。范围使用先比较 offset、再做减法的形式，避免加法溢出。拒绝的可选暖配方沿既有 warm build 失败路径处理，普通运行时管线构建继续使用其原入口。

合法 core descriptor 0 至 10、数组、稀疏 binding、零 count 保留项、`VK_SHADER_STAGE_ALL`、不同 stage 的重叠 push 字节范围和有效缓存复用均有执行覆盖。当前逻辑设备仅请求 core features、选定 Vulkan12 features、swapchain 及 mirror-clamp，新增检查对应此 enabled 范围。

Vulkan 规范要求 binding 唯一，input attachment 限定 fragment 或零 stage，push offset/size 按 4 字节对齐且服从设备上限，同一 stage 只能出现在一个 push range。参考规范页面均于本轮成功读取。Vulkan-Docs HEAD 为 `f7de774cace1509af0f10b8d859c973b5354ff62`，项目许可证入口为同 revision 的 LICENSE。这里只引用接口规则，无上游实现代码复制。[descriptor binding](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutBinding.html) [descriptor layout](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutCreateInfo.html) [push range](https://docs.vulkan.org/refpages/latest/refpages/source/VkPushConstantRange.html) [pipeline layout](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineLayoutCreateInfo.html) [固定 revision](https://github.com/KhronosGroup/Vulkan-Docs/tree/f7de774cace1509af0f10b8d859c973b5354ff62) [许可证](https://github.com/KhronosGroup/Vulkan-Docs/blob/f7de774cace1509af0f10b8d859c973b5354ff62/LICENSE.adoc)

## 实际验证

日志位于本工作区 `.local/mali-layout-20261118/`。

| 检查 | 实际结果 | 覆盖 |
| --- | --- | --- |
| `python3 tests/native/test_warm_layout_validation.py` | exit0，ASan/UBSan 通过 | 提取当前完整 Reader 和 make_layout，驱动桩记录创建调用；合法布局、缓存复用、坏布局拒绝 |
| 同测试传入 `xbg_game.baseline.cpp` | 旧函数执行 SIGABRT，测试进程 exit1 | 完成编译后在接受结果断言失败，旧路径负对照成立 |
| `python3 tests/native/test_warm_layout.py` | exit0 | 既有并发发布、获胜对象保全及失败竞争对象释放 |
| `python3 -m unittest tests.installer.test_pipeline_warm_inspector tests.installer.test_apk_pipeline_warm_seed` | 17 tests，exit0 | 现有配方发行、身份及原子 seed 路径 |
| 两份既有种子布局回放 | 1750 与 1898 条记录中的 641 个唯一 layout，exit0，ASan/UBSan 通过 | 当前完整 make_layout，maxPushConstantsSize=128，驱动桩创建 |
| Android API28 原生产编译参数语法检查 | exit0 | 当前完整 xbg_game.cpp；命令在 syntax-plan.json |
| `git diff --check` | exit0 | 空白与补丁格式 |

两份种子 SHA256 分别为 `595d5e1825e06ec1a3d6931e939c1c3a6d5d2649a95700d3cdd766bf50b84b8f` 和 `c7a1d89d498e013d9a8407268892f913193626fcb83e0e91fa5c416e702dd287`，完整路径、记录数量和结果在 `seed-layouts.json`。

首次负对照因驱动桩常量未使用触发编译警告，修正桩标注后重新执行，`negative-control-runtime.log` 保留了运行期断言失败。首次本地种子解析脚本存在 Python 复合赋值求值次序错误，修正后重新运行，两份输入以完整 framing 解析且全部唯一布局通过。历史失败日志予以保留。

## 覆盖边界与接续

本次交付为可选暖配方输入加固。G925 玩家报告中的原故障与坏布局之间尚无证据关联。完整 Android APK 构建、G925 原机、实际暖驱动对象和两台现有设备的游戏回归均为 NOT_RUN，设备未被本代理操作。

总控可将新增修复补丁合入已验证的生命周期候选，运行完整宿主和 Android 构建，再做同构建实际游戏回归。原事故继续需要能对齐故障线程和 API 的证据，现有 frame1174 相邻模块记录单独保留。

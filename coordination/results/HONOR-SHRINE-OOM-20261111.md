# HONOR-SHRINE-OOM-20261111 — 土之神殿过图与方块修复

## 当前结果

普通安装包已在 Honor ROD2-W69 / A42BBB5109000333 / Adreno 735 上实测：Continue 载入神殿内室、退出到入口、再次进入内室、左右移动，进程 PID27357 全程存活；入口原有大片矩形光照异常消失。该范围自测通过，不代表全游戏、战斗特效、独立 QA 或稳定60 FPS通过。

- 分支：`task/T-honor-shrine-20261111`，输入 HEAD `21d270e51a184a6145fc269a850ca0298b590fac`，验证时下述源码为 dirty。
- APK：`.local/android-apk/FFRS-Shrine-Fix.apk`，3753362 B，SHA256 `e7c3f82c2c3113a1446fbedf7a574524c3ef5515769f2d9d523462a6f6c5095c`。设备已安装 base.apk 的 SHA256 相同。install-r，未清数据或改存档。
- 普通 GameActivity，无 capture layer；图形捕获4个 global settings 和7个 properties 恢复原值；默认16个 frame slots，无2槽临时覆盖。
- 本地原始证据根：`.local/shrine-takeover-20261111/`。

## 原因与最小修复

### 过图 LOW_MEMORY

Cursor run9..13 退出记录为 LOW_MEMORY；GPU Memory 旧轨迹达到11173044224 B，显式 Vulkan 分配不能解释全部增长。每个 mip 写入前重复整条 mip-chain 上传，驱动命令池暂存累计。实验对照：保持原复制顺序只增加同步、16槽仍退出；同包2槽存活；最终保持16槽、原复制/上传语义，在累计64MiB上传后的 copy_texture_buffer 安全边界等待所有已提交 GPU 工作和异步 reset job，并使用 RELEASE_RESOURCES 重置空闲命令池，实测成功载入及出入神殿。

修复在 `src/graphics/nvn_vulkan/engine/xbg_game.cpp`。不在 get_image 内等待/重置，因为其调用者可能持有当前录制的命令缓冲。没有删 mip、跳绘制或改分辨率。最初的合并 CPU mip 写入实验已弃用并保留归档，不能把该实验的4.57GB峰值归给最终包。

最终60秒内存捕获 `codex-shrine-final.pftrace` 仅覆盖移动后段/静态窗口，GPU0全局计数在4859207680..4915900416 B之间（3649次采样）；无按进程计数，不能据此报告本进程最终过图峰值。`final-memory-metrics.json` 记录该限制。实测路由每次检查均存活，可用内存最低采样约2.4GB，不能替代完整峰值轨迹。

### 地面大方块

同一帧 GPU capture `blocks.gfxr`（185956748 B，frame1358，132645 blocks，精确EOF）追踪到首次出现方块的 draw131709，前一 pass 的 draw131679 无异常。FS `97ae64dbc5e7ff26` / module1871 的 binding64 实际为 R16_UINT（VkFormat74），buffer2407 / offset903168 / range696320；API和资源导出相符。着色器却执行 float ImageFetch 后 bitcast 到整数光照索引。

修复在 `xbg_spirv_stencil.h`：仅该 stage4/key 的 B64 两次 fetch 改为 UInt，保留原消费者位模式，使用现有 helper。保留原 R16 视图和范围，不加入会强制变 R32 的 scalar binding fact。同一 capture 只替换该 module 的 GPU replay 即消除方块，光束、雾、存档点保留；生产 helper 输出与该 override 逐字节相同，普通安装包运行同样修复。

原 shader SHA256：`6ff4a0d86654323c6e4692e8208c6f5214bd7febfced670169293a5c216e130e`。原/改截图 `shrine-original_frame_1.png`、`shrine-fixed-index_frame_1.png`；因果索引 `replay-causal-index.json`。普通包截图 `final-entrance.png`、`final-reenter.png`、`final-movement.png`。

## 验证

- `FFRS_APK_NAME=FFRS-Shrine-Fix.apk bash scripts/android/build_game_window_apk.sh`：exit0，`final-build.log`；`final-install.log` 为 install-r 成功。
- 自有 SPIR-V fixture `tests/device/reflection_uint_fetch.spvasm`，原/改 `spirv-val --target-env vulkan1.1` 通过；`test_reflection_uint_fetch.cpp` 经 clang++ ASan/UBSan 编译执行通过：两次 fetch 保留，共用 float binding 不变、原 R16 view fact不变、错误 stage/key/binding 拒绝。
- 真机普通包使用现有 optional frame CSV 记录真正新渲染帧，image_writer 每帧变化、present_result 全0，非屏幕刷新率计数。`regress-route.py`/`.log`、`final-frame-27357.csv`、`final-regression-guest.log`、`final-route-metrics.json` 可复核。
- 移动标记16680..17460：780个间隔/26.118414834秒 = **29.86398696 FPS**，P50 31.6676ms、P95 47.0589ms、最大81.3775ms，所有间隔保留。
- 过图标记16080..16680：600个间隔/21.970125825秒 =27.3098FPS，最大3178.1434ms（载入停顿包括在内）。不能称过图零停顿或稳定60帧。
- 原有音频源码/缓冲/fence_spin=0保留。验收引用 `coordination/prompts/_STANDING-audio-acceptance.md`；本轮未做人耳蓝牙两分钟/热插拔验收，音频门 NOT_RUN。独立QA NOT_RUN。

## 保存与恢复

用户关闭 Cursor 后接手，原 Cursor WIP 完整归档 `cursor-wip.patch`、`xbg_game.cursor.cpp`，未混入最终生产源码。预存未跟踪 `FFRS/`、`FFRS-data.zip` 未变更/暂存。没有修改或操作 RG477M、原始游戏模块、数据包、存档，没有推送。

## 后续真机技能及战斗验证

同一普通 e7 APK、同一 PID27357，实际随机遇敌：拉斯韦尔释放冰结、雷因释放火焰黑魔法、敌方受击/攻击、普通物理攻击、胜利结算并返回地图，本轮未退出。录像抽帧可见蓝色冰晶/碎片、橙红火焰/爆炸及受击粒子，无先前整块矩形异常或整片黑效果；仅覆盖这两个技能和这一场战斗，不代表全部技能/LIMIT/全游戏验收。截图 `battle-*-skills.png`、`battle-ice-target.png`、`battle-end.png`、`after-battle-map.png`，原日志 `battle-guest.log`。

- `battle-ice.mp4`：8218737 B，SHA256 `7b0182ff1929bb6d15468b8f996759a4b11f7d6ddf18bfc09ea868918458ec70`。
- `battle-fire.mp4`：8898923 B，SHA256 `f5ccb99a1576670accd7deba07cb5b5d27e428e4ffcc03be5d6e7d1128db6941`。
- 采集命令/hostPID/退出码在 `battle-{ice,fire}-{plan,launch,record}.txt/log`；录像期间不做无采集开销FPS结论。抽帧：`ffmpeg -v error -i <video> -vf 'fps=2,scale=750:-1,tile=5x4' -frames:v 1 <contact.png>`。
- 运行输入模块 Build ID（实际 boot log）：`736837afd390442ed7c74a7de730e71b`，image162869248 B，651 imports；Vulkan driver0x802fa02e，Android ARM64，NDK26.1.10909125 RelWithDebInfo。
- 战斗后 MemAvailable 1828744kB、SwapFree12643756kB，仅时点采样。日志实际音频 performance=10；未改音频源码，但不能声称满足 NONE/蓝牙音频门。

另尝试实际路线CPU采样：首个 simpleperf --app 报15474samples但外部输出0B，导出失败原记录保留；run-as重试取得12331samples（RHI6292），途中触发随机遇敌，包含战斗转场/菜单，不能作为连续移动性能结论。FP错误调用链11138，inclusive百分比不可用；仅leaf PC提示descriptor和内存拷贝值得后续研究。`profile-scope.json`、`profile-move2.data`及report/source-line索引保留；没有因此改动性能参数或源码。

下一步：单独记录明确连续移动/动作窗口，避免战斗菜单冒充性能验收；针对真实瓶颈再做最小优化。继续更多过图和技能，稳定帧率、全部特效和独立QA仍未完成。当前目标仍 in_progress。

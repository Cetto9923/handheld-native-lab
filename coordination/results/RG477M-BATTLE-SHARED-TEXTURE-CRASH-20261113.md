# RG477M 战斗开始即 FATAL —— 共享 image type 的 formatless texel 绑定（2026-11-13 真机复现）

观察时刻：2026-11-13 09:05–09:20 CST
设备：RG477M `RG477M01445122`（Mali-G615, `k6897v1_64`），APK 1.0.9 / versionCode 9，sha256 `3309cfc8…`
release HEAD：`4156929`

## 现象

**战斗一开始，游戏进程直接死亡。** 这是用户在睡觉前报的「战斗结算画面闪退了」的**真机复现**，
只不过这次崩在**战斗开始**（加载战斗 compute shader 时），不是结算。

- `engine.log.fatal`（51 B）= `FATAL: Texel format lacks formatless-write support`
- `ps -A | grep ffrs` → 进程不存在
- 巡游脚本 `MOB/hunt10.sh` 在 step 158 检测到 FATAL 并中止；崩溃前一帧画面均值 128（白闪）

## 关键日志（`guest_boot.log` 尾部，顺序即发生顺序）

```
[engine] NVN shader ba0d8842fa53e521 stage=5 loaded words=19145 ms read=5.25 rewrite=2.10 analyse=1.72 module=0.11 reused=0
[engine] NVN shader slow-load identity=ba0d8842fa53e521 stage=5 frame=402084 monotonic_begin_ns=36521097643944 monotonic_end_ns=36521106862175 total_ms=9.22
[engine] NVN typed storage UNSUPPORTED shader=ba0d8842fa53e521 binding=53 format=100 reason=rewrite-refused shared=0 unchanged=1
[engine] FATAL: Texel format lacks formatless-write support
[engine] FATAL: Texel format lacks formatless-write support
```

## 模块实证

游戏自带 compute shader `ba0d8842fa53e521`（stage 5，13 个绑定），已归档到两仓 `.local/modules/`（gitignored，不提交）：

```
%31 = OpTypeImage %float Buffer 0 0 0 1 Unknown     <- binding 47, STORAGE_IMAGE, sampled=1
%44 = OpTypeImage %uint  Buffer 0 0 0 2 R32ui       <- binding 17, 已 typed
%48 = OpTypeImage %float Buffer 0 0 0 2 Unknown     <- 被两个绑定共用的 formatless image type
%cp_i_cb2_4A_  = OpVariable %_ptr_UniformConstant_48   (OpDecorate Binding 53)
%cp_i_cb2_48_ = OpVariable %_ptr_UniformConstant_48   (OpDecorate Binding 59)
```

- binding 53 / 59 都是 `STORAGE_TEXEL_BUFFER`（descriptor type 5），包标志 `0x80000501`（formatless write 位置 1）
- 49 个 `OpImageWrite`、1 个 `OpImageRead`（在 %44/binding 17 上）、2 个 `OpImageTexelPointer`（也在 %44）

## 根因（与已修的两条都不同，是第三条）

`xbg_typed_storage_rewrite()` 在**该 image type 的 descriptor 使用者不止一个**且 `shared_ok==false` 时返回 -1（拒绝改写）。
`typed_storage_variant()` 用「共用同一 image type 的所有绑定是否需要**同一个**格式」来算 `shared_ok`。
本模块里 **binding 53 与 59 共用 %48，但两者绑定的视图格式不同**（53 → format 100），
于是 `shared_ok=false` → 改写被拒 → **绑定保持 formatless** → Mali-G615 对该格式没有
`VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT` → 描述符护栏 abort。

上一轮 worker 的报告里已经把这个列为未覆盖风险：
「共享 image type 仅覆盖『所有使用者格式一致』，不一致仍需拆分 image type，未实现」。

**这不是上一轮 `4156929` 的漏网，而是它明确声明未实现的那一项。**

## 影响面

- 只影响 Mali（Adreno 支持 formatless write，不需要这条改写路径）。
- **对 477M 是阻断级**：任何用到这个 compute shader 的战斗都会立刻 FATAL，
  这正是「477M 必须能进战斗实际玩」这条验收底线卡住的原因。

## 已派工

worker 任务 `T-RG477M-SHARED-TEXTURE-SPLIT-20261113`（session `mvs_343d7a537efa428fb142059f4844103d`）：
实现**共享 image type 的拆分**（每个绑定一份私有 `OpTypeImage` + `OpTypePointer`，
各自声明自己的格式），并要求：字节等价论证、宿主契约 + 变异自证、spirv-val、
以及 **477M 真机打完整场到战斗结算画面**。

## 平板线的进展（同一时间）

平板在 `PAD-flow/hunt10.sh` 的 step 40 **首次进入真实战斗**：
Lv.55 巨兽 vs 队伍，回合条、属性相性、生命值/法力值框全部渲染（截图 `PAD-flow/hunt10/h040-NW.png`）。
**随后平板从 USB 掉线**（macOS USB 层也认不到，物理断开，非 adb 问题）。
平板此刻正停在那场战斗里 —— 需要用户重新插拔平板 USB，才能继续驱动到战斗结算画面，
验证 `89386a8` 那条 typed-storage 严格匹配误杀的修复。

## 明确不主张

- 平板的战斗结算路径**尚未验证**（平板掉线，战斗还在跑但无法取证）。
- 8E5 / 神殿仍然 NOT_RUN。
- 没有把「检测到战斗画面」当成「打完一场并到结算」。

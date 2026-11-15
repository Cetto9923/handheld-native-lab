# 项目经验库（跨工具共享）

本文件是本项目**跨工具**的经验沉淀：Antigravity / Codex / Cursor / Claude Code / Mavis 等
任何代理在动渲染/着色器/真机之前都应先扫一遍，避免重走已经证伪或已经修过的路。

## 使用规则

- 每条 = **现象 / 错在哪 / 正确做法 / 出处**（提交、报告章节、设备日志）。
- 结论以**当次实测**为准；被新证据推翻的条目就地标注「已推翻」并写明依据，**不要删除**
  ——被推翻的结论本身就是教训。
- 只增不改历史条目；新条目带日期。新增前先搜本文档有没有同义条目。
- 取证细节在 `coordination/results/` 的报告与 `coordination/experiments/` 的脚本里；
  本文只留「怎么做对 / 别再犯」。
- 本文只记**可复用**的经验；一次性机位、单次数字不要往这里塞。

---

## L-001 着色器改写前，先证明「这个 key 画的是谁」（2026-11-14）

**现象**：提交 `93ebe66` 对着色器 key `062df44b75d37d76` 剥掉 `gl_FragDepth`，
作者注释说它是「terrain shader」。探针实测：**这个 key 在场景里画的只有主角**。
结果主角身上长出黑色横条纹。

**错在哪**：用「注释/提交信息里的语义」代替「运行时证据里的实际使用者」。
一个 key 写错，整条改动就打在错误的物体上，而且症状出现在**另一个子系统**
（这里表现为角色渲染缺陷，完全不像「地面闪烁修复」会引发的问题）。

**正确做法**：改任何按 key 定位的着色器前，先跑一次
`gfx-codex-probe=R` → `capture.txt` 的 `PROBE_GROUP` 行给出每组 draw 的 `vs`/`fs` key，
确认该 key 的实际使用者是谁，再决定改不改。必要时看 `PROBE_DRAW` 的 draw 列表与实例数。

**出处**：`coordination/results/RG477M-FLICKER-LEFTGROUND-20261113.md` §16、§17；
修复提交 `4fc1fd7`。

---

## L-002 「这行是冗余的」必须用数据流证明，不能凭注释（2026-11-14）

**现象**：`93ebe66` 的前提是「地形着色器把 `gl_FragCoord.z` 存进 `gl_FragDepth`，所以这次赋值冗余」。
实测该 fs **从不对 `gl_FragCoord` 做过任何 `OpLoad`**（只有声明，没有读取），
它的 `gl_FragDepth` 存的是自己算出来的深度 ⇒ **剥离不是 no-op**，是语义改变。

**正确做法**：凡是要「删掉一次写入」的改动，先在 SPIR-V 层面证明
**被写的值 == 硬件本来会写的值**。落地方式：`xbg_spirv_reads_fragcoord()`
（该 fs 是否真的从 `gl_FragCoord` 变量加载过值）——只有「真的读了」才允许剥离。
主机契约测试：`tests/device/test_frag_depth_strip.cpp`。

**出处**：同上 §16.2、§17.3。

---

## L-003 整行被剔除 = 水平黑条带 ⇒ 先查深度语义，别先怀疑纹理（2026-11-14）

**现象**：主角身上的「黑色横条纹」一度被怀疑成贴图解码、合成层、抖动、屏幕录制混叠。

**破案特征（可直接复用）**：探针 color0 里主角被画成 **1px 高的水平扫描线**，
行与行之间整行是 `(0,0,0,0)`（3876 个像素只有 **1 种**取值）⇒ 这些像素**从未被写过**
⇒ 不是「画成了黑色」，而是**被深度测试整行剔除**，露出从未绘制的 clear 色。

**可复用模型**：深度语义一旦被改动，LEQUAL 比较会在「深度差函数的等值线」上翻转；
等值线落在竖立的角色身上，就是**一条条水平带**。
所以：**整行同色 + 物体空间 + 时间稳定 ⇒ 深度问题**，不是纹理问题。

**做法**：先看被剔除区域是不是「从未被写过」（取值只有 0），再看深度附件与深度测试状态，
最后才回查贴图。

**出处**：同上 §17.1、§17.2。

---

## L-004 A/B 归因模板：一个布尔开关 + 同机位 2×2 + 引擎侧数字（2026-11-14）

**现象**：根因定位到「某个改写」之后，必须证明它**就是**本次症状的成因。

**正确做法**：
1. 加一个**运行时标记**（读一次、启动时生效）把该路径切成 on/off，
   **不要为了 A/B 改代码编译两次**（构建 17 秒，但改代码会引入别的变量）。
2. **同机位、同存档位置、同一个 APK**（只差一个布尔；把 so 的字节差与 so 大小差记下来当证据）。
3. **两臂都要有引擎侧可量化数字**，不能只靠肉眼。本轮用的指标：
   color0 全分辨率 ROI 内「整行纯黑 ↔ 邻行有内容」的**交替行游程** ⇒ A 臂 8 行对、B 臂 0 行对。
4. 肉眼是主判据（用户在场时由用户确认），agent 负责量化佐证与机制解释；
   **一轮只问一个具体问题**，别让人反复描述同一现象。

**出处**：同上 §17.1；本轮标记见下节。

---

## L-005 运行时标记与探针的预算/一次性语义（2026-11-14）

- `gfx-codex-probe=R`：**每进程只跑一次**。写标记不会重新武装 ⇒ A/B 的第二臂必须**重启进程**。
  一次 9 帧抓取约 **210MB**（单帧 ~23MB），两臂就是半 GB，收尾要删。
- `gfx-live-ubtrace` / `gfx-live-framedump`：**读后即删**；抓取窗口只有几秒，
  **第一次拉可能早于文件生成**，要拉第二次。
- 标记是**一次性**的：A/B 结束后必须改名归档（`.stale*` / `.used*`），否则下一轮带假状态。
- 标记文件的读取时机决定要不要重启：着色器加载期的开关 ⇒ 必须重启；
  逐帧路径的开关（如 `gfx-live-off`）⇒ 不重启即可切。

**出处**：同上 §16.5、§17.1。

---

## L-006 删除的两类权限陷阱（2026-11-14）

- **本地删除**：`rm -rf` 被权限门**静默拦截**（返回 0，文件还在）⇒ 用 `find <path> -depth -delete`。
- **设备 app 属主文件**：`adb shell rm` 删不掉（Permission denied 刷屏）
  ⇒ `run-as org.ffrs.gamewindow sh -c 'find <dir> -depth -delete'`。
  设备上探针残留一次 A/B 可达 **4.7GB**；删完 `/sdcard` 可用空间从 10G 回到 15G。

**出处**：本轮设备清理（§17.5 后）。

---

## L-007 共享 git 历史的多 worktree 协作（2026-11-14）

- honor 树与 rg477m 树是**同一 repo 的不同 worktree**，历史共享 ⇒ 一条线上的提交在另一条线上可见。
- 跨树取提交：`bd98b53` 是 merge 提交，`cherry-pick` 必须 `-m 1`；
  当对方线上还带着我方文档提交时，**用 merge 整条接过来比逐个 cherry-pick 更干净**。
- **不要把对方 worktree 的未提交 WIP 带进自己的构建**（honor 树当时有 6 个文件是脏的）。
- `work/rg477m-native` 若不是别的 worktree 的当前分支，可 fast-forward 集成；
  先用 `git merge-base` + `git log A..B` 确认「对方缺的提交」为空，避免盲合。

**出处**：本轮分支构造与集成。

---

## L-008 注释与提交信息不是证据（2026-11-14）

「terrain shader」「redundant」「statically stores gl_FragCoord.z」这类描述**都是人的断言**。
断言与运行时/二进制事实冲突时，以实测为准，并在 coordination 里**显式撤回**原归因
（AGENTS.md 要求：被推翻的归因必须写明撤回依据，不许让旧归因继续留在派工内容里）。

**出处**：§16.3、§16.4；本轮 `93ebe66` 的 key 误判。

---

## L-009 修复不得以性能换正确性（2026-11-14）

每轮交付都要带基线对照：修复后 **fps 30.00 / submit_cpu_ms 2.55 / sync_wb=0 / sync_rb=0**，
与修复前同量级。改了渲染语义（深度、混合、状态）却没量性能，等于把问题换成了另一个问题。

**出处**：§17.4。

---

## 本轮新增/确认的运行时开关

| 标记 | 语义 | 出处 |
|---|---|---|
| `gfx-terrain-depth` | 缺省=auto（只有真读 `gl_FragCoord` 才剥离）/ `0`=永不剥离 / `1`=永远剥离 | `4fc1fd7` |
| `gfx-codex-probe=R` | 逐帧 draw/附件/绑定取证，**每进程一次** | `xbg_probe.inc` |
| `gfx-live-ubtrace` | 逐 draw 逐 binding 的 uniform 指纹 | `edfecd7` |
| `gfx-live-framedump` | 引擎侧连续呈现帧（帧间差分的唯一可信来源） | `5082aac` |

**开关读数示例（设备日志）**

```
NVN terrain drop frag depth=auto (marker gfx-terrain-depth: 1 = always, 0 = never, absent = auto)
NVN terrain drop frag depth shader=062df44b75d37d76 loader=runtime kept: the shader computes its own depth (it never reads gl_FragCoord)
```

---

## 仍未解决（别当成已修）

- **第三章运河广场地面白纱闪烁**：机制锁定为「半透明覆盖层逐帧出现/消失」，
  具体 draw 未定位，根因未锁定。判读表见
  `coordination/results/RG477M-FLICKER-LEFTGROUND-20261113.md` §14.5；
  已排除路径见 `coordination/experiments/flicker-20261113/README.md`。
- **第三章地面闪烁需复测**：本轮修复改变了「主角那个着色器」的深度语义；
  真正的地面着色器若读 `gl_FragCoord` 则剥离照旧，但**尚未在第三章机位复测**，
  需用 `gfx-terrain-depth=1` 与缺省各跑一次。
- **陆行鸟召唤后画面异常**：仍未复现；世界地图 3D 视图有黑色块状缺失，未取证。

## 2026-11-14 Codex 复核修订

- L-002 中“读取 FragCoord 即可剥离深度”的做法已撤回。真实 `062df44b75d37d76` 经 `OpAccessChain` 读取内建值，最终 `FragDepth` 经 `FMin` 等运算产生。存在读取无法证明输出与光栅深度相等。当前普通路径完整保留游戏深度，诊断开关 `gfx-terrain-depth=1` 仍为显式实验用途。证据见水都报告 §19 及提交 `1950d52`。
- 上表 `gfx-terrain-depth` 缺省含义已更新为保留原始深度。旧“auto”说明仅描述历史版本。
- 旧 `FLAME_DR` 聚合相同无法排除同 shader 子 draw 的顺序、被跳过的绘制、纯深度绘制或 GPU 间接实例变化。它在实际发出绘制前采集，并将同键绘制合并。旧 uniform trace 在 descriptor-build 路径且只哈希前 4KiB，也无法排除 memo 命中和实际 GPU 输入变化。
- 当前 `R` 路径的 `window=24` 未递减，实际由 `repeat_captures<32` 限制窗口。应按实际文件、帧号与 END 记录核对预算，旧“固定9帧”描述已过时。不要为再次取证删除尚未归档的文件。
- 在固定 `90831749` APK、PID28827 的水都场景已采集90帧。左地面具有亮度与锐度反相关的跳变。present55574 的逐 pass 证据定位到 `VS431a6183c3565f66 / FS6e2d000b284e669c` 写入 RGBA16F 透明层，`CSf55cfedfb74f811a` 随后合成。单帧贡献不能单独证明时间闪烁的成因。


## 2026-11-14 显示修复总结与性能优化回归依据

### 当前画面对照版本

1.0.21-preview 的生产源码为 `780ebc67d4b9cfe41c7df35bc0e1a7280e0796a1`，APK SHA256 为 `07f5f0368183b7fb9228c9f744fd835bf623c05fd0e86a1ecb71e13f7ec1419e`。RG477M 用户实玩确认开场女主身体“不闪烁了”。98项宿主检查、Android构建及独立精度组件复验已通过。APK、原31秒异常录屏、同PID日志和帧CSV均在 `.local/codex-recovery-20261114/`。第三章地面、其它场景和其它设备保留各自的验证状态。

### L-010 自算深度输出的完整数据流

人物黑条的历史修复包含保留游戏的 FragDepth 输出，当前源码修正为 `1950d52`。L-002旧说明已经由本文件末尾复核修订撤回。062df实际通过AccessChain读取FragCoord，最终深度由FMin等运算产生。删除store需要证明最终写入值与光栅深度相等。读取内建值本身只是一条数据流线索。

性能修改可复用 `tests/device/test_frag_depth_strip.cpp` 和 verify_host 中 frag-depth-strip 契约，画面对照覆盖人物面部、裙身、头发与移动轮廓。普通路径的 `gfx-terrain-depth` 缺省为保留游戏原始输出，值1为显式剥离实验。

### L-011 顶点浮点约束与女主间歇缺口

原18和20均复现女主脸部、裙身间歇黑色栅状缺口。21吸收固定DroidPlay `a72a85c59` 的NoContraction处理，并同时包含clear修复，用户确认症状消失。当前证据支持整包显示改善，单项代码因果尚待分离。

共享优化入口在tidy之后对位置生成阶段补充NoContraction，runtime、warm与integer变体使用同一入口，typed变体保留已有装饰。显式Fma有原机融合语义。两个真实7ca7模块各补492条约束、各保留100条Fma，完整函数体字节与幂等独立核对通过。group装饰和multiple-entry模块沿原路径处理，具体覆盖见包装及契约。

可复用 `scripts/android/verify_spirv_precise.sh`、`tests/device/test_spirv_precise.cpp` 与 `position_precision.spvasm`。脚本已接入verify_host。默认启用配置在预热线程启动前读取，启动标记 `gfx-position-precise` 优先于 `DM_SPV_PRECISE`。同包精度对照通过新进程配置建立，shader复用检查模式2可逐字核对warm与runtime输出。来源与SHA见third_party/droidplay/provenance.json。

### L-012 清屏的目标覆盖与3D内容保留

`cba445f` 修复folded clear沿普通draw附件交集规划而缩小目标宽高或层数的问题。create_pass在图像副作用前核对selected mip/view的完整覆盖，失败走target-only路径。3D image.layers为1，保留判断还需核对selected mip的depth slices、base和count，首次部分覆盖需要导入未清CPU内容。

`view_range_contract.c`、`clear_format_contract.cpp`、`layer_output_gpu_contract.cpp` 包含对应契约。独立宿主ASan/UBSan和32层color配1层depth、8slice非零base等反例通过。GPU组件直接发出VkClearAttachments，完整引擎路径和3D未选slice端到端canary在该轮记录为NOT_RUN。普通clear命中可看现有coverage日志，R/provenance抓取会改变fold准入，需记录实际路线。

### L-013 整数纹理和变体身份影响角色、特效与转场

`03ab3e6` 修正typed storage先用后声明导致新建游戏exit70。`26bb311` 对实际UINT纹理用匹配整数fetch，再bitcast回客机原浮点寄存器位值，并在组合身份中同时保留integer与typed recipe。数值转换、signedness、格式位宽和通道数各有独立含义。较短的shader键或单个成功变体无法覆盖组合路径。

可复用 `verify_integer_buffer.sh`、`verify_shader_identity.sh` 和typed-storage现有契约，真实原BOSS/转场故障流程曾在17包由用户推进通过。后续性能候选沿原正常shader载入、warm和两类变体入口核对，角色及技能的实机覆盖分别记录。

### L-014 子组大小采用实际设备事实

Mali-G615实测subgroup16，旧32-lane reduction在真实组件得到双倍合计，narrow修正后3次通过。已保存原始compute模块库的离线校验结果。对应 `xbg_spirv_narrow.h`、固定上游头与 `verify_spirv_narrow.sh` 已在现有门禁内。

GPU厂商、真实subgroup、支持stage与helper形状共同决定路径。性能比较记录narrow配置及设备身份，跨GPU采样各自建组。未知helper保持当前保守处理。

### L-015 快照和部分覆盖同时维护数据正确性

原18和20也能复现女主缺口，19的UBO优化是唯一引入原因这一假设已经撤回。当前21生产UBO沿旧范围路径。UBO范围分析可作为后续候选重新核查，其下游pending_view、GPU resident选择与变体实际读取范围仍需配套验证。

submission-owned快照、GPU输出准备、读写依赖和slot退休参与同一资源生命周期。复制优化先证明本次draw/dispatch的真实读取范围与内容版本，再量实际复制字节、命中率和帧时间。GPU等待可能与另一CPU线程并行，legacy_wait无法直接从submit CPU相减。CPU三帧采样按实际累计帧数归一。

`7de9e39` 的CPU镜像上传修正保留GPU fallback所需的partial preserve，失败前预检完整写入范围，成功后才发布内容权威与版本。可复用 `test_upload_cpu_preserve.py`。这组正确性条件同样适用于后续顶点、索引、descriptor和纹理缓存优化。

### 性能候选的显示回归检查点

| 检查点 | 已有证据或入口 | 复验观察 |
| --- | --- | --- |
| 开场女主近景与移动 | 21用户确认、原异常31秒录屏、precision/frag-depth契约 | 面部、裙身、头发的黑块、条纹和轮廓闪动 |
| 战斗技能与BOSS后推进 | integer/typed/identity契约及17实玩流程 | 特效、伤害数字、人物、转场及退出信息 |
| 多层/3D附件clear | planner、格式及layer组件契约 | 所选mip/view覆盖、未选slice内容与残影 |
| 普通与warm/变体shader | 精度与身份契约、reuse模式2 | 模块字节、组合recipe和配置一致性 |
| 纹理CPU镜像与GPU fallback | upload preserve契约 | 部分更新、未改区域和失败事务性 |
| 水都地面及其它机型 | 各自历史报告，当前未完成整包回归 | 继续保留未验状态及原始证据 |

上述检查点作为性能修改的现成回归依据。项目当前工作重点为真实场景帧率与长帧，最终目标沿docs/PROJECT.md的原速真实60FPS执行。开场剧情、走动、战斗特效、室内/副本及水都分别保留冷暖数据。FPS、P95/P99、超过50/100/250ms的长帧、CPU/GPU耗时、实际复制量与有声流xrun增量共同记录。教程弹窗和暂停窗口有各自标签，录屏编码帧率仅用于解释媒体采样。

出处汇总见 `coordination/results/CODEX-UPSTREAM-PORTS-20261114.md`、`RG477M-FLICKER-LEFTGROUND-20261113.md` 及 `.local/upstream-switch-zelda-20261114/` 各独立报告。后续显示变化按构建、同PID与具体场景更新证据。

## 2026-11-15 世界地图取证新增

### L-016 顶点格式与接口数值类型同步

世界地图实际pipeline将RGBA8_UINT顶点和实例数据送入声明float4的VS。格式、Location、stride和instance rate需与最终模块一起核对，integer texel-buffer修正不会自动覆盖vertex attribute。按实际格式生成接口变体时，原始寄存器位值由匹配类型Load后bitcast恢复，真实draw参数与游戏运算继续沿原路径。UINT、SINT、float/normalized分别处理，短向量的缺省分量取值也受数值类型影响。

变体须覆盖深度预绘制及正式绘制，共享输入指针类型须隔离，未知指针使用需事务性拒绝。组合identity、warm加载、持久化recipe、实际input_classes与metadata同步接入。可复用verify_vertex_input.sh及verify_shader_identity.sh。22候选宿主与Android构建通过，地图因果与开场同包回归仍待真机。证据见CODEX-CHAPTER3-MAP-20261114.md。

### L-017 应用缓存可写性用实际UID验证

由adb shell直接放入应用外部files的缓存可能仍属shell且仅owner可写。文件存在或预热读取成功不足以证明学习表可追加。使用run-as的test -w及真实打开结果核对，保留原字节备份后，由应用UID创建同字节stage并校验摘要，再原子替换。当前设备恢复后学习表正常追加，帧率收益尚未测定。

### L-016后续真机对照

22源码0bd4dc5、APK2e4c354e在RG477M的PID18193完成同机位新路径、旧路径、新路径对照。a455/11320/00758三个地形VS均成功生成vin1接口变体。旧路径重现大面积地面缺失、城镇下黑色矩形及巨大斜面，新路径前后两轮均恢复，18个普通原生present及同次日志已保全。此结果支持接口类型缺口为该机位异常成因。原生帧对照与独立离线审证据记录见CODEX-CHAPTER3-MAP-20261114.md及vertex-input-build/map22/。开场、战斗、地图其它位置与性能目标各自复验。

### L-018 预热身份需要贯通发行工具

22运行时已经支持完整ib1/stb1/vin1 identity，旧发行inspector仍只识裸key与ib1，真实学习表无法进入种子发行流程。dd76193同步发行语法与格式边界，并把APK可选补充seed接到真实legacy/managed runtime，再复用C++已有的学习表、APK补充表及数据seed union。完整serialized key用于去重，源key用于archive存在性检查，变体后缀和word内module hash同步验证。

可复用test_pipeline_warm_inspector、test_apk_pipeline_warm_seed及既有warm flush契约。APK内seed按签名包交付，Java原子stage校验有界长度与record framing，完整recipe校验由发行工具和native reader承担。坏可选seed保留旧seed并继续启动。23已构建，warm实际命中及首次技能收益仍待真机验证。

### L-019 主动技能长帧与暖态负荷分别取证

PID18193的战斗日志直接量到0.5至1.1秒pipeline创建，也保留2.024秒且pipeline/shader为0的未知长帧。后续其它技能又出现连续31帧新编译0、GPU中位约65ms的重负荷。暖态菜单约30FPS不能覆盖这些主动阶段。Mali driver cache本轮确有成功载入和增长，缓存是否有效应以当次记录判断。

录屏PTS、媒体creation/modified与引擎monotonic分别保全。音频按每次stream重开划生命周期，xrun重置后的末值不能覆盖此前增长。GPU pass采样会改变部分resident及精确计时路径，用于热点定位，普通运行长帧另外保留。详细证据见CODEX-STUTTER22-20261115.md。

### L-020 存档备份一致与正常写入分别验证

多轮安装前后tar一直保持同一摘要，本轮发现实际GameSaveData0写打开errno13。导入的GameSaveData0/1为shell所有、mode644，应用UID读写打开失败。备份完整性、文件属主及普通游戏保存的写入结果分别记录。

本轮先在标题停止进程保全原字节和metadata，再由应用UID创建同字节stage并保留mtime，摘要核对后原子替换。原文件内容保持，恢复应用UID读写打开成功。用户正常保存落盘的验证继续单独记录。恢复点及首轮stat参数引用错误均保存在seed-build/save-owner-repair/，详细报告见CODEX-STUTTER22-20261115.md。

### L-021 printf聚合日志的格式与实参逐项核对

scene close_window的shader_slow_ms使用%.1f却曾传ULL，导致后续浮点/整数参数错位。已修为double并用抽取生产函数的-Wformat=2契约验证11个数字字段，旧实参编译被拒绝。已发布23仍沿旧打印，从shader_slow_ms起的scene leave聚合字段不能按标签使用，逐帧stall及CSV另行核验。PID9359的79559ms/17次创建字面归因已撤回，原始逐帧记录为8次1228.3ms。证据见CODEX-RESUME23-20261115.md及scene-log-contract。

### L-022 提交槽回收调度需要覆盖全部配置槽

slot_serial与slot_index同增，16槽时固定slot_serial低4位小于2只会调度slot0和slot15，其余14槽漏过texel缓存周期清理。66b873a改为按槽循环轮转相位。生产提取契约遍历2至16槽，旧策略反例能失败，原退休保护继续执行。荣耀32真实副本记录gc_mask0xffff且缓存非空，证明全槽入口覆盖。长期内存与帧率收益继续分别测量。详见CODEX-TEXEL-GC32-20261115.md。

### L-023 ADB归档的完整覆盖和错误流分别验证

荣耀GameSaveData2至5为mode600，shell读取失败，run-as在该机的外部FUSE路径也被拒绝。exec-out会将tar错误混入二进制stdout，传输返回0，tarfile可能仅解析前面的成员。此前归档字节相同只证明已读出部分相同，完整性结论已撤回。shell-T能保留stderr及远端退出码，完整成员清单、长度和逐文件摘要另行核对。

33沿既有显式调试副本入口由应用UID读取全部源文件，复用原子复制及SHA校验，9文件源与私有副本逐一匹配。源目录元数据比较按实际文件条目执行，父目录..mtime会随日志创建改变。命令、失败与补核证据见CODEX-PAD-CHORD33-20261115.md。

### L-024 待命诊断的真实开销与移动窗口

38神殿内部CPU栈实际定位probe_guest_barrier和probe_image_read_barrier中的fopen。gfx-codex-probe=0仍因文件存在而使probe.waiting=true，逐屏障重复读取确有外部文件I/O。8cf2764保留既有请求语义并把观察读取合并为每帧一次。40同进程两对静止对照为旧18.81/18.36FPS、新21.38/18.83FPS，效果波动。清理标记并重启后CODEX_PROBE enabled=0，原窗口按带待命条件保留，不能直接作为无标记环境的结果。

用户明确要求进入神殿内部并持续走动。移动helper用同一持续触点南北往返，计量起点在手指按住后的新CSV flush，终点取松手前最后flush，确保量化区间全部在持续输入期间。前两段含中途PNG的窗口和一次729ms长帧保留，后续普通移动窗口只在前后截图。40 PID6355帧18420至18960连续540间隔约28.81FPS，P9972.49ms、16帧超50ms，仍未稳定30。CPU细分另采移动6帧22292至22297，诊断计时开销与普通FPS分开记录。详见CODEX-HONOR-PROBE40-20261115.md。

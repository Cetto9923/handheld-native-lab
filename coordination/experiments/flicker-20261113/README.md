# 运河广场地面白纱闪烁 — 分析工具集（2026-11-13）

本目录固化《最终幻想：共鸣》第 3 章水都（奥尔德里昂）运河广场「主角左侧地面白色区域忽闪忽闪」
这一闪烁的采集/分析脚本。原始抓帧数据是易失的临时文件，脚本本身入库以便复现。

问题单：`coordination/results/RG477M-FLICKER-LEFTGROUND-20261113.md`
分支：`task/T-rg477m-flicker-liveoff-20261113`

## 判定口径（踩过的坑，别再犯）

1. **最小样本 90 帧 = 10 段 × 9 帧连续呈现帧**。少于 90 帧只能出「疑似」，不能出结论。
   反例：4 帧样本曾把 `memofront` 误判为「消除闪烁」；90 帧复验 10/10 段仍闪（44 次转换）→ 假阳性。
2. **一切闪烁幅度以引擎侧呈现帧为准**（标记 `gfx-live-framedump` 落盘 `files/frame-dump/f<present>.bin`）。
   `screenrecord` 在 60Hz 屏按 30fps 采样会产生 23% 重复帧（混叠），**不可用于帧间差分**。
3. **ROI 必须排除主角**：地面纯鹅卵石区 x260..560 y400..540；主角在 x≈400~480。
4. **场景守卫**：广场场景 draws≈505 / passes≈80；读档与菜单画面 draws 24~148 且画面近黑（lapvar<4）。
   用户在读档/走动期间抓到的数据一律作废。
5. **帧对齐用引擎 present 计数器**，不要用墙钟：设备 `CLOCK_MONOTONIC` 与 `/proc/uptime` 不一致。
6. **A/B 结束后必须把设备标记改名归档**（`.stale*` / `.used*`），否则下一轮测量带假状态。

## 脚本

| 脚本 | 作用 |
|---|---|
| `collect_contig.sh` | 一次 armed 标记 = 9 帧连续 present；每档 10 段 = 90 帧；输出 `maxContiguousRun` |
| `an_runs.py` | **可靠闪烁判定器**：按连续段检测状态切换（\|Δmean\| ≥ 3.0），输出段数/转换次数/alpha |
| `an_cap4.py` | 每帧 mean/lapvar + SHARP/BLUR 分类 |
| `an_alpha_seq.py` | alpha 强度、相邻帧一致性 |
| `an_evt.py` | 候选 draw 帧级对齐（配合 `engine*.log`） |
| `an_present.py` | present 目标 draw 分析 |
| `an_sep.py` | 全字段（含十六进制 hash）类别分离测试 |
| `an_corr.py` | 逐帧点二列相关 |
| `an_align.py` / `an_align2.py` / `an_ord.py` | census 与有序 draw 列表比对 |
| `an_veil.py` / `an_shape.py` / `an_struct.py` | 仿射拟合、alpha 空间结构 |
| `absweep3.sh` | `gfx-live-ab` 档位扫描 |
| `verify_memofront.sh` | 单档 90 帧复验 |
| `ub_pipeline.sh` | ubtrace + 呈现帧一键采集（下一轮用） |

## 已排除的路径（别重走）

合成层 / 屏幕录制混叠 / 整幅曝光与 tone-mapping（RG477M 上 `dm_bright_k` 弱符号未实现，压根不生效） /
分辨率与渲染目标尺寸切换 / 亚像素位移 / 贴图滚动 / 贴图逐帧重上传 / GPU 常驻索引与间接参数来源 4 档 /
每帧全 GPU 同步 / 着色器改写与复用 / 13 条 `gfx-live-set` 路径 / 11 档 `ab_off` 路径（含 `memofront`）/
屏幕空间合成 alpha。

## 已被推翻的自身结论（方法学教训）

- 「两态 draw 列表完全相同」——当时 census 容量只有 64，每帧溢出约 95 条，假阴性。512 容量后才是真结论。
- 「`pmean`/`rtpx` 完美分离两态」——值域大量重叠，伪相关。
- 「`memofront` 关闭后闪烁消失」——4 帧假阳性，90 帧复验证伪。

## 当前定位（截至 2026-11-13）

完整 census（512 容量，45 帧，`over=0`）显示：两态 draw 条目集合完全相同（共同 80 条，独有 0 条），
有序列表跨态并不比态内更不一致，没有任何 draw 计数能预测状态，`FLAME_FRAME`/`FLAME_Z` 全字段无一能分离两态。
⇒ **差异必然在送进同一批 draw 的数据里**（uniform 字节 / 顶点数据 / 纹理内容 / 渲染状态 blend+depth）。
下一步：`gfx-live-ubtrace` 逐 draw 逐 binding 指纹追踪，按 present 号与像素状态对齐，找跳变 binding。

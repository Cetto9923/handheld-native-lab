# 给 Codex 的任务 v3：追查黑屏根因 —— 延迟渲染 GBuffer→光照→合成链

> 接 v2（occ A/B）和你刚交的「无 FS 诊断」。你那轮**成功推翻了"3D 世界普遍缺片元着色器"的假设**（那是 Kiro 提的，错了），干得好。本轮在你的结论上收窄方向，**只查一条链**，不要泛查。

---

## 一、已排除 / 已锁定（你上一轮的成果）

- **排除**："几何缺片元着色器导致黑屏"。无 FS 的 draw 全是合法纯深度管线（2 个 VS，均无颜色附件）；121 条管线里仅 3 条无 FS 且全是无颜色附件/写掩码全零；六 MRT 颜色管线子集 0% 缺 FS。几何**有 FS 且正确绑定**。
- **排除**：`-5`（`xbg_game.cpp:3108` 的 `xbg_graphics_stages`）是每 draw 即时的深度专用管线合法性判定，非累计、与黑屏无直接因果。
- **锁定**：几何正常写入 6 MRT + 深度的 GBuffer（与截图"六 MRT 颜色管线"、账本 GPU 分解"延迟渲染主 pass = 1384×780 att7 fmt122、7 附件、几何仅 6.4ms"吻合），**但画面全黑，UI/小地图正常**。

## 二、本轮唯一方向：延迟渲染数据链

这是 deferred rendering 的典型黑屏形态：**几何把 GBuffer 写对了，但光照/合成这一步没把 GBuffer 正确读出来合成到屏幕**，最终输出全黑；UI/小地图在合成之后单独叠加，所以正常。

**只读**追查这条链，重点四项：

1. **GBuffer 写 vs 读的格式/view/layout 一致性**（最可疑）
   - 找到 7 附件主 pass（att7 fmt122，1384×780）写完后，**哪个 pass 或 compute 把这些 GBuffer 当输入纹理采样**做光照/合成。读 `begin_pass` / render target 绑定 / `descriptors` 路径。
   - 对每个 GBuffer MRT + 深度，列出：写时（作为**颜色/深度附件**）的 VkImage format / view format / layout，vs 读时（作为**sampled image**）的 format / view format / layout。**任何一处不一致**（尤其深度被当颜色采样、sRGB vs UNORM、view format 覆盖错、layout 不是 SHADER_READ_ONLY）就是高度嫌疑。

2. **合成结果最终去向**
   - 确认光照/合成的输出**是否真的写到了呈现用的 swapchain / present image**。对比标题画面（正常显示）的呈现路径——标题是单颜色附件无深度，它的最终输出怎么到 present 的，世界的输出到了哪里。

3. **GBuffer 内存 alias / 同步**
   - 查 GBuffer 附件是否有 alias / 内存复用，导致光照 pass 读时内容已被后续写覆盖。
   - 查同步：GBuffer 写完（颜色附件输出）到被光照 pass 采样（SHADER_READ）之间，有没有正确的 layout transition + barrier；缺失会读到未定义/全 0。

4. **标题 vs 世界 的差异点**
   - 标题（单颜色附件、无深度、正常显示）与世界（6 MRT + 深度、黑屏）在"附件 → 被后续采样/呈现"这一步的**具体代码路径差异**。差异处就是根因候选。

---

## 三、严禁 / 交回

**严禁**：改源码、编译、git、伪造数据、在黑屏场景测 fps 当成果。本轮纯只读源码+已有设备日志分析。

**交回**：
- GBuffer 各 MRT + 深度的「写时 format/view/layout」vs「读时 format/view/layout」对照表。
- 合成结果最终去向（到没到 present image），与标题呈现路径的对比。
- 是否存在格式/layout/同步/alias 不匹配的**具体代码位置**（文件:行）。
- 黑屏根因定位结论 + 候选排序。
- **若找到明确不匹配点**：给最小修复候选（记为"改动 D"，只描述改哪里、怎么改，不要动手改）。

## 四、边界

沿用 AGENTS.md 与今日约定：只在 `.worktrees/codex-black-screen`（task/T02）；不 push/不碰 main/不动主树脏改动；产物进 `.local/kiro-runs/43-gbuffer-chain/`，不进 git；一切以源码+真机日志实证为准。

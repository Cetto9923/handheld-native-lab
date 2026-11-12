# RG477M Texel — 出处记录（OSS-NOTES）

> 20261113 轮**新建**本文件。前序报告 `RG477M-TEXEL-20261112-RESULT.md` 引用了它，但仓库里
> 并不存在该文件（`ls coordination/results/` 无此名），因此本轮按新建处理，而不是追加。

本文件只记录**本轮（20261113，覆盖统计与 P1 改写）真正用到或试图用到的上游出处**。
每条都写明：URL、不可变 revision、许可证、支持范围、**是否真的取到**。
搜索摘要、404 页面、其它游戏的演示都**不**作为支持证据。

## 1. 工具链（实际用于判定）

| 项 | 值 |
|---|---|
| 名称 | SPIRV-Tools（`spirv-val` / `spirv-as`） |
| URL | https://github.com/KhronosGroup/SPIRV-Tools/releases/tag/v2026.4 |
| 不可变 revision | tag `v2026.4`；release 元数据 https://api.github.com/repos/KhronosGroup/SPIRV-Tools/releases/tags/v2026.4 （`published_at` = 2026-10-06T15:28:28Z） |
| 许可证 | Apache License 2.0（LICENSE 实际取到：https://raw.githubusercontent.com/KhronosGroup/SPIRV-Tools/v2026.4/LICENSE ，HTTP 200） |
| 支持范围 | `--version` 输出：SPIR-V 1.0–1.6，OpenCL 2.2，Vulkan 1.0 / 1.1 / 1.2 / 1.3 目标环境 |
| 是否真的取到 | **是**：release 元数据 HTTP 200，LICENSE HTTP 200 |
| 本轮实际使用的二进制 | `/opt/homebrew/bin/spirv-val`，版本串 `SPIRV-Tools v2026.4 unknown hash, 2026-09-10T13:28:18+00:00`（Homebrew 安装的构建；其构建日期早于 release 公布日期，**不是**从上面的 tag 源码现场构建） |
| 本轮用途 | ① 验证 9079 个改写后模块（`--target-env vulkan1.1`，全部通过）；② 发现基线的 1355 个失败（1353 capability + 2 重复类型）；③ 装配宿主契约 fixture（`spirv-as --target-env vulkan1.1`） |

## 2. 规范

| 项 | 值 |
|---|---|
| 名称 | Khronos SPIR-V 规范（Unified Specification, unified1） |
| URL | https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html |
| 不可变 revision | `unified1` 是滚动版本，**无 tag**；本轮**未**记录正文哈希，故不能当作可复现的不可变引用 |
| 许可证 | Khronos 规范许可（规范文本，非代码） |
| 支持范围 | SPIR-V 1.6 统一规范 |
| 是否真的取到 | **部分**：HTTP 200，但正文体积超出取回上限被截断，本轮**没有逐条引用**其中条文 |
| 本轮用途 | 仅作为 `OpTypeImage` 的 Image Format 操作数、`StorageImageExtendedFormats` 能力、「非聚合类型不得重复声明」三条规则的**规范来源指引**。**判定不依赖这份文本**：本轮的每一条结论都由 `spirv-val` 在真实 FFRS 模块上的实测给出（1353 条 capability 错误与 2 条重复类型错误的原文见覆盖报告 §4） |

| 项 | 值 |
|---|---|
| 名称 | Vulkan 规范 VUID（`vkCmdDispatch-OpImageWrite-04469`、`format-08795/08796`、`StandaloneSpirv-Image-04965` 等） |
| URL | https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html |
| 不可变 revision | Vulkan 1.3 扩展规范（滚动版本，无 tag） |
| 许可证 | Khronos 规范许可 |
| 是否真的取到 | **本轮否**：未重新抓取；沿用前序报告 `RG477M-TEXEL-20261112-RESULT.md` §2 已记录的引用 |
| 本轮用途 | 逐字节等价论证（写入字节、地址步长、分量数由绑定视图格式决定）的规范依据，未新增 VUID |

## 3. 社区做法

本轮**没有**引入新的第三方实现参考。P1 的改写规则来自三处：

1. 仓库既有实现（`src/graphics/nvn_vulkan/engine/xbg_spirv_typed_storage.h`、`xbg_spirv_stencil.h`）；
2. SPIR-V 规范对 `OpTypeImage` 的 Image Format 操作数与能力声明的要求（见 §2）；
3. Vulkan 规范对 typed storage image 的要求（见 §2，VUID 沿用前序报告）。

明确**未**做：没有引用任何模拟器、串流、其它游戏移植项目或其它游戏的演示作为兼容性证据；
没有用「搜索结果摘要」当作取到来源；没有修改任何外部项目源码。

## 4. 与本项目规范相关的一处事实

`AGENTS-HONOR.md` 在主仓 `/Users/yuyan9923/GitHub/ff-resonance-rg477m-native/` 根目录**不存在**
（`ls` / `find` 均为 ENOENT），主仓与 honor 仓本轮均不可动，故本轮唯一规范来源是
`AGENTS.md`。该缺失已记入覆盖报告 §9 与 §11。

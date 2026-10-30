# 验收用例集（RG477M native port）

本目录是 **验收口径的可执行定义**。用例由 QA 编写，覆盖
`docs/PROJECT.md` 的全部验收要求。**用例存在 ≠ 用例通过。**

- 覆盖矩阵与缺口：[`docs/analysis/acceptance_matrix.md`](../../docs/analysis/acceptance_matrix.md)
- 数据 schema：[`schema/`](schema/)
- T00 证据复核报告：[`coordination/reports/T00R-evidence-review.md`](../../coordination/reports/T00R-evidence-review.md)

---

## 1. 目录

| 文件 | 任务 | 用例数 | 当前整体状态 |
|---|---|---|---|
| `T03B-device-capability.md` | T03B 真机能力 | 10 | BLOCKED_DEVICE |
| `T02-native-sample.md` | T02 原生样例 | 8 | NOT_RUN（真机子项 BLOCKED_DEVICE） |
| `T04-title-screen.md` | T04 标题画面 | 7 | NOT_RUN（真机子项 BLOCKED_DEVICE） |
| `T05-playable-scenario.md` | T05 可操作场景 | 9 | NOT_RUN（真机子项 BLOCKED_DEVICE） |
| `T06-resolution-frametime.md` | T06 分辨率与帧时间 | 23（RS 9 + FT 14） | BLOCKED_DEVICE |
| **合计** | | **57** | **一条都未执行** |

---

## 2. 状态词表

用例状态只能取以下值（与 `schema/acceptance-evidence.schema.json` 的 `status` 一致）：

| 状态 | 含义 |
|---|---|
| `PASS` | 在真实目标平台上按口径执行并满足判定口径。必须附证据路径、构建身份、退出码。 |
| `FAIL` | 执行了但不满足判定口径。必须保留失败数据，不得改口径改绿。 |
| `NOT_RUN` | 从未执行（依赖未满足、或尚未排期）。**不是**通过，也不是失败。 |
| `BLOCKED_DEVICE` | 需要 RG477M 真机而当前无设备。 |
| `BLOCKED_DEPENDENCY` | 依赖的上游任务未通过（如 T04 未过则 T05 无法执行）。 |
| `BLOCKED_TOOLCHAIN` | 工具链缺失导致无法执行。 |
| `INCONCLUSIVE` | 执行了但证据不足以判定（如采集口径自检失败）。 |

**禁止**把 `NOT_RUN` / `BLOCKED_*` / `INCONCLUSIVE` 计入通过数。

---

## 3. 执行纪律（来自 `AGENTS.md` 与 `docs/PROJECT.md`）

1. **真机唯一性**：T03B/T04/T05/T06 的性能与画面结论必须来自 RG477M。
   模拟器、Mac、其他手机、空场景、菜单、系统刷新率、重复呈现帧**一律不算**。
2. **主机与真机分开记录**：`acceptance-evidence.json` 的 `environment.platform` 必须区分
   `host-macos-arm64` 与 `android-arm64-device`；主机 PASS 不等于真机 PASS。
3. **独立复跑**：实现者自测不能替代 QA 复跑；`executor.independent_of_implementer` 必须如实填写。
4. **口径先定后测**：帧时间的 loading 边界、分段规则、缓存状态必须在**运行前**写定并随数据保存；
   事后调整分段以改善结论 = 违规。
5. **阈值不可放宽**：PROJECT.md 的 29 FPS / P95 35 ms / P99 50 ms / >100 ms 每 5 分钟 ≤1 次，
   由用户决定是否调整，实现者与 QA 不得自行放宽。
6. **失败保留**：卡顿、错图、存档损坏、音频破音都必须逐次定位并保留原始数据。
7. **PASS 关联身份**：每个 PASS 关联 commit / dirty 状态、输入版本、构建 ID、测试条件、原始日志、退出码、覆盖范围。
8. **禁止恒真断言**：判定口径必须可计算，不得写成"看起来正常"。

---

## 4. 证据落盘位置

| 类型 | 路径 | 进 Git? |
|---|---|---|
| 设备能力 JSON | `evidence/device/<capture_id>/device-capability.json` | 是（脱敏后） |
| 帧时间原始数据 | `.local/runs/<run_id>/frametime.json` | **否**（大文件），Git 只留摘要与索引 |
| 帧时间统计 | `evidence/t06/perf/<run_id>/stats.json` | 是 |
| 截图 / 视频 | `evidence/t05/...`, `evidence/t06/...` | 是（挑选性） |
| 日志 | `evidence/<task>/.../logcat.txt` 等 | 是（脱敏后） |
| 运行记录 | `evidence/<task>/<case_id>/acceptance-evidence.json` | 是 |

`.local/` 已在 `.gitignore` 中，**不得**提交游戏本体、生成的游戏代码、密钥、固件、APK、存档或完整资源。

---

## 5. 已知需要随引擎版本修订的内容

T00 报告把引擎写作「UE 5.4」。T00R 独立复核发现二进制里的链接器模块标识串为

```
SDK MW+EpicGames+UnrealEngine-5_6_1        (main.bin 偏移 108,874,700)
```

即引擎基线为 **UE 5.6.1（直接证据）**。因此：

- 凡涉及引擎内部结构、反射名、序列化格式、渲染器/着色器版本的用例，
  **一律以 UE 5.6.1 为基线**；T02 样例卡与 T01 路线文档需同步修订。
- T00R 报告同时澄清了模块职责：`sdk` = Nintendo SDK 20.5.17 **NVN** 图形栈、
  `subsdk0` = Nintendo SDK 20.5.17 **movie**、`rtld` = Nintendo 运行时加载器；
  **游戏与 UE 代码只在 `main`**。T02 选样例只能从 `main` 选。
- 已在 `docs/analysis/acceptance_matrix.md` 中标出受此影响的条目。

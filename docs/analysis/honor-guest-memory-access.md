# 荣耀 GT Pro 上读取 guest 进程内存的取证方法（2026-11-11）

本文件记录一次真机取证的方法与结论，供后续角色名/实时内存结构攻关直接复用，避免重复走弯路。

## 1. 不用重装 App 就能读游戏进程内存

App 是 debuggable 构建，`run-as` 可用；而 `/proc/<pid>/mem` 的属主正是 App 自己的 uid，
因此 **adb shell + run-as 就能直接 dump 正在运行的 guest 进程内存**，无需安装新包、不打断游戏。

```bash
PID=$(adb -s A42BBB5109000333 shell pidof org.ffrs.gamewindow | tr -d '\r')
# 读取地址区间 [BASE, BASE+SIZE)，按 4K 对齐
adb -s A42BBB5109000333 shell "run-as org.ffrs.gamewindow sh -c \
  'dd if=/proc/$PID/mem of=chunk.bin bs=4096 skip=$((BASE>>12)) count=$((SIZE/4096))'"
adb -s A42BBB5109000333 shell "run-as org.ffrs.gamewindow cat chunk.bin" > chunk.bin
```

注意：`skip` 是 **4096 字节块序号**，不是字节偏移，算错会静默读出 0 字节。
进程布局看 `run-as org.ffrs.gamewindow cat /proc/<pid>/maps`（该文件全局可读，无需 run-as）。

## 2. 中文本地化表在 guest 内存的高地址区

- 命中示例：`0x639fa252a0`（拉斯韦尔）、`0x63bfb44720`、`0x63df8e9330`。
- 三个主要文本区：`0x639f800000-0x639fa65000`、`0x63bf800000-0x63c00d4000`、`0x63df800000-0x63dfb54000`。
- 存储形式：**UTF-16LE、NUL 结尾的字符串池**，ASCII 键与中文值混排在同一个字符池里
  （例如 `[VO_CHR0050_SILENCE][这里是……？]`）。键多为 `VO_*` / `SE_*` / `BGM_*`。
- 池中含**独立成条的角色名**（NPC 与主角混排，按源语顺序）：
  拉斯韦尔、雷因、杰科、塞壬、斯科特、菲娜、埃玛、莉德、榭拉、索菲亚、苏尔、维伦、艾尔……
  以及职业/地名类条目（反叛军士兵、陆行鸟商人、斗技场报名处等）。

## 3. 单位 id 表的结构（已确认）

在 `0x028d6eec` 附近是一张 16 字节一条的表：

```
struct { int32 unitId; int32 offset; uint64 arrayBase; uint32 flags; }
```

- unitId 连续（997、998、999、1000、1001 …），`offset` 每次 +4，`arrayBase = 0x027cbb10`，
  `flags = 0x10000019`。
- `arrayBase + offset` 处是一个 32 位值，形如 `0x279AAF8 + 16*(id-1001)`，指向一串 16 字节
  小对象；但这些对象内是引擎内部字符串（JAPANESE 一类的资源/配置文本），**不是单位名**。

## 4. 已排除的取名路线

| 路线 | 结论 |
|---|---|
| 存档明文 | 无名字（UTF-8 / UTF-16 均 0 命中） |
| guest 内存按 id 邻近取名 | id 表附近没有名字字段 |
| 本地化键值配对反查 | 字符池不严格交替，键与值无稳定配对关系 |
| 内存 ASCII 搜 `1001` | 只命中自家存档 |
| 内存搜 `DT_UnitParameter` | 0 命中 |
| pak 索引 | `DT_UnitParameter.uasset` 不在 pak，在 ucas |
| pak 数据块 | Oodle 压缩，zlib 探针无果，无解码器不可用 |

**结论：unit id → 角色名的映射只存在于 IoStore 的 `.ucas` 内（Oodle 压缩的 uasset 数据表），
或者在游戏自己解析 DataTable 后构造的运行时结构里——但当前运行态没有把名字挂在 id 旁边。**

## 5. 下一步可选路径

1. 让玩家在状态界面念出队伍 8 人的名字（存档名册顺序 1001→1008 推测与队伍顺序一致），
   直接写进内置 `UnitNames.java`，金手指立刻能显示名字。
2. 解 Oodle（Kraken/Mermaid）后离线解析 `DT_UnitParameter`，得到权威 id→名 表。
3. 若游戏把 DataTable 的 `FText` 键（形如 `UNIT_1001`）留在内存，扩大扫描范围找该键。

## 6. 本轮可复用的产物

- `tools/save/build_unit_names.py`：dump → `unit-names.txt` / `UnitNames.java`。
- App 运行时读 app files 目录下的 `unit-names.txt`（可免重编译刷新），内置表为回落。

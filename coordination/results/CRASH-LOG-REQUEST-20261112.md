# 崩溃日志索取清单（可直接转发给测试者）

生成时间：2026-11-12　｜　对应工程：ff-resonance-rg477m-native　｜　目标版本：1.0.7 内部包

---

## A. 发给「三季人」——神殿 / 大地图闪退（荣耀平板）

原文照发即可：

> 你好，收到你的反馈了——进幻兵水晶神殿大概率闪退、每次加载大地图偶尔闪退，这两条我们已经在查了。
>
> 想请你帮个忙，把崩溃当时的日志抓一份给我，这样我们能精确定位到是哪一步出的问题：
>
> **1. 抓崩溃栈**（手机连着电脑时，在电脑上执行）：
> ```
> adb logcat -b crash -d > crash.txt
> ```
> 然后把 `crash.txt` 发我就行。
>
> **2. 抓引擎自己的记录**（如果存在）：
> 手机上的 `/sdcard/Android/data/org.ffrs.gamewindow/files/engine.log.fatal`
> ```
> adb pull /sdcard/Android/data/org.ffrs.gamewindow/files/engine.log.fatal
> ```
>
> **3. 顺手确认三件事**（回我一句话就行）：
> - 崩的时候是**刚进神殿那一下**，还是**进神殿之后打第一场**的时候？
> - 神殿是**每次都崩**，还是**有时候崩**？大概几次里崩一次？
> - 崩之前有没有**黑屏、花屏、或者画面卡住不动**？
>
> 不用重装任何东西，也不用改设置。如果 adb 不方便，直接把手机上的
> `/sdcard/Android/data/org.ffrs.gamewindow/files/` 整个目录打包发我也行。
>
> 另外**大地图卡顿**那条我们也在改：正在查是不是每次进大地图都在重新加载着色器/管线。
> 1.0.9 会带上修复，到时候再请你验一次。

---

## B. 发给 8E5 手机那位报告人

> 你好，收到你说的巴哈姆特召唤兽击败对手后闪退了。
>
> 我们这边手边没有这台设备，只能先从代码上做预防性加固。要想确认到底是哪一处出的问题，
> 麻烦你帮我抓一下崩溃现场：
>
> **1. 崩溃类型**（最重要）：是闪退（秒退）、黑屏卡死、还是提示「应用无响应」？
>
> **2. 崩溃栈**：
> ```
> adb logcat -b crash -d > crash.txt
> ```
>
> **3. 引擎记录**：
> ```
> adb pull /sdcard/Android/data/org.ffrs.gamewindow/files/engine.log.fatal
> ```
>
> **4. 复现步骤**：哪只召唤兽？第几回合？是否**每次必现**？是否只在战斗结算那一下？
>
> **5. 版本**：当时装的是哪个包（版本号 / 从哪儿拿的）。
>
> 有了 2 和 3 我们基本就能定位；只有 4 和 5 的话，我们只能做推测性的加固。

---

## C. 为什么这些日志不可替代（给总控/QA 存档）

工程规则要求：**没有设备就不写设备 PASS**。8E5 那条目前只能写 NOT_RUN + 代码级加固；
平板这两条（神殿/大地图）则**有机会在真机上复现**，拿到崩溃栈就能把审计里 P0-1
（FinalizeEvent × WaitEvent UAF，触发条件含场景卸载）从「推测」升级为「确诊」。

日志归属与处理纪律：设备日志会被后续会话覆盖，取证必须对齐 pid 与采集时刻；
跨会话的行不得混用。

已入库位置：`coordination/state.json` → `user_field_report_20261112`
---

## D. 1.0.9 装上之后，同一条自查命令会看到什么（请连同这段一起转给测试者）

旧包（1.0.7）崩的时候，日志里是这行：

```
[multiwait] refusing Event finalization during active wait
```

这行**之后进程就被杀了**（SIGABRT）——这就是神殿/大地图闪退的原因：切场景时旧场景的 Event 要被终结，
但还有人在等它，宿主直接 abort。

1.0.9 装上以后，同样情况下**不会再崩**，日志里会变成这行：

```
[hle] nn::os::FinalizeEvent: event=0x... retired (multiwait_waiting=1 multiwait_referenced=1)
```

意思是「竞态照样发生了，但这次被安全处理了：等待者被释放，进程活着」。**这一行在新包里出现是好事，不是坏消息。**

所以自查命令不变：

```
adb shell "grep -a -n multiwait /sdcard/Android/data/org.ffrs.gamewindow/files/guest_boot.log"
```

另外 1.0.9 还会**多打一条**这行，说明「已终结的事件被再次使用，宿主拒绝了」——这属于防崩保护被触发的计数：

```
[hle] nn::os::Event: event=0x... was finalized; use refused (count=N)
```

如果这一行的 count 很大，说明游戏在频繁地复用已终结的事件，值得回来说一声。

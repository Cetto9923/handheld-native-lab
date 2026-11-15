# 页映射空洞路径优化与28候选

## 实际依据

输入源码c6ba89d。26录屏frame30214的201ms无编译长帧已取得50个CPU独占样本，其中guest_pages_map与nso_host_owned_pages_map各5个。保全26 APK中的指令确认，2e42fc/2e4310是allocation重叠检查链表循环，2df8bc是accessible插入位置的链表循环。该样本量只定位正在执行的路径，无法分配整帧墙钟占比。

26的最高地址和末尾提示可加速纯追加，但已有高地址映射时，逐步填入其下方的连续空洞仍会回到完整扫描。28针对此路径扩展提示，未改游戏逻辑时间和图形输出。

## 实现与恢复条件

guest_pages_map在原owner锁内记住最近一次成功发布allocation之后、下一个已有allocation之前的已验证空区间。后续请求完整落入该区间时可以直接沿原配额、VM和提交路线，发布后缩小该区间。请求越界、重叠或地址回退仍走原完整扫描，扫描同时找到新的上边界。

free/split只会减少原已占用范围，旧空区间继续安全。任何新映射都会在原锁内发布新边界。未发布allocation的失败不会更改提示，保留的异常allocation也沿原占位规则排除自身范围，quarantine依然拒绝新映射。

原始address_region将实际末尾提示扩展为最后一次成功发布的accessible节点。请求位于该节点之后时，从其next开始沿原有序链表搜索。任何free入口均先撤销游标，避免删除和分割留下悬空指针。失败映射不会发布新游标。其余保护粒度、邻页保留、零化、账本和回滚路径保持原调用次序。

## 组件与契约

原源码保存在page-gap28-20261115/baseline-*。同一真实guest_pages/Context/VM组件在6个区域先建立高地址末页，再按递增地址各填4096页，共计24576次计时映射。每轮结束核对原账本、实际后备映射及完整回收。

| 环境 | 原26/27实现三轮毫秒 | 空洞提示三轮毫秒 |
| --- | --- | --- |
| Mac 16KiB页 | 766.574、748.469、767.532 | 21.423、8.852、8.331 |
| RG477M 4KiB页 | 783.740、848.469、831.589 | 32.994、31.301、33.135 |

执行次序为before/after/after/before/before/after。RG游戏仍在世界地图，组件为独立进程，未发送游戏按键。RG中位831.589至32.994ms，所有ledger_valid及cleanup为1。该结果仅是指定空洞填充循环收益，不能换算战斗FPS，也未证明201ms整帧已解决。

guest_pages宿主正常及8类恢复检查通过，恢复553项、errors0，最终pending/charged/tickets/raw_regions均0。原始owned_pages正常与4类恢复在宿主ASan/UBSan通过，既有8线程各32轮、32释放竞态及16KiB主机子页保护回归保留。

新增回归覆盖先映射最高末页、再递增填低地址洞，跨洞上边界与既有页重叠拒绝，倒序插入后继续向前填洞，释放/分割/重用及最终账本。RG正常guest_pages实跑293项、errors0，原始owned_pages的8线程×32轮及32释放竞态通过。当前全部为root自测，没有独立Agent复跑。

## Android游戏分类

源码Manifest此前未声明游戏分类。27设备命令cmd game list-modes org.ffrs.gamewindow实际返回unsupported及空可用模式列表。Android官方GameManager接口规定，未标记为游戏的应用返回GAME_MODE_UNSUPPORTED，官方接入文档要求application声明appCategory="game"。28补上该类别，装后再核对系统识别。此变更本身尚无帧率收益证据，当前未强制切换系统游戏模式。资料于2026-11-15获取，使用公开Manifest属性，未引入上游实现源码。[Android官方Game Mode接入说明](https://developer.android.com/games/optimize/adpf/gamemode/gamemode-api) [GameManager接口](https://developer.android.com/reference/android/app/GameManager)

## 种子与待验

27实战后的有效学习表1462549字节，SHA256为6186c5f39f6cb5beffbefb4c7125b06b2edf73ce525cc38453da8ccab8f3bad4。原inspector核对1400条唯一recipe，1291 graphics、109 compute，1345个source shader key均存在于授权archive索引。28APK补充表使用这份实际学习结果，保留全部历史包及原学习数据。

28已完成103项宿主检查及Android构建，退出0，50份输入摘要前后一致。APK为ffrs-1.0.28-preview-page-gap.apk、3699783字节，SHA256为987ab1eea7fad4a45735d83f7d16785dec4e68422f267532dbcd1e3fac56f2a1。首次明镜与火炎剑仍有未覆盖的编译停顿，机械陆行鸟已覆盖但效果窗口仍约23.48FPS，持续GPU和shader加载开销继续开放。用户已明确授权今后测试包直接安装，后续直接保全当次数据、覆盖安装及真机验证。

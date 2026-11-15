# 荣耀平板测试数据清理与成熟实现复核

## 本次结果

用户授权清理平板测试数据和每次启动产生的冗余存档目录。设备仅 A42BBB5109000333，安装包为 1.0.37-preview，源码508ec73。root先停止本应用，以普通日志记录全部精确路径和操作，再归档、检查和清理。

已移除47个 Download/FFRS-save-* 旧自动导出目录、8个过期私有回归存档目录、9个私有采样文件，以及外部206个测试文件和22个抓帧/采样目录。三组共8165份文件、3591115728字节。磁盘空闲从125216056 KiB变为128768848 KiB，释放3552792 KiB，约3.39 GiB。外部files由约3.64 GiB降至422684 KiB。

8个私有存档目录来自我们显式的逐轮回归操作。保留单一完整测试目录files/shield-regression-202611153461，后续继续使用同一目录。47个Download目录来自旧版自动导出行为。29版已移除后台自动导出，本次37版真实Home/恢复后Download目录仍为0，私有回归目录仍为1。

正式codex-local-save、手动ffrs-quicksave、shaders和shrine-light-index-override的所有文件大小、权限、uid/gid和mtime前后逐项相同。有效vk-pipeline-cache.bin的SHA256前后相同，为139807a6ecb0a6114b1998416d9e7f5783aacab0ec5c6945c7ace2c97a52304c。保留测试目录9文件归档前后逐份SHA256一致，未恢复旧进度。源正式存档中历史mode600文件的内容读取限制仍按L023处理，本轮正式目录仅作元数据核对。

清理后复用保留目录启动37，PID21784，从真实Continue读入土之神殿入口，截图与引擎日志已保存。Home/恢复后进程相同，目录数未增长。上述为实现者自测，独立QA未运行。本轮清理不构成帧率收益验证。

## 可恢复备份

本地目录为 `.local/honor-stable30-20261115/cleanup/`。三份tar.gz总计1340236431字节，约1.25 GiB，全部留在电脑。

| 归档 | 文件数 | 压缩字节 | SHA256 |
|---|---|---|---|
| private | 72 | 20526862 | `3d039a03c6ea7e22108b68ac0069e96c0cbfc9bc02990c0fde5f67484fdf4d21` |
| downloads | 268 | 7163008 | `64eb486fe35229ecf06db51074052a15e02e12fe41eb7779840010b000752401` |
| external | 7825 | 1312546561 | `5510e221569164a83b207693e305fee822f983286b48d2485da6d3824e6392e9` |

每份归档均检查远端tar退出码、成员完整可读、路径无越界、全部成员大小与原始清单相同，并计算每个成员SHA256。归档前后源文件元数据相同。清理前再次校验压缩包SHA256。明细在plan.json、*-manifest.json及cleanup-result.json，原始stdout/stderr分离保存。被清理数据可按相应归档及原base路径恢复，恢复需停本应用并逐项选择，避免覆盖后续进度。APK历史全部保留。

## 52.1 FPS来源与具体机制

固定DroidPlay revision a72a85c59cac54f3428827cfb72e1adf63b1521a的同款游戏1.0.1报告，在Odin2 SM8550/Adreno740/Android13上测试开场、野外、峡谷、前三场战斗及相邻剧情。约30秒窗口统计，加载和首分钟排除。chain6中位52.1FPS、最低窗口43.1FPS，剧情60FPS。作者同时列出火焰块状、岩壁纹理条纹、音频underrun等限制。[固定游戏报告](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/dm/games/0100606026F12000/README.md)

最明确的单组优化为按buffer实际范围同步，并让dispatch结果继续被GPU读取。上游记录Odin2开场15.9升至40.6FPS，OnePlus Pad3为10.3升至41.0FPS。buf_fwd在范围被最新写入完整覆盖、偏移满足绑定对齐、产生端屏障及资源寿命均满足时复用GPU副本。own_alias使同一dispatch中相同内存的多个绑定共享写入副本，避免Niagara同一计数缓冲被分裂。UE5.6分配器和按驱动的shader兼容修正同样属于完整方案。[固定渲染说明](https://github.com/enjoyerofslop/droidplay-core/blob/a72a85c59cac54f3428827cfb72e1adf63b1521a/docs/reference/rendering.md#buffer-textures-synced-by-their-range-and-dispatch-outputs-read-on-the-gpu)

## 本项目具体差异与下一步

当前xbg_game.cpp的pending_view已覆盖GPU待写回范围，保持反向最新写入搜索、完整范围/对齐拒绝、跨槽寿命引用。storage和texel另有alias路径。36日志实际命中UNIFORM_TEXEL_PENDING_HIT，已有上游机制的一部分。当前uniform texel pending分支仍按内容版本复制到不可变快照，并有texel_copy_cache复用。36 controlled-north3普通统计窗口约3.15至3.35次复制/帧、1222至1230 KiB/帧、2.15至2.35次结束pass/帧，同时缓存命中约5.6至6.4次/帧。继续检查这些必要复制与可转发边界的差异，按L015保留写入内容版本和submission寿命，不能仅删掉vkCmdCopyBuffer就称已移植buf_fwd。

当前游戏BuildID736837afd390442ed7c74a7de730e71b与上游profile84e182f72ee1da4f477804bd05b54c15不同。上游模块地址和shader key需重新映射。现有36/37受控入口约29.36FPS、北移窗口26.51至29.86FPS，深处和技能仍未稳定30。下一轮优先按已有计数和CPU/GPU长帧定位GPU转发落空与descriptor CPU开销，再作同存档对照。ADPF已确认服务支持，接线与收益均未验证。

## 提交与并发范围

当前开发分支task/T-codex-recovery-20261114，全部已有源码已提交到508ec73。本报告及协调状态在本轮收尾提交。实时origin/main仍ecb9b57，尚未吸收本分支，删除条件未满足。独立金手指会话T-CHEAT-OPTIONS-20261115已登记其独立worktree写入范围，主树相同源码保持冻结，后续由root串行审查集成。

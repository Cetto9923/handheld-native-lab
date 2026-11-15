# CODEX-HONOR-SUBMIT44-20261116

## 输入与诊断

43源4a52571/记录ebccd39，APK e92965c5，Honor A42BBB5109000333 PID7643。42纯移动trace c4854f7e复核RHI25942在5.902619秒内CPU运行5320.17ms，睡眠509.45ms，Runnable24.56ms。43先采1b1f11a5遇敌混合，排除纯探索验收。逃走后重新采43纯移动833b63e1db6c61a07b0073564d648570aedca9dfe6293aef059930631340993b，前后截图均神殿内部桥段探索，3轮南北非零轴且最终归零。RHI7839运行5494.24ms，trace约5.91秒。栈含展开错误，inclusive项重叠，不能相加作exclusive占比。描述符构建、malloc和模板更新高频。普通FPS与Perfetto窗口分别处理。

原始证据.local/honor-submit44-20261116/，两个43 trace及lib43.symbols.so均保留。43内存快照PSS4622223KiB/RSS4733872KiB，属已运行较久探索阶段，后续候选需同路径与时长比较。

## 候选

Arena::destroy原先对所有VkBuffer销毁均增加全局memo_epoch，导致所有16槽惰性清空描述符缓存。44追踪缓存构建实际绑定的uniform/storage/C1/support/driver和驻留uniform-texel Buffer所引用槽。销毁时使这些槽失效并移除handle记录，进入槽的memo查找前正常unpin，front generation同步变化。无引用临时buffer不触发全槽重建。记录可能含已清缓存的保守引用，最多造成额外失效，不会漏掉实际引用。动态uniform命中仍刷新且核buffer身份，存储快照、texel generation、Image注册revision/identity及sampler全局epoch保留。现有8192 pinned上限及Arena内存回收规则保持。所有GPU销毁时机沿既有fence退役路径。

复用gfx-live-ab=memobuffer，保留原全buffer全局epoch作为同进程对照，两个分组都追踪引用以支持安全切换。PERF增加累计destroyed/referenced/slot_drops/tracked，采样不新增文件或UI。shader字节、NoContraction/FragDepth、guest时钟、音频配置不变。

生产函数提取编译ASan/UBSan覆盖无引用释放、跨槽引用含slot15、重复引用、handle复用、global sampler epoch、原路AB和8193边界及1000次释放循环，test_render_policy共6项通过。实际场景效果仍待验证。

与Khronos描述符缓存及动态offset建议核对，本次按现有实现缩小缓存失效范围，未复制示例代码。资料 https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html 。外部示例的帧率不作为本游戏性能证据。

## 待完成

宿主/Android全量构建、存档保全安装、同进程旧新旧普通移动及火焰群攻技能、显示/内存/冷暖与声音回归。用户补充最卡是当前位置遇敌后第一位主角的四字火焰群攻，名称待从实机菜单确认。发现PAD隐藏与位置变化后根停止输入，等待输入归属回复。独立QA NOT_RUN。验收必须完整包含coordination/prompts/_STANDING-audio-acceptance.md。

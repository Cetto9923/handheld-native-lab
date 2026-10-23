# RG477M 真机诊断检查点

用户确认连接并完成ADB授权，亲自两次按键/拨杆。本检查点不是游戏标题、可玩或性能验收。总控是唯一设备操作者；所有应用更新均 install -r 保留数据，没有卸载、清数据、root、SELinux修改、刷机或其他应用数据读取。

| 范围 | 实际结果 | 限制 |
|---|---|---|
| 身份 | Anbernic RG477M，Android14/API34，MT6897，arm64-v8a，nativebridge0，4KiB页 | 荣耀平板未连接 |
| APK | canonical v6 org.ffrs.diagnostic，451092B，SHA256 4b40d3896ac9d8f7cb9069df81dd4d30148d030dda3cbaf449ba9a9b02a74e0a；无申请权限；更新0、冷启动195ms | clean source fb7d870；35静态构建步骤0；诊断宿主不等于游戏 |
| 签名 / 数据 | 使用原交付目录本地开发签名，公开证书SHA256 c269a0fd0ccfb29665f6bc80f09a3090db9a6de079d3f2cf6186c3dbbbe72424。v4→v5→v6更新保留旧事件日志前缀与leaf摘要 | 未复制或读取私钥；不要安装其他worktree不同签名的包来覆盖 |
| 屏幕 | 1280×960窗口，1280×918内容，top inset42，280dpi，模式60/90/120Hz，当前120.000008Hz | 不是游戏FPS。游戏按真实内容区等比例fit/留边、不拉伸裁切。屏幕确为正常诊断JSON，Game startup and rendering: NOT_RUN |
| GPU查询 | Mali-G615 MC6，Vulkan1.3.247，ARM r44p1 | 功能查询不等于实际使用 |
| GPU实际操作 | 独立Vulkan1.0 core探针exit0；64KiB buffer copy、8×8 RGBA magenta clear/readback，零差异；31 VkResult0，12对象销毁/3unmap | 未测试shader、surface/present、descriptor indexing或游戏渲染；source f7ac445，frozen C SHA 2e11d78de212d9b1a6d974ba9a68a88474cf42506865a7d318d852950c457403 |
| 原始leaf | 真机16875比较0差异，无构造器；main+0x694bd0原始44B | 不是完整T02或游戏 |
| C++ ABI | 8线程80000次，构造1、abort竞争winner1、异常重试2、errors0，exit0 | 仅host compiler-runtime契约，guest NSO unwind未注册 |
| 内存页 | e112d65独立owned-pages正常契约exit0，4KiB，8线程×32，32释放竞争，mapped0/validtrue/errors0；62GiB仅虚拟预留，实际映射4096B | 未绑定SDK。受控VM失败只在主机测试，真机没有故障注入。懒映射计量不是物理resident预算 |
| 严格CLI | production info0；nnMain/malloc QueryMemoryInfo +0x27a50/+0x1398c exit133；CRT __nnmusl_init_dso +0x5920a34/133；startup AllocateTlsSlot +0x133a0/133，无超时 | 独立调用定位阻塞，完整CRT和游戏启动未完成 |
| APK JNI | main.nso125577976B，651imports、4612RELA、1370974RELR，LOADED_DIAGNOSTIC_ONLY | NULLresolver，只load_and_relocate，不调用imports/constructors/entry |
| 输入能力 | v5公开InputDevice枚举完整，1逻辑gamepad id5 retrogame_joypad；左X/Y、右Z/RZ、HAT及触发轴能力 | 能力表不是物理操作或游戏映射验收；raw-node旧探针空列表误报已修 |
| 实际物理事件 | 首轮45key/47motion，方向/B/BACK/左X/Y[-1,1]。补测44条：26motion/18key，右Z/RZ均[-1,1]，L2键8、R2键10事件 | 本次LTRIGGER/RTRIGGER/BRAKE/GAS均0，只证明L2/R2数字事件；B/BACK会退出诊断Activity。游戏映射NOT_RUN |
| 显式音频 | 0.25秒、PCM16 mono、peak327；AudioManager实际48000Hz/256frames，route speaker type2；12000frames写入/播放头推进，246ms，stop/release正常、无错误；音量7/15未改 | 只是诊断音。人回复“刚才没注意”，audibility未确认；游戏音频NOT_RUN |
| 文件 / 休眠 | app-private临时文件与生命周期已有证据 | 游戏存读档与休眠恢复NOT_RUN |

原始证据均在协调工作树 `.local/device/rg477m-session1/`：identity-before-install.json；install-v6-plan.json / install-v6-result.json / v6-data-preservation.json；audio-v6.json / audio-v6.png；manual-right-v6-summary.json / events-after-right-test-v6.jsonl；vulkan-execution.json / vulkan-result.json / vulkan-validator.json；owned-pages-execution.json；leaf-result.json；runtime-execution.json；diagnostic-module-loaded.json；diagnostic-v5.json。用户反馈“页面一串代码”时实际截屏 current-screen-v5.png，v6音频后截图 audio-v6.png，同为正常结果文本。

Canonical APK位于原交付目录 `.local/artifacts/android/ffrs-diagnostic-debug.apk`；重建会改变摘要，安装前必须重新核对build-evidence。当前证据accepted_static_build=true，源码指纹稳定，完整NDK归档校验仍NOT_VERIFIED。

| 单独产物 | Bytes | SHA256 |
|---|---:|---|
| production HLE CLI source8b817b6 | 558000 | 5cf82020353e061f3c2cef4fd2dd4cb417dbb3eb883460ecd688bb1b1ff89853 |
| C++guard sourcea22d0dc | 542984 | 0b627417417bf09595e7b869f0501efd26087a2144c0aa15a53fc02c77bec14c |
| Vulkan操作ELF | 174856 | 6e66aced9f0db6e67db754a2122be75febd51173d005515a831f9c60c36869b8 |
| owned-pages e112d65 ELF | 89936 | d95b592eedbcd35b9c30283a529222860f38f232e81f8dc0d5a96e697ea45a94 |

主模块SHA256 6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5，未改变。shell诊断目录为新建 `/data/local/tmp/ffrs-rg477m-efd1123620814764ae32529c18081259`；安装/复制均摘要回读，模块独占创建并只读。adb exec-out的远端错误可能返回0，证据需解析JSON/长度/摘要，不能只看宿主退出码。

T03B仍in_progress：部分所需GPU操作、听感、完整键轴、休眠仍未全部验收；游戏标题、可玩、30FPS/45分钟仍未通过。

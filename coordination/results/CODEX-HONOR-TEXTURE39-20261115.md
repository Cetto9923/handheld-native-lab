# 神殿内部纹理注册表查询候选

## 当次热点

38 PID24174，在用户要求的神殿内部水晶断桥机位，关闭ADPF后另采6帧7412至7417。6402 draw、402 dispatch、54提交，descriptor exclusive约19.89ms/帧，image_lookup约6.06ms/帧，snapshot约3.58ms/帧，coverage=OK。普通AB与诊断窗口分别保留。34的CPU栈也记录RegisteredTexture的std::map lower_bound。

## 本轮修改

39从撤回ADPF后的a93b211起步。仅在现有纹理注册表节点查询前添加2048项直接映射缓存，表地址及descriptor ID全部匹配才返回已有节点指针。记录原地更新保留最新texture/view/revision，已有revision及Image身份校验仍执行。碰撞和未命中沿原std::map查找，空结果仍触发原有未注册错误。注册表节点直到shutdown保持稳定，清表后同步清空front。复用gfx-live-set texrecord做同进程AB，普通PERF记录命中数。此处保留所有shader、buffer内容、同步、画面比例与游戏时钟语义。

生产提取ASan/UBSan组件检查覆盖未注册后新增、重复命中、原地更新、表切换、ID碰撞、开关回退、清表重建及生产shutdown接线。全量host、Android和内部同机位AB待执行。恢复点a93b211，APK37与实验38历史均保留。独立QA未运行。

## 39构建与内部对照

9462bf2，39 APK0c6d3909f6bd534538be5a883a4483c61548ea751a0a3519fb45f4eb213c8f1c/3720407B，111宿主及Android0，75源码输入一致。native df42d5ce4225072b5ed11de2590d5eabdba13d30884a4097311b8972f794610a，已确认移除38实验字符串。设备回读APK、私有全部9文件SHA和正式目录元数据通过。PID28432从唯一461副本Continue并实际前进至神殿内部水晶断桥。

同进程四轮25秒关闭16.3403FPS、开启15.0508FPS、关闭15.1161FPS、开启18.6972FPS。P99分别106.91/115.97/115.85/111.46ms，零present错误。两对结果方向不同，帧率收益尚未确认。普通计数已命中约1625次/帧、miss约291次/帧，功能入口生效。组件等价与帧率验收分别记录。

38深处10秒CPU栈另发现遗留gfx-codex-probe=0使probe.waiting=true。__openat的已回溯调用链包括probe_guest_barrier、probe_image_read_barrier及probe_present，原本以为普通窗口中的纯观测入口仍有真实外部文件I/O。本轮39四轮均保持相同标记条件，可作缓存单变量对照，但该测试条件须显式列出，不能直接泛化到无待命标记的用户安装。后续40将按帧缓存已有标记读取，清理待命状态后另验真实普通帧率。

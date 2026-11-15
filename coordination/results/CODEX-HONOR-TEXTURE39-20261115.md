# 神殿内部纹理注册表查询候选

## 当次热点

38 PID24174，在用户要求的神殿内部水晶断桥机位，关闭ADPF后另采6帧7412至7417。6402 draw、402 dispatch、54提交，descriptor exclusive约19.89ms/帧，image_lookup约6.06ms/帧，snapshot约3.58ms/帧，coverage=OK。普通AB与诊断窗口分别保留。34的CPU栈也记录RegisteredTexture的std::map lower_bound。

## 本轮修改

39从撤回ADPF后的a93b211起步。仅在现有纹理注册表节点查询前添加2048项直接映射缓存，表地址及descriptor ID全部匹配才返回已有节点指针。记录原地更新保留最新texture/view/revision，已有revision及Image身份校验仍执行。碰撞和未命中沿原std::map查找，空结果仍触发原有未注册错误。注册表节点直到shutdown保持稳定，清表后同步清空front。复用gfx-live-set texrecord做同进程AB，普通PERF记录命中数。此处保留所有shader、buffer内容、同步、画面比例与游戏时钟语义。

生产提取ASan/UBSan组件检查覆盖未注册后新增、重复命中、原地更新、表切换、ID碰撞、开关回退、清表重建及生产shutdown接线。全量host、Android和内部同机位AB待执行。恢复点a93b211，APK37与实验38历史均保留。独立QA未运行。

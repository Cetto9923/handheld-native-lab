# 工单 P-AG-3｜Antigravity｜主路径：nn::os::CreateThread + 越过 nnMain 首阻

你是实现 Agent。上一单 P-AG-2 已闭环：HEAD `5245f3e`，真机全量 **1890/1890** 构造函数连跑通过，已进入 `nnMain`，卡在 CreateThread。

## 仓库与约束
- 根路径：`/Users/yuyan9923/GitHub/ff-resonance-rg477m-native`，分支 `work/rg477m-native`，工作区当前应 clean
- 先读：`AGENTS.md`、`coordination/HANDOFF.md`、`coordination/state.json`、本工单
- 真机：`adb -s RG477M01445122`；先 `adb devices`；不 root/刷机/清数据；不 push 远端
- 保护 WIP：禁止 reset/clean/stash 丢掉他人未提交；本仓 clean 时可正常提交检查点
- 密钥/大二进制只留 `.local/`；原始日志与真实退出码必须保留
- 验收不降级：不以空窗口/模拟器/函数冒烟冒充可玩/标题

## 当前阻塞（证据）
- `[boot-step3] constructors completed successfully! total finished=1890`
- `[boot-step4] calling nnMain at offset 0x352f160...`
- `[hle] nn::util::ReferSymbol(...): verified SDK link anchor`
- `[unresolved-import] index=126 name=_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi caller_offset=0x352f1b8`
- exit **133**；日志：`.local/logs/rg477m-boot-v17-tp.log` + `.exitcode`（约 02:11 CST）
- CreateThread 实参线索（日志寄存器）：x0=ThreadType*，x1=entry，x2=arg(0)，x3=stack，x4=0x100000(stack size?)，x5=0x10(priority?)

## 目标（按序）
1. 落地最小可用 `nn::os::CreateThread`（及紧随其后必现的 StartThread / 相关 OS 线程 API）HLE：真实宿主 pthread 或已有 guest 线程基础设施；**每个新线程必须有独立 guest TP/TLS 镜像**（复用 `guest_tp`，禁止写宿主 Bionic TPIDR）
2. 共享 GOT / 导入表在子线程上仍能正确分派；join/析构/失败路径不破坏主线程
3. 宿主契约测试能覆盖 CreateThread 最小路径；RG477M 复跑 `--boot`（沿用既有 `--guest-resource-quota 16777216` 等已验证参数），日志证明越过 CreateThread；能推则继续推下一未绑定 import，朝标题/首帧前进
4. 更新 `coordination/HANDOFF.md` + `state.json`，跑 `python3 scripts/project_status.py --write`
5. 本地提交检查点；回报：HEAD、是否越过 CreateThread、新阻塞符号、日志路径、exitcode

## 明确未完成（勿伪称完成）
- `ProtectRelro` 仍是桩，未真 mprotect
- 尚无标题画面 / 可操作场景证据
- NVN→Vulkan 完整桥接、RomFS/IoStore 运行时挂载未闭环

## 禁止
- 不 reset/clean/stash 覆盖；不 push；不清数据
- 不声称标题/可玩，除非真机日志或截图证明
- 不要只塞空桩返回 0 放行后立刻崩在不可诊断处；失败须留可归因日志

做完停下汇报，等项目经理下一派工。

# 1.0.71 GitHub发布记录

用户本轮明确授权发布APK。已发布至 https://github.com/Cetto9923/handheld-native-lab/releases/tag/v1.0.71 ，普通Release，设为最新版。

标签v1.0.71精确指向构建源码b7a08b3ed5a30ab41593539c328957e10e29cc0c。推送引用为refs/tags/v1.0.71，发布前后Main引用均核对为ecb9b57fe90d5a5d24299980095bc49953c26aaa。本轮外来指南工作区修改继续由原写者持有。

## 附件与说明

- FFRS-1.0.71-universal.apk，3777820字节，SHA256 bd27d46e82ea4e79a52e98f677faee986da0922f06b03f6b9bef89cdb82cbbe9。
- FFRS-1.0.71-universal.apk.sha256。
- FFRS-1.0.71-release-notes-zh-CN.md，正文与统一发布目录中文更新说明一致。

发布正文包含小帧率显示、两列菜单、五项玩法金手指、R3恢复操作、保存及重启生效步骤、旧版数据使用范围、113项宿主检查和真机自测范围。战斗收益完整对照、其它机型、独立QA及蓝牙音频的待验项已披露，帧率长帧问题继续跟进。

## 核验

先上传草稿并下载核对三份附件，再发布，并再次下载三份公开附件。两轮均与本地发布文件逐字节一致。发布正文回读一致，远端标签的commit核对一致，latest接口确认v1.0.71，isDraft及isPrerelease均为false。最终状态PUBLISHED_VERIFIED。

命令、进程PID、退出码、普通日志、远端元数据、下载内容与SHA索引位于.local/releases/1.0.71/publication/。统一发布目录保留在/Users/yuyan9923/GitHub/游戏发布包/02-最终幻想_共鸣_FFRS/，本地artifact索引已记录GitHub URL和核验时间。

初次只读查询使用了当前gh release list版本不支持的url字段，按工具返回字段清单改正后成功。无现有v1.0.71标签和源码commit的404结果作为发布前状态记录，后续创建和推送成功。

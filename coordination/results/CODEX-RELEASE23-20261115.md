# 1.0.23预览版Git发布

用户明确要求“先发布当前版本”“GIT上发布，并给我包”。本次授权涵盖既有GitHub仓库的当前版本源码推送、版本标签及APK Release附件上传。发布目标为既有公开仓库Cetto9923/handheld-native-lab，账号具备push权限。

发布标签使用v1.0.23-preview，与AndroidManifest的1.0.23-preview及versionCode23对应。标签指向实际构建源码dd76193d9fab006e7318737b2eecf54250b933d8，38份源码输入均以该提交blob重算摘要，与构建输入一致。当前开发分支用于发布源码与清理后的工程记录。

APK沿用已构建、已安装到RG477M的同字节产物，原历史APK保留。交付文件为.local/releases/1.0.23/FFRS-1.0.23-preview-20261115.apk，3,679,303字节，SHA256为845d1c4c57272a429edc918be71389b8ab25f4fe5c0b431d4fc335af20763fe3。旁置sha256文件便于下载校验。

101项宿主检查、Android构建及签名验证通过。真机已验证覆盖安装、补充seed摘要、union1162及warm失败0。持续GPU技能卡顿、声音短卡、个别转场长停顿、整体提帧收益与正常保存回归仍保留待测。用户反馈设备没电，充电后再继续真机验证。

发布说明按阶段预览版描述上述范围。发布计划、说明文本、上传和下载复核记录保存在.local/releases/1.0.23/publication/，独立公开内容审计保存在同级audit/。

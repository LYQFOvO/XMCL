# KAMUCL 功能迁入 XMCL

用户于 2026-10-25 确认：原生 C++ 移植，按模块分批实现和验证全部功能；保留当前 QML 主界面、圆角和液态玻璃效果。新增功能通过现有入口打开同风格面板。

## 功能范围与入口

| README 模块 | 原生服务 | 现有入口 |
|---|---|---|
| 游戏实例 | InstanceService：创建、删除、重命名、目录隔离、实例级 Java、内存（智能分配）与启动参数 | 实例卡片、实例设置 |
| 版本安装 | GameService：原版/Fabric/Forge/NeoForge/Quilt 清单、安装、校验与启动 | 探索新版本、开始游戏 |
| 账号中心 | AccountService：微软设备码、离线、Yggdrasil、安全保存 | 右上角账号 |
| 资源管理 | ResourceService：模组、资源包、光影、世界、服务器 | 实例管理面板 |
| 实例管理中心 | InstanceService：复制、备份、恢复、诊断 | 实例管理面板 |
| 社区资源 | CommunityService：Modrinth/CurseForge 搜索、版本、依赖、下载导入 | 下载入口 |
| 皮肤衣柜 | AccountService / SkinPreview：角色预览、皮肤、披风、历史 | 账号面板 |
| 联机 | ConnectivityService：FRP、VoxLink、Terracotta、直连 | 实例管理面板 |
| MOD 面板 | IntegrationService：Bridge 安装、清单、修改与重置 | 实例管理面板 |
| 个性化 | IntegrationService：主题、背景、快捷键、插件、缩略图 | 设置入口 |
| 诊断 | MaintenanceService：任务、日志、启动诊断、更新与回滚 | 下载与设置入口 |

## 技术边界

- C++17 / Qt 6，网络使用 Qt Network，进程使用 QProcess，压缩包使用 libarchive（路径与符号链接校验），不引入 Node/Electron 主程序。
- 账号凭据进入平台安全存储；不在日志、QML 通用响应或配置明文中暴露令牌。
- 第三方网络服务需用户自己的凭据和配置；缺配置时报具体原因，不以模拟成功替代真实结果。例外是微软正版登录使用 KAMUCL 公开的内置公共客户端 ID（非密钥，可在面板覆盖为自己的应用），玩家无需自行注册 OAuth 应用。
- 保留 MIT 原创代码对应声明；VoxLink 等单独许可证在复用或移植时保留相应声明和源码要求。
- 桌面端 Bridge 通信为本机带鉴权接口；游戏端 Fabric MOD 作为独立 Java 插件。
- 所有下载验证所给摘要；玩家可在设置中选择官方源或 BMCLAPI 镜像（非默认），镜像只改写 Mojang/Fabric/Forge/NeoForge/authlib-injector 官方地址并在下载界面标明来源。备份、恢复、删除限定在实例根目录。
- 版本、Java、内存与启动参数按实例隔离；内存默认值由本机物理内存计算（为系统预留约 2 GB，取可用内存约 40%，限制 2–16 GB），玩家可在实例设置中智能分配或手动覆盖。
- 启动前检查客户端/库/资源与 Java 匹配，缺失或大小不符的文件可一键修复；启动失败后按日志证据给出诊断与设置入口，未识别的错误保留日志路径。更新仅面向 XMCL，不能指向 KAMUCL 发行版。
- 功能完成以代码、测试和验证记录为准；受外部服务限制的联调与尚未实现的部分单独记录。

## 实施状态

当前逐项进度、验证证据和明确缺口以 [STATUS.md](STATUS.md) 为准。基础服务与新增面板已接入，当前仍处于原生迁移阶段；VoxLink、自动更新替换、跨平台凭据等不得标记完成。

## 模块协作协议

继承 `FeatureService`，实现 `QVariantList actions() const` 与 `execute(action,args)`。只通过 `completed(action,data)`、`failed(action,message)`、`activity(message)` 返回结果。数据根目录来自构造函数。通用返回：`message`、`details`、`items`（id/title/subtitle/path 等）与可选 `previewPath`；禁止把凭据返回给通用 UI。操作描述包含 id、label、description、fields（key/label/type/default/required/options），支持 text/password/path/bool/number/choice；危险操作需 `confirm: true`。

实例路径：`<dataRoot>/instances/<uuid>/instance.json` 和 `game/`。实例元数据：id/name/version/loader/javaPath/jvmArgs/gameArgs/isolated。共享游戏目录 `<dataRoot>/minecraft`；共用库和资源位于该目录。账号模块公开仅供 C++ 调用的 `launchIdentity()`。

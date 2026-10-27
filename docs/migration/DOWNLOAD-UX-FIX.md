# 下载与下拉菜单修复

用户反馈：下载难以使用；下拉菜单文字与背景接近；正版账号无法登录；资源下载不了。继续沿用已确认的圆角/森林/玻璃布局。

## 根因

- 下拉菜单：`GlassComboBox` 的委托文字同时写了 `font: control.font` 与 `font.weight`，QML 加载时报
  “Property has already been assigned a value”，生产界面无法启动。即使修好加载，Basic 样式的弹层背景与
  文字都依赖 palette，深色面板上缺少完整的选中/悬停/禁用配色。
- 下载：下载入口只打开社区资源通用操作面板；安装要求先创建实例、手工切操作及填版本。安装元数据阶段不产生
  下载卡片，失败后也无原位重试。
- 账号：微软设备码登录要求用户自己注册 OAuth 应用并填写 Client ID，普通玩家无法完成登录。
- 资源下载：社区资源只能走 “搜索 → 手工切换查看文件 → 手工切换安装 → 手工填写实例 ID” 的高级面板流程，
  没有面向玩家的下载入口，失败后无法在原任务上重试。

## 修复

- `ui/GlassComboBox.qml` + `ui/Theme.qml`：统一下拉框的折叠文字、弹层底色、选中/悬停、禁用、焦点与滚动条
  配色；修复委托字体冲突导致的 QML 致命错误；弹层最大高度 420，可滚动查看更多操作。
- `ui/InstallDialog.qml`、`ui/DownloadsPage.qml`、`src/services/GameService.*`：现有下载按钮及实例页探索入口
  打开专用安装弹窗，自动加载官方版本、查询所选加载器版本、输入名称后自动创建独立实例并安装。下载页从元数据
  准备开始显示阶段、文件进度、错误和原位重试；重试按任务所属模块分发，不再固定发送给游戏模块。
- `src/services/AccountService.*`、`ui/FeaturePanel.qml`、`ui/Main.qml`：内置 KAMUCL 的公开 Microsoft 客户端
  身份，未登录时点击右上角账号按钮直接进入微软设备码登录；设备码单独大字号显示并附打开登录页按钮；授权链错误
  映射为可操作的中文提示；Minecraft 档案 UUID 统一为带连字符形式。
- `ui/CommunityDialog.qml`、`src/services/CommunityService.*`、`src/services/ServiceHub.*`：新增“下载模组与资源”
  弹窗，从下载页一键完成搜索 → 选项目 → 选文件 → 选实例 → 安装；自动带出当前实例的版本与加载器；社区任务显示
  阶段/进度/失败原因，支持 `community.retry` 原位重试；CurseForge 密钥可在弹窗内保存。
- `src/services/PackService.*`：整合包导入记录失败原因并支持 `pack.retry`，下载页同样按模块分发。

## 下载源选择（官方 / BMCLAPI 镜像）

- 设置页「游戏与下载」新增下载源开关，保存在个性化设置中并即时生效；默认官方源。
- `src/services/DownloadSource.h` 按 [BMCLAPI 官方文档](https://bmclapidoc.bangbang93.com/) 的“使用”章节实现地址改写：Mojang 元数据/对象、资源 objects、libraries、Fabric meta/maven、Forge/NeoForge maven、authlib-injector 走镜像；Quilt 与 Modrinth/CurseForge 内容保持官方源；Forge/NeoForge 安装器进程内部下载不受影响。
- 下载界面对应位置（设置页、下载管理页、游戏安装弹窗）标明当前来源，满足 BMCLAPI “需在下载界面或其他可视部分标明来源”的要求。
- 下载器与元数据请求增加 5xx/429/连接中断的有限退避重试（保留 `.part` 续传）；镜像不可用时自动回退官方源，并在同一次下载/安装内记住回退状态，避免逐个文件重复等待。

界面验证：`docs/screenshots/settings.png` 与 `docs/screenshots/download-source.png`。

## 验收

- CTest 12 组全部通过，其中 UI 测试从生产 QML 完成：下拉菜单折叠/展开/选中/悬停文字与背景对比度 ≥ 4.5:1、
  未登录账号入口直达微软登录、设备码提示、安装弹窗真实回环下载与失败重试、社区资源搜索/安装与下载页联动。
- 下载源：映射规则单元测试逐条覆盖；`XMCL_LIVE_MIRROR=1` 真实镜像版本清单、authlib-injector 与单文件下载通过；完整镜像模式安装实测完成（本机到部分镜像 CDN 节点被重置，自动回退官方源，468 个资产摘要全部校验）。
- 真实网络联调（`XMCL_LIVE=1`）：微软设备码申请、Mojang 官方版本清单、Modrinth 搜索/版本/文件下载通过；
  `XMCL_LIVE_OLD_INSTALL=1` 真实安装 Minecraft 1.0，客户端、库与 749 项旧版资源索引全部按摘要校验后发布。
- 验证截图：[下拉菜单](../screenshots/dropdown.png)、[微软设备码](../screenshots/accounts.png)、
  [社区下载完成](../screenshots/community.png)；测试运行时另输出到 `build/ui-verified/`（含安装弹窗、下载进度、失败重试）。

真实微软账号完成授权、所有加载器完整安装与公网联机仍需人工验收；不得用只打开面板替代下载验收。

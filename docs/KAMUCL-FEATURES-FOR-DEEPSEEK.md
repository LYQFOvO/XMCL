# KAMUCL 实用功能与 XMCL 原生实现交接说明

整理日期：2026-10-27。交接对象：DeepSeek。目标项目：`/Volumes/olo/XMCL`。

这份文档列出 **11 个主模块、54 项功能**，说明 KAMUCL 的实现思路、XMCL 当前可复用的代码、建议补齐方式与验收条件。用途是指导后续分批开发，本次只整理文档，没有修改应用代码。

## 1. 先读：范围、依据和不可破坏的现状

### 1.1 参考依据

- 已在线核对用户指定仓库的 [README 功能一览](https://github.com/YUN-SAN3/KAMUCL/blob/main/README.md) 和 [功能树](https://github.com/YUN-SAN3/KAMUCL/blob/main/docs/FEATURE_TREE.md)。README 包含游戏实例、版本安装、账号、资源、实例中心、社区、皮肤、联机、MOD 面板、个性化和诊断 11 个模块。
- 下文的源码机制主要依据本地只读参考：`build/reference/KAMUCL/`，提交 `30874ab871c8ee00c1edab275bb3aeb25704330b`。文末提供固定提交的源文件链接，便于定位。远程 `main` 的部分说明已经变化，不要把本地快照当成远程最新实现。
- XMCL 状态依据当前 `src/`、`ui/` 和 [迁移状态](migration/STATUS.md) 的静态阅读；本次没有重新构建、运行测试或使用真实账号验收。文中“已有”表示有实现可复用，不表示所有平台、版本和外部服务都已验证。
- “KAMUCL 做法”描述参考项目；“XMCL 实现”是针对 C++/Qt 的建议。两者有意分开，不能把建议接口当成已存在的接口。
- F01–F52 展开 README 的 11 个模块；F53–F54 是从功能树/源码找到的额外实用工具。

### 1.2 项目约束

1. 保留现在的首页、侧栏、森林背景、圆角和液态玻璃风格。新功能从现有实例、下载、账号和设置入口进入同风格弹窗或详情面板。
2. 保留 DeepSeek 已完成的 `InstallDialog.qml`、`CommunityDialog.qml`、`InstanceSettingsDialog.qml`、`GlassComboBox.qml`、`DownloadSource.h`、`MemoryAdvisor.h` 及其行为；先读代码，再增量补齐。
3. 继续 C++17、Qt 6.7+、QML、Qt Network、QProcess、libarchive。KAMUCL 的 TypeScript/Electron IPC 要改为原生服务和 Qt 信号，不引入 Electron 主程序。
4. 不要求玩家输入实例 UUID、内部 action 名、临时任务 token 或文件哈希。面板从当前实例和后台结果自动取得。
5. 稳定 UUID 是实例身份；显示名称可以修改。保留现有用户配置、存档、模组、账号和资源缓存。
6. 网络请求异步；大目录扫描、哈希、压缩、复制放到工作线程；QML 和 `QAbstractItemModel` 的更新回到 GUI 线程。不要把 `QNetworkAccessManager` 跨线程直接调用。
7. 真实完成标准：玩家可以从界面执行，后端确实改变了目标文件/进程/配置，错误可以解释和恢复。只打开通用面板、模拟进度或返回“成功”不算完成。
8. KAMUCL 参考目录中的发布流程不适用于 XMCL。本任务不授权向 KAMUCL 仓库推送、发布 Release，也不把 XMCL 更新源指向 KAMUCL 成品。

### 1.3 当前应复用的接口

| 文件 | 当前职责 | 后续使用原则 |
| --- | --- | --- |
| `src/services/FeatureService.h` | `actions()`、`execute(action,args)`；`completed/failed/activity` 信号 | 保持已有调用兼容；新增能力继续走原生服务 |
| `src/services/ServiceHub.*` | QML `features` 入口，聚合实例、账号、外观、下载任务 | 服务只返回适合 UI 展示的数据，凭据不进入结果 |
| `src/services/InstanceService.*` | 实例 CRUD、配置、复制、备份、恢复 | 新的实例面板复用这些操作 |
| `src/services/GameService.*` | 版本、加载器、安装、Java、启动 | 把较大新增逻辑拆成专用 helper，避免继续堆进一个函数 |
| `src/services/DownloadEngine.*` | 下载、`.part`、摘要、有限重试、官方回退 | 新模块复用下载器；不要各自写一份不校验的下载 |
| `src/services/DownloadSource.h` | 官方地址到 BMCLAPI 的映射 | 保留来源提示及不镜像的范围 |
| `src/services/CommunityService.*`、`PackService.*` | 社区资源与整合包 | 依赖、补文件、事务安装在这里扩展 |
| `src/services/ResourceService.*` | 本地资源、服务器文件等 | 模组/世界逻辑变大时按职责拆服务 |
| `src/services/AccountService.*`、`SecretStore.*` | 账号、授权、凭据和衣柜 | 网络令牌只能在 C++ 内部传递 |
| `src/services/ConnectivityService.*` | 直连基础能力、FRP/Terracotta 进程 | 各联机后端独立状态机，界面统一显示 |
| `src/services/BridgeService.*`、`bridge/` | 本机游戏参数桥接 | 保留独立 Fabric MOD 与鉴权 |
| `src/services/PersonalizationService.*`、`NativePluginManager.*` | 外观、快捷键、Qt 插件 | 不声明兼容 Electron 插件 |
| `src/services/MaintenanceService.*` | 诊断、日志、更新暂存、备份 | 更新替换 helper 尚需单独实现 |
| `ui/Theme.qml`、`GlassComboBox.qml` | 控件颜色、下拉弹层 | 所有新增选择器复用，不回退系统默认浅色弹层 |

**状态用词：**“已有，完善”=复用后补体验或边界；“部分已有”=底层有一部分但流程不完整；“新增”=当前主流程没有等价能力。优先级 P0 为核心可靠性，P1 为常用增强，P2 为较大或可后置功能。优先级是对 XMCL 的建议，不是 KAMUCL 的标签。

## 2. 建议开发顺序

| 批次 | 内容 | 功能编号 | 可交付结果 |
| --- | --- | --- | --- |
| A | Java 匹配、启动检查、启动诊断 | F09、F10、F13、F49 | 装完能启动；失败能定位 |
| B | 模组详情、批量管理、更新、前置与回滚 | F18–F23、F30 | 真正可日常使用的模组管理面板 |
| C | 整合包导入闭环、缺失文件补全、社区浏览 | F32–F36 | 下载包后能完成安装，失败可继续 |
| D | 文件夹、迁移、实例备份与存档 | F04、F05、F26、F29、F31 | 多目录与玩家数据可安全管理 |
| E | 服务器、游戏选项、快捷启动与衣柜 | F02、F07、F27、F28、F37–F39 | 减少重复配置和手工操作 |
| F | 下载任务扩展、账号平台适配、Bridge | F11、F12、F14–F17、F44、F45 | 长任务稳定、平台行为清晰 |
| G | 联机、更新分发、扩展工具 | F40–F43、F46–F48、F50–F54 | 分协议、分平台单独验收 |

F01、F03、F06、F08、F24、F25、F33 等已有能力或关联能力按相应批次补齐；不要为了顺序表重写已经工作的功能。第一轮建议只交付 A，然后 B，每批单独验证。

## 3. 游戏实例与启动会话

### F01 创建、删除、重命名实例｜P1｜已有，完善

- **用途：**每种玩法有独立名称、版本和配置，重命名不会丢失关联数据。
- **KAMUCL 做法：**`instances.ts` 管目录与实例元数据，`instanceCenter.ts` 校验目标及运行状态。[K01]
- **XMCL 实现：**继续用 `InstanceService`，实例卡片提供名称输入、打开目录、删除预览。删除前列出独立目录与受影响数据；共享库/资产不跟着删除。运行中的实例拒绝删除。把取消、失败、完成分开显示。
- **验收：**重名显示可区分；重命名后账号/服务器绑定不失效；删除一个实例后另一个仍能使用共享资源；删除确认的目标与最终删除 UUID 一致。

### F02 实例收藏、排序与快速选择｜P1｜新增完整管理

- **用途：**常玩的实例置顶，多个版本容易查找。
- **KAMUCL 做法：**功能树列出实例卡片收藏排序；具体入口在渲染层实例卡片与设置数据中。[K00]
- **XMCL 实现：**在 `instance.json` 或独立索引保存 `favorite`、`sortOrder`、`lastPlayedAt`。`LauncherController` 提供排序结果，支持收藏优先、最近使用、名称顺序。QML 只改变数据顺序，不改卡片结构；首次迁移保留原顺序。
- **验收：**重启后收藏与手动排序保持；删除实例能清理索引；选中状态按 UUID 保持，不依赖数组位置。

### F03 共享与隔离目录｜P1｜已有，完善

- **用途：**不同整合包的模组、设置和存档互不污染，同时复用大体积库和资产。
- **KAMUCL 做法：**`instances.ts` 与 `resourceDirectory.ts` 按实例隔离状态解析资源位置。[K01]
- **XMCL 实现：**集中提供游戏目录/资源目录解析函数，所有下载、资源、服务器、启动模块共用。已有 `isolated` 开关切换时预览目标、已有文件、是否复制玩家数据；不要仅改布尔值却让用户误以为原存档已经迁过去。
- **验收：**两个隔离实例的 mods/options/saves 分离；切换后启动器和游戏使用同一目录；共享目录不被独立实例清理操作破坏。

### F04 多个游戏文件夹与扫描导入｜P1｜部分已有

- **用途：**发现其他启动器或其他磁盘上的 `.minecraft`，减少重复下载。
- **KAMUCL 做法：**`gameFolders.ts` 登记、扫描、设默认目录；识别目录结构与安装版本。[K01]
- **XMCL 实现：**新增 `GameFolderService`，保存稳定 folderId、规范化路径、显示名和默认标记。扫描 `versions/*/*.json`、客户端 JAR、继承关系及玩家数据；返回候选清单让用户选导入。首次扫描只读，导入时生成 XMCL UUID 与来源映射。外置盘不在线显示“目录不可用”，不删配置。
- **验收：**路径有中文和空格可用；同一路径重复登记不产生双份；缺 JAR 或损坏 JSON 单独报错；默认目录变更不重定向已经开始的任务。

### F05 目录迁移与冲突预览｜P1｜部分已有

- **用途：**把游戏搬到大磁盘，操作前知道哪些文件会冲突。
- **KAMUCL 做法：**目录/实例模块区分源目录与目标，功能树提供扫描、迁移和冲突预览入口。[K01]
- **XMCL 实现：**在 `instance.migrate` 上增加只读计划阶段，输出新增、相同、冲突、不可读文件与预计容量。执行阶段复制到暂存目录，按大小/哈希复核，成功后才改引用；跨盘不能依赖一次 `rename`。默认保留源，删除源作为独立明确操作。
- **验收：**空间不足在提交前报错；断电/取消后源数据完整；同名不同内容不静默覆盖；预览之后源内容变化要求重新预览。

### F06 实例级 Java、内存和窗口参数｜P0｜已有，保留

- **用途：**老版本、原版和大型整合包使用不同运行配置。
- **KAMUCL 做法：**实例元数据叠加默认启动配置，启动时解析为 JVM/游戏参数。[K01][K02]
- **XMCL 实现：**保留 `InstanceSettingsDialog`、`MemoryAdvisor` 与现有 `instance.configure`。明确“继承全局默认”和“实例覆盖”的区别，建议用缺省字段/显式模式表达，不能把创建时复制的值一直宣称为动态继承。智能内存是建议值，不等于机器当前可用内存。
- **验收：**改 A 实例不影响 B；内存边界同时在 UI 和 C++ 校验；修改默认后只影响真正继承默认的实例；保存失败弹窗保留输入。

### F07 快速重启、结束游戏与运行会话｜P1｜部分已有

- **用途：**调整模组后快速重启，能看到游戏是否真正退出。
- **KAMUCL 做法：**`launch.ts`、`gameSession.ts` 跟踪进程和会话；提供结束、重启。[K02]
- **XMCL 实现：**从 `GameService` 的单个 QProcess 开始补会话 ID、instanceId、pid、startedAt、exitCode、logPath。重启先请求结束并等待退出，再重新准备启动；需要强制结束时单独提示。若后续支持多开，再引入会话映射；仅操作本应用创建并跟踪的进程。
- **验收：**重复点击不生成两个启动；取消重启不再启动；启动失败/正常退出/崩溃状态不同；不会根据进程名结束其他 Java 程序。

## 4. 版本、Java 与下载

### F08 原版、加载器与 Fabric API 安装｜P0｜已有，完善

- **用途：**选择 Minecraft 版本后，选兼容加载器及常见必要组件。
- **KAMUCL 做法：**`versions.ts` 读官方清单和继承链；`loaders.ts` 区分 Fabric/Quilt profile 与 Forge/NeoForge 安装流程，并提供 Fabric API 选择。[K03]
- **XMCL 实现：**保留 `InstallDialog`。补 Fabric API 为独立可选组件，按 Minecraft/loader 选择兼容版本，并经社区下载事务安装。旧版本、快照、预发布的分类读官方元数据，版本名不转成浮点数。每个加载器分别处理历史坐标和安装器输出，尤其 NeoForge 的早期版本。
- **验收：**至少各完成一次原版/Fabric/Quilt/Forge/NeoForge 实际安装与启动；安装器退出 0 后仍校验产物；没有兼容加载器/API 时禁用对应确认而非装最新版本。

### F09 Java 扫描、匹配、健康检查与自动下载｜P0｜部分已有

- **用途：**玩家不必自己寻找正确 Java；损坏或不完整运行时能提前发现。
- **KAMUCL 做法：**`java.ts` 扫描、缓存、手动添加/隐藏、按主版本及架构选择；`javaRuntimeHealth.ts` 检查游戏所需模块；缺运行时则从供应商下载并校验。[K04]
- **XMCL 实现：**新增 `JavaRuntimeService`。候选来自用户路径、PATH、JAVA_HOME 和系统安装位置；用带超时的 QProcess 探测版本/位数/架构并缓存文件指纹。优先使用 profile 的 `javaVersion.majorVersion`，未知版本不要猜成某个固定 Java。没有合适候选时显示推荐包、架构、大小和下载按钮，经 `DownloadEngine` 校验 SHA-256 后安全解压到托管 runtimes。保留实例手工选择。
- **兼容性：**KAMUCL 快照采用“优先目标主版本，否则更高版本”策略，但更高 Java 不保证兼容所有旧模组。XMCL 应优先精确匹配，版本回退依据可验证的规则，明确提示而非宣称普遍向上兼容。
- **验收：**错误架构、不完整 JRE、探测超时不会卡 UI；同一 runtime 的并发准备合并；坏摘要不发布；真实 macOS/Windows 分别验证。JRE 包若含合法内部符号链接需专用受限解压策略，不能关闭全局路径校验。

### F10 启动前完整性检查与一键修复｜P0｜部分已有

- **用途：**下载中断、文件被删或损坏时，自动找出要补的部分。
- **KAMUCL 做法：**`launch.ts`、`launchPreparation.ts` 协调启动准备；版本、库和资源元数据生成校验/下载任务，共享准备过程。[K02][K03]
- **XMCL 实现：**在启动前生成必需文件清单：客户端、libraries、natives、资源索引、assets、logging、loader profile、认证组件。对缺失/损坏项形成修复计划，复用下载器只补缺项；不要删除未知模组或用户文件。缓存校验结果必须与路径、大小、mtime 绑定，强校验入口仍可主动重算。
- **验收：**删除一个库只补该库；坏资产重新下载；离线且文件完整能继续准备；修复失败不发出“游戏已启动”；取消后迟到网络结果不提交实例。

### F11 下载队列、并发、速度、暂停与任务历史｜P1｜部分已有

- **用途：**下载很多小资源时更快，用户知道正在做什么、还能恢复什么。
- **KAMUCL 做法：**`download.ts` 支持批量并发、分段/断点、摘要、缓存复用；`tasks.ts` 管暂停继续取消；进度模块聚合速度。[K05]
- **XMCL 实现：**当前 `DownloadEngine` 是串行文件队列。先增加有上限的文件级并发和全局限流，再考虑大文件分段。任务统一 taskId、owner、phase、已校验文件数、传输字节、速度和错误。暂停保留 `.part`；重启恢复需保存非敏感任务描述并重新检查 URL/文件。未知总量显示不定进度，元数据、校验和加载器安装阶段不伪装成下载百分比。
- **验收：**多模块同时下载不超过总并发；服务端忽略 Range 时正确重下；206 的范围必须匹配；暂停/取消后不继续提交；恢复完成前不显示 100%；任务所属模块决定 retry/pause action。

### F12 镜像选择、限流退避和本地复用｜P1｜已有，完善

- **用途：**网络不稳定时仍能下载，已有相同文件不重复传输。
- **KAMUCL 做法：**官方/镜像候选、源健康状态、`Retry-After`、校验后本地复用；并对慢速和可替代来源作处理。[K05]
- **XMCL 实现：**保留 `DownloadSource` 的现有映射与官方回退。补 429 的 `Retry-After` 解析、可重试错误分类、请求取消时清除重试定时器；跨目录复用前验证摘要。UI 同时区分“配置选择的源”和“本任务已回退的实际源”。Quilt、社区资源、整合包及加载器子进程自身下载不要假装已经被镜像接管。
- **验收：**镜像失败能退回官方；之后文件不反复等待同一坏镜像；404/凭据错误不无限重试；复用同名坏文件必须失败；下载日志能看到真实来源且不含令牌。

### F13 正确生成启动参数与 Quick Play｜P0｜部分已有

- **用途：**各种版本、路径、账号、加载器能够正确启动，并连接目标服务器。
- **KAMUCL 做法：**启动模块解析版本继承、规则、Java、账户与 natives，并根据版本能力启用 Quick Play。[K02][K03]
- **XMCL 实现：**继续 QProcess 参数数组，避免 shell 字符串拼接。合并 inherited libraries 时按 Maven 身份处理覆盖，区分新旧 arguments；按 OS/arch/features 应用规则。提取 natives 到实例会话目录。Quick Play 由真实版本能力决定，不支持时显示可复制服务器地址。外置认证 Java agent 只用于明确选中的外置账号。
- **验收：**中文/空格路径、离线/Microsoft/Yggdrasil、旧版/新版各有参数测试；令牌不进日志；库覆盖不会生成冲突 classpath；实际启动验证不能用假 Java 进程代替。

## 5. 账号中心

### F14 微软设备码登录、刷新与账号切换｜P0｜已有，完善

- **用途：**玩家扫码/输入设备码登录，过期后可刷新，不必重复授权。
- **KAMUCL 做法：**Device Code → Microsoft token → Xbox Live → XSTS → Minecraft token → profile，账号层保存选择和刷新状态。[K06]
- **XMCL 实现：**保留内置公共 Client ID 与设备码入口；C++ 中串联授权。区分 pending、slow_down、expired、拒绝、XSTS 账号限制、无游戏档案等状态。新请求取消旧轮询；选中账号刷新失败时保留账号记录并提示重登。不能把获取设备码成功当成完整登录成功。
- **验收：**完整授权链夹具验证每阶段；用户实际完成一次授权、启动和后续刷新；取消后不会把迟到结果选为当前账号；QML 结果没有 access/refresh token。

### F15 离线账号与 UUID｜P1｜已有，完善

- **用途：**本地单人测试或允许离线身份的使用场景。
- **KAMUCL 做法：**为离线玩家生成稳定身份，与联网账号分开处理。[K06]
- **XMCL 实现：**复用现有离线 UUID，按 Java `OfflinePlayer:<name>` 规则生成，校验名称并稳定持久化。界面清楚标识离线账号；启动时不混用另一个联网账号的令牌。
- **验收：**同名 UUID 稳定；大小写行为符合既定规则；选择账号后的身份确实进入启动参数；不把离线登录描述为获得正版服务器认证。

### F16 Yggdrasil 提供商卡片、探测与多角色｜P1｜部分已有

- **用途：**导入皮肤站配置，减少手填多个端点；一个账号可选多个角色。
- **KAMUCL 做法：**`yggdrasilProvider.ts` 支持 JSON/TXT/URL/拖入与 authlib-injector URI；探测 ALI 标头和元数据；`yggdrasil.ts` 处理登录、角色选择。[K06]
- **XMCL 实现：**在现有 provider/登录能力上增加导入预览：解析格式、规范化 apiRoot、受限重定向、读取 serverName/skinDomains，确认后保存。只把经过验证的端点交给 AccountService；多角色显示名称/UUID，选择后请求服务端确认。密码一次性使用。默认 HTTPS，不因失败静默降级。
- **验收：**合法卡片/URI/拖入都到同一预览；错误端点和超大文件明确拒绝；确认前不写配置；多角色选中的身份用于启动；纹理域名按提供商声明匹配。

### F17 跨平台凭据存储｜P0（对应平台发布前）｜部分已有

- **用途：**重启后能刷新账号，同时避免把登录凭据存进普通 JSON。
- **KAMUCL 做法：**账号敏感数据经 Electron `safeStorage`，平台能力决定实际保护方式。[K06]
- **XMCL 实现：**保留 macOS Keychain；为 `SecretStore` 增加 Windows DPAPI/凭据管理器和 Linux Secret Service 后端。普通账号 JSON 只存公开信息与凭据引用。安全存储不可用时提供仅会话登录或明确错误，不悄悄退化为明文。移除账号同步移除其凭据。
- **验收：**配置/日志/导出包不含凭据；锁定钥匙串、拒绝访问、缺桌面 Secret Service 分别可解释；真实目标系统验证写入、读取、删除和刷新。

## 6. 模组与本地资源管理

### F18 模组详情、兼容性和拖入导入｜P1｜部分已有

- **用途：**把一堆 JAR 显示成模组名称、版本、作者、前置和加载器。
- **KAMUCL 做法：**`modinfo.ts`/`modMetadata.ts` 读取 `fabric.mod.json`、`quilt.mod.json`、`META-INF/mods.toml`、`META-INF/neoforge.mods.toml`、`mcmod.info`，缓存扫描与哈希。[K07]
- **XMCL 实现：**新增 `ModMetadata` 与 `ModCatalogService`，用 libarchive 只读指定条目，JSON 用 Qt、TOML 使用明确选定的解析库。一个 JAR 可以含多个 mod descriptor，不能只读第一个。表格显示兼容/不兼容/无法判断及原因。拖入先展示导入清单，再写入当前实例 mods。
- **验收：**五类元数据夹具、多模组 JAR、损坏包、缺图标、大压缩条目都有结果；不执行 JAR；无法识别的版本范围不被标成兼容；UI 扫描时仍可操作。

### F19 批量启停、版本锁定和模组身份｜P1｜部分已有

- **用途：**排查冲突时成批禁用；整合包关键模组不会被更新覆盖。
- **KAMUCL 做法：**`modManagement.ts` 使用 `.jar.disabled` 表示禁用；`modState.ts` 以真实目录、内容哈希和平台项目身份关联锁定状态，换文件名不会丢锁。[K08]
- **XMCL 实现：**复用 `resource.toggle` 并增加批量目标状态，而非逐个“反转”。锁定记录按目录身份+hash/projectId 保存，更新迁移锁状态。操作前检查所有使用同一 mods 目录的运行会话；报告每个文件成功/失败，同名目标不覆盖。
- **验收：**重复禁用是幂等操作；同名冲突只影响该项；离线仍可锁定；重命名后锁保留；其他实例不会无意继承锁。

### F20 实例内及跨实例重复模组检测｜P1｜部分已有

- **用途：**找到同一 modId 的多个版本，识别占用空间的重复副本。
- **KAMUCL 做法：**`findDuplicates()` 按 modId 分组；`findCrossDuplicates()` 对所选实例扫描，展示出现位置。[K07]
- **XMCL 实现：**在现有重复检测上区分“同 modId 不同版本”与“内容哈希完全相同”。展示加载器、版本范围、是否启用、依赖影响和建议保留项；跨实例显示真实目录并去重共享目录。清理进入可恢复区，用户选择后再执行。
- **验收：**不同 modId 不因文件名相似被误删；无法比较的版本只提示；跨实例重复不自动删除正常依赖；还原后内容与状态一致。

### F21 按哈希检查模组更新与事务回滚｜P1｜新增完整流程

- **用途：**一键检查能用的新版本，更新失败也保留旧模组。
- **KAMUCL 做法：**`checkModUpdates()` 扫描 SHA-1，调用 Modrinth `/version_files/update` 并限定 MC/loader；`replaceModFiles()` 暂存新文件、复核旧文件、备份、替换，失败恢复。批量更新可返回逐项结果。[K09]
- **XMCL 实现：**新增 `ModUpdateService` 和事务 helper，输出只读更新计划（旧 hash、新 hash、目标版本、体积、依赖、锁状态），用户选项后才下载。提交前重新验证旧 hash 与运行状态；保留 `.disabled`。一次依赖连带变更作为一个事务；独立更新失败可不影响其他已完成项，界面明确实际粒度。
- **验收：**断网、坏 hash、磁盘写失败、源文件被外部修改时旧文件保持；中途崩溃有恢复日志；锁定项不会默认更新；被禁用的模组更新后仍禁用。

### F22 缺失前置检测与一键补齐｜P1｜部分已有

- **用途：**启动前发现缺 Fabric API 或其他必要库，展示原因并补下载。
- **KAMUCL 做法：**元数据解析前置；`planMissingDependencies()` 生成兼容文件计划，`applyMissingDependencies()` 通过事务安装。[K07][K08]
- **XMCL 实现：**建立依赖图，节点含 modId/projectId、版本约束和来源，边区分 required/optional/incompatible。先检查本地已启用模组，再查询社区候选；检测循环，冲突时展示约束来源，不武断选最新版。只对必需且已找到可信兼容文件的项目提供一键补齐。
- **验收：**已满足前置不重复下载；固定 versionId 与通用 projectId 正确处理；A→B→A 不死循环；找不到依赖不给成功提示；冲突不会偷偷删除用户模组。

### F23 切换模组版本、降级与单项恢复｜P1｜新增

- **用途：**新版本模组出问题时选回兼容的旧版本。
- **KAMUCL 做法：**`modVersionChoices()` 查候选，`planModVersionChange()` 预览文件和前置变化，`applyModVersionChange()` 复核快照后事务替换。[K08]
- **XMCL 实现：**模组详情增加版本列表，按当前 Minecraft/loader 筛选，计划绑定旧 hash、目录和候选版本。锁定模组需用户明确解除或确认本次变更；复用 F21 事务，不写第二套替换逻辑。
- **验收：**降级后文件、锁定、禁用状态正确；过期计划拒绝执行；其他模组依赖不满足时阻止自动提交或要求解决，不能只换 JAR 后宣布兼容。

### F24 把模组集合迁到新的 Minecraft 版本｜P2｜新增

- **用途：**从旧版整合包建立新版测试副本，原存档和配置仍可继续玩。
- **KAMUCL 做法：**`modMigration.ts` 扫描来源并匹配目标 MC/loader；生成可迁移/不可用清单，确认缺项后创建新实例，保留原实例及禁用状态。[K10]
- **XMCL 实现：**“升级玩法”向导先选目标版本和加载器，列出可匹配、无需变化、缺失、不兼容及额外依赖。安装到新 UUID。未知 JAR 默认不启用，单独列出。配置/存档迁移分开选择；不承诺世界能无损降级。
- **验收：**任何失败原实例 hash 集合不变；缺项必须被确认；新实例的客户端与 loader 真正安装；迁移不是原地把 `version` 字段改掉。

### F25 资源包与光影包管理｜P1｜部分已有

- **用途：**浏览本地包、看图标、导入并找到兼容实例。
- **KAMUCL 做法：**`resourceFiles.ts` 与目录解析模块管理资源文件，默认资源包另有同步逻辑。[K11]
- **XMCL 实现：**在资源面板读取资源包 `pack.mcmeta`/`pack.png` 和光影包结构；导入前检查路径、同名冲突。安装只表示文件已放到正确目录；自动启用资源包要安全合并 `options.txt`，光影启用依赖具体模组，不能一概写同一配置。
- **验收：**不兼容 pack_format 可提示；坏 ZIP 不进入目标；导入不会覆盖另一包；删除不破坏已有其他资源包选择。

### F26 世界导入、元数据与按世界安装数据包｜P1｜部分已有

- **用途：**导入下载的地图，识别版本，数据包装进指定存档而非错误的全局目录。
- **KAMUCL 做法：**`worlds.ts` 检测文件夹/ZIP 中的世界根，读压缩 NBT `level.dat`，展示 DataVersion、世界名等；导入采用独立目标名称。[K11]
- **XMCL 实现：**新增复用 NBT 读取器与世界选择器，识别多世界包及嵌套目录，保留数据版本提示。普通数据包目标为 `<gameDir>/saves/<world>/datapacks/`；只有明确识别到支持全局数据包的模组配置时才提供其专用路径。游戏运行时不覆盖活动存档。
- **验收：**ZIP 外包一层目录、多世界、中文世界名均可选；NBT 解压大小/深度受限；新导入不覆盖旧世界；数据包选 A 世界后 B 不发生变化。

### F27 默认游戏选项与游戏按键配置｜P1｜新增

- **用途：**新实例自动沿用音量、视野、灵敏度、语言和常用游戏按键。
- **KAMUCL 做法：**`defaultGameOptions.ts`/`keybindings.ts` 按版本编码设置，逐键合并 `options.txt`，保留未知项；高对比等选项还关联内置资源包。[K12]
- **XMCL 实现：**新增 `GameOptionsService`，与启动器自身快捷键明确区分。维护逻辑设置→不同 MC 版本字段/编码的映射；用户启用同步后，在未运行且启动准备阶段合并。保留换行、无关键和资源包列表；不支持项给出说明，识别不了版本时不写。
- **验收：**老版本数字键码与新版本字符串键码有独立夹具；同步前后无关行不丢；重复键处理确定；无效现有配置不被清空；不覆盖世界难度或未知模组配置。

### F28 服务器收藏、实例绑定、状态探测与一键进入｜P1｜部分已有

- **用途：**选择服务器即可使用匹配实例启动，并看到在线人数、版本和 MOTD。
- **KAMUCL 做法：**`servers.ts` 持久化记录、收藏与实例绑定，合并 `servers.dat`，通过 Minecraft Status 协议与 DNS SRV 获取状态。[K13]
- **XMCL 实现：**现有 TCP 连通性升级为真正 SLP：QTcpSocket 分帧解析 VarInt，发送 handshake/status，解析 JSON；可加 ping/pong RTT。无显式端口域名通过 QDnsLookup 查询 SRV。保存原始逻辑域名与实际连接端点，支持 `[IPv6]:port`。绑定 instanceId，启动交给 F13。
- **验收：**拆包/粘包、超长帧、超时、离线返回正确；仅 TCP 打开但非 MC 服务不得显示游戏在线；servers.dat 写入保留其他记录；SRV、显式端口及 IPv6 各有用例。

## 7. 实例管理中心与玩家数据

### F29 实例复制、可选范围备份与恢复｜P1｜已有，完善

- **用途：**做一份实验副本，或只备份需要的配置/存档。
- **KAMUCL 做法：**`instanceCenter.ts` 分别处理实例、玩家目录、存档、截图；`backupStore.ts` 记录文件和校验信息，支持事务恢复。[K14]
- **XMCL 实现：**将当前同步复制/压缩改为后台任务。面板选择 mods/config/saves/screenshots 等范围，显示大小与剩余空间。备份清单记录 schema、来源版本、文件路径、大小、hash；恢复默认创建新 UUID。共享库引用与完整可搬迁备份要明确区分，不把仅实例目录 ZIP 宣称为完全离线包。
- **验收：**大目录复制期间 UI 可操作；取消后原实例完整；备份损坏在写入正式目标前发现；恢复副本能找到所需 profile/client/library；不复制账号密钥。

### F30 修改前自动保护与恢复记录｜P1｜新增完整流程

- **用途：**模组更新、批量禁用、还原存档之前留下恢复点。
- **KAMUCL 做法：**`changeProtection.ts` 对将修改的文件创建保护备份，并保存模组锁定状态；恢复操作有目录锁与运行检查。[K14][K09]
- **XMCL 实现：**把 F21 的文件事务与备份服务关联，保存操作类型、受影响路径、旧 hash、锁定/禁用状态。按容量/数量做可见的保留策略，手工备份不被自动清理。原位恢复先创建当前状态备份，再验证并替换；冲突时保留恢复材料和路径。
- **验收：**恢复点能真正还原文件与状态；未参与操作的文件不改；外部修改阻止自动覆盖；恢复失败不会删除唯一旧副本。

### F31 存档与截图浏览｜P1｜部分已有

- **用途：**从实例详情直接找世界、查看截图、打开目录或恢复某个存档。
- **KAMUCL 做法：**`centerWorlds()` 读取世界资料，`centerScreenshots()` 分页加载图片，`restoreWorld()` 单独恢复世界。[K14]
- **XMCL 实现：**实例详情增加存档/截图页签；世界读取名称、DataVersion、最后游玩时间、图标；截图懒加载缩略图，按实例缓存。恢复世界使用备份事务，导出/打开路径由 C++ 校验。
- **验收：**上千截图不一次解码全部；坏图片可跳过；切换实例不会显示旧请求的图片；恢复单个世界不影响其他世界。

## 8. 社区与整合包

### F32 社区搜索、分页、排序与收藏｜P1｜部分已有

- **用途：**按版本和加载器找资源，不限于第一页搜索结果。
- **KAMUCL 做法：**`community.ts` 统一 Modrinth/CurseForge 搜索及文件结构，`communityPaging.ts` 处理分页；收藏由 `modFavorites.ts` 管理。[K15]
- **XMCL 实现：**保留 `CommunityDialog`，补 provider+kind+query+MC+loader+sort+page 的完整缓存键，按 API 游标/offset 翻页。收藏保存 provider/projectId，详情再刷新。每次筛选改变递增 requestId，旧响应不可覆盖新搜索；UI 显示请求中/空列表/请求失败三种状态。
- **验收：**快速切换筛选不串结果；第二页不重复；详情返回后保留筛选；CurseForge 缺 Key 显示设置入口；收藏在离线时仍有基本名称。

### F33 社区安装确认、依赖解析与文件事务｜P0｜部分已有

- **用途：**下载一个模组时，连同必要前置正确安装到当前实例。
- **KAMUCL 做法：**社区层规范化文件、版本和依赖，安装前展示计划；下载器校验，事务层负责提交。[K15][K09]
- **XMCL 实现：**扩展现有 Modrinth required 解析，补 CurseForge 必需依赖与冲突处理。安装预览展示主文件、必要/可选前置、已有文件、替换内容及体积，kind 决定目标目录。数据包转入 F26 世界选择；整合包转 F34，不把整合包 ZIP 塞进 mods。
- **验收：**依赖循环、固定版本、重复依赖、客户端不支持项都有处理；任一必需文件失败时这次事务不部分提交；用户改动的同名文件不被静默覆盖。

### F34 整合包识别、导入与安装完成闭环｜P0｜部分已有

- **用途：**拖入 mrpack/CurseForge ZIP 后得到能启动的实例。
- **KAMUCL 做法：**`modpacks.ts` 解析 Modrinth、CurseForge 和部分完整实例包，处理覆盖目录与目标版本；安装完成前准备游戏和加载器。[K16]
- **XMCL 实现：**当前 PackService 已有暂存导入，但导入资源不等于游戏安装完成。新增导入向导：识别清单→预览名称/版本/loader/体积→解决文件→安装基础游戏和 loader→应用 overrides→校验→发布实例。区别资源导入状态和可启动状态。完整实例包识别只支持有明确结构规则的格式，不把任意 ZIP 当整合包。
- **验收：**mrpack 的客户端环境过滤、`overrides` 后应用 `client-overrides`；Forge/Fabric 包能实际启动；坏 hash、取消、压缩穿越不产生“已安装”实例；包内凭据/日志/可执行启动器文件不自动执行。

### F35 禁止自动下载文件的手工补全｜P1｜新增

- **用途：**部分 CurseForge 作者限制第三方下载时，用户手工下载后继续安装。
- **KAMUCL 做法：**`modpackManualFiles.ts` 暂停安装等待文件；按 projectID/fileID、大小和 SHA-1 匹配选入文件，全部补齐才继续。缓存根在任务开始时固定。[K16]
- **XMCL 实现：**PackService 增加 `awaitingFiles` 阶段和持久化缺失清单。界面列出名称、官方页面、原因和“选择已下载文件”。按内容校验而非文件名接受；补文件请求绑定 taskId，任务取消后拒绝继续写入。浏览器下载由用户完成，不绕过平台下载限制。
- **验收：**同名错误内容被拒绝；正确文件改名后仍可识别；一次补多个、取消后补、重启后继续都有明确行为；未补齐不能标为完成。

### F36 整合包更新与保留玩家文件｜P2｜新增

- **用途：**升级整合包时保留世界、截图及用户自己加的内容。
- **KAMUCL 做法：**`modpacks.ts` 识别已安装包、记录受管文件集合，`restoreModpackUserFiles()` 保留玩家文件。[K16]
- **XMCL 实现：**每次安装记录来源项目/版本、managedFiles 与原始 hash。更新比较旧清单、新清单、当前磁盘：可安全替换、已被玩家修改、不再受管、玩家新增分别处理。先在新目录生成升级实例，明确配置冲突和旧模组残留策略；用户确认后才切换默认实例。
- **验收：**saves/screenshots 不丢；已修改 config 不被悄悄覆盖；旧受管模组不与新版本并存导致冲突；取消/失败旧实例仍可启动。

## 9. 皮肤衣柜

### F37 3D 角色预览与模型切换｜P1｜已有，完善

- **用途：**上传前确认经典/纤细模型、第二层及头像是否正确。
- **KAMUCL 做法：**皮肤模块获取纹理/缓存，前端 3D 组件展示角色；账号与纹理数据分离。[K17]
- **XMCL 实现：**保留 `SkinGeometry`/`SkinPreview`，增加易发现的模型切换、第二层开关、复位视角和加载错误。纹理下载遵守账号来源的域名策略；缓存以内容/账号身份区分。
- **验收：**64×64、旧 64×32、纤细手臂、透明外层分别检查；拖拽/缩放正常；坏图片不造成页面崩溃；截图不等于真实上传成功。

### F38 皮肤上传与披风选择｜P1｜部分已有

- **用途：**从衣柜选择本地皮肤，应用到选中的账号。
- **KAMUCL 做法：**`skins.ts` 根据账号类型调用档案/皮肤接口，并提供官方披风选择；README 的“披风上传”不能套用于所有账号。[K17]
- **XMCL 实现：**保留官方上传与已拥有披风列表；按 provider 的真实能力暴露操作。Microsoft 账号只能切换其拥有的官方披风，不支持任意上传 PNG 成为官方披风。外置皮肤站只有协议明确支持时实现上传；成功后重新取 profile 验证，不仅弹 toast。
- **验收：**账号切换期间响应不串号；权限失效要求重登；图片尺寸/格式拒绝在发送前完成；真实上传验收单列；不支持的能力不显示可点假按钮。

### F39 历史皮肤重命名、删除、复用和缓存｜P1｜部分已有

- **用途：**找回以前用过的皮肤，再次应用。
- **KAMUCL 做法：**`history()`、`historyRename()`、`historyDelete()`、`uploadHistory()` 管历史索引与复用。[K17]
- **XMCL 实现：**在当前本地衣柜上增加稳定 entryId、显示名、hash、variant、时间和来源账号，引用受管纹理文件；相同内容去重。删除索引后仅清理没有其他引用的纹理，应用历史仍经过 F38 正常授权。
- **验收：**重启历史保留；重命名不改内容；删除一条不破坏另一条共用纹理；缺失文件显示可修复状态；历史记录不含令牌。

## 10. 联机

### F40 好友直连、邀请、局域网端口检测与 UPnP｜P2｜部分已有

- **用途：**有可达网络条件时，让好友通过邀请进入本机开放的世界。
- **KAMUCL 做法：**`directConnect.ts` 构造双栈 TCP 转发到游戏 LAN 端口；IPv6/IPv4/LAN 候选，UPnP 短租约，邀请含地址、版本和有效期。该路径不包含 STUN/TURN 或中央房间服务。[K18]
- **XMCL 实现：**在现有地址解析/网卡能力上增加 QTcpServer 转发、连接上限、背压和生命周期；邀请使用有版本、大小限制的 JSON/Base64URL，解析后重算地址类型。UPnP 仅操作本机网关上自己创建的短租约，续租/删除前核对唯一标记。接收邀请只预览，用户发起检测/连接。
- **验收：**IPv4/IPv6 回环夹具及两台真实网络机器；关闭房间清连接/监听/租约；无公网路径明确说不可达；邀请不是密码，TCP 可达不等于游戏认证通过。

### F41 FRP 节点、隧道管理与状态｜P2｜部分已有

- **用途：**使用已有 FRP 服务把本机游戏端口映射给好友。
- **KAMUCL 做法：**`frp.ts`/`frpIpc.ts` 对接配置/提供商 API 与 frpc；`frpLog.ts` 解析运行日志，区分客户端启动和隧道注册结果。[K19]
- **XMCL 实现：**保留用户提供 frpc 路径的流程；如增加特定服务商账号适配，独立 provider 层读取节点/隧道，用户明确创建后调用 API。用 QProcess 管本应用进程，凭据写受限临时配置；显示 connecting/authenticated/registered/error，确认隧道注册后才给外部地址。
- **验收：**错误密钥、节点离线、注册拒绝、进程崩溃各显示真实状态；停止只结束自有进程；API 夹具与真实自有隧道验收分开记录。

### F42 Terracotta 房间创建与加入｜P2｜部分已有

- **用途：**用陶瓦/EasyTier 提供的联机能力建立房间。
- **KAMUCL 做法：**`terracotta.ts` 管官方程序、独立环境、回环 HTTP 状态和退出；参考快照记录了固定版本的实际 CLI 行为。[K19]
- **XMCL 实现：**保留当前进程与本机 HTTP 控制，增加版本能力检测和“准备工具/启动服务/创建或加入/在线/退出”状态。若提供工具安装器，使用官方版本、平台包与校验值，不编造 CLI 参数。状态读取失败不能保留假在线。
- **验收：**真实官方程序创建/加入/停止，本机夹具覆盖超时和取消；再用两台机器实际进游戏；平台不支持时明确提示，不回退到假房间码。

### F43 VoxLink 房间码、打洞与中继｜P2｜新增大模块

- **用途：**房间码加入，尝试 P2P，必要时按协议切换中继。
- **KAMUCL 做法：**`voxlink/` 拆分 API、STUN、punch、会话认证、可靠传输、TCP 桥接、TURN 和清理。来源与未覆盖能力记录在 `VOXLINK-UPSTREAM.md`；本地协议测试不等于公网双端联调。[K20]
- **XMCL 实现：**不要用“解析一个房间字符串”代替 VoxLink。先固定上游协议/许可版本；拆 `VoxRoomClient`、`StunClient`、`PunchSession`、`RelayTransport`、`TcpBridge`，以 QUdpSocket/QTcpSocket/QNetworkAccessManager 实现协议状态机。沿用可核对的帧格式和会话认证，处理乱序/丢包/重传/限流/取消与迟到分配回收；不自行发明加密协议。
- **验收：**固定协议向量、真实本地 UDP/TCP 双向数据、错误票据/认证篡改、丢包重传、取消释放、两种 NAT 网络真实游戏。未完成公网测试须明确保留“未验证”。语音等上游功能另列范围，不默认承诺。

## 11. MOD 实时参数面板

### F44 Bridge 安装、发现、握手与版本检查｜P1｜已有，完善

- **用途：**启动器发现正在运行的 Fabric 游戏桥接服务。
- **KAMUCL 做法：**内置独立 Bridge JAR；客户端读取发现文件，检查本机端点并携 token 请求状态/清单。[K21]
- **XMCL 实现：**保留内置 JAR 和 `BridgeService`，补游戏/loader 兼容显示、重复旧版本检测、重启提示和协议版本不匹配说明。发现文件属于指定实例，只访问受控 loopback，拒绝通过代理发送 token。
- **验收：**未启动/未安装/旧发现文件/协议不匹配分别可见；不能覆盖不明同名 MOD；token 不进入 QML 和日志；真实 Fabric 游戏里完成握手。

### F45 按参数类型生成可用的 MOD 设置面板｜P1｜部分已有

- **用途：**用户不用手动输入参数 ID，可直接调整支持 Bridge 的模组。
- **KAMUCL 做法：**`bridgeManifest()` 取 MOD、分组、类型、默认值等，`bridgeSet()`/`bridgeReset()` 修改，前端支持筛选。[K21]
- **XMCL 实现：**专用 QML 面板按 manifest 类型生成 Toggle、数值控件、GlassComboBox 和文本输入；显示单位、范围、说明、是否需重启。输入校验在 QML/C++/服务端都有边界；等待服务器实际返回值后确认状态。按 MOD、分组、关键词搜索。
- **验收：**无效值被拒绝后恢复实际值；断线不显示已保存；恢复单项/全部范围清楚；仅 Bridge 注册参数可修改，不能宣称支持所有模组配置。

## 12. 个性化与扩展

### F46 主题、背景、启动图轮播和实例缩略图｜P1｜部分已有

- **用途：**个性化而不降低可读性，长期开启仍流畅。
- **KAMUCL 做法：**`appearanceAssets.ts` 管受管图片导入，设置与前端消费主题、背景和启动缩略图。[K22]
- **XMCL 实现：**保留当前森林默认主题；扩展图片列表、轮播间隔、缩放/遮罩、实例缩略图选择。图片复制到受管目录并生成限尺寸缓存，避免引用会被删除的临时文件。主题色仅覆盖适合的变量，下拉文本/背景对比度继续由 Theme 保证。
- **验收：**原图移走后受管背景仍可用；大图内存有上限；浅背景下文字可读；最小窗口所有操作可达；恢复默认保留其他用户数据。

### F47 启动器快捷键、启动后行为与功能开关｜P1｜部分已有

- **用途：**自定义导航键，选择游戏启动后最小化或保留窗口。
- **KAMUCL 做法：**`settings.ts` 与启动/会话层联动，保存功能开关、窗口和启动后行为；这与 Minecraft 游戏按键不同。[K02][K22]
- **XMCL 实现：**保留已有 QML Shortcut，补冲突检查、恢复默认和按操作启停。启动后行为以收到真实进程 started 为条件。退出启动器前检查下载、Bridge、联机房间等依赖；用户选择退出时说明会中断的任务，不能默认结束游戏或丢失任务。
- **验收：**重复快捷键提示明确；文本输入时不错误触发操作；启动失败不最小化；房主依赖启动器转发时不会静默退出。

### F48 原生插件安装、启停与扩展入口｜P2｜已有，完善

- **用途：**提供可选扩展，不把全部特性写进主程序。
- **KAMUCL 做法：**`plugins.ts` 管插件目录、元数据、启用状态与前端代码协议；技术栈是 Electron/JavaScript。[K22]
- **XMCL 实现：**继续使用现有 Qt 原生插件 ABI、元数据校验和显式启用；写清支持的 Qt/编译器/架构。插件变更 hash 后重新确认；插件 UI/命令只经定义好的注册接口接入。原生插件在进程内拥有本机权限，不称为沙箱；如以后需要隔离，再设计独立进程协议。
- **验收：**ABI 不匹配拒绝；安装不执行；启用后执行真实示例；停用释放定时器/任务；不宣称能直接加载 KAMUCL JS 插件。

## 13. 诊断、日志与更新

### F49 启动失败诊断与可执行建议｜P0｜部分已有

- **用途：**玩家看到“Java 不匹配/缺依赖/内存不足/文件损坏”，而不是一串退出码。
- **KAMUCL 做法：**`diagnosticRules.ts`、`diagnosticSession.ts` 汇总日志与会话，`diagnostics.ts` 提供脱敏等辅助。[K23]
- **XMCL 实现：**把已知规则整理成有证据的结果：ruleId、严重性、命中的日志片段、推断、建议操作。优先精确识别 Java class version、缺模组、重复模组、OutOfMemory、无效 JVM 参数、缺库/原生库。不要见到任意 crash 就推荐“加内存”；未知错误给出日志入口。
- **验收：**每条规则有正反例；建议有对应的可用设置/修复入口；同一日志可显示多个相关原因；诊断明确是推断，不自动删除模组或修改存档。

### F50 实时日志、搜索与脱敏导出｜P1｜已有，完善

- **用途：**玩家能看最近错误并发给别人排查。
- **KAMUCL 做法：**日志归档模块汇集启动器/游戏/诊断信息，导出前隐藏凭据、个人路径等。[K23]
- **XMCL 实现：**现有 MaintenanceService 增加按会话查看、搜索、级别过滤和尾随模式，限制 UI 保留行数。原始日志留本地，导出生成独立脱敏副本及环境摘要；不打包整个用户数据目录。错误提示给出导出位置。
- **验收：**accessToken、refreshToken、Authorization、设备码、账号私密标识和已知秘密不进导出；大日志流式处理不卡 UI；脱敏不修改原始文件。

### F51 自动更新检查、下载与独立替换 helper｜P2｜部分已有

- **用途：**使用 XMCL 自己的发行包安全升级。
- **KAMUCL 做法：**`selfUpdate.ts` 检查版本/包与摘要；`applyUpdate.ts` 暂存更新事务，由外部更新器在退出后替换，启动后确认。[K24]
- **XMCL 实现：**先建立 XMCL 发行源、平台/架构标识、可验证签名与版本策略，再补独立 Qt/C++ helper。主程序只下载并验证到 staging；helper 等待指定进程退出、备份旧应用、替换、启动新版本；新版本发出健康确认后提交事务。macOS bundle 和 Windows 被占用 EXE 分平台处理。摘要保证文件一致性，不能单独代替发布者签名。
- **验收：**错误 appId/架构/签名/摘要拒绝；应用仍运行时不覆盖；只读 DMG、权限不足、磁盘不足说明原因；真实安装包升级验证，不用解压目录测试替代。

### F52 更新失败回滚、本地更新包与数据迁移兼容｜P2｜部分已有

- **用途：**新版不能启动时恢复上一个可用程序，离线也能导入可信更新包。
- **KAMUCL 做法：**更新状态文件记录 pending、失败、备份和启动确认；支持检查本地包、恢复备份。[K24]
- **XMCL 实现：**helper 持久化步骤日志并可幂等重试；新进程未确认则提供回滚，保留失败原因。本地包走与在线包相同的验证。应用回滚与数据 schema 回滚分开：迁移前备份、版本化、能否被旧版读取写进策略，不因换回二进制就承诺数据自动兼容。
- **验收：**每个事务阶段模拟崩溃后可恢复；回滚不删除用户目录；坏本地包不能安装；无可信发行源时明确不可用，而不是检查 KAMUCL Release。

## 14. 源码中额外值得考虑的工具

### F53 一键创建命令测试世界并启动｜P2｜新增

- **用途：**模组开发、测试建筑或命令时，快速进入新的创造/允许命令世界。
- **KAMUCL 做法：**`commandWorld.ts` 从真实 client JAR 的 `version.json` 取世界数据版本，生成新目录与压缩 NBT，按客户端格式差异写世界生成数据，再通过 Quick Play 启动。[K25]
- **XMCL 实现：**新增 `CommandWorldService`，复用 NBT 编解码和 F13 启动；仅支持明确验收过的数据格式。随机唯一目录，绝不复用现有世界；取真实 DataVersion，未知格式回退提示玩家在游戏内创建。先覆盖一个现代版本，再逐版本扩展。
- **验收：**生成世界确实可进入且允许命令；原存档不变；不支持版本不写半成品；不能只成功生成一个 `level.dat` 就宣布兼容。

### F54 建筑投影文件管理与格式转换｜P2｜新增，可独立后置

- **用途：**集中收藏 `.litematic`、`.schem`、`.schematic`、结构 `.nbt`，复制到目标实例。
- **KAMUCL 做法：**`projectionsIpc.ts` 扫描/管理文件；`projectionFormats.ts` 解析不同 NBT 布局、方块调色板及压缩数组；转换在 worker 中执行，并校验范围。[K26]
- **XMCL 实现：**第一阶段只做有边界的扫描、元数据、导入/导出；第二阶段再增加 `ProjectionCodec` 和工作线程转换。内部模型保存区域、偏移、调色板、方块索引、实体与 block entities。旧 schematic 无法表示的新方块必须拒绝或让用户明确选择有损策略；不把未知方块静默替换空气。
- **验收：**负坐标、多区域、跨 64 位边界的 bit packing、VarInt、实体偏移、压缩炸弹限制均有测试；可无损格式往返比较语义；源文件始终保留。

## 15. 给 DeepSeek 的具体实施规则

### 15.1 新增接口应长什么样

现有调用方式继续保持：

```cpp
// 已有接口：FeatureService 子类实现。
QVariantList actions() const override;
void execute(const QString &action, const QVariantMap &args) override;

// 已有信号：由 ServiceHub 再附加 module 名转发给 QML。
void completed(const QString &action, const QVariantMap &data);
void failed(const QString &action, const QString &message);
void activity(const QString &message);
```

下列是 **建议新增的 action 合约，当前未实现**。计划由后台生成并保存，QML 不能提交任意文件 URL/目标路径要求覆盖。

| 建议 action | 请求 | 返回/效果 |
| --- | --- | --- |
| `mods.update.plan` | `{instanceId}` | `{planId, expiresAt, items:[{id,name,oldVersion,newVersion,size,locked,warnings}], snapshotId}` |
| `mods.update.apply` | `{planId, itemIds}` | 验证后台计划与源快照，创建 taskId；进入下载/校验/提交 |
| `mods.dependencies.plan` | `{instanceId, fileId}` | 缺失、冲突、可选前置及可安装文件计划 |
| `instance.migrate.plan` | `{instanceId, targetFolderId}` | 文件冲突清单、需要空间、sourceFingerprint、planId |
| `instance.migrate.apply` | `{planId, conflictChoices}` | 后台复制校验，成功后更新实例引用 |
| `pack.missingFiles` | `{taskId}` | 不含凭据的文件清单、官方页面与剩余数量 |
| `pack.supplyFiles` | `{taskId, localFiles}` | `{accepted,rejected,remaining}`；必须校验选入文件 |
| `java.provision` | `{runtimeCandidateId}` | 下载后台事先验证过的候选运行时，返回 taskId |

跨模块任务建议统一字段（逐步兼容旧 `downloadTasks`，不要一次重写全部服务）：

```json
{
  "taskId": "uuid",
  "owner": "mods",
  "instanceId": "uuid",
  "title": "更新所选模组",
  "phase": "downloading",
  "active": true,
  "completedFiles": 2,
  "totalFiles": 5,
  "transferredBytes": 2097152,
  "totalTransferBytes": 8388608,
  "speedBytesPerSecond": 524288,
  "canPause": true,
  "canCancel": true,
  "canRetry": false,
  "error": ""
}
```

约定 phase 可取 `queued / metadata / downloading / verifying / awaitingFiles / committing / installing / complete / paused / cancelled / error`。暂停要保存暂停前阶段；`totalTransferBytes` 未知时为空值，不能为了百分比伪造数值。`transferredBytes` 表示传输量，不代表全部文件已验证。多任务支持后，操作必须按 taskId 路由，不能只给 module 全局“暂停”。

### 15.2 所有会改文件的批量功能遵循同一事务

1. **规划：**解析真实实例目录，记录待变更文件和 hash，生成短期有效 planId；预览不修改文件。
2. **准备：**把新文件下载到同一目标卷上的独立 staging，验证大小/摘要；保留旧文件。
3. **复核：**取得规范化目录写锁，检查游戏未运行、计划未过期、源文件未被修改、目标无新冲突。
4. **保护：**保存旧文件与状态清单，写持久化 journal；备份失败则停止提交。
5. **提交：**逐步替换并记录完成项；元数据通过 QSaveFile 提交。一个文件原子写并不能让整批文件自动拥有原子性。
6. **恢复：**失败只撤回本事务写入且 hash 仍匹配的文件，不覆盖其他程序刚改的内容；恢复失败保留 staging 和可操作路径。
7. **结束：**实际校验完成后才发 `complete`。清理仅限本事务创建的目录；用户源文件不在自动清理范围。

目录锁还要覆盖共享目录的多个实例；大任务用 cooperative cancellation，提交关键段结束后再响应取消或执行恢复。支持任务续传时，应用重启先读 journal 决定恢复/重试，不能无条件删除 `.part` 和旧备份。

### 15.3 首批文件安排建议

以下新文件均为建议名，开始实现前确认目录没有同名新改动。增加源文件后核对 CMake，新增 QML 必须加入 `QML_FILES`。

| 批次 | 新增文件建议 | 修改现有文件 | 测试落点 |
| --- | --- | --- | --- |
| A：Java/启动 | `src/services/JavaRuntimeService.h/.cpp`、`LaunchPreparation.h/.cpp` | `GameService.*`、`ServiceHub.*`、`InstanceSettingsDialog.qml` | `tests/tst_game.cpp`；新增 `tests/tst_java.cpp` 并注册 CTest |
| B：模组 | `src/services/ModMetadata.h/.cpp`、`ModCatalogService.h/.cpp`、`ModUpdateService.h/.cpp`、`FileTransaction.h/.cpp`、`ui/ModsDialog.qml` | `ResourceService.*`、`ServiceHub.*`、`Main.qml` | 新增 `tests/tst_mods.cpp`；事务失败夹具；`tests/tst_ui.cpp` |
| C：整合包 | `ui/PackImportDialog.qml`、`ui/MissingFilesDialog.qml` | `PackService.*`、`CommunityService.*`、`CommunityDialog.qml`、`DownloadsPage.qml` | `tests/tst_packs.cpp`、`tst_community.cpp`、`tst_ui.cpp` |
| D：目录/玩家数据 | `src/services/GameFolderService.h/.cpp`、`WorldMetadata.h/.cpp`、`ui/InstanceCenterDialog.qml` | `InstanceService.*`、`ArchiveUtils.*`、`ResourceService.*` | `tests/tst_instances.cpp`；备份往返/取消/跨盘夹具 |

不要把表中所有文件一次性建成空类。每个可使用的小功能实现时再创建它需要的文件。

### 15.4 验证方式

在 XMCL 根目录执行当前工程已有命令：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH='/opt/homebrew;/opt/homebrew/opt/libarchive' -DBUILD_TESTING=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

其他机器使用实际 Qt/libarchive 路径。预期是构建成功、已有 12 组及本批新增测试全部通过；文档并不声称本次已经运行这些命令。

每批必须验证：

- **后端行为：**临时目录与本机 HTTP/TCP 服务，真实读写文件并核对 hash；测试成功、取消、网络错误、损坏、源文件变化和提交失败。
- **生产 QML：**从实际入口点击到完成，不绕过 UI 直接调用 C++ 后宣称界面已完成；最小窗口 `1060×720`，检查列表滚动、错误可见、下拉可读。
- **数据保留：**提前备份用户数据，开发测试只使用临时目录；重命名/升级后实例 UUID、存档、锁定、收藏保持。
- **真实外部联调：**新接口至少一次真实请求/文件下载；加载器和 Java 需实际游戏启动；登录需真实授权；联机需真实双端。没做就写“未验收”，不能用本机夹具结果替代。
- **记录：**交付涉及文件、操作入口、验证命令、结果和剩余限制；更新 `docs/migration/STATUS.md` 的对应行，未完成模块继续保留缺口。

### 15.5 可直接复制给 DeepSeek 的首轮提示词

```text
请阅读 docs/KAMUCL-FEATURES-FOR-DEEPSEEK.md、README.md 和 docs/migration/STATUS.md，
并以当前 src/ 与 ui/ 实际代码为准。

保持现在的森林、圆角、液态玻璃主界面，保留已经完成的下载弹窗、社区弹窗、
实例设置和 GlassComboBox；不要重写为 Electron，也不要覆盖其他已有修改。

先执行文档批次 A：F09 Java 扫描/匹配/健康检查，F10 启动前校验与修复，
F13 启动参数兼容，F49 基于证据的启动诊断。
从最小可用闭环开始：选择实例 → 检查 Java 和缺失文件 → 用户可见的修复任务 → 实际启动。
已有实现应复用，只补缺失部分，不先创建大量空类。

每项完成后验证真实文件/参数/状态，并从生产 QML 操作一次。
运行相关测试和全量 CTest，记录实际游戏联调结果；没有验证的内容如实列出。
不要用空按钮、假进度、QProcess started 或测试 Java 夹具代替游戏启动验收。
不要推送到 KAMUCL 仓库，不要发布 Release，不要改用 KAMUCL 更新包。

输出本批改动、使用入口、验证结果和下一批建议；本批完成前不要扩展到 VoxLink 等大模块。
```

## 16. 固定源码索引

本地源代码根：`build/reference/KAMUCL/`。以下链接固定到已检查的提交，避免远程主分支变化后定位不一致。相邻模块名都相对于 `src/main/core/`；这些是参考实现，不是要直接复制到 XMCL 的完整文件。

| 编号 | 参考源文件与关联内容 |
| --- | --- |
| K00 | [功能树](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/docs/FEATURE_TREE.md)：页面、业务与模块映射 |
| K01 | [instances.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/instances.ts)、`resourceDirectory.ts`、`gameFolders.ts`：实例、隔离与多目录 |
| K02 | [launch.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/launch.ts)、`launchPreparation.ts`、`gameSession.ts`：启动与会话 |
| K03 | [loaders.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/loaders.ts)、`versions.ts`、`installerDependencies.ts`：安装器与版本 |
| K04 | [java.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/java.ts)、`javaRuntimeHealth.ts`、`javaProbeCache.ts`：运行时 |
| K05 | [download.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/download.ts)、`tasks.ts`、`downloadLimits.ts`、`downloadProgress.ts`：下载与任务 |
| K06 | [accounts.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/accounts.ts)、`yggdrasil.ts`、`yggdrasilProvider.ts`；[提供商格式](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/docs/auth/yggdrasil-provider-card.md) |
| K07 | [modinfo.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modinfo.ts)、`modMetadata.ts`、`modScan.ts`：模组元数据与重复检测 |
| K08 | [modManagement.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modManagement.ts)、`modState.ts`：启停、锁定、版本及前置 |
| K09 | [modUpdates.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modUpdates.ts)、[modTransaction.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modTransaction.ts)：哈希更新与提交/恢复 |
| K10 | [modMigration.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modMigration.ts)：跨 MC 版本迁移 |
| K11 | [worlds.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/worlds.ts)、`resourceFiles.ts`、`defaultResourcePacks.ts`：世界与资源 |
| K12 | [defaultGameOptions.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/defaultGameOptions.ts)、`keybindings.ts`、`src/shared/gameOptions.ts`：游戏选项编码 |
| K13 | [servers.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/servers.ts)：NBT 同步、服务器状态与实例绑定 |
| K14 | [instanceCenter.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/instanceCenter.ts)、`backupStore.ts`、`changeProtection.ts`：备份与保护 |
| K15 | [community.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/community.ts)、`communityPaging.ts`、`modFavorites.ts`：社区统一接口 |
| K16 | [modpacks.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modpacks.ts)、[modpackManualFiles.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modpackManualFiles.ts)、`modpackLocalFiles.ts`、`modpackBundledFiles.ts`：整合包 |
| K17 | [skins.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/skins.ts)：皮肤、披风、档案与历史 |
| K18 | [directConnect.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/directConnect.ts)；[好友直连说明](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/docs/features/friend-direct-connect.md) |
| K19 | [frp.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/frp.ts)、`frpIpc.ts`、`frpLog.ts`、`terracotta.ts`：第三方工具集成 |
| K20 | [voxlink/](https://github.com/YUN-SAN3/KAMUCL/tree/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/voxlink)；[协议同步与验证边界](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/docs/VOXLINK-UPSTREAM.md) |
| K21 | [modBridge.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/modBridge.ts)、[bridge/src/](https://github.com/YUN-SAN3/KAMUCL/tree/30874ab871c8ee00c1edab275bb3aeb25704330b/bridge/src)：本机 Bridge |
| K22 | [appearanceAssets.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/appearanceAssets.ts)、`settings.ts`、`plugins.ts`：外观与扩展 |
| K23 | [diagnosticRules.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/diagnosticRules.ts)、`diagnosticSession.ts`、`diagnostics.ts`、`diagnosticArchive.ts`：诊断与日志 |
| K24 | [applyUpdate.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/applyUpdate.ts)、`selfUpdate.ts`、`macUpdate.ts`：更新事务 |
| K25 | [commandWorld.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/commandWorld.ts)：命令测试世界 |
| K26 | [projectionFormats.ts](https://github.com/YUN-SAN3/KAMUCL/blob/30874ab871c8ee00c1edab275bb3aeb25704330b/src/main/core/projectionFormats.ts)、`projectionsIpc.ts`、`projectionJobs.ts`、`projectionValidation.ts`、`typedNbt.ts`：投影工具 |

### 许可证与能力边界

引用/移植源码时检查 [KAMUCL LICENSE](https://github.com/YUN-SAN3/KAMUCL/blob/main/LICENSE)、[第三方声明](https://github.com/YUN-SAN3/KAMUCL/blob/main/THIRD_PARTY_NOTICES.md) 和具体文件头；不能把整个仓库当成单一 MIT 授权。VoxLink、Terracotta 及其依赖分别核对许可、版本和对应源码义务。此处提供的是实施注意点，不替代正式分发时的许可证核对。

以“已有代码 + 明确测试 + 实际可用入口”评估完成度；真实第三方联调、平台支持与未实现行为单独列出。最终目标是稳定可用的 XMCL，而不是只把 KAMUCL 菜单名字复制一遍。

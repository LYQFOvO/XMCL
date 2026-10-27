# 实例级 Java / 内存设置与智能内存分配

## 背景

旧版本里“内存”只有设置页的一个全局默认值；Java 路径、JVM 参数虽然已经存在实例元数据中，但没有面向用户的实例设置入口。玩家需要在功能面板里手填实例 ID 才能修改，而且无法直观看到“这台电脑应该分配多少内存”。

## 目标

1. 版本配置按实例隔离：每个实例独立保存自己的 Minecraft 版本、加载器、游戏目录、Java、内存与启动参数。
2. 提供实例设置 UI：从实例卡片或首页当前实例直接打开，而不是通用功能面板。
3. 智能内存分配：读取本机物理内存，为系统预留空间后自动给出游戏堆上限建议，并允许一键应用或手动覆盖。

## 实现

### 智能内存算法（`src/services/MemoryAdvisor.h`）

- 物理内存检测：macOS `sysctlbyname("hw.memsize")`、Linux `sysconf(_SC_PHYS_PAGES)`、Windows `GlobalMemoryStatusEx`。
- 可用内存 = 物理内存 − 2 GB（系统与启动器预留）。
- 建议值 = ceil(可用内存 × 40%)，限制在 2–16 GB；允许的最大值 = min(16, 物理内存 − 2)，未知内存时回退 16 GB。
- 16 GB 机器示例：可用 14 GB → 建议 6 GB，滑块上限 14 GB。
- 纯函数 `suggestForTotalGb()` 便于跨平台单元测试；`LauncherController` 暴露 `totalMemoryGb`、`suggestedMemoryGb`、`maxMemoryGb`，并据此约束全局默认内存。

### 后端

- `instance.create`：新实例写入智能建议内存、窗口尺寸与 Java/参数默认值。
- `instance.configure`：可写 `javaPath`、`memoryGb`、`jvmArgs`、`gameArgs`、`width`、`height`；内存校验为 1..本机允许最大值，窗口尺寸有范围校验；错误信息含本机物理内存。
- `instance.get`：读取单个实例配置，供 UI 刷新。
- `GameService::launch`：`-Xmx` 取实例内存，未设置时使用全局默认；两者都被本机允许最大值约束。Java 优先使用实例 `javaPath`，为空时按版本要求自动选择。
- 启动参数与游戏参数只从当前实例元数据读取，互不影响。

### UI

- 新增 `ui/InstanceSettingsDialog.qml`，可从实例卡片右上角「设置」、首页当前实例「实例设置」打开。
- 面板内容：版本/加载器标签（只读，体现版本隔离）、Java 路径 + “扫描本机 Java”、JVM 参数、游戏参数、窗口尺寸、内存滑块与「智能分配」。
- 设置页默认内存增加智能建议提示与「智能分配」按钮，并说明单个实例可覆盖。
- 首页当前实例显示该实例自己的内存值。

## 验收

- 单元测试：智能建议纯函数、控制器上限约束、创建实例默认内存、配置校验（超上限/无效窗口尺寸拒绝）、`instance.get` 回读。
- UI 测试：创建两个不同版本实例，从目标实例卡片打开设置，验证智能分配、Java 路径输入、保存后仅目标实例被修改且版本/加载器不变。
- 全量 `ctest` 12/12；干净构建 12/12。
- 截图：`docs/screenshots/instance-settings.png`、`docs/screenshots/home-instance-memory.png`、`docs/screenshots/settings.png`。

## 限制

- 仅 macOS 实机验证；Linux/Windows 的内存检测代码未实机运行。
- 智能建议是通用起点，模组整合包仍可能需要手动调大；面板中始终可以覆盖。
- 实例的 Minecraft 版本和加载器在安装时确定；本功能负责该实例的 Java、内存与启动参数隔离，不改变已安装版本。

# 批次 A：Java 匹配、启动检查与诊断

依据 `docs/KAMUCL-FEATURES-FOR-DEEPSEEK.md` 的 A 批（F09、F10、F13、F49）实现的最小可用闭环：
选择实例 → 启动检查（Java + 文件）→ 一键修复 → 启动；启动失败后展示基于日志的诊断建议。

## 入口

- 首页「开始游戏」：先打开「启动检查」弹窗（不再直接启动）。
- 「启动检查」弹窗：Java 匹配状态、要求主版本、缺失/大小不符文件、可修复项、继续启动；失败后自动调用诊断并显示建议；可直接打开「实例设置」。
- 实例设置 → Java 运行时：扫描本机 Java、显示该版本要求与匹配结果、自动匹配填入、手动路径。
- 功能面板新增「Java 运行时」模块（java.scan / java.match / java.health）。
- 功能面板「版本与启动」新增 launch.check / launch.repair / diagnostic.analyze。

## 实现文件

- 新增 `src/services/JavaRuntimeService.h/.cpp`
  - 候选来源：JAVA_HOME、PATH、macOS JavaVirtualMachines/Homebrew、Linux /usr/lib/jvm、Windows Program Files/Java 注册表、sdkman/.jdks 等。
  - 探测：`-XshowSettings:properties -version`，4 秒超时，解析 java.version/vendor/os.arch/sun.arch.data.model；结果按路径 + 大小 + mtime 缓存到 `java-runtimes.json`，`refresh` 强制重探。
  - 主版本解析兼容 `1.8.0_x` 与 `17.x`；匹配优先精确主版本，其次最小的较高主版本（明确警告“不保证兼容”）；手工指定 Java 优先，无效或 32 位会给出警告并阻止启动检查通过。
- `GameService` 新增
  - `launch.check`：按版本与继承链构建客户端、库、natives、logging、资源索引/对象的必需文件清单；检查存在性与大小；Java 匹配交给 JavaRuntimeService；返回 issues/fileIssues/fixable。
  - `launch.repair`：只下载缺失或大小不符的文件；资源索引缺失时先修复索引再补齐对象；复用 DownloadEngine（摘要校验、.part、镜像/官方回退）；进度接入下载页任务。
  - `diagnostic.analyze`：读取最近 256KB `logs/game.log` 与 64KB `logs/launcher.log`，按类版本、OOM、缺类/natives、JVM 参数、模组依赖、JVM 崩溃等规则给出证据、建议与设置入口；无匹配时保留日志路径。
  - 启动参数：profile 支持 Quick Play 时使用 `--quickPlayMultiplayer host:port`，否则保留 `--server/--port`；Java 选择优先实例路径，其次 JavaRuntimeService 匹配结果，测试环境回退 PATH。
- 新增 `ui/LaunchCheckDialog.qml`；`InstanceSettingsDialog.qml` 增加 Java 状态/扫描/自动匹配；`ServiceHub` 注册 Java 模块。

## 验证

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH='/opt/homebrew;/opt/homebrew/opt/libarchive' -DBUILD_TESTING=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

结果：13/13 通过（新增 java 套件）。新增夹具覆盖：
- Java 版本解析、fake Java 探测、坏 Java 拒绝、JAVA_HOME 发现、探测缓存与强制刷新、精确/较高版本匹配、继承链主版本、手填 Java 警告。
- 安装夹具（含库与 logging）后删除库文件：launch.check 报告可修复，launch.repair 只补该文件，再次检查通过。
- 日志含 UnsupportedClassVersionError + OutOfMemoryError 时诊断给出 Java 17 与内存建议。
- 版本支持 Quick Play 时启动参数使用 `--quickPlayMultiplayer`。
- UI 测试从首页「开始游戏」打开启动检查：删除客户端 JAR 后显示「需修复」，点击「一键修复」通过真实回环下载补齐文件并重新检查通过，继续启动按钮启用。
- 真实外部联调：`XMCL_LIVE_JAVA=1 ./build/tst_java liveFindsInstalledJava` 在本机发现并精确匹配 Java 17（Microsoft JDK，64 位）；`XMCL_LIVE_OLD_INSTALL=1 ./build/tst_game liveInstallDownloadsRealFiles` 真实安装 Minecraft 1.0 后执行真实启动检查：缺失/大小不符文件均为 0，并匹配到本机 Java。

## 限制（未完成批次 A 的部分）

- F09 的 Java 运行时自动下载（`java.provision`）未实现：当前只匹配本机已安装 Java，并在缺失时提示安装/指定路径。
- JRE 模块级健康检查（如 jdk.unsupported）未实现；目前检查版本、架构、可执行性与超时。
- F10 的文件校验基于存在性/大小，未做全量哈希与 mtime 缓存；未知模组和用户文件不参与检查或修复。
- F49 为有限规则推断，未提供导出脱敏（F50）与自动修复；真实账号下的实际游戏启动与崩溃复现尚未验收。
- Quick Play 仅实现多人服务器；单人或世界入口未实现。

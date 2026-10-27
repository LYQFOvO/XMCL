# 个性化与原生插件

`PersonalizationService` 使用数据根目录下的 `personalization/settings.json`；空主题色、空背景地址表示继续使用现有界面默认值。默认快捷键保持 `home=Ctrl+1`、`library=Ctrl+2`、`downloads=Ctrl+3`、`settings=Ctrl+4`、`features=Ctrl+Shift+M`。`settings()` 返回 `accentColor`、`backgroundUrl`、`shortcuts` 和按实例 ID 索引的 `thumbnails`；`changed()` 通知界面刷新。缩略图和背景均解码后重新编码为 PNG，按内容哈希保存，原图移动后仍可使用。单张源图最大 20 MiB、1600 万像素、单边 8192 像素。实例 ID 只允许字母、数字、下划线和短横线；图片文件无法通过实例 ID 跳出管理目录。重置只清除配置，不删除原始图片或插件。

## 原生 ABI v1

插件是实现 `src/services/NativePlugin.h` 中 `NativePlugin` 接口的 Qt `QObject`，带有 `Q_PLUGIN_METADATA(IID XMCL_NATIVE_PLUGIN_IID FILE "metadata.json")` 和 `Q_INTERFACES(NativePlugin)`。元数据必须包含：

```json
{"id":"org.example.plugin","name":"Example","version":"1.0.0","apiVersion":1}
```

插件必须匹配启动器的 CPU 架构、Qt 主次版本和兼容 C++ 工具链。使用 `QPluginLoader` 加载；ABI 或系统依赖不兼容时报告 Qt 的加载错误。KAMUCL 的 Electron/JavaScript 插件不能安装，也不提供假兼容层。

生命周期：

1. `plugin.install {path}`：检查真实 Qt 插件元数据并复制到 `native-plugins`，记录 SHA-256，不调用插件代码。
2. `plugin.enable {id}`：用户明确确认后校验安装文件并调用 `start(context,error)`；`context.dataDirectory` 是插件自己的持久化目录，`context.apiVersion=1`。启动失败时调用 `stop()`。
3. `plugin.execute {id,command,text}`：仅对本次会话已启用的插件调用 `execute(command,args)`；插件也可以在启用期间注册自身后台功能。
4. `plugin.disable {id}`：同步调用 `stop()`。插件必须取消回调、释放资源并等待工作线程结束；`stop()` 不得抛异常。插件应支持关闭后再次 `start()`。
5. `plugin.remove {id}`：关闭后卸载注册和库文件；保留插件数据。操作系统占用文件时报告遗留路径。

启用状态只在内存保存；启动应用绝不自动运行已安装插件。列出和检查插件也不会调用插件。启用动作带 `confirm=true` 并明确说明：插件属于具有当前用户完整文件与网络权限的本机代码，不在沙箱中运行；错误或恶意插件能够崩溃、挂起或破坏启动器。停止是对插件约定的调用，不能强制隔离本机代码。为避免 Qt 回调跳入已卸载代码，关闭后库仍保持映射直到退出进程；替换已运行过的插件需要先重启启动器。

## 可编译示例与验证

`examples/native-plugin` 中的 Echo 插件只有明确启用后才会响应 `echo`，并返回 `text` 参数。

```sh
cmake -S examples/native-plugin -B build/native-plugin-example -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build/native-plugin-example
```

使用构建生成的 `.so`、`.dylib` 或 `.dll` 执行安装，再明确启用。无需从第三方下载插件来验证 ABI。测试使用临时目录，不接触用户配置：

```sh
XMCL_TEST_PLUGIN="$PWD/build/native-plugin-example/libxmcl_echo.so" ./build/tst_personalization
```

未提供 `XMCL_TEST_PLUGIN` 时，仅跳过动态插件生命周期测试；配置、图片、快捷键、坏元数据等测试仍执行。不要将该环境变量指向来源不明的库，生命周期测试会执行它。

配置文件损坏时回退默认配置；保留损坏文件直到用户保存新设置。管理目录和目标文件拒绝符号链接。插件哈希用于发现安装后的修改，不是签名或可信来源证明。图片旧版本暂时保留，避免撤销/缓存引用失效；未来可在明确清理操作中回收。

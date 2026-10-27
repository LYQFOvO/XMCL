# KAMUCL Bridge

游戏端 Fabric MOD 来自 YUN-SAN3/KAMUCL，固定参考提交 `30874ab871c8ee00c1edab275bb3aeb25704330b`，保留原包名与通信协议。MIT 原始许可在 `../licenses/kamucl/LICENSE`。

构建：使用 JDK 17+ 执行 `python3 bridge/build.py`。仅构建时下载固定 SHA-256 的 Fabric Loader 和 Gson；依赖不打入 MOD。桌面端无 Node/Electron 依赖。

该 MOD 暴露自己注册的参数及演示模块，并不自动支持所有 Minecraft MOD。第三方 MOD 需要接入 Bridge 参数注册接口。

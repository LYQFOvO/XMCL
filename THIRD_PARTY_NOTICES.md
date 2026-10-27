# Third-party notices

- XMCL 的桌面 UI 与 C++ 服务为本项目实现；功能对照来源为 [YUN-SAN3/KAMUCL](https://github.com/YUN-SAN3/KAMUCL)，参考固定提交 `30874ab871c8ee00c1edab275bb3aeb25704330b`。参考源码位于忽略的 build/reference，不是应用运行依赖。
- `bridge/src/` 为该提交中 KAMUCL Bridge 的 MIT 原创 Java 源；原版权与许可保留于 `licenses/kamucl/LICENSE`，构建时随 JAR 打包。修改或分发时保留许可。详见 `bridge/README.md`。
- 微软设备码登录默认使用 KAMUCL 在源码中公开的桌面公共客户端 ID `06cc8cf6-4b8f-4631-8459-b2bd828808f8`（见其 `src/shared/types.ts` 的 `DEFAULT_MS_CLIENT_ID`）。该标识不是密钥，只用于发起微软公共客户端设备码授权；应用注册与 Xbox/Minecraft 服务许可归其维护者。XMCL 在账号面板保留了填写自己 Client ID 的入口，分发或商用前建议换用自己注册的公共客户端。
- Bridge 编译使用 Fabric Loader API 与 Gson；编译依赖不打入本项目 JAR。它们各自许可证适用。
- Qt（含 Quick3D）、libarchive 为链接依赖，各自许可证适用；发布安装包前需要完成对应许可证、源码/重链接义务与运行库部署核对。
- FRP、Terracotta、authlib-injector、Minecraft 与加载器均为外部组件，各自版权和许可证适用。本项目不重新授权这些组件。FRP/Terracotta 二进制由用户提供；authlib-injector 由显式操作从官方源获取。
- BMCLAPI 下载镜像（可选，默认关闭）：文档 <https://bmclapidoc.bangbang93.com/>，维护者 bangbang93。XMCL 仅在用户主动选择“BMCLAPI 镜像”时把 Mojang/Fabric/Forge/NeoForge/authlib-injector 的官方下载地址改写为其公开 API 路由，不内置其源码，也不在其上二次封装其他协议。按文档要求，设置页与下载界面标明来源；镜像文件版权归各源站点，BMCLAPI 本身为非开源服务，使用风险由使用者承担。镜像请求失败时程序会自动改用官方源。
- VoxLink 原生协议尚未移植，没有把其 LGPL 适配代码复制到本项目或作为已完成功能发布。
- 背景来源与生成记录见 `assets/ARTWORK.md`。

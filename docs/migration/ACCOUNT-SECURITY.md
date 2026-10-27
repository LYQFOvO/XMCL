# 微软设备码登录的安全边界

本文说明 XMCL 的微软（Minecraft Java Edition）登录会接触什么、不会接触什么，以及仍然存在的风险。

## 授权流程

1. XMCL 用 Microsoft OAuth 2.0 **设备码（device code）** 流程向 `login.microsoftonline.com/consumers/oauth2/v2.0/devicecode` 申请设备码。
2. 启动器显示 `user_code` 和官方登录地址；玩家在微软页面输入代码并批准。
3. 启动器轮询 token 端点，拿到 Microsoft access/refresh token 后依次换取 Xbox Live、XSTS 和 Minecraft 访问令牌。
4. 只请求 `XboxLive.signin offline_access` 范围：用于 Xbox Live / Minecraft 身份与离线刷新，不涉及邮箱、通讯录、文件、支付等能力。

启动器**从不接触微软账号密码**，也无法绕过微软的多重验证；密码只在微软自己的登录页输入。

## 本项目的保护措施（代码现状）

- 内置 Client ID 是公共客户端标识，不是密钥；二进制中没有 client secret。可在登录表单换成自己的 Microsoft OAuth 应用。
- 认证端点固定为官方 HTTPS；认证请求禁用自动重定向（防止把凭据转发到其他主机）；自定义端点只供测试注入，未暴露给 QML。
- macOS：access/refresh token 写入系统钥匙串（`com.xmcl.launcher.credentials`），`accounts.json` 只保存昵称、UUID、账号类型与公开档案信息；测试会断言 `accounts.json` 不含令牌。
- 令牌不进入通用 QML 面板、`launcher.log` 或错误提示；只有 C++ 内部在启动游戏时把 Minecraft access token 交给游戏进程。
- 设备码过期、`slow_down` 退避、429/5xx 有限重试、取消递增 generation 丢弃迟到响应，避免重复/串号登录。
- 刷新失败保留账号记录并提示重新登录，不会删除账号；移除账号会删除钥匙串条目。
- Windows/Linux 未接入系统凭据库时**拒绝明文保存令牌**（功能未完成，而不是降级为明文）。

## 仍然存在的风险

1. **设备码钓鱼（流程固有）**：攻击者先发起登录，再诱导你在微软页面输入他生成的代码并批准；之后攻击者获得该账号的 Minecraft/Xbox 令牌。这不是密码泄露，但会获得账号访问权。防御：只输入本机启动器刚刚为你生成的代码；任何人索要设备码都拒绝；在微软授权页确认应用名称后再批准。UI 已加入该提示。
2. **共享公共 Client ID 的信任边界**：内置 Client ID 属于 KAMUCL 的 Azure 应用（公开标识，本身不是密钥，应用所有者不能直接读取你本机保存的令牌），但应用注册、名称、配置由第三方维护；微软可能因滥用/政策禁用该应用，且授权页显示的是第三方应用名称。正式分发建议注册自己的 Azure 公共客户端（启用设备码流程），或在登录表单填自己的 Client ID。
3. **本机令牌安全**：钥匙串条目通常受当前用户与 Keychain 访问控制保护，但同一用户权限下的恶意程序可能读取钥匙串/进程内存，或触发授权提示。游戏启动时 access token 以 `--accessToken` 命令行参数传给 Java（Minecraft 启动协议要求），本机其他进程可看到该进程参数；JVM 崩溃报告也可能记录完整命令行。这属于所有同类启动器的共同残余风险。
4. **refresh token 生命周期**：`offline_access` 刷新令牌有效期较长。若泄露，攻击者可长期获取 Minecraft 访问权，直到令牌失效或被撤销。怀疑泄露时应在微软账号安全页退出所有设备/更改密码，并移除本机账号。
5. **第三方 Yggdrasil 提供商**：密码与令牌会发送给玩家自行配置的提供商；其安全性由该提供商决定。
6. **Windows/Linux 功能缺口**：没有系统凭据库后端时在线账号不能持久化，登录会报错；不会明文落盘，但也还没有“仅本次会话”的替代登录方式。

## 建议

- 正式发布前替换为自己的 Microsoft OAuth 应用（公共客户端 + 设备码授权），不要长期依赖第三方 Client ID。
- 保持钥匙串存储；如需更强保护，可后续增加 `kSecAccessControlUserPresence`（代价是刷新时可能提示用户）。
- 在设备码/登录界面持续保留防钓鱼提示；不要提供“粘贴别人代码”之类的入口。
- 玩家侧：开启微软账号两步验证，定期在微软账号安全页检查登录设备；不要使用来源不明的启动器或把设备码发给他人。

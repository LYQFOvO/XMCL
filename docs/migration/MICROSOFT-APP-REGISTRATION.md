# 自己申请微软设备码登录用的 OAuth 应用（Client ID）

## 先分清两件事

- **设备码不需要申请**：`device_code` / `user_code` 是每次点击登录时由微软 `POST /devicecode` 动态生成的，约 15 分钟有效，不需要提交材料审批。
- **需要申请的是一个 Microsoft Entra OAuth 应用（Client ID）**：它代表“哪个启动器在请求授权”，会显示在微软授权页上，也是令牌的受众应用。设备码流程是公共客户端，**不需要 client secret**。

## 在 Azure / Entra 门户创建应用

1. 用普通微软账号登录 [Microsoft Entra 管理中心](https://entra.microsoft.com/)（Azure AD 免费层足够，不需要付费订阅；个人 Microsoft 账号也可以）。
2. 进入 **应用注册（App registrations）→ 新注册（New registration）**。
3. 填写：
   - **名称**：显示在微软授权页，例如 `MyLauncher`。
   - **支持的账户类型**：必须支持个人 Microsoft 账户，选择
     **“任何组织目录中的账户和个人 Microsoft 账户”** 或 **“仅个人 Microsoft 账户”**。
     - 本项目固定使用 `login.microsoftonline.com/consumers/...` 且申请 `XboxLive.signin`；单租户/仅组织账户的应用不可用。
   - **重定向 URI**：设备码流程**不需要**重定向 URI，可留空。不要选“Web”平台去填一个用不到的地址。
4. 注册后复制 **应用程序（客户端）ID**（GUID）。**不要**创建客户端密码/证书——设备码是公共客户端，创建 secret 反而增加泄露面。
5. 开启“允许公共客户端流”（设备码登录必需）：
   - 在**应用注册列表里点进你刚创建的应用**（不是“新注册”页面）→ 左侧「管理」分组 → **身份验证**。
   - 新版门户可能显示为 **Authentication (Preview)**（中文界面仍是英文条目），这就是原来的「身份验证」页面；点它，不是「证书和密码」。
   - 页面拉到最底部 → **高级设置（Advanced settings）** → **允许公共客户端流（Allow public client flows）** → 选择 **是 / Yes** → 顶部 **保存（Save）**。
   - 注意：它不在「证书和密码」里，也不是「隐式授权 / 混合流」那两个复选框；不要勾隐式授权。
   - 如果中文/新版门户中没有这一项，用下面的清单兜底法。
   
   **兜底：清单（Manifest）方式**
   1. 同一个应用 → 左侧「管理」→ **清单（Manifest）**（截图里也有这一项）。
   2. 搜索 `allowPublicClient`，把值改为 `true`（原来是 `null` 或 `false` 都改成 `true`）：
      ```json
      "allowPublicClient": true,
      ```
   3. 点顶部 **保存**，再回到「身份验证」刷新，就能看到“允许公共客户端流 = 是”。
   - 未开启时设备码换令牌会返回 `invalid_client` / `unauthorized_client` 一类错误。

   深链接（把 `<APP_ID>` 换成你的 Client ID，登录后可直接打开对应页面）：
   `https://entra.microsoft.com/#view/Microsoft_AAD_RegisteredApps/ApplicationMenuBlade/~/Authentication/appId/<APP_ID>`
6. API 权限通常不需要手工添加 `XboxLive.signin`，本项目会在运行时请求 `XboxLive.signin offline_access`；`offline_access` 是 OAuth 标准范围，同意后返回 refresh token。若你的租户门户里能看到 Xbox Live API，也可以把 `XboxLive.signin` 加为委托权限，但最终以运行时请求结果为准。

## Minecraft 专用的一步（必须申请，容易漏）

新建的应用只完成 Microsoft/Xbox 授权还不够：Minecraft 服务端对调用 `api.minecraftservices.com` 的第三方应用实行 **allow list 审核制**。官方说明见 [Java Edition Game Service API Review or Application Process](https://help.minecraft.net/hc/en-us/articles/16254801392141-Java-Edition-Game-Service-API-Review-or-Application-Process)：现有应用继续可用，**新应用必须通过 [AppID Review 表单](https://aka.ms/mce-reviewappid)申请**，由 Mojang/Microsoft 人工审核后加入 allow list；未获授权时 Minecraft Services 返回 **403 / Invalid app registration**。

### 官方表单要求（据表单页面）

1. 已阅读并同意 Minecraft EULA 与 Usage Guidelines（<https://aka.ms/mcusageguidelines>）。
2. **联系邮箱**：需与 Azure Portal 中能核对到的账号信息一致。
3. 请求类型：`New AppID for Approval`（新应用）或 `Existing AppID for Review/Report`。
4. **Application Name**：不得包含 Mojang、Minecraft、Microsoft、Live、Xbox、Discord、Hypixel 等字样。
5. **Application (Client) ID**：GUID，一次提交只填一个；新应用必须填写。
6. **Directory (Tenant) ID**：GUID，新应用审批必需（在 Entra「概述」页可复制）。
7. 应用/网站/品牌的公开链接（GitHub 仓库、官网等），用于核实。
8. **Justification**：应用简介、需要 API 的原因；没有有效理由的提交不会被审核。
9. 补充信息（仅报告可疑应用时可选）。

### 审核关注点

- 应用必须**验证用户游戏所有权**，并在进入游戏/服务器前要求认证；不得绕过或禁用认证、所有权/许可检查或其他安全特性。
- 不得用于钓鱼、账号窃取、绕过 Xbox/Minecraft 安全机制等行为。
- 审核每周进行，重复提交不会加快；结果不保证通过，政策可能变化。
- 申请的是**这个 Client ID 对应的应用**（不是单个玩家账号）；通过后使用你 Client ID 的启动器用户才能正常访问 Minecraft 服务。

### 实用建议

- 先完成应用注册（个人账户 + 公共客户端流），拿到同一租户的 Client ID 与 Tenant ID，再提交一次表单。
- 表单里写清楚：启动器名称、是否开源/下载来源、如何做 Microsoft 授权与所有权校验、是否有服务器/联机功能、用户数据与令牌存储方式（例如系统钥匙串、无明文令牌）。诚实的说明比“仅个人使用”更容易被理解，但仍以官方审核为准。
- 名称不要蹭 Minecraft/Mojang/Microsoft 商标；`XMCL` 这类独立名称可以。
- 审核期间/未通过时，只能用已经获授权的公共 Client ID 做本地试验；不要把它当成自己的应用分发，也不要在已获授权的 ID 上叠加违规功能。
- 有疑问可联系 `enforce@minecraft.net`（官方文章给出的联系方式）。

## 在 XMCL 里使用

1. 打开「账号与衣柜」→「微软设备码登录」。
2. 在 **Microsoft OAuth Client ID** 字段填入上一步的 GUID，执行登录；默认字段值是内置公共客户端，可以覆盖。
3. 首次登录时微软页面会显示你注册的应用名称；确认后批准。
4. 刷新账号时使用该账号保存的 Client ID；换回默认后旧账号仍保留原应用身份，需要重新登录才会切换。

如果要把自己的 Client ID 作为新默认值（发布用途），修改：

```cpp
// src/services/AccountService.cpp
constexpr auto kDefaultMicrosoftClientId = "你的-Client-ID";
```

同时更新 `THIRD_PARTY_NOTICES.md` 中的说明，并重新构建。若仍长期借用别人的 Client ID，应明确告知用户授权页显示的是第三方应用。

## 关于 onmicrosoft.com / 工作学校账号

`xxx.onmicrosoft.com` 默认是 Microsoft Entra ID（原 Azure AD）租户的初始域名，这类账号是**工作/学校账号**，不是个人 Microsoft 账户：

- 本项目固定使用 `login.microsoftonline.com/consumers/...` 且申请 `XboxLive.signin`，按微软与 Minecraft 的认证要求，**只能使用个人 Microsoft 账户**（outlook.com / hotmail.com / live.com，或你自己注册的个人账户）。
- 用工作/学校账号登录时，微软通常返回 `AADSTS50020` / `AADSTS50059` / “does not exist in tenant 'Microsoft Accounts'” 一类错误；启动器现在会直接提示“这是工作/学校 Microsoft 账户……请改用个人 Microsoft 账户”。
- 创建 Azure 应用租户时生成的“租户用户”也不能用来登录 Minecraft；申请应用操作用工作账号登录 Azure 门户可以，但**玩家登录**仍要用个人 Microsoft 账户。
- Minecraft Java Edition 的购买/所有权也必须绑定在个人 Microsoft 账户上；工作账号/教育账号不适用于 Java Edition 的消费级登录。

## 常见错误对照

| 现象 | 可能原因 |
| --- | --- |
| `invalid_client` / `unauthorized_client` | Client ID 不是 GUID、应用不支持个人账户、未开启“允许公共客户端流” |
| `invalid_scope` | scope 拼写不对，或应用不支持个人账户；代码固定请求 `XboxLive.signin offline_access` |
| `AADSTS7000218` / 提示 client secret | 应用被当成机密客户端：**不要创建 secret**，开启公共客户端流 |
| 提示“工作/学校 Microsoft 账户” | 使用了 `xxx.onmicrosoft.com` 等 Entra 工作/学校账号；请改用个人 Microsoft 账户 |
| Xbox 授权失败 | 账号未创建 Xbox 档案、儿童账号限制、区域限制（本项目已有对应中文提示） |
| `api.minecraftservices.com` 403 / Invalid app registration | 未通过 Minecraft API 权限审核，或应用未被授权访问 Minecraft 服务 |
| 同意页显示“未验证的发布者” | 应用未做发布者验证；不影响基本登录，但用户需要确认应用名 |

## 安全提醒

- Client ID 是公开标识，可以出现在开源代码里；**client secret 不能**，设备码流程也不需要它。
- 不要把自己的应用借给未知第三方使用，也不要把用户设备码发给别人——设备码钓鱼可以把账号授权骗走。
- 审核通过不等于可以绕过 Minecraft 使用准则/商标要求；分发前核对条款。

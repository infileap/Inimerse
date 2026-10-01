# GitHub / Bilibili 账号关联

引擎身份模块提供四个 builtin：

- `oauth_config(provider, client_id, redirect_uri)` 保存非敏感 OAuth 应用配置。
- `oauth_authorize(provider, state)` 生成授权跳转地址。目前 GitHub 使用官方 OAuth Apps 地址；Bilibili 使用登录跳转地址，具体开放平台回调需由应用配置提供。
- `oauth_bind(provider, access_token, user_id, display_name, avatar)` 保存公开资料，并用 Windows DPAPI 加密 access token（`userdata/oauth_<provider>.bin`）。
- `oauth_unbind(provider)` 删除本地令牌和关联资料。
- `oauth_status(provider)` 返回已绑定的公开资料 JSON；未绑定时返回空字符串。

GitHub 应用需在 GitHub Developer Settings 中配置回调地址；授权后由桌面层交换 code，再调用 `oauth_bind`。Client secret 不写入仓库，也不写入 profile.json。

官方参考（Bilibili 的 OAuth 端点由开放平台应用类型决定，不能硬编码通用 token 地址）：

- https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps
- https://docs.github.com/en/rest/users/users#get-the-authenticated-user
- https://openhome.bilibili.com/（Bilibili 开放平台申请与回调配置）

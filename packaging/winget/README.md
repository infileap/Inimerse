# Winget 发布

`infileap.inimerse/0.4.0/` 已按 Winget 要求拆分为 version、defaultLocale、installer 三个 YAML 文件。
当前仍是待发布模板，不属于已生成的 0.4.0 发行资产。正式发布前：

1. 生成真实 Windows 安装器并创建对应 GitHub Release。
2. 将 installer 文件中的下载 URL 和 `InstallerSha256` 更新为实际 Release。
3. 运行 `python3 tools/release_verify.py ... --winget-dir ...`、`winget validate` 和 `winget install --manifest`。
4. 提交到 `microsoft/winget-pkgs`，等待官方 CI 审核。

Linux 的 `.deb`、`.tar.gz` 和 `.zip` 仍由 CPack 与 GitHub Release 工作流生成。

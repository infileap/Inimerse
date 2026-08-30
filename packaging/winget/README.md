# Winget 发布

`infileap.inimerse/0.4.0/` 已按 Winget 要求拆分为 version、defaultLocale、installer 三个 YAML 文件。正式发布前：

1. 将 installer 文件中的下载 URL 和 `InstallerSha256` 更新为实际 Release。
2. 运行 `winget validate` 和 `winget install --manifest` 验证安装、升级和卸载。
3. 提交到 `microsoft/winget-pkgs`，等待官方 CI 审核。

Linux 的 `.deb`、`.tar.gz` 和 `.zip` 仍由 CPack 与 GitHub Release 工作流生成。

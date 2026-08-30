# Winget 发布

`infileap.inimerse.yaml` 是提交到 `microsoft/winget-pkgs` 的 V0.4 模板。正式发布前：

1. 将 `PackageVersion`、下载 URL 和 `InstallerSha256` 更新为实际 Release。
2. 按 Winget manifest schema 拆成 `version`、`defaultLocale`、`installer` 三个 YAML 文件（当前模板用 `---` 分隔）。
3. 运行 `winget validate` 和 `winget install --manifest` 验证安装、升级和卸载。
4. 提交到 `microsoft/winget-pkgs`，等待官方 CI 审核。

Linux 的 `.deb`、`.tar.gz` 和 `.zip` 仍由 CPack 与 GitHub Release 工作流生成。

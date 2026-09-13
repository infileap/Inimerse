# 发布产物验证

使用 `tools/release_verify.py` 在上传 GitHub Release 前检查 Linux 发行物：

```bash
python3 tools/release_verify.py build-local --version 0.4.0 \
  --require-wasm --require-deb-python3
```

只有生成真实 Windows 安装器并把实际 SHA-256 写入 Winget manifest 后，才追加：

```bash
python3 tools/release_verify.py build-local --version 0.4.0 \
  --require-wasm --require-deb-python3 \
  --winget-dir packaging/winget/infileap.inimerse/0.4.0
```

检查项：

- `tar.gz`、`zip`、`deb` 三个版本化文件齐全；
- `SHA256SUMS` 存在且每个文件摘要匹配；
- Linux `tar.gz`/`zip` 包含 `bin/inimerse`、`bin/inim` 且保留 Unix 可执行位；
- 使用 `--require-wasm` 时，Linux `tar.gz`/`zip` 还必须包含 `share/inimerse/wasm_probe.wasm`；
- 使用 `--require-wasm` 时，DEB 还必须包含对应的 `/usr/bin` 入口和 WASM probe；
- 使用 `--require-deb-python3` 时，DEB 必须声明 `Depends: python3`；
- 指定 Winget manifest 的版本字段一致，且不含 `REPLACE_WITH_RELEASE_SHA256` 占位符。

该工具只做本地验证，不创建、删除或上传 Release，也不替代 GitHub Actions 的构建测试。
它会拒绝 `REPLACE_WITH_RELEASE_SHA256` 等 Winget 摘要占位符。

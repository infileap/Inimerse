# 发布产物验证

使用 `tools/release_verify.py` 在上传 GitHub Release 前检查 Linux 发行物：

```bash
python3 tools/release_verify.py build --version 0.4.0 \
  --winget-dir packaging/winget/infileap.inimerse/0.4.0
```

检查项：

- `tar.gz`、`zip`、`deb` 三个版本化文件齐全；
- `SHA256SUMS` 存在且每个文件摘要匹配；
- 指定 Winget manifest 的版本字段一致。

该工具只做本地验证，不创建、删除或上传 Release，也不替代 GitHub Actions 的构建测试。

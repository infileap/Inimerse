# GitHub 发布操作集（infileap/inimerse）

本文件是 Inimerse 的持久化发布手册。切换对话后，先读取本文件，再执行发布操作。默认发布仓库为 `infileap/inimerse`，默认目标分支为 `main`。0.4.0 的 CI 发布 workflow 只响应 `v0.4.0` tag。

## 当前授权与默认值

用户已授权执行完整远程发布流程：提交变更、推送 `main`、创建并发布 GitHub Release、上传构建产物。默认版本为 `0.4.0`（标签 `v0.4.0`），Release 类型为正式版，说明使用本手册第 4 节模板，产物为已通过门禁的 Linux `tar.gz`、`zip` 和 `deb` 三种包。Windows 安装器和 Winget 暂不属于 0.4.0 发布资产。除非用户另行指定，不需要再次询问这些选项；不要索取或记录 GitHub Token。

## 1. 发布前门禁

在仓库根目录执行：

```powershell
$ErrorActionPreference = 'Stop'
$repo = 'infileap/inimerse'
$version = '0.4.0'       # 每次发布只修改这一处
$tag = "v$version"

gh auth status
gh repo view $repo
git fetch origin --tags
git status --short
git diff --check

# Linux 构建（Windows 主机通过 WSL）
wsl.exe -d Ubuntu-26.04 -- bash -lc "cd /mnt/d/inimerse_stable && cmake -S . -B build -DINIMERSE_BUILD_ENGINE=ON -DINIMERSE_PACKAGE_VERSION=$version && cmake --build build -j2 && ctest --test-dir build --output-on-failure && cmake --build build --target package && find build -maxdepth 1 -type f -name 'inimerse-*' -print0 | sort -z | xargs -0 sha256sum > build/SHA256SUMS && python3 tools/release_verify.py build --version $version --require-wasm --require-deb-python3"
```

工作树中若有未提交的预期变更，应先提交并推送；不要在发布过程中使用 `git reset --hard` 或清理用户文件。

## 2. 生成并核对校验和

```powershell
$artifacts = @(
  "build/inimerse-$version-Linux-x86_64.tar.gz",
  "build/inimerse-$version-Linux-x86_64.zip",
  "build/inimerse-$version-Linux-x86_64.deb"
)
Get-FileHash $artifacts -Algorithm SHA256 | Format-Table -AutoSize
```

也可在 WSL 中使用：

```bash
sha256sum build/inimerse-<version>-Linux-x86_64.{tar.gz,zip,deb}
```

## 3. 提交、打标签并推送

```powershell
git add -A
git commit -m "Release v$version"
git push origin main
git tag -a $tag -m "Inimerse $tag"
git push origin $tag
```

如果提交和标签已经存在，先确认内容和指向是否正确：

```powershell
git show --stat $tag
git ls-remote --tags origin $tag
```

## 4. 创建 GitHub Release 并上传产物

使用仓库中的 0.4.0 发布范围说明，并显式上传三个安装包和 `SHA256SUMS`：

```powershell
$notes = Get-Content -LiteralPath "docs/RELEASE_0.4.0.md" -Raw
$notes += "`n## SHA-256`n`n````text`n"
$notes += (Get-Content -LiteralPath "build/SHA256SUMS" -Raw)
$notes += "````n"

$notesFile = Join-Path $env:TEMP "inimerse-$version-release.md"
Set-Content -LiteralPath $notesFile -Value $notes -Encoding UTF8

gh release create $tag `
  build/inimerse-$version-Linux-x86_64.tar.gz `
  build/inimerse-$version-Linux-x86_64.zip `
  build/inimerse-$version-Linux-x86_64.deb `
  build/SHA256SUMS `
  --repo $repo `
  --title "Inimerse $tag" `
  --notes-file $notesFile
```

正式版不要使用 `--prerelease`；候选版可加上该选项。首次发布前可先用 `gh release create ... --draft` 生成草稿，核对页面后再发布。

## 5. 发布后复核

```powershell
gh release view $tag --repo $repo
gh release view $tag --repo $repo --json tagName,isDraft,isPrerelease,assets,url
git status --short
```

确认标签、Release 状态和三个 asset 均存在，下载后再次执行 SHA-256 校验。若发现包错误，优先删除并重建同一标签对应的 Release asset；不要强制移动已公开标签，除非明确记录并通知用户。

## 6. 对话交接最小信息

每次发布完成后，在对话或提交说明中记录：

1. 版本号和标签（例如 `0.4.0 / v0.4.0`）。
2. CTest 结果和构建平台。
3. 三个 asset 文件名及 SHA-256。
4. GitHub Release URL。
5. 尚未完成的路线图项目（以 `docs/ROADMAP_0.3_AUDIT.md` 为准）。

当前仓库的路线图审计仍明确记录：真实 UDP NAT 打洞/中继、完整跨端编排、真正语法高亮及更广泛的端到端签名覆盖尚未完全交付；发布说明不得将这些项目描述为已完成。

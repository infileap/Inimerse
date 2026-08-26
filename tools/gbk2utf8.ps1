# gbk2utf8.ps1 - convert GBK file to UTF-8 (no BOM). usage: powershell -File gbk2utf8.ps1 <src> <dst>
param([string]$src, [string]$dst)
$gbk = [Text.Encoding]::GetEncoding(936)
$u8 = New-Object System.Text.UTF8Encoding($false)
$bytes = [IO.File]::ReadAllBytes($src)
$text = $gbk.GetString($bytes)
[IO.File]::WriteAllText($dst, $text, $u8)
Write-Output ("ok {0} -> {1} ({2} bytes)" -f $src, $dst, (Get-Item $dst).Length)

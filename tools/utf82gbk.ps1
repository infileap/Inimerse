# utf82gbk.ps1 - convert UTF-8 (no BOM) file to GBK. usage: powershell -File utf82gbk.ps1 <src> <dst>
param([string]$src, [string]$dst)
$gbk = [Text.Encoding]::GetEncoding(936)
$u8 = New-Object System.Text.UTF8Encoding($false)
$text = [IO.File]::ReadAllText($src, $u8)
[IO.File]::WriteAllText($dst, $text, $gbk)
Write-Output ("ok {0} -> {1} ({2} bytes)" -f $src, $dst, (Get-Item $dst).Length)

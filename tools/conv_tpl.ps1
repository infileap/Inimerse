# conv_tpl.ps1 - convert template files UTF-8 -> GBK
$ErrorActionPreference = "Stop"
$u8 = New-Object System.Text.UTF8Encoding($false)
$gbk = [Text.Encoding]::GetEncoding(936)
New-Item -ItemType Directory -Force -Path "C:\Users\Lenovo\Infiverse\templates" | Out-Null
foreach ($n in @("main.tpl","net.tpl","manifest.tpl","README.tpl")) {
    $src = "D:\inimerse_stable\templates\$n"
    $text = $u8.GetString([System.IO.File]::ReadAllBytes($src))
    [System.IO.File]::WriteAllText($src, $text, $gbk)
    [System.IO.File]::WriteAllText("C:\Users\Lenovo\Infiverse\templates\$n", $text, $gbk)
    "converted $n"
}
"done"
# check_enc.ps1 - detect encoding status of one or more files (no Chinese literals inside)
# usage: powershell -File check_enc.ps1 -paths file1,file2,...   or   -dir folder [-filter *.md]
param([string[]]$paths, [string]$dir, [string]$filter = "*")
function Test-File($f) {
  try {
    $bytes = [IO.File]::ReadAllBytes($f)
  } catch { Write-Output ("{0}`tREAD_ERR`t{1}" -f $f, $_.Exception.Message); return }
  $isUtf8 = $true
  try { $u8 = New-Object System.Text.UTF8Encoding($false, $true); [void]$u8.GetString($bytes) } catch { $isUtf8 = $false }
  $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
  # GBK decode FFFD count (sample: full)
  $fffd = 0
  try {
    $gbk = [Text.Encoding]::GetEncoding(936)
    $s = $gbk.GetString($bytes)
    $fffd = ([regex]::Matches($s, [char]0xFFFD)).Count
  } catch { $fffd = -1 }
  # CRLF / LF mix detection
  $crlf = 0; $lf = 0
  for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($bytes[$i] -eq 0x0A) { $lf++ }
  }
  for ($i = 0; $i -lt $bytes.Length - 1; $i++) {
    if ($bytes[$i] -eq 0x0D -and $bytes[$i+1] -eq 0x0A) { $crlf++ }
  }
  $mixed = ($crlf -gt 0 -and $lf -gt $crlf)
  Write-Output ("{0}`tsize={1}`tutf8={2}`tbom={3}`tgbkFFFD={4}`tcrlf={5}`tlfOnly={6}`tmixedEOL={7}" -f $f, $bytes.Length, $isUtf8, $hasBom, $fffd, $crlf, ($lf - $crlf), $mixed)
}
if ($dir) {
  Get-ChildItem -Path $dir -Filter $filter -Recurse -File | ForEach-Object { Test-File $_.FullName }
} else {
  foreach ($p in $paths) { Test-File $p }
}

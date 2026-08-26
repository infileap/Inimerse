# read_gbk.ps1 - dump a GBK file as UTF-8 to stdout (safe for pipes/Node consumption)
# usage: powershell -File read_gbk.ps1 <file> [lineStart] [lineCount]
param([string]$file, [int]$from = 1, [int]$count = 100000)
$gbk = [Text.Encoding]::GetEncoding(936)
$text = $gbk.GetString([IO.File]::ReadAllBytes($file))
$lines = $text -split "`r?`n"
$end = [Math]::Min($from + $count - 1, $lines.Length)
for ($i = $from - 1; $i -lt $end; $i++) {
  Write-Output ("{0}: {1}" -f ($i + 1), $lines[$i])
}

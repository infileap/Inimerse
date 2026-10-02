$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
function Measure-CpuRun($label, $scriptName) {
  $outFile = 'cpu_out_' + $scriptName + '.txt'
  $p = Start-Process -FilePath 'D:\inimerse_stable\inimerse.exe' -ArgumentList $scriptName -PassThru -RedirectStandardOutput $outFile
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while (-not $p.HasExited) {
    Start-Sleep -Milliseconds 100
    try { $p.Refresh() } catch {}
  }
  $sw.Stop()
  $cpu = $p.TotalProcessorTime.TotalSeconds
  $wall = $sw.Elapsed.TotalSeconds
  $cores = [Environment]::ProcessorCount
  $single = $cpu / $wall * 100
  Write-Host ("{0,-12} wall={1,6:N2}s cpu={2,5:N2}s 等效单核={3,5:N1}% (本机{4}核)" -f $label, $wall, $cpu, $single, $cores)
  if (Test-Path $outFile) { Get-Content $outFile | Where-Object { $_ -match 'done|ALL|n=' } | ForEach-Object { Write-Host ('    ' + $_.Trim()) } }
}
Measure-CpuRun "cpu1" "cpu1.im"
Measure-CpuRun "cpu8" "cpu8.im"
Measure-CpuRun "cpu8s" "cpu8s.im"
$ErrorActionPreference = 'Continue'
$st = @{ '200'='es_200.im'; '400'='es_400.im'; '800'='es_800.im'; '1600'='es_1600.im' }
foreach ($k in @('200','400','800','1600')) {
  $p = Start-Process -FilePath 'D:\inimerse_stable\inimerse.exe' -ArgumentList $st[$k] -PassThru -RedirectStandardOutput ('es_out_' + $k + '.txt')
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  while (-not $p.HasExited) { Start-Sleep -Milliseconds 100; try { $p.Refresh() } catch {} }
  $sw.Stop()
  $cpu = $p.TotalProcessorTime.TotalSeconds
  $wall = $sw.Elapsed.TotalSeconds
  $core = $cpu / $wall * 100
  Write-Host ("{0,5}精灵  wall={1,5:N2}s  cpu={2,4:N2}s  等效单核={3,4:N0}%" -f $k, $wall, $cpu, $core)
  $o = Get-Content ('es_out_' + $k + '.txt') -ErrorAction SilentlyContinue
  $o | Where-Object { $_ -match 'created|MOVE_START|MOVE_END|fail' } | ForEach-Object { Write-Host ('    ' + $_.Trim()) }
}
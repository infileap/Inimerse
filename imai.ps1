# imai.ps1 - 本地 AI 开发助手（Qwen2.5-7B via Ollama, /api/chat）
param([string]$Q = '', [string]$Ctx = '')
$ErrorActionPreference = 'Stop'
$OLLAMA = 'http://127.0.0.1:11434'
if ($Q -eq '') { Write-Host '用法: imai "问题" [--ctx 文件:行号]'; exit 0 }
$SYS = [string](Get-Content -Raw 'D:\inimerse_stable\imai_sys.txt')
$ctxBlock = ''
if ($Ctx -ne '') {
    $parts = $Ctx -split ':'
    $cf = $parts[0]; $ln = [int]$parts[1]
    if (Test-Path $cf) {
        $start = [Math]::Max(1, $ln - 4); $end = $ln + 4
        $lines = Get-Content $cf | Select-Object -Skip ($start - 1) -First ($end - $start + 1)
        $ctxBlock = "`n[源码上下文 " + $cf + " 第" + $ln + "行]:`n" + ($lines -join "`n")
    }
}
$userMsg = $Q + $ctxBlock
$mSys = @{ role = 'system'; content = $SYS } | ConvertTo-Json -Compress
$mUsr = @{ role = 'user'; content = $userMsg } | ConvertTo-Json -Compress
$body = '{"model":"Qwen2.5-7B-Instruct","stream":false,"messages":[' + $mSys + ',' + $mUsr + '],"options":{"num_predict":400,"temperature":0.3}}'
Write-Host '[imai] 提问中...'
$r = Invoke-RestMethod -Uri ($OLLAMA + '/api/chat') -Method Post -Body $body -ContentType 'application/json' -TimeoutSec 600
Write-Host ''
Write-Host $r.message.content

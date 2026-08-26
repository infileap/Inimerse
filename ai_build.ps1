# ai_build.ps1 - 构建 + 失败时 AI 解释（本地 Qwen）
$ErrorActionPreference = 'Continue'
Set-Location D:\inimerse_stable
Write-Host '[ai_build] 构建中...'
$log = & powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 2>&1 | Out-String
if ($LASTEXITCODE -eq 0) {
    Write-Host 'BUILD OK'
    exit 0
}
Write-Host 'BUILD FAILED - 收集错误并请求 AI 分析...'
$errLines = $log -split "\r?\n" | Where-Object { $_ -match 'error:' } | Select-Object -First 8
if ($errLines.Count -eq 0) { Write-Host $log; exit 1 }
$errBlock = ($errLines -join "\n")
$ctxBlock = ''
if ($errLines[0] -match '([A-Za-z]:\[^:]+):(\d+):') {
    $cf = $Matches[1]; $ln = [int]$Matches[2]
    if (Test-Path $cf) {
        $start = [Math]::Max(1, $ln - 3); $end = $ln + 3
        $lines = Get-Content $cf | Select-Object -Skip ($start - 1) -First ($end - $start + 1)
        $ctxBlock = "\n[源码上下文] " + $cf + " 第" + $ln + "行:\n" + ($lines -join "\n")
    }
}
$prompt = "你是 Inimerse 引擎 C11 开发者。以下是一次 gcc 构建的错误输出，请用中文简洁解释每个错误的根因，并给出具体修复（文件+行号+代码片段）。注意：源码是 GBK 混合编码，修改补丁必须 GBK 无损。\n\n错误输出:\n" + $errBlock + $ctxBlock
$body = @{ model = 'Qwen2.5-7B-Instruct'; stream = $false; prompt = $prompt; options = @{ num_predict = 2000; temperature = 0.2 } } | ConvertTo-Json -Compress
try {
    $r = Invoke-RestMethod -Uri 'http://127.0.0.1:11434/api/generate' -Method Post -Body $body -ContentType 'application/json' -TimeoutSec 600
    Write-Host ''
    Write-Host $r.response
} catch {
    Write-Host 'AI 调用失败:'
    Write-Host $errBlock
}
exit 1

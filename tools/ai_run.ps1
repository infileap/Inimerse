# ai_run.ps1 - run an .im script in the AI sandbox (safe + structured errors)
# usage:  powershell -File D:\inimerse_stable\tools\ai_run.ps1 <script.im> [-Timeout N] [-Unsafe]
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [int]$Timeout = 10,
    [switch]$Unsafe
)
$exe = if ($env:INIMERSE_EXE) { $env:INIMERSE_EXE } else { 'D:\inimerse_stable\inimerse.exe' }
$args2 = @()
if (-not $Unsafe) { $args2 += '--safe' }
$args2 += '--err-json'
$args2 += '--time-limit'
$args2 += "$Timeout"
$args2 += $Script
& $exe @args2 2>&1
exit $LASTEXITCODE

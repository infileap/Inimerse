$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
# Inno Setup 7 installs per-user by default and 6 machine-wide; take whichever
# is present, newest first.
$isccCandidates = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
    'C:\Program Files\Inno Setup 7\ISCC.exe',
    'C:\Program Files (x86)\Inno Setup 7\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup ISCC.exe was not found. Tried:`n$($isccCandidates -join "`n")" }
Write-Host "Using ISCC: $iscc"

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Engine build failed.' }

Push-Location (Join-Path $root 'Infiverse_standard\src-tauri')
try { & cargo build --release --offline; if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed.' } }
finally { Pop-Location }

& $iscc (Join-Path $root 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
Write-Host 'Installer ready: D:\Infiverse_release\InfiverseSetup-0.5.2.exe'

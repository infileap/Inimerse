$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$iscc = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
if (-not (Test-Path -LiteralPath $iscc)) { throw 'Inno Setup 6 ISCC.exe was not found.' }

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Engine build failed.' }

Push-Location (Join-Path $root 'Infiverse_standard\src-tauri')
try { & cargo build --release --offline; if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed.' } }
finally { Pop-Location }

& $iscc (Join-Path $root 'installer.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer build failed.' }
Write-Host 'Installer ready: D:\Infiverse_release\InfiverseSetup.exe'

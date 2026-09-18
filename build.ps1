# build.ps1 - build Inimerse from the repository directory and deploy to %USERPROFILE%\Infiverse
$ErrorActionPreference = "Stop"
if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    $gccCandidates = @()
    if ($env:MSYS2_LOCATION) {
        $gccCandidates += Join-Path $env:MSYS2_LOCATION "ucrt64\bin\gcc.exe"
        $gccCandidates += Join-Path $env:MSYS2_LOCATION "mingw64\bin\gcc.exe"
    }
    $gccCandidates += "C:\msys64\ucrt64\bin\gcc.exe"
    $gccCandidates += "C:\msys64\mingw64\bin\gcc.exe"

    $gccPath = $gccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $gccPath) {
        throw "gcc not found. Install MinGW-w64, set MSYS2_LOCATION, or add gcc to PATH."
    }
    $env:PATH = "$(Split-Path $gccPath);$env:PATH"
}
$gccCommand = Get-Command gcc
Write-Host "Using GCC: $($gccCommand.Source)"
& gcc --version | Select-Object -First 1
$repo = (Resolve-Path $PSScriptRoot).Path

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake not found. Install CMake or add it to PATH."
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "ninja not found. Install Ninja or add it to PATH."
}

$buildDir = Join-Path $repo "build-windows-gcc"
if (Test-Path $buildDir) {
    Remove-Item -Recurse -Force $buildDir
}

& cmake -S $repo -B $buildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=Release" `
    "-DCMAKE_C_COMPILER=$($gccCommand.Source)" `
    "-DINIMERSE_BUILD_ENGINE=ON"
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
}

& cmake --build $buildDir --parallel 2
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE."
}

$builtExe = Join-Path $buildDir "inimerse.exe"
if (-not (Test-Path $builtExe)) {
    throw "CMake build succeeded but $builtExe was not created."
}

$exePath = Join-Path $repo "inimerse.exe"
Copy-Item $builtExe $exePath -Force
$importLib = Join-Path $buildDir "libinimerse.dll.a"
if (Test-Path $importLib) {
    Copy-Item $importLib (Join-Path $repo "inimerse.lib") -Force
}

$deployDir = Join-Path $env:USERPROFILE "Infiverse"
New-Item -ItemType Directory -Path $deployDir -Force | Out-Null
$deployPath = Join-Path $deployDir "inimerse.exe"
Copy-Item $exePath $deployPath -Force
Write-Host "BUILD OK"
Write-Host "deployed to $deployPath"

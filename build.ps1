# build.ps1 - build Inimerse from the repository directory and deploy to %USERPROFILE%\Infiverse
$ErrorActionPreference = "Stop"
if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    throw "gcc not found. Install MinGW-w64 or add gcc to PATH."
}
$repo = (Resolve-Path $PSScriptRoot).Path
$src = Join-Path $repo "src"
$incs = @("-I$src", "-I$src\parser", "-I$src\compiler", "-I$src\vm", "-I$src\runtime", "-I$src\mod", "-I$src\common", "-I$src\lexer")
$files = @(
    "$src\main.c", "$src\platform\platform.c", "$src\platform\thread.c", "$src\platform\fiber.c", "$src\platform\process.c", "$src\platform\socket.c", "$src\platform\dir.c", "$src\common\common.c", "$src\common\sha256.c", "$src\common\ed25519.c", "$src\lexer\lexer.c", "$src\parser\parser.c",
    "$src\compiler\bytecode.c", "$src\compiler\compiler.c", "$src\vm\vm.c", "$src\runtime\runtime.c",
    "$src\child_proc.c", "$src\headless_server.c", "$src\isolate_mod.c", "$src\desugar_mod.c", "$src\lint_mod.c", "$src\mod\mod.c", "$src\mod\gui_mod.c", "$src\mod\io_mod.c", "$src\mod\net_mod.c",
    "$src\mod\json_mod.c", "$src\mod\record_mod.c", "$src\mod\infiverse_mod.c", "$src\mod\verse_dist_mod.c", "$src\mod\server_mod.c", "$src\mod\identity_mod.c", "$src\mod\social_mod.c", "$src\mod\ai_mod.c", "$src\mod\say_mod_windows.c",
    (Join-Path $repo "mods\build\build_mod.c")
)
$libs = @("-lm", "-lwinhttp", "-lcrypt32", "-lgdi32", "-lwinmm", "-lmsimg32", "-lwindowscodecs", "-lole32", "-lws2_32", "-lcomdlg32", "-lshell32", "-lshlwapi", "-liphlpapi")
$exePath = Join-Path $repo "inimerse.exe"
$libPath = Join-Path $repo "inimerse.lib"
$buildOut = & gcc -O2 -s -std=c11 "-Wl,--export-all-symbols" "-Wl,--out-implib=$libPath" @incs -o $exePath @files @libs 2>&1
$buildExit = $LASTEXITCODE
$buildOut | Select-String -Pattern "error|warning" | Select-Object -First 20
if ($buildExit -eq 0) {
    Write-Host "BUILD OK"
    Copy-Item (Join-Path $repo "inimerse.exe") (Join-Path $env:USERPROFILE "Infiverse\inimerse.exe") -Force
    Write-Host "deployed to $env:USERPROFILE\Infiverse\inimerse.exe"
} else {
    Write-Host "BUILD FAILED: $LASTEXITCODE"
    exit 1
}

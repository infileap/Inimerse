# build_asan.ps1 - ASAN debug build (no deploy)
$ErrorActionPreference = "Continue"
$src = "D:\inimerse_stable\src"
$incs = @("-I$src", "-I$src\parser", "-I$src\compiler", "-I$src\vm", "-I$src\runtime", "-I$src\mod", "-I$src\common", "-I$src\lexer")
$files = @(
    "$src\main.c", "$src\common\common.c", "$src\lexer\lexer.c", "$src\parser\parser.c",
    "$src\compiler\bytecode.c", "$src\compiler\compiler.c", "$src\vm\vm.c", "$src\runtime\runtime.c",
    "$src\mod\mod.c", "$src\mod\gui_mod.c", "$src\mod\io_mod.c", "$src\mod\net_mod.c",
    "$src\mod\json_mod.c", "$src\mod\record_mod.c", "$src\mod\infiverse_mod.c", "$src\mod\verse_dist_mod.c",
    "D:\inimerse_stable\mods\build\build_mod.c"
)
$libs = @("-lm", "-lwinhttp", "-lgdi32", "-lwinmm", "-lmsimg32", "-lwindowscodecs", "-lole32", "-lws2_32", "-lcomdlg32")
& gcc -g -O0 -fsanitize=address -fno-omit-frame-pointer -std=c11 @incs -o "D:\inimerse_stable\inimerse_asan.exe" @files @libs 2>&1 | Select-String -Pattern "error" | Select-Object -First 15
if ($LASTEXITCODE -eq 0) { Write-Host "ASAN BUILD OK" } else { Write-Host "ASAN BUILD FAILED: $LASTEXITCODE" }

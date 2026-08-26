# make_patch.ps1 - helper: create a UTF-8 patch json from a UTF-8 source json template.
# usage: powershell -File make_patch.ps1 -out <patch.json> -old "..." -new "..."   (repeat pairs via -oldArr/-newArr)
# NOTE: parameters carrying Chinese must be passed from a file, not the command line.
# Better usage: prepare patch via write_file (UTF-8) directly - this helper only validates it.
param([string]$patch)
if (-not $patch) { Write-Error "usage: make_patch.ps1 -patch <file.json>"; exit 1 }
if (-not (Test-Path $patch)) { Write-Error "patch file not found: $patch"; exit 1 }
$e = Get-Content -Path $patch -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $e.edits -or $e.edits.Count -eq 0) { Write-Error "patch json must contain edits[]"; exit 2 }
Write-Output ("patch valid: {0} edits" -f $e.edits.Count)

# patch_gbk.ps1 - apply exact string replacements to a GBK file. Chinese-safe: ALL Chinese (paths & text) goes through the UTF-8 patch json, never the command line.
# usage: powershell -File patch_gbk.ps1 -patch <utf8-patch.json> [-target <gbk-file>] [-dry]
# patch json: {"target":"path(optional)","edits":[{"old":"...","new":"..."}, ...]}
# workflow: backup(.bak) -> apply -> verify -> commit; on failure restore backup.
param([string]$patch, [string]$target, [switch]$dry)
$gbk = [Text.Encoding]::GetEncoding(936)
$u8 = New-Object System.Text.UTF8Encoding($false)

if (-not (Test-Path $patch)) { Write-Error "patch file not found: $patch"; exit 1 }
$json = Get-Content -Path $patch -Raw -Encoding UTF8 | ConvertFrom-Json
$edits = $json.edits
if (-not $edits -or $edits.Count -eq 0) { Write-Error "patch json must contain edits[]"; exit 2 }
if (-not $target) { $target = $json.target }
if (-not $target) { Write-Error "no target (pass -target or set json.target)"; exit 3 }
if (-not (Test-Path -LiteralPath $target)) { Write-Error "target not found: $target"; exit 4 }

$origBytes = [IO.File]::ReadAllBytes($target)
$text = $gbk.GetString($origBytes)
$changed = 0
foreach ($e in $edits) {
  $old = $e.old; $new = $e.new
  $count = ([regex]::Matches($text, [regex]::Escape($old))).Count
  if ($count -eq 0) { Write-Error "anchor NOT found: [$old]"; exit 5 }
  if ($count -gt 1) { Write-Error "anchor NOT unique ($count): [$old]"; exit 6 }
  $text = $text.Replace($old, $new)
  $changed++
}
if ($dry) { Write-Output ("dry-run ok, {0} edits" -f $changed); exit 0 }
$bak = $target + ".bak"
[IO.File]::WriteAllBytes($bak, $origBytes)
[IO.File]::WriteAllText($target, $text, $gbk)
# verify round-trip: decode again, every new present
$vt = $gbk.GetString([IO.File]::ReadAllBytes($target))
foreach ($e in $edits) {
  if (-not $vt.Contains($e.new)) {
    Write-Error ("VERIFY FAIL, restoring backup"); [IO.File]::WriteAllBytes($target, $origBytes); exit 7
  }
}
Write-Output ("patched ok: {0} edits, {1} -> {2} bytes" -f $changed, $origBytes.Length, (Get-Item -LiteralPath $target).Length)

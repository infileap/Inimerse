# gbktool.ps1 - unified GBK file tool. ALL parameters (paths, text) travel via a UTF-8 JSON config, command line stays ASCII-only.
# usage: powershell -File gbktool.ps1 -config <utf8-config.json>
# config cmds:
#   {"cmd":"read",  "target":"...", "from":1, "count":200}
#   {"cmd":"patch", "target":"...", "edits":[{"old":"..","new":".."},...], "dry":false}
#   {"cmd":"convert","src":"..","dst":"..","to":"gbk|utf8"}
#   {"cmd":"check", "target":"..."}
# patch workflow: backup(.bak) -> apply -> verify -> commit; failure restores backup.
param([string]$config)
$gbk = [Text.Encoding]::GetEncoding(936)
$u8 = New-Object System.Text.UTF8Encoding($false)

if (-not $config) { Write-Error "usage: gbktool.ps1 -config <utf8.json>"; exit 1 }
if (-not (Test-Path $config)) { Write-Error "config not found: $config"; exit 2 }
$cfg = Get-Content -Path $config -Raw -Encoding UTF8 | ConvertFrom-Json
$cmd = $cfg.cmd

function Read-GbkText($path) {
  return $gbk.GetString([IO.File]::ReadAllBytes($path))
}
function Write-GbkText($path, $text) {
  [IO.File]::WriteAllText($path, $text, $gbk)
}

switch ($cmd) {
  "read" {
    $t = Read-GbkText $cfg.target
    $lines = $t -split "`r?`n"
    $from = if ($cfg.from) { [int]$cfg.from } else { 1 }
    $cnt = if ($cfg.count) { [int]$cfg.count } else { 1000000 }
    $end = [Math]::Min($from + $cnt - 1, $lines.Length)
    for ($i = $from - 1; $i -lt $end; $i++) { Write-Output ("{0}: {1}" -f ($i + 1), $lines[$i]) }
  }
  "patch" {
    if (-not (Test-Path -LiteralPath $cfg.target)) { Write-Error "target not found: $($cfg.target)"; exit 3 }
    $orig = [IO.File]::ReadAllBytes($cfg.target)
    $text = $gbk.GetString($orig)
    $changed = 0
    foreach ($e in $cfg.edits) {
      $count = ([regex]::Matches($text, [regex]::Escape($e.old))).Count
      if ($count -eq 0) { Write-Error "anchor NOT found: [$($e.old)]"; exit 4 }
      if ($count -gt 1) { Write-Error "anchor NOT unique ($count): [$($e.old)]"; exit 5 }
      $text = $text.Replace($e.old, $e.new)
      $changed++
    }
    if ($cfg.dry) { Write-Output ("dry-run ok: {0} edits" -f $changed); break }
    [IO.File]::WriteAllBytes($cfg.target + ".bak", $orig)
    Write-GbkText $cfg.target $text
    $vt = $gbk.GetString([IO.File]::ReadAllBytes($cfg.target))
    foreach ($e in $cfg.edits) {
      if (-not $vt.Contains($e.new)) {
        [IO.File]::WriteAllBytes($cfg.target, $orig)
        Write-Error "VERIFY FAIL, restored backup"; exit 6
      }
    }
    Write-Output ("patched ok: {0} edits, {1} -> {2} bytes" -f $changed, $orig.Length, (Get-Item -LiteralPath $cfg.target).Length)
  }
  "convert" {
    if ($cfg.to -eq "utf8") {
      $srcText = Read-GbkText $cfg.src
      [IO.File]::WriteAllText($cfg.dst, $srcText, $u8)
    } else {
      # to=gbk: source is UTF-8 (no BOM), write GBK
      $srcText = $u8.GetString([IO.File]::ReadAllBytes($cfg.src))
      Write-GbkText $cfg.dst $srcText
    }
    Write-Output ("converted ok: {0} -> {1}" -f $cfg.src, $cfg.dst)
  }
  "check" {
    $bytes = [IO.File]::ReadAllBytes($cfg.target)
    $isUtf8 = $true
    try { $u8strict = New-Object System.Text.UTF8Encoding($false, $true); [void]$u8strict.GetString($bytes) } catch { $isUtf8 = $false }
    $s = $gbk.GetString($bytes)
    $fffd = ([regex]::Matches($s, [char]0xFFFD)).Count
    Write-Output ("{0} size={1} utf8={2} gbkFFFD={3}" -f $cfg.target, $bytes.Length, $isUtf8, $fffd)
  }
  default { Write-Error "unknown cmd: $cmd"; exit 7 }
}

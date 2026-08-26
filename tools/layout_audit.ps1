# layout_audit.ps1 - layout audit: report text bands
param([string]$Image = "D:\inimerse_stable\_rel.png")
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap($Image)
$W = $bmp.Width; $H = $bmp.Height
$bandList = New-Object System.Collections.ArrayList
$curStart = -1; $curEnd = -1
for ($y = 0; $y -lt $H; $y++) {
    $d = 0
    for ($x = 0; $x -lt $W; $x += 2) {
        $c = $bmp.GetPixel($x, $y)
        if ($c.R -lt 110 -and $c.G -lt 110) { $d++ }
    }
    if ($d -gt 2) {
        if ($curStart -lt 0) { $curStart = $y }
        $curEnd = $y
    } else {
        if ($curStart -ge 0 -and ($y - $curEnd) -gt 4) {
            $null = $bandList.Add($curStart.ToString() + "-" + $curEnd.ToString())
            $curStart = -1
        }
    }
}
if ($curStart -ge 0) { $null = $bandList.Add($curStart.ToString() + "-" + $curEnd.ToString()) }
"=== LAYOUT AUDIT (" + $W + "x" + $H + ") ==="
"text bands: " + $bandList.Count
foreach ($b in $bandList) { "  y=" + $b }
$bmp.Dispose()
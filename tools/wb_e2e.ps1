Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class E2E {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
  public delegate bool EnumProc(IntPtr h, IntPtr lp);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder sb, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
}
"@
function FindMainWin([int]$procId) {
  $script:hwin = [IntPtr]::Zero
  $cb = [E2E+EnumProc]{ param($w, $l) $cn = New-Object System.Text.StringBuilder 128; [E2E]::GetClassName($w, $cn, 128) | Out-Null; $wpid = 0; [E2E]::GetWindowThreadProcessId($w, [ref]$wpid) | Out-Null; if ($wpid -eq $procId -and $cn.ToString() -eq "inimerse_gui") { $script:hwin = $w; return $false }; return $true }
  [E2E]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:hwin
}
function Click([int]$cx, [int]$cy) {
  $r = New-Object E2E+RECT
  [E2E]::GetWindowRect($script:hw, [ref]$r) | Out-Null
  $sx = $r.L + 8 + $cx
  $sy = $r.T + 31 + $cy
  [E2E]::SetCursorPos($sx, $sy) | Out-Null
  Start-Sleep -Milliseconds 120
  [E2E]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
  Start-Sleep -Milliseconds 80
  [E2E]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
  Start-Sleep -Milliseconds 200
}
function TypeText([string]$t) {
  foreach ($ch in $t.ToCharArray()) {
    [E2E]::PostMessage($script:hw, 0x0102, [IntPtr][int]$ch, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 40
  }
}
$inf = "C:\Users\Lenovo\Infiverse"
Get-Process inimerse -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Remove-Item "$inf\projects\demo" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "$inf\projects\demo.vverse" -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
$p = Start-Process -FilePath "$inf\inimerse.exe" -ArgumentList "workbench.im" -WorkingDirectory $inf -PassThru
Start-Sleep -Milliseconds 1500
$script:hw = FindMainWin $p.Id
if ($script.hw -eq [IntPtr]::Zero) { "workbench window not found"; Stop-Process -Id $p.Id -Force; exit 1 }
"wb window ok"
# 1) 点项目名输入框,输入 demo
Click 210 63
TypeText "demo"
Start-Sleep -Milliseconds 300
# 2) 点[新建项目] (btn_y[0]=90, y 90-120)
Click 85 103
Start-Sleep -Milliseconds 1200
"proj main.im exists: $(Test-Path "$inf\projects\demo\main.im")"
"proj net.im exists: $(Test-Path "$inf\projects\demo\net.im")"
"proj manifest.json exists: $(Test-Path "$inf\projects\demo\manifest.json")"
"proj README.md exists: $(Test-Path "$inf\projects\demo\README.md")"
# 3) 文件列表第1行(main.im)选中 + 运行
Click 280 82
Start-Sleep -Milliseconds 300
Click 85 403
Start-Sleep -Milliseconds 2200
$newProc = Get-Process inimerse -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $p.Id }
"run new procs: $($newProc.Count)"
if ($newProc) { "new win: $($newProc[0].MainWindowTitle)" }
Get-Process inimerse -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 600
"done"
# UI stress on a big library: scan time, memory, frame stats while scrolling and hovering.
param([string]$Exe = (Join-Path $PSScriptRoot "..\..\build\Moonbow.exe"), [string]$Tag = "stress", [switch]$Keep)
$t = (Join-Path $PSScriptRoot "..\..\tools\dev")
$trace = "$env:TEMP\moonbow-trace.txt"
$env:MOONBOW_TRACE = "1"
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class St {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public static IntPtr Find(uint pid) { IntPtr f = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (p == pid && c.ToString() == "MoonbowHost" && GetWindow(h, 4) == IntPtr.Zero) { f = h; return false; } return true; }, IntPtr.Zero); return f; }
  public static void Wheel(IntPtr h, int x, int y, int delta, int count, int sleepMs) {
    var pt = new POINT { X = x, Y = y }; ClientToScreen(h, ref pt);
    for (int i = 0; i < count; i++) { PostMessage(h, 0x20A, (IntPtr)((delta * 120) << 16), (IntPtr)((pt.Y << 16) | (pt.X & 0xFFFF))); System.Threading.Thread.Sleep(sleepMs); } }
  public static void Hover(IntPtr h, int x0, int y0, int x1, int y1, int steps, int sleepMs) {
    for (int i = 0; i <= steps; i++) { int x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
      PostMessage(h, 0x200, IntPtr.Zero, (IntPtr)((y << 16) | (x & 0xFFFF))); System.Threading.Thread.Sleep(sleepMs); } }
}
"@
function Mem($p, $label) { $p.Refresh(); "{0,-22} WS={1,5} MB Private={2,5} MB CPU-total={3,7} ms threads={4}" -f $label,
  [math]::Round($p.WorkingSet64/1MB), [math]::Round($p.PrivateMemorySize64/1MB), [math]::Round($p.TotalProcessorTime.TotalMilliseconds), $p.Threads.Count }
function Frames($label) { "--- $label"; if (Test-Path $trace) { Get-Content $trace | Where-Object { $_ -match "frames" }; Remove-Item $trace } }

Remove-Item $trace -ErrorAction SilentlyContinue
$sw = [Diagnostics.Stopwatch]::StartNew()
$id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\moonbow-qa\perf-$Tag" -Fresh -Library "$env:TEMP\moonbow-qa\big"
$p = Get-Process -Id $id
Start-Sleep 1
$h = [St]::Find([uint32]$id)
# Wait until the scan settles (CPU stops growing).
$last = -1; for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); $c = $p.TotalProcessorTime.TotalMilliseconds; if ($i -gt 2 -and $c - $last -lt 15) { break }; $last = $c }
"scan settled after ~{0:N1} s" -f ($sw.ElapsedMilliseconds / 1000)
Mem $p "home after scan"
Frames "startup/scan"
# Home: scroll down and up, hover cards.
[St]::Wheel($h, 700, 400, -1, 40, 16); [St]::Wheel($h, 700, 400, 1, 40, 16); Start-Sleep -Milliseconds 400
[St]::Hover($h, 250, 200, 1250, 700, 120, 8); Start-Sleep -Milliseconds 300
Frames "home scroll+hover"
# Music -> Songs (2000 rows)
& "$t\input.ps1" -ProcessId $id -Click "100,160" | Out-Null; Start-Sleep 2
Mem $p "music hub"
Frames "open music"
& "$t\capture.ps1" -ProcessId $id -Out "$env:TEMP\moonbow-qa\v\stress-music.png" -Scale 0.5 | Out-Null
# Videos: 60 show folders
& "$t\input.ps1" -ProcessId $id -Click "100,114" | Out-Null; Start-Sleep 2
Frames "open videos"
[St]::Wheel($h, 700, 400, -1, 60, 16); Start-Sleep -Milliseconds 400
Frames "videos scroll"
Mem $p "videos"
# Search typing
& "$t\input.ps1" -ProcessId $id -Click "1060,75" | Out-Null
$sw.Restart(); & "$t\input.ps1" -ProcessId $id -Type "Song 5" | Out-Null; Start-Sleep 1
Frames "search typing"
& "$t\capture.ps1" -ProcessId $id -Out "$env:TEMP\moonbow-qa\v\stress-search.png" -Scale 0.5 | Out-Null
Mem $p "after search"
if ($Keep) { "kept PID $id" } else { $p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(5000)) { Stop-Process -Id $id -Force; "killed" } }

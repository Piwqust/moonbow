# Sends synthetic input to a Lumen window with PostMessage: the real mouse and
# keyboard focus are untouched, so several test runs (and the user) don't collide.
# Coordinates are client-area pixels. Start Lumen with --automation so hover
# state survives while the real cursor is elsewhere.
# Usage:
#   tools\dev\input.ps1 -ProcessId 1234 -Click 300,200 [-Right] [-Double]
#   tools\dev\input.ps1 -ProcessId 1234 -Move 300,200
#   tools\dev\input.ps1 -ProcessId 1234 -Wheel 300,200 -Delta -1
#   tools\dev\input.ps1 -ProcessId 1234 -Key Space | -Key F | -Key Escape | -Type "hello"
#   tools\dev\input.ps1 -ProcessId 1234 -Drag 100,100,400,100
#   add -Ctrl / -Shift to any of these to hold the modifier (e.g. -Key P -Ctrl)
param([Parameter(Mandatory = $true)][int]$ProcessId, [string]$Title = "",
      [string]$Click = "", [string]$Move = "", [string]$Wheel = "", [int]$Delta = 1, [string]$Drag = "",
      [switch]$Right, [switch]$Double, [string]$Key = "", [string]$Type = "", [switch]$Ctrl, [switch]$Shift)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class LumenIn {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="PostMessageW")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public static IntPtr Find(uint pid, string title) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true;
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (c.ToString() != "LumenHost") return true;
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (title.Length > 0 && !t.ToString().Contains(title)) return true;
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }
}
"@
$h = [LumenIn]::Find([uint32]$ProcessId, $Title)
if ($h -eq [IntPtr]::Zero) { throw "no visible LumenHost window for process $ProcessId" }
function MakeLParam([int]$x, [int]$y) { [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF)) }
function Post([uint32]$m, [int64]$w, [IntPtr]$l) { [LumenIn]::PostMessage($h, $m, [IntPtr]$w, $l) | Out-Null }
$mk = 0; if ($Ctrl) { $mk = $mk -bor 0x8 }; if ($Shift) { $mk = $mk -bor 0x4 }
function XY($s) { $p = $s -split ','; return [int]$p[0], [int]$p[1] }

if ($Move) { $x, $y = XY $Move; Post 0x200 $mk (MakeLParam $x $y) }
if ($Click) {
  $x, $y = XY $Click
  Post 0x200 $mk (MakeLParam $x $y); Start-Sleep -Milliseconds 30
  if ($Right) { Post 0x204 (0x2 -bor $mk) (MakeLParam $x $y); Post 0x205 $mk (MakeLParam $x $y) }
  else {
    Post 0x201 (0x1 -bor $mk) (MakeLParam $x $y); Post 0x202 $mk (MakeLParam $x $y)
    if ($Double) { Start-Sleep -Milliseconds 40; Post 0x203 (0x1 -bor $mk) (MakeLParam $x $y); Post 0x202 $mk (MakeLParam $x $y) }
  }
}
if ($Drag) {
  $p = $Drag -split ','; $x1 = [int]$p[0]; $y1 = [int]$p[1]; $x2 = [int]$p[2]; $y2 = [int]$p[3]
  Post 0x200 0 (MakeLParam $x1 $y1); Post 0x201 1 (MakeLParam $x1 $y1)
  for ($i = 1; $i -le 10; $i++) { Start-Sleep -Milliseconds 15; Post 0x200 1 (MakeLParam ([int]($x1 + ($x2 - $x1) * $i / 10)) ([int]($y1 + ($y2 - $y1) * $i / 10))) }
  Post 0x202 0 (MakeLParam $x2 $y2)
}
if ($Wheel) {
  $x, $y = XY $Wheel
  $pt = New-Object LumenIn+POINT; $pt.X = $x; $pt.Y = $y
  [LumenIn]::ClientToScreen($h, [ref]$pt) | Out-Null
  Post 0x20A ([int64](($Delta * 120) -shl 16)) (MakeLParam $pt.X $pt.Y)
}
# Held modifiers for this call (Lumen reads them only with --automation).
$mods = ([int][bool]$Ctrl) -bor (2 * [int][bool]$Shift)
if ($mods) { Post 0x802D $mods ([IntPtr]0) }
if ($Key) {
  $vk = [int][System.Windows.Forms.Keys]::$Key
  Post 0x100 $vk ([IntPtr]1); Post 0x101 $vk ([IntPtr]0xC0000001)
}
if ($Type) { foreach ($ch in $Type.ToCharArray()) { Post 0x102 ([int]$ch) ([IntPtr]1) } }
if ($mods) { Start-Sleep -Milliseconds 30; Post 0x802D 0 ([IntPtr]0) }
Start-Sleep -Milliseconds 250

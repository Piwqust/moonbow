# Moves/resizes one of YOUR Lumen test windows (never other windows): -ProcessId N -Size 800,500 [-Pos 100,100]
# or -Maximize / -Restore; -Title "Настройки" picks another window of the process by title.
param([Parameter(Mandatory = $true)][int]$ProcessId, [string]$Title = "", [string]$Size = "", [string]$Pos = "", [switch]$Maximize, [switch]$Restore)
if (-not ("LumenWin" -as [type])) {
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class LumenWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hh, uint f);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll", EntryPoint="GetWindowLongW")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  public static IntPtr Find(uint pid, string title) { IntPtr f = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (p != pid || c.ToString() != "LumenHost") return true;
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (title.Length > 0 ? t.ToString().Contains(title) : GetWindow(h, 4) == IntPtr.Zero) { f = h; return false; }
      return true; }, IntPtr.Zero); return f; }
}
"@ }
$h = [LumenWin]::Find([uint32]$ProcessId, $Title)
if ($h -eq [IntPtr]::Zero) { throw "no Lumen window for $ProcessId" }
# Never the owner's Lumen: only --automation instances, whose windows are WS_EX_NOACTIVATE (ui/Host.cpp).
if (([LumenWin]::GetWindowLong($h, -20) -band 0x08000000) -eq 0) { throw "process $ProcessId is not a --automation instance: window left alone" }
if ($Maximize) { [LumenWin]::ShowWindow($h, 3) | Out-Null; return }   # SW_MAXIMIZE (no activation in automation: window is NOACTIVATE)
if ($Restore) { [LumenWin]::ShowWindow($h, 9) | Out-Null; return }
$x = 100; $y = 100
if ($Pos) { $p = $Pos -split ','; $x = [int]$p[0]; $y = [int]$p[1] }
$s = $Size -split ','
# SWP_NOZORDER | SWP_NOACTIVATE (| SWP_NOMOVE when no -Pos)
$flags = 0x0004 -bor 0x0010; if (-not $Pos) { $flags = $flags -bor 0x0002 }
[LumenWin]::SetWindowPos($h, [IntPtr]::Zero, $x, $y, [int]$s[0], [int]$s[1], $flags) | Out-Null

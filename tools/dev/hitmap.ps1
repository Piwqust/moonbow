# Asks a Moonbow window what WM_NCHITTEST answers across its whole client area, the way the
# real mouse does before any click arrives. Posted input (input.ps1) skips this step, so a
# region that answers HTCAPTION or a resize border swallows real clicks while tests pass.
# Prints every region that is not plain client area, with the widgets under it.
# Usage: tools\dev\hitmap.ps1 -ProcessId 1234 [-Step 8] [-Title "Moonbow"]
param([Parameter(Mandatory = $true)][int]$ProcessId, [int]$Step = 8, [string]$Title = "")
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class MoonbowHit {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
  public static IntPtr Find(uint pid, string title) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true;
      var c = new StringBuilder(64); GetClassName(h, c, 64); if (c.ToString() != "MoonbowHost") return true;
      var t = new StringBuilder(256); GetWindowText(h, t, 256);
      if (title.Length > 0 && !t.ToString().Contains(title)) return true;
      found = h; return false; }, IntPtr.Zero);
    return found;
  }
  public static long Hit(IntPtr h, int x, int y) {
    var p = new POINT { X = x, Y = y }; ClientToScreen(h, ref p); IntPtr r;
    SendMessageTimeout(h, 0x84, IntPtr.Zero, (IntPtr)(((p.Y & 0xFFFF) << 16) | (p.X & 0xFFFF)), 2, 1000, out r);
    return r.ToInt64();
  }
}
"@
$names = @{ 0 = "HTNOWHERE"; 1 = "HTCLIENT"; 2 = "HTCAPTION"; 9 = "HTMAXBUTTON"; 10 = "HTLEFT"; 11 = "HTRIGHT"; 12 = "HTTOP";
            13 = "HTTOPLEFT"; 14 = "HTTOPRIGHT"; 15 = "HTBOTTOM"; 16 = "HTBOTTOMLEFT"; 17 = "HTBOTTOMRIGHT"; -1 = "HTTRANSPARENT" }
$h = [MoonbowHit]::Find([uint32]$ProcessId, $Title)
if ($h -eq [IntPtr]::Zero) { throw "no visible MoonbowHost window for process $ProcessId" }
$scale = [MoonbowHit]::GetDpiForWindow($h) / 96.0
$rc = New-Object MoonbowHit+RECT; [MoonbowHit]::GetClientRect($h, [ref]$rc) | Out-Null

# Widget tree (rects are window DIPs); a hidden ancestor hides its subtree.
# Sent, not posted: the dump file is complete when this returns (a fixed wait could read the old one).
$sr = [IntPtr]::Zero
if ([MoonbowHit]::SendMessageTimeout($h, 0x8000 + 43, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$sr) -eq [IntPtr]::Zero) {
  throw "window $($h.ToInt64()) did not answer the tree dump"
}
$nodes = @(); $hiddenDepth = -1; $popup = $false
foreach ($line in Get-Content (Join-Path $env:TEMP "moonbow-tree-$ProcessId-$($h.ToInt64()).txt") -Encoding UTF8) {
  # v2 header: the DIP scale, including the --scale test override.
  if ($line -match '^=== window .* scale=([\d.]+)') { $scale = [double]$matches[1]; continue }
  if ($line -eq "--- popup") { $popup = $true; $hiddenDepth = -1; continue }
  # Type names of local classes contain spaces ("`public: __cdecl ...'::QueueButton"), so match lazily.
  if ($line -notmatch '^( *)(.+?)(?: #(\S+))? \[(-?[\d.]+),(-?[\d.]+) ([\d.]+)x([\d.]+)\](.*)$') { continue }
  $depth = $matches[1].Length / 2
  if ($hiddenDepth -ge 0 -and $depth -gt $hiddenDepth) { continue }
  $hiddenDepth = -1
  if (($matches[8] -split ' text=| tip=')[0] -match ' hidden\b') { $hiddenDepth = $depth; continue }
  $name = $matches[2]; if ($matches[3]) { $name += "#" + $matches[3] }
  $nodes += [pscustomobject]@{ Name = $name; Depth = $depth; Popup = $popup
    X = [double]$matches[4]; Y = [double]$matches[5]; W = [double]$matches[6]; H = [double]$matches[7] }
}
function Under([double]$x, [double]$y) {
  # Deepest visible widget names at a DIP point, popups first (they draw on top).
  $hits = $nodes | Where-Object { $x -ge $_.X -and $x -lt $_.X + $_.W -and $y -ge $_.Y -and $y -lt $_.Y + $_.H } |
    Sort-Object @{ e = { $_.Popup }; Descending = $true }, @{ e = { $_.Depth }; Descending = $true }
  ($hits | Select-Object -First 2 | ForEach-Object { $_.Name }) -join " < "
}

$regions = @{}
for ($y = [int]($Step / 2); $y -lt $rc.B; $y += $Step) {
  for ($x = [int]($Step / 2); $x -lt $rc.R; $x += $Step) {
    $code = [MoonbowHit]::Hit($h, $x, $y)
    if ($code -eq 1) { continue }
    $n = $names[[int]$code]; if (-not $n) { $n = "HT$code" }
    $what = Under ($x / $scale) ($y / $scale)
    $key = "$n | $what"
    if (-not $regions[$key]) { $regions[$key] = [pscustomobject]@{ Code = $n; Under = $what; Count = 0; X1 = $x; Y1 = $y; X2 = $x; Y2 = $y } }
    $r = $regions[$key]; $r.Count++
    $r.X1 = [math]::Min($r.X1, $x); $r.Y1 = [math]::Min($r.Y1, $y); $r.X2 = [math]::Max($r.X2, $x); $r.Y2 = [math]::Max($r.Y2, $y)
  }
}
"client {0}x{1} px, scale {2}" -f $rc.R, $rc.B, $scale
$regions.Values | Sort-Object Code, Y1, X1 | ForEach-Object {
  "{0,-14} px [{1},{2} .. {3},{4}] x{5}  over: {6}" -f $_.Code, $_.X1, $_.Y1, $_.X2, $_.Y2, $_.Count, $_.Under
}

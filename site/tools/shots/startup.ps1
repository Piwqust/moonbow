# The startup of Moonbow, frame by frame, for the gate on the site's home page: launches the hero
# episode in a hidden --automation instance (1280x800 at 200 %) and saves the window as fast as
# PrintWindow can (every ~30 ms) with the time since launch. Based on tools\dev\frames.ps1.
#   powershell -File site\tools\shots\startup.ps1 [-Ms 1500]
# Output: %TEMP%\moonbow-site\startup\f<NNN>_<ms>.png and frames.txt. Pick the states by eye.
param([int]$Ms = 1500)
. "$PSScriptRoot\session.ps1"
$Out = Join-Path $env:TEMP "moonbow-site\startup"
$DataDir = Join-Path $env:TEMP "moonbow-site\data-startup"
$hero = Join-Path $Media "Видео\Сериалы\Дом у моря\Дом у моря S01E04.mp4"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class FCap {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr after, string cls, string title);
  public static IntPtr FindMain(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true;
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (c.ToString() != "MoonbowHost") return true;
      if (GetWindow(h, 4) != IntPtr.Zero) return true;  // owned (onboarding, settings)
      found = h; return false;
    }, IntPtr.Zero);
    return found;
  }
}
"@
[FCap]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force $Out | Out-Null
Get-ChildItem $Out -Filter *.png -ErrorAction SilentlyContinue | Remove-Item
if (Test-Path $DataDir) { Remove-Item -Recurse -Force $DataDir }
New-Item -ItemType Directory -Force $DataDir | Out-Null
[IO.File]::WriteAllText("$DataDir\ui.json", '{"onboarded":"1","window.placement":"100,60,2660,1700,0"}', $Enc)
$sw = [Diagnostics.Stopwatch]::StartNew()
$id = & "$Dev\launch.ps1" -DataDir $DataDir -Exe $Exe -Scale 1.6 -Settings '{"general.language":"ru","general.theme":"dark"}' $hero
if (-not $id) { throw "launch failed" }
$Open = ""
$log = New-Object System.Collections.Generic.List[string]
$n = 0; $opened = $false
try {
  while ($sw.ElapsedMilliseconds -lt $Ms) {
    if ($Open -and -not $opened -and $sw.ElapsedMilliseconds -ge $OpenAfterMs) {
      $log.Add(("{0,6} OPEN" -f $sw.ElapsedMilliseconds))
      & "$tools\command.ps1" -ProcessId $id -Command "open $Open" -Quiet; $opened = $true
    }
    $h = [FCap]::FindMain([uint32]$id)
    if ($h -ne [IntPtr]::Zero) {
      $r = New-Object FCap+RECT; [FCap]::GetWindowRect($h, [ref]$r) | Out-Null
      # The client area: the window's invisible resize band (left, right, bottom) is never seen.
      $cr = New-Object FCap+RECT; [FCap]::GetClientRect($h, [ref]$cr) | Out-Null
      $co = New-Object FCap+POINT; [FCap]::ClientToScreen($h, [ref]$co) | Out-Null
      $w = $cr.R; $hh = $cr.B
      if ($w -gt 0 -and $hh -gt 0) {
        $revealed = [FCap]::GetProp($h, "MoonbowRevealed") -ne [IntPtr]::Zero
        $video = ""
        $vh = [FCap]::FindWindowEx($h, [IntPtr]::Zero, "MoonbowVideo", $null)
        if ($vh -ne [IntPtr]::Zero -and [FCap]::IsWindowVisible($vh)) {
          $vr = New-Object FCap+RECT; [FCap]::GetWindowRect($vh, [ref]$vr) | Out-Null
          if ($vr.R - $vr.L -gt 4) { $video = " video={0},{1},{2},{3}" -f ($vr.L - $co.X), ($vr.T - $co.Y), ($vr.R - $vr.L), ($vr.B - $vr.T) }
        }
        $full = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
        $g = [System.Drawing.Graphics]::FromImage($full); $dc = $g.GetHdc()
        [FCap]::PrintWindow($h, $dc, 2) | Out-Null; $g.ReleaseHdc($dc); $g.Dispose()
        $cx = [math]::Max(0, $co.X - $r.L); $cy = [math]::Max(0, $co.Y - $r.T)
        $w = [math]::Min($w, $full.Width - $cx); $hh = [math]::Min($hh, $full.Height - $cy)
        $bmp = $full.Clone((New-Object System.Drawing.Rectangle $cx, $cy, $w, $hh), $full.PixelFormat); $full.Dispose()
        $t = $sw.ElapsedMilliseconds
        $bmp.Save(("{0}\f{1:D3}_{2:D5}.png" -f $Out, $n, $t)); $bmp.Dispose()
        $log.Add(("{0,6} f{1:D3} {2}x{3} at {4},{5}{6}" -f $t, $n, $w, $hh, $co.X, $co.Y, ($(if ($revealed) { " revealed" } else { "" }) + $video))); $n++
      }
    } else { $log.Add(("{0,6} no window" -f $sw.ElapsedMilliseconds)) }
  }
} finally {
  $log | Set-Content "$Out\frames.txt"
  Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
}
"$n frames, pid $id"

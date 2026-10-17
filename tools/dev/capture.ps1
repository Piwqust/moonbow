# Captures a Lumen window to PNG without bringing it to the front or touching
# the real mouse (PrintWindow with PW_RENDERFULLCONTENT also captures the
# DirectComposition UI layer).
# Usage: tools\dev\capture.ps1 -ProcessId 1234 -Out shot.png [-Title "Lumen"] [-Scale 0.5]
param([Parameter(Mandatory = $true)][int]$ProcessId, [Parameter(Mandatory = $true)][string]$Out,
      [string]$Title = "", [double]$Scale = 1)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class LumenCap {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
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
[LumenCap]::SetProcessDPIAware() | Out-Null
$h = [LumenCap]::Find([uint32]$ProcessId, $Title)
if ($h -eq [IntPtr]::Zero) { throw "no visible LumenHost window for process $ProcessId" }
$r = New-Object LumenCap+RECT
[LumenCap]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[LumenCap]::PrintWindow($h, $dc, 2) | Out-Null   # PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc)
if ($Scale -ne 1) {
  $o = New-Object System.Drawing.Bitmap ([int]($w * $Scale)), ([int]($hh * $Scale))
  $g2 = [System.Drawing.Graphics]::FromImage($o); $g2.InterpolationMode = 'HighQualityBicubic'
  $g2.DrawImage($bmp, 0, 0, $o.Width, $o.Height); $bmp.Dispose(); $bmp = $o
}
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"$w x $hh"

# Captures one --automation Moonbow window as DWM composes it (Windows Graphics Capture through
# ffmpeg 9 gfxcapture), 60 frames/s, while actions are posted to it. Unlike PrintWindow
# (frames.ps1) it sees composition-level faults: a frame of an old picture after a size change, a
# bare GDI background, mpv's video a frame late. The window stays cloaked: nothing shows on screen.
# Frames come only when the window changes (a paused video gives few).
# Artifact: when the window grows beyond its size at the start of the capture, ffmpeg keeps the
# old-sized buffer for 1-2 frames and smears its right/bottom edge. Measure shrinking switches
# (start the capture in the big state, e.g. -Settings '{"video.startFullscreen":true}').
# Usage: wincap.ps1 -Out <dir> [-Actions "600:F","1800:F"] [-Seconds 4] [-File <media>] [-Mode crop|scale]
#                   [-Settings <json>] [-Library <dir>] [-Exe <exe>]
# Actions "ms:what": F, esc, space, pip (Ctrl+Shift+P), side (Ctrl+B), max, restore.
# Check with wincap-stale.py <dir>.
param([string]$Exe = (Join-Path $PSScriptRoot "..\..\build\Moonbow.exe"), [string]$Mode = "scale", [string]$Out, [string[]]$Actions = @(), [int]$Seconds = 4, [string]$File = "$env:TEMP\moonbow-test-media\The Wand S01\The_Wand_S01E01.mkv", [string]$Settings = "", [string]$Library = "")
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class Wg {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public static IntPtr Find(uint pid) { IntPtr f = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid || !IsWindowVisible(h)) return true; var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (c.ToString() != "MoonbowHost" || GetWindow(h, 4) != IntPtr.Zero) return true; f = h; return false; }, IntPtr.Zero); return f; }
}
"@
[Wg]::SetProcessDPIAware() | Out-Null
$tools = $PSScriptRoot
Remove-Item -Recurse -Force $Out -ErrorAction SilentlyContinue; New-Item -ItemType Directory $Out | Out-Null
$a = @{ DataDir = "$env:TEMP\moonbow-qa\wgc-data"; Fresh = $true; Exe = $Exe }
if ($Settings) { $a.Settings = $Settings }
if ($Library) { $a.Library = $Library }
$id = if ($File) { & "$tools\launch.ps1" @a $File } else { & "$tools\launch.ps1" @a }
try {
  Start-Sleep -Milliseconds 3000
  $h = [Wg]::Find([uint32]$id)
  $ff = Start-Process ffmpeg -ArgumentList '-hide_banner','-loglevel','error','-f','lavfi','-i',"gfxcapture=hwnd=$([int64]$h):max_framerate=60:capture_cursor=0:resize_mode=$Mode",'-t',"$Seconds",'-vf','hwdownload,format=bgra',"`"$Out\f%03d.png`"" -WindowStyle Hidden -PassThru -RedirectStandardError "$Out\ffmpeg.log"
  $sw = [Diagnostics.Stopwatch]::StartNew(); $log = @()
  foreach ($act in $Actions) { $ms, $what = $act.Split(':'); while ($sw.ElapsedMilliseconds -lt [int]$ms) { Start-Sleep -Milliseconds 5 }
    switch ($what) {
      'F' { [Wg]::PostMessage($h, 0x100, [IntPtr]0x46, [IntPtr]0x00210001) | Out-Null; [Wg]::PostMessage($h, 0x101, [IntPtr]0x46, [IntPtr]0xC0210001) | Out-Null }
      'esc' { [Wg]::PostMessage($h, 0x100, [IntPtr]0x1B, [IntPtr]0x00010001) | Out-Null; [Wg]::PostMessage($h, 0x101, [IntPtr]0x1B, [IntPtr]0xC0010001) | Out-Null }
      'space' { [Wg]::PostMessage($h, 0x100, [IntPtr]0x20, [IntPtr]0x00390001) | Out-Null; [Wg]::PostMessage($h, 0x101, [IntPtr]0x20, [IntPtr]0xC0390001) | Out-Null }
      'pip' { & "$tools\input.ps1" -ProcessId $id -Key P -Ctrl -Shift }
      'side' { & "$tools\input.ps1" -ProcessId $id -Key B -Ctrl }
      'max' { [Wg]::PostMessage($h, 0x112, [IntPtr]0xF030, [IntPtr]::Zero) | Out-Null }
      'restore' { [Wg]::PostMessage($h, 0x112, [IntPtr]0xF120, [IntPtr]::Zero) | Out-Null }
    }
    $log += "$($sw.ElapsedMilliseconds) $what" }
  $ff.WaitForExit(($Seconds + 5) * 1000) | Out-Null
  $log | Set-Content "$Out\actions.txt"
} finally { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
"frames: " + (Get-ChildItem $Out -Filter f*.png).Count

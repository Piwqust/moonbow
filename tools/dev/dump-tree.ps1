# Writes the widget tree of a Lumen window and prints it (UTF-8). Only --automation instances
# answer (launch.ps1 starts them that way); a normal Lumen ignores the request.
# Usage:
#   tools\dev\dump-tree.ps1 -ProcessId 1234              main window
#   tools\dev\dump-tree.ps1 -ProcessId 1234 -All         every visible window (Settings, dialogs...)
#   tools\dev\dump-tree.ps1 -ProcessId 1234 -Out x.txt   also copy the dump(s) to x.txt
#   add -Quiet to skip printing.
# Each window writes %TEMP%\lumen-tree-<pid>-<hwnd as decimal>.txt; the main window also writes
# the old %TEMP%\lumen-tree-<pid>.txt. The message is sent synchronously: the file is complete
# when this script reads it.
#
# Format (v2). First line of every window:
#   === window title="..." client=WxH scale=S theme=dark|light fullscreen=0|1 maximized=0|1 hwnd=N main=0|1 dpi=D revealed=0|1
#   (client in DIPs; scale = DIP-to-pixel factor incl. --scale; dpi = the monitor's real DPI;
#   revealed = a user would see the window now: its first complete frame is up, Host::Reveal)
# Then one line per widget, two spaces of indent per depth, popups after "--- popup":
#   <indent><Type>[ #id] [x,y wxh][ hidden][ disabled][ interactive][ focus][ hot][ pressed]
#       [<state>...][ text="..."[ trunc][ clip][ overflow][ faded][ box=[x,y wxh]]]...[ tip="..."]
#   Rects are window DIPs. <state> comes from Widget::DebugInfo: " scroll offset=Y/MAX"
#   (ScrollView), " on"/" off" (Toggle), " checked", " selected=N", " value=V", " input",
#   " empty" (TextBox showing its placeholder), " password", " readonly", " icon=<name>", " active",
#   " dragging" (ItemGrid: a row is being reordered).
#   text: every string the widget painted in this frame, in paint order; a widget that paints
#   many items (grids, menus, lists) has several. Strings are escaped: \" \\ \n \r \t.
#     trunc    natural one-line width exceeds the box it was drawn in (ellipsized or cut);
#              for wrapped text: the wrapped height exceeds the box and it is ellipsized
#     clip     natural height (one line, or wrapped) exceeds the box height (+1 DIP)
#     overflow glyphs extend past the widget's own rect (+1 DIP)
#     faded    drawn with alpha < 0.05 (e.g. auto-hidden controls)
#     box      the text box when it differs from the widget rect
#   tip: the widget's tooltip.
param([Parameter(Mandatory = $true)][int]$ProcessId, [switch]$All, [string]$Out = "", [switch]$Quiet)
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class LumenDump {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageTimeoutW")]
  public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
  // Visible LumenHost windows of the process, the main (unowned) one first.
  public static List<IntPtr> Windows(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (p == pid && c.ToString() == "LumenHost" && IsWindowVisible(h)) {
        if (GetWindow(h, 4) == IntPtr.Zero) list.Insert(0, h); else list.Add(h);
      }
      return true; }, IntPtr.Zero);
    return list;
  }
}
"@
$wins = [LumenDump]::Windows([uint32]$ProcessId)
if ($wins.Count -eq 0) { throw "no visible LumenHost window for process $ProcessId" }
if (-not $All) { $wins = @($wins[0]) }
$text = @()
foreach ($h in $wins) {
  $r = [IntPtr]::Zero
  $ok = [LumenDump]::SendMessageTimeout($h, 0x8000 + 43, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$r)
  if ($ok -eq [IntPtr]::Zero) { Write-Warning "window $($h.ToInt64()) did not answer"; continue }
  $file = Join-Path $env:TEMP "lumen-tree-$ProcessId-$($h.ToInt64()).txt"
  $text += [System.IO.File]::ReadAllText($file, [System.Text.Encoding]::UTF8)
}
$joined = $text -join ""
if ($Out) { [System.IO.File]::WriteAllText($Out, $joined, (New-Object System.Text.UTF8Encoding $false)) }
if (-not $Quiet) { $joined.TrimEnd("`n") }

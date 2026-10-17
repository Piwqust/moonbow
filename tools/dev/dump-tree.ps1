# Writes the widget tree of every Lumen window of a process to
# %TEMP%\lumen-tree-<pid>.txt (type, rect in DIPs, hidden/disabled) and prints it.
# Usage: tools\dev\dump-tree.ps1 -ProcessId 1234
param([Parameter(Mandatory = $true)][int]$ProcessId)
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class LumenDump {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr FindMain(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (p == pid && c.ToString() == "LumenHost") { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
}
"@
$h = [LumenDump]::FindMain([uint32]$ProcessId)
if ($h -eq [IntPtr]::Zero) { throw "no LumenHost window for process $ProcessId" }
[LumenDump]::PostMessage($h, 0x8000 + 43, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 300
Get-Content (Join-Path $env:TEMP "lumen-tree-$ProcessId.txt")

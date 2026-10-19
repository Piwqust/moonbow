# Prints what screen readers see: the UI Automation tree of every Lumen window of a process, one
# element per line. Runs as a UIA client only (no mouse, keys or focus), so it works on invisible
# --automation instances.
# Usage:
#   tools\dev\uia-dump.ps1 -ProcessId 1234                 the tree
#   tools\dev\uia-dump.ps1 -ProcessId 1234 -Invoke "<name>" then invokes the first element with that
#                                                          name (Invoke, Toggle or SelectionItem)
#   add -Out x.txt to also write the tree to a file (UTF-8), -Max N to stop after N elements.
# Line format:
#   <indent>Type "Name" [x,y wxh] [off] [disabled] [focus] {Patterns} value=... toggle=... range=v/min..max
#   Rects are screen pixels.
param([Parameter(Mandatory = $true)][int]$ProcessId, [string]$Invoke = "", [string]$Out = "", [int]$Max = 5000)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class LumenUia {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  public static List<IntPtr> Windows(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (p == pid && IsWindowVisible(h) && c.ToString() == "LumenHost") list.Add(h);
      return true;
    }, IntPtr.Zero);
    return list;
  }
}
"@
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false
$A = [System.Windows.Automation.AutomationElement]
$walker = [System.Windows.Automation.TreeWalker]::RawViewWalker
$lines = New-Object System.Collections.Generic.List[string]
$script:count = 0
$script:target = $null

function Describe($e, [int]$depth) {
  $c = $e.Current
  $type = $c.ControlType.ProgrammaticName -replace '^ControlType\.', ''
  $r = $c.BoundingRectangle
  $line = ("  " * $depth) + $type + ' "' + $c.Name + '"'
  if (-not $r.IsEmpty) { $line += " [{0:0},{1:0} {2:0}x{3:0}]" -f $r.X, $r.Y, $r.Width, $r.Height }
  if ($c.IsOffscreen) { $line += " off" }
  if (-not $c.IsEnabled) { $line += " disabled" }
  if ($c.HasKeyboardFocus) { $line += " focus" }
  $pats = @($e.GetSupportedPatterns() | ForEach-Object { $_.ProgrammaticName -replace 'Identifiers\.Pattern$', '' -replace 'Pattern$', '' })
  if ($pats.Count) { $line += " {" + ($pats -join ",") + "}" }
  $o = $null
  if ($e.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$o)) { $line += ' value="' + $o.Current.Value + '"' }
  if ($e.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$o)) { $line += " toggle=" + $o.Current.ToggleState }
  if ($e.TryGetCurrentPattern([System.Windows.Automation.RangeValuePattern]::Pattern, [ref]$o)) {
    $line += " range={0:0.##}/{1:0.##}..{2:0.##}" -f $o.Current.Value, $o.Current.Minimum, $o.Current.Maximum
  }
  if ($e.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$o) -and $o.Current.IsSelected) { $line += " selected" }
  if ($c.HelpText) { $line += ' help="' + $c.HelpText + '"' }
  return $line
}

function Walk($e, [int]$depth) {
  if ($script:count -ge $Max) { return }
  $script:count++
  $lines.Add((Describe $e $depth))
  if ($Invoke -and -not $script:target -and $e.Current.Name -eq $Invoke) { $script:target = $e }
  $child = $walker.GetFirstChild($e)
  while ($child) {
    Walk $child ($depth + 1)
    $child = $walker.GetNextSibling($child)
  }
}

$hwnds = [LumenUia]::Windows([uint32]$ProcessId)
if ($hwnds.Count -eq 0) { throw "no visible LumenHost window for process $ProcessId" }
foreach ($h in $hwnds) { Walk ($A::FromHandle($h)) 0 }
$lines | ForEach-Object { $_ }
"elements: $($script:count)"
if ($Out) { [IO.File]::WriteAllLines($Out, $lines, (New-Object System.Text.UTF8Encoding $false)) }

if ($Invoke) {
  if (-not $script:target) { throw "no element named `"$Invoke`"" }
  $o = $null
  if ($script:target.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$o)) { $o.Invoke(); "invoked: $Invoke" }
  elseif ($script:target.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$o)) { $o.Toggle(); "toggled: $Invoke" }
  elseif ($script:target.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$o)) { $o.Select(); "selected: $Invoke" }
  else { throw "`"$Invoke`" has no Invoke, Toggle or SelectionItem pattern" }
}

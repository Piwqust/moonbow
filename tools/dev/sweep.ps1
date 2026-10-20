# State-matrix sweep (qa/round5/METHOD.md, layer 2): runs every scenario of a scenarios file in its own
# fresh, invisible Lumen instance, captures a full-resolution PNG and a widget-tree dump (and a hit map
# when the scenario asks), audits the dump with ui-audit.py and writes <Out>\index.html, a contact sheet
# for a human or a vision model.
#
# Usage:
#   powershell -File tools\dev\sweep.ps1 -Exe %TEMP%\lumen-qa\r5-base\Lumen.exe -Out %TEMP%\lumen-qa\r5sw-run
#              [-Scenarios qa\round5\scenarios.json] [-Filter <regex>] [-MaxPriority 1..3] [-Parallel 4]
#              [-Prefix r5sw] [-List] [-NoHitmap]
#
# Each instance is started with --new-instance --automation (invisible, no focus, startup volume 0)
# and killed by its own PID. Never touches the real mouse or keyboard: input is posted to the window.
# Steps use tools\dev\command.ps1 ("cmd") when it exists, otherwise their keys/click fallback.
# Scenario format: see the "_doc" block at the top of qa\round5\scenarios.json.
[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$Exe = "",
  [string]$Scenarios = "",
  [string]$Out = "",
  [string]$Filter = "",
  [int]$MaxPriority = 9,
  [int]$Parallel = 4,
  [string]$Prefix = "r5sw",
  [switch]$List,
  [switch]$NoHitmap,
  [switch]$AssumeScale,
  [switch]$Reaudit,   # re-run ui-audit on the dumps of an earlier -Out and rebuild index.html (no app runs)
  [int]$TimeoutMin = 30,
  # Internal: worker mode.
  [string]$WorkerPlan = "",
  [int]$WorkerId = 0
)
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $here "..\..")).Path
if (-not $Scenarios) { $Scenarios = Join-Path $repo "qa\round5\scenarios.json" }
$allowFile = Join-Path $repo "qa\round5\audit-allow.json"
$auditPy = Join-Path $here "ui-audit.py"
$dumpTreePs1 = Join-Path $here "dump-tree.ps1"
$commandPs1 = Join-Path $here "command.ps1"
$winposPs1 = Join-Path $here "winpos.ps1"
$hitmapPs1 = Join-Path $here "hitmap.ps1"
$qaTemp = Join-Path $env:TEMP "lumen-qa"

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
if (-not ("LumenSweep" -as [type])) {
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public static class LumenSweep {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public static IntPtr[] Windows(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid) return true;
      var c = new StringBuilder(64); GetClassName(h, c, 64);
      if (c.ToString() == "LumenHost") list.Add(h);
      return true; }, IntPtr.Zero);
    return list.ToArray();
  }
  public static string Title(IntPtr h) { var t = new StringBuilder(512); GetWindowText(h, t, 512); return t.ToString(); }
}
"@
}
[LumenSweep]::SetProcessDPIAware() | Out-Null

# --- small helpers -------------------------------------------------------------------------------

function Get-Prop($o, [string]$name, $default = $null) {
  if ($null -eq $o) { return $default }
  if ($o -is [System.Collections.IDictionary]) { if ($o.Contains($name)) { return $o[$name] } else { return $default } }
  $p = $o.PSObject.Properties[$name]
  if ($p) { return $p.Value } else { return $default }
}
function To-Hash($o) {
  # PSCustomObject (from ConvertFrom-Json) -> nested hashtables/arrays.
  if ($null -eq $o) { return $null }
  if ($o -is [System.Collections.IDictionary]) { $h = [ordered]@{}; foreach ($k in $o.Keys) { $h[$k] = To-Hash $o[$k] }; return $h }
  if ($o -is [System.Management.Automation.PSCustomObject]) {
    $h = [ordered]@{}; foreach ($p in $o.PSObject.Properties) { $h[$p.Name] = To-Hash $p.Value }; return $h
  }
  if ($o -is [System.Collections.IEnumerable] -and -not ($o -is [string])) { return ,@($o | ForEach-Object { To-Hash $_ }) }
  return $o
}
function Merge-Hash($a, $b) {
  $r = [ordered]@{}
  if ($a) { foreach ($k in $a.Keys) { $r[$k] = $a[$k] } }
  if ($b) { foreach ($k in $b.Keys) { $r[$k] = $b[$k] } }
  return $r
}
function Expand-Vars([string]$s, $vars) {
  if ($null -eq $s) { return $s }
  foreach ($k in $vars.Keys) { $s = $s.Replace('$' + $k, [string]$vars[$k]) }
  return [Environment]::ExpandEnvironmentVariables($s)
}
function Safe-Name([string]$s) { ($s -replace '[^A-Za-z0-9._-]', '_') }
function Write-Utf8([string]$path, [string]$text) { [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false)) }
function Html([string]$s) { if ($null -eq $s) { return "" }; [System.Net.WebUtility]::HtmlEncode($s) }
function Log([string]$msg) {
  $line = "{0:HH:mm:ss} [w{1}] {2}" -f (Get-Date), $WorkerId, $msg
  if ($script:logFile) { [IO.File]::AppendAllText($script:logFile, $line + "`r`n") }
  Write-Host $line
}

# --- scenario expansion --------------------------------------------------------------------------

function Expand-Scenarios($doc) {
  $dims = To-Hash (Get-Prop $doc "dimensions")
  $defaults = To-Hash (Get-Prop $doc "defaults")
  $list = @()
  foreach ($raw in (Get-Prop $doc "scenarios")) {
    $sc = To-Hash $raw
    $vary = $sc["vary"]
    $combos = @(@{})
    if ($vary) {
      foreach ($dim in $vary.Keys) {
        $next = @()
        foreach ($c in $combos) { foreach ($v in @($vary[$dim])) { $n = @{}; foreach ($k in $c.Keys) { $n[$k] = $c[$k] }; $n[$dim] = [string]$v; $next += $n } }
        $combos = $next
      }
    }
    foreach ($combo in $combos) {
      $s = Merge-Hash $defaults $null
      $s["settings"] = Merge-Hash $defaults["settings"] $null
      $s["ui"] = Merge-Hash $defaults["ui"] $null
      $pre = @(); $post = @(); $requires = @()
      $suffix = @()
      foreach ($dim in @($combo.Keys | Sort-Object { @("size", "wsize", "theme", "barStyle", "bar", "lang", "scale").IndexOf($_) })) {
        $val = $combo[$dim]
        $suffix += $val
        $spec = $null
        if ($dims -and $dims[$dim]) { $spec = $dims[$dim][$val] }
        if ($null -eq $spec) { throw "scenario $($sc['name']): unknown $dim value '$val' (add it to dimensions.$dim)" }
        foreach ($k in $spec.Keys) {
          switch ($k) {
            "settings" { $s["settings"] = Merge-Hash $s["settings"] $spec[$k] }
            "ui" { $s["ui"] = Merge-Hash $s["ui"] $spec[$k] }
            "preSteps" { $pre += @($spec[$k]) }
            "postSteps" { $post += @($spec[$k]) }
            "requires" { $requires += @($spec[$k]) }
            default { $s[$k] = $spec[$k] }
          }
        }
      }
      foreach ($k in $sc.Keys) {
        switch ($k) {
          "vary" {}
          "settings" { $s["settings"] = Merge-Hash $s["settings"] $sc[$k] }
          "ui" { $s["ui"] = Merge-Hash $s["ui"] $sc[$k] }
          "requires" { $requires += @($sc[$k]) }
          default { $s[$k] = $sc[$k] }
        }
      }
      $s["steps"] = @($pre) + @($sc["steps"] | Where-Object { $null -ne $_ }) + @($post)
      $s["requires"] = @($requires | Select-Object -Unique)
      $s["base"] = $sc["name"]
      $s["variant"] = ($suffix -join " ")
      $s["name"] = if ($suffix.Count) { $sc["name"] + "." + (($suffix | ForEach-Object { Safe-Name $_ }) -join ".") } else { $sc["name"] }
      $list += ,$s
    }
  }
  return $list
}

# --- Win32 access ------------------------------------------------------------------------------

function Find-Main([int]$procId) {
  foreach ($h in [LumenSweep]::Windows([uint32]$procId)) {
    if ([LumenSweep]::IsWindowVisible($h) -and [LumenSweep]::GetWindow($h, 4) -eq [IntPtr]::Zero) { return $h }
  }
  return [IntPtr]::Zero
}
function Find-Target([int]$procId, $spec) {
  # spec: "main" | "owned" | @{ title = regex }
  if ($null -eq $spec -or $spec -eq "main") { return Find-Main $procId }
  $re = if ($spec -is [string]) { if ($spec -eq "owned") { "" } else { $spec } } else { [string](Get-Prop $spec "title" "") }
  foreach ($h in [LumenSweep]::Windows([uint32]$procId)) {
    if (-not [LumenSweep]::IsWindowVisible($h)) { continue }
    $owned = [LumenSweep]::GetWindow($h, 4) -ne [IntPtr]::Zero
    if ($re) { if ([LumenSweep]::Title($h) -match $re) { return $h } }
    elseif ($owned) { return $h }
  }
  return [IntPtr]::Zero
}
function Get-Scale([IntPtr]$h) {
  if ($script:treeScale) { return $script:treeScale }
  $d = [LumenSweep]::GetDpiForWindow($h); if ($d -le 0) { $d = 96 }; return $d / 96.0
}
function Client-Size([IntPtr]$h) { $r = New-Object LumenSweep+RECT; [LumenSweep]::GetClientRect($h, [ref]$r) | Out-Null; return @($r.R, $r.B) }
function Post([IntPtr]$h, [int]$msg, [int64]$w, [int64]$l) { [LumenSweep]::PostMessage($h, [uint32]$msg, [IntPtr]$w, [IntPtr]$l) | Out-Null }
function LParam([int]$x, [int]$y) { [int64](($y -shl 16) -bor ($x -band 0xFFFF)) }

function Send-Key([IntPtr]$h, [string]$combo) {
  $parts = @($combo -split '\+' | Where-Object { $_ -ne "" })
  if ($combo.EndsWith("++")) { $parts += "Oemplus" }
  $mods = 0; $key = $parts[-1]
  foreach ($p in $parts[0..($parts.Count - 2)]) {
    switch ($p.ToLower()) { "ctrl" { $mods = $mods -bor 1 } "shift" { $mods = $mods -bor 2 } "alt" { $mods = $mods -bor 4 } }
  }
  if ($parts.Count -eq 1) { $mods = 0 }
  $vk = [int][System.Windows.Forms.Keys]::$key
  if ($vk -eq 0) { throw "unknown key '$key' in '$combo' (use System.Windows.Forms.Keys names)" }
  if ($mods) { Post $h 0x802D $mods 0 }
  Post $h 0x100 $vk 1
  Post $h 0x101 $vk 0xC0000001
  if ($mods) { Start-Sleep -Milliseconds 40; Post $h 0x802D 0 0 }
}
function Send-Text([IntPtr]$h, [string]$text) { foreach ($ch in $text.ToCharArray()) { Post $h 0x102 ([int]$ch) 1 } }
function Send-Mouse([IntPtr]$h, [string]$kind, [int]$x, [int]$y, [int]$delta = -1, [int]$mods = 0) {
  $mk = 0; if ($mods -band 1) { $mk = $mk -bor 0x8 }; if ($mods -band 2) { $mk = $mk -bor 0x4 }
  if ($mods) { Post $h 0x802D $mods 0 }
  $l = LParam $x $y
  switch ($kind) {
    "move" { Post $h 0x200 $mk $l }
    "click" { Post $h 0x200 $mk $l; Start-Sleep -Milliseconds 30; Post $h 0x201 (0x1 -bor $mk) $l; Post $h 0x202 $mk $l }
    "double" { Post $h 0x200 $mk $l; Start-Sleep -Milliseconds 30; Post $h 0x201 (0x1 -bor $mk) $l; Post $h 0x202 $mk $l
               Start-Sleep -Milliseconds 40; Post $h 0x203 (0x1 -bor $mk) $l; Post $h 0x202 $mk $l }
    "right" { Post $h 0x200 $mk $l; Start-Sleep -Milliseconds 30; Post $h 0x204 (0x2 -bor $mk) $l; Post $h 0x205 $mk $l }
    "wheel" {
      $pt = New-Object LumenSweep+POINT; $pt.X = $x; $pt.Y = $y
      [LumenSweep]::ClientToScreen($h, [ref]$pt) | Out-Null
      Post $h 0x200 $mk $l
      Post $h 0x20A ([int64](($delta * 120) -shl 16) -band 0xFFFFFFFF) (LParam $pt.X $pt.Y)
    }
  }
  if ($mods) { Start-Sleep -Milliseconds 30; Post $h 0x802D 0 0 }
}

# --- tree dumps ----------------------------------------------------------------------------------

$script:dumpV2 = (Test-Path $dumpTreePs1) -and ((Get-Content -Raw $dumpTreePs1) -match '\$All\b') -and ((Get-Content -Raw $dumpTreePs1) -match '\$Out\b')
$script:hasCommand = Test-Path $commandPs1

function Dump-Tree([int]$procId, [IntPtr]$h, [string]$path) {
  if ($script:dumpV2) {
    try { & $dumpTreePs1 -ProcessId $procId -All -Out $path -Quiet | Out-Null } catch { }
    if (Test-Path $path) { return $true }
  }
  $tmp = Join-Path $env:TEMP "lumen-tree-$procId.txt"
  if (Test-Path $tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue }
  Post $h (0x8000 + 43) 0 0
  $len = -1
  for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 50
    if (Test-Path $tmp) {
      $n = (Get-Item $tmp).Length
      if ($n -gt 0 -and $n -eq $len) { Copy-Item $tmp $path -Force; return $true }
      $len = $n
    }
  }
  return $false
}

function Short-Type([string]$t) {
  $t = $t.Trim(); if ($t.StartsWith("class ")) { $t = $t.Substring(6) }
  $t = $t.Replace("``anonymous namespace'::", "")
  $i = $t.LastIndexOf("'::"); if ($i -ge 0) { $t = $t.Substring($i + 3) }
  $i = $t.LastIndexOf("::"); if ($i -ge 0) { $t = $t.Substring($i + 2) }
  return $t
}
function Parse-Tree([string]$path) {
  # Returns windows: @{ title; scale; client; nodes = list of node objects }.
  $wins = New-Object System.Collections.ArrayList
  $win = $null; $stack = New-Object System.Collections.ArrayList; $layer = 0
  $lineNo = 0
  foreach ($line in [IO.File]::ReadAllLines($path)) {
    $lineNo++
    if ($line.StartsWith("=== window")) {
      $win = @{ title = ""; scale = $null; client = $null; nodes = New-Object System.Collections.ArrayList; layers = 0 }
      if ($line -match 'title="((?:[^"\\]|\\.)*)"') { $win.title = $matches[1] -replace '\\(.)', '$1' }
      if ($line -match '\bscale=([\d.]+)') { $win.scale = [double]$matches[1] }
      if ($line -match '\bclient=(\d+)x(\d+)') { $win.client = @([double]$matches[1], [double]$matches[2]) }
      [void]$wins.Add($win); $stack.Clear(); $layer = 0; continue
    }
    if ($line.StartsWith("--- popup")) { if ($null -eq $win) { $win = @{ title = ""; scale = $null; nodes = New-Object System.Collections.ArrayList; layers = 0 }; [void]$wins.Add($win) }; $win.layers++; $layer = $win.layers; $stack.Clear(); continue }
    if ($line -notmatch '^( *)(.*?) \[(-?[\d.]+),(-?[\d.]+) (-?[\d.]+)x(-?[\d.]+)\](.*)$') { continue }
    if ($null -eq $win) { $win = @{ title = ""; scale = $null; nodes = New-Object System.Collections.ArrayList; layers = 0 }; [void]$wins.Add($win) }
    $depth = $matches[1].Length / 2; $body = $matches[2]; $rest = $matches[7]
    $x = [double]$matches[3]; $y = [double]$matches[4]; $w = [double]$matches[5]; $hh = [double]$matches[6]
    $id = ""; if ($body -match ' #(\S+)$') { $id = $matches[1]; $body = $body.Substring(0, $body.Length - $id.Length - 2) }
    # v2: every painted string is a text="..." entry; flags are tested with the strings blanked out.
    $text = $null
    $tm = [regex]::Matches($rest, ' (?:text|tip)="((?:[^"\\]|\\.)*)"')
    if ($tm.Count) { $text = (@($tm | ForEach-Object { $_.Groups[1].Value -replace '\\n', "`n" -replace '\\(.)', '$1' }) -join "`n") }
    $rest = [regex]::Replace($rest, '="(?:[^"\\]|\\.)*"', '=""')
    while ($stack.Count -gt 0 -and $stack[$stack.Count - 1].Depth -ge $depth) { $stack.RemoveAt($stack.Count - 1) }
    $parent = if ($stack.Count) { $stack[$stack.Count - 1] } else { $null }
    $hidden = $rest -match '\bhidden\b'
    $node = [pscustomobject]@{ Type = (Short-Type $body); Id = $id; X = $x; Y = $y; W = $w; H = $hh; Depth = $depth; Layer = $layer
      Text = $text; Flags = $rest; Line = $lineNo; Parent = $parent; Visible = (-not $hidden) -and ($null -eq $parent -or $parent.Visible) }
    [void]$win.nodes.Add($node); [void]$stack.Add($node)
  }
  return ,$wins
}
function Pick-Window($wins, [IntPtr]$h) {
  if ($wins.Count -le 1) { return $wins[0] }
  $t = [LumenSweep]::Title($h)
  foreach ($w in $wins) { if ($w.title -eq $t) { return $w } }
  return $wins[0]
}

# Resolves a selector to client pixels. A selector is "x,y" (DIPs) or an object:
#   type (regex on the short type), id, text (regex, v2 dumps), popup (true/false), parent (regex), nth (negative from the end),
#   then the point inside it: center by default, or dx/dy (DIPs from the left/top, negative from the right/bottom),
#   fx/fy (fractions), card k (ItemGrid card, cardStyle square|video), row k (list row, rowTop extra DIPs).
function Resolve-Point($sel, [int]$procId, [IntPtr]$h) {
  $scale = Get-Scale $h
  if ($sel -is [string]) { $p = $sel -split ','; return @([int]([double]$p[0] * $scale), [int]([double]$p[1] * $scale)) }
  $tmp = Join-Path $script:workDir "select.txt"
  if (-not (Dump-Tree $procId $h $tmp)) { throw "no tree dump for selector" }
  $wins = Parse-Tree $tmp
  $win = Pick-Window $wins $h
  if ($win.scale) { $scale = $win.scale }
  $cands = @($win.nodes | Where-Object { $_.Visible -and $_.W -gt 0 -and $_.H -gt 0 })
  $type = Get-Prop $sel "type"; $id = Get-Prop $sel "id"; $text = Get-Prop $sel "text"; $popup = Get-Prop $sel "popup"; $parent = Get-Prop $sel "parent"
  if ($type) { $cands = @($cands | Where-Object { $_.Type -match "^(?:$type)$" }) }
  if ($id) { $cands = @($cands | Where-Object { $_.Id -eq $id }) }
  if ($text) { $cands = @($cands | Where-Object { $_.Text -and $_.Text -match $text }) }
  if ($null -ne $popup) { $cands = @($cands | Where-Object { ($_.Layer -gt 0) -eq [bool]$popup }) }
  if ($parent) { $cands = @($cands | Where-Object { $_.Parent -and $_.Parent.Type -match "^(?:$parent)$" }) }
  if ($cands.Count -eq 0) { throw "selector matched nothing: $(ConvertTo-Json $sel -Compress)" }
  $nth = [int](Get-Prop $sel "nth" 0)
  if ($nth -lt 0) { $nth = $cands.Count + $nth }
  if ($nth -ge $cands.Count -or $nth -lt 0) { throw "selector nth $nth out of range ($($cands.Count) matches): $(ConvertTo-Json $sel -Compress)" }
  $n = $cands[$nth]
  $x = $n.X + $n.W / 2; $y = $n.Y + $n.H / 2
  $card = Get-Prop $sel "card"; $row = Get-Prop $sel "row"
  if ($null -ne $card) {
    # ItemGrid card layout for stretched pages (ItemGrid::Compute): min width 200 (square) / 316 (video), gap 20.
    $video = (Get-Prop $sel "cardStyle" "square") -eq "video"
    $min = if ($video) { 316 } else { 200 }
    $cols = [math]::Max(1, [math]::Floor(($n.W + 20) / ($min + 20)))
    $cw = ($n.W - 20 * ($cols - 1)) / $cols
    $thumb = if ($video) { [math]::Round($cw * 178 / 316) } else { [math]::Round($cw) }
    $cardH = $thumb + 68; $gapY = if ($video) { 40 } else { 32 }
    $top = [double](Get-Prop $sel "rowTop" 0)
    $k = [int]$card
    $x = $n.X + ($k % $cols) * ($cw + 20) + $cw / 2
    $y = $n.Y + $top + [math]::Floor($k / $cols) * ($cardH + $gapY) + $thumb / 2
  } elseif ($null -ne $row) {
    $x = $n.X + [math]::Min(200, $n.W / 2)
    $y = $n.Y + [double](Get-Prop $sel "rowTop" 0) + [int]$row * 42 + 21
  } else {
    $dx = Get-Prop $sel "dx"; $dy = Get-Prop $sel "dy"; $fx = Get-Prop $sel "fx"; $fy = Get-Prop $sel "fy"
    if ($null -ne $fx) { $x = $n.X + $n.W * [double]$fx }
    if ($null -ne $fy) { $y = $n.Y + $n.H * [double]$fy }
    if ($null -ne $dx) { $x = if ([double]$dx -ge 0) { $n.X + [double]$dx } else { $n.X + $n.W + [double]$dx } }
    if ($null -ne $dy) { $y = if ([double]$dy -ge 0) { $n.Y + [double]$dy } else { $n.Y + $n.H + [double]$dy } }
  }
  return @([int]($x * $scale), [int]($y * $scale))
}

function Capture-Png([IntPtr]$h, [string]$path, [string]$thumb) {
  $r = New-Object LumenSweep+RECT
  [LumenSweep]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  if ($w -le 0 -or $hh -le 0) { throw "window has no size" }
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc()
  [LumenSweep]::PrintWindow($h, $dc, 2) | Out-Null   # PW_RENDERFULLCONTENT: includes the DComp layer
  $g.ReleaseHdc($dc); $g.Dispose()
  # The client area only, without the window's invisible resize band (as capture.ps1).
  $cr = New-Object LumenSweep+RECT; [LumenSweep]::GetClientRect($h, [ref]$cr) | Out-Null
  $o = New-Object LumenSweep+POINT; [LumenSweep]::ClientToScreen($h, [ref]$o) | Out-Null
  $cx = [math]::Max(0, $o.X - $r.L); $cy = [math]::Max(0, $o.Y - $r.T)
  $cw = [math]::Min($cr.R, $w - $cx); $ch = [math]::Min($cr.B, $hh - $cy)
  if ($cw -gt 0 -and $ch -gt 0 -and ($cw -ne $w -or $ch -ne $hh)) {
    $c = $bmp.Clone((New-Object System.Drawing.Rectangle $cx, $cy, $cw, $ch), $bmp.PixelFormat)
    $bmp.Dispose(); $bmp = $c; $w = $cw; $hh = $ch
  }
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $tw = [math]::Min(560, $w); $th = [int]($hh * $tw / $w)
  $t = New-Object System.Drawing.Bitmap $tw, $th
  $g2 = [System.Drawing.Graphics]::FromImage($t); $g2.InterpolationMode = 'HighQualityBicubic'
  $g2.DrawImage($bmp, 0, 0, $tw, $th); $g2.Dispose()
  $t.Save($thumb, [System.Drawing.Imaging.ImageFormat]::Jpeg)
  $t.Dispose(); $bmp.Dispose()
  return "${w}x${hh}"
}

# --- one scenario ----------------------------------------------------------------------------------

function Prepare-DataDir([string]$dir, $sc, [string]$warm) {
  for ($i = 0; $i -lt 10 -and (Test-Path $dir); $i++) {
    try { Remove-Item -Recurse -Force $dir -ErrorAction Stop } catch { Start-Sleep -Milliseconds 300 }
  }
  New-Item -ItemType Directory -Force $dir | Out-Null
  $ui = [ordered]@{}
  if (-not $sc["onboarding"]) { $ui["onboarded"] = "1" }
  if ($sc["ui"]) { foreach ($k in $sc["ui"].Keys) { $ui[$k] = [string]$sc["ui"][$k] } }
  Write-Utf8 (Join-Path $dir "ui.json") (ConvertTo-Json $ui -Depth 6)
  $values = [ordered]@{ "audio.startupVolumeMode" = "fixed"; "audio.startupVolume" = 0 }
  # String values expand %TEMP% (output folders of snapshot and recording scenarios).
  if ($sc["settings"]) { foreach ($k in $sc["settings"].Keys) { $v = $sc["settings"][$k]; $values[$k] = if ($v -is [string]) { [Environment]::ExpandEnvironmentVariables($v) } else { $v } } }
  $values["audio.startupVolumeMode"] = "fixed"; $values["audio.startupVolume"] = 0   # never louder than 0
  Write-Utf8 (Join-Path $dir "settings.json") (ConvertTo-Json ([ordered]@{ version = 1; values = $values }) -Depth 8)
  $roots = @($sc["libraryResolved"])
  if ($roots.Count) { Write-Utf8 (Join-Path $dir "library.json") (ConvertTo-Json ([ordered]@{ version = 1; roots = $roots }) -Depth 4) }
  if ($sc["seedResolved"]) { foreach ($f in $sc["seedResolved"].Keys) { Write-Utf8 (Join-Path $dir (Split-Path -Leaf $f)) ([string]$sc["seedResolved"][$f]) } }
  if ($warm -and (Test-Path $warm)) { Copy-Item $warm (Join-Path $dir "library-cache.json") -Force }
}

function Wait-Stable([string]$file, [int]$quietMs, [int]$maxMs) {
  $sw = [Diagnostics.Stopwatch]::StartNew(); $last = -1; $since = 0
  while ($sw.ElapsedMilliseconds -lt $maxMs) {
    Start-Sleep -Milliseconds 200
    $n = if (Test-Path $file) { (Get-Item $file).Length } else { -1 }
    if ($n -gt 0 -and $n -eq $last) { $since += 200; if ($since -ge $quietMs) { return $true } } else { $since = 0 }
    $last = $n
  }
  return $false
}

function Start-Lumen($sc, [string]$dataDir) {
  $argList = @("--new-instance", "--automation", "--data-dir", "`"$dataDir`"")
  if ($sc["scale"] -and [double]$sc["scale"] -ne 1) { $argList += @("--scale", [string]$sc["scale"]) }
  foreach ($f in @($sc["filesResolved"])) { if ($f) { $argList += "`"$f`"" } }
  $p = Start-Process $script:plan.exe -ArgumentList $argList -PassThru
  Write-Utf8 (Join-Path $script:plan.out "pids\$($p.Id).pid") "$($sc['name'])"
  return $p.Id
}

function Run-Step($step, [int]$procId) {
  $h = Find-Target $procId $script:target
  if ($h -eq [IntPtr]::Zero -and -not (Get-Prop $step "window")) { throw "target window '$($script:target)' not found" }
  $after = [int](Get-Prop $step "after" 250)
  $cmd = Get-Prop $step "cmd"
  if ($cmd -and $script:hasCommand) {
    # A failed command (unknown name, older command.ps1) falls back to the step's keys/click.
    $failed = $null
    try { $global:LASTEXITCODE = 0; & $commandPs1 -ProcessId $procId -Command $cmd | Out-Null; if ($LASTEXITCODE) { $failed = "exit $LASTEXITCODE" } }
    catch { $failed = $_.Exception.Message }
    if (-not $failed) {
      if (-not (Get-Prop $step "noIdle")) { try { & $commandPs1 -ProcessId $procId -Command "wait-idle" -TimeoutMs 4000 -Quiet | Out-Null } catch {} }
      Start-Sleep -Milliseconds $after; return
    }
    $script:notes += "cmd '$cmd' failed ($failed), used the fallback"
  }
  $acted = $false
  $win = Get-Prop $step "window"
  if ($null -ne $win) {
    $script:target = $win; $acted = $true
    $deadline = [DateTime]::Now.AddSeconds(5)
    while ((Find-Target $procId $win) -eq [IntPtr]::Zero -and [DateTime]::Now -lt $deadline) { Start-Sleep -Milliseconds 150 }
    $h = Find-Target $procId $win
    if ($h -eq [IntPtr]::Zero) { throw "window $(ConvertTo-Json $win -Compress) did not appear" }
  }
  $size = Get-Prop $step "size"
  if ($size) { Apply-Size $procId $size; $acted = $true }
  foreach ($k in @(Get-Prop $step "keys" @())) { if ($k) { Send-Key $h $k; Start-Sleep -Milliseconds 120; $acted = $true } }
  $text = Get-Prop $step "type"
  if ($text) { Send-Text $h ([Environment]::ExpandEnvironmentVariables([string]$text)); $acted = $true }
  $mods = 0; foreach ($m in @(Get-Prop $step "mods" @())) { switch ("$m".ToLower()) { "ctrl" { $mods = $mods -bor 1 } "shift" { $mods = $mods -bor 2 } "alt" { $mods = $mods -bor 4 } } }
  foreach ($kind in "move", "click", "double", "right", "wheel") {
    $sel = Get-Prop $step $kind
    if ($null -eq $sel) { continue }
    $pt = Resolve-Point $sel $procId $h
    Send-Mouse $h $kind $pt[0] $pt[1] ([int](Get-Prop $step "delta" -1)) $mods
    $acted = $true
  }
  $wait = Get-Prop $step "wait"
  if ($wait) { Start-Sleep -Milliseconds ([int]$wait); $acted = $true }
  $cap = Get-Prop $step "capture"
  if ($cap) { Take-Capture $procId ([string]$cap) $false; $acted = $true }
  if (-not $acted) {
    if ($cmd) { $script:notes += "step '$cmd' needs command.ps1 (no fallback)"; $script:partial = $true }
    return
  }
  Start-Sleep -Milliseconds $after
}

function Apply-Size([int]$procId, [string]$size) {
  # Sizes the current target window: the main one, or an owned one (Settings) by its title.
  $h = Find-Target $procId $script:target
  if ($h -eq [IntPtr]::Zero) { $h = Find-Main $procId }
  $wp = @{ ProcessId = $procId }
  if ($h -ne (Find-Main $procId)) { $wp["Title"] = [LumenSweep]::Title($h) }
  switch -regex ($size) {
    '^min$' { & $winposPs1 @wp -Size "1,1" | Out-Null }
    '^max$' { & $winposPs1 @wp -Maximize | Out-Null }
    '^restore$' { & $winposPs1 @wp -Restore | Out-Null }
    '^fullscreen$' { Send-Key $h "F" }
    '^\d+[x,]\d+$' { & $winposPs1 @wp -Size ($size -replace 'x', ',') | Out-Null }
    default { throw "unknown size '$size'" }
  }
  Start-Sleep -Milliseconds 450
}

function Take-Capture([int]$procId, [string]$label, [bool]$final) {
  $sc = $script:sc
  $h = Find-Target $procId $script:target
  if ($h -eq [IntPtr]::Zero) { throw "no window to capture" }
  $dir = $script:workDir
  $base = Safe-Name $label
  $png = Join-Path $dir "$base.png"; $jpg = Join-Path $dir "$base.thumb.jpg"; $tree = Join-Path $dir "$base.tree.txt"
  if ($script:hasCommand -and $final) { try { & $commandPs1 -ProcessId $procId -Command "wait-idle" -TimeoutMs 3000 -Quiet | Out-Null } catch {} }
  $px = Capture-Png $h $png $jpg
  $okTree = Dump-Tree $procId $h $tree
  $cs = Client-Size $h
  $state = "normal"
  if ($script:fullscreen) { $state = "fullscreen" } elseif ([LumenSweep]::IsZoomed($h)) { $state = "maximized" }
  $hit = $null
  if ($final -and $sc["hitmap"] -and -not $script:plan.noHitmap) {
    $hit = Join-Path $dir "$base.hitmap.txt"
    $step = if ($cs[0] -gt 1400) { 16 } else { 8 }
    $title = [LumenSweep]::Title($h)
    try {
      $lines = & $hitmapPs1 -ProcessId $procId -Step $step -Title $title 2>&1 | ForEach-Object { "$_" }
      Write-Utf8 $hit (($lines -join "`r`n") + "`r`n")
      $script:hitStep = $step
    } catch { $script:notes += "hitmap failed: $($_.Exception.Message)"; $hit = $null }
  }
  $audit = $null
  if ($okTree) {
    $json = Join-Path $dir "$base.audit.json"; $md = Join-Path $dir "$base.audit.md"
    # Only the captured window: the others (the main window behind Settings) have their own scenarios.
    $a = @($auditPy, $tree, "--allow", $allowFile, "--json", $json, "--md", $md, "--quiet", "--context", $sc["name"], "--state", $state,
           "--window", ("^" + [regex]::Escape([LumenSweep]::Title($h)) + "$"))
    if ($hit) { $a += @("--hitmap", $hit, "--hitmap-step", "$($script:hitStep)", "--hitmap-window", ("^" + [regex]::Escape([LumenSweep]::Title($h)) + "$")) }
    & $script:plan.python @a 2>&1 | Out-Null
    if (Test-Path $json) { $audit = "$base.audit.json" }
  }
  $script:captures += [ordered]@{ label = $label; png = "$base.png"; thumb = "$base.thumb.jpg"; pixels = $px; client = "$($cs[0])x$($cs[1])"
    state = $state; tree = $(if ($okTree) { "$base.tree.txt" } else { $null }); hitmap = $(if ($hit) { "$base.hitmap.txt" } else { $null })
    audit = $audit; auditMd = $(if ($audit) { "$base.audit.md" } else { $null }); window = [LumenSweep]::Title($h) }
}

function Run-Scenario($sc) {
  $script:sc = $sc
  $script:notes = @(); $script:partial = $false; $script:captures = @(); $script:target = "main"; $script:fullscreen = $false
  $script:treeScale = $null
  $name = $sc["name"]
  $script:workDir = Join-Path $script:plan.out (Safe-Name $name)
  $result = [ordered]@{ name = $name; base = $sc["base"]; variant = $sc["variant"]; priority = $sc["priority"]; screen = $sc["screen"]
    description = $sc["description"]; status = "ok"; notes = @(); captures = @(); seconds = 0 }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  foreach ($r in @($sc["requires"])) {
    if ($r -eq "scale" -and -not $script:plan.scale) { $result.status = "skipped"; $result.notes = @("needs Lumen --scale (exe without the v2 dump)"); return $result }
    if ($r -eq "cmd" -and -not $script:hasCommand) { $result.status = "skipped"; $result.notes = @("needs tools\dev\command.ps1"); return $result }
  }
  $dataDir = Join-Path $qaTemp ("{0}-w{1}" -f $script:plan.prefix, $WorkerId)
  $procId = 0
  try {
    $warm = $null
    if (@($sc["libraryResolved"]).Count) { $warm = $script:plan.warm[(@($sc["libraryResolved"]) -join "|")] }
    Prepare-DataDir $dataDir $sc $warm
    $procId = Start-Lumen $sc $dataDir
    $deadline = [DateTime]::Now.AddSeconds(20)
    $wantOwned = [bool]$sc["onboarding"]
    while ((Find-Main $procId) -eq [IntPtr]::Zero -and [DateTime]::Now -lt $deadline) {
      if ($wantOwned -and (Find-Target $procId "owned") -ne [IntPtr]::Zero) { break }
      Start-Sleep -Milliseconds 100
    }
    $h = Find-Main $procId
    if ($h -eq [IntPtr]::Zero -and -not $wantOwned) { throw "no main window after 20 s" }
    if ($h -ne [IntPtr]::Zero) {
      $probe = Join-Path $script:workDir "probe.txt"
      for ($i = 0; $i -lt 20 -and -not (Dump-Tree $procId $h $probe); $i++) { Start-Sleep -Milliseconds 200 }
      if (Test-Path $probe) {
        $wins = Parse-Tree $probe
        if ($wins.Count -and $wins[0].scale) { $script:treeScale = $wins[0].scale }
        Remove-Item $probe -Force
      }
    }
    if (@($sc["libraryResolved"]).Count -and -not $warm) {
      Wait-Stable (Join-Path $dataDir "library-cache.json") 1000 20000 | Out-Null
    }
    Start-Sleep -Milliseconds ([int]($sc["settleMs"]))
    if ($sc["size"]) {
      if ($sc["size"] -eq "fullscreen") { $script:fullscreen = $true }
      if ($sc["size"] -ne "fullscreen" -or -not $sc["fullscreenAfterSteps"]) { Apply-Size $procId $sc["size"] }
    }
    if ($sc["onboarding"]) { $script:target = "owned" }
    foreach ($step in @($sc["steps"])) {
      if ($null -eq $step) { continue }
      try { Run-Step $step $procId }
      catch {
        $script:notes += "step $(ConvertTo-Json $step -Compress -Depth 5): $($_.Exception.Message)"
        $script:partial = $true
        if (Get-Prop $step "required" $true) { if ($sc["stopOnError"]) { throw } }
      }
    }
    if ($sc["size"] -eq "fullscreen" -and $sc["fullscreenAfterSteps"]) { Apply-Size $procId "fullscreen" }
    if ($sc["postSize"]) { Apply-Size $procId $sc["postSize"]; if ($sc["postSize"] -eq "fullscreen") { $script:fullscreen = $true } }
    if ($sc["target"]) { $script:target = $sc["target"] }
    Start-Sleep -Milliseconds ([int]($sc["waitMs"]))
    Take-Capture $procId "final" $true
  } catch {
    $result.status = "error"
    $script:notes += $_.Exception.Message
  } finally {
    if ($procId) {
      try { Stop-Process -Id $procId -Force -ErrorAction Stop; Wait-Process -Id $procId -Timeout 5 -ErrorAction SilentlyContinue } catch {}
      Remove-Item (Join-Path $script:plan.out "pids\$procId.pid") -Force -ErrorAction SilentlyContinue
    }
  }
  if ($result.status -eq "ok" -and $script:partial) { $result.status = "partial" }
  $result.notes = @($script:notes)
  $result.captures = @($script:captures)
  $result.seconds = [math]::Round($sw.Elapsed.TotalSeconds, 1)
  return $result
}

# --- worker ------------------------------------------------------------------------------------

if ($WorkerPlan) {
  $script:plan = Get-Content -Raw -Encoding UTF8 $WorkerPlan | ConvertFrom-Json
  $warmHash = @{}; if ($script:plan.warm) { foreach ($p in $script:plan.warm.PSObject.Properties) { $warmHash[$p.Name] = $p.Value } }
  $script:plan | Add-Member -NotePropertyName warm -NotePropertyValue $warmHash -Force
  $script:hasCommand = [bool]$script:plan.hasCommand
  $script:dumpV2 = [bool]$script:plan.dumpV2
  $script:logFile = Join-Path $script:plan.out "sweep.log"
  foreach ($raw in $script:plan.scenarios) {
    $sc = To-Hash $raw
    $dir = Join-Path $script:plan.out (Safe-Name $sc["name"])
    New-Item -ItemType Directory -Force $dir | Out-Null
    try { $fs = [IO.File]::Open((Join-Path $dir ".claim"), 'CreateNew'); $fs.Close() } catch { continue }   # another worker has it
    if ((Get-Date) -gt [DateTime]$script:plan.deadline) { Log "deadline reached, skipping $($sc['name'])"; continue }
    Log "start $($sc['name'])"
    $res = Run-Scenario $sc
    Write-Utf8 (Join-Path $dir "result.json") (ConvertTo-Json $res -Depth 8)
    Log ("{0} {1} in {2}s {3}" -f $res.status, $sc["name"], $res.seconds, (($res.notes | Select-Object -First 2) -join " | "))
  }
  return
}

# --- main: plan, warm caches, run workers, build the contact sheet ---------------------------------

if (-not (Test-Path $Scenarios)) { throw "no scenarios file $Scenarios" }
$doc = Get-Content -Raw -Encoding UTF8 $Scenarios | ConvertFrom-Json
$vars = @{}
$dv = Get-Prop $doc "vars"; if ($dv) { foreach ($p in $dv.PSObject.Properties) { $vars[$p.Name] = [Environment]::ExpandEnvironmentVariables([string]$p.Value) } }
$all = Expand-Scenarios $doc
$sel = @($all | Where-Object { (-not $Filter -or $_["name"] -match $Filter) -and ([int]$(if ($_["priority"]) { $_["priority"] } else { 2 })) -le $MaxPriority })
foreach ($s in $sel) {
  $s["libraryResolved"] = @(@($s["library"]) | Where-Object { $_ } | ForEach-Object { Expand-Vars $_ $vars })
  $s["filesResolved"] = @(@($s["files"]) | Where-Object { $_ } | ForEach-Object { Expand-Vars $_ $vars })
  foreach ($st in @($s["steps"])) { if ($st -and (Get-Prop $st "cmd")) { $st.cmd = Expand-Vars $st.cmd $vars } }  # "nav videos $MEDIA\Mixed"
  if ($s["seed"]) {
    # Data files written into the data dir; $VARS inside become JSON-escaped paths.
    $seeds = [ordered]@{}
    foreach ($f in $s["seed"].Keys) {
      $j = ConvertTo-Json $s["seed"][$f] -Depth 12
      foreach ($k in $vars.Keys) { $j = $j.Replace('$' + $k, $vars[$k].Replace('\', '\\')) }
      $seeds[$f] = $j
    }
    $s["seedResolved"] = $seeds
  }
  if (-not $s["settleMs"]) { $s["settleMs"] = 1200 }
  if (-not $s["waitMs"]) { $s["waitMs"] = 700 }
}
if ($List) {
  $sel | ForEach-Object { "{0,-3} {1,-60} {2}" -f $_["priority"], $_["name"], (@($_["requires"]) -join ",") }
  "{0} scenarios ({1} before the filter)" -f $sel.Count, $all.Count
  return
}
if ($Reaudit) {
  if (-not $Out -or -not (Test-Path (Join-Path $Out "plan.json"))) { throw "-Reaudit needs the -Out of an earlier sweep" }
  $Out = (Resolve-Path $Out).Path
  $planObj = Get-Content -Raw -Encoding UTF8 (Join-Path $Out "plan.json") | ConvertFrom-Json
  $Exe = $planObj.exe; $scaleOk = [bool]$planObj.scale; $script:hasCommand = [bool]$planObj.hasCommand; $script:dumpV2 = [bool]$planObj.dumpV2
  $ordered = @($planObj.scenarios | ForEach-Object { To-Hash $_ })
  $python = (Get-Command python).Source
  $sw = [Diagnostics.Stopwatch]::StartNew()
  foreach ($s in $ordered) {
    $dir = Join-Path $Out (Safe-Name $s["name"]); $rf = Join-Path $dir "result.json"
    if (-not (Test-Path $rf)) { continue }
    $r = Get-Content -Raw -Encoding UTF8 $rf | ConvertFrom-Json
    foreach ($cap in @($r.captures)) {
      if (-not $cap.tree) { continue }
      $base = [IO.Path]::GetFileNameWithoutExtension($cap.png)
      $a = @($auditPy, (Join-Path $dir $cap.tree), "--allow", $allowFile, "--json", (Join-Path $dir "$base.audit.json"), "--md", (Join-Path $dir "$base.audit.md"),
             "--quiet", "--context", $s["name"], "--state", $cap.state)
      if ($cap.window) { $a += @("--window", ("^" + [regex]::Escape([string]$cap.window) + "$")) }
      if ($cap.hitmap) {
        $stepPx = if ([int](($cap.client -split 'x')[0]) -gt 1400) { 16 } else { 8 }
        $a += @("--hitmap", (Join-Path $dir $cap.hitmap), "--hitmap-step", "$stepPx", "--hitmap-window", ("^" + [regex]::Escape([string]$cap.window) + "$"))
      }
      & $python @a 2>&1 | Out-Null
    }
  }
} else {
if (-not $Exe) { throw "-Exe is required (never the owner's build\Lumen.exe)" }
if (-not $Out) { throw "-Out is required" }
$Exe = (Resolve-Path $Exe).Path
if ($Exe -ieq (Join-Path $repo "build\Lumen.exe")) { Write-Warning "using build\Lumen.exe: make sure the owner is not running it" }
$Parallel = [math]::Max(1, [math]::Min(4, $Parallel))
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
New-Item -ItemType Directory -Force (Join-Path $Out "pids") | Out-Null
# Only folders an earlier sweep made (they hold .claim) are removed.
Get-ChildItem $Out -Directory | Where-Object { Test-Path (Join-Path $_.FullName ".claim") } | ForEach-Object { Remove-Item -Recurse -Force $_.FullName }
$script:logFile = Join-Path $Out "sweep.log"
Write-Utf8 $script:logFile ""
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $python) { throw "python is not on PATH" }

# Capabilities of the exe and the tools.
$scaleOk = [bool]$AssumeScale -or $script:dumpV2
Log ("exe {0}; command.ps1: {1}; tree v2: {2}; --scale: {3}; {4} scenarios, {5} workers" -f $Exe, $script:hasCommand, $script:dumpV2, $scaleOk, $sel.Count, $Parallel)

# Warm library caches: one scan per distinct library, copied into every scenario's data dir.
$warm = @{}
$libs = @($sel | ForEach-Object { (@($_["libraryResolved"]) -join "|") } | Where-Object { $_ } | Select-Object -Unique)
$warmDir = Join-Path $Out "_warm"
New-Item -ItemType Directory -Force $warmDir | Out-Null
$k = 0
foreach ($lib in $libs) {
  $k++
  $cache = Join-Path $warmDir "cache-$k.json"
  $dd = Join-Path $qaTemp ("{0}-warm{1}" -f $Prefix, $k)
  if (Test-Path $dd) { Remove-Item -Recurse -Force $dd }
  New-Item -ItemType Directory -Force $dd | Out-Null
  Write-Utf8 (Join-Path $dd "ui.json") '{"onboarded":"1"}'
  Write-Utf8 (Join-Path $dd "settings.json") '{"version":1,"values":{"audio.startupVolumeMode":"fixed","audio.startupVolume":0}}'
  Write-Utf8 (Join-Path $dd "library.json") (ConvertTo-Json ([ordered]@{ version = 1; roots = @($lib -split '\|') }) -Depth 4)
  $p = Start-Process $Exe -ArgumentList @("--new-instance", "--automation", "--data-dir", "`"$dd`"") -PassThru
  Write-Utf8 (Join-Path $Out "pids\$($p.Id).pid") "warm $k"
  $ok = Wait-Stable (Join-Path $dd "library-cache.json") 3000 90000
  try { Stop-Process -Id $p.Id -Force; Wait-Process -Id $p.Id -Timeout 5 -ErrorAction SilentlyContinue } catch {}
  Remove-Item (Join-Path $Out "pids\$($p.Id).pid") -Force -ErrorAction SilentlyContinue
  if ($ok) { Copy-Item (Join-Path $dd "library-cache.json") $cache -Force; $warm[$lib] = $cache }
  Log ("warm cache {0}: {1} ({2})" -f $k, $lib, $(if ($ok) { "ok" } else { "not stable, scenarios scan themselves" }))
  Remove-Item -Recurse -Force $dd -ErrorAction SilentlyContinue
}

$planFile = Join-Path $Out "plan.json"
$deadline = (Get-Date).AddMinutes($TimeoutMin)
# Highest priority first, the slow ones (big library) early so they don't finish last.
$ordered = @($sel | Sort-Object @{ e = { [int]$(if ($_["priority"]) { $_["priority"] } else { 2 }) } }, @{ e = { if ($_["hitmap"]) { 0 } else { 1 } } })
$planObj = [ordered]@{ exe = $Exe; out = $Out; prefix = $Prefix; python = $python; hasCommand = $script:hasCommand; dumpV2 = $script:dumpV2
  scale = $scaleOk; noHitmap = [bool]$NoHitmap; deadline = $deadline.ToString("o"); warm = $warm; scenarios = $ordered }
Write-Utf8 $planFile (ConvertTo-Json $planObj -Depth 30)

$sw = [Diagnostics.Stopwatch]::StartNew()
$jobs = @()
for ($i = 1; $i -le $Parallel; $i++) {
  $jobs += Start-Job -ScriptBlock { param($s, $p, $id) & $s -WorkerPlan $p -WorkerId $id *> $null } -ArgumentList $PSCommandPath, $planFile, $i
  Start-Sleep -Milliseconds 400
}
while (($jobs | Where-Object { $_.State -eq "Running" }).Count -gt 0) {
  Start-Sleep -Seconds 5
  $done = @(Get-ChildItem $Out -Recurse -Filter result.json -ErrorAction SilentlyContinue).Count
  if ((Get-Date) -gt $deadline.AddMinutes(3)) { Log "timeout: stopping workers"; $jobs | Stop-Job; break }
  Write-Progress -Activity "sweep" -Status "$done / $($sel.Count)" -PercentComplete ([math]::Min(100, 100 * $done / [math]::Max(1, $sel.Count)))
}
$jobs | Receive-Job -ErrorAction SilentlyContinue | Out-Null
$jobs | Remove-Job -Force
# Kill anything a stopped worker left behind (only PIDs this sweep started).
foreach ($f in Get-ChildItem (Join-Path $Out "pids") -Filter *.pid -ErrorAction SilentlyContinue) {
  $procNum = [int]$f.BaseName
  $proc = Get-Process -Id $procNum -ErrorAction SilentlyContinue
  if ($proc -and $proc.Path -and ($proc.Path -ieq $Exe)) { Stop-Process -Id $procNum -Force -ErrorAction SilentlyContinue }
  Remove-Item $f.FullName -Force
}
for ($i = 1; $i -le 4; $i++) { $dd = Join-Path $qaTemp ("{0}-w{1}" -f $Prefix, $i); if (Test-Path $dd) { Remove-Item -Recurse -Force $dd -ErrorAction SilentlyContinue } }
}

# --- results and the contact sheet -----------------------------------------------------------------

$results = @()
foreach ($s in $ordered) {
  $dir = Join-Path $Out (Safe-Name $s["name"])
  $rf = Join-Path $dir "result.json"
  if (Test-Path $rf) { $results += (Get-Content -Raw -Encoding UTF8 $rf | ConvertFrom-Json) }
  else { $results += [pscustomobject]@{ name = $s["name"]; base = $s["base"]; variant = $s["variant"]; priority = $s["priority"]; screen = $s["screen"]; description = $s["description"]; status = "not run"; notes = @("timeout or worker failure"); captures = @(); seconds = 0 } }
}
$checkNames = @("hitmap", "overlap", "clip-window", "text-clip", "text-overflow", "target-size", "zero-size", "offscreen", "clip-parent", "overlap-cross", "text-trunc", "hitmap-odd")
$totals = @{}; foreach ($c in $checkNames) { $totals[$c] = 0 }
$allFindings = New-Object System.Collections.ArrayList
foreach ($r in $results) {
  foreach ($c in @($r.captures)) {
    if (-not $c.audit) { continue }
    $aj = Get-Content -Raw -Encoding UTF8 (Join-Path (Join-Path $Out (Safe-Name $r.name)) $c.audit) | ConvertFrom-Json
    $c | Add-Member -NotePropertyName findings -NotePropertyValue @($aj.findings) -Force
    $c | Add-Member -NotePropertyName allowedCount -NotePropertyValue $aj.allowedCount -Force
    $c | Add-Member -NotePropertyName format -NotePropertyValue $aj.format -Force
    foreach ($f in @($aj.findings)) {
      if ($totals.ContainsKey($f.check)) { $totals[$f.check]++ }
      $f | Add-Member -NotePropertyName scenario -NotePropertyValue $r.name -Force
      $f | Add-Member -NotePropertyName capture -NotePropertyValue $c.label -Force
      [void]$allFindings.Add($f)
    }
  }
}
Write-Utf8 (Join-Path $Out "findings.json") (ConvertTo-Json @($allFindings) -Depth 6)
Write-Utf8 (Join-Path $Out "summary.json") (ConvertTo-Json ([ordered]@{ exe = $Exe; started = $planObj.deadline; minutes = [math]::Round($sw.Elapsed.TotalMinutes, 1)
  hasCommand = $script:hasCommand; dumpV2 = $script:dumpV2; scale = $scaleOk; totals = $totals; results = $results }) -Depth 10)

$sb = New-Object System.Text.StringBuilder
$css = @"
:root { --bg:#0e0e0e; --panel:#191919; --line:#2a2a2a; --text:#eaeaea; --muted:#8f8f8f; --bad:#e5484d; --warn:#d6a243; --ok:#4cc38a; --chip:#242424; }
@media (prefers-color-scheme: light) { :root { --bg:#f2f2f2; --panel:#ffffff; --line:#d6d6d6; --text:#141414; --muted:#5c5c5c; --chip:#e6e6e6; } }
* { box-sizing: border-box; } body { margin:0; background:var(--bg); color:var(--text); font:14px/1.45 "Segoe UI", Inter, system-ui, sans-serif; }
header { padding:20px 24px 8px; } h1 { font-size:22px; font-weight:500; margin:0 0 6px; } .muted { color:var(--muted); }
.bar { display:flex; flex-wrap:wrap; gap:8px; align-items:center; padding:8px 24px 16px; position:sticky; top:0; background:var(--bg); z-index:2; border-bottom:1px solid var(--line); }
input[type=search] { background:var(--panel); color:var(--text); border:1px solid var(--line); border-radius:6px; padding:6px 10px; width:280px; }
.chip { background:var(--chip); border-radius:12px; padding:2px 10px; font-size:12px; cursor:pointer; user-select:none; border:1px solid transparent; }
.chip.on { border-color:var(--text); } .chip b { font-weight:600; }
main { display:grid; grid-template-columns:repeat(auto-fill, minmax(420px, 1fr)); gap:16px; padding:16px 24px 48px; }
.card { background:var(--panel); border:1px solid var(--line); border-radius:10px; overflow:hidden; display:flex; flex-direction:column; }
.card img { width:100%; display:block; background:#000; image-rendering:auto; }
.meta { padding:10px 12px; } .name { font-weight:600; word-break:break-all; } .desc { font-size:12px; }
.st { font-size:11px; padding:1px 7px; border-radius:9px; margin-left:6px; } .st.ok { background:var(--ok); color:#000; } .st.partial { background:var(--warn); color:#000; } .st.error, .st.not { background:var(--bad); color:#fff; } .st.skipped { background:var(--chip); }
.counts { display:flex; flex-wrap:wrap; gap:4px; margin:6px 0; } .counts span { font-size:11px; background:var(--chip); border-radius:8px; padding:1px 7px; }
.counts span.high { color:var(--bad); } details { font-size:12px; } details li { margin:2px 0; word-break:break-word; } a { color:inherit; }
.links { font-size:12px; display:flex; gap:10px; flex-wrap:wrap; } .notes { font-size:12px; color:var(--warn); }
table.tot { border-collapse:collapse; font-size:12px; margin-top:8px; } table.tot td, table.tot th { border:1px solid var(--line); padding:2px 8px; text-align:left; }
@media (max-width:520px) { main { grid-template-columns:1fr; padding:12px 16px; } .bar { padding:8px 16px; } input[type=search] { width:100%; } }
"@
[void]$sb.Append("<!doctype html><html lang=`"en`"><head><meta charset=`"utf-8`"><meta name=`"viewport`" content=`"width=device-width, initial-scale=1`"><title>Lumen sweep</title><style>$css</style></head><body>")
$okN = @($results | Where-Object { $_.status -eq "ok" }).Count
[void]$sb.Append("<header><h1>Lumen state-matrix sweep</h1><div class=`"muted`">$(Html $Exe) &middot; $($results.Count) scenarios ($okN ok) &middot; $([math]::Round($sw.Elapsed.TotalMinutes, 1)) min &middot; command.ps1: $($script:hasCommand) &middot; tree v2: $($script:dumpV2) &middot; --scale: $scaleOk &middot; $(Get-Date -Format 'yyyy-MM-dd HH:mm')</div>")
[void]$sb.Append("<table class=`"tot`"><tr><th>check</th><th>findings</th></tr>")
foreach ($c in $checkNames) { if ($totals[$c]) { [void]$sb.Append("<tr><td>$c</td><td>$($totals[$c])</td></tr>") } }
[void]$sb.Append("</table><p class=`"muted`">Each card: the final capture (click for full resolution), the scenario, and the ui-audit findings of its tree dump. Review with METHOD.md section 2: compare with the same screen in the other theme or size, crop toolbars and rows, verify every candidate in the tree.</p></header>")
[void]$sb.Append("<div class=`"bar`"><input type=`"search`" id=`"q`" placeholder=`"Filter by name, screen, check...`"><span class=`"chip`" data-f=`"has`">with findings</span><span class=`"chip`" data-f=`"bad`">not ok</span>")
foreach ($c in $checkNames) { if ($totals[$c]) { [void]$sb.Append("<span class=`"chip`" data-f=`"c:$c`">$c <b>$($totals[$c])</b></span>") } }
[void]$sb.Append("</div><main id=`"grid`">")
foreach ($r in $results) {
  $d = Safe-Name $r.name
  $caps = @($r.captures)
  $checks = @{}
  foreach ($c in $caps) { foreach ($f in @($c.findings)) { if ($f) { $checks[$f.check] = 1 + [int]$checks[$f.check] } } }
  $tags = (@($checks.Keys | ForEach-Object { "c:$_" }) + @($r.status, $r.screen, $r.base)) -join " "
  $stClass = ($r.status -split ' ')[0]
  [void]$sb.Append("<section class=`"card`" data-status=`"$(Html $r.status)`" data-tags=`"$(Html $tags)`" data-name=`"$(Html $r.name)`" data-n=`"$(($checks.Values | Measure-Object -Sum).Sum)`">")
  $final = $caps | Where-Object { $_.label -eq "final" } | Select-Object -First 1
  if (-not $final -and $caps.Count) { $final = $caps[-1] }
  if ($final) { [void]$sb.Append("<a href=`"$d/$($final.png)`" target=`"_blank`"><img loading=`"lazy`" src=`"$d/$($final.thumb)`" alt=`"$(Html $r.name)`"></a>") }
  [void]$sb.Append("<div class=`"meta`"><div class=`"name`">$(Html $r.name)<span class=`"st $stClass`">$(Html $r.status)</span></div>")
  if ($r.description) { [void]$sb.Append("<div class=`"desc muted`">$(Html $r.description)</div>") }
  $info = @(); if ($final) { $info += "client $($final.client) DIP-px, $($final.state)"; if ($final.window) { $info += "window `"$($final.window)`"" } }
  $info += "$($r.seconds) s"
  [void]$sb.Append("<div class=`"desc muted`">$(Html ($info -join ' · '))</div>")
  if (@($r.notes).Count) { [void]$sb.Append("<div class=`"notes`">$(Html ((@($r.notes) | Select-Object -First 4) -join ' | '))</div>") }
  [void]$sb.Append("<div class=`"counts`">")
  foreach ($c in $checkNames) { if ($checks[$c]) { $cls = if ($c -in @("hitmap", "overlap", "clip-window", "text-clip")) { "high" } else { "" }; [void]$sb.Append("<span class=`"$cls`">$c $($checks[$c])</span>") } }
  [void]$sb.Append("</div>")
  foreach ($c in $caps) {
    $fs = @($c.findings | Where-Object { $_ })
    if ($fs.Count) {
      [void]$sb.Append("<details><summary>$(Html $c.label): $($fs.Count) findings ($($c.allowedCount) allowlisted)</summary><ul>")
      foreach ($f in ($fs | Select-Object -First 40)) { [void]$sb.Append("<li><b>$(Html $f.check)</b> $(Html $f.detail) <span class=`"muted`">line $($f.line)</span></li>") }
      if ($fs.Count -gt 40) { [void]$sb.Append("<li class=`"muted`">... $($fs.Count - 40) more in the audit file</li>") }
      [void]$sb.Append("</ul></details>")
    }
    $links = @("<a href=`"$d/$($c.png)`">$(Html $c.label).png</a>")
    if ($c.tree) { $links += "<a href=`"$d/$($c.tree)`">tree</a>" }
    if ($c.auditMd) { $links += "<a href=`"$d/$($c.auditMd)`">audit</a>" }
    if ($c.hitmap) { $links += "<a href=`"$d/$($c.hitmap)`">hitmap</a>" }
    [void]$sb.Append("<div class=`"links`">$($links -join '')</div>")
  }
  [void]$sb.Append("</div></section>")
}
$js = @"
const q=document.getElementById('q'),chips=[...document.querySelectorAll('.chip')],cards=[...document.querySelectorAll('.card')];
function apply(){const t=q.value.toLowerCase(),on=chips.filter(c=>c.classList.contains('on')).map(c=>c.dataset.f);
cards.forEach(c=>{let ok=!t||(c.dataset.name+' '+c.dataset.tags).toLowerCase().includes(t);
on.forEach(f=>{if(f==='has')ok=ok&&+c.dataset.n>0;else if(f==='bad')ok=ok&&c.dataset.status!=='ok';else ok=ok&&c.dataset.tags.split(' ').includes(f);});
c.style.display=ok?'':'none';});}
q.addEventListener('input',apply);chips.forEach(c=>c.addEventListener('click',()=>{c.classList.toggle('on');apply();}));
"@
[void]$sb.Append("</main><script>$js</script></body></html>")
Write-Utf8 (Join-Path $Out "index.html") $sb.ToString()
Log ("done: {0} scenarios in {1:N1} min; contact sheet {2}" -f $results.Count, $sw.Elapsed.TotalMinutes, (Join-Path $Out "index.html"))
$results | Group-Object status | ForEach-Object { "{0}: {1}" -f $_.Name, $_.Count }

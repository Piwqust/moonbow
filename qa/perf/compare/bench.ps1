# Cross-player comparison: Moonbow, VLC and MPC-HC measured from outside with one tool (CmpBench.cs).
# Players are unpacked copies with their own settings; the installed VLC and MPC-HC are not touched:
#   VLC     <Players>\vlc\vlc-3.0.24\vlc.exe     (a "portable" folder next to vlc.exe holds its settings)
#   MPC-HC  <Players>\mpc\mpc-hc64.exe           (an mpc-hc64.ini next to the exe holds its settings)
#   Moonbow the installed exe with --new-instance --data-dir <work folder>
# Media: bash qa/perf/compare/make-media.sh "$TEMP/moonbow-qa/compare-media"
# Usage: bench.ps1 -Mode probe|start|start4k|empty|load|load4k|seek|seekplay|colors [-Runs N] [-Only id,id] [-Keep]
#   -MoveTo x,y,w,h   move the window there as soon as it exists (physical pixels), e.g. to a monitor
#           without HDR: a window capture can't judge colours of an HDR surface.
#   -Keep   settings survive between runs (an everyday start); without it every run is a first run.
#           With -Keep one extra run is made first and left out of the statistics.
# The players' windows do show up on the desktop while this runs.
param([string]$Mode = "probe", [int]$Runs = 10, [string[]]$Only = @(), [switch]$Keep,
      [string]$Players = "$env:TEMP\moonbow-qa\players", [string]$Media = "$env:TEMP\moonbow-qa\compare-media",
      [string]$Work = "$env:TEMP\moonbow-qa\compare", [string]$Moonbow = "$env:LOCALAPPDATA\Programs\Moonbow\Moonbow.exe",
      [string]$Out = "", [string]$MoveTo = "")
$ErrorActionPreference = "Stop"
Add-Type -Path (Join-Path $PSScriptRoot "CmpBench.cs") -ReferencedAssemblies System.Drawing, System.Core
[CmpBench]::Init()
New-Item -ItemType Directory -Force $Work | Out-Null
$enc = New-Object System.Text.UTF8Encoding $false
$vlcDir = Join-Path $Players "vlc\vlc-3.0.24"
$mpcDir = Join-Path $Players "mpc"
$rcPort = 42123

function Reset-Dir([string]$dir) {
  $root = [IO.Path]::GetFullPath("$env:TEMP").TrimEnd('\') + '\'
  if (-not [IO.Path]::GetFullPath($dir).StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { throw "refusing to wipe $dir" }
  if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
  New-Item -ItemType Directory -Force $dir | Out-Null
}
# Every run starts from first-run defaults: no history, no remembered position, no saved window.
$defs = [ordered]@{
  "moonbow" = @{
    Name = "Moonbow 0.2.0"
    Prep = { Reset-Dir "$Work\moonbow"; [IO.File]::WriteAllText("$Work\moonbow\ui.json", '{"onboarded":"1"}', $enc) }
    Cmd  = { param($file, $rc) "`"$Moonbow`" --new-instance --data-dir `"$Work\moonbow`"" + $(if ($file) { " `"$file`"" }) }
    Dir  = (Split-Path $Moonbow)
  }
  "moonbow-auto" = @{
    Name = "Moonbow 0.2.0 (--automation)"
    Prep = { Reset-Dir "$Work\moonbow-auto"; [IO.File]::WriteAllText("$Work\moonbow-auto\ui.json", '{"onboarded":"1"}', $enc) }
    Cmd  = { param($file, $rc) "`"$Moonbow`" --new-instance --automation --data-dir `"$Work\moonbow-auto`"" + $(if ($file) { " `"$file`"" }) }
    Dir  = (Split-Path $Moonbow)
  }
  "vlc" = @{
    Name = "VLC 3.0.24"
    Prep = { Reset-Dir "$vlcDir\portable" }
    Cmd  = { param($file, $rc) "`"$vlcDir\vlc.exe`" --no-qt-privacy-ask --no-qt-updates-notif" +
             $(if ($rc) { " --extraintf=rc --rc-quiet --rc-host=127.0.0.1:$rcPort" }) + $(if ($file) { " `"$file`"" }) }
    Dir  = $vlcDir
  }
  "mpc" = @{
    Name = "MPC-HC 2.8.5"
    # First-run defaults, with the one first-run question ("check for updates?") already answered "no".
    Prep = { [IO.File]::WriteAllText("$mpcDir\mpc-hc64.ini", "; MPC-HC`r`n[Settings]`r`nUpdaterAutoCheck=0`r`n", [Text.Encoding]::Unicode)
             Remove-Item "$mpcDir\mpc-hc64.history.ini" -ErrorAction SilentlyContinue }
    Cmd  = { param($file, $rc) "`"$mpcDir\mpc-hc64.exe`"" + $(if ($file) { " `"$file`"" }) }
    Dir  = $mpcDir
  }
}
$Only = @($Only | ForEach-Object { $_.Split(',') } | Where-Object { $_ })   # "-Only a,b" arrives as one string with -File
$ids = @($defs.Keys | Where-Object { $Only.Count -eq 0 -or $Only -contains $_ })
$f1080 = Join-Path $Media "bench-1080p-h264.mkv"
$f4k = Join-Path $Media "bench-2160p60-hevc10.mkv"
$fcol = Join-Path $Media "colors-1080p.mkv"

$prepped = @{}
function Start-Player([string]$id, [string]$file, [bool]$rc = $false) {
  $d = $defs[$id]
  if (-not ($Keep -and $prepped[$id])) { & $d.Prep; $prepped[$id] = $true }
  [CmpBench]::Launch((& $d.Cmd $file $rc), $d.Dir) | Out-Null
}
function Parse([string]$line) {
  $h = [ordered]@{}
  foreach ($kv in $line.Split(' ')) { $p = $kv.Split('='); if ($p.Count -eq 2) { $h[$p[0]] = $p[1] } }
  $h
}
function Stats([double[]]$v) {
  $s = @($v | Where-Object { $_ -ge 0 } | Sort-Object)
  if ($s.Count -eq 0) { return "n=0" }
  $med = if ($s.Count % 2) { $s[($s.Count - 1) / 2] } else { ($s[$s.Count / 2 - 1] + $s[$s.Count / 2]) / 2 }
  "{0,7:0.0} median   {1,7:0.0} min   {2,7:0.0} max   n={3}" -f $med, $s[0], $s[-1], $s.Count
}
$results = [ordered]@{}
function Pause-Player([string]$id) {
  if ($id -like "moonbow*") { [CmpBench]::Post(0x100, 0x20, 0x00390001); [CmpBench]::Post(0x101, 0x20, 0xC0390001) }   # Space
  elseif ($id -eq "mpc") { [CmpBench]::Post(0x111, 888, 0) }                                                           # ID_PLAY_PAUSE
  elseif ($id -eq "vlc") { if ([CmpBench]::RcConnect($rcPort, 4000)) { [CmpBench]::RcSend("pause") } }
}
function Save-Results { if ($Out) { [IO.File]::WriteAllText($Out, ($results | ConvertTo-Json -Depth 6), $enc) } }

switch ($Mode) {
  # One start per player with a screenshot: does the tool see the square and read frame numbers?
  "probe" {
    foreach ($id in $ids) {
      Start-Player $id $f1080
      $m = [CmpBench]::MeasureStart(15000)
      Start-Sleep -Milliseconds 1500
      $a = [CmpBench]::CurrentFrame(); $sq = [CmpBench]::Square()
      [CmpBench]::SavePng("$Work\probe-$id.png")
      Start-Sleep -Milliseconds 1000
      $b = [CmpBench]::CurrentFrame()
      "{0,-14} {1}`n               frames {2} -> {3} in ~1 s; {4}; {5}; close {6:0} ms" -f $id, $m, $a, $b, $sq, [CmpBench]::WindowInfo(), [CmpBench]::CloseAndWait(5000)
    }
  }
  # Start with a file, players in turn so that background noise spreads evenly.
  { $_ -in "start", "start4k" } {
    $file = if ($Mode -eq "start4k") { $f4k } else { $f1080 }
    foreach ($id in $ids) { $results[$id] = @() }
    $firstRun = if ($Keep) { -1 } else { 0 }
    for ($r = $firstRun; $r -lt $Runs; $r++) {
      foreach ($id in $ids) {
        Start-Player $id $file
        $m = Parse ([CmpBench]::MeasureStart(20000))
        $m["close"] = "{0:0.0}" -f [CmpBench]::CloseAndWait(5000)
        if ($r -ge 0) { $results[$id] += , $m }
        "{0,2} {1,-14} window {2,7} first {3,7} full {4,7} moving {5,7} frame0 {6,3} period {7,5} close {8,7}  {9}" -f $r, $id, $m.window, $m.first, $m.full, $m.moving, $m.frame0, $m.period, $m.close, $m.size
        Start-Sleep -Milliseconds 400
      }
    }
    ""
    foreach ($id in $ids) {
      foreach ($k in "window", "first", "full", "moving", "close") {
        "{0,-14} {1,-7} {2}" -f $id, $k, (Stats ($results[$id] | ForEach-Object { [double]$_[$k] }))
      }
    }
    Save-Results
  }
  # Start without a file: window, then memory and CPU of the idle player.
  "empty" {
    foreach ($id in $ids) { $results[$id] = @{ window = @(); idle = "" } }
    for ($r = 0; $r -lt $Runs; $r++) {
      foreach ($id in $ids) {
        Start-Player $id ""
        $w = [CmpBench]::MeasureWindow(15000)
        $results[$id].window += $w
        if ($r -eq $Runs - 1) { Start-Sleep 6; $results[$id].idle = [CmpBench]::Load(10) }
        $c = [CmpBench]::CloseAndWait(5000)
        "{0,2} {1,-14} window {2,7:0.0} close {3,7:0.0}" -f $r, $id, $w, $c
        Start-Sleep -Milliseconds 400
      }
    }
    ""
    foreach ($id in $ids) { "{0,-14} window  {1}`n               idle    {2}" -f $id, (Stats $results[$id].window), $results[$id].idle }
    Save-Results
  }
  # Playback cost: CPU, memory and GPU over 20 s at the same window size, how evenly frames are
  # shown, then the same on pause.
  { $_ -in "load", "load4k" } {
    $file = if ($Mode -eq "load4k") { $f4k } else { $f1080 }
    $fps = if ($Mode -eq "load4k") { 60.0 } else { 24000.0 / 1001.0 }
    foreach ($id in $ids) {
      Start-Player $id $file ($id -eq "vlc")
      $start = [CmpBench]::MeasureStart(20000)
      [CmpBench]::Resize(1600, 960)
      Start-Sleep 4
      $playing = [CmpBench]::Load(20)
      $smooth = [CmpBench]::Smoothness(8, $fps)
      Pause-Player $id
      Start-Sleep -Milliseconds 1500
      $a = [CmpBench]::CurrentFrame()
      $paused = [CmpBench]::Load(8)
      $b = [CmpBench]::CurrentFrame()
      $results[$id] = [ordered]@{ start = $start; window = [CmpBench]::WindowInfo(); playing = $playing; smooth = $smooth; paused = $paused; pausedFrames = "$a $b" }
      "{0}`n   start   {1}`n   window  {2}`n   playing {3}`n   smooth  {4}`n   paused  {5} (frame {6} -> {7})" -f $defs[$id].Name, $start, [CmpBench]::WindowInfo(), $playing, $smooth, $paused, $a, $b
      "   close   {0:0} ms" -f [CmpBench]::CloseAndWait(5000)
      [CmpBench]::RcClose()
      Start-Sleep 1
    }
    Save-Results
  }
  # Seek while paused: time from the command to another frame on screen, and where it landed.
  "seek" {
    foreach ($id in $ids) {
      Start-Player $id $f1080 ($id -eq "vlc")
      [CmpBench]::MeasureStart(20000) | Out-Null
      Start-Sleep -Milliseconds 1500
      Pause-Player $id
      Start-Sleep -Milliseconds 800
      $rows = @()
      for ($i = 0; $i -lt $Runs; $i++) {
        $from = [CmpBench]::CurrentFrame(); $to = -1
        $ms = switch -Wildcard ($id) {
          "moonbow*" { [CmpBench]::PostAndWait(0x100, 0x27, 0x014D0001, $from, 5000, [ref]$to) }          # Right arrow: +10 s
          "mpc"      { [CmpBench]::PostAndWait(0x111, 902, 0, $from, 5000, [ref]$to) }                    # jump forward (medium): +5 s
          "vlc"      { [CmpBench]::RcSendAndWait("seek $([int]($from / 23.976) + 10)", $from, 5000, [ref]$to) }   # absolute seconds
        }
        if ($id -like "moonbow*") { [CmpBench]::Post(0x101, 0x27, 0xC14D0001) }
        Start-Sleep -Milliseconds 700
        $settled = [CmpBench]::CurrentFrame()
        $rows += [ordered]@{ ms = [math]::Round($ms, 1); from = $from; first = $to; settled = $settled }
        "{0,-14} seek {1,2}: {2,7:0.0} ms   frame {3,4} -> {4,4} (+{5}), settled at {6}" -f $id, $i, $ms, $from, $to, ($to - $from), $settled
      }
      $results[$id] = $rows
      "{0,-14} seek     {1}" -f $id, (Stats ($rows | ForEach-Object { [double]$_.ms }))
      [CmpBench]::CloseAndWait(5000) | Out-Null
      [CmpBench]::RcClose()
      Start-Sleep 1
    }
    Save-Results
  }
  # Seek during playback: time from the command to the first frame from the new place.
  "seekplay" {
    foreach ($id in $ids) {
      Start-Player $id $f1080 ($id -eq "vlc")
      [CmpBench]::MeasureStart(20000) | Out-Null
      if ($id -eq "vlc") { [CmpBench]::RcConnect($rcPort, 4000) | Out-Null }
      Start-Sleep -Milliseconds 2000
      $rows = @()
      for ($i = 0; $i -lt $Runs; $i++) {
        $from = [CmpBench]::CurrentFrame(); $to = -1
        $ms = switch -Wildcard ($id) {
          "moonbow*" { [CmpBench]::PostAndWaitJump(0x100, 0x27, 0x014D0001, $from, 48, 5000, [ref]$to) }
          "mpc"      { [CmpBench]::PostAndWaitJump(0x111, 902, 0, $from, 48, 5000, [ref]$to) }
          "vlc"      { [CmpBench]::RcSendAndWaitJump("seek $([int]($from / 23.976) + 10)", $from, 48, 5000, [ref]$to) }
        }
        if ($id -like "moonbow*") { [CmpBench]::Post(0x101, 0x27, 0xC14D0001) }
        $rows += [ordered]@{ ms = [math]::Round($ms, 1); from = $from; first = $to }
        "{0,-14} seek {1,2}: {2,7:0.0} ms   frame {3,4} -> {4,4} (+{5})" -f $id, $i, $ms, $from, $to, ($to - $from)
        Start-Sleep -Milliseconds 1000
      }
      $results[$id] = $rows
      "{0,-14} seek while playing  {1}" -f $id, (Stats ($rows | ForEach-Object { [double]$_.ms }))
      [CmpBench]::CloseAndWait(5000) | Out-Null
      [CmpBench]::RcClose()
      Start-Sleep 1
    }
    Save-Results
  }
  # Colour accuracy: the middle of ten flat patches against a BT.709 limited-range decode.
  "colors" {
    $ref = Get-Content (Join-Path $Media "colors-reference.txt") | ForEach-Object { $p = ($_ -split '#')[0].Trim().Split(' '); , @($p[0], [double]$p[1], [double]$p[2], [double]$p[3]) }
    foreach ($id in $ids) {
      Start-Player $id $fcol
      if ($MoveTo) { $m = $MoveTo.Split(',') | ForEach-Object { [int]$_ }; [CmpBench]::MoveWhenShown($m[0], $m[1], $m[2], $m[3], 10000) | Out-Null }
      $t0 = [CmpBench]::WaitSquare(20000)
      if ($t0 -lt 0) { "$id : square not found"; [CmpBench]::Kill(); continue }
      $rows = @(); $worst = 0.0
      for ($i = 1; $i -lt $ref.Count; $i++) {
        $at = $t0 + (3 + 2 * ($i - 1) + 1.0) * 1000
        while ([CmpBench]::Ms() -lt $at) { Start-Sleep -Milliseconds 20 }
        $s = @(); for ($k = 0; $k -lt 5; $k++) { $c = [CmpBench]::SquareColor(); if ($c) { $s += , ($c.Split(' ') | ForEach-Object { [double]$_ }) }; Start-Sleep -Milliseconds 60 }
        $rgb = 0..2 | ForEach-Object { $ch = $_; ($s | ForEach-Object { $_[$ch] } | Sort-Object)[[int]($s.Count / 2)] }
        $err = 0..2 | ForEach-Object { $rgb[$_] - $ref[$i][$_ + 1] }
        $max = ($err | ForEach-Object { [math]::Abs($_) } | Measure-Object -Maximum).Maximum
        if ($max -gt $worst) { $worst = $max }
        $rows += [ordered]@{ patch = $ref[$i][0]; want = "$($ref[$i][1]) $($ref[$i][2]) $($ref[$i][3])"; got = ($rgb | ForEach-Object { "{0:0.0}" -f $_ }) -join ' '; maxError = [math]::Round($max, 1) }
        "{0,-14} {1}  want {2,3:0} {3,3:0} {4,3:0}   got {5,5:0.0} {6,5:0.0} {7,5:0.0}   error {8,5:0.0} {9,5:0.0} {10,5:0.0}" -f $id, $ref[$i][0], $ref[$i][1], $ref[$i][2], $ref[$i][3], $rgb[0], $rgb[1], $rgb[2], $err[0], $err[1], $err[2]
      }
      $results[$id] = [ordered]@{ worst = [math]::Round($worst, 1); patches = $rows }
      "{0,-14} worst channel error {1:0.0} of 255" -f $id, $worst
      [CmpBench]::Kill()
      Start-Sleep 1
    }
    Save-Results
  }
}
[CmpBench]::Kill()

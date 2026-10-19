# Nothing may stay behind after a drag or a hover: the seek preview, hot/pressed widgets, a row's
# drag tooltip, the mini player's overlay, the volume slider popup, a control bar hidden mid-drag.
# Drives invisible --automation instances with posted input (input.ps1) and reads the widget tree
# (dump-tree.ps1) after each case. Prints PASS/FAIL per case; exit code 1 when one fails.
# Usage: tools\dev\check-stuck-hover.ps1 -Exe <Lumen.exe> [-Work <folder in %TEMP%>]
[CmdletBinding(PositionalBinding = $false)]
param([Parameter(Mandatory)][string]$Exe, [string]$Work = "$env:TEMP\lumen-qa\stuck-hover")
$tools = $PSScriptRoot
$videos = "$env:TEMP\lumen-qa\media\Videos"
New-Item -ItemType Directory -Force $Work | Out-Null
$script:failed = 0

function Dump([int]$id) {
  & "$tools\dump-tree.ps1" -ProcessId $id -Out "$Work\dump.txt" -Quiet
  return [IO.File]::ReadAllLines("$Work\dump.txt", [Text.Encoding]::UTF8)
}
function Find($lines, [string]$re) { foreach ($l in $lines) { if ($l -match $re) { return $l } }; return $null }
function RectOf([string]$line) {
  if ($line -notmatch '\[(-?[\d.]+),(-?[\d.]+) ([\d.]+)x([\d.]+)\]') { throw "no rect in: $line" }
  return @{ x = [double]$Matches[1]; y = [double]$Matches[2]; w = [double]$Matches[3]; h = [double]$Matches[4] }
}
# Client pixels of a DIP point (the dump is in DIPs).
function Px([double]$x, [double]$y) { return "{0},{1}" -f [int][Math]::Round($x * $script:scale), [int][Math]::Round($y * $script:scale) }
function Report([string]$name, [string[]]$problems) {
  if ($problems.Count -eq 0) { Write-Output "PASS  $name" }
  else { $script:failed++; Write-Output "FAIL  $name"; $problems | ForEach-Object { Write-Output "        $_" } }
}
# Problems every case checks: a widget left pressed, the seek preview on screen while no drag runs.
function Leftovers($lines, [bool]$previewAllowed = $false) {
  $p = @()
  $pressed = Find $lines ' pressed( |$)'
  if ($pressed) { $p += "pressed left behind: $($pressed.Trim())" }
  $seek = Find $lines '^\s+app::SeekBar \[\d+,\d+ [1-9]'
  if (-not $previewAllowed -and $seek -and $seek -match ' text=') { $p += "seek preview still shown: $($seek.Trim())" }
  return $p
}
function Start-Lumen([string]$name, [string]$settings, [string[]]$files) {
  $id = & "$tools\launch.ps1" -Exe $Exe -DataDir "$Work\data-$name" -Fresh -Settings $settings -Library "$env:TEMP\lumen-qa\media" @files
  $deadline = [DateTime]::Now.AddSeconds(20)
  while ($true) {
    try {
      $lines = Dump $id
      $time = Find $lines 'TimeLabel \[[^\]]+\] text="\d'
      if ($time -and (Find $lines '^\s+app::SeekBar \[\d+,\d+ [1-9]')) { break }
    } catch {}
    if ([DateTime]::Now -gt $deadline) { Stop-Process -Id $id -ErrorAction SilentlyContinue; throw "$name did not open its video" }
    Start-Sleep -Milliseconds 300
  }
  $script:scale = [double]((Find $lines '^=== window') -replace '.* scale=([\d.]+).*', '$1')
  return $id
}

# --- the floating bar (the default) -------------------------------------------------------------
$id = Start-Lumen "bar" '{"interface.autoHide":"never"}' @("$videos\Long Seek Test 10min.mp4", "$videos\Chaptered Demo (1080p).mkv")
try {
  & "$tools\input.ps1" -ProcessId $id -Key Space  # paused: the times below stay put
  $lines = Dump $id
  $client = (Find $lines '^=== window') -replace '.* client=(\d+)x(\d+).*', '$1 $2' -split ' '
  $cw = [double]$client[0]; $ch = [double]$client[1]
  $bar = RectOf (Find $lines '^\s+app::SeekBar \[\d+,\d+ [1-9]')
  $cy = $bar.y + $bar.h / 2
  $time = { (Find (Dump $id) 'TimeLabel \[[^\]]+\] text="\d') -replace '.*text="([^"]+)".*', '$1' }

  # The owner's case: dragged to the left end and released far off the bar, bottom left.
  & "$tools\input.ps1" -ProcessId $id -Drag "$(Px ($bar.x + $bar.w * 0.4) $cy),$(Px 10 ($ch - 4))"
  Report "seek drag released bottom left, off the bar" @(Leftovers (Dump $id))

  & "$tools\input.ps1" -ProcessId $id -Drag "$(Px ($bar.x + $bar.w * 0.3) $cy),$(Px ($bar.x + $bar.w * 0.5) -40)"
  $lines = Dump $id
  $p = @(Leftovers $lines)
  $hot = Find $lines ' hot( |$)'
  if ($hot) { $p += "hot after a release outside the window: $($hot.Trim())" }
  Report "seek drag released outside the window" $p

  foreach ($end in @(@{ name = "capture lost"; args = @{ CaptureLost = $true } }, @{ name = "Esc"; args = @{ Key = "Escape" } })) {
    $before = & $time
    $a = $end.args
    & "$tools\input.ps1" -ProcessId $id -Drag "$(Px ($bar.x + $bar.w * 0.5) $cy),$(Px ($bar.x + $bar.w * 0.8) ($cy - 150))" -Hold @a
    & "$tools\input.ps1" -ProcessId $id -Move (Px ($cw / 2) ($ch / 3))
    $p = @(Leftovers (Dump $id))
    $after = & $time
    if ($after -ne $before) { $p += "position not restored: $before before the drag, $after after" }
    Report "seek drag ended by $($end.name)" $p
  }

  # Released on the bar: the preview stays, at the pointer, with the time under the pointer.
  $x = $bar.x + $bar.w * 0.6
  & "$tools\input.ps1" -ProcessId $id -Drag "$(Px ($bar.x + $bar.w * 0.2) $cy),$(Px $x $cy)"
  $lines = Dump $id
  $p = @(Leftovers $lines $true)
  $seek = Find $lines '^\s+app::SeekBar \[\d+,\d+ [1-9]'
  if ($seek -notmatch 'text="([^"]+)" box=\[([\d.]+),[\d.]+ ([\d.]+)x') { $p += "no preview over the bar: $($seek.Trim())" }
  else {
    $label = $Matches[1]; $mid = [double]$Matches[2] + [double]$Matches[3] / 2
    if ([Math]::Abs($mid - $x) -gt 2) { $p += "preview at x=$mid, the pointer at x=$x" }
    if ($label -ne (& $time)) { $p += "preview says $label, the position is $(& $time)" }
  }
  Report "seek drag released on the bar: preview at the pointer" $p

  # A queue row dragged by its handle and dropped off the panel: no row hover or tooltip left.
  & "$tools\input.ps1" -ProcessId $id -Key L -Ctrl
  $list = Find (Dump $id) '#queue-list \[\d+,\d+ [1-9]'
  if (-not $list) { Report "queue drag released off the panel" @("the queue did not open") }
  else {
    $r = RectOf $list
    $start = Px ($r.x + 20) ($r.y + 60)  # the second row's handle
    & "$tools\input.ps1" -ProcessId $id -Move $start
    $p = @()
    if ((Find (Dump $id) '#queue-list \[\d+,\d+ [1-9]') -notmatch ' tip=') { $p += "the handle shows no tooltip: the start point missed it" }
    & "$tools\input.ps1" -ProcessId $id -Drag "$start,$(Px ($cw / 4) ($ch / 3))"
    $lines = Dump $id
    $p += @(Leftovers $lines)
    $q = Find $lines '#queue-list \[\d+,\d+ [1-9]'
    if ($q -match ' tip=') { $p += "row hover left behind: $($q.Trim() -replace ' text=.*?(tip=)', ' $1')" }
    Report "queue drag released off the panel" $p
    & "$tools\input.ps1" -ProcessId $id -Key Escape
  }

  # The mini player's overlay: on the player, onto its close button, then off it.
  $lines = Dump $id
  # The title bar's folder button ("Show in library"): the line above its folder icon.
  $i = [Array]::IndexOf($lines, (Find $lines 'IconView \[[^\]]+\] icon=folder'))
  $r = RectOf $lines[$i - 1]
  & "$tools\input.ps1" -ProcessId $id -Click (Px ($r.x + $r.w / 2) ($r.y + $r.h / 2))
  $lines = Dump $id
  $mini = Find $lines '^\s+app::MiniPlayer \[\d+,\d+ [1-9]'
  if (-not $mini) { Report "mini player overlay after leaving through a button" @("no mini player") }
  else {
    $m = RectOf $mini
    $close = RectOf (Find $lines 'MiniOverlayButton #close')
    & "$tools\input.ps1" -ProcessId $id -Move (Px ($m.x + $m.w / 2) ($m.y + $m.h / 2))
    & "$tools\input.ps1" -ProcessId $id -Move (Px ($close.x + $close.w / 2) ($close.y + $close.h / 2))
    & "$tools\input.ps1" -ProcessId $id -Move (Px ($m.x - 60) ($close.y + $close.h / 2))
    $lines = Dump $id
    $i = [Array]::IndexOf($lines, (Find $lines '^\s+app::MiniPlayer \['))
    $p = @()
    if ($lines[$i + 1] -notmatch ' hidden') { $p += "overlay still shown: $($lines[$i + 1].Trim())" }
    Report "mini player overlay after leaving through a button" $p
  }
} finally { Stop-Process -Id $id -ErrorAction SilentlyContinue }

# --- the minimal bar, hiding by itself: the volume slider popup and a drag held still ----------
$id = Start-Lumen "minimal" '{"interface.barStyle":"minimal","interface.autoHide":"always","interface.hideDelay":0.5}' @("$videos\Long Seek Test 10min.mp4")
try {
  $lines = Dump $id
  $bar = RectOf (Find $lines '^\s+app::SeekBar \[\d+,\d+ [1-9]')
  $cy = $bar.y + $bar.h / 2
  # Over the video (the bar shows and its hide timer starts), then straight to the bar: a drag
  # held still above the bar outlasts the hide delay.
  & "$tools\input.ps1" -ProcessId $id -Move (Px ($bar.x + $bar.w * 0.3) ($cy - 200))
  # Held still above the bar for longer than the hide delay: the bar stays.
  & "$tools\input.ps1" -ProcessId $id -Drag "$(Px ($bar.x + $bar.w * 0.3) $cy),$(Px ($bar.x + $bar.w * 0.5) ($cy - 150))" -Hold
  $deadline = [DateTime]::Now.AddMilliseconds(2500)
  $p = @()
  while ([DateTime]::Now -lt $deadline) {
    $cb = Find (Dump $id) '^\s+app::ControlBar \['
    if ($cb -match ' hidden') { $p += "the bar hid during the drag"; break }
  }
  & "$tools\input.ps1" -ProcessId $id -Up (Px ($bar.x + $bar.w * 0.5) ($cy - 150))
  Report "control bar stays while a seek drag is held" $p

  & "$tools\input.ps1" -ProcessId $id -Move (Px ($bar.x + 10) $cy)  # the bar back
  $speaker = RectOf (Find (Dump $id) 'VolumeControl \[')
  & "$tools\input.ps1" -ProcessId $id -Click (Px ($speaker.x + $speaker.w / 2) ($speaker.y + $speaker.h / 2))
  $pop = Find (Dump $id) 'VolumePopup \['
  if (-not $pop) { Report "volume popup after a drag released off it" @("the popup did not open") }
  else {
    $v = RectOf $pop
    # From just below the slider's bottom (volume 0, above the mute button) down and away: the
    # volume stays 0.
    # Held off the popup past its close delay (350 ms), then released there.
    $off = Px ($v.x - 300) ($v.y + $v.h - 20)
    & "$tools\input.ps1" -ProcessId $id -Drag "$(Px ($v.x + $v.w / 2) ($v.y + $v.h - 48)),$off" -Hold
    Dump $id | Out-Null; Dump $id | Out-Null
    & "$tools\input.ps1" -ProcessId $id -Up $off
    $deadline = [DateTime]::Now.AddMilliseconds(1500)
    while ([DateTime]::Now -lt $deadline -and (Find (Dump $id) 'VolumePopup \[')) {}
    $lines = Dump $id
    $p = @(Leftovers $lines)
    if (Find $lines 'VolumePopup \[') { $p += "the volume popup stayed open" }
    Report "volume popup after a drag released off it" $p
  }
} finally { Stop-Process -Id $id -ErrorAction SilentlyContinue }

# --- a playlist in its own order: a press that lost its capture leaves no reorder behind ---------
$dd = "$Work\data-playlist"
if (Test-Path $dd) { Remove-Item -Recurse -Force $dd }
New-Item -ItemType Directory -Force $dd | Out-Null
$items = @("Long Seek Test 10min.mp4", "Chaptered Demo (1080p).mkv", "Vertical phone clip.mp4") | ForEach-Object { @{ path = "$videos\$_" } }
$pl = @{ version = 1; playlists = @(@{ id = "p1"; name = "Drag"; created = 1789000000; modified = 1789000000; items = @($items) }) }
[IO.File]::WriteAllText("$dd\playlists.json", ($pl | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding $false))
$id = & "$tools\launch.ps1" -Exe $Exe -DataDir $dd -Library "$env:TEMP\lumen-qa\media"
try {
  $deadline = [DateTime]::Now.AddSeconds(20)
  while ($true) {
    try { $lines = Dump $id; if (@($lines -match 'NavItem \[\d+,\d+ [1-9]').Count -ge 5) { break } } catch {}
    if ([DateTime]::Now -gt $deadline) { throw "the library did not open" }
    Start-Sleep -Milliseconds 300
  }
  $script:scale = [double]((Find $lines '^=== window') -replace '.* scale=([\d.]+).*', '$1')
  $r = RectOf (@($lines -match 'NavItem \[\d+,\d+ [1-9]')[4])  # Playlists
  & "$tools\input.ps1" -ProcessId $id -Click (Px ($r.x + $r.w / 2) ($r.y + $r.h / 2))
  $card = (Find (Dump $id) 'text="Drag" box=') -replace '.*text="Drag" box=(\[[^\]]+\]).*', '$1'
  $r = RectOf $card
  $row = $null
  for ($try = 0; $try -lt 3 -and -not $row; $try++) {  # a page just opened ignores clicks for a moment
    & "$tools\input.ps1" -ProcessId $id -Click (Px ($r.x + $r.w / 2) ($r.y + $r.h / 2)) -Double
    $row = Find (Dump $id) 'text="Chaptered Demo \(1080p\)" box='
  }
  $row = $row -replace '.*text="Chaptered Demo \(1080p\)" box=(\[[^\]]+\]).*', '$1'
  if ($row -notmatch '^\[') { Report "playlist press with a lost capture" @("the playlist did not open") }
  else {
    $r = RectOf $row
    $at = Px ($r.x + 40) ($r.y + $r.h / 2)
    & "$tools\input.ps1" -ProcessId $id -Drag "$at,$at" -Hold -CaptureLost
    & "$tools\input.ps1" -ProcessId $id -Move (Px ($r.x + 40) ($r.y + $r.h + 20))  # no button down
    & "$tools\input.ps1" -ProcessId $id -Move (Px ($r.x + 40) ($r.y + $r.h * 2 + 10))
    $lines = Dump $id
    $p = @(Leftovers $lines)
    $grid = Find $lines 'ItemGrid \[\d+,\d+ [1-9][^\]]*\][^\n]* dragging'
    if ($grid) { $p += "a reorder follows the pointer with no button down" }
    Report "playlist press with a lost capture" $p
  }
} finally { Stop-Process -Id $id -ErrorAction SilentlyContinue }

if ($script:failed) { Write-Output "$($script:failed) case(s) failed"; exit 1 }
Write-Output "all cases passed"
exit 0

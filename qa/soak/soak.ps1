# Soak test: one hidden Lumen instance goes through a loop of everyday actions for hours (videos and
# songs, seeks, the seek preview, side panels, menus, fullscreen, PiP, library pages, Settings, theme
# switches) while its memory, handles, GDI and USER objects, threads and CPU are sampled once a round.
# A steady climb after the warm-up is a leak; report.txt fits a line to each and flags the slopes.
#
# Usage: powershell -File qa\soak\soak.ps1 -Exe <Lumen.exe> -Out <folder in %TEMP%> [-Hours 8]
#        [-Library %TEMP%\lumen-qa\media] [-WarmupMinutes 30]
#   or only the report of an earlier run: soak.ps1 -Out <folder> -ReportOnly
# Writes <Out>\samples.csv, events.log (crashes, hangs, failed steps), handles-<round>.txt (HandleTypes.cs),
# report.txt. Only --automation
# instances (tools\dev\launch.ps1): nothing on screen, no sound, the real mouse untouched.
param([string]$Exe = "", [Parameter(Mandatory = $true)][string]$Out, [double]$Hours = 8,
      [string]$Library = (Join-Path $env:TEMP "lumen-qa\media"),
      [int]$WarmupMinutes = 30, [switch]$ReportOnly)
$ErrorActionPreference = "Stop"
$dev = Join-Path $PSScriptRoot "..\..\tools\dev"
New-Item -ItemType Directory -Force $Out | Out-Null
$csv = Join-Path $Out "samples.csv"
$events = Join-Path $Out "events.log"

function Write-Report {
  $rows = @(Import-Csv $csv)
  $metrics = [ordered]@{ privateMB = 20; workingSetMB = 30; handles = 50; gdi = 10; user = 10; threads = 5 }  # allowed growth per hour
  $lines = @("Soak report: $($rows.Count) samples, $([math]::Round(($rows[-1].minutes -as [double]), 1)) min, warm-up $WarmupMinutes min excluded from the fit", "")
  $after = @($rows | Where-Object { [double]$_.minutes -ge $WarmupMinutes })
  if ($after.Count -lt 10) { $after = $rows }
  foreach ($m in $metrics.Keys) {
    $xs = $after | ForEach-Object { [double]$_.minutes / 60 }; $ys = $after | ForEach-Object { [double]$_.$m }
    $n = $xs.Count; $mx = ($xs | Measure-Object -Average).Average; $my = ($ys | Measure-Object -Average).Average
    $sxy = 0; $sxx = 0
    for ($i = 0; $i -lt $n; $i++) { $sxy += ($xs[$i] - $mx) * ($ys[$i] - $my); $sxx += ($xs[$i] - $mx) * ($xs[$i] - $mx) }
    $slope = if ($sxx -gt 0) { $sxy / $sxx } else { 0 }
    $first = [double]$rows[0].$m; $min = ($ys | Measure-Object -Minimum).Minimum; $max = ($ys | Measure-Object -Maximum).Maximum
    $flag = if ($slope -gt $metrics[$m]) { "SUSPICIOUS" } else { "ok" }
    $lines += "{0,-13} start {1,8:N1}  after warm-up {2,8:N1} .. {3,8:N1}  slope {4,8:N2}/h  (limit {5}/h)  {6}" -f $m, $first, $min, $max, $slope, $metrics[$m], $flag
  }
  $from = Join-Path $Out "handles-$($after[0].round).txt"; $to = Join-Path $Out "handles-$($rows[-1].round).txt"
  if ((Test-Path $from) -and (Test-Path $to)) {
    $tally = { param($f) $g = @{}; foreach ($s in Get-Content -Encoding UTF8 $f) { $g[$s] = 1 + [int]$g[$s] }; $g }
    $a = & $tally $from; $b = & $tally $to
    $lines += "", "Handles by type and name, threads by start address (round $($after[0].round) -> $($rows[-1].round)):"
    foreach ($k in @($a.Keys) + @($b.Keys) | Sort-Object -Unique) {
      $d = [int]$b[$k] - [int]$a[$k]
      if ($d -ne 0) { $lines += "  {0,5}  {1}" -f $d, $k }
    }
  }
  $lines += ""
  $lines += if (Test-Path $events) { @("Events:") + @(Get-Content $events) } else { "Events: none (no crash, hang or failed step)" }
  $lines | Set-Content -Encoding UTF8 (Join-Path $Out "report.txt")
  $lines
}
if ($ReportOnly) { Write-Report; return }

Add-Type -Namespace Soak -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
'@
Add-Type -Path (Join-Path $PSScriptRoot "HandleTypes.cs")
$ownCrashes = Join-Path $Out "crashes"
New-Item -ItemType Directory -Force $ownCrashes | Out-Null
$env:LUMEN_CRASH_DIR = (Resolve-Path $ownCrashes).Path

function Event([string]$text) {
  $line = "{0:HH:mm:ss} {1}" -f (Get-Date), $text
  Add-Content -Encoding UTF8 $events $line
  Write-Host $line
}
function Cmd([string]$c, [int]$timeoutMs = 15000) {
  try { $r = (& "$dev\command.ps1" -ProcessId $script:id -Command $c -TimeoutMs $timeoutMs 2>&1 | Out-String).Trim() }
  catch { $r = "error: $($_.Exception.Message)" }
  if ($r -notmatch "^(ok|idle)" -and $r -notmatch "disabled:|did not open") { $script:failures++; if ($script:failures -le 200) { Event "step '$c': $(($r -split "`n")[0..1] -join ' | ')" } }
  $r
}
function Start-Instance {
  $script:starts++
  $script:id = [int](& "$dev\launch.ps1" -DataDir (Join-Path $Out "data-$($script:starts)") -Fresh -Library $Library -Exe $Exe)
  $until = (Get-Date).AddSeconds(30)
  while ((Get-Date) -lt $until) {
    try { if ((& "$dev\command.ps1" -ProcessId $script:id -Command state -TimeoutMs 2000 2>&1 | Out-String) -match "^ok") { break } } catch {}
    Start-Sleep -Milliseconds 300
  }
  $script:proc = Get-Process -Id $script:id
  [void](Cmd "wait-idle" 120000)  # the first library scan
}
function Pause([int]$ms) { Start-Sleep -Milliseconds $ms }
function Input([hashtable]$a) {
  try { & "$dev\input.ps1" -ProcessId $script:id @a 2>&1 | Out-Null }
  catch { $script:failures++; if ($script:failures -le 200) { Event "input $($a.Keys -join ' '): $($_.Exception.Message)" } }
}
function Hover-SeekBar {
  # Over the seek bar in a few places: the preview instance loads, then unloads after 30 s unused.
  $h = $script:proc.MainWindowHandle
  foreach ($fx in 0.2, 0.4, 0.6, 0.8) {
    Input @{ Move = ("{0},{1}" -f [int](1280 * $fx), 753) }
    Pause 700
  }
  Input @{ Leave = $true }
}

# Once a round, at the same quiet point (no file open, library home, idle), so samples compare.
function Sample {
  $p = Get-Process -Id $script:id -ErrorAction SilentlyContinue
  if (-not $p) { return }
  [HandleTypes]::Snap($script:id) + [HandleTypes]::ThreadStarts($script:id) |
    Set-Content -Encoding UTF8 (Join-Path $Out "handles-$($script:round).txt")
  "{0:HH:mm:ss},{1:F1},{2},{3:F1},{4:F1},{5},{6},{7},{8},{9:F1},{10}" -f (Get-Date), ((Get-Date) - $t0).TotalMinutes, $script:round,
    ($p.PrivateMemorySize64 / 1MB), ($p.WorkingSet64 / 1MB), $p.HandleCount,
    [Soak.Native]::GetGuiResources($p.Handle, 0), [Soak.Native]::GetGuiResources($p.Handle, 1),
    $p.Threads.Count, $p.TotalProcessorTime.TotalSeconds, $script:starts | Add-Content -Encoding ASCII $csv
}

$videos = @(Get-ChildItem -LiteralPath (Join-Path $Library "Videos") -Recurse -File -Include *.mp4, *.mkv | ForEach-Object FullName)
$albums = @(Get-ChildItem -LiteralPath (Join-Path $Library "Music") -Directory | ForEach-Object FullName)
$mixed = Join-Path $Library "Mixed\mixed list.m3u"

# The loop body: one round is about two and a half minutes.
$steps = @(
  { Cmd "open $($videos[$script:round % $videos.Count])"; [void](Cmd "wait-idle" 20000); Pause 8000 },
  { foreach ($j in "playback.jump_30", "playback.seek_forward", "playback.seek_backward_long", "playback.jump_80", "playback.next_chapter") { [void](Cmd "action $j"); Pause 600 } },
  { Hover-SeekBar },
  { foreach ($p in "tracks", "subtitles", "chapters", "queue", "bookmarks", "speed") { [void](Cmd "panel $p"); Pause 700 }; [void](Cmd "panel close") },
  { [void](Cmd "menu player"); Pause 800; [void](Cmd "menu close") },
  { [void](Cmd "action video.statistics"); Pause 3000; [void](Cmd "action video.statistics") },
  { [void](Cmd "action video.fullscreen"); Pause 2500; [void](Cmd "action video.exit_fullscreen"); Pause 1500 },
  { [void](Cmd "action video.pip"); Pause 2500; [void](Cmd "action video.pip"); Pause 1500 },
  { Cmd "open $($albums[$script:round % $albums.Count])"; [void](Cmd "wait-idle" 20000); Pause 6000 },
  { foreach ($k in 1..3) { [void](Cmd "action playback.next"); Pause 2500 } },
  { foreach ($r in "music albums", "music artists", "music songs", "music genres", "home", "videos movies", "videos shows", "videos folders", "recent", "playlists", "network") { [void](Cmd "nav $r"); Pause 900 } },
  { [void](Cmd "nav player"); Pause 3000 },
  { foreach ($s in "general", "interface", "video", "audio", "subtitles", "library", "performance") { [void](Cmd "nav settings $s"); Pause 700 }
    Input @{ Title = "Настройки"; Key = "Escape" }; Pause 1000 },
  { [void](Cmd "set general.theme light"); Pause 1500; [void](Cmd "set general.theme dark"); Pause 1500 },
  { Cmd "open $mixed"; Pause 4000; [void](Cmd "action playback.next"); Pause 3000 },
  { [void](Cmd "action media.close"); [void](Cmd "nav home"); [void](Cmd "wait-idle" 20000); Pause 5000; Sample }
)

"time,minutes,round,privateMB,workingSetMB,handles,gdi,user,threads,cpuSeconds,starts" | Set-Content -Encoding ASCII $csv
$script:starts = 0; $script:failures = 0; $script:round = 0
Start-Instance
$t0 = Get-Date
$end = $t0.AddHours($Hours)
Event "start: $Exe, $Hours h, library $Library"
while ((Get-Date) -lt $end) {
  foreach ($step in $steps) {
    if (-not (Get-Process -Id $script:id -ErrorAction SilentlyContinue)) {
      $reports = @(Get-ChildItem $ownCrashes -Filter "crash-*.txt" | Sort-Object LastWriteTime | Select-Object -Last 1 | ForEach-Object Name)
      Event "CRASH in round $($script:round) (instance $($script:starts)); report: $($reports -join ' '); restarting"
      Start-Instance
    }
    $r = Cmd "state" 10000
    if ($r -notmatch "^ok") {
      Event "HANG in round $($script:round): no answer to 'state'; killing and restarting"
      Stop-Process -Id $script:id -Force -ErrorAction SilentlyContinue
      Wait-Process -Id $script:id -Timeout 15 -ErrorAction SilentlyContinue
      Start-Instance
    }
    & $step | Out-Null
  }
  $script:round++
}
Event "end after $($script:round) rounds, $($script:starts) instance(s), $($script:failures) failed steps"
Stop-Process -Id $script:id -Force -ErrorAction SilentlyContinue
Write-Report

# Lumen performance probe: startup trace, memory, CPU. Background (--automation) instances only.
param([string]$Exe = (Join-Path $PSScriptRoot "..\..\build\Lumen.exe"), [string]$Tag = "base", [int]$Runs = 3)
$t = (Join-Path $PSScriptRoot "..\..\tools\dev")
$m = "$env:TEMP\lumen-qa\media"
$trace = "$env:TEMP\lumen-trace.txt"
$env:LUMEN_TRACE = "1"

function Probe($id, $label, $seconds) {
  $p = Get-Process -Id $id
  $c0 = $p.TotalProcessorTime.TotalMilliseconds
  Start-Sleep -Seconds $seconds
  $p.Refresh()
  $c1 = $p.TotalProcessorTime.TotalMilliseconds
  $cpu = [math]::Round(($c1 - $c0) / ($seconds * 10), 2)
  "{0,-28} WS={1,6} MB  Private={2,6} MB  CPU={3,5}% of one core  threads={4}" -f $label,
    [math]::Round($p.WorkingSet64 / 1MB), [math]::Round($p.PrivateMemorySize64 / 1MB), $cpu, $p.Threads.Count
}
function Stop($id) {
  $p = Get-Process -Id $id -ErrorAction SilentlyContinue
  if ($p) { $sw = [Diagnostics.Stopwatch]::StartNew(); $p.CloseMainWindow() | Out-Null
    if (-not $p.WaitForExit(5000)) { Stop-Process -Id $id -Force; "close: killed" } else { "close: {0} ms" -f $sw.ElapsedMilliseconds } }
}

"=== $Tag ==="
for ($i = 0; $i -lt $Runs; $i++) {
  Remove-Item $trace -ErrorAction SilentlyContinue
  $id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\lumen-qa\perf-$Tag" -Fresh -Library $m
  Start-Sleep 3
  "library start #$i : " + ((Get-Content $trace) -join " | ")
  if ($i -eq 0) { Probe $id "library idle" 5 }
  Stop $id
}
for ($i = 0; $i -lt $Runs; $i++) {
  Remove-Item $trace -ErrorAction SilentlyContinue
  $id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\lumen-qa\perf-$Tag" -Fresh "$m\Videos\Chaptered Demo (1080p).mkv"
  Start-Sleep 3
  "file start #$i : " + ((Get-Content $trace) -join " | ")
  if ($i -eq 0) {
    Probe $id "1080p playing" 5
    & "$t\input.ps1" -ProcessId $id -Key Space | Out-Null
    Probe $id "1080p paused" 5
  }
  Stop $id
}
Remove-Item $trace -ErrorAction SilentlyContinue
$id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\lumen-qa\perf-$Tag" -Fresh "$m\Videos\4K Mandelbrot.mp4"
Start-Sleep 3
Probe $id "4K playing" 5
Stop $id

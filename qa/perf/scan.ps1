# First-scan time and CPU for the big library (biglib.ps1); -NoThumbs isolates the scan.
# -Threads only worked with a temporary LUMEN_SCAN_THREADS override in MediaLibrary.cpp
# (MetadataThreads); re-add it there when comparing thread counts.
param([int]$Threads = 1, [switch]$NoThumbs)
$t = (Join-Path $PSScriptRoot "..\..\tools\dev")
$env:LUMEN_SCAN_THREADS = "$Threads"
$set = if ($NoThumbs) { '{"library.thumbnails":false}' } else { '' }
$d = "$env:TEMP\lumen-qa\perf-scan"
$sw = [Diagnostics.Stopwatch]::StartNew()
$id = if ($set) { & "$t\launch.ps1" -DataDir $d -Fresh -Settings $set -Library "$env:TEMP\lumen-qa\big" } else { & "$t\launch.ps1" -DataDir $d -Fresh -Library "$env:TEMP\lumen-qa\big" }
$p = Get-Process -Id $id
$last = 0; $quiet = 0
for ($i = 0; $i -lt 240; $i++) {
  Start-Sleep -Milliseconds 250; $p.Refresh(); $c = $p.TotalProcessorTime.TotalMilliseconds
  if ($i -gt 8 -and $c - $last -lt 10) { $quiet++ } else { $quiet = 0 }
  $last = $c; if ($quiet -ge 3) { break }
}
"threads={0} thumbs={1}: done ~{2:N1} s, cpu {3} ms" -f $Threads, (-not $NoThumbs), (($sw.ElapsedMilliseconds - 750) / 1000), [math]::Round($last)
$p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(5000)) { Stop-Process -Id $id -Force }

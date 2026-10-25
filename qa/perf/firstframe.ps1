# Startup timeline (MOONBOW_TRACE): window, first frame (WARP), hardware device, mpv, file.
# Alternates library and file starts. Background (--automation) instances only.
param([string]$Exe = (Join-Path $PSScriptRoot "..\..\build\Moonbow.exe"), [int]$Runs = 6)
$t = Join-Path $PSScriptRoot "..\..\tools\dev"
$trace = "$env:TEMP\moonbow-trace.txt"
$env:MOONBOW_TRACE = "1"
$file = "$env:TEMP\moonbow-qa\media\Videos\Chaptered Demo (1080p).mkv"
for ($i = 0; $i -lt $Runs; $i++) {
  Remove-Item $trace -ErrorAction SilentlyContinue
  if ($i % 2 -eq 0) {
    $what = "library"
    $id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\moonbow-qa\perf-ff$i" -Fresh -Library "$env:TEMP\moonbow-qa\media"
  } else {
    $what = "file   "
    $id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\moonbow-qa\perf-ff$i" -Fresh $file
  }
  Start-Sleep 2
  $line = (Get-Content $trace | Where-Object { $_ -notmatch "frames|mpv (load|dll|init)" } | ForEach-Object { $_.Trim() }) -join " | "
  "$what : $line"
  Stop-Process -Id $id -Force
}

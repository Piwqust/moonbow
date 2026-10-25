# Library idle cost and open-from-library latency (mpv log: loadfile -> Starting playback).
param([string]$Extra = "", [string]$Tag = "lib", [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\Moonbow.exe"))
$t = (Join-Path $PSScriptRoot "..\..\tools\dev")
$log = "$env:TEMP\moonbow-qa\mpv-$Tag.log"
Remove-Item $log -ErrorAction SilentlyContinue
$opts = ("--log-file=" + $log.Replace('\', '/') + " " + $Extra).Trim()
$json = @{ "general.extraOptions" = $opts } | ConvertTo-Json -Compress
$id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\moonbow-qa\perf-$Tag" -Fresh -Settings $json -Library "$env:TEMP\moonbow-qa\media"
Start-Sleep 4
$p = Get-Process -Id $id
$c0 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep 4; $p.Refresh()
"{0} library idle: WS={1} MB Private={2} MB CPU={3}% threads={4}" -f $Tag, [math]::Round($p.WorkingSet64/1MB), [math]::Round($p.PrivateMemorySize64/1MB),
  [math]::Round(($p.TotalProcessorTime.TotalMilliseconds - $c0)/40, 2), $p.Threads.Count
& "$t\input.ps1" -ProcessId $id -Click "100,114" | Out-Null; Start-Sleep 2
& "$t\input.ps1" -ProcessId $id -Click "1094,200" -Double | Out-Null; Start-Sleep 2
& "$t\input.ps1" -ProcessId $id -Click "1094,200" -Double | Out-Null; Start-Sleep 3
$p.Refresh()
"{0} playing: WS={1} MB Private={2} MB title={3}" -f $Tag, [math]::Round($p.WorkingSet64/1MB), [math]::Round($p.PrivateMemorySize64/1MB), $p.MainWindowTitle
$p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(5000)) { Stop-Process -Id $id -Force }
if (Test-Path $log) {
  $lines = Get-Content $log
  $load = $lines | Where-Object { $_ -match "Run command: loadfile" } | Select-Object -First 1
  $start = $lines | Where-Object { $_ -match "Starting playback" } | Select-Object -First 1
  $vo = $lines | Where-Object { $_ -match "Initializing GPU context 'd3d11'|Using Direct3D 11 feature" } | Select-Object -First 2
  "loadfile: $load"; "start:    $start"; $vo
}

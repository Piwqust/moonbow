# Plays one file with extra mpv options, prints CPU/memory and key mpv log lines.
param([string]$File = "$env:TEMP\moonbow-qa\media\Videos\Chaptered Demo (1080p).mkv", [string]$Extra = "", [string]$Tag = "opt",
      [string]$Exe = (Join-Path $PSScriptRoot "..\..\build\Moonbow.exe"))
$t = (Join-Path $PSScriptRoot "..\..\tools\dev")
$log = "$env:TEMP\moonbow-qa\mpv-$Tag.log"
Remove-Item $log -ErrorAction SilentlyContinue
$opts = ("--log-file=" + $log.Replace('\', '/') + " " + $Extra).Trim()
$json = @{ "general.extraOptions" = $opts } | ConvertTo-Json -Compress
$id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\moonbow-qa\perf-$Tag" -Fresh -Settings $json $File
Start-Sleep 3
$p = Get-Process -Id $id
$c0 = $p.TotalProcessorTime.TotalMilliseconds; Start-Sleep 5; $p.Refresh()
"{0}: WS={1} MB Private={2} MB CPU={3}% threads={4}" -f $Tag, [math]::Round($p.WorkingSet64/1MB), [math]::Round($p.PrivateMemorySize64/1MB),
  [math]::Round(($p.TotalProcessorTime.TotalMilliseconds - $c0)/50, 2), $p.Threads.Count
$p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(5000)) { Stop-Process -Id $id -Force }
if (Test-Path $log) { Select-String -Path $log -Pattern "Using hardware decoding|hwdec|Using video output|VO:|first video frame|playback start|Starting playback|gpu-next|d3d11" | Select-Object -First 25 | ForEach-Object { $_.Line } }

# Fast scroll through a 400-video folder; checks that bottom thumbnails arrive quickly.
param([string]$Exe = (Join-Path $PSScriptRoot "..\..\build\Lumen.exe"), [string]$Tag = "th")
$t = (Join-Path $PSScriptRoot "..\..\tools\dev"); $o = "$env:TEMP\lumen-qa\v"
. "$PSScriptRoot\stlib.ps1"
$id = & "$t\launch.ps1" -Exe $Exe -DataDir "$env:TEMP\lumen-qa\perf-$Tag" -Fresh -Library "$env:TEMP\lumen-qa\flat"
Start-Sleep 4
$h = [St]::Find([uint32]$id)
& "$t\input.ps1" -ProcessId $id -Click "100,114" | Out-Null; Start-Sleep 1
& "$t\input.ps1" -ProcessId $id -Click "745,200" -Double | Out-Null; Start-Sleep 1   # first folder card (Clips)
& "$t\capture.ps1" -ProcessId $id -Out "$o\th-0.png" -Scale 0.4 | Out-Null
[St]::Wheel($h, 700, 400, -1, 80, 10)
Start-Sleep -Milliseconds 700
& "$t\capture.ps1" -ProcessId $id -Out "$o\th-1.png" -Scale 0.4 | Out-Null
Start-Sleep 2
& "$t\capture.ps1" -ProcessId $id -Out "$o\th-2.png" -Scale 0.4 | Out-Null
$p = Get-Process -Id $id; "WS={0} MB Private={1} MB" -f [math]::Round($p.WorkingSet64/1MB), [math]::Round($p.PrivateMemorySize64/1MB)
$p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(5000)) { Stop-Process -Id $id -Force }


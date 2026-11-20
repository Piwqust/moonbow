# Shared helpers for the site's screenshot scripts. Dot-source it: . "$PSScriptRoot\session.ps1"
# Every Moonbow here is a hidden --automation test instance (no window on screen, no focus taken),
# driven by the repo's own tools in tools\dev. Only processes started here are stopped.
$Repo = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$Dev = Join-Path $Repo "tools\dev"
$Exe = Join-Path $Repo "build\Moonbow.exe"
$Media = Join-Path $env:TEMP "moonbow-site\media"
$ShotsOut = Join-Path $PSScriptRoot "..\..\src\assets\shots"
$Enc = New-Object System.Text.UTF8Encoding $false

# A fresh data folder with the window placed at an exact size (client 1280x800 DIP at 2x = 2560x1600 px
# plus the frame) and the onboarding skipped. -Settings is merged into settings.json.
function New-Moonbow([string]$Name, [hashtable]$Settings = @{}, [string[]]$Files = @(), [switch]$NoLibrary) {
  $data = Join-Path $env:TEMP "moonbow-site\data-$Name"
  if (Test-Path $data) { Remove-Item -Recurse -Force $data }
  New-Item -ItemType Directory -Force $data | Out-Null
  [IO.File]::WriteAllText("$data\ui.json", '{"onboarded":"1","window.placement":"100,60,2660,1700,0"}', $Enc)
  $s = @{ "general.language" = "ru"; "general.theme" = "dark" }
  foreach ($k in $Settings.Keys) { $s[$k] = $Settings[$k] }
  $json = ($s | ConvertTo-Json -Compress)
  $a = @{ DataDir = $data; Exe = $Exe; Settings = $json; Scale = 1.6 }
  if (-not $NoLibrary) { $a.Library = $Media }
  $id = & "$Dev\launch.ps1" @a @Files
  Start-Sleep -Milliseconds 1500
  return [int]$id
}
function Cmd([int]$Id, [string]$Command, [int]$TimeoutMs = 20000) {
  $r = & "$Dev\command.ps1" -ProcessId $Id -Command $Command -TimeoutMs $TimeoutMs
  if (($r | Select-Object -First 1) -ne "ok") { Write-Warning "$Command -> $($r -join ' ')" }
}
function Idle([int]$Id, [int]$TimeoutMs = 30000) { & "$Dev\command.ps1" -ProcessId $Id -Command "wait-idle" -TimeoutMs $TimeoutMs -Quiet | Out-Null }
function Shot([int]$Id, [string]$Name, [string]$Title = "") {
  New-Item -ItemType Directory -Force $ShotsOut | Out-Null
  $out = Join-Path (Resolve-Path $ShotsOut) "$Name.png"
  $a = @{ ProcessId = $Id; Out = $out }
  if ($Title) { $a.Title = $Title }
  $size = & "$Dev\capture.ps1" @a
  "$Name  $size"
}
function Key([int]$Id, [string]$Key, [switch]$Ctrl, [switch]$Shift) {
  $a = @{ ProcessId = $Id; Key = $Key }
  if ($Ctrl) { $a.Ctrl = $true }
  if ($Shift) { $a.Shift = $true }
  & "$Dev\input.ps1" @a | Out-Null
}
function Set-Pointer([int]$Id, [int]$X, [int]$Y) { & "$Dev\input.ps1" -ProcessId $Id -Move "$X,$Y" | Out-Null }
function Stop-Moonbow([int]$Id) { Stop-Process -Id $Id -Force -ErrorAction SilentlyContinue }


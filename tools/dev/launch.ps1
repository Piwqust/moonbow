# Starts an isolated Lumen test instance and prints its PID.
# Usage: tools\dev\launch.ps1 -DataDir $env:TEMP\lumen-test-x [-Exe path] [-Fresh] [-Settings '{"key":value}'] [files...]
#   -Fresh      wipe the data dir first
#   -Settings   JSON object merged into settings.json "values" (startup volume is forced to 0)
#   -Library    library root folder written to library.json
#   -Onboarding keep the first-run onboarding (skipped by default)
#   -Scale      lay out at this factor of the monitor scale (1.5 = 150 % on a 100 % screen)
[CmdletBinding(PositionalBinding = $false)]
param([Parameter(Mandatory = $true)][string]$DataDir, [string]$Exe = "",
      [switch]$Fresh, [string]$Settings = "", [string]$Library = "", [switch]$Onboarding, [double]$Scale = 0,
      [Parameter(ValueFromRemainingArguments = $true)][string[]]$Files)
if (-not $Exe) { $Exe = Join-Path $PSScriptRoot "..\..\build\Lumen.exe" }
if ($Fresh -and (Test-Path $DataDir)) { Remove-Item -Recurse -Force $DataDir }
New-Item -ItemType Directory -Force $DataDir | Out-Null
$enc = New-Object System.Text.UTF8Encoding $false
if (-not $Onboarding -and -not (Test-Path "$DataDir\ui.json")) {
  [IO.File]::WriteAllText("$DataDir\ui.json", '{"onboarded":"1"}', $enc)
}
$values = @{ "audio.startupVolumeMode" = "fixed"; "audio.startupVolume" = 0 }
if (Test-Path "$DataDir\settings.json") {
  try { (Get-Content -Raw "$DataDir\settings.json" | ConvertFrom-Json).values.PSObject.Properties | ForEach-Object { $values[$_.Name] = $_.Value } } catch {}
}
if ($Settings) { ($Settings | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $values[$_.Name] = $_.Value } }
[IO.File]::WriteAllText("$DataDir\settings.json", (@{ version = 1; values = $values } | ConvertTo-Json -Depth 5), $enc)
if ($Library) { [IO.File]::WriteAllText("$DataDir\library.json", (@{ version = 1; roots = @($Library) } | ConvertTo-Json), $enc) }
$argList = @("--new-instance", "--automation", "--data-dir", "`"$DataDir`"")
if ($Scale -gt 0) { $argList += @("--scale", [string]::Format([Globalization.CultureInfo]::InvariantCulture, "{0}", $Scale)) }
foreach ($f in $Files) { $argList += "`"$f`"" }
$p = Start-Process $Exe -ArgumentList $argList -PassThru
$p.Id

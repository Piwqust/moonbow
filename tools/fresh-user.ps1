# Shows Moonbow the way a person who never had it sees it.
#   powershell -ExecutionPolicy Bypass -File tools\fresh-user.ps1
#     Windows Sandbox: a clean throwaway Windows. The newest dist\*-setup.exe is put in Downloads
#     marked as downloaded from the internet (so SmartScreen warns as for a tester) and started.
#     Closing the Sandbox window throws everything away.
#   ... -Setup <file.exe or .zip>   that build instead of the newest
#   ... -Media <folder>             also shows the folder in the Sandbox as Videos\media (read-only)
#   ... -App                        no Sandbox: this PC's current build with an empty profile (first
#                                   run, no library, default settings). Your data and registry stay as they are.
param([string]$Setup, [string]$Media, [switch]$App)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path

if ($App) {
  $exe = @("$root\build\Moonbow.exe", "$root\dist\build\Moonbow.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (!$exe) { throw "No Moonbow.exe in build\ or dist\build\: run build.bat first." }
  $dataDir = Join-Path $env:TEMP ("moonbow-fresh-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
  # --new-instance: a running Moonbow of your own would otherwise take the start over.
  Start-Process $exe -ArgumentList "--new-instance", "--data-dir", "`"$dataDir`""
  Write-Host "Fresh profile: $dataDir (delete it when done)"
  return
}

if (!(Test-Path "$env:windir\System32\WindowsSandbox.exe")) {
  throw "Windows Sandbox is off. In an admin PowerShell: Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM, then restart."
}
if (!$Setup) {
  $Setup = Get-ChildItem "$root\dist\*-setup.exe" | Sort-Object LastWriteTime | Select-Object -Last 1 -ExpandProperty FullName
  if (!$Setup) { throw "No dist\*-setup.exe: run tools\package-beta.ps1 first." }
}
$Setup = (Resolve-Path $Setup).Path

$box = Join-Path $env:TEMP "moonbow-fresh-sandbox"
if (Test-Path $box) { Remove-Item -Recurse -Force $box }
New-Item -ItemType Directory $box | Out-Null
Copy-Item $Setup $box
$name = Split-Path $Setup -Leaf
@"
`$f = Copy-Item "C:\moonbow\$name" "`$env:USERPROFILE\Downloads" -PassThru
Set-Content -Path `$f.FullName -Stream Zone.Identifier -Value "[ZoneTransfer]", "ZoneId=3"
Start-Process explorer "`$env:USERPROFILE\Downloads"
if (`$f.Extension -eq ".exe") { Start-Process `$f.FullName }
"@ | Set-Content "$box\start.ps1" -Encoding UTF8

$folders = "<MappedFolder><HostFolder>$box</HostFolder><SandboxFolder>C:\moonbow</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>"
if ($Media) {
  $folders += "<MappedFolder><HostFolder>$((Resolve-Path $Media).Path)</HostFolder><SandboxFolder>C:\Users\WDAGUtilityAccount\Videos\media</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>"
}
@"
<Configuration>
  <MappedFolders>$folders</MappedFolders>
  <LogonCommand><Command>powershell -ExecutionPolicy Bypass -WindowStyle Hidden -File C:\moonbow\start.ps1</Command></LogonCommand>
</Configuration>
"@ | Set-Content "$box\moonbow.wsb" -Encoding UTF8
Start-Process "$box\moonbow.wsb"
Write-Host "Sandbox starting with $name"

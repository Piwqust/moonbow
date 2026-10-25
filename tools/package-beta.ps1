# Builds a beta for testers from the current commit:
#   dist\Moonbow-<version>-beta-<commit>.zip       Moonbow.exe, libmpv-2.dll, README-BETA.txt
#   dist\Moonbow-<version>-beta-<commit>-setup.exe the same files as Moonbow's own setup (src/setup):
#                                                install, update, go back to a kept version, uninstall
#   dist\symbols\<version>-<commit>\             Moonbow.exe + Moonbow.pdb, to read the testers' crash dumps
# Usage: powershell -ExecutionPolicy Bypass -File tools\package-beta.ps1
# Reading a dump: open crash-*.dmp in WinDbg, .sympath+ dist\symbols\<version>-<commit>, then !analyze -v.
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
# A dump is only readable with the .pdb of the exact build, so the zip must match a commit.
if (git -C $root status --porcelain) { throw "Commit first: uncommitted changes would not match the saved symbols." }
$commit = (git -C $root rev-parse --short HEAD).Trim()
$version = [regex]::Match((Get-Content -Raw "$root\res\app.rc"), 'VALUE "ProductVersion", "([^"]+)"').Groups[1].Value
$name = "Moonbow-$version-beta-$commit"
$dist = Join-Path $root "dist"
$build = Join-Path $dist "build"

$env:MOONBOW_OUT = $build
cmd /d /c "$root\build.bat"
if ($LASTEXITCODE) { throw "build.bat failed" }

$stage = Join-Path $dist $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item "$build\Moonbow.exe", "$build\libmpv-2.dll", "$PSScriptRoot\beta\README-BETA.txt" $stage
Compress-Archive -Path "$stage\*" -DestinationPath "$dist\$name.zip" -Force
# The setup: this Moonbow.exe with the other files appended. A GUI exe: wait for it.
$setup = Start-Process "$build\Moonbow.exe" -ArgumentList "--make-setup", "`"$dist\$name-setup.exe`"", "`"$stage\libmpv-2.dll`"", "`"$stage\README-BETA.txt`"" -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode) { throw "Moonbow.exe --make-setup failed" }
Remove-Item -Recurse -Force $stage

$symbols = Join-Path $dist "symbols\$version-$commit"
New-Item -ItemType Directory -Force $symbols | Out-Null
Copy-Item "$build\Moonbow.exe", "$build\Moonbow.pdb" $symbols

Write-Host "Beta: $dist\$name.zip"
Write-Host "Symbols: $symbols (keep them as long as testers may send dumps of this build)"

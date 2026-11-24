# Builds the public release files from the current, committed state:
#   dist\release\v<version>\Moonbow-windows-x64-setup.exe   Moonbow's own setup (install, update, go back, uninstall)
#   dist\release\v<version>\Moonbow-windows-x64.zip         portable: the same files, unpack and run
#   dist\release\v<version>\*.sha256                        "<hash>  <file>", one per file (sha256sum -c reads it)
#   dist\release\v<version>\moonbow-update.json             the signed update feed installed Moonbows read
#                                                         (app/Updater.h, tools\release\make-update-feed.ps1)
#   dist\release\v<version>\Moonbow-windows-x64-symbols.zip  Moonbow.exe + .pdb, to read crash dumps of this build
#   dist\symbols\<version>-<commit>\                       the same, unpacked
# The names carry no version, so README links to releases/latest/download/<name> keep working.
# -Mirror <url>: a second download address in the feed (the release workflow passes MOONBOW_MIRROR_URL).
# -RequireFeed: fail without the signing key instead of leaving the feed out (the release workflow).
# Usage: powershell -ExecutionPolicy Bypass -File tools\package-release.ps1 [-SkipTests] [-Mirror <url>] [-RequireFeed]
param([switch]$SkipTests, [string]$Mirror = "", [switch]$RequireFeed)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
# A crash dump is only readable with the .pdb of the exact build, so the files must match a commit.
if (git -C $root status --porcelain) { throw "Commit first: uncommitted changes would not match the saved symbols." }
$commit = (git -C $root rev-parse --short HEAD).Trim()
$version = [regex]::Match((Get-Content -Raw "$root\res\app.rc"), 'VALUE "ProductVersion", "([^"]+)"').Groups[1].Value
if (-not $version) { throw "No ProductVersion in res\app.rc" }
$dist = Join-Path $root "dist"
$build = Join-Path $dist "release-build"
$out = Join-Path $dist "release\v$version"

$env:MOONBOW_OUT = $build
cmd /d /c "`"$root\build.bat`""
if ($LASTEXITCODE) { throw "build.bat failed" }
if (-not $SkipTests) {
  cmd /d /c "`"$root\tests\build-tests.bat`""
  if ($LASTEXITCODE) { throw "tests failed" }
}

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
$stage = Join-Path $dist "release-stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item "$build\Moonbow.exe", "$build\libmpv-2.dll", "$build\MoonbowThumbs.dll", "$PSScriptRoot\release\README.txt", "$root\THIRD_PARTY_NOTICES.md" $stage
Copy-Item "$root\LICENSE" (Join-Path $stage "LICENSE.txt")

# The zip is the portable copy: portable.txt keeps its data in "Data" beside it (src/main.cpp).
# The setup below takes its files by name and leaves portable.txt out.
Copy-Item "$PSScriptRoot\release\portable.txt" $stage
$zip = Join-Path $out "Moonbow-windows-x64.zip"
Compress-Archive -Path "$stage\*" -DestinationPath $zip -Force
# The setup: this Moonbow.exe with the other files appended. A GUI exe: wait for it, hidden.
$setupFile = Join-Path $out "Moonbow-windows-x64-setup.exe"
$extra = @("libmpv-2.dll", "MoonbowThumbs.dll", "README.txt", "LICENSE.txt", "THIRD_PARTY_NOTICES.md") | ForEach-Object { "`"$(Join-Path $stage $_)`"" }
$setup = Start-Process "$build\Moonbow.exe" -ArgumentList (@("--make-setup", "`"$setupFile`"") + $extra) -Wait -PassThru -WindowStyle Hidden
if ($setup.ExitCode -or -not (Test-Path $setupFile)) { throw "Moonbow.exe --make-setup failed" }
Remove-Item -Recurse -Force $stage

foreach ($f in @($setupFile, $zip)) {
  $hash = (Get-FileHash -Algorithm SHA256 $f).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText("$f.sha256", "$hash  $(Split-Path -Leaf $f)`n", (New-Object System.Text.UTF8Encoding $false))
}

# The update feed, signed: without it installed Moonbows do not see this version. A local test build
# without the key leaves it out.
$haveKey = $env:MOONBOW_UPDATE_KEY -or (Test-Path "$env:USERPROFILE\.moonbow-release\update-signing-key.txt")
if ($haveKey) {
  & "$PSScriptRoot\release\make-update-feed.ps1" -Version $version -Setup $setupFile -Out (Join-Path $out "moonbow-update.json") -Mirror $Mirror
} elseif ($RequireFeed) {
  throw "No update signing key: set MOONBOW_UPDATE_KEY (tools\release\new-update-key.ps1)"
} else {
  Write-Warning "No update signing key: no moonbow-update.json, installed Moonbows will not see this version."
}

$symbols = Join-Path $dist "symbols\$version-$commit"
New-Item -ItemType Directory -Force $symbols | Out-Null
Copy-Item "$build\Moonbow.exe", "$build\Moonbow.pdb", "$build\MoonbowThumbs.dll", "$build\MoonbowThumbs.pdb" $symbols
Compress-Archive -Path "$symbols\*" -DestinationPath (Join-Path $out "Moonbow-windows-x64-symbols.zip") -Force

Write-Host "Moonbow $version ($commit):"
Get-ChildItem $out | ForEach-Object { Write-Host ("  {0,-38} {1,8:N1} MB" -f $_.Name, ($_.Length / 1MB)) }
Write-Host "Symbols: $symbols (keep them while people may send crash dumps of this build)"

# Writes the package manager manifests for a published release:
#   packaging\winget\<version>\Piwqust.Moonbow*.yaml   for a pull request to microsoft/winget-pkgs
#                                                       (manifests\p\Piwqust\Moonbow\<version>\)
#   bucket\moonbow.json                                 Scoop: scoop bucket add moonbow https://github.com/Piwqust/moonbow
# The hashes come from the release's .sha256 files (gh release download), so run it after the release
# workflow has published the version. winget installs the setup silently for the current user; Scoop
# unpacks the portable zip and keeps its Data folder between versions.
# Usage: powershell -ExecutionPolicy Bypass -File tools\release\make-package-manifests.ps1 <version> [-Date yyyy-mm-dd]
param([Parameter(Mandatory = $true)][string]$Version, [string]$Date = "")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
$repo = "Piwqust/moonbow"
$tag = "v$Version"
$base = "https://github.com/$repo/releases/download/$tag"

$tmp = Join-Path $env:TEMP "moonbow-manifests-$Version"
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory $tmp | Out-Null
gh release download $tag --repo $repo --pattern "*.sha256" --dir $tmp
if ($LASTEXITCODE) { throw "gh release download failed: is $tag published?" }
function Hash($name) {
  $line = [IO.File]::ReadAllText((Join-Path $tmp "$name.sha256")).Trim()
  $h = ($line -split '\s+')[0]
  if ($h -notmatch '^[0-9a-f]{64}$') { throw "Bad hash in $name.sha256: $line" }
  return $h
}
$setupHash = (Hash "Moonbow-windows-x64-setup.exe").ToUpperInvariant()
$zipHash = Hash "Moonbow-windows-x64.zip"
if (-not $Date) {
  $Date = (gh release view $tag --repo $repo --json publishedAt --jq .publishedAt).Substring(0, 10)
}
Remove-Item -Recurse -Force $tmp

$utf8 = New-Object System.Text.UTF8Encoding $false
function Save($path, $text) {
  $dir = Split-Path -Parent $path
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
  [IO.File]::WriteAllText($path, ($text -replace "`r`n", "`n"), $utf8)
}

$schema = "1.9.0"
$id = "Piwqust.Moonbow"
$winget = Join-Path $root "packaging\winget\$Version"
Save (Join-Path $winget "$id.yaml") @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.$schema.schema.json
PackageIdentifier: $id
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: $schema
"@
Save (Join-Path $winget "$id.installer.yaml") @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.$schema.schema.json
PackageIdentifier: $id
PackageVersion: $Version
MinimumOSVersion: 10.0.18362.0
InstallerType: exe
Scope: user
InstallModes:
- interactive
- silent
- silentWithProgress
InstallerSwitches:
  Silent: --silent
  SilentWithProgress: --silent
UpgradeBehavior: install
ProductCode: Moonbow
ReleaseDate: $Date
AppsAndFeaturesEntries:
- DisplayName: Moonbow
  Publisher: Moonbow
  ProductCode: Moonbow
Installers:
- Architecture: x64
  InstallerUrl: $base/Moonbow-windows-x64-setup.exe
  InstallerSha256: $setupHash
ManifestType: installer
ManifestVersion: $schema
"@
Save (Join-Path $winget "$id.locale.en-US.yaml") @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.defaultLocale.$schema.schema.json
PackageIdentifier: $id
PackageVersion: $Version
PackageLocale: en-US
Publisher: Piwqust
PublisherUrl: https://github.com/Piwqust
PublisherSupportUrl: https://github.com/$repo/issues
PackageName: Moonbow
PackageUrl: https://moonbow.angleford.co
License: GPL-3.0-or-later
LicenseUrl: https://github.com/$repo/blob/main/LICENSE
ShortDescription: A fast, quiet video and music player for Windows, built on mpv.
Description: >-
  Moonbow plays video and music with mpv's picture quality and opens a film in about a third of a second.
  It keeps a library with Continue Watching and series by season without a server, shows synced lyrics,
  and has every setting in a searchable window instead of a config file.
Moniker: moonbow
Tags:
- media-player
- music-player
- video-player
- mpv
- subtitles
ReleaseNotesUrl: https://github.com/$repo/releases/tag/$tag
ManifestType: defaultLocale
ManifestVersion: $schema
"@
Save (Join-Path $winget "$id.locale.ru-RU.yaml") @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.locale.$schema.schema.json
PackageIdentifier: $id
PackageVersion: $Version
PackageLocale: ru-RU
Publisher: Piwqust
PackageName: Moonbow
License: GPL-3.0-or-later
ShortDescription: Быстрый и тихий видео- и музыкальный плеер для Windows на основе mpv.
ManifestType: locale
ManifestVersion: $schema
"@

# Scoop: the portable zip. portable.txt keeps the data in Data (zips before 0.3.1 lack it), which
# Scoop carries over between versions.
$scoop = [ordered]@{
  version      = $Version
  description  = "A fast, quiet video and music player for Windows, built on mpv."
  homepage     = "https://moonbow.angleford.co"
  license      = "GPL-3.0-or-later"
  architecture = [ordered]@{ "64bit" = [ordered]@{ url = "$base/Moonbow-windows-x64.zip"; hash = $zipHash } }
  pre_install  = @('if (!(Test-Path "$dir\portable.txt")) { Set-Content "$dir\portable.txt" "Scoop: Moonbow keeps its data in the Data folder." }')
  shortcuts    = @(, @("Moonbow.exe", "Moonbow"))
  persist      = "Data"
  checkver     = "github"
  autoupdate   = [ordered]@{
    architecture = [ordered]@{
      "64bit" = [ordered]@{ url = "https://github.com/$repo/releases/download/v`$version/Moonbow-windows-x64.zip"; hash = [ordered]@{ url = "`$url.sha256" } }
    }
  }
}
Save (Join-Path $root "bucket\moonbow.json") (($scoop | ConvertTo-Json -Depth 8) + "`n")
Write-Host "winget: $winget"
Write-Host "Scoop:  $(Join-Path $root 'bucket\moonbow.json')"

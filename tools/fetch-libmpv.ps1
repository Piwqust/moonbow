# Downloads a libmpv dev build (shinchiro/mpv-winbuild-cmake) into third_party/mpv.
# Usage: powershell -ExecutionPolicy Bypass -File tools\fetch-libmpv.ps1 [-Tag 20261017]
param(
    [string]$Tag = "latest"
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root "third_party\mpv"
$api  = if ($Tag -eq "latest") {
    "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest"
} else {
    "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/tags/$Tag"
}

$release = Invoke-RestMethod $api
$asset = $release.assets | Where-Object { $_.name -like "mpv-dev-x86_64-2*.7z" } | Select-Object -First 1
if (-not $asset) { throw "No mpv-dev-x86_64 asset in release $($release.tag_name)" }

$work = Join-Path $root "third_party\_download"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force $work | Out-Null
$archive = Join-Path $work $asset.name

Write-Host "Downloading $($asset.name) ($([math]::Round($asset.size / 1MB, 1)) MB)"
Invoke-WebRequest $asset.browser_download_url -OutFile $archive

$extract = Join-Path $work "x"
New-Item -ItemType Directory -Force $extract | Out-Null
# Windows' own bsdtar reads .7z; a GNU tar earlier on PATH (Git for Windows, CI runners) does not.
$tar = Join-Path $env:SystemRoot "System32	ar.exe"
if (-not (Test-Path $tar)) { $tar = "tar" }
& $tar -xf $archive -C $extract
if ($LASTEXITCODE -ne 0) { throw "tar failed to extract $archive" }

if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item -Recurse (Join-Path $extract "include") $dest
Get-ChildItem $extract -Filter "libmpv*.dll" | Copy-Item -Destination $dest
Set-Content -Path (Join-Path $dest "VERSION.txt") -Value $asset.name

Remove-Item -Recurse -Force $work
Write-Host "libmpv ready in $dest"
Get-ChildItem $dest -Recurse | Select-Object -ExpandProperty FullName

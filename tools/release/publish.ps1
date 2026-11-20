# Publishes dist\release\v<version> (tools\package-release.ps1) as the GitHub release v<version>.
#
# The release is made as a draft, gets every file, and only then becomes the latest release: the
# installed Moonbows read releases/latest/download/moonbow-update.json and must never see a feed
# whose setup is not uploaded yet.
#
# Release notes: docs\releases\v<version>.md when it exists (written by hand, like v0.1.0), else the
# version's section of CHANGELOG.md with a short download table.
# Needs the gh command line (signed in, or GH_TOKEN in the release workflow).
# Usage: powershell -ExecutionPolicy Bypass -File tools\release\publish.ps1 [-Version 0.2.0] [-Repo owner/name]
param([string]$Version = "", [string]$Repo = "Piwqust/moonbow")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $Version) {
  $Version = [regex]::Match((Get-Content -Raw "$root\res\app.rc"), 'VALUE "ProductVersion", "([^"]+)"').Groups[1].Value
}
$tag = "v$Version"
$dir = Join-Path $root "dist\release\$tag"
$files = @(
  "Moonbow-windows-x64-setup.exe", "Moonbow-windows-x64-setup.exe.sha256",
  "Moonbow-windows-x64.zip", "Moonbow-windows-x64.zip.sha256",
  "moonbow-update.json", "Moonbow-windows-x64-symbols.zip"
) | ForEach-Object { Join-Path $dir $_ }
foreach ($f in $files) { if (-not (Test-Path $f)) { throw "Missing $f (run tools\package-release.ps1 with the signing key)" } }

# --- notes ---------------------------------------------------------------------------------------
$notesFile = Join-Path $root "docs\releases\$tag.md"
if (-not (Test-Path $notesFile)) {
  $changelog = Get-Content "$root\CHANGELOG.md" -Encoding UTF8
  $section = @()
  $inside = $false
  foreach ($line in $changelog) {
    if ($line -match '^## ') {
      if ($inside) { break }
      $inside = $line -match ('^## ' + [regex]::Escape($Version) + '(\s|$)')
      continue
    }
    if ($inside) { $section += $line }
  }
  if (-not $section) { throw "CHANGELOG.md has no '## $Version' section" }
  $body = @(
    "| File | What it is |",
    "| --- | --- |",
    "| **Moonbow-windows-x64-setup.exe** | **Installer, recommended.** Installs for your account, no administrator rights needed |",
    "| Moonbow-windows-x64.zip | Portable: unpack anywhere and run ``Moonbow.exe`` |",
    "",
    "Moonbow 0.2 and later update themselves: the new version downloads in the background and installs when you",
    "close Moonbow (**Settings > General > Updates**). From 0.1.0, run the installer once.",
    ""
  ) + $section + @(
    "",
    "[Website](https://moonbow.angleford.co) | [Changelog](https://github.com/$Repo/blob/$tag/CHANGELOG.md)"
  )
  $notesFile = Join-Path $dir "release-notes.md"
  [IO.File]::WriteAllText($notesFile, (($body -join "`n").Trim() + "`n"), (New-Object System.Text.UTF8Encoding $false))
}

# --- the release ---------------------------------------------------------------------------------
gh release view $tag --repo $Repo --json isDraft *> $null
if ($LASTEXITCODE -eq 0) {
  # A draft left by a failed run is reused; a published release is never touched again.
  $isDraft = (gh release view $tag --repo $Repo --json isDraft | ConvertFrom-Json).isDraft
  if (-not $isDraft) { throw "Release $tag is already published" }
  gh release upload $tag @files --repo $Repo --clobber
} else {
  gh release create $tag @files --repo $Repo --draft --verify-tag --title "Moonbow $Version" --notes-file $notesFile
}
if ($LASTEXITCODE) { throw "gh release create/upload failed" }
gh release edit $tag --repo $Repo --draft=false --latest
if ($LASTEXITCODE) { throw "gh release edit failed" }
Write-Host "Published https://github.com/$Repo/releases/tag/$tag"

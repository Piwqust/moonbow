# Starts a Moonbow release with one command. It sets the version in res\app.rc, commits, tags v<version>
# and pushes; GitHub Actions (.github/workflows/release.yml) then builds, tests, signs and publishes it,
# and the installed Moonbows find it within a day.
#
# Before running it, write what changed for the person using Moonbow:
#   CHANGELOG.md               a "## <version> (<date>)" section (also the GitHub release notes)
#   src\setup\ReleaseNotes.h   a few plain lines for "What's new" inside Moonbow
#   docs\releases\v<version>.md  optional: hand-written GitHub release notes instead of the changelog
# Those files may still be uncommitted: they go into the release commit. Anything else must be committed.
#
# Usage: powershell -ExecutionPolicy Bypass -File tools\release.ps1 0.2.0 [-DryRun]
param(
  [Parameter(Mandatory = $true, Position = 0)][string]$Version,
  [switch]$DryRun
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
function Invoke-Git { & git.exe -C $root @args; if ($LASTEXITCODE) { throw "git $args failed" } }

if ($Version -notmatch '^(\d+)\.(\d+)\.(\d+)$') { throw "The version is three numbers, like 0.2.0" }
$parts = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
$tag = "v$Version"
$rcPath = Join-Path $root "res\app.rc"
$rc = [IO.File]::ReadAllText($rcPath)
$current = [regex]::Match($rc, 'VALUE "ProductVersion", "([^"]+)"').Groups[1].Value
$cmp = [version]$Version -gt [version]$current
if (-not $cmp) { throw "$Version is not newer than the current $current" }

# --- the state of the repository ----------------------------------------------------------------
$branch = (git -C $root rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne "main") { throw "Release from main (now on $branch)" }
Invoke-Git fetch --quiet origin main --tags
$behind = (git -C $root rev-list --count "HEAD..origin/main").Trim()
if ($behind -ne "0") { throw "main is $behind commit(s) behind origin/main: pull first" }
if (git -C $root tag --list $tag) { throw "The tag $tag already exists" }
$allowed = @("CHANGELOG.md", "src/setup/ReleaseNotes.h")
$dirty = git -C $root status --porcelain | ForEach-Object { $_.Substring(3).Trim('"') } |
  Where-Object { $_ -notin $allowed -and $_ -notlike "docs/releases/*" }
if ($dirty) { throw "Commit these first: $($dirty -join ', ')" }

# --- what the release says -----------------------------------------------------------------------
$changelog = [IO.File]::ReadAllText((Join-Path $root "CHANGELOG.md"))
if ($changelog -notmatch ('(?m)^## ' + [regex]::Escape($Version) + '(\s|$)')) {
  throw "CHANGELOG.md has no '## $Version' section: write what changed first"
}
$notes = [IO.File]::ReadAllText((Join-Path $root "src\setup\ReleaseNotes.h"))
if (-not $notes.Contains("{L`"$Version`"")) {
  throw "src\setup\ReleaseNotes.h has no entry for $Version (the 'What's new' lines inside Moonbow)"
}

# --- the version ---------------------------------------------------------------------------------
$comma = "$($parts[0]),$($parts[1]),$($parts[2]),0"
$rc = [regex]::Replace($rc, '(?m)^(\s*FILEVERSION\s+)[\d,]+', "`${1}$comma")
$rc = [regex]::Replace($rc, '(?m)^(\s*PRODUCTVERSION\s+)[\d,]+', "`${1}$comma")
$rc = [regex]::Replace($rc, '(VALUE "FileVersion", ")[^"]+', "`${1}$Version")
$rc = [regex]::Replace($rc, '(VALUE "ProductVersion", ")[^"]+', "`${1}$Version")
# "## 0.2.0 (unreleased)" gets today's date.
$dated = [regex]::Replace($changelog, ('(?m)^(## ' + [regex]::Escape($Version) + ') \(unreleased\)'),
  "`${1} ($(Get-Date -Format 'yyyy-MM-dd'))")
if ($DryRun) {
  Write-Host "Dry run: would set $current -> $Version in res\app.rc, commit 'Moonbow $Version', tag $tag and push."
  exit 0
}
[IO.File]::WriteAllText($rcPath, $rc, (New-Object System.Text.UTF8Encoding $false))
if ($dated -ne $changelog) {
  [IO.File]::WriteAllText((Join-Path $root "CHANGELOG.md"), $dated, (New-Object System.Text.UTF8Encoding $false))
}

Invoke-Git add -- res/app.rc CHANGELOG.md src/setup/ReleaseNotes.h
if (Test-Path (Join-Path $root "docs\releases")) { Invoke-Git add -- docs/releases }
Invoke-Git commit --quiet -m "Moonbow $Version"
Invoke-Git tag -a $tag -m "Moonbow $Version"
Invoke-Git push --quiet origin main
Invoke-Git push --quiet origin $tag
Write-Host "Pushed $tag. GitHub Actions builds and publishes it:"
Write-Host "  https://github.com/Piwqust/moonbow/actions/workflows/release.yml"

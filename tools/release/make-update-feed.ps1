# Writes moonbow-update.json: what Moonbow reads to find, download and check a new version.
#
#   {"payload": "<the manifest as a JSON string>", "signature": "<base64>"}
#
# The payload is signed as it is, byte for byte (ECDSA P-256, SHA-256, r|s), so nothing has to be
# re-serialized the same way on both ends. Moonbow checks the signature against kUpdateKeys
# (src/setup/UpdateFeed.cpp), then the size and SHA-256 of the setup it downloads.
#
# The key: -KeyFile, else $env:MOONBOW_UPDATE_KEY (the release workflow's secret), else the file
# tools\release\new-update-key.ps1 wrote. -Mirror adds a second download address (own server).
# Usage: make-update-feed.ps1 -Version 0.2.0 -Setup <Moonbow-windows-x64-setup.exe> -Out <moonbow-update.json>
#                             [-Mirror https://moonbow.angleford.co/update] [-KeyFile <file>]
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [Parameter(Mandatory = $true)][string]$Setup,
  [Parameter(Mandatory = $true)][string]$Out,
  [string]$Mirror = "",
  [string]$KeyFile = "",
  [string]$Repo = "Piwqust/moonbow"
)
$ErrorActionPreference = "Stop"

$keyText = $null
if ($KeyFile) { $keyText = Get-Content -Raw $KeyFile }
elseif ($env:MOONBOW_UPDATE_KEY) { $keyText = $env:MOONBOW_UPDATE_KEY }
elseif (Test-Path "$env:USERPROFILE\.moonbow-release\update-signing-key.txt") {
  $keyText = Get-Content -Raw "$env:USERPROFILE\.moonbow-release\update-signing-key.txt"
}
if (-not $keyText) { throw "No signing key: pass -KeyFile, set MOONBOW_UPDATE_KEY or run tools\release\new-update-key.ps1" }

$file = Get-Item $Setup
$name = $file.Name
$tag = "v$Version"
$urls = @("https://github.com/$Repo/releases/download/$tag/$name")
if ($Mirror) { $urls += "$($Mirror.TrimEnd('/'))/$tag/$name" }

# Built by hand: the order of the fields stays fixed and nothing is escaped differently by
# Windows PowerShell 5.1 and PowerShell 7. Every value here is plain ASCII.
function Q([string]$s) {
  if ($s -notmatch '^[\x20-\x7E]*$' -or $s.Contains('"') -or $s.Contains('\')) { throw "Not a plain value: $s" }
  return '"' + $s + '"'
}
$hash = (Get-FileHash -Algorithm SHA256 $file.FullName).Hash.ToLowerInvariant()
$payload = '{' +
  '"product":"Moonbow",' +
  '"version":' + (Q $Version) + ',' +
  '"date":' + (Q (Get-Date -Format "yyyy-MM-dd")) + ',' +
  '"page":' + (Q "https://github.com/$Repo/releases/tag/$tag") + ',' +
  '"setup":{' +
    '"name":' + (Q $name) + ',' +
    '"size":' + $file.Length + ',' +
    '"sha256":' + (Q $hash) + ',' +
    '"urls":[' + (($urls | ForEach-Object { Q $_ }) -join ',') + ']' +
  '}}'

# Only the base64 characters: a BOM or a line break that came along with the secret is dropped.
$blob = [Convert]::FromBase64String(($keyText -replace '[^A-Za-z0-9+/=]', ''))
$cng = [System.Security.Cryptography.CngKey]::Import($blob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$ecdsa = New-Object System.Security.Cryptography.ECDsaCng($cng)
$data = [Text.Encoding]::UTF8.GetBytes($payload)
$sig = $ecdsa.SignData($data, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
if ($sig.Length -ne 64) { throw "Unexpected signature length $($sig.Length)" }
if (-not $ecdsa.VerifyData($data, $sig, [System.Security.Cryptography.HashAlgorithmName]::SHA256)) { throw "The signature does not verify" }
$ecdsa.Dispose()
$cng.Dispose()

# The payload has no quotes or backslashes of its own besides the field quotes: escape those.
$json = '{"payload":"' + $payload.Replace('"', '\"') + '","signature":"' + [Convert]::ToBase64String($sig) + '"}'
[IO.File]::WriteAllText($Out, $json + "`n", (New-Object System.Text.UTF8Encoding $false))
Write-Host "Update feed: $Out ($Version, $name, $hash)"

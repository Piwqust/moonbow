# Makes the key pair that signs Moonbow's update manifest (moonbow-update.json). Run it once.
#
# Moonbow installs an update only when the manifest is signed by a key listed in kUpdateKeys
# (src/setup/UpdateFeed.cpp), so a changed file on GitHub or on the mirror cannot install anything.
#   - The private key goes to -Out, outside the repository. Keep a copy somewhere safe (a password
#     manager): without it no new version can reach the Moonbows already installed.
#   - -SetSecret also stores it as the MOONBOW_UPDATE_KEY secret of the GitHub repository, for the
#     release workflow (.github/workflows/release.yml). Needs the gh command line, signed in.
#   - The public key is printed: put it into kUpdateKeys.
# Usage: powershell -ExecutionPolicy Bypass -File tools\release\new-update-key.ps1 [-Out <file>] [-SetSecret]
param(
  [string]$Out = "$env:USERPROFILE\.moonbow-release\update-signing-key.txt",
  [switch]$SetSecret
)
$ErrorActionPreference = "Stop"
if (Test-Path $Out) {
  throw "$Out exists. A new key locks out every Moonbow that trusts the old one; move the file away first if that is really meant."
}
$params = New-Object System.Security.Cryptography.CngKeyCreationParameters
$params.ExportPolicy = [System.Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
# [NullString]: a plain $null becomes "", which would store the key in the user's key store.
$key = [System.Security.Cryptography.CngKey]::Create([System.Security.Cryptography.CngAlgorithm]::ECDsaP256, [NullString]::Value, $params)
# BCRYPT_ECCKEY_BLOB: magic, key length (32), then X, Y and the private D.
$blob = $key.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$key.Dispose()
$public = ($blob[8..71] | ForEach-Object { $_.ToString("x2") }) -join ""
$private = [Convert]::ToBase64String($blob)

New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null
[IO.File]::WriteAllText($Out, $private + "`n", (New-Object System.Text.UTF8Encoding $false))
Write-Host "Private key: $Out (keep a copy somewhere safe, never commit it)"
if ($SetSecret) {
  $private | gh secret set MOONBOW_UPDATE_KEY --repo Piwqust/moonbow
  if ($LASTEXITCODE) { throw "gh secret set failed" }
  Write-Host "Stored as the MOONBOW_UPDATE_KEY secret of Piwqust/moonbow"
}
Write-Host "Public key for kUpdateKeys in src/setup/UpdateFeed.cpp:"
Write-Host "    `"$public`","

# Exports all icon variants from the Figma "Icon" component set into design/icons/*.svg
# through the local Claude Bridge plugin.
param([string]$Bridge = "C:\Users\wheny\Desktop\CODE\motion_storyboard\tools\figma-bridge\fig.js", [int]$Chunk = 13)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dir = Join-Path $root "design\icons"
New-Item -ItemType Directory -Force $dir | Out-Null
$tpl = Get-Content (Join-Path $PSScriptRoot "icons.js") -Raw
$count = [int]((node $Bridge eval "return (await figma.getNodeByIdAsync('42:356')).children.length" | Out-String | ConvertFrom-Json).value)
$seen = @{}; $ok = 0; $skipped = @()
for ($start = 0; $start -lt $count; $start += $Chunk) {
    $tmp = Join-Path $env:TEMP "lumen-icons-$start.js"
    [IO.File]::WriteAllText($tmp, $tpl.Replace("START", "$start").Replace("COUNT", "$Chunk"))
    $res = (node $Bridge run $tmp --timeout 90000 | Out-String) | ConvertFrom-Json
    Remove-Item $tmp
    if (-not $res.ok) { $skipped += "chunk $start ($($res.error))"; continue }
    foreach ($i in $res.value) {
        if ($i.error) { $skipped += "$($i.key) ($($i.error))"; continue }
        $k = $i.key
        if ($seen[$k]) { $seen[$k]++; $k = "$k-$($seen[$k])" } else { $seen[$k] = 1 }
        [IO.File]::WriteAllText((Join-Path $dir "$k.svg"), $i.svg)
        $ok++
    }
}
"exported $ok of $count"
if ($skipped) { "skipped: " + ($skipped -join "; ") }

# Exports Figma frames through the local "Claude Bridge" plugin (see
# motion_storyboard/tools/figma-bridge) into design/ref as PNG files.
# Usage: tools\figma\export-frames.ps1 -Ids "247:445","970:4028" [-Scale 1]
param(
    [Parameter(Mandatory = $true)][string[]]$Ids,
    [double]$Scale = 1,
    [string]$Bridge = "C:\Users\wheny\Desktop\CODE\motion_storyboard\tools\figma-bridge\fig.js"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root "design\ref"
New-Item -ItemType Directory -Force $out | Out-Null

$idList = ($Ids -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ } | ForEach-Object { "'$_'" }) -join ","
$job = @"
const ids = [$idList];
const out = [];
for (const id of ids) {
  const n = await figma.getNodeByIdAsync(id);
  if (!n) { out.push({ id, error: 'missing' }); continue; }
  const bytes = await n.exportAsync({ format: 'PNG', constraint: { type: 'SCALE', value: $Scale } });
  out.push({ id, frame: n.name, img: { __png: figma.base64Encode(bytes), name: n.name + '__' + id.replace(':', '-') } });
}
return out;
"@
$tmp = Join-Path $env:TEMP "moonbow-figma-job.js"
Set-Content -Path $tmp -Value $job -Encoding UTF8
$json = node $Bridge run $tmp --out $out --timeout 300000 | Out-String
Remove-Item $tmp
$res = $json | ConvertFrom-Json
if (-not $res.ok) { throw "bridge error: $($res.error)" }
$res.value | ForEach-Object { if ($_.img) { "$($_.id)`t$($_.frame)`t$(Split-Path -Leaf $_.img.png)" } else { "$($_.id)`tERROR $($_.error)" } }

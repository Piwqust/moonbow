# Subsets the Inter TTFs in res/fonts to Latin + Cyrillic + common symbols.
# Needs fonttools (pip install fonttools). Run once after replacing the fonts.
$ErrorActionPreference = "Stop"
$fonts = Join-Path (Split-Path -Parent $PSScriptRoot) "res\fonts"
$unicodes = "U+0020-007E,U+00A0-024F,U+0300-036F,U+0400-04FF,U+2000-206F,U+20AC,U+2116,U+2122,U+2190-21FF,U+2212,U+2215,U+221E,U+2248,U+2260,U+2264,U+2265,U+25B2-25BC,U+2713"
foreach ($f in Get-ChildItem $fonts -Filter "Inter-*.ttf") {
    $tmp = "$($f.FullName).sub"
    python -m fontTools.subset $f.FullName --unicodes=$unicodes --layout-features="kern,liga,calt,tnum,case,ss01,ss02,cv05,cv08" --output-file=$tmp --no-hinting --desubroutinize
    if ($LASTEXITCODE -ne 0) { throw "subset failed for $($f.Name)" }
    Move-Item -Force $tmp $f.FullName
}
Get-ChildItem $fonts -Filter "Inter-*.ttf" | Select-Object Name, Length

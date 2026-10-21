# Scans a make-bad-media.py folder in a hidden test instance, then opens every file in it and records
# what happened: ok, crash (the process ended; its report is in <Out>\crashes) or hang (no answer).
# Usage: powershell -File qa\bad-media\run-bad-media.ps1 -Exe <Lumen.exe> -Dir <corpus> -Out <folder in %TEMP%>
#        [-Filter <regex on the relative path>]
# Writes <Out>\results.csv, a capture per file (<Out>\shots) and prints a summary. Uses only
# --automation instances (tools\dev\launch.ps1) and kills only their PIDs.
param([Parameter(Mandatory = $true)][string]$Exe, [Parameter(Mandatory = $true)][string]$Dir,
      [Parameter(Mandatory = $true)][string]$Out, [string]$Filter = "")
$ErrorActionPreference = "Stop"
$dev = Join-Path $PSScriptRoot "..\..\tools\dev"
New-Item -ItemType Directory -Force "$Out\crashes", "$Out\shots" | Out-Null
$env:LUMEN_CRASH_DIR = (Resolve-Path "$Out\crashes").Path
$dataDir = Join-Path $Out "data"
$script:starts = 0

function Command([int]$id, [string]$cmd, [int]$timeoutMs = 20000) {
  try { $r = & "$dev\command.ps1" -ProcessId $id -Command $cmd -TimeoutMs $timeoutMs 2>&1 | Out-String }
  catch { $r = "error: $($_.Exception.Message)" }
  return $r.Trim()
}

function Start-Lumen([switch]$Scan) {
  $lib = if ($Scan) { $Dir } else { Join-Path $Out "empty-lib" }
  New-Item -ItemType Directory -Force (Join-Path $Out "empty-lib") | Out-Null
  # A folder per start: a killed instance's files can stay locked for a moment (antivirus scans).
  $script:starts++
  $id = [int](& "$dev\launch.ps1" -DataDir "$dataDir-$($script:starts)" -Fresh -Library $lib -Exe $Exe)
  # The window comes a moment after the process: commands before it fail.
  $until = (Get-Date).AddSeconds(30)
  while ((Get-Date) -lt $until -and (Command $id "state" 2000) -notmatch "^ok") { Start-Sleep -Milliseconds 200 }
  $id
}

function Stop-Lumen([int]$id) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue; Wait-Process -Id $id -Timeout 15 -ErrorAction SilentlyContinue }
function Alive([int]$id) { [bool](Get-Process -Id $id -ErrorAction SilentlyContinue) }

# Files without following junctions (the corpus has one that loops) and with paths over 260 characters.
function Get-Files([string]$root) {
  $stack = New-Object System.Collections.Stack
  $stack.Push((Get-Item -LiteralPath "\\?\$root"))
  while ($stack.Count) {
    foreach ($e in (Get-ChildItem -LiteralPath $stack.Pop().FullName -Force)) {
      if ($e.PSIsContainer) { if (-not ($e.Attributes -band [IO.FileAttributes]::ReparsePoint)) { $stack.Push($e) } }
      else { $e.FullName.Substring(4) }
    }
  }
}

$results = New-Object System.Collections.ArrayList
function Add-Result($file, $phase, $outcome, $ms, $detail) {
  $rel = if ($file) { $file.Substring($Dir.Length).TrimStart('\') } else { "(library scan)" }
  [void]$results.Add([pscustomobject]@{ file = $rel; phase = $phase; outcome = $outcome; ms = $ms; detail = $detail })
  Write-Host ("{0,-6} {1,6} ms  {2}  {3}" -f $outcome, $ms, $rel, $detail)
}
function New-Crashes { @(Get-ChildItem "$Out\crashes" -Filter "crash-*.txt" | Where-Object { -not $seen.ContainsKey($_.Name) }) }
$seen = @{}

# 1. The library scan of the whole folder.
$id = Start-Lumen -Scan
$t = [Diagnostics.Stopwatch]::StartNew()
$r = Command $id "wait-idle" 180000
if (-not (Alive $id)) { Add-Result $null "scan" "CRASH" $t.ElapsedMilliseconds ((New-Crashes | ForEach-Object { $seen[$_.Name] = 1; $_.Name }) -join ' ') }
elseif ($r -notmatch "idle") { Add-Result $null "scan" "HANG" $t.ElapsedMilliseconds $r }
else { Add-Result $null "scan" "ok" $t.ElapsedMilliseconds "" }
Stop-Lumen $id

# 2. Every file, opened like a double-click in Explorer.
$files = @(Get-Files $Dir | Where-Object { -not $Filter -or $_.Substring($Dir.Length) -match $Filter })
$id = Start-Lumen
$n = 0
foreach ($f in $files) {
  $n++
  $t = [Diagnostics.Stopwatch]::StartNew()
  $open = Command $id "open $f"
  $idle = if (Alive $id) { Command $id "wait-idle" 20000 } else { "" }
  $ms = $t.ElapsedMilliseconds
  if (-not (Alive $id)) {
    Add-Result $f "open" "CRASH" $ms ((New-Crashes | ForEach-Object { $seen[$_.Name] = 1; $_.Name }) -join ' ')
    $id = Start-Lumen
    continue
  }
  $state = Command $id "state" 5000
  if ($idle -notmatch "idle") {
    $outcome = if ($state -match "^ok") { "BUSY" } else { "HANG" }  # BUSY: answers but never settles
    Add-Result $f "open" $outcome $ms (($idle -split "`n")[0])
    if ($outcome -eq "HANG") { Stop-Lumen $id; $id = Start-Lumen }
    continue
  }
  & "$dev\capture.ps1" -ProcessId $id -Out ("$Out\shots\{0:D3}.png" -f $n) -Scale 0.5 | Out-Null
  $loaded = if ($state -match "file=(\d)") { $Matches[1] } else { "?" }
  Add-Result $f "open" "ok" $ms "file=$loaded queue=$(if ($state -match 'queue=(\d+)') { $Matches[1] }) $(($open -split "`n")[0])"
}
Stop-Lumen $id

$results | Export-Csv -NoTypeInformation -Encoding UTF8 "$Out\results.csv"
$bad = @($results | Where-Object { $_.outcome -ne "ok" })
Write-Host ""
Write-Host ("{0} checks: {1} ok, {2} crash, {3} hang, {4} busy. Shots: $Out\shots" -f $results.Count,
  @($results | Where-Object outcome -eq "ok").Count, @($bad | Where-Object outcome -eq "CRASH").Count,
  @($bad | Where-Object outcome -eq "HANG").Count, @($bad | Where-Object outcome -eq "BUSY").Count)

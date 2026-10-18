# Builds a large synthetic library of hard links (no extra disk space) in %TEMP%\lumen-qa\big.
$src = "$env:TEMP\lumen-qa\media"
$dst = "$env:TEMP\lumen-qa\big"
if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
$vids = @("$src\Videos\Series S01\Series S01E01.mp4", "$src\Videos\Vertical phone clip.mp4", "$src\Videos\Chaptered Demo (1080p).mkv")
$songs = Get-ChildItem "$src\Music" -Recurse -File | ForEach-Object FullName
$n = 0
for ($s = 1; $s -le 60; $s++) {
  $dir = New-Item -ItemType Directory -Force ("$dst\Videos\Show {0:D2}\Season 1" -f $s)
  for ($e = 1; $e -le 25; $e++) {
    $v = $vids[($s + $e) % $vids.Count]
    New-Item -ItemType HardLink -Path ("{0}\Show {1:D2} S01E{2:D2}{3}" -f $dir, $s, $e, [IO.Path]::GetExtension($v)) -Target $v | Out-Null; $n++
  }
}
for ($a = 1; $a -le 100; $a++) {
  $dir = New-Item -ItemType Directory -Force ("$dst\Music\Artist {0:D3}\Album {0:D3}" -f $a)
  for ($t = 1; $t -le 20; $t++) {
    $f = $songs[($a + $t) % $songs.Count]
    New-Item -ItemType HardLink -Path ("{0}\{1:D2} Song {2}-{1}{3}" -f $dir, $t, $a, [IO.Path]::GetExtension($f)) -Target $f | Out-Null; $n++
  }
}
"created $n links in $dst"

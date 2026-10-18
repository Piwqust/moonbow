# Builds the worst-case media set of qa/round5/METHOD.md, layer 3: the data that breaks "nice demo"
# layouts. Everything is generated with ffmpeg (nothing is downloaded); the total stays well under
# 300 MB. Idempotent: files that already exist are kept, so a second run only fills gaps. -Force
# rebuilds everything.
# Usage: powershell -File qa\make-worstcase.ps1 [-Out %TEMP%\lumen-qa\worst] [-Force]
#
# Layout (library roots: Music, Videos, Big, Broken, or the whole folder):
#   Music\    long, diacritic, CJK, RTL, emoji and unbreakable names in tags and file names; no tags;
#             identical titles in different folders; names that differ only at the end; covers
#             (huge, tiny, very wide, missing); 1-second and 10-hour tracks; albums of 1/2/5/11/21 songs
#   Videos\   21:9, 9:16, 4:3, 1:1, 160x90; 1 second and 10 hours; folders of 1/2/5/11/21 videos;
#             12 nested folders; identical file names in different folders; long names
#   Big\      one folder with 1284 tiny untagged tracks (1000+ items, the "1284 файла" plural)
#   Broken\   a 0-byte file, a text file named .mp4, an audio file named .mkv
[CmdletBinding()]
param([string]$Out = (Join-Path $env:TEMP "lumen-qa\worst"), [switch]$Force)
$ErrorActionPreference = "Stop"
$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { throw "ffmpeg is not on PATH (where ffmpeg)" }
# Bump when the content changes: an older set is rebuilt instead of being topped up.
$version = "2"
# A rebuild deletes -Out: only ever a folder inside %TEMP% that this script made (it has the stamp).
$tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
$Out = [IO.Path]::GetFullPath($Out).TrimEnd('\')
if (-not $Out.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "-Out must be a folder inside %TEMP%: $Out" }
$stamp = Join-Path $Out ".worstcase-version"
if ((Test-Path $Out) -and ($Force -or -not (Test-Path $stamp) -or (Get-Content -Raw $stamp).Trim() -ne $version)) {
  if (Test-Path $stamp) { Remove-Item -Recurse -Force $Out }
  elseif (Get-ChildItem -Force $Out | Select-Object -First 1) { throw "$Out is not a worst-case set (no .worstcase-version): not deleting it" }
}
New-Item -ItemType Directory -Force $Out | Out-Null
$tmp = Join-Path (Split-Path -Parent $Out) ((Split-Path -Leaf $Out) + "-base")  # outside the library roots
New-Item -ItemType Directory -Force $tmp | Out-Null
[IO.File]::WriteAllText($stamp, $version)
$script:made = 0

function SubDir([string]$rel) { $p = Join-Path $Out $rel; New-Item -ItemType Directory -Force $p | Out-Null; $p }
function Done([string]$path) { (Test-Path -LiteralPath $path) -and ((Get-Item -LiteralPath $path).Length -gt 0) }
function FF([string[]]$a) {
  & $ffmpeg -hide_banner -loglevel error -nostdin -y @a
  if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed: $($a -join ' ')" }
  $script:made++
}
# Writes to a temporary ASCII name first: a half-written file never looks finished to the next run.
function Make([string]$path, [scriptblock]$build) {
  if (Done $path) { return }
  $part = Join-Path $tmp ("part-" + [guid]::NewGuid().ToString("N") + [IO.Path]::GetExtension($path))
  & $build $part
  if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
  [IO.File]::Move($part, $path)
}
# File names cannot hold : ? * " < > | / \ (the tags keep them).
function Safe([string]$name) { ($name -replace '[:?*"<>|/\\]', '').Trim() }
function Tags([hashtable]$t) {
  $a = @()
  foreach ($k in $t.Keys) { $a += "-metadata"; $a += "$k=$($t[$k])" }
  $a
}

# --- base clips (ASCII names, reused by copy) --------------------------------------------------
$tone1 = Join-Path $tmp "tone-1s.mp3"
Make $tone1 { param($o) FF @("-f", "lavfi", "-i", "sine=frequency=440:duration=1", "-ac", "1", "-ar", "22050", "-b:a", "32k", "-map_metadata", "-1", $o) }
$tone4 = Join-Path $tmp "tone-4s.mp3"
Make $tone4 { param($o) FF @("-f", "lavfi", "-i", "sine=frequency=330:duration=4", "-ac", "1", "-ar", "22050", "-b:a", "32k", "-map_metadata", "-1", $o) }
# 20 s: long enough that a scenario still shows the file it opened (2 s clips had ended and moved on).
$clip = Join-Path $tmp "clip-320x180-20s.mp4"
Make $clip { param($o) FF @("-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24:duration=20", "-f", "lavfi", "-i", "sine=frequency=550:duration=20",
  "-c:v", "libx264", "-preset", "ultrafast", "-crf", "35", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "32k", "-shortest", $o) }

function Song([string]$path, [hashtable]$tags, [string]$cover = "", [string]$src = $tone4) {
  Make $path {
    param($o)
    $a = @("-i", $src)
    if ($cover) { $a += @("-i", $cover, "-map", "0:a", "-map", "1:v", "-c:a", "copy", "-c:v", "copy", "-disposition:v", "attached_pic",
                          "-metadata:s:v", "title=Album cover", "-metadata:s:v", "comment=Cover (front)") }
    else { $a += @("-map", "0:a", "-c", "copy") }
    $a += @("-map_metadata", "-1", "-id3v2_version", "3") + (Tags $tags) + @($o)
    FF $a
  }
}
function Video([string]$path, [int]$w, [int]$h, [double]$sec = 20, [hashtable]$tags = @{}) {
  Make $path {
    param($o)
    FF (@("-f", "lavfi", "-i", "testsrc2=size=${w}x${h}:rate=24:duration=$sec", "-f", "lavfi", "-i", "sine=frequency=500:duration=$sec",
          "-c:v", "libx264", "-preset", "ultrafast", "-crf", "35", "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "32k", "-shortest") + (Tags $tags) + @($o))
  }
}
function CopyAs([string]$src, [string]$dst) { if (-not (Done $dst)) { Copy-Item -LiteralPath $src -Destination $dst -Force; $script:made++ } }

# --- covers --------------------------------------------------------------------------------------
$coverHuge = Join-Path $tmp "cover-6000.jpg"
Make $coverHuge { param($o) FF @("-f", "lavfi", "-i", "testsrc2=size=6000x6000:duration=1", "-frames:v", "1", "-q:v", "6", $o) }
$coverTiny = Join-Path $tmp "cover-16.png"
Make $coverTiny { param($o) FF @("-f", "lavfi", "-i", "testsrc2=size=16x16:duration=1", "-frames:v", "1", $o) }
$coverWide = Join-Path $tmp "cover-3000x300.jpg"
Make $coverWide { param($o) FF @("-f", "lavfi", "-i", "testsrc2=size=3000x300:duration=1", "-frames:v", "1", "-q:v", "5", $o) }
$coverNormal = Join-Path $tmp "cover-500.jpg"
Make $coverNormal { param($o) FF @("-f", "lavfi", "-i", "mandelbrot=size=500x500", "-frames:v", "1", "-q:v", "4", $o) }

# --- music: names ----------------------------------------------------------------------------------
$longArtist = "Die Ärzte & Sigur Rós feat. Ólafur Arnalds, Mötley Crüe and the Orchestre Philharmonique de Radio France"
$longAlbum = "Live at the Royal Albert Hall: The Complete Unabridged Anniversary Edition (Remastered Deluxe Box Set, Disc 1 of 3)"
$longTitle = "Привет, мир — очень длинное название песни с диакритикой: Ça va? Ünïcödé Çàñ Bé Lóñg Ëñöügh To Wrap Twice"
$d = SubDir "Music\Long names"
Song (Join-Path $d "01 $(Safe $longTitle).mp3") @{ title = $longTitle; artist = $longArtist; album = $longAlbum; track = "1"; date = "2024"; genre = "Post-Rock / Neo-Classical / Ambient Électronique" } $coverNormal
Song (Join-Path $d "02 Short.mp3") @{ title = "Short"; artist = $longArtist; album = $longAlbum; track = "2" } $coverNormal

$d = SubDir "Music\日本語のアーティスト"
Song (Join-Path $d "01 夜に駆ける.mp3") @{ title = "夜に駆ける（ロングバージョン・リマスター）"; artist = "YOASOBI と 東京フィルハーモニー交響楽団"; album = "THE BOOK 2 ～完全生産限定盤～"; track = "1" }
Song (Join-Path $d "02 中文歌名.mp3") @{ title = "月亮代表我的心（现场版）"; artist = "邓丽君"; album = "永恒的邓丽君 精选集"; track = "2" }
Song (Join-Path $d "03 한국어.mp3") @{ title = "봄날 (Spring Day) — 한국어 제목"; artist = "방탄소년단"; album = "YOU NEVER WALK ALONE"; track = "3" }

$d = SubDir "Music\עברית ועربية"
Song (Join-Path $d "01 שיר בעברית.mp3") @{ title = "שיר ארוך מאוד בעברית עם מילים רבות"; artist = "להקה ישראלית"; album = "אלבום הבכורה"; track = "1" }
Song (Join-Path $d "02 أغنية عربية.mp3") @{ title = "أغنية عربية طويلة جداً مع كلمات كثيرة"; artist = "فرقة عربية"; album = "الألبوم الأول"; track = "2" }
Song (Join-Path $d "03 Mixed English עברית.mp3") @{ title = "Mixed English and עברית in one title"; artist = "Bidi Artist ערבית"; album = "Bidi"; track = "3" }

$d = SubDir "Music\🎵 Emoji"
Song (Join-Path $d "🔥 Fire at the start.mp3") @{ title = "🔥 Fire at the start"; artist = "😀 Smiley Band"; album = "🎉 Party 🎉"; track = "1" }
Song (Join-Path $d "Ends with emoji 👍🏽.mp3") @{ title = "Ends with a skin-tone emoji 👍🏽"; artist = "👨‍👩‍👧‍👦 Family ZWJ"; album = "🎉 Party 🎉"; track = "2" }

$d = SubDir "Music\Unbreakable"
$noSpaces = "Supercalifragilisticexpialidocious_ThisFileNameHasNoSpacesAtAllAndKeepsGoing_0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ"
Song (Join-Path $d "$noSpaces.mp3") @{ title = $noSpaces; artist = "NoSpacesArtistNameThatNeverBreaksAnywhere"; album = "NoSpacesAlbum_" + ("X" * 60); track = "1" }

$d = SubDir "Music\Untagged"
CopyAs $tone4 (Join-Path $d "no tags at all, the title is the file name.mp3")
CopyAs $tone1 (Join-Path $d "x.mp3")

foreach ($side in "Same title A", "Same title B", "Same title C") {
  $d = SubDir "Music\$side"
  Song (Join-Path $d "01 Intro.mp3") @{ title = "Intro"; artist = "Various"; album = "Intro"; track = "1" }
}
$d = SubDir "Music\Similar"
foreach ($n in 1..3) {
  Song (Join-Path $d "A name that only differs at the very end of a long title, version $n.mp3") @{
    title = "A name that only differs at the very end of a long title, version $n"; artist = "Similar"; album = "Similar"; track = "$n" }
}

foreach ($c in @(@("Huge cover", $coverHuge), @("Tiny cover", $coverTiny), @("Wide cover", $coverWide), @("No cover", ""))) {
  $d = SubDir "Music\Covers\$($c[0])"
  Song (Join-Path $d "01 $($c[0]).mp3") @{ title = "$($c[0]) song"; artist = "Cover Tester"; album = "$($c[0])"; track = "1" } $c[1]
}

$d = SubDir "Music\Durations"
Song (Join-Path $d "One second.mp3") @{ title = "One second"; artist = "Durations"; album = "Durations"; track = "1" } "" $tone1
$tenHours = Join-Path $d "Ten hours.mp3"
Make $tenHours { param($o) FF @("-f", "lavfi", "-i", "anullsrc=r=8000:cl=mono", "-t", "36000", "-c:a", "libmp3lame", "-b:a", "8k",
  "-id3v2_version", "3", "-metadata", "title=Ten hours of silence", "-metadata", "artist=Durations", "-metadata", "album=Durations", "-metadata", "track=2", $o) }

# Albums with 1, 2, 5, 11 and 21 songs, and an artist with 5 albums: every Russian plural form.
foreach ($n in 1, 2, 5, 11, 21) {
  $d = SubDir "Music\Plurals\Album of $n"
  foreach ($i in 1..$n) {
    Song (Join-Path $d ("{0:D2} Song {0}.mp3" -f $i)) @{ title = "Song $i of $n"; artist = "Plural Artist $n"; album = "Album of $n"; track = "$i" } "" $tone1
  }
}
foreach ($n in 1..5) {
  $d = SubDir "Music\Plurals\Five albums $n"
  Song (Join-Path $d "01 One.mp3") @{ title = "One"; artist = "Artist with five albums"; album = "Album number $n"; track = "1" } "" $tone1
}

# Synced lyrics with lines that must wrap: long Russian, CJK, unbreakable, emoji, an empty line.
$tone60 = Join-Path $tmp "tone-60s.mp3"
Make $tone60 { param($o) FF @("-f", "lavfi", "-i", "sine=frequency=262:duration=60", "-ac", "1", "-ar", "22050", "-b:a", "32k", "-map_metadata", "-1", $o) }
$d = SubDir "Music\Lyrics"
Song (Join-Path $d "01 Long lyric lines.mp3") @{ title = "Long lyric lines"; artist = "Lyrics Tester"; album = "Lyrics"; track = "1" } "" $tone60
$lrc = Join-Path $d "01 Long lyric lines.lrc"
if (-not (Done $lrc)) {
  $lines = @(
    "[ti:Long lyric lines]", "[ar:Lyrics Tester]",
    "[00:00.50]Первая строка песни, достаточно длинная, чтобы перенестись на вторую строку даже в широком окне плеера",
    "[00:03.00]夜に駆ける 沈むように溶けてゆくように 二人だけの空が広がる夜に",
    "[00:06.00]Unbreakable_line_without_any_spaces_ThatKeepsGoingAndGoingAndGoing_0123456789_ABCDEFGHIJKLMNOP",
    "[00:09.00]",
    "[00:10.00]🎵 Emoji at the start and the end 🎶",
    "[00:13.00]Short",
    "[00:16.00]שורה בעברית באמצע השיר",
    "[00:20.00]The last line, shown for the rest of the minute")
  [IO.File]::WriteAllText($lrc, ($lines -join "`r`n") + "`r`n", (New-Object Text.UTF8Encoding $false)); $script:made++
}

# --- videos ---------------------------------------------------------------------------------------
$d = SubDir "Videos\Aspect"
Video (Join-Path $d "Cinema 21x9 (840x360).mp4") 840 360
Video (Join-Path $d "Phone 9x16 (360x640).mp4") 360 640
Video (Join-Path $d "Classic 4x3 (640x480).mp4") 640 480
Video (Join-Path $d "Square 1x1 (480x480).mp4") 480 480
Video (Join-Path $d "Tiny 160x90.mp4") 160 90

$d = SubDir "Videos\Durations"
Video (Join-Path $d "One second.mp4") 320 180 1
$tenHoursVideo = Join-Path $d "Ten hours.mp4"
Make $tenHoursVideo { param($o) FF @("-f", "lavfi", "-i", "color=c=0x303030:size=160x90:rate=1", "-t", "36000", "-c:v", "libx264", "-preset", "ultrafast",
  "-crf", "40", "-g", "600", "-pix_fmt", "yuv420p", "-metadata", "title=Ten hours", $o) }

foreach ($n in 1, 2, 5, 11, 21) {
  $d = SubDir "Videos\Plurals\Folder of $n"
  foreach ($i in 1..$n) { CopyAs $clip (Join-Path $d ("Episode {0:D2}.mp4" -f $i)) }
}

$rel = "Videos\Nested"
foreach ($i in 1..12) { $rel += "\Level $i" }
$d = SubDir $rel
CopyAs $clip (Join-Path $d "Twelve levels deep.mp4")

foreach ($side in "Same name A", "Same name B") {
  $d = SubDir "Videos\$side"
  CopyAs $clip (Join-Path $d "Clip.mp4")
}
$d = SubDir "Videos\Long names"
CopyAs $clip (Join-Path $d "$(Safe $longTitle).mp4")
CopyAs $clip (Join-Path $d "$noSpaces.mp4")
CopyAs $clip (Join-Path $d "The.Series.Name.S01E01.A.Very.Long.Episode.Title.That.Goes.On.And.On.1080p.WEB-DL.DDP5.1.H.264-GROUP.mp4")
CopyAs $clip (Join-Path $d "🎬 Movie night — Ночь кино 映画の夜.mp4")
$d = SubDir "Videos\Long names\Series S01"
foreach ($i in 1..3) { CopyAs $clip (Join-Path $d ("Series S01E{0:D2}.mp4" -f $i)) }

# --- 1000+ items ----------------------------------------------------------------------------------
$d = SubDir "Big\1284 tracks"
$have = @{}; Get-ChildItem -LiteralPath $d -File | ForEach-Object { $have[$_.Name] = $_.Length }
$bytes = [IO.File]::ReadAllBytes($tone1)
foreach ($i in 1..1284) {
  $name = "Track {0:D4}.mp3" -f $i
  if (-not $have[$name]) { [IO.File]::WriteAllBytes((Join-Path $d $name), $bytes); $script:made++ }
}

# --- broken -----------------------------------------------------------------------------------
$d = SubDir "Broken"
$empty = Join-Path $d "Empty 0 bytes.mp4"
if (-not (Test-Path -LiteralPath $empty)) { [IO.File]::WriteAllBytes($empty, [byte[]]@()); $script:made++ }
$text = Join-Path $d "Text file named like a video.mp4"
if (-not (Done $text)) { [IO.File]::WriteAllText($text, "This is not a video, only text with a video extension.`r`n"); $script:made++ }
CopyAs $tone4 (Join-Path $d "Audio with a video extension.mkv")

$size = (Get-ChildItem -LiteralPath $Out -Recurse -File | Measure-Object Length -Sum)
"{0}: {1} files, {2:N1} MB ({3} made now)" -f $Out, $size.Count, ($size.Sum / 1MB), $script:made

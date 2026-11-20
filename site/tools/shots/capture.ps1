# Screenshots for the site: real Moonbow windows (build\Moonbow.exe) over the demo library from
# make-media.mjs, 1280x800 at 200 %. Hidden --automation instances only: nothing shows on screen.
#   powershell -File site\tools\shots\capture.ps1 [-Only hero,series]
# Writes PNGs to site\src\assets\shots\. Look at every one before using it.
# -Language en takes the English interface (README.md); -OutDir puts the PNGs elsewhere.
param([string]$Only = "", [string]$Language = "ru", [string]$OutDir = "")
$OnlyList = @($Only -split "," | Where-Object { $_ })
. "$PSScriptRoot\session.ps1"
$script:Lang = $Language
if ($OutDir) { $ShotsOut = $OutDir }
$want = { param($n) $OnlyList.Count -eq 0 -or $OnlyList -contains $n }
$hero = Join-Path $Media "Видео\Сериалы\Дом у моря\Дом у моря S01E04.mp4"
$song = Join-Path $Media "Музыка\Lunar Drift\Glass Hours\01 Glass Hours.mp3"

$id = New-Moonbow "dark"
try {
  Idle $id 90000
  # History, so the home page has "Продолжить": watch parts of a few files.
  foreach ($p in @(@("Видео\Фильмы\Высокая вода (2022).mp4", "playback.jump_60"), @("Видео\Фильмы\Последний трамвай (2019).mp4", "playback.jump_20"),
                   @("Видео\Сериалы\Северный ветер\Северный ветер S02E01.mp4", "playback.jump_40"), @("Видео\Сериалы\Дом у моря\Дом у моря S01E04.mp4", "playback.jump_30"))) {
    Cmd $id "open $(Join-Path $Media $p[0])"; Idle $id; Start-Sleep -Milliseconds 600
    Cmd $id "action $($p[1])"; Idle $id; Start-Sleep -Milliseconds 800
  }
  Cmd $id "action media.close"; Idle $id
  if (& $want "library-home") { Cmd $id "nav home"; Idle $id; Start-Sleep -Milliseconds 800; Shot $id "library-home" }
  if (& $want "series") { Cmd $id "nav videos show:дом у моря"; Idle $id; Start-Sleep -Milliseconds 800; Shot $id "series" }
  if (& $want "album") { Cmd $id "nav music album:Glass Hours"; Idle $id; Start-Sleep -Milliseconds 800; Shot $id "album" }

  # The player: the hero episode at 30 %, paused, Russian subtitles on.
  Cmd $id "open $hero"; Idle $id; Start-Sleep -Milliseconds 800
  Cmd $id "action playback.jump_30"; Idle $id
  Cmd $id "action playback.play_pause"; Idle $id; Start-Sleep -Milliseconds 700
  Set-Pointer $id 1300 700; Start-Sleep -Milliseconds 3000  # the time bubble of the jump fades out
  if (& $want "hero") { Shot $id "hero" }
  # Each player shot on its own scene of the episode, so the site doesn't repeat one still.
  # Scenes: 0-10 min lighthouse, 10-19 storm, 19-28 calm sea, 28-37 ships, 37-46 sunset.
  function Scene([string]$Jump) { Cmd $id "action playback.$Jump"; Idle $id; Set-Pointer $id 1300 700; Start-Sleep -Milliseconds 3000 }
  if (& $want "player-seek") { Scene "jump_10"; Set-Pointer $id 1500 1575; Start-Sleep -Milliseconds 300; Set-Pointer $id 1520 1575; Start-Sleep -Milliseconds 1200; Shot $id "player-seek" }
  if (& $want "player-stats") { Scene "jump_50"; Cmd $id "action video.statistics"; Start-Sleep -Milliseconds 900; Shot $id "player-stats"; Cmd $id "action video.statistics" }
  if (& $want "palette") { Scene "jump_70"; Cmd $id "action app.commands"; Start-Sleep -Milliseconds 700; Shot $id "palette"; Key $id "Escape"; Key $id "Escape"; Cmd $id "menu close"; Start-Sleep -Milliseconds 400 }
  if (& $want "subtitles") { Scene "jump_90"; Cmd $id "panel subtitles"; Idle $id; Start-Sleep -Milliseconds 700; Shot $id "subtitles"; Cmd $id "panel close" }

  # Music with synced lyrics.
  if (& $want "music-lyrics") {
    Cmd $id "open $song"; Idle $id; Start-Sleep -Milliseconds 1200
    Cmd $id "action playback.jump_30"; Idle $id; Start-Sleep -Milliseconds 1500
    Set-Pointer $id 1300 600; Start-Sleep -Milliseconds 400
    Shot $id "music-lyrics"
  }
  if (& $want "meters") {
    Cmd $id "open $song"; Idle $id; Start-Sleep -Milliseconds 800
    Cmd $id "action app.audio_meters"; Start-Sleep -Milliseconds 2500
    Shot $id "meters" $(if ($Language -eq "en") { "Audio meters" } else { "Индикаторы" })
    Cmd $id "action app.audio_meters"
  }
  if (& $want "settings") { Cmd $id "nav settings subtitles"; Idle $id; Start-Sleep -Milliseconds 900; Shot $id "settings" $(if ($Language -eq "en") { "Settings" } else { "Настройки" }) }
} finally { Stop-Moonbow $id }

# The light theme: its own instance, so no other window is open.
if (& $want "home-light") {
  $id = New-Moonbow "light" @{ "general.theme" = "light" }
  try {
    Idle $id 90000
    Cmd $id "open $(Join-Path $Media "Видео\Фильмы\Полярная ночь (2020).mp4")"; Idle $id; Start-Sleep -Milliseconds 600
    Cmd $id "action playback.jump_40"; Idle $id; Start-Sleep -Milliseconds 600
    Cmd $id "action media.close"; Idle $id
    Cmd $id "nav home"; Idle $id; Start-Sleep -Milliseconds 900; Shot $id "home-light"
  } finally { Stop-Moonbow $id }
}

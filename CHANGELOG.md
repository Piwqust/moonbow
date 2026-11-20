# Changelog

What changes for the person using Moonbow, newest first. The release notes on GitHub are taken from here.
The day-by-day development journal (in Russian) is in [`changelog/`](changelog/README.md).

## 0.1.0 beta (2026-11-20)

The first public beta: videos and music in one fast, quiet player for Windows 10 and 11.

### Player
- A double-clicked video plays about a third of a second after the click: the window and the player are drawn
  before the video engine is ready, and the picture fades in from black in 200 ms instead of flashing.
- Seek bar with frame previews and chapters, exact clicks and a hand pointer; A-B loop; queue with gapless
  playback; bookmarks; audio, video and subtitle tracks, external files.
- Short panels at their own buttons for speed, picture quality, equalizer, delays and subtitle style.
- <kbd>Ctrl</kbd>+<kbd>K</kbd> runs any command by name; the right-click menu is short, with a quick row on top.
- "Next episode" over the credits, skip intro and credits by chapters.
- Mouse gestures on the video, each of which can be turned off; dragging the video moves the window.
- Fullscreen with auto-hide, picture in picture, mini player, statistics over the video.

### Video and sound
- Crop, sharpening and deinterlacing on the GPU without copying frames to memory; quality presets and scalers;
  NVIDIA RTX and Intel AI upscaling, RTX Video HDR, debanding, HDR tone mapping, motion smoothing, own shaders.
- Passthrough to a receiver, WASAPI exclusive mode, Replay Gain and loudness normalization, volume up to 200 %.
- Audio meters in their own window: VU meters, EBU R128 loudness, stereo (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>M</kbd>).

### Subtitles and music
- Subtitles in the player's own font with ready styles, chosen on a real frame; a soft shadow by default; second
  subtitles on top; OpenSubtitles search with your own key; click a word on pause to copy, look up or translate it.
- Music screen with cover, visualizers and synced lyrics (`.lrc`, tags, or LRCLIB if you allow it); the song's
  waveform on the seek bar (can be turned off); crossfade between songs.

### Library
- Home with Continue Watching and the time left, next episode, recent and frequent folders; series grouped by
  season; music by song, album, artist and genre; playlists, history, network streams and radio.
- Its own tag reader (MP3, FLAC, Ogg, Opus, M4A, WAV, AIFF, APE, WavPack); a library of 3,500 files is checked in
  about 0.1 s on later starts.

### Look and system
- Monochrome interface in Onest; dark, light and system theme; skins, including your own JSON reloaded live.
- English and Russian; Windows text size; full keyboard access and Narrator support.
- Its own installer: per-user, no administrator rights, update to any version and go back to a kept one.
- Media keys, the Windows media card, taskbar buttons, tray icon, jump list, cover thumbnails in File Explorer.

### Known limits
- The build is not code-signed: Windows SmartScreen asks before the first run.
- Moonbow does not update itself yet; a new version is a new installer.
- Windows 10 and 11, 64-bit only.

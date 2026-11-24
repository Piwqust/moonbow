# Changelog

What changes for the person using Moonbow, newest first. The release notes on GitHub are taken from here.
The day-by-day development journal (in Russian) is in [`changelog/`](changelog/README.md).

## 0.4.0 (2026-11-24)

### Skip intro without chapters
- **Skip intro** now works for episodes that have no chapters. Moonbow compares the first minutes of an episode with
  the next one's in the background, on this computer, and finds the opening song they share. The button appears over
  it; the next time either episode plays, it is there from the start. Nothing is sent anywhere.
  **Settings → Playback → Find intros by sound** turns it off.

### Series
- Series numbered without a season are recognized: anime releases such as `[Group] Show - 05 (1080p)` and
  `Show - 12v2 [BD]`, a `Show - 05` file in the show's own folder, `Show.E05` and `Show Ep 05`. They group under
  their show as season 1. `Apollo - 13 (1995)` stays a film.
- **Next episode** and the series' audio and subtitle languages also work for these, and for numbered files in
  season folders (`Season 2\03 Title.mkv`). TMDB looks their posters up as episodes.
- **Series Settings…** in a library video's menu says what its folder is when the names don't: a series with its
  name and season (the episode numbers come from the names), or not a series at all. **Read From Names** forgets it.

### Subtitles
- **Settings → Subtitles → Download missing subtitles**: when a video has no subtitles in your language, Moonbow
  finds them on OpenSubtitles, saves them next to the video and turns them on. "Only ones made for this file" takes
  subtitles timed for exactly this release; "Also the best match by name" takes the most downloaded ones otherwise.
  It uses your OpenSubtitles key and turns subtitle search on.
- The subtitle search window says what is sent to OpenSubtitles and where your free API key comes from, with a
  button to Settings; once the key is there, **Turn On and Search** starts the search in place.
- When the day's downloads are used up, Moonbow says when they reset and that a free OpenSubtitles account gives
  more.

### Music
- **Scrobbling** to Last.fm and ListenBrainz under your account (**Settings → Privacy → Online services**, off by
  default). A song counts after half its length or 4 minutes; the one you start shows as playing. Songs heard
  offline are sent later, up to 14 days. Last.fm needs your own free API account (its API key and shared secret, from
  last.fm/api/account/create) and your password once: Moonbow keeps the session Last.fm gives, not the password.
  ListenBrainz needs your user token.
- **Show what plays in Discord**: your Discord profile shows the song, or the video too, with its time, while the
  Discord app runs on this computer. It needs an application of your own at discord.com/developers (its Application
  ID goes into Settings). Off by default; a pause and a seek update it, quitting clears it.
- Podcasts, audiobooks and long mixes go on where you stopped, a few seconds back (**Settings → Playback → Continue
  long audio**: longer than 20 minutes by default; audiobooks in `.m4b` always). Songs still start from the
  beginning.

### Video
- **DVD and Blu-ray**: open an `.iso` image or a disc folder (one with `VIDEO_TS` or `BDMV`; also by dropping it on
  the window) and Moonbow plays the disc. Its menus answer the arrow keys, <kbd>Enter</kbd>, <kbd>Backspace</kbd>
  (back) and clicks.
- On AMD Radeon graphics, **Settings → Video → Double the frame rate on AMD Radeon** (AMD Fluid Motion Video): the
  card computes a frame between each two, so motion looks smoother.
- Laptops on battery draw the picture with the **Fast** preset while Windows energy saver is on: the graphics card
  works less and Moonbow takes about 75 MB less memory with 1080p video, 115 MB less with 4K. **Settings → Video →
  On battery** makes it always on battery, or never. An upscaling method you picked stays.
- **HDR output** no longer lists "HDR10+" and "Dolby Vision": they only ever sent HDR10. Such videos still reach an
  HDR display as HDR10, with their brightness data used for tone mapping. A saved choice of either becomes HDR10.

### Languages
- Moonbow can show other languages from translation files: put `<code>.json` into `%APPDATA%\Moonbow\i18n\` and
  pick the language in **Settings → General**. `i18n/en.json` in the repository holds every English string of the
  interface as the source for translators (Weblate and Crowdin read the format); see `i18n/README.md`.

### Install
- The zip is truly portable now: its settings, library and history stay in a `Data` folder next to `Moonbow.exe`
  (the `portable.txt` beside it does this; delete it to use `%APPDATA%\Moonbow`).
- Moonbow can be installed with Scoop: `scoop bucket add moonbow https://github.com/Piwqust/moonbow`, then
  `scoop install moonbow`.

### Music view
- The VU meters read 3 dB lower by default ("0 VU = −1 dBFS"): a loud modern song now tops out around 0 VU and only
  the very loudest go a little into the red. Before, such songs sat in the red and often held the needles at +3.
  A calibration you picked yourself stays as it is.

## 0.3.0 (2026-11-21)

### Separate windows
- Any part of the player opens in a window of its own: the controls, the seek bar, the cover, what plays, the song
  lyrics, the queue, the visualization, the audio meters together or each meter alone (VU meters, a single VU meter,
  loudness, levels, stereo, spectrum). Right-click in the player or open the Moonbow menu → **Separate windows**, or
  find it with <kbd>Ctrl</kbd>+<kbd>K</kbd>. The main window keeps its own copy.
- The windows stay on screen when the main window is minimized, have no taskbar buttons, and come back where they were
  at the next start, also on a second monitor (or on the nearest one when that monitor is gone).
- Drag a window by any part that is not a control; double-click it for fullscreen (<kbd>Esc</kbd> leaves). Its
  right-click menu keeps it above other windows or hides its title bar. The cover window stays square.
- All views share one capture of Moonbow's sound: more windows don't measure the sound again, and resetting peaks or
  loudness resets every window.

### Music view
- The VU meters grow when the window gets wider, not only when it gets taller, and keep growing on large screens
  (no size cap). The title moves right under them, so a squarish window leaves no empty gap.
- Everything in a VU meter follows its size: the numbers, ticks, needle and reading grow smoothly from a small wing
  beside the cover to a 4K screen, and the in-between numbers drop away when a meter is too small for them.
- On a tall music view the title, artist and one-line lyrics grow up to 1.5 times.
- The VU meters are redrawn: the level ends exactly on the needle, the needle is a tapered pointer that reaches the
  ticks, the 0 VU edge of the loud zone is clean, the numbers sit at an even distance from their ticks, and the channel
  and the reading are on the hub's line under the ends of the scale.
- A double click on the music view toggles fullscreen, as on a video (**Settings → Controls → Mouse**); moving the
  mouse there in fullscreen brings the controls back.
- The VU meters read 3 dB lower by default, so loud modern releases stay around 0 VU instead of holding the needles
  at +3.
- **VU meter calibration** now counts dBFS as studio meters do (a test tone's peak): at "−18 dBFS (EBU)" a −18 dBFS
  tone reads 0 VU; before, every setting read it 3 dB low. The default is now "−4 dBFS (loud modern masters)", which
  keeps music where the needles were; the old −10 setting is now −7.
- Past +3 the reading says ">+3 VU" instead of a made-up number, and the held peak stays on the scale instead of
  hanging past its end.
- Right-click a part of the music view to hide it: the title and artist, the Lyrics and Queue buttons, the lyrics, the
  "LRCLIB · Save .lrc" line, the blurred background. On the cover or the visualization the menu changes the
  visualization and where it goes. Whatever is hidden comes back from the player menu → **Bring back hidden** (it
  shows only when something is hidden). Hide the title or the buttons and the cover or meters grow into the room.
- Right-clicking the lyrics opens their own menu again (style, timing, lyrics only); it had stopped opening.
- Right-click a button on the control bar to remove it, or the bar itself to **Add a button**, change the bar's style,
  restore the default buttons or open **Customize player**. A right click on the seek bar no longer seeks: it turns the
  song's wave on and off.
- The **Queue** button under the song title opens the queue at the right edge, next to the cover and the lyrics. It
  used to open over the cover or the meters and the title.

### Look
- Covers and frames have a faint 1 px edge, so a dark cover no longer melts into the dark background (in the library,
  the queue, the music view, the mini player and the dialogs).
- The highlighted row of a menu follows the menu's rounded corners.
- Chapter marks on the seek bar show on both parts of it: a dark notch in the played part, a grey tick ahead.

### Small things
- Hiding a part of the music view or removing a button from the control bar says so and offers **Bring back** for
  8 seconds. The "Hide…" row is now at the end of the right-click menu, not the first row under the pointer.
- A link to a settings page ("Buffering settings…", "Lyrics settings…", "All audio settings", "Open Settings" in
  the subtitle search) opens that page with every setting on it, scrolled to the right part, also while Settings
  shows **Basic** only. It used to open "General" or a page without those settings.
- In a narrow window the mini player is a strip along the bottom of the library instead of a card over the right
  half of the list.
- Beside the music the queue takes the whole right side, not a box at the bottom of an empty column.
- The folder of what plays, in the title bar, is reachable with <kbd>Tab</kbd>.
- A show's page counts "серии", not "выпуски" (Russian). **Customize player** names its keyboard keys.
- Shuffle and "Stop after this item" line up with the other rows of the Playback panel.
- On NVIDIA graphics Moonbow runs about 50 fewer threads: 35 instead of 87 with no file open, 99 instead of 151
  while a video plays.

### Fixed
- After a file failed to open, opening the next one from Explorer left the keyboard on the hidden "Back to library"
  button: <kbd>Space</kbd> left the player instead of pausing.
- Under an open right-click menu the tooltip or the seek preview of what was under the pointer stayed on screen.
- The queue in a window of its own had an "Unpin the queue" button that acted on the main window.
- In the smallest window the Lyrics and Queue buttons of the music view slid half under the control bar.
- The search field showed on the Home page of an empty library.
- With **Seek precisely** turned off the arrow keys jump to the nearest keyframe, which takes about half the time.
  They kept seeking to the exact frame, so the setting did nothing.

## 0.2.0 (2026-11-20)

### Updates
- Moonbow updates itself. Once a day it checks GitHub for a new version, downloads it in the background and installs
  it when you close Moonbow; **Restart now** in the notification installs it at once and opens Moonbow again.
- **Settings → General → Updates** shows the version and what the updater knows, with **Check now**; both the daily
  check and installing on close can be turned off. **About Moonbow** shows the same line.
- Every update is signed: Moonbow installs a setup only when the signature of the update feed checks out and the
  downloaded file matches it byte for byte.
- A version you went back from (**About Moonbow → Versions on this computer**) is not installed again by itself.
- The portable copy only says that a new version is out and opens its page.

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
  subtitles on top; OpenSubtitles search with your own key.
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

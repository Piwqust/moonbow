# Audit raw findings: src/app/player/* without LiveState (reviewer A4)

1. **must** — Queue "Remove" with nothing selected runs a per-item Remove. Each call is O(n), emits `changed` and rebuilds the header summary, so the whole operation is O(n²+) and freezes the app on a 5k-20k queue.
   - Where: QueuePanel.cpp:736-739, :746, :613-630; PlayQueue.cpp:64-81.
   - Fix: a single `Set({current}, 0, name)` or `Clear()`. (checked)
2. **should** — ErrorCard::Refresh calls GetFileAttributesW on the UI thread, which blocks for the SMB timeout on a dead share, once per file change.
   - Where: PlayerView.cpp:394.
   - Fix: use lib::MaybeExists. (checked)
3. **should** — SubWords downgrades maxLevel_ for the whole session on one MPV_FORMAT_NONE answer. mpv answers NONE without pts, e.g. paused right after a seek or load, so the overlay loses the ASS styles and \an until restart.
   - Where: SubWords.cpp:176-181, :238-241, :142.
   - Fix: delete the fallback; the bundled mpv 0.41 has all three properties. (likely)
4. **should** — The word selection is kept on a rebuild with the same text but a different word count, because tiny blocks are skipped. words_[i] is then read out of range.
   - Where: SubWords.cpp:434-441, :552, :751-753, :1002-1005.
   - Fix: reset the selection when words_.size() changes. (checked)
5. **should** — RTL subtitles with left or right alignment are drawn on the wrong side, because DWrite LEADING means the right edge with RTL reading direction.
   - Where: SubWords.cpp:583-587.
   - Fix: swap LEADING and TRAILING for rtl blocks. (checked)
6. **nice** — The Forward10/Backward10 buttons use controls.skipShort (5 s when set so), while the 15 s buttons skip the OSD.
   - Where: ControlBar.cpp:634-645.
   - Fix: SeekBy(±10/±15). (checked)
7. **nice** — "Skip intro/credits" shows for chapters containing the whole word "op" or "ed" ("Sonata Op. 27", "Ed's Story"), even over music.
   - Where: Episodes.h:113-136.
   - Fix: "op"/"ed" only when it is the whole title, optionally with a number. (checked)
8. **nice** — The speed slider max is 2× while the menu offers 3× and SpeedUp goes higher.
   - Where: SettingsPanel.cpp:505-514.
   - Fix: raise the max. (checked)
9. **nice (lean)** — The subtitle style is read in three places: SubWords.cpp:34-59 and :351-371, SettingsSubtitlePreview.cpp:33-96, and PlayerView SubtitleBand.
   - Fix: one shared function. (checked)
10. **nice** — Every local video starts a lyrics worker that parses the movie's tags (LyricsPathFor is non-empty for every path).
    - Where: MusicView.cpp:457.
    - Fix: for non-audio, only look for the .lrc. (checked)

Left out (~10):
- g_menu is not cleared.
- IsRtl counts U+FEFF as RTL.
- ReadEmbeddedLyrics makes up to 513 synchronous mpv reads.
- DurationProbe::known_ only grows.
- SortQueue with no current item.
- The lookup toast text says https only.
- Overlong lookup URLs fail silently.
- SubtitleBand ignores the lift.
- The PiP overlay polls at 8 Hz while paused.
- ListHeader::compact_ is unused, and PresetField copies the presets on every paint.

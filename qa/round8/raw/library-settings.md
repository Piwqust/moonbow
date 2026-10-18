# Audit raw findings: library pages, settings, dialogs, tools (reviewer A5)

1. **must** — The OpenSubtitles extension isn't filtered, so the server can create `x.hta`/`.lnk`/`.url`, or `x.a\b` (makes subfolders in the data dir), or `x.:abc` (an NTFS stream). Same as storage #3.
   - Where: OnlineData.cpp:341-353, Online.cpp:477, :509-512, Http.cpp:223-231.
   - Fix: whitelist srt/ass/ssa/vtt/sub/smi; add tests. (checked)
2. **should** — Save in Add Subtitles forgets subtitles downloaded into the data folder and the remembered pairs that AutoMatch dropped.
   - Where: AddSubtitles.cpp:529-547, :159-161, :972-976.
   - Fix: keep the old entries that aren't in model_->subs. (checked)
3. **should** — The panel EQ preset "none" does nothing (ApplyEqPreset has no "none"); Settings has its own branch.
   - Where: SettingsPanel.cpp:797-801, Config.cpp:342-344, SettingsRows.cpp:216-222.
   - Fix: handle "none" in Config::ApplyEqPreset and delete the branch in SettingsRows. (checked)
4. **should** — The "Новое" rule differs on Home (no history entry; most callers pass h=nullptr) from the pages (not watched and no progress).
   - Where: LibHome.cpp:292, :338, :403, :524, :541 vs Views.cpp:505.
   - Fix: one helper, and pass hist.Find(path). (checked)
5. **should** — MarkSeen runs on every refresh, even behind the player and on Playlists/Network/Recent, so the New badge never shows for Explorer-only users.
   - Where: Views.cpp:610, :389; LibHome.cpp:553, :192.
   - Fix: only when visible, and only on Home/Folder/Music. (checked)
6. **should** — Menus copy the whole queue by value many times (20k songs × ~16 copies).
   - Where: LibraryCommon.cpp:423-437, :475-480; Views.cpp:659-668, :690, :835-841; LibMusic.cpp:269-279.
   - Fix: a shared_ptr<const vector>, or build the queue inside the action. (checked)
7. **should** — Settings "Reset picture" bypasses LiveState: grayscale and negate stay, video-unscaled is not reset, zoomMode and aspect go stale and are saved per file.
   - Where: SettingsActions.cpp:38-48.
   - Fix: call Live().ResetPicture(), SetZoomMode("fit") and SetAspect("auto"), reset video.aspect. (checked)
8. **should** — Settings "Clear history" emits no libraryChanged, so Home still shows the history. It differs from Recent's version (online data). rescan-library skips the emit.
   - Where: SettingsActions.cpp:26-31, :72-75 vs Views.cpp:1118-1124, LibraryCommon.cpp:373-376.
   - Fix: one ClearHistory() helper; the action calls lib::RescanLibrary(). (checked)
9. **should** — qa/make-worstcase.ps1 runs `Remove-Item -Recurse` on any -Out that lacks the stamp.
   - Where: make-worstcase.ps1:16, :23.
   - Fix: refuse a folder outside %TEMP%, or delete only when the stamp exists. (checked)
10. **should** — Back after a search restores a scroll offset taken from the filtered list.
    - Where: Views.cpp:340-346, :310-322; LibHome.cpp:230-240.
    - Fix: SaveState sets scroll=-1 while a filter is active. (checked)
11. **nice** — Number fields clamp before the InRange check, so the "Enter a value from…" toast never shows.
    - Where: SettingsRows.cpp:989-997.
    - Fix: test the raw value. (checked)
12. **nice (lean)** — Dead and duplicate code (about 40 lines can go):
    - ItemGrid::SelectedOr and VisibleCount have no callers;
    - FolderPage::SortMenu duplicates SortMenuItems;
    - two Grouped() helpers;
    - three "looks like a folder" checks.

Left out (12):
- Meta() runs twice per card paint.
- AddScripts overwrites a same-named script.
- The Network Stream playlist emits no libraryChanged.
- A stale search answer overwrites a newer one in Find Match and Online Subtitles.
- AttachRemembered calls PathFileExistsW on the UI thread.
- Changing the onboarding folder removes the old root without a save.
- A newline in a stream rename splits the entry.
- Network group counts ignore the filter.
- AlbumOf/ArtistOf/GenreOf return copies.
- input.ps1 and command.ps1 don't guard against the owner's PID.
- ListFiles runs on the UI thread in Add Subtitles.
- A language switch loses the scroll position.

Tests missing for: hostile SubtitleFileName names, subtitle-pairs.json, ParseBinding, FindStateEntry.

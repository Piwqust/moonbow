# Round 5: library hunt (r5-library-eye)

- **Exe:** `%TEMP%\lumen-qa\r5-v2\Lumen.exe`, not rebuilt.
- **Instances:**
  - `--automation` through `tools\dev\launch.ps1`, with data dirs `%TEMP%\lumen-qa\r5le-1` … `r5le-6`.
  - Every one was started with `-Library` pointing at a test folder (`media`, `worst`, `r5le-media\empty`, `r5le-media\one`) and with onboarding skipped.
  - None of them saw `%USERPROFILE%\Videos`. All were killed by PID.
- **Evidence:** `%TEMP%\lumen-qa\r5le\`.
  - `<shot>.png`: full resolution.
  - `<shot>.tree.txt`: tree dump v2.
  - `<shot>.audit.md`: `ui-audit.py` with the round-5 allowlist.
  - `crop-*.png` and `sheet-*.png`: zoomed crops and contact sheets.
  - Some screens were reviewed from the baseline run `%TEMP%\lumen-qa\r5sw-baseline\<scenario>\final.png` (same exe).
- **Helper:** `%TEMP%\lumen-qa\r5le\snap.ps1 -P <pid> -Name <shot> -Cmd "a;;b"`.
  - Each step is a `command.ps1` command, or `key <K>`, `type <text>`, `in <input.ps1 args>`, `size w,h`, `sleep ms`.
  - Then it runs `wait-idle`, `capture.ps1`, `dump-tree.ps1 -Out` and `ui-audit.py`.
  - The "Steps" column below uses this notation. `P` is the PID that `launch.ps1` printed.
- **Datasets:**
  - `media`, with a seeded `history.json`: 50 %, finished S01E01 (gives "next episode"), 91 %, audio, and an entry from 3 days ago.
  - `worst`: 1415 files, including `Big\1284 tracks`.
  - empty;
  - one video.

## Summary

| ID | Sev | Status | Screen / state | What is wrong | Fix |
|---|---|---|---|---|---|
| LB-01 | Broken | confirmed | Playlist › ⋯ › Удалить… › Удалить (also Delete key / context menu on a playlist card) | The confirm dialog closes, but the playlist is **not deleted**. Cause: use-after-free of the `done` callback in `ShowConfirm` | fixed |
| LB-02 | Ugly | confirmed | Every library list/grid, Back/Forward (button, Backspace, `nav back`) | Scroll position and selection reset to the top. On the 1355-song list, offset 2980 becomes 0. The folder you came from is not re-selected | fixed |
| LB-03 | Ugly | confirmed | Header breadcrumb, playlist or folder with a long name | The **current** crumb is capped at 280 DIP and ellipsized while ~130 DIP of the header stay empty. It has no tooltip | fixed |
| LB-04 | Ugly | confirmed | Breadcrumb deeper than 4 levels | The "…" crumb is a plain `Label`. Levels 1–8 of a 12-level path can't be reached from the header | fixed |
| LB-05 | Ugly | confirmed | Music › Artists (and Genres), list view | The song count sits in a 366-DIP "subtitle" column at 60 % of the row. Artist names are ellipsized at 556 DIP with ~300 DIP empty to their right, and the count doesn't line up with the right-aligned counts of other lists | fixed |
| LB-06 | Fragile→Ugly | confirmed | Every ItemGrid (cards, rows, Home, search, queue-style lists) | No tooltip at all: an ellipsized title, artist or album can't be read anywhere in the library. Upgrades baseline #19 from "verify" to confirmed | fixed |
| LB-07 | Ugly | confirmed | Songs / album / artist song lists, window < ~800 DIP | The summary "1355 песен · 9:31:45" runs 36 DIP past its row and the window clips it mid-glyph ("9:31:4") | fixed |
| LB-08 | Ugly | confirmed | Search results, Recent, Home, with identical names (worst `Same name A/B`, `Same title A/B/C`) | Two "Clip" cards and three identical "Intro · Various · Intro · 0:04" rows, with nothing (folder, path, tooltip) to tell them apart | fixed |
| LB-09 | Ugly | confirmed | Add to playlist › Новый плейлист… | The dialog says "Эти файлы будут добавлены в плейлист" over an **empty** drop zone. The right-clicked file is silently added after Create | fixed |
| LB-10 | Ugly | confirmed | Collapsed rail → hover an item → Ctrl+B | The rail tooltip ("Музыка") stays on screen over the expanded item's own label. It survives navigation and a window resize | fixed |
| LB-11 | Ugly | confirmed | Empty Videos page, empty Playlists page, an empty playlist | Sort and Grid/List buttons are shown with nothing to sort. Round 4 W-12 is **still open** and also hits Videos | fixed |
| LB-12 | Ugly | confirmed | Worst `Broken\Audio with a video extension.mkv` | Marked "Повреждён" (dimmed, warning chip), but the file opens and plays (`state`: file=1). The label is wrong data | fixed |
| LB-13 | Ugly | confirmed | Cards with a progress bar (Continue watching, Recently added, folders) | The duration badge moves 4 DIP up when the card has progress, so badges in one row don't line up | fixed |
| LB-14 | Ugly | confirmed | Recent (and Network) list group headers | Counts ("4 элемента", "1 элемент") are right-aligned at 1260 DIP, while the column grid ends at 1224 (Videos folders: 1224). The right edges differ by page | fixed |
| LB-15 | Fragile | confirmed | Delete-playlist confirm | Keyboard focus starts on the destructive "Удалить", so Enter deletes. This is masked today by LB-01 | fixed |
| LB-16 | Fragile | confirmed | "Ничего не найдено" with a long query | The whole query is echoed into the paragraph with no cap, so a long paste makes a 5+ line block. The one-word orphan "слово." is the Russian twin of round 4 W-13 | fixed |
| LB-17 | Fragile | confirmed | Dialogs: New playlist vs Rename / Delete | Two dialog kits side by side. `dlg::Frame` has its Cancel button on the left and 100-DIP buttons. `ui::Dialog` has Cancel next to OK on the right, 86-DIP buttons, and a different field-label style | fixed |
| LB-18 | Fragile | suspected | Songs list, one wheel notch | ~66 frames are rendered over ~2 s for a 180 ms scroll. Paint is cheap (avg 1.6 ms, max 7.4 ms), but 20 notches cost 5 s of CPU | fixed |

**Re-checks of known items:**

| Item | Result |
|---|---|
| Baseline #8 (nested crumb collapses) | **Still broken, and worse.** At 1024×640 with 12 levels the current crumb is 3 DIP wide and shows "L." (`nest12-1024.tree.txt`: `CrumbButton [577,63 3x24] … "Level 12" trunc`; `crop-nest12-1024.png`) |
| Baseline #9 (Albums crumb at min) | Same cause as #8; not re-captured |
| Baseline #10 ("№" header cut) | **Fixed at the default size**: "№ ↑" is whole at 1280. **Still cut at 720×440** (`songs-min.tree.txt`: `"№" trunc box=[228,186 12x32]`) |
| Baseline #11 (song columns never drop) | **Still open.** At 720×440 the artist, album and "Исполнитель" are cut to 57 DIP (`songs-min.png`) |
| Baseline #19 (card tooltips) | Confirmed missing: LB-06 |
| Round 4 W-04 (maximized covers the taskbar) | **Fixed**: maximized client 2560×1392 = the work area (`home-max2.tree.txt` line 1) |
| Round 4 W-05 (dead right edge of rows) | **Fixed by code**: `ScrollView::HitTest` now takes the strip only at the thumb or while the bar is shown (`Controls.cpp:931-940`) |
| Round 4 W-06 (rail tooltip over its icon) | **Fixed**: the tooltip sits at x=56 beside the rail (`rail.png`). But see LB-10 |
| Round 4 W-11 (Home "Recently added" short) | **Not a bug now.** The row shows as many cards as fit: 4 at 2560, where cards stretch to 474 DIP (`crop-home-max-added.png`). Card stretching is a design gap (G-07) |
| Round 4 W-12 (sort/view on empty Playlists) | **Still open**, and it also hits empty Videos: LB-11 |
| Round 4 W-13 ("Nothing found" orphan) | **Still present** in Russian ("слово." alone on the last line, `search-long.png`) |

**Verified fine (no row):**

- **Russian plurals:**
  - 1 песня, 2 песни, 5 песен, 11 песен, 21 песня, 22 элемента;
  - 1355 песен, 0 выпусков, 5 станций;
  - 4 альбома, 71 песня, 28 альбомов, 22 исполнителя, 2 жанра.
- **Keyboard in lists and grids:**
  - Down, End, Home and Enter work.
  - The first item is selected after opening a folder from the keyboard.
  - End scrolls the last row into view (`kb-end.png`).
- **Context menus:** video, recent row, playlist ⋯ (disabled items greyed). The "Add to playlist" submenu works.
- **Other features:**
  - Mark as watched dims the card.
  - Favorites: the heart, Recent › Избранное and Home.
  - "Next episode" card for a finished S01E01 (S01E02 · "Следующая серия").
  - Continue-watching "осталось" times.
  - Rename playlist.
  - Drag-reorder in a playlist (the UI order changes).
  - Scan badge "Сканирование медиатеки… найдено файлов: 1415".
  - CJK, RTL, emoji and diacritic names render (`plur-albums-list.png`).
  - Covers: huge, tiny, wide and none (`covers-artist.png`).
  - Empty states on every page (`sheet-empty.png`).
  - One-item library in the light theme (`sheet-one.png`).
  - Scale 1.5 and 2.0 videos (baseline).
- **CPU and frames:**
  - Idle CPU is 0 on the 1355-song list (2 s sample).
  - Frame times while scrolling: layout max 3.4 ms, paint max 7.4 ms (`LUMEN_TRACE`, private `TMP` dir `r5le-tr`).

## Fix round (r5-fix-library)

Agent r5-fix-library, branch `worktree-agent-a353d2df4f48757cb`, on main `67222df`. Every item was run in
`--automation` instances started with `-Library` on a test folder (`worst`, `media` with the seeded
`history.json`, `r5le-media\empty`, `r5fa-media` for 6000 songs), before (`%TEMP%\lumen-qa\r5-v6\Lumen.exe`)
and after, with the steps of the finding and tree dump v2. Evidence: `%TEMP%\lumen-qa\r5fb\<base|new|new2>-<shot>.*`
(scripts `v1.ps1`…`v3.ps1`, `perf.ps1`, `drag.ps1` there). Each finding below has a "Fix" line.

**Re-checks:**

| Item | Result |
|---|---|
| Baseline #8 (nested crumb) | Fixed: 12 levels at 1024×640 `… › Level 10 › Level 11 › Level 12`, current crumb whole (base `3x24` "L."); at 720×440 `… › Level 12` (`new-nest12-1024`, `new-nest12-720`) |
| Baseline #9 (Albums crumb at min) | Fixed: `… › Альбомы` 74 DIP, whole (base 51 DIP "Альбо…") (`new-albums-min`) |
| Baseline #10 ("№" cut) | Fixed: track column 32 DIP, "№ ↑" whole at 720×440 and every other size (sweep: the 8 "№" findings are gone) |
| Baseline #11 (song columns never drop) | Fixed: below 420 DIP of text columns the album column goes, below 240 the artist; at 720×440 the title gets 214 DIP (base 99 + three 57-DIP columns); a dropped column's text is in the row's tooltip |
| W-12 (sort/view on empty pages) | Fixed with LB-11 |
| W-13 / LB-16 orphan word | Fixed: no-break space before "слово." / "word." |
| Baseline #19 (card tooltips) | Fixed with LB-06 for the library; queue rows (player panel) not in this scope |

**Other items of this round:**

| Item | Status | Verified |
|---|---|---|
| "Продолжить просмотр" thumbnail | fixed where Media Foundation decodes the file | The design (`design/ref/Player_6261-15078.png`) shows the frame where playback stopped. Cards now ask for `"<file>\|<seconds>"`; the thumbnail workers decode that frame with Media Foundation (loaded on first use, never on the UI thread) and fall back to the shell thumbnail. "Chaptered Demo (1080p).mkv" at 81.8 s shows the 81-s frame (compare `crop-new-continue.png` with `ff-chaptered-81.png`). "Long Seek Test 10min.mp4" is H.264 **4:4:4**, which the Windows decoder can't open, so it keeps the shell's frame (2:20) |
| Window drag relayout | fixed | `MainWindow.cpp`: `WM_WINDOWPOSCHANGED` refreshes the title bar only when the size changed; `TitleBar::Refresh` lays out only what changed and sets the caption only when it differs. 40 `SetWindowPos(SWP_NOSIZE)` moves: 55 frames and 62 ms of UI thread before, 14 frames (the hover after the final mouse move) and 5 ms after (`drag.ps1`) |
| Menu filter highlight | fixed | The typed text is underlined in the accent colour in every matching label, also on the hovered row; offsets are measured once per keystroke (`crop-new2-menu-filter.png`) |
| G-07 card width cap | fixed | Cards fill the row but add a column past 1.25× the design width. Maximized 2560: video cards 368 DIP (base 474), square 213 (base 300); 1920: video 318 (base 352) (`new-home-max`, `new-home-seeded`) |
| G-08 "Неизвестный исполнитель" on cards | fixed | Untagged songs leave the card's second line empty; list columns still say it |
| G-09 empty Music without "Пересканировать" | fixed (Music) | Empty Music now has "Добавить папку" + "Пересканировать" like Videos. The empty-playlist "Добавить файлы" button is not done: it would open a native file picker that can't be tested here |
| G-01, G-02, G-03, G-04, G-05, G-06, G-10, G-11 | not done | Larger features or another agent's area (G-05/G-06: the window chrome and sidebar layout in `MainWindow.cpp`) |

**Performance** (`perf.ps1`: UI-thread cycle time at 3.2 GHz, frames from `LUMEN_TRACE`; 143-Hz monitor):

| | before (r5-v6) | after |
|---|---|---|
| Open `worst\Big\1284 tracks` (cold / warm / warm) | 29 / 22 / 20 ms | 37 / 19 / 23 ms |
| Open Songs, 6000 songs (cold / warm / warm) | 83 / 50 / 46 ms, 86 / 49 / 42 ms | 91 / 49 / 49 ms |
| One wheel notch, 1284 tracks | 43 frames, 116 ms | 28 frames, 56 ms |
| One wheel notch, 6000 songs | 45 / 41 frames, 139 / 113 ms | 32 frames, 58 ms |
| Idle, 3 s | 0 ms | 0 ms |

**Sweep** (`sweep.ps1 -Filter "home|library|videos|music|playlist|recent|network|search|worst"`, 108 scenarios, all ok):
25 findings on r5-v6 (6 medium) → 14 on the fix build (4 medium), none new. Gone: both crumb findings
(`music-albums.min`, `worst-nested.1024x640`) and the nine "№" / narrow-songs ones. Left: the player title
and MusicView title on worst-case tags (r5-fix-player's area) and low "ellipsized in a grid" candidates,
which now have hover tooltips (the dump only shows the hovered one).

## Findings

### LB-01: deleting a playlist does nothing (Broken, confirmed)

- **Screen:** a playlist page, `⋯` › "Удалить…" › "Удалить". The same `ShowConfirm` is used by Delete on a playlist card and by the card's context menu.
- **Steps:**
  1. `launch.ps1 -DataDir %TEMP%\lumen-qa\r5le-2 -Fresh -Library %TEMP%\lumen-qa\worst`
  2. `nav playlists`, then click "Создать плейлист" (616,528), `type Вечер`, click "Создать" (927,641).
  3. `menu more`, click "Удалить…" (1100,330), click "Удалить" (794,448). The same happens with `key Return` on the focused "Удалить".
  4. `state`.
- **Expected:** the playlist is deleted, the page goes back to Playlists, and `playlists.json` no longer lists it.
- **Actual:**
  - The dialog closes.
  - `state` still says `route=playlists pl-1a14f80a1d4-1`, the crumb still says "Вечер", and `playlists.json` still holds "Вечер".
  - Tried 3 times: twice with the mouse, once with Enter. Back on Playlists the card "Вечер · 0 элементов" is still there.
- **Evidence:** `pl-delete.tree.txt` (the dialog), `pl-deleted.tree.txt` / `pl-del3d.tree.txt` (after: no popup, crumb "Вечер"), `pl-deleted-back.tree.txt` ("1 элемент"), and `%TEMP%\lumen-qa\r5le-2\playlists.json`.
- **Cause:**
  - `src/app/library/LibraryCommon.cpp:501-504`: `okBtn->onClick = [raw, done] { raw->Close(); if (done) done(); };`.
  - `Dialog::Close` → `Host::ClosePopupAt` (`src/ui/Host.cpp:472-477`) runs `popup.content.reset()` synchronously. That destroys the dialog, the button and the lambda that is running, together with its captured `done`. The next line then reads a freed `std::function`, which is undefined behaviour. Here it reads as empty, so nothing runs.
  - `ShowTextPrompt` has the same pattern (`LibraryCommon.cpp:477-480`: `raw->Close(); if (done) done(text);`). Rename happened to work in this run. It is the same UB and can crash or no-op.
- **Fix:** move the callback out before closing, for example `auto fn = std::move(done); raw->Close(); if (fn) fn();`. The same goes for `text` and `done` in `ShowTextPrompt`. Alternatively post the close (`host->Post`). Grep the other `raw->Close();` sites for captures used after the close.
- **Fix (r5-fix-library, fixed):** Re-verified on the r5fb build (the audit fix #1 is kept): a new playlist, `⋯` › "Удалить…" › "Удалить" closes the dialog, `state` says `route=playlists`, and `playlists.json` has `"playlists": []` (`%TEMP%\lumen-qa\r5fb\new-pl-delete2.tree.txt`).

### LB-02: Back/Forward loses scroll and selection (Ugly, confirmed)

- **Steps:**
  1. Use the r5le-2 instance (worst-case library).
  2. `nav music songs`, then `key Home`, `in -Wheel 740,500 -Delta -30`. The dump shows `scroll offset=2980/56380`.
  3. `nav music albums`, then `nav back`. Offset `0/56380`.
  4. `nav forward;;nav back`. Still 0.
  5. In `media`: `nav videos`, then `key Down`×2, `key Return` (into "Videos"), `key Back`. Nothing is selected in the parent, and the "Videos" folder is not highlighted.
- **Expected:** like Explorer, Plex, foobar2000 and MusicBee, going back restores the scroll offset and selects the item you entered.
- **Evidence:** `bk-mid.tree.txt`, `bk-albums.tree.txt`, `bk-back.tree.txt`, `bk-fwd.tree.txt` (ScrollView offsets); `sheet-kbfolder.png`.
- **Cause:**
  - `src/app/App.h:36-40`: `Route` is only `{section, path}`.
  - `App::GoBack/GoForward` (`src/app/App.cpp:783-795`) just move `historyPos_` and emit `routeChanged`.
  - The page is rebuilt from scratch, and no view state is stored per history entry.
- **Fix:** store `{scrollY, selected path}` next to each `nav_` entry when leaving a page, for example through a `LibraryView::SaveState()` / `RestoreState()` pair. Restore it after the first layout. Restore the selection by path, not by index, because the content may have changed. When going up (Backspace or a crumb), select the child folder.
- **Fix (r5-fix-library, fixed):** Each history entry keeps a `ViewState` (scroll offset, selected path + title) next to its `Route`; `App::routeLeaving` lets the panes save it, `LibraryView::RestoreState` applies it after the first layout without animation or scrollbar flash. Going up a level to a page with no saved state (a crumb, the "…" menu, `nav` to an ancestor) selects the folder you came from. Songs: offset 2880/56380 → `nav music albums` → `nav back` gives 2880 (base 0); forward + back 2880 (base 0) (`new-lb02-back`, `new-lb02-fwdback` vs `base-*`). Videos: `key Right ×2`, Return into "Videos", Backspace → `ItemGrid … selected=1` (base: nothing selected) (`new-kb2`). `src/app/App.h` `ViewState`/`NavEntry`, `src/app/Shell.cpp` `LibraryPane::ShowRoute`, `src/app/library/Views.cpp` `ApplyViewState`.

### LB-03: the current breadcrumb is capped at 280 DIP (Ugly, confirmed)

- **Steps:**
  1. In r5le-2: `nav playlists`, then create a playlist with `type Любимые видео 🎬 и очень длинное название плейлиста которое не помещается` and click Create.
- **Expected:** the current crumb takes the room up to the counter or Sort button, as the comment at `Shell.cpp:433` says. When cut, a tooltip shows the full name.
- **Actual:** `CrumbButton [416,63 280x24] … trunc` shows "Любимые видео 🎬 и очень длин…". The crumb row reaches x=824, so ~128 DIP are unused. There is no tooltip.
- **Evidence:** `pl-created.png`, `pl-created.tree.txt`.
- **Cause:**
  - `src/app/Shell.cpp:290`: `maxW = 280;` in the `CrumbButton` constructor also applies to the last crumb.
  - `LibraryPane::OnLayout` (`Shell.cpp:466-467`) resets `maxW` only for non-last crumbs.
- **Fix:** set `maxW = 0` (unbounded) for `last`. Set `tooltip = text` on any crumb whose text is wider than its rect.
- **Fix (r5-fix-library, fixed):** `CrumbBar` (`src/app/Shell.cpp`) gives the current crumb all the free space before cutting it, and every cut crumb gets its full name as `tooltip`. The long playlist: `CrumbButton [342,63 546x24] … trunc tip="Любимые видео 🎬 …"` (base `280x24`, no tip, ~130 DIP empty) (`new-pl-created`).

### LB-04: the "…" crumb is dead (Ugly, confirmed)

- **Steps:** `nav videos %TEMP%\lumen-qa\worst\Videos\Nested\Level 1\…\Level 12` in the default window.
- **Actual:** the header shows `… › Level 9 › Level 10 › Level 11 › Level 12`. "…" is `ui::Label [308,64 14x23]` and is not interactive. Levels 1–8 can only be reached by pressing Back several times.
- **Expected:** "…" opens a menu of the hidden ancestors, as in Explorer's address bar or Windows 11's BreadcrumbBar.
- **Evidence:** `nest12-1280.tree.txt`, `nest12-1024.tree.txt`, `crop-nest12-1024.png`.
- **Cause:** `src/app/Shell.cpp:428`.
- **Fix:** make it a small `LinkButton` whose `onClick` opens `ShowMenu` with `crumbs[0 .. start-1]`. The same change can carry the baseline #8 fix: drop to `keep = 2` when tight, so the current crumb never goes below its text width.
- **Fix (r5-fix-library, fixed):** "…" is a `LinkButton` (tooltip "Показать весь путь") that opens a menu of the hidden ancestors. 12 levels at 1280: `… › Level 8 … Level 12`; the menu lists Видео, Videos, Nested, Level 1 … Level 7 and each item navigates (`new-nest12-menu`).

### LB-05: the counts column on Artists/Genres wastes the row (Ugly, confirmed)

- **Steps:** in r5le-2, `nav music artists`, then `click tip=Список`.
- **Actual:**
  - Title cell `box=[270,…,556x42]` and count cell `box=[842,…,366x42]`, left-aligned.
  - "Die Ärzte & Sigur Rós feat. Ólafur Arnalds, Mötley Crüe and the Orchestre Philhar…" is cut, while x 900–1260 of every row is empty.
  - Folder, playlist and album rows put their count right-aligned at the row end, so the two kinds of list look different.
- **Evidence:** `plur-albums-list.png`, `plur-albums-list.tree.txt`.
- **Cause:**
  - Artist and Genre entries carry the count as `subtitle` with `count = -1` (`src/app/library/LibMusic.cpp:632-637`).
  - So `PaintRow` gives the title only 60 % of the flex width (`src/app/library/ItemGrid.cpp:510-514`).
  - `ItemGrid.cpp:491-493` formats `CountSongs` only for `EntryKind::Album`.
- **Fix:** set `e.count` for Artist and Genre rows. In `ItemGrid.cpp:491` use `CountSongs` for Album, Artist and Genre. The title then gets the whole flex width and the count sits at the right edge, like folders. Optionally show "N альбомов · M песен" for artists, as MusicBee and Plex do.
- **Fix (r5-fix-library, fixed):** Artist and Genre list rows carry `count`, so the count is right-aligned at the row end like folders and the name takes the flex width: name `762` wide, "5 песен" `[1064,… 160]` (base name 556, count column 366 at x 842) (`new-artists-list`).

### LB-06: no tooltips in ItemGrid (Fragile→Ugly, confirmed)

- **Steps:**
  1. In r5le-2, `nav videos %TEMP%\lumen-qa\worst\Videos\Long names`.
  2. `in -Move 700,250;;in -Move 705,252;;sleep 1200`.
- **Actual:**
  - No `TooltipBubble`, while the hover tooltip of a NavItem does appear in the same way (`rail.png`).
  - The dump shows the card title "Supercalifragilistic…" with `trunc`.
  - The same goes for "Неизвестный исполнит…" on every untagged music card on Home (`sheet-scan.png`), and for songs-table cells at narrow widths.
- **Evidence:** `tip-card.tree.txt`.
- **Cause:** `src/app/library/ItemGrid.cpp` never sets `tooltip`. Items are painted, not widgets, and `Host::UpdateTooltip` (`src/ui/Host.cpp:917-951`) reads only `hot_->tooltip`, which is fixed per widget.
- **Fix:**
  - In `ItemGrid::UpdateHover`, set `tooltip` to the hovered entry's full title (+ subtitle) when its painted text was ellipsized. Clear it otherwise.
  - The host must re-arm when the text changes for the same hot widget: compare the text in `UpdateTooltip`, not only the pointer.
- **Fix (r5-fix-library, fixed):** `ItemGrid` notes while painting the hovered item whether any of its texts was cut and sets `tooltip` to title / subtitle / album; the Host re-arms its timer when the hot widget changes its text (same 550 ms delay). "Long names", hover a card: `ItemGrid … tip="Supercalifragilistic…"` and `TooltipBubble [700,268 319x63]` with the full name (base: none) (`new-longnames-tip`). Also covers baseline #19 for library grids and rows (queue rows are in the player panel, not changed here).

### LB-07: the song-list summary is clipped by the window (Ugly, confirmed)

- **Steps:** in r5le-2, `nav music songs;;size 720,440`.
- **Actual:**
  - `Label [599,136 137x20] "1355 песен · 9:31:45"` lies outside its toolbar row (228..700) by 36 DIP. Audit: `clip-parent`.
  - The capture shows "1355 песен · 9:31:4", cut mid-glyph at the window edge with no ellipsis.
- **Evidence:** `songs-min.png`, `songs-min.audit.md`.
- **Cause:** `src/app/library/LibMusic.cpp:171-178`. The toolbar row is "Play All" + "Shuffle" + `Spacer` + `summary_`. The label keeps its natural width, and the row neither shrinks nor wraps it.
- **Fix:** give `summary_` `width = Length::Grow()` with ellipsis, plus the full text as a tooltip. Or hide the duration part, then the whole label, below ~560 DIP. The Shuffle button could also become icon-only there.
- **Fix (r5-fix-library, fixed):** The summary label takes the rest of the toolbar row, right-aligned; when "1355 песен · 9:31:45" doesn't fit it shows "1355 песен", then ends with "…", always with the whole line as tooltip. 720×440: `Label [591,136 109x20] text="1355 песен" tip="1355 песен · 9:31:45"` inside the row (base `[599,136 137x20]`, 36 DIP past the row, cut mid-glyph); 1024: the full line (`new2-songs-min`, `new2-songs-1024`).

### LB-08: identical names can't be told apart (Ugly, confirmed)

- **Steps:** in r5le-2, `nav home`, click the search box (1060,75), then `type clip`, then `type intro`.
- **Actual:**
  - "Видео · 2": two cards titled "Clip" with identical thumbnails, from `Same name A` and `Same name B`.
  - "Музыка · 3": three identical rows "Intro · Various · Intro · 0:04".
  - Nothing shows the folder, and LB-06 removes the tooltip as a fallback.
- **Expected:** Plex and Jellyfin add the folder or library as a subtitle when titles collide. foobar2000 shows the path column.
- **Evidence:** `same-search.png`, `same-intro.png`, `sheet-same.png`.
- **Cause:** `MediaCard` (`src/app/library/LibraryCommon.cpp`) and the result builders in `src/app/library/LibHome.cpp:540-575` set no subtitle for videos, and set artist/album for songs.
- **Fix:** after building a result set (search, Recent, Home rows), find entries with equal `title` and set `subtitle` (or `extra`) to the parent folder name. If the folder names are equal too, add the grandparent.
- **Fix (r5-fix-library, fixed):** `Disambiguate` (in `ItemGrid::SetEntries`, so every grid) adds the folder name to the subtitle of entries whose title, subtitle and album all match (the two folders above, or the folder path, when the names match too). Search "clip": "Clip / Same name A", "Clip / Same name B"; "intro": rows "Various · Same title A/B/C", album cards "Various · Same title A/B/C" (base: identical) (`new2-same-clip`, `new2-same-intro`).

### LB-09: "New playlist" from a file shows an empty file list (Ugly, confirmed)

- **Steps:**
  1. In r5le-1 (`media`): `nav videos %TEMP%\lumen-qa\media\Videos`.
  2. `in -Click 400,226 -Right`, hover "Добавить в плейлист", click "Новый плейлист…" (780,374).
  3. `type Тест`, click "Создать".
- **Actual:**
  - The dialog says "Эти файлы будут добавлены в плейлист." over an empty `FileDropZone`.
  - After Create, `playlists.json` holds `4K Mandelbrot.mp4`. The file was added, but the dialog never said so.
- **Evidence:** `addpl-typed.tree.txt`, `%TEMP%\lumen-qa\r5le-1\playlists.json`.
- **Cause:**
  - `src/app/dialogs/Dialogs.cpp:753-778`: `ShowNewPlaylistDialog(done)` has no parameter for initial items.
  - The caller adds them in `done` (`src/app/library/LibraryCommon.cpp:396`).
- **Fix:** pass the items (`std::vector<QueueItem>`) into `ShowNewPlaylistDialog`, pre-fill the zone's list with them, and let the user remove them. Or replace the zone with a line such as "Будет добавлено: 4K Mandelbrot".
- **Fix (r5-fix-library, fixed):** `ShowNewPlaylistDialog` takes the files it was opened for and lists them in the drop zone (removable); Create adds the ones left, with title and length. `FileDropZone … text="4K Mandelbrot.mp4" … "Выбрано: 1"`; after Create `playlists.json` has the file with `"duration": 8` (`new-addpl2`, `%TEMP%\lumen-qa\r5fb\..\r5fb-new-k\playlists.json`).

### LB-10: a stale rail tooltip over the expanded sidebar (Ugly, confirmed)

- **Steps:**
  1. In r5le-2: `action media.sidebar` (collapse), then `in -Move 32,160;;in -Move 33,161;;sleep 1200`. The tooltip "Музыка" shows beside the rail: correct.
  2. `action media.sidebar` (expand; the same as Ctrl+B), `nav music songs`, `size 720,440`.
- **Actual:**
  - `--- popup TooltipBubble [56,146 66x29] "Музыка"` is drawn on top of the expanded item's own label "Музыка".
  - It survives the expand, a route change and a resize.
- **Evidence:** `songs-min.png` (top left), `songs-min.tree.txt`.
- **Cause:**
  - `Host::UpdateTooltip` (`src/ui/Host.cpp:917-920`) acts only when the hot *pointer* changes.
  - The NavItem stays hot while its `tooltip` is cleared by `NavSidebar::SetCollapsed` (`src/app/Shell.cpp:258`), so the bubble is never hidden.
- **Fix:** `SetCollapsed` calls `host->HideTooltip()`. More generally, in `UpdateTooltip`, hide when `tooltipFor_->tooltip` is empty or differs from the bubble's text, and also on `DoLayout` when the target's rect moved.
- **Fix (r5-fix-library, fixed):** `Host::UpdateTooltip` also runs after each frame and each mouse move: a cleared tooltip, a changed text or a moved widget hides the bubble. Collapse, hover "Музыка", expand, `nav music songs`, 720×440: no popup (base: `TooltipBubble [56,146 66x29] "Музыка"` stays) (`new-rail-after`).

### LB-11: Sort and View buttons on empty pages (Ugly, confirmed; round 4 W-12 still open)

- **Steps:**
  1. Launch r5le-4 with `-Library %TEMP%\lumen-qa\r5le-media\empty`, then `nav videos` and `nav playlists`.
  2. In r5le-2, an empty playlist: `pl-created`.
- **Actual:**
  - The empty Videos page shows `IconButton [828,59] icon=sort` and `[864,59] icon=list`. So does empty Playlists, and so does the empty playlist.
  - Empty Music hides both.
- **Evidence:** `empty-videos.tree.txt`, `empty-playlists.tree.txt`, `sheet-empty.png`.
- **Cause:**
  - `src/app/Shell.cpp:441,444` test `n != 0`.
  - `Page::ItemCount` returns `count_`, which `src/app/library/Views.cpp:383` sets to −1 on an empty state, to hide "0 элементов". −1 passes `n != 0`.
- **Fix:** separate "hide the counter" from "has items". For example, add `virtual bool IsEmpty()` to `LibraryView` and use `!view_->IsEmpty()` for `mode_` and `sort_`.
- **Fix (r5-fix-library, fixed):** `LibraryView::IsEmpty()` (a Page with its empty state shown) hides Sort and View. Empty Videos and empty Playlists: no `icon=sort` / `icon=list` (base: both at 828/864); the new empty playlist: none (base: sort + grid) (`new-empty-videos`, `new-empty-playlists`, `new-pl-created`). W-13 re-check: see LB-16.

### LB-12: a playable file is labelled "Повреждён" (Ugly, confirmed)

- **Steps:**
  1. Launch r5le-6 with `-Library %TEMP%\lumen-qa\worst` and wait for the scan.
  2. `nav videos %TEMP%\lumen-qa\worst\Broken`.
  3. `open …\Broken\Audio with a video extension.mkv;;sleep 1500;;state`.
- **Actual:** the card is dimmed with the "Повреждён" chip, yet `state` reports `player=1 | file=1` and the file plays. It is an MP3 with an `.mkv` name. The other two files (0 bytes, text) are rightly damaged.
- **Evidence:** `broken-grid.tree.txt`.
- **Cause:** `src/library/MediaLibrary.cpp:181`: `it.damaged = headRead && !HeaderMatchesExtension(...)`. A mismatch between header and extension is treated as damage, but mpv sniffs the content.
- **Fix:** on a mismatch, classify by the sniffed header instead (MP3/ID3 → audio, playable) and keep "damaged" for headers that no known format matches. Optionally show a softer "неверное расширение" hint.
- **Fix (r5-fix-library, fixed):** A header that doesn't match the extension is damage only when it is no known media container either (`HeaderLooksLikeMedia`: MP4/MOV, Matroska, RIFF, FLAC, Ogg, ID3/MPEG audio, ASF, FLV, MPEG-PS); cached "damaged" entries are re-checked. `Broken`: 2 "Повреждён" chips (empty .mp4, text .mp4), the MP3 named .mkv is a normal card (base: 3) (`new-broken`). New core_tests cases.

### LB-13: duration badges don't line up (Ugly, confirmed)

- **Steps:** in r5le-1 with the seeded history: `nav home;;size 1920,1080`.
- **Actual:** in Continue Watching, "10:00", "1:30" and "0:30" sit at y≈367, while "0:12" on the next-episode card (no progress) sits at y≈371.
- **Evidence:** `home-seeded.png`, `crop-badges.png`.
- **Cause:** `src/app/library/ItemGrid.cpp:359`: `by = t.Bottom() - 8 - bh - (e.progress > 0 ? 4 : 0)`.
- **Fix:** keep the badge at one height. Use `- 12` always, which clears the 4-DIP bar, or draw the bar under the image instead of over it.
- **Fix (r5-fix-library, fixed):** The badge (and the damaged chip) sits 12 DIP above the image bottom on every card. 1920×1080 Home: "10:00", "0:12", "1:30", "0:30" all at y=338 (base 357 / 361) (`new-home-seeded`).

### LB-14: the right edge of counts varies by page (Ugly, confirmed)

- **Actual:**
  - Recent group headers right-align "4 элемента" and "1 элемент" at x=1260, but the "Последний раз" column ends at 1224.
  - Network rows ("0 выпусков", "5 станций") end at 1260.
  - Videos folder rows ("3 элемента") end at 1224.
  - On one page, adding the first favourite shifts every column left by 28 DIP: the heart slot appears.
- **Evidence:** `recent-all.png`, `net-root.tree.txt`, `videos-list.png`, `sheet-fav.png`.
- **Cause:**
  - `ItemGrid::PaintHeader` uses the full row width.
  - `PaintRow` reserves the "⋯" button (36) and the heart slot (28) only when `anyFavorite_` (`src/app/library/ItemGrid.cpp:546-551`).
- **Fix:** right-align group counts to the same `right` that `MediaCells` computes. Reserve the heart slot whenever the page can show favourites, so the columns don't jump.
- **Fix (r5-fix-library, fixed):** List rows always reserve the "⋯" slot (when the grid has menus) and the heart slot, and group-header counts end at the same edge. Recent "4 элемента" ends at 1196 = the date column's end; Videos folder "3 элемента" 1036..1196; Network "5 станций" 1036..1196 (base 1260 / 1224 / 1260) (`new-recent`, `new-videos-list`, `new2-network`).

### LB-15: the destructive button gets default focus (Fragile, confirmed)

- **Actual:** `ShowConfirm` focuses OK (`src/app/library/LibraryCommon.cpp:507`: `w->SetFocus(okBtn, true)`), so Enter confirms "Удалить".
- **Fix:** for destructive confirms, focus Cancel (Windows UX guideline for irreversible actions), or add a `destructive` flag.
- **Fix (r5-fix-library, fixed):** Every `ShowConfirm` (all of them remove something) focuses Cancel with its focus ring: Enter or Space cancels, Tab reaches the action. Enter on the delete-playlist confirm left the playlist in `playlists.json` (`new-pl-delete`: `DButton [440,451 100x32] focus text="Отмена"`). Note: the audit's "Delete + Enter removes a station" now cancels; Delete, Tab, Enter removes.

### LB-16: the "Ничего не найдено" text echoes an unbounded query (Fragile, confirmed)

- **Steps:** in r5le-2, `nav home`, click search, then type a 150-character query.
- **Actual:**
  - The paragraph (`Label [511,448 458x100]`) is 5 lines long and quotes the whole query.
  - A 1000-character paste would fill the page.
  - The last line is the orphan "слово.".
- **Evidence:** `search-long.png`.
- **Fix:** ellipsize the quoted query to about 40 characters. Use a balanced wrap, or a shorter second sentence.
- **Fix (r5-fix-library, fixed):** The quoted query is cut at 40 characters with "…" (`ShortQuery`), and "слово."/"word." are joined to the previous word with a no-break space (W-13). Long query: `Label [531,468 418x60]` "…запрос, который…» ничего не нашлось. Попробуйте другое или более короткое слово." (base 441x80 with the whole query) (`new-long-query`, `crop-new-long-query.png`).

### LB-17: two dialog styles side by side (Fragile, confirmed)

- **New playlist:** `dlg::Frame` 713×573. Cancel is `DButton 100x32` at the left, Create is at the right, the field title "Плейлист" is a heading, and the header is 50 DIP.
- **Rename and Delete:** `ui::Dialog` 480 or 440 wide. Cancel is `Button 86x32` and is a ghost button next to OK, the field label "Название" is grey body text, and the header is 52 DIP.
- **Evidence:** `crop-create.png` vs `crop-rename.png`.
- **Fix:** build the prompts (`ShowTextPrompt`, `ShowConfirm` in `LibraryCommon.cpp:461-509`) on the DialogKit `Frame`/`DButton`, so one button order and one size are used everywhere.
- **Fix (r5-fix-library, fixed):** `ShowTextPrompt` and `ShowConfirm` are built on `dlg::Frame` / `DButton`: Cancel on the left, the action on the right, 100x32 buttons, the field title in the dialogs' SoftText style, Enter submits a prompt (`new-pl-delete`: `app::dlg::Frame [420,277 440x226]`).

### LB-18: extra frames per wheel notch (Fragile, suspected)

- **Steps:**
  1. Launch with `LUMEN_TRACE=1` and a private `TMP`/`TEMP` (`%TEMP%\lumen-qa\r5le-tr`) on the worst library.
  2. `nav music songs`, then a single `in -Wheel 740,500 -Delta -1`.
- **Actual:**
  - The trace shows 23 + 43 frames in the next ~2 s.
  - The scroll itself animates for 180 ms. The bar fades in over 80 ms, then out over 250 ms after 900 ms. Those should account for about 25 frames.
  - With 20 notches about 1.3 s apart, ~55 fps ran continuously, and Lumen used 5.06 s of CPU over ~20 s.
- **Suspected cause:** cover or thumbnail requests for rows scrolling into view (1284 untagged tracks without art), each completion invalidating the grid. This is not confirmed.
- **Fix to try:**
  - Coalesce `imagesConn_` invalidations to one per frame.
  - Remember "no art" per path, so rows don't re-request.
- **Fix (r5-fix-library, fixed):** Confirmed: one notch rendered 43 frames (1284 tracks) and 45 / 41 (6000 songs) at 143 Hz: the 180-ms scroll (~26 frames) plus a 250-ms scrollbar fade-out that rendered every vsync (~36). The fade-out is now four 50-ms steps (4 frames). After: 28 and 32 frames (incl. one flush frame), UI thread 116 → 56 ms and 139 / 113 → 58 ms per notch; frames stop when the scroll animation ends. Thumbnail completions were not the cause (`%TEMP%\lumen-qa\r5fb\perf-*.txt`).

## Design gaps (vs Plex, Jellyfin, foobar2000, MusicBee, WMP 2022, Kodi)

These are not bugs. The fixers should take them only if the owner wants them.

| ID | Gap | Reference |
|---|---|---|
| G-01 | No type-ahead in grids or lists. Typing a letter does nothing (`ItemGrid` has no `OnChar`) | Explorer, foobar2000, MusicBee jump to the first match |
| G-02 | Single selection only: no Ctrl/Shift multi-select, so you can't add 20 songs to a playlist or queue at once | All of them |
| G-03 | The watched marker is only 0.55 opacity, close to the 0.4 used for damaged files. There is no check mark or "unwatched" dot. List rows show progress as a 2-DIP line on a 32-DIP thumbnail, invisible over bright frames (`crop-list-prog.png`) | Plex: check mark / orange triangle; Jellyfin: check badge |
| G-04 | The context menu has no "Сведения" (info), "Удалить из медиатеки" or "Сбросить позицию". "Mark as Unwatched" deletes the whole history entry (play count, last played; `LibraryCommon.cpp:425-428`) | Plex, Kodi |
| G-05 | In library fullscreen the title bar hides and there is no reveal, so there is no visible way out (F11 or Esc only) (`MainWindow.cpp:67-69`; baseline `videos.fullscreen`) | WMP 2022 shows the bar on top hover |
| G-06 | The sidebar doesn't auto-collapse at narrow widths. At 720 it takes 200 of 720 DIP, and the songs table gets 472 (`songs-min.png`) | Windows NavigationView "compact" below 1008 |
| G-07 | Home cards stretch: at 2560 a video card is 474 DIP wide and only 4 fit. DESIGN.md §4 says a 316 card on a 336 step | Plex keeps the card size and shows more |
| G-08 | "Неизвестный исполнитель" fills the second line of every untagged card and is ellipsized ("Неизвестный исполнит…") | MusicBee and Plex leave it blank |
| G-09 | The empty playlist page has no "Добавить файлы" button; the hint only says to right-click elsewhere. Empty Music has no "Пересканировать" while empty Videos has one | Jellyfin, MusicBee |
| G-10 | Artist rows show only songs, not albums | MusicBee, Plex |
| G-11 | The default "Songs" sort is album/track, but the arrow sits on the "№" column, so it reads as "sorted by track number" | foobar2000 shows the sort as "Album / Track" |

## Not tested and why

- **Real drag and drop from Explorer onto the playlist drop zone:** OLE drag needs the real mouse.
- **Clicking the CoverTile, the FileDropZone or "Импортировать плейлист":** each opens a native file picker (hard rule).
- **Playing radio stations or URLs:** that is a network service (hard rule). Only the Network and Radio pages were inspected.
- **Hover tooltips on caption buttons:** these depend on `mouseInside_` and are covered by the baseline hit-map.
- **Sweeping all four scales × five sizes × two themes with my own instances:** the baseline sweep (same exe) already covers the matrix for home, videos, music, playlists, recent, network, search and the worst-case sets, and has 0 tree findings at 1.25/1.5/2.0. I reviewed its captures (`sheet-scale.png`, `sheet-fs.png`, `sheet-music.png`) instead of re-running them.
- **Persistence of the drag-reorder across a restart:** `playlists.json` was not yet flushed when I checked. Left for a follow-up (possible deferred save, not confirmed).

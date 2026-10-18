# Round 5, layer 4: flows (interaction, state, timing)

Hunter: r5-flows (branch `worktree-agent-a9e0c0e308faecc5c`).

- **Exe.** `%TEMP%\lumen-qa\r5-base\Lumen.exe`, prebuilt; nothing was rebuilt.
- **Instances.** `%TEMP%\lumen-qa\r5fl-1` … `r5fl-5`, all `--automation`.
- **Input.** Posted with `PostMessage` from an in-process helper. It does the same thing as `tools\dev\input.ps1`, plus raw `WM_CAPTURECHANGED` and contact-sheet frame grabs.
- **Evidence.** `%TEMP%\lumen-qa\r5fl\` (file names below).
- **Screen.** 2560x1440 at 100 %.

Line numbers refer to the worktree source at d2b6b81.

## Summary

| ID | Sev | Status | Ch. | Finding |
|---|---|---|---|---|
| FL-01 | Broken | confirmed | 1 | In fullscreen the side panel (Ctrl+B) is drawn but dead: clicks, double clicks and the wheel go to the video under it. |
| FL-02 | Broken | confirmed (code) | 1, 2 | Dropping files on the window, even onto the open queue, **replaces** the whole queue. The queue's own empty-state text says to drop files to add them. |
| FL-03 | Broken | confirmed | 2 | Zoom mode "1:2 / 1:4 / 1:1 / 2:1" leaks into the next file: it shows at 1:1, cropped, while the panel says "По размеру окна". |
| FL-04 | Ugly | confirmed | 4 | A click outside a context menu closes the menu **and** acts on what is under it. Dismissing the video menu pauses or resumes the film. |
| FL-05 | Broken | confirmed | 6 | In the player's side panel, any section in the rail (Главная, Видео, Музыка…) throws you out of the player into the library. |
| FL-06 | Ugly | confirmed | 1 | Keyboard editing of the queue: Up/Down change the volume while the queue is open, and the row that Del removes is invisible after a mouse action. |
| FL-07 | Fragile | confirmed | 1 | Losing mouse capture mid-drag (Alt+Tab, a toast, a UAC prompt) **commits** the queue reorder instead of cancelling it. |
| FL-08 | Ugly | confirmed | 3 | When the seek bar is narrower than the preview (small windows), the preview is pinned in place, and the code calls `std::clamp` with lo > hi, which is undefined behaviour. |
| FL-09 | Ugly | suspected | 3 | The seek preview never shows the exact frame on a file with one keyframe (4K Mandelbrot), even after a 2 s rest, although the code promises one. |
| FL-10 | Fragile | confirmed | 2 | Flip (Отражение) carries over to the next file while rotation, zoom and aspect reset. The rule is inconsistent. |
| FL-11 | Ugly | confirmed | 4 | A submenu opened while filtering a menu stays open, detached from its row, after Esc clears the filter. |
| FL-12 | Fragile | confirmed | 1 | After "×" removes a queue row, the next row slides under the pointer with its "×" already armed: a double click removes two rows. |
| FL-13 | Ugly | confirmed | 2 | At the end of the last file the bar reads 0:07 / 0:08, not 0:08 / 0:08. |
| FL-14 | Ugly | confirmed | 6 | Restart does not bring back the side panel or the library section: it always opens on Главная with the panel closed. |
| — | Info | — | 6 | Maximized restore can't be tested under `--automation` (`App.cpp:267` forces `SW_SHOWNOACTIVATE`). Window size and position, resume position, bar style and theme do persist. |

### Charter 1 conclusion: the owner's "can't edit the queue in fullscreen"

**The queue popover itself (Ctrl+L, or the bar button in the floating and minimal styles) works in fullscreen.** I tested, in fullscreen:

- drag-reorder, "×" remove, the row context menu ("Убрать из очереди"), Del, Esc layering and wheel scrolling;
- playing and paused, after the chrome had auto-hidden, after F toggles from windowed and from maximized, and with the minimal bar.

I read every real-mouse path that posted input skips (`src/ui/Host.cpp`):

- WM_NCHITTEST (713-740): the popup rect answers HTCLIENT first.
- WM_SETCURSOR (1154-1164): it only picks the cursor; HTERROR comes from the disabled video child.
- TrackMouseEvent/WM_MOUSELEAVE (1084-1112): the only window under the cursor is the host. The video child is `WS_DISABLED` (`MainWindow.cpp:110`), so its input goes to the parent, and the same is true windowed and fullscreen.
- Capture (548-556, 797, 1129).
- The cursor hide (`MainWindow.cpp:298-329`): it never hides while a popup is open, and keys un-hide it.
- Z-order: popups are drawn in-window by DComp, so there is no separate popup window to lose topmost.

None of these behaves differently in fullscreen than windowed. That matches round 4 and `hitmap.ps1`.

**What does break only in fullscreen is the side panel (FL-01).** If the owner opened it with Ctrl+B or the bar's sidebar button and tried to work there, nothing he clicked could respond. Each click went to the video, so it paused or played, and a double click left fullscreen. That reads exactly as "nothing worked, only outside fullscreen".

Two more things make queue editing in fullscreen feel broken even in the popover:

- **FL-06.** The keyboard doesn't reach the list: Up/Down change the volume.
- **A single click on a row plays it.** VLC, MPC-HC and PotPlayer select on a single click and play on a double click. An attempt to "select a row to move or remove it" instead switches the video.

**Recommendation.** Fix FL-01 first. Then ask the owner which surface he used (the side panel, the popover, or drag-and-drop, FL-02).

## Findings

### FL-01 (Broken, confirmed): the side panel is unclickable in fullscreen
- **Steps.** Open 5 videos (r5fl-1), Space (pause), `F`, `Ctrl+B`, then click the "Series S01" row in the panel at client (140,112).
- **Expected.** The panel opens the folder, as in the windowed player.
- **Actual.** The panel doesn't change and the video starts playing: the click was a click on the video. By the same route a double click leaves fullscreen and the wheel changes the volume.
- **Evidence.** `c1-02-fs-side.png` (before) and `c1-03-fs-side-click.png` (after: the panel is unchanged, the video has advanced and the bar has auto-hidden). In the tree, `ui::Panel [0,0 416x1440]` comes before `app::PlayerView::VideoArea [0,0 2560x1440]`.
- **Cause.**
  - `src/app/player/PlayerView.cpp:382-392` adds `side_` before `video_`.
  - `Widget::HitTest` (`src/ui/Widget.cpp:256-261`) tests children last-first, so `VideoArea` wins.
  - Windowed, the two don't overlap (`x += sideW`, line 481). In fullscreen (lines 482-490) the panel lies over the full-size `VideoArea`.
  - The round 4 P-08 fix made the panel opaque but not hittable.
- **Fix.** Add the side panel after `video_` and before the chips and `bar_`, either in the constructor or with `AddChild(std::move(…), index)`. The bar stays last, so it still draws over the panel.
- **Test.** In fullscreen, `InteractiveAt(centre of a panel row)` must be inside `side_`.
- **Fix (r5-fix-flows): fixed.** `PlayerView` now adds `video_`, `music_` and `error_` first, then `side_`, then the PiP overlay, the chips and `bar_` (bar last, so it still draws over the panel). Other overlays checked: the next-episode / skip-chapter chips and the bar were already after the video; toasts, menus and the seek preview are host popups or `PaintOver` of the bar, above every child; the subtitle lift reads the bar rect only. Also: the side panel is hidden in PiP without forgetting that it was open (in PiP it used to cover the whole picture). Evidence (`%TEMP%\lumen-qa\r5ff\`): dump-tree in fullscreen, base `Panel` before `VideoArea`, fixed `VideoArea, ErrorCard, Panel, ControlBar`; Space, F, Ctrl+B, click (140,112): base `fl01-102504-after.png` the video resumed (pause icon), fixed `fl01-62116-after.png` the row is selected and the video stays paused; a double click on the row then acted on the row and fullscreen stayed (`fl01-62116-after2.png`).

### FL-02 (Broken, confirmed by code): dropping files replaces the queue
- **Steps.** Have a queue you built by hand, open the queue popover, and drop one or more files from Explorer onto it, or anywhere on the window.
- **Expected.** The files are added to the queue, as the empty-state text promises: "Add files with the + button or drop them on the window." (`QueuePanel.cpp:248`). PotPlayer, MPC-HC and VLC append when you drop on the playlist.
- **Actual.** `Host` `WM_DROPFILES`/OLE drop → `MainWindow::onDropFiles` (`MainWindow.cpp:153`) → `App::Open` → `PlayQueueItems` → `queue_.Set(…)` (`App.cpp:630-644`). The queue is thrown away and the dropped files start playing. In fullscreen this is the only way to "drop into the queue", and it destroys the queue.
- **Evidence.** This is a code path; a real OLE drop cannot be posted. `App.cpp:563-644` has no append branch.
- **Fix.** When the drop point is over the queue popover or the queue tab, call `queue().Add` (as `AddFilesToQueue` does, `QueuePanel.cpp:458-465`). Better still, show two drop zones in the drop overlay: "Play" and "Add to queue". Also fix the empty-state text if replacing stays the behaviour.
- **Fix (r5-fix-flows): fixed (code-verified for the drop itself).** `MainWindow::onDropFiles`: while the queue is shown (the Queue tab of the list popover, or the mini player's queue) a drop goes to the new `App::Enqueue`, anywhere else to `App::Open` as before (plays and replaces the queue). A drop with the queue open always lands on or beside it, so "dropped onto the queue" and "dropped while it is open" are the same case; the drop point isn't needed (`Host`'s OLE target, not in this scope, doesn't pass it). The side panel has no queue (it is the library: Home, Videos, Music…), so a drop there plays, as on the library. `Enqueue` expands folders and .m3u playlists, keeps "subtitles dropped on a playing video are added to it", reports missing files, starts the first added item when nothing plays, and toasts "Добавлено в очередь: N". While the queue shows, the drop overlay says "Отпустите, чтобы добавить в очередь" (`Panels.cpp` sets `dropHint`), otherwise "Отпустите, чтобы воспроизвести". The empty-state text is now "Добавьте файлы кнопкой «+» или перетащите их сюда." Verification: an OLE drop can't be posted (it follows the real cursor), so the branch is checked by reading; `Enqueue` itself was run through `WM_COPYDATA` with `library.enqueue` on (it shares the function): a two-entry .m3u sent to a playing 2-item queue gave base 3 rows with one row "two", fixed 4 rows with the playlist's two tracks (`fl02-enqueue-m3u.png`).

### FL-03 (Broken, confirmed): zoom mode leaks into the next file and the panel lies
- **Steps** (r5fl-5, 1280x780):
  1. Open "4K Mandelbrot.mp4" then "Chaptered Demo (1080p).mkv".
  2. Gear → Видео → Масштаб → "1:2 Половина".
  3. Esc, then `N`.
- **Expected.** Chaptered Demo fits the window: all 7 colour bars are visible.
- **Actual.** It shows at 1:1, cropped to 4 bars, and quick settings shows "Масштаб: По размеру окна".
- **Evidence.** `c2-zoom-carry-strip.png` (left: 1:2 on the 4K file; middle: the next file cropped; right: the panel claiming "по размеру окна").
- **Cause.**
  - `LiveState::SetZoomMode` (`src/app/player/LiveState.cpp:434-444`) sets `video-unscaled=yes` plus `video-zoom`.
  - `reset-on-next-file` (`src/Player.cpp:179-180`) resets `video-zoom` but not `video-unscaled`.
  - On a file change `LiveState` only resets its label, `zoomMode = "fit"` (`LiveState.cpp:135-141`).
- **Fix.** Add `video-unscaled` to `reset-on-next-file`, or call `SetZoomMode("fit")` in that handler instead of assigning the string.
- **Fix (r5-fix-flows): fixed, with FL-10 under one rule.** The rule: **picture transforms are per file** (flip, rotation, zoom including the 1:x modes, pan, aspect ratio); everything else in the Video tab stays (HDR, tone mapping, resolution, deinterlace and the picture filters are settings or session choices, and crop goes back to its Settings value, as before). mpv's `reset-on-next-file` covered aspect, zoom, pan and rotation but not `video-unscaled`, which the 1:x modes set; it is added (`Player.cpp`). The Video tab now says so under its first section: "Отражение, поворот, масштаб и соотношение сторон действуют только для текущего файла." The video context menu's aspect items and the aspect cycle key now go through `LiveState::SetAspect`, so the tab's label matches them (they set the mpv property behind its back), and the menu marks the current aspect. Evidence (`%TEMP%\lumen-qa\r5ff\fl03-strip.png`, right half): on 4K Mandelbrot Flip "По горизонтали" + Масштаб "1:2 Половина", Esc, N: Chaptered Demo fits the window with all bars in their normal order, and the Video tab shows "Нет" and "По размеру окна". (The left half, the base, had already moved to Chaptered Demo before the clicks; the hunter's `c2-zoom-carry-strip.png` is the before evidence.)

### FL-04 (Ugly, confirmed): the click that dismisses a menu also clicks what is under it
- **Steps.** With the video playing, right-click the video at (500,300) and then left-click the video at (300,500) to close the menu.
- **Expected.** The menu closes and playback continues. Native Windows menus, used by MPC-HC and PotPlayer, swallow the dismissing click, and so do VLC's Qt menus.
- **Actual.** The menu closes and the film pauses. A click outside onto a bar button (Next, Fullscreen) would also fire it.
- **Evidence.** `c4-ctx-pair.png`: the pause icon before, the play icon after, frozen at 0:06.
- **Cause.**
  - `Host::OnMouseButton` (`src/ui/Host.cpp:773-781`) closes non-modal popups and then carries on dispatching the click.
  - Player popovers dodge this with `panel::PanelJustClosed()` (`PlayerView.cpp:224`), but context menus never set `closedAt`.
- **Fix.** Swallow the click that closes a popup with `dismissOnOutside`, so that `return` also covers the non-modal case. Then remove the `PanelJustClosed` hack. Alternatively, add a `PopupOptions::swallowDismissClick` that defaults to true for menus and popovers.
- **Fix (r5-fix-flows): fixed.** `Host::OnMouseButton`: any button-down that closes one or more popups and doesn't land in a popup left open is used up (a click on the parent menu after a submenu closes still acts on the parent). A `WM_LBUTTONDBLCLK` right after such a click is treated as a plain single click, so two quick clicks outside a menu don't toggle fullscreen. The `PanelJustClosed` / `closedAt` hacks in `Panels.cpp` and `PlayerView.cpp` are gone. Evidence (`%TEMP%\lumen-qa\r5ff\`): right-click (500,300) then click (300,500): base `fl04-81392-after.png` paused, fixed `fl04-98760-after.png` still playing; menu open then click on Previous: base restarted the file (0:01), fixed kept 0:24 (`fl04-prev-pair.png`); Ctrl+L, click the bar's queue button, click it again: closes, then opens, with no flicker or reopen (`fl04-queue-toggle-strip.png`). Behaviour change: with a popover open, a click on another bar button now only closes the popover (one more click opens the other one), as with Windows menus.

### FL-05 (Broken, confirmed): the side panel rail leaves the player
- **Steps.** Windowed, play a video, `Ctrl+B`, then click "Музыка" in the panel's icon rail at (28,160).
- **Expected.** The side pane shows Music and the video keeps playing in the player. That is the point of a side panel.
- **Actual.** The window switches to the library and the video drops into the mini player.
- **Evidence.** `c6-01-before-close.png`.
- **Cause.** `NavItem::onClick` (`src/app/Shell.cpp:28-30`) always calls `TheApp().ShowPlayer(false)` before `Navigate`, including for the collapsed rail inside `PlayerView` (`PlayerView.cpp:387-388`).
- **Fix.** Give `NavSidebar`/`NavItem` an "in player" flag, set by `PlayerView`, that skips `ShowPlayer(false)`. If the side pane should keep its own route, give it one; today both panes share `App::Navigate`.
- **Fix (r5-fix-flows): fixed. Choice:** the rail is the side panel's own navigation. The side panel is "the library beside the video" (`PlayerView.h`: "icon rail + compact library list"), its `LibraryPane` shows `App::route()`, and nothing else in the panel can switch sections, so the icons belong there; they must only stop leaving the player. `NavSidebar::SetInPlayer(true)` (set by `PlayerView`) makes `NavItem` navigate without `ShowPlayer(false)`, and in the player the item of the current section is highlighted (it never was, since `Selected()` required the library screen). The route stays shared with the library: going back to the library opens where the side panel was, which reads as one library, not two. The hidden full library no longer takes keyboard focus when the route changes while the player is shown (`LibraryPane::ShowRoute`), so Space and the arrows stay with the player. Evidence: Ctrl+B, click Музыка (28,160): base switched to the library with the video in the mini player, fixed kept the player, the panel shows Музыка with its icon lit, and Up then changed the volume ("Громкость 5 %") (`%TEMP%\lumen-qa\r5ff\fl05-rail.png`).

### FL-06 (Ugly, confirmed): the keyboard can't edit the queue the way the menu says
- **Steps.**
  1. Open the queue (`Ctrl+L`). Up/Down change the volume, and Del does nothing.
  2. Right-click a row, then Esc: focus is now on the list.
  3. Press Down, then Del.
- **Expected.** The arrows move a visible selection and Del removes the selected row. The row menu even shows "Убрать из очереди — Del".
- **Actual.** At first focus is cleared (`Panels.cpp:202`, "keep the keyboard on the player"), so the keys go to the window shortcuts. Once the list has focus, the selection is painted only as a keyboard focus ring (`QueuePanel.cpp:208` requires `ShowFocusRing()`), so after mouse use it is invisible. Del then removes a row the user can't see.
- **Evidence.** `c1-12-pair.png`, `c1-13-crop.png`: Del removed "Series S01E02", the row right-clicked earlier, with no selection visible.
- **Fix.** Always paint the selected row, with a subtle fill when the list isn't focused. Give the list focus when the popover is opened on the Queue tab with Ctrl+L, but leave Space to play and pause.
- **Fix (r5-fix-flows): fixed**, together with the single-click behaviour. In the queue (popover and mini-player queue) a single click selects the row, a double click or Enter plays it (the playing row resumes if paused), Del removes the selected row, Up/Down/Home/End move the selection, Ctrl/Alt+Up/Down move the row. The selected row is always filled (`bgSelected`, half strength while the list isn't focused). The list takes keyboard focus when the queue opens (Ctrl+L, the bar or mini-player button) or its tab is chosen (`Panels.cpp` `FocusQueue`), and when clicked; Space still reaches the window (play/pause), Left/Right still seek. Arrows are consumed by the list even when it is empty, so they never change the volume there. Drag-reorder by the handle is unchanged. Evidence (`%TEMP%\lumen-qa\r5ff\`, 5 videos, Ctrl+L): Down, Down, Up×3 then Down, Del: base `fl06-48280-keys.png` shows the "Громкость 15 %" toast and Del did nothing; fixed `fl06-32244-keys.png` / `-del.png`: no volume change, the selection moved and Del removed the selected row (`fl06-keys-strip.png`). Single click on "Chaptered Demo": base played the clicked row, fixed only selected it and kept playing Long Seek Test at 0:22; a double click then played it (`fl06-click-strip.png`).

### FL-07 (Fragile, confirmed): losing capture during a queue drag commits it
- **Steps.**
  1. In the queue, press the handle of row 0 at (1276,552) and move it down 90 px.
  2. Post `WM_CAPTURECHANGED` (lParam 0), as Alt+Tab, a toast or UAC would.
- **Expected.** The drag is cancelled, as in Explorer.
- **Actual.** The row is moved to where the pointer was.
- **Evidence.** `c1-14-crop.png`: Series S01E01 moved from row 0 to row 2.
- **Cause.** `Host.cpp:1129-1137` calls `OnMouseUp` on capture loss, and `QueueList::OnMouseUp` (`QueuePanel.cpp:325-333`) commits.
- **Fix.** Pass a `cancelled` flag, or add `OnCaptureLost()`, and have `QueueList` drop the drag. The same applies to the slider drags.
- **Fix (r5-fix-flows): fixed** for the queue. `MouseEvent` got `cancelled` (`src/ui/Widget.h`, one field), `Host`'s `WM_CAPTURECHANGED` path sets it, and `QueueList::OnMouseUp` drops the drag (and a pending "×" click) when it is set. The sliders in `src/ui/Controls.cpp` are another agent's file and were left alone; the seek bar keeps committing on capture loss, which only seeks. Evidence: handle of row 0 pressed at (896,186), moved 90 px, `WM_CAPTURECHANGED` (lParam 0) posted, then the release: base moved S01E01 to row 2, fixed put it back at row 0 (`fl07-strip.png`: mid-drag and after, base then fixed).

### FL-08 (Ugly, confirmed): the seek preview is pinned in small windows and calls `std::clamp` with lo > hi
- **Steps.** At 720x440, hover the seek bar at its left end (252,412) and at its right end (421,412).
- **Expected.** The preview follows the pointer, clamped to the window or the bar.
- **Actual.** The 190 px preview sits at the same x for every position.
- **Evidence.** `c3-02-seek-edges.png`, `c3-03-seek-preview-3pos.png`.
- **Cause.** `SeekPreview.cpp:493` calls `std::clamp(x - w / 2, track.x, track.Right() - w)` with `track.w` (182) < `w`. This is undefined behaviour, and MSVC debug builds assert "invalid bounds arguments passed to std::clamp".
- **Fix.** Clamp to the bar or client rect, e.g. `std::clamp(x - w/2, 8, clientW - 8 - w)`, and guard lo <= hi. The same pattern is at `ControlBar.cpp:642` for the time tip (safe today because the tip is narrow).
- **Fix (r5-fix-flows): fixed.** New `ClampedLeft(x, w, track, bounds)` (`SeekPreview.h/.cpp`): the box stays over the bar while the bar is wider than it; otherwise it follows the pointer inside the window, 8 DIP from its edges; `std::clamp` is only called with lo <= hi. The preview and the time tip both use it (`SeekBar::PaintOver` passes the client rect). The header comment of `SeekPreview.h` described a "short-lived encoder instance" and now describes the persistent render-API instance (audit, stale comment). Evidence at 720x440 (seek bar 182 DIP), hovering x = 252, 336, 421: base, the preview at the same place three times; fixed, it moves with the pointer (`%TEMP%\lumen-qa\r5ff\fl08-strip.png`, base then fixed).

### FL-09 (Ugly, suspected): the exact seek-preview frame never arrives
- **Steps.** "4K Mandelbrot.mp4" (one keyframe, at 0.000, checked with ffprobe). Hover 0:04, 0:07 and 0:08 and rest 1.5–2 s on each.
- **Expected.** The exact frame once the pointer rests (`SeekPreview.cpp:88`: "an exact frame once it rests").
- **Actual.** The keyframe-0 thumbnail stays for every position, while the main video at 0:07 is a deep zoom.
- **Evidence.** `c3-03-seek-preview-3pos.png`.
- **Unconfirmed.** The rest detection under automation, and whether a slow 4K decode simply takes longer than 2 s.
- **Check (r5-fix-flows): rejected, the exact frame arrives; it is slow on this file.** Run with `LUMEN_TRACE` (preview timings to a private `TMP`): hovering 0:04, 0:06 and 0:07 on the paused 4K Mandelbrot and resting gives "exact decode" 1589 ms, 2286 ms and 2401 ms (decoding 4K from the only keyframe at 0.000 in software). At 0:07 the preview shows the keyframe-0 picture after 1 s and the exact deep zoom after 5.5 s (`%TEMP%\lumen-qa\r5ff\fl09-rest-pair.png`; `fl09-exact-pair.png` for 0:04 / 0:06). The hunter's 1.5–2 s rest was just shorter than the decode. The rest detection works under posted input (the worker times the rest itself, from the last hover change). Limit worth knowing, not changed: an exact decode is abandoned after 4 s (`kFrameTimeoutMs`) and not retried for that time, so on a file with one keyframe and a longer distance to it the exact frame would never come; a longer timeout costs seconds of CPU per rest, so it is a trade-off for the owner, not a bug fix.

### FL-10 (Fragile, confirmed): flip survives a file change; rotation and zoom don't
- **Steps.** Gear → Видео → Отражение "По горизонтали", then `N`.
- **Actual.** The next file is mirrored and the panel still shows "По горизонтали". Rotation, zoom and aspect reset through `reset-on-next-file`.
- **Evidence.** `c2-zoom-carry-strip.png`: the bars run magenta, blue, yellow, green, i.e. reversed.
- **Cause.** Flip is a `vf` (`LiveState.cpp:332-336`) and `vf` persists in mpv across files.
- **Fix.** Pick one rule for all picture transforms. Either all of them are per file (clear `flip` and `ApplyVideoFilters()` in the `kChangeFile` handler) or all of them stick until reset. Document it in the Видео tab.
- **Fix (r5-fix-flows): fixed.** Per file, as rotation, zoom and aspect already were: the `LiveState` file-change handler clears `flip` and reapplies the filter chain (only when a flip was on, so no filter churn otherwise), and the Video tab states the rule (see FL-03). Evidence: `fl03-strip.png`, fixed half: after N the bars run red → cyan again and Отражение shows "Нет".

### FL-11 (Ugly, confirmed): a submenu opened while filtering stays after the filter is cleared
- **Steps.**
  1. Right-click the video and type "суб". The pointer rests on "Субтитры ›", so its submenu opens beside the menu.
  2. Press Esc.
- **Actual.** The filter clears and the full menu comes back, but the subtitle submenu stays open at its old spot. The "Субтитры" row is now far below it. A second Esc closes it.
- **Evidence.** `c4-menu-strip.png`, frames 1–2 (the ✓ and "Shift+O" column left of the menu).
- **Cause.** `Controls.cpp:1167-1170`: Esc clears `filter_` without closing child popups.
- **Fix.** Close submenus when the filter changes.
- **Fix (r5-fix-flows): fixed.** `MenuPopup::Refilter` (`src/ui/Controls.cpp`, one local hunk; the file belongs to r5-fix-audit for its own items) closes the open submenu, cancels a pending submenu timer and clears the highlight before it rebuilds the rows, on every filter change (typing, Backspace, Esc). Evidence: right-click the video, type "суб" (the Субтитры submenu opens beside the filtered menu), Esc: base keeps the submenu at its old spot beside the full menu, fixed shows the full menu alone (`%TEMP%\lumen-qa\r5ff\fl11-strip.png`, base pair then fixed pair).

### FL-12 (Fragile, confirmed): the "×" re-arms under the pointer
- **Steps.** In the queue, click "×" on row 2.
- **Actual.** Row 3 moves up under the pointer and shows its "×" hot at once, so a double click deletes two rows.
- **Evidence.** `c1-12-pair.png`, left: "Long Seek Test" with an armed "×".
- **Fix.** After a removal, keep the remove button cold until the pointer moves. VLC and Spotify need a fresh hover.
- **Fix (r5-fix-flows): fixed.** After "×" removes a row, every "×" stays hidden and inert until the pointer moves 3 DIP from where it clicked; a click there in the meantime only selects the row, and a double click on it does nothing. Evidence: two quick clicks on the "×" of row 0 without moving: base removed two rows (4 → 2), fixed removed one (3 → 2) and selected the next (`fl12-two-clicks.png`); a double click on "×": the next row shows no armed "×" (`fl12-strip.png`, the base shows it armed with its tooltip).

### FL-13 (Ugly, confirmed): end of the last file shows 0:07 / 0:08
- **Steps.** Let the last item of the queue end (4K Mandelbrot, 8 s).
- **Actual.** The bar reads 0:07 / 0:08 with the play icon.
- **Evidence.** `c2-10-bar-after-eof.png`.
- **Fix.** At `eof`, show the position as the duration, or round both values the same way.
- **Fix (r5-fix-flows): fixed.** At `eof` the time label, the remaining time and the seek bar use the duration as the position (`ControlBar.cpp` `ShownPosition`). Evidence: 4K Mandelbrot alone (folder-as-playlist off) left to end: base "0:07 / 0:08", fixed "0:08 / 0:08" (`%TEMP%\lumen-qa\r5ff\fl13-bar-pair.png`).

### FL-14 (Ugly, confirmed): restart forgets the side panel and the library section
- **Steps** (r5fl-4):
  1. Set the window to 1000x620 at 300,200 and seek to 5:01.
  2. Open the side panel and go to Музыка.
  3. Close (WM_CLOSE) and relaunch.
- **What persists.** The window rect (including after closing in fullscreen), resume at 5:02, the bar style and the theme.
- **What doesn't.** The side panel is closed and the library opens on Главная.
- **Evidence.** `c6-02-after-restart.png`, `c6-03-reopen-resume.png`.
- **Note.** The "Продолжить просмотр" tile shows a frame from about 2:20, not the resume point at 5:01.
- **Fix.** Save `player.sidePanel` and the last route in `ui.json`.
- **Fix (r5-fix-flows): fixed.** `ui.json` is the natural place (it already holds the window placement, the bar layout and the remaining-time toggle). `player.sidePanel` ("1"/"0") is written by `PlayerView::SetSidePanel` and read when the player view is built; `library.section` (the section index) is written on every route change and read in `App::Run` before the window is built. Only the section is restored, not the folder or sub-page inside it, since a folder may be gone by the next start. The side panel has no tabs of its own: its content is the library section, so restoring the section covers "the panel's tab". Not addressed: the "Продолжить просмотр" tile's frame (a library thumbnail, outside this scope). Evidence: fresh data dir, Ctrl+B, rail Музыка, `WM_CLOSE`, relaunch with the same file: base opens the player without the panel and the library on Главная, fixed opens the player with the panel on Музыка and Esc shows the library on Музыка (`%TEMP%\lumen-qa\r5ff\fl14-restart.png`; `ui.json` has `"library.section": "2"`, `"player.sidePanel": "1"`).

## Checked and working

### Switching media (charter 2)
- N, P and end-of-file advance with the queue, chapters or subtitles popover open. The window size is kept, the popover stays on its tab, and the title, time and track lists update at once with no stale tracks (`c2-switch-strip.png`, `c2-subs-pair.png`).
- Spamming N 10 times stops at the last item with "Конец списка" (`c5-04-next-spam.png`).
- P after 2 s restarts the current file.

### Rapid input (charter 5)
- 5 and 10 fast F toggles, windowed and from maximized, end in the right state. P-09 is fixed: maximized → F → F gives back -8,-8 2576x1408, zoomed.
- A menu is re-clamped into the window when the window shrinks under it (`c5-14-menu-after-resize.png`).
- A live theme switch repaints the open popover correctly (`c5-10…c5-11`).
- A live switch of the bar style keeps the popover anchored (`c5-style-pair.png`).
- Closing Settings with a dropdown open, then reopening it, is clean.

### Layers and Esc (charter 4)
- Fullscreen + queue + row menu: Esc closes the menu, then the queue, then fullscreen.
- In menus, Esc first clears the filter.

### The control bar (charter 3)
- At 720x440 the default bar fits, with a 182 px seek bar.
- The minimal style keeps the queue button.

### Not done
- Volume persistence: the launcher forces the startup volume to 0.
- URL open: it would need a dialog.
- The real cursor's visibility: under `--automation` the cursor isn't observable.

## Audit items

Items of `qa/AUDIT-PONYTAIL.md` in the player and queue code, taken by r5-fix-flows. Each was confirmed in the code or at runtime before the change.

- **#16 (crash when dragging in the queue): fixed.** Confirmed at runtime: press the handle of the last row, Del: the base removed the row mid-drag and painted the floating row from past the end of the list (an empty row, `%TEMP%\lumen-qa\r5ff\fl16-pair.png`, left; out-of-bounds read, no crash in release). Now a queue change cancels the drag, `PaintOver` checks the index, and the list's keys are ignored (but consumed) mid-drag. Fixed: Del does nothing until the drag ends (`fl16-pair.png`, right).
- **#5 (Record overwrites itself on the next file): fixed.** Confirmed at runtime: Record (Ctrl+Shift+R) on Series S01E01 and let it advance: the base's .mkv was 4.07 MB during E01 and **0 bytes** once E02 started (mpv reopened the same name); pressing Record again then said "Запись сохранена" (the static flag was stale). `stream-record` is now in `reset-on-next-file`, so the recording ends, finalized, with its file, and the action reads mpv's `stream-record` instead of a static flag. Fixed: the file kept its 3.9 MB after the change, and Record afterwards says "Запись началась" (`%TEMP%\lumen-qa\r5ff\audit5-record-again.png`). The recording stops silently at the file change; a toast there would be a new feature.
- **.m3u/.pls queued as one line ("ставить в очередь"): fixed for .m3u.** Confirmed at runtime: with `library.enqueue` on, an .m3u sent to a playing window became one queue row named after the file (`fl02-enqueue-m3u.png`, left). `OnCopyData` now calls `App::Enqueue`, which adds the playlist's tracks (right). `.pls` isn't a playlist Lumen can read at all (`IsPlaylistFile` is .m3u/.m3u8 only, there is no .pls reader), so it is queued as one item that mpv expands, as before; reading .pls is a feature, not this fix.
- **Two volume-wheel rules and touchpad rounding: fixed.** Confirmed in code: over the video `round(volume + step × delta)` with the Settings step and unmute, over the bar's volume `round(volume + 5 × delta)` with neither; a tenth-of-a-notch delta at step 1 rounds back to the same volume. Both now call `WheelVolume` (`Actions.cpp`): the Settings step, unmute, and partial notches accumulate (reset when the direction flips). "Reverse scroll direction" still applies to the video only, as it is a setting of the video mouse. Evidence: step 1, ten `WM_MOUSEWHEEL` of delta 12 over the video: base "Громкость 0 %", fixed "Громкость 1 %" (`%TEMP%\lumen-qa\r5ff\audit-wheel-touchpad.png`).
- **The chapter list repaints every chapter: fixed.** Confirmed in code: `ChapterList` invalidated on every `kChangeTime` and painted all rows. It now paints only the rows in the clip (like the queue) and repaints on a time tick only when the highlighted chapter changes.
- **Repeat One vs All on the bar: fixed.** Confirmed at runtime: with Repeat in the bar layout, L twice (Off → All → One): the bar's button looked the same for All and One. The bar now uses the queue header's `RepeatButton` (the "1" badge, the One/All tooltip; `panel::MakeRepeatButton`), so the copy is gone. Evidence: `%TEMP%\lumen-qa\r5ff\audit-repeat-one-bar.png` (base, then fixed with the "1").
- **Chapter marks beyond the duration: fixed.** Confirmed in code: `SeekBar::Paint` placed `t.x + t.w × time / duration` with no upper bound, so a chapter past a (shorter, real) duration was drawn beyond the bar. Marks at or past the duration are skipped.
- **Per-row history scan in the queue: fixed.** Confirmed in code: every paint of every row called `History::Find` (normalizes and scans up to 500 entries) for the title and again for the duration. Row title, artist and stored duration are now cached per row and dropped when the queue, the library or the playing file's tags change (`QueuePanel.cpp` `RowInfo`).

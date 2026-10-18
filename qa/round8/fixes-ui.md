# Round 8 fixes: UI framework, gfx, MainWindow, Shell (r8-fix-ui)

Findings: qa/round8/raw/ui.md, #1–#12 and the left-out list. V7-08 and V7-10 from
qa/round7/verify.md, and two Shell.cpp leftovers from r8-fix-pages, were added by the coordinator.

Branch: `worktree-agent-ad3784255b88bea11`. Temp prefix `r8fu`. main merged in (0da5024).

Runtime checks ran in background `--automation` instances only, built into the worktree's own
`build\`. The "base" exe is the branch point (4ed6a84) built the same way. Scripts are in the
session scratchpad (`r8fu-*.ps1`).

Checks after the fixes:
- build at /W4: no warnings;
- tests\build-tests.bat: 47 tests, 6435 checks, 0 failures;
- check-icons.py: 0 missing; check-settings.py: 0 without a reader; i18n_check.py: 0 conflicts;
- `tools\dev\sweep.ps1 -Filter '^(home\.|videos\.|music-songs|menu-|dialog-url|search|worst-songs|onboarding-1|settings-playback)'`:
  53 scenarios ok, 0 high, 0 medium, 2 low (music-songs and worst-songs at 1024x640, the same 2 in base);
- base vs fixed runs above for #1, #2, #5, #7, V7-08, V7-10 and the language scroll.

## Numbered findings

| # | Status | How it was checked (before → after) | Where |
|---|---|---|---|
| 1 | fixed | Reproduced first (r8fu-repro1.ps1): Home → Videos, `menu sort`, then Alt+Left posted as WM_SYSKEYDOWN with the automation Alt mod. Base: the menu stays open over Home (`popups=1`), and a click on its second row kills the process (use after free in `Page::SetSort`). The Browser Back key (VK_BROWSER_BACK) and `set general.language en` do the same: the process dies on the click. Fixed: `LibraryPane` closes the popups before it destroys its page, in `ShowRoute` and in the language rebuild. All three paths now leave `popups=0`, and the process lives. Other paths that destroy a subtree were checked: CrumbBar::Set (its menu items capture a Route by value), EmptyState::Set (no popups), Popover tabs, and the settings rebuild (it already calls CloseAllPopups). Menu actions are posted after the menu closes, so a Navigate from a menu item is safe. ControlBar::Rebuild (player, not mine) opens panels by `PanelKind`, not by pointer. | Shell.cpp:476-482, :500, :567 |
| 2 | fixed | LUMEN_TRACE frame stats, with the search box focused and a posted WM_ACTIVATE(WA_INACTIVE). Base: frames every second the whole time the window is inactive (2–3 frames/s, the 530 ms blink). Fixed: 0 frames in 4 s while inactive; after WM_ACTIVATE(WA_ACTIVE) the blink is back. The timer stops itself when the host is inactive and hides the caret; the first paint after activation restarts it. A read-only box never starts the timer. | Controls.cpp:668-690, :902; Host.cpp:1564 |
| 3 | fixed | Code trace; a device loss can't be triggered from a test. `Host::Render` notes the device generation before `Surface::Begin`. If Begin or End recreated the device, every host gets RequestLayout, not only the one that met the loss. The "WARP for the whole session" part is left: RecreateDevice on the UI thread tries hardware, then WARP, and it doesn't start the background hardware thread again. That only happens if hardware creation fails at the moment of the TDR. | Host.cpp:693-697, :719-726, :754 |
| 4 | fixed | Code trace; this PC has one monitor, so the case can't be reproduced. Host::Create sizes the window at the system DPI. If GetDpiForWindow differs after creation, size and centring are now worked out again at the real DPI (before, the old pixel size was re-applied). Regression check at `--scale 1.5` (the same path): Settings and the main window get the same rects as in base (1572x1298 at 494,47). | Host.cpp:318-353 |
| 5 | fixed | r8fu-t5b.ps1, Settings › Interface, with the pointer still on the "Choose a skin file" button and two wheel notches. Base: the button stays `hot` at [948,136], 192 DIP away from the pointer. Fixed: hover is hit-tested again while the scroll runs; the hot widget is the one under the pointer. The audit's ItemGrid case doesn't hold: ItemGrid already re-hovers in Paint (screenshots base/fix are the same). The re-hit only runs after a wheel notch that a widget took, and only sends OnMouseMove to widgets inside that scroller, so the video area (wheel = volume) gets no synthetic moves. | Host.cpp:709-719, :1470-1476, :284 |
| 6 | fixed | Code trace; changing the Windows theme is a system setting, which is off limits. MainWindow handles WM_SETTINGCHANGE "ImmersiveColorSet" and calls App::ApplyTheme under the System theme. ApplyTheme moved to App's public part (App.h hunk). | MainWindow.cpp:535-540, App.h:155-157 |
| 7 | fixed | Tree dump after `set general.language en`. Base: the title bar tips stay "Меню / Боковая панель / Свернуть / Развернуть / Полный экран / Закрыть". Fixed: "Menu / Toggle sidebar / Minimize / Maximize / Fullscreen / Close". The drop hint and the collapsed rail's tooltips are set again from languageChanged as well. | Shell.cpp:186-193, :292; MainWindow.cpp:174 |
| 8 | fixed | Code trace. ScrollView::OnMouseUp returns early unless a thumb drag ends. A blank click no longer calls ScrollTo(target, false), so it doesn't flash the bar or cut a smooth scroll or a track page short. | Controls.cpp:1074-1081 |
| 9 | fixed | Code trace. ShowPopup clears `pressed_` only for popups that take input. A toast or tooltip that comes up during a press lets the click land. | Host.cpp:491-495 |
| 10 | rejected (measured) | LUMEN_TRACE frame stats on the 1 284-song list (worst set): menu fade frames have layout avg 0.02–0.05 ms (max 0.2), paint 0.5–1.0 ms, present ~5 ms; wheel scrolls have layout 0.05 ms. A layout pass per animated frame costs about 1 % of the frame. Laying out only when dirty would break widgets that read animated values in OnLayout: PlayerView's bar opacity and slide (chrome_), MiniPlayer's `overlay_->visible = hover_ > 0.01`, ScrollView's offset. Most of those are in the player, outside this scope. Not changed. The other cost, that every window renders while any animation runs, is bounded by the animation (≤ 320 ms) and was left too. Done instead: TickAll no longer copies the active list every frame. | Host.cpp:707; Anim.cpp:83 |
| 11 | fixed | Code trace. ScrollView::OnLayout clamps to 0 when it's not Scrollable (overflow ≤ kNoScroll), not to MaxOffset. Before, a list that shrank to within 4 DIP of the view stayed 1–4 DIP down, and the wheel couldn't fix it. | Controls.cpp:965-968 |
| 12 | fixed | Grep of the whole tree, tests and tools included, before deleting. Removed: ShellRoot::OnMouseMove (ShellRoot is never `interactive`, so it never got moves), ShellBody::window_ and its constructor argument, Host::onActivate (never set), Host::RenderNow (one caller, now Render), Text::Size (only LineHeight used it; it reads the table now). | MainWindow.cpp:29-75; Host.h; Text.h/.cpp |

## Left out (7)

| Item | Status | How it was checked | Where |
|---|---|---|---|
| Slider::OnMouseUp ignores `cancelled` | fixed | Code trace (MouseEvent says "undo, don't commit"). A drag that loses capture goes back to its start value with onChange and calls no onCommit. | Controls.cpp:529-554, Controls.h:225 |
| Dropdown/Segmented with no choices hit clamp(0,-1) | fixed | Code trace: std::clamp with hi < lo is UB (and asserts in debug). The arrow keys return false when there are no choices. | Controls.cpp:380, :498 |
| The toast map keeps stale Host* entries | fixed | Code trace. The slot is erased in the toast's onDismiss, however it closes (its timer, a newer toast, CloseAllPopups, now also on a route change). A later popup at the same address can't be taken for it. Left: a host destroyed with a toast up (~Host pops popups without onDismiss) still leaves its entry, but its timers die with it. | Controls.cpp:1683-1740 |
| An orphan tooltip bubble stays up | fixed | Code trace: Forget() cleared tooltipFor_ but left the bubble, and UpdateTooltip returned early for target == tooltipFor_ == null. Now a bubble with no owner is hidden on the next frame or move. | Host.cpp:759, :1001-1002 |
| kDumpTreeMessage honoured outside --automation | fixed | Gated on g_automation, as the coordinator asked. dump-tree.ps1 and the `dump` command already run against automation instances only. | Host.cpp:1634-1637, Host.h:101 |
| TextBox paste has no length cap | fixed | Code trace. Insert caps the text at 32 768 characters (a long stream URL fits) and doesn't split a surrogate pair at the cap. | Controls.cpp:727-735 |
| Text::Layout allocates its key on every call | fixed | The cache is looked up by a `wstring_view` key (transparent hash/equal); a std::wstring is built only on a miss. Paint time on the songs list didn't change measurably (0.45–1.05 ms base, 0.47–1.11 ms fixed: noise), but each cached text no longer allocates per frame. | Text.cpp:38-62, :140-144, :167 |

## Added by the coordinator

| Item | Status | How it was checked (before → after) | Where |
|---|---|---|---|
| V7-08 library toasts cover the scan banner | fixed | r8fu-tmisc.ps1 on the 24 000-file set, Music › Songs, during the first scan, toast from `action playback.shuffle`. Base: ToastBubble at y 122 (the banner is 110–162). Fixed: y 174, 12 DIP below the banner. When no banner shows, toasts stay at 122. | MainWindow.cpp:116-128 |
| V7-10 toast actions are mouse-only and time out | fixed | r8fu-tv710.ps1: an unwritable snapshot folder gives the "Can't save to …" toast with "Настройки". Base: hovered, it's gone after 6.5 s; Shift+Tab focuses the queue button. Fixed: hovered, it stays up and goes within 1 s after the pointer leaves; Shift+Tab (nothing focused) or Tab after the window's last control focuses "Настройки" (Enter/Space run it), and it stays up while focused. Design: Tab order, no new shortcut. A clickable toast is still out of the way of the mouse and keys until someone reaches for it. The volume popup (also input without focus) joins the Tab order the same way. | Controls.cpp:1689-1700; Host.cpp:951-956 |
| Shell.cpp folder check | fixed | Uses `lib::LooksLikeFolder`, the single copy. | Shell.cpp:582 |
| Language switch loses the library scroll | fixed | r8fu-tlang.ps1, Music › Songs scrolled to 960: base 960 → 0 after `set general.language en`; fixed 960 → 960. The rebuild saves the view state first, the way Back/Forward does. | Shell.cpp:472-477 |

## Outside my files

Nothing was edited outside the scope. Notes for others:
- tools\dev\dump-tree.ps1's header still says "any host window"; the message now works on `--automation` instances only (pages fixer's tools).
- #10: the full "layout only when dirty" change needs the player widgets (PlayerView, MiniPlayer) to invalidate their layout themselves while chrome_/hover_ animate. Measured not worth it now.

# Round 5: how to find every UI bug

Round 4 checked screens by eye and fixed what two hunters noticed. The owner still sees many bugs, so this round changes the method. The sources are listed at the end.

The method has four layers. Each one catches what the others miss.

| Layer | Catches | Tool |
|---|---|---|
| 1. Deterministic audits of the widget tree | overlap, clipping, truncated text, tiny targets, dead click zones | `tools/dev/ui-audit.py`, `tools/dev/hitmap.ps1` |
| 2. State-matrix screenshots reviewed against a reference | layout, rendering, appearance and state bugs a tree cannot show | `tools/dev/sweep.ps1` + vision review |
| 3. Worst-case data | the ways real libraries break "nice demo" layouts | `qa/make-worstcase.ps1` |
| 4. Exploratory charters with timing and real-input fidelity | flows, interruptions, focus, animation, input routing | charters below |

Code-level rules for visual bugs that only real media shows (opaque colors over pictures and the
like) are in [../visual-rules.md](../visual-rules.md), with `tools/dev/check-colors.py`. Worst-case
data includes a saturated cover, not only black or generated art.

## 1. Deterministic audits

The widget tree (`dump-tree.ps1`) holds every widget's rect, flags and text. A script can check rules on it without guessing:

- **Overlap.** Two visible sibling leaves with text or input intersect by more than 1 DIP. Legitimate overlays (video, chips, toasts, popovers) go in an allowlist.
- **Clipping.**
  - A child extends outside its parent's rect, unless the parent scrolls.
  - A widget extends outside the window's client rect.
- **Truncated text.** The text's natural width exceeds the label's width. That is fine with an ellipsis and a tooltip; it is a bug when the text is cut mid-glyph or the label never shows its full value anywhere.
- **Target size.** An interactive widget smaller than 24 × 24 DIP (WCAG 2.2, 2.5.8) is a bug, unless it is inline text or spaced 24 DIP apart.
- **Empty widgets.** A visible interactive widget with zero size, or a widget that is visible but off-screen.
- **Hit map.** WM_NCHITTEST is swept over the client area. A region that answers anything except HTCLIENT, but lies over an interactive widget, is a dead zone for the real mouse: the caption swallows the click or a resize border catches it. Posted test input never goes through this step, so earlier rounds could not see these bugs.

Each audit runs in every state of the matrix, not only on the default screen.

## 2. State matrix and vision review

Bugs hide in combinations. Every screen is captured in this matrix:

- **Window:**
  - minimum size;
  - 1024×640;
  - 1280×780 (default);
  - 1920×1080 maximized;
  - fullscreen;
  - PiP and the mini player where they apply.
- **Theme:** dark and light.
- **Scale:** 100 %, 125 %, 150 % and 200 % (the automation `--scale` override).
- **Data:**
  - empty;
  - one item;
  - normal test media;
  - worst case (layer 3);
  - 1000+ items.
- **Transient state:**
  - hover;
  - pressed;
  - focus ring (keyboard);
  - disabled;
  - loading;
  - error;
  - mid-animation (two captures 60 ms apart);
  - control bar shown or hidden;
  - each bar style (floating, classic, minimal).

How to review screenshots with a vision model. The research shows these steps raise detection a lot:

- **Give context.** Pass a short description of what the screen is and what it must show (from design/DESIGN.md), plus the bug taxonomy below. The context-free prompt "find bugs in this image" detected about a third fewer bugs.
- **Compare with a reference.** A bug-free screenshot of the same screen was the single most useful input. Here the references are:
  - the same screen in the other theme or at the other size;
  - the Figma frame (`design/`);
  - the same component on another page.
  
  Ask "what differs and which differences are wrong".
- **Crop and zoom.** Rendering artifacts such as hairlines, blur, 1 px seams and clipped glyphs show only when cropped. Use full-resolution crops of toolbars, list rows and edges, not the 0.4-scale overview.
- **Look twice.** A second pass on the same image with a different focus (layout first, then colour and state) finds more than one pass.
- **Verify every candidate.** Check each candidate in the tree dump or by interacting. A vision-only claim is "suspected" until confirmed.

### Bug taxonomy for the review

From the UI-Lens and canvas studies, adapted to Lumen.

| Type | Examples |
|---|---|
| Layout | overlap; misalignment between rows or columns; inconsistent paddings; content stranded on one side of a wide window; elements pushed off-screen; a panel covering controls it should not; jumps between states |
| Text | cut without an ellipsis; ellipsis with no way to read the full value; wrong plural ("1 файлов"); untranslated English in the Russian UI; raw ids or `NaN`/`0:00` placeholders; mixed decimal separators |
| Rendering | see-through regions; 1 px seams; blurry icons or text at fractional scales; stale frames after a resize; flicker; black or white flashes; DComp layer showing the GDI surface |
| Appearance | colour from the wrong theme; contrast below 4.5:1 for text or 3:1 for icons; inconsistent radii, stroke widths or icon sizes; a hover state that looks disabled |
| State | wrong item highlighted; a stale value after a change; a panel open on the wrong tab; a control that doesn't reflect the setting; a disabled control that still reacts; focus lost after closing a popup |
| Motion | animation that jumps, restarts or can't be interrupted; an element that slides in from the wrong edge; layout that moves while the user aims; anything slower than 200 ms on a frequent action |

## 3. Worst-case data

Demo media flatters the layout, so real libraries break it. `qa/make-worstcase.ps1` builds a worst-case set (catalogue adapted from the break-ui skill):

- **Long titles:** the longest realistic title, artist and album names, with diacritics, CJK and RTL.
- **Emoji:** an emoji at the start of a title.
- **Unbreakable names:** names with no spaces.
- **Missing tags:** no tags at all; the title is only the file name.
- **Similar names:** identical titles in different folders; names that differ only at the end.
- **Durations:** 1 second, 0 frames, and 10+ hours.
- **Aspect ratios:** 21:9, 9:16, 4:3, 1:1, and a tiny 160×90.
- **Covers:** a cover that is huge, tiny, very wide or missing.
- **Collection sizes:** 1 item, page size + 1 items, 1000+ items, and 12 levels of nested folders.
- **Counts:** 1, 2, 5, 11, 21 and 1284, which exercise all three Russian plural forms.

The library, the queue, the player title, the info dialog, PiP, the mini player, the tray tooltip and toasts are all checked with this set.

## 4. Exploratory charters

Each charter is a 30–60 minute mission: "Explore *area* with *resources* to discover *risks*". Two heuristic families help:

- **SFDIPOT** decides what to vary: Structure, Function, Data, Interfaces, Platform, Operations, Time.
- **FEW HICCUPPS** gives the oracles that recognise a problem: consistent with History, Image, Comparable products (VLC, mpv/uosc, PotPlayer, MPC-HC, IINA), Claims (STATUS.md, settings labels), User expectations, Product itself, Purpose, Standards (Windows UX, WCAG).

Interaction risks to try in every charter:

- **Interrupt.** Open a popup during an animation, resize during a transition, switch files while a menu is open, press Esc in each layer, close a window mid-drag.
- **Repeat fast.** Double and triple clicks, key auto-repeat, 20 wheel notches, toggling fullscreen 5 times.
- **Reorder.** Do the same thing via the mouse, a shortcut, the context menu and the tray, and expect the same result.
- **Boundary.** Start or end of file, first or last queue item, volume 0 and max, speed limits, empty queue.
- **Persistence.** Restart and check that the size, panel, tab, bar style and position come back.
- **Focus and keyboard.** Tab order, a visible focus ring, Esc closing the topmost layer only, Enter/Space on every control, arrow keys in lists.
- **Real-input fidelity.** Posted messages skip:
  - WM_NCHITTEST;
  - WM_SETCURSOR;
  - TrackMouseEvent/WM_MOUSELEAVE;
  - capture;
  - cursor hiding.
  
  Simulate these explicitly (`hitmap.ps1`, posting WM_MOUSELEAVE, WM_SETCURSOR, WM_CAPTURECHANGED) and read the code paths that depend on them.

## Report format

Each finding gets one row: id, severity, screen and state, steps, expected vs actual, evidence (crop path, tree line), suspected cause (file:line) and the fix.

- **Severities:**
  - **Broken:** content unreadable, an action unreachable, or wrong data.
  - **Ugly:** visibly wrong but usable.
  - **Fragile:** fine now, one realistic step from breaking.
- **Status:** "confirmed" or "suspected". Fixers only take confirmed rows.

## Sources

- Macklon and Bezemer, VLMs for visual bugs in canvas apps: https://arxiv.org/html/2501.09236v1. Context, a bug-free reference and multiple samples raise detection.
- UI-Lens (CVPR 2026), a display-defect benchmark and annotation guidelines: https://openaccess.thecvf.com/content/CVPR2026/supplemental/Xiang_UI-Lens_Assessing_General_CVPR_2026_supplemental.pdf
- OwlEye, display issues in GUI screenshots: https://arxiv.org/abs/2009.01417
- Exploratory heuristics (Hendrickson's cheat sheet, SFDIPOT, FEW HICCUPPS): https://qaskills.sh/blog/exploratory-testing-heuristics-cheatsheet, https://club.ministryoftesting.com/t/what-would-you-change-and-or-add-to-the-test-heuristics-cheat-sheet/52669
- Microsoft high-DPI app quality, testing clipped and off-screen UI at 125 % and 150 %: https://learn.microsoft.com/en-us/windows/win32/win7appqual/user-interface---high-dpi-awareness
- The browserbase ui-test skill and its adversarial checks (empty states, rapid clicks, keyboard-only): https://skillselion.com/skills/browserbase/skills/ui-test
- The break-ui skill catalogue (installed locally).

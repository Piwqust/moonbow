# Round 5, layer 4: flows (interaction, state, timing)

Hunter: r5-flows. Exe: `%TEMP%\lumen-qa\r5-base\Lumen.exe` (prebuilt, not rebuilt). Instances: `%TEMP%\lumen-qa\r5fl-<n>`, all `--automation`, input posted with `PostMessage`. Evidence: `%TEMP%\lumen-qa\r5fl\`. Screen: 2560x1440 at 100 %.

Work in progress; the summary table is filled as charters finish.

## Summary

| ID | Sev | Status | Charter | Finding |
|---|---|---|---|---|
| FL-01 | Broken | confirmed | 1 | In fullscreen the side panel (Ctrl+B) is painted but dead: every click, wheel and double click on it goes to the video underneath. |

## Findings

### FL-01 (Broken, confirmed): the side panel is unclickable in fullscreen

- **Steps.** Open 5 videos, pause, `F`, `Ctrl+B`, click the "Series S01" row in the panel (client 140,112).
- **Expected.** The panel opens the folder, as in the windowed player.
- **Actual.** Nothing happens in the panel; the video starts playing (the click was a click on the video). A double click there would leave fullscreen and the wheel changes the volume.
- **Evidence.** `c1-02-fs-side.png` (before), `c1-03-fs-side-click.png` (after: panel unchanged, video advanced and the bar auto-hid). Tree: `ui::Panel [0,0 416x1440]` is followed by `app::PlayerView::VideoArea [0,0 2560x1440]`.
- **Cause.** `src/app/player/PlayerView.cpp:382-392` adds `side_` before `video_`; `Widget::HitTest` (`src/ui/Widget.cpp:256-261`) tests children last-first, so the later `VideoArea` wins. In windowed mode they don't overlap (`x += sideW`, line 481); in fullscreen `PlayerView::OnLayout` (lines 482-490) lays the panel over the full-size `VideoArea`. This is the round 4 P-08 fix: it made the panel opaque but not hittable.
- **Fix.** Add the side panel after the video (before the chips and the bar), e.g. create `side_` right before `bar_`, or move it with `RemoveChild`/`AddChild(…, index)` in the constructor. The bar stays last, so it still draws over the panel. Add a regression check: in fullscreen, `InteractiveAt(center of a panel row)` must be inside `side_`.
- **Owner bug link.** This is one real way the owner's "can't edit in fullscreen" happens; see the charter 1 conclusion.

# Round 7: clicking a word in subtitles (r7-subwords, MARKET.md P2-7)

Branch `worktree-agent-ac061f2c4b941b8a5`, on top of main 4f1b361. Base exe for comparison: `%TEMP%\lumen-qa\r7sw-build-base\Lumen.exe` (main as it was), new exe: `%TEMP%\lumen-qa\r7sw-build-new\Lumen.exe`. Test media: `%TEMP%\lumen-qa\r7sw-media` (generated: SRT with `<i>`, multi-line, Cyrillic; ASS with tags, a top sign, karaoke, a drawing, `\N \n \h`; CJK; Hebrew, Arabic and mixed bidi) and the standard `%TEMP%\lumen-qa\media`. Evidence: `%TEMP%\lumen-qa\r7sw-shots`, `r7sw-sweep-*`. Every instance ran with `--automation` and `-Library` set to a test folder, and was killed by its own PID. Nothing touched the clipboard or opened a browser (see "Copy and links in tests").

## What was built

mpv draws subtitles in its own window, so Lumen has no word geometry. On pause Lumen draws the current subtitle itself and hides mpv's copy while it does.

- **New files.**
  - `src/app/player/SubText.h`, header-only and window-free, so `core_tests` checks it:
    - ASS tag stripping, with the `\an` / legacy `\a` alignment and italic / bold per character;
    - events from `sub-text/ass` and whole event lines from `sub-text/ass-full`;
    - the `.ass` header (PlayRes, styles);
    - word segmentation (see "Text");
    - the lookup links.
  - `src/app/player/SubWords.{h,cpp}`: the `SubtitleWords` widget, which holds the state, the layout, the painting, the input and the menu.
- **The trigger.**
  - Pausing shows the overlay when a text subtitle is on screen.
  - A click on the subtitle line also pauses and shows it. This matters when a click on the video doesn't pause by itself (Settings > Controls > Mouse: single click "none" or "show controls"). With the default play/pause click, that click pauses anyway.
  - The overlay hides on resume, on a new file, in PiP, in the mini player, when the player's quick settings panel opens, and on Esc (until the next pause).
- **Hiding mpv's copy.**
  - `sub-visibility` (and `secondary-sub-visibility` when a second track shows) goes off while the overlay is up. It goes back on whenever the overlay goes.
  - Interplay:
    - Shift+V ("hide subtitles") while the overlay is up keeps mpv's copy hidden and drops the overlay. A second Shift+V brings both back.
    - A snapshot with subtitles turns mpv's copy back on for that one frame (set, screenshot, unset: mpv runs them in order).
    - A frame step unpauses for one frame: the overlay ignores that blink, so mpv's copy doesn't flash.
    - `PlayerView::SubtitleBand` counts the hidden copy as shown, so the skip and next-episode chips keep clear of the line.
    - The quick settings panel has a "Show subtitles" switch that reads mpv's state. While it is open, the overlay steps aside.
- **Position and style.** The text is drawn with DirectWrite, close to libass:
  - **Text subtitles** (SRT, WebVTT and the like) use the Settings > Subtitles style: font (sans-serif is Arial, as libass resolves it), size, bold, colour, opacity, outline, shadow, "Box behind each line", "Box behind the text". Line height is the font size, as in libass, and the em comes from the font's ascent and descent. `sub-scale`, `sub-pos`, `sub-margin-y`, `secondary-sub-pos` and "Show subtitles in the black bars" are applied.
  - **The lift over a floating bar** comes from mpv's own `sub-margin-y`, observed while paused. MainWindow needed no change.
  - **Native .ass tracks** use their script's styles (`sub-ass-extradata` and `sub-text/ass-full`): font, size, PlayRes scaling, bold, italic, primary and outline colours, outline and shadow width, BorderStyle 3, alignment, style and per-event margins. This is skipped when Settings apply the Lumen style to styled subtitles.
  - **Inline styling.** `<i>`, `<b>`, `\i1` and `\b1` are kept. Inline `\c`, `\fs` and `\pos` are not.
- **Words are hit targets.**
  - Hover underlines a word with a text-coloured line on an outline, and the cursor becomes a hand.
  - A click opens the menu for the word. A shift-click from the last word, or a drag, picks several words of one block, and the menu opens for them.
  - The picked words sit on a dark pill.
  - The menu has four items: Копировать слово / Copy word ("слова" / "words" for several), Копировать фразу / Copy phrase (the block the words are in, lines joined), Найти в словаре / Look up in dictionary, Перевести / Translate.
  - The menu sits above the whole block, so it never covers the line being read.
  - A right click on a word opens the same menu. Away from the words, the right click opens the video menu.
  - A double click goes to the video: fullscreen by default. Pausing by the first click doesn't steal it.
  - The wheel acts as it does on the video.
- **Links.**
  - Dictionary: `https://{lang}.wiktionary.org/w/index.php?search={text}`. Wiktionary search goes straight to the entry, case-insensitively.
  - Translator: `https://translate.google.com/?sl=auto&tl={lang}&text={text}&op=translate`.
  - `{text}` is the picked words, UTF-8 percent-encoded. `{lang}` is the interface language (ru / en).
  - Only `http(s)://` templates are opened, so a template can never start a program. Anything else shows a toast.
  - These are the only network actions. Lumen itself sends nothing. `ShellExecute` opens the default browser, only on the click.
- **Keyboard.** New action `subtitles.words` "Pick a word in subtitles" / "Выбрать слово в субтитрах", W by default (C is taken by the YouTube key preset). It shows in Keyboard Shortcuts and Settings > Controls.
  - W pauses and puts the keyboard focus on the first word of the dialogue.
  - Left and Right move in reading order, reversed on right-to-left lines. Up and Down move to the nearest word on the next line. Home and End go to the first and last word.
  - Shift+arrows extend the pick. Enter or the menu key opens the menu. Ctrl+C copies.
  - Esc closes the overlay and gives mpv its subtitles back. Space still plays.
  - The focus ring is drawn in the text colour on a dark ring, because the theme's focus colour is made for panels, not video.
  - W also gives toasts: no subtitles ("Сначала включите субтитры"), picture subtitles, the feature off, and nothing on screen.
- **Settings.** Subtitles > "Слова в субтитрах / Words in Subtitles" has a switch, "Выбор слов на паузе / Pick words on pause" (on by default), and the two link templates. The templates are disabled while the switch is off.
- **Bitmap subtitles** (PGS, DVD, DVB, XSUB) have no text. The overlay never arms on them, a click on the line is an ordinary video click, and W explains why.

## Text

- **Tags.** `{…}` override blocks are dropped. Vector drawings (`\p1 … \p0`) are dropped, but `\pbo` is not mistaken for `\p`. `\N` is a break, `\n` a space, `\h` a no-break space. An unclosed `{` is shown as written, as libass does. Events made only of drawings are dropped.
- **Multi-line.** One event's lines stay one block. Events of one vertical alignment stack into one block, as libass stacks them. Top signs, middle lines and the secondary track are separate blocks.
- **Words.** Found with `GetStringTypeW` (no ICU):
  - letters, digits and combining marks form words, with apostrophes, hyphens and the Catalan middle dot inside them ("don't", "кое-как", "l·lengua");
  - punctuation, spaces and emoji separate words;
  - Chinese characters are one word each, the stated fallback, since there is no dictionary to segment them;
  - runs of hiragana or of katakana (with ー) are one word each;
  - Hangul is space-separated like Latin;
  - a surrogate pair stays one character;
  - Thai comes out as one word per phrase (noted in "Left out").
- **RTL.** The paragraph direction follows the first strong character. Bidi runs inside a line come from DirectWrite. Hit rects come from `HitTestTextRange`, so mixed lines like "Mixed: שלום and مرحبا" pick the right word.

## How I verified it

- **Unit tests.** `tests\build-tests.bat`: 27 tests, 5646 checks, 0 failures. The new `subtitle_text` test has 80 checks:
  - tag stripping, alignment, drawings, karaoke and marks;
  - events, the `.ass` header and full events;
  - segmentation in Latin, Cyrillic, CJK, kana, Hangul, Hebrew, Arabic, emoji and surrogate pairs;
  - word ranges and the phrase;
  - bitmap codecs;
  - URL encoding, and link templates that are rejected (`calc.exe`, `file:`, `javascript:`, quotes, empty).
- **Checks.** `check-icons` 0 missing, `check-settings` 0 of 223 without a reader, `i18n_check` 0 conflicts, the build has no warnings at /W4.
- **Overlay against mpv's own rendering** (same frame, word picker on, then off; crops are pairs, overlay on top):
  - SRT: `a1-seek-crop.png` / `a1-resumed-crop.png` (Cyrillic, 2 lines), `barhidden-pair.png` (lifted over the floating bar). They match within 1-2 px.
  - ASS: `ass-10-pair-bottom.png` (italic, bold, 2 lines; the inline yellow `\c` is not reproduced), `ass-10-pair-top.png` (the `\an8` sign), `ass-70-pair.png` (`\h`, `\n`; the inline `\fs60` word is not reproduced). Before the .ass styles were read, the overlay was a third smaller than libass (`ass-10-words.png` vs `ass-10-mpv.png` of the first run). Now it matches.
  - CJK: `cjk-10-pair.png`, `cjk-40-pair.png`. Kana and Hangul match. Single-line Chinese and Japanese text is about 10 % larger than libass's, which sizes each fallback font by its own line height.
  - RTL: `rtl-10/40/70-pair.png`. Hebrew, Arabic in 2 lines and mixed bidi match.
  - Secondary track (Chaptered Demo, English + Russian): `sec-pair-top.png`. Both copies hidden, the top line in place.
  - Fullscreen at 2560x1440: `fsbar-pair.png`. The overlay is about 8 px higher than mpv's line, which is not explained. Windowed it matches.
- **Flows** (tree dump lines of `app::SubtitleWords`, which list `words`, `sel`, `picked`, `focus`, `copied`, `opened`, `mpv-subs-off`):
  - Click: the menu opens (`a1-menu.png`).
  - Copy word: `copied="world"`. Copy phrase: the whole line.
  - Dictionary: `opened="https://ru.wiktionary.org/w/index.php?search=stop"`. Translate: `opened="https://translate.google.com/?sl=auto&tl=ru&text=Don%27t&op=translate"`.
  - Drag "Hello" to "stop": `sel=0-3 picked="Hello, world! Don't stop"`, menu "Копировать слова" (`a1-drag.png`).
  - Shift-click: `sel=4-8 picked="jumps over the lazy dog"` (`a1-shift2.png`). The menu is above the block.
  - Seek and frame step while paused: the overlay follows the new line. Resume: hidden, and mpv shows the line again (`a1-resumed-crop.png`).
  - Keyboard:
    - W: `keyboard focus=0`;
    - Right: `focus=1`;
    - Shift+Right twice: `sel=1-3`;
    - Down: the word below;
    - Enter: the menu with "Ctrl + C" (`kb15-menu.png`, light theme at 1.5);
    - Esc closes the menu and the focus stays on the word;
    - Ctrl+C: `copied="over"`;
    - Esc: the overlay goes, the player stays paused.
  - Focus ring: `kb-ring-light.png` (from the sweep capture), next to the first version in the theme colour, which was hard to see on video (`kb15-focus-crop.png`).
  - Quick settings panel open: the overlay hides and mpv shows the line (`flow-panel.png`). Closing the panel brings the overlay back.
  - Right arrow while paused and not in keyboard mode: seeks, and the overlay follows.
  - Shift+V hides the line, a second Shift+V shows it and the words again.
  - Setting off: no overlay, and W shows "Выбор слов выключен в настройках субтитров". Setting on again: the words come back.
  - Double click on the line while playing: fullscreen, still playing.
  - Single-click mode "none": a click on the picture does nothing, a click on the line pauses and shows the words.
  - A file without subtitles: W shows "Сначала включите субтитры".
- **Themes, scales, bar.** These were checked:
  - dark and light;
  - `--scale` 1.0 and 1.5;
  - the minimum window at 1.5;
  - fullscreen at 1.5;
  - bar shown;
  - auto-hide "always" (paused, the floating bar stays and the line is lifted over it);
  - fullscreen with the bar.

  Captures: `s15light-overlay.png`, `barhidden-*.png`, `fsbar-*.png`, `r7sw-sweep-words\player-subwords.*\final.png`.
- **Sweep.** New scenarios in `qa/round5/scenarios.json`:
  - `player-subwords` (hover; dark, light; min and fullscreen at 1.5);
  - `player-subwords-menu`;
  - `player-subwords-keyboard`;
  - `settings-subtitles-words` (dark, light, ru, en).

  Results:
  - Before, `^player`, base exe: 42 scenarios, 2 low findings (title bar ellipsis on worst-case names). `r7sw-sweep-before`.
  - After, `^player|settings-subtitles`, new exe: 56 scenarios, 0 medium or high findings. The same 2 low findings, plus 2 low `subtitles.matching` dropdown truncations. The base has those 2 as well (`r7sw-sweep-before-settings`). `r7sw-sweep-after`, `r7sw-sweep-words` (14 of 14 ok).
  - No new finding of any severity.

### Copy and links in tests

The owner's clipboard was never read or written. In `--automation` runs `Copy()` records the text it would copy and shows the toast, but skips `SetClipboardText`. `Host::ShellOpen` already opens nothing there. Both values show in the tree dump (`copied="…"`, `opened="…"`), and the flows above read them from there. In a normal run the same code calls `dlg::SetClipboardText` and `ShellExecute`. That path was checked by reading the code only.

## Performance

`r7sw-perf.ps1` and `r7sw-mem.ps1` (scratchpad). Each launches a fresh instance per run, plays 10 s, pauses on a subtitle, then waits 10 s. All of it is CPU time of the whole process, `PrivateMemorySize64`.

| | playing, % of a core | paused, % of a core | private MB, paused on a line |
|---|---|---|---|
| base (3 runs) | 9.5 / 8.1 / 1.7 | 0.00 / 0.47 / 0.00 | 210.4 / 210.2 / 210.3 |
| new, picker off (3) | 10.6 / 10.6 / 10.9 | 0.00 / 0.78 / 0.00 | 209.8 / 210.8 / 208.5 |
| new, picker on, words shown (3) | 9.2 / 0.3 / 4.4 | 0.00 / 0.00 / 0.16 | 220.1 / 217.9 / 218.6 |

- **Playing.** The numbers above are noise of the 10 s window, with mpv decoding. No difference shows. While playing the feature has no observer, no timer, no thread and no allocation. The player-change handler returns on time ticks, `Place()` compares a few values per layout, and `Frame()` returns unless paused.
- **Paused.** 0 % with the words up. There is no timer and no animation, and repaints happen only on hover or a pick.
- **Memory.** A staged run pins it down: playing, paused without a line, paused on a line, another line, playing again. Base 220 / 226 / 270 / 270 / 270 MB, new 218 / 226 / 271 / 271 / 271 MB. The jump is mpv seeking while paused (it shows on the base too). The overlay itself is within 1 MB.
- **Layout.** 100-250 µs per new line (`build-us` in the dump). That is one DirectWrite layout per block plus the hit tests, made only when the line, the size or the style changes.
- **Drawing.** The outline (the text stamped around a circle, as in the Settings preview) is recorded once per line into a command list on one kept device context. A repaint only replays it.
- **Showing.** The overlay comes up one mpv event batch after the pause: observe, values, one posted re-evaluation. The tree-dump polling used to measure it is slower than that, so no latency number is given.

## Left out, and why

- **PGS / VobSub at runtime.** The test media has no picture subtitles, and ffmpeg can't make them: it has no PGS encoder, and dvdsub only takes picture input. The bitmap path was checked by code and by the `IsBitmapCodec` unit test only.
- **Inline `\c`, `\fs`, `\pos`, `\move`, `\fad`, karaoke colours.** The overlay draws one style per block. Signs placed with `\pos` show at their alignment's edge, not at their point.
- **CJK fallback size.** About 10 % larger than libass (see above). Fixing it needs the fallback font's metrics per run.
- **Thai, Lao, Khmer.** No dictionary word breaking without ICU, so a phrase is one "word". Selecting by drag still works.
- **Fullscreen offset.** About 8 px at 1440p, cause not found.
- **Clicking a word in the mini player and in PiP.** Too small to be useful. The overlay stays off there.

## Changes needed outside my file scope

- **Per-file settings (the parallel agent).** Don't store `sub-visibility` while `PlayerView::Words()->HidesMpvSubs()` is true, or a file paused with the words up would come back with subtitles off. Lumen itself doesn't persist `sub-visibility` (watch-later keeps only start, aid, sid and secondary-sid).
- **`tools/dev/input.ps1`.** It posts the automation modifier message (0x802D) after the click messages, so `-Shift` and `-Ctrl` clicks reach Lumen without the modifier. Keys are fine. I used a scratch copy that posts it first. Moving the `if ($mods) { Post 0x802D … }` line above the `-Move` / `-Click` block fixes it.
- **`tools/dev/sweep.ps1`.** Its tree parser reads any flag containing the word `hidden` as an invisible widget, even `mpv-hidden`. I renamed my flag to `mpv-subs-off`. The parser could match ` hidden` as a whole token.

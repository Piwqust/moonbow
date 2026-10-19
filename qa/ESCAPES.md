# Escapes: what the owner found after the agents

Six rounds of agents hunted bugs, and the owner still finds things in a few minutes of real use.
Each such find is an escape: it shows a hole in the method, not only in the code. Every escape is
logged here with the reason it got through and what changed so the next one gets caught.
Read this before planning a review round.

## Log

| # | Date | What the owner saw | Kind | Why every round missed it | What changed |
|---|---|---|---|---|---|
| E1 | 2026-10-19 | Folder queue sorted only by name, not like the folder in Explorer | Product | Reviewers checked that the queue works, not that its order matches what the user just looked at. No one asked where the user comes from. | `playback.folderOrder`; product pass, Q2 below |
| E2 | 2026-10-19 | Opening one video rolls on into the next file of the folder | Product | An inherited default (MPC-HC auto-load) that was even documented as "resolved by design" in round 3. Reviewers judged it against the setting, not against intent. | Default off; product pass, Q1 |
| E3 | 2026-10-19 | Songs resume from the middle like films | Product | A video default (mpv watch-later) applied to music. Tests used one file type at a time and never asked whether music and video want the same thing. | Positions for videos only; product pass, Q1 |
| E4 | 2026-10-19 | Karaoke fill jitters back and forth | Motion | Checked with still screenshots. A still frame can't show jitter. | Frame series (`fillcheck.ps1` pattern): a value must change monotonically |
| E5 | 2026-10-19 | Lyrics are flat grey patches over a blue cover | Visual | No test file ever had lyrics *and* a colored cover; lyrics songs had no art. On black, opaque grey and translucent white look the same. The bug taxonomy asks "wrong", never "worse than Spotify". | `qa/visual-rules.md`, `check-colors.py`, cover+lyrics media, design pass |
| E6 | 2026-10-19 | Right click did nothing on the music screen with lyrics | Interaction | Right click was tried on video only; the music screen with lyrics was a separate case nobody combined. | Interaction matrix: every input on every screen state |

## Open, found by the new passes

- **O1 (2026-10-19, design pass, white cover).** A light cover leaves a mid-grey backdrop, and
  the dimmed lyric lines nearly merge with it. A per-channel gamma curve on the blur was tried and
  rejected: it shifted a blue cover to purple. The fix needs the cover's average luminance (on the
  thumbnail thread, where the pixels are) to set how dark the scrim gets.

## The pattern

1. **Testing data is the unit, not the situation.** Media was generated one property at a time:
   a song with a cover, a song with lyrics, a long title. Real songs have all of them at once, and
   most escapes live in the combinations.
2. **Reviewers look for defects, the owner looks for quality.** The brief asks for crashes,
   clipping and gaps against Figma. Nothing in it is wrong by that standard: the text renders,
   aligned and readable. "It looks cheap next to Spotify" is a different question, and nobody asked it.
3. **Defaults were inherited, not decided.** mpv and MPC-HC behaviors came along with the engine.
   Nobody asked, for each default, which user wants it, in which situation, and for which kind of
   media.
4. **Still images for moving things.** Jitter, lag and flicker show only across frames.
5. **New features skip the hunt.** Rounds review a frozen build. What is added afterwards gets
   only the author's "it works" check.

## Questions every review now asks

Product pass. For each behavior and default:
- **Q1.** Is it right for each kind of media: a song, a film, a series episode, a stream?
- **Q2.** Where does the user come from (Explorer, the library, a playlist, a drop), and does the
  result continue what they just saw?
- **Q3.** Would a user of Spotify, Apple Music, YouTube, VLC or IINA be surprised? If yes, is the
  difference on purpose?

Design pass. For each screen, on realistic media and at full scale, next to a best-in-class reference:
- **Q4.** What would a designer at Spotify or Apple change first? What looks cheap, flat or
  unconsidered?
- **Q5.** Does it hold on the extremes: a white, a black and a saturated cover, the light theme,
  long text, an empty state?

Motion pass. For anything that moves, a series of frames: does it move one way, at an even pace,
without a jump at the start or the end?

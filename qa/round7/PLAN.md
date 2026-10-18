# Round 7 (STATUS.md section 7): plan and state

The owner's request on 2026-10-18: "делай пункт 7". The owner's decisions:
- **Online features:** LRCLIB lyrics, OpenSubtitles and TMDB. Each is off by default.
- **Keys:** a field where the user pastes their own key; no keys are built in.
- **Testing:** agents may make a few test requests to these three APIs only. OpenSubtitles and TMDB are tested against a local mock.
- **Not chosen, so not done:** web remote, DLNA/Chromecast, Whisper and SVP.

## Agents (each one on its own worktree branch; its write-up goes to qa/round7/<name>.md)

| Agent | Area | Status |
|---|---|---|
| r7-music | crossfade, the visualizer (process loopback) | ✅ merged |
| r7-perfile | per-file settings | ✅ merged (+ coordinator follow-ups 18fb3bc) |
| r7-subwords | clicking a word in subtitles | ✅ merged |
| r7-skins | skins and token JSON, hot reload, contrast check | ✅ merged |
| r7-library | own tag parser, scan banner, "Новое"/"Битый файл" badges | ✅ merged |
| r7-leftovers | round 3 leftovers and unchecked areas (CPU filters live, mini player cover, R3-16, close time, recording and snapshots, handoff, tray) | ✅ merged |
| r7-online | HTTP module, LRCLIB, OpenSubtitles, TMDB | ✅ merged (4b06089: SYLT lyrics reach the view) |

All merged at 4b06089: 47 tests, 6431 checks, checks clean. Test exe: %TEMP%\lumen-qa\r7-m7. ⏳ The full sweep is running; then perf and the r7-verify agent.

Not done, with reasons:
- **slim libmpv:** needs the mpv sources downloaded and built; downloading is forbidden.
- **exclusive mode on a real device:** it would mute the owner's apps.

## After the agents

1. Merge each branch with `git merge --no-ff -F .git\MSG`. Resolve conflicts in SettingsSchema.cpp, the strings files and Player.cpp.
2. Build, run the tests and the checks, run the full sweep: 0 medium/high findings.
3. Measure: qa/perf/firstframe.ps1 and idle CPU.
4. Run a verification agent (fresh eyes) over all new features, then fix what it finds.
5. Update STATUS.md (section 7 and a new section for the round) and memory.
6. The owner's request (2026-10-18, before stepping away): when round 7 is done, run a whole-repo audit with the `ponytail-audit` skill. Then apply every reasonable fix (agents in worktrees, split by disjoint files). Verify: build, tests, checks, the sweep, perf. Write a proper report (qa/round8/REPORT.md, in Russian).
7. The very last step, after everything is committed and reported: put the PC to sleep.
   `powershell -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Application]::SetSuspendState('Suspend', $false, $false)"`
   Before that, delete the fallback cron job (CronDelete).

If the work stops (limits), the hourly fallback job resumes it from this file.

## State (updated as work goes)

- **Round 7:** the full sweep (378 scenarios) shows 0 medium/high findings; first frame 69–85 ms. ⏳ r7-verify (fresh eyes on round 7; report qa/round7/verify.md).
- **Round 8 audit:** five read-only reviewers. The raw findings are in qa/round8/raw/: storage, player, library-settings, ui; playback is still running.
- **Round 8 fixers:**
  - ⏳ running: r8-fix-storage, r8-fix-ui, r8-fix-player, r8-fix-pages;
  - ☐ next: the playback fixer, after the playback reviewer reports; it also takes the V7 findings that land in its files.
- **Then:**
  - merge everything;
  - build, tests, checks, the full sweep, perf;
  - qa/round8/REPORT.md, in Russian, in ponytail format, with every finding and what happened to it;
  - STATUS.md;
  - memory;
  - delete the cron job;
  - sleep.

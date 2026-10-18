# Round 7 (STATUS.md section 7): plan and state

The owner's request on 2026-10-18: "делай пункт 7". The owner's decisions:
- **Online features:** LRCLIB lyrics, OpenSubtitles and TMDB. Each is off by default.
- **Keys:** a field where the user pastes their own key; no keys are built in.
- **Testing:** agents may make a few test requests to these three APIs only. OpenSubtitles and TMDB are tested against a local mock.
- **Not chosen, so not done:** web remote, DLNA/Chromecast, Whisper and SVP.

## Agents (each one on its own worktree branch; its write-up goes to qa/round7/<name>.md)

| Agent | Area | Status |
|---|---|---|
| r7-music | crossfade, the visualizer (process loopback) | running |
| r7-perfile | per-file settings | running |
| r7-subwords | clicking a word in subtitles | running |
| r7-skins | skins and token JSON, hot reload, contrast check | running |
| r7-library | own tag parser, scan banner, "Новое"/"Битый файл" badges | running |
| r7-leftovers | round 3 leftovers and unchecked areas (CPU filters live, mini player cover, R3-16, close time, recording and snapshots, handoff, tray) | running |
| r7-online | HTTP module, LRCLIB, OpenSubtitles, TMDB | running |

Not done, with reasons:
- **slim libmpv:** needs the mpv sources downloaded and built; downloading is forbidden.
- **exclusive mode on a real device:** it would mute the owner's apps.

## After the agents

1. Merge each branch with `git merge --no-ff -F .git\MSG`. Resolve conflicts in SettingsSchema.cpp, the strings files and Player.cpp.
2. Build, run the tests and the checks, run the full sweep: 0 medium/high findings.
3. Measure: qa/perf/firstframe.ps1 and idle CPU.
4. Run a verification agent (fresh eyes) over all new features, then fix what it finds.
5. Update STATUS.md (section 7 and a new section for the round), memory, and the report to the owner.

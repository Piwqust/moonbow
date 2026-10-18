# Round 7: verification of the merged features (r7-verify)

Fresh eyes on the seven round 7 features after the merge. Exe `%TEMP%\lumen-qa\r7-m7\Lumen.exe` (main 4b06089, all merged), nothing built or changed in code. Every instance ran with `--automation`, a data dir `%TEMP%\lumen-qa\r7vf-*` and `-Library` set to a test folder, and was killed by its own PID. Own media: `%TEMP%\lumen-qa\r7vf-media` (copies of the shared test files plus a few generated ones). Evidence: `%TEMP%\lumen-qa\r7vf-out\` (captures, crops, logs, `perf.txt`), sweeps in `%TEMP%\lumen-qa\r7vf-sweep`, `r7vf-sweep-contrast`, `r7vf-sweep-oled`. Scripts: session scratchpad `vf.ps1` (helpers), `t-*.ps1`.

Network: only the online agent's mock (`qa/round7/online-mock.py`, 127.0.0.1:18765) and a closed localhost port (18799). No request went to lrclib.net or any other real host.

## Summary

Interactions first, then the rest.

| Id | Severity | Status | Area | Finding |
|---|---|---|---|---|
| V7-01 | Broken | confirmed | crossfade (× per-file timing) | Crossfade is silently skipped in about 4 of 10 song changes: the fade is planned before the duration is known and is then marked "too late" for good |
| V7-02 | Ugly | confirmed | TMDB × "Битый файл" | A damaged file is looked up on TMDB and gets a movie backdrop and a "2010 · ★ 8,4" chip |
| V7-03 | Ugly | confirmed (code + run) | crossfade × per-file speed | After the switch the old song's tail takes the new song's stored speed |
| V7-04 | Ugly | confirmed | subtitle words × subtitle size/lift | A live change of the subtitle size while paused redraws the words but leaves the hit area where it was |
| V7-05 | Ugly | confirmed (code) | "Очистить историю" × per-file × online | The two clear-history buttons clear different things; the texts mention neither per-file settings nor online data |
| V7-06 | Ugly | confirmed | tag parser × player | The library shows the parser's tags, the player shows mpv's: "Песня в CP1251" in the list, "Ïåñíÿ â CP1251" in the music view |
| V7-07 | Fragile | confirmed | per-file × deinterlace | D pressed twice stores "deinterlace: no" for the file, which differs from the default "auto" |
| V7-08 | Fragile | suspected (code) | toasts × scan banner | Library toasts sit at y = 122, exactly over the new scan banner |
| V7-09 | Fragile | suspected (code) | crossfade × per-file audio delay | The crossfade helper doesn't copy `audio-delay` |
| V7-10 | Fragile | suspected (code) | toasts with actions | "Открыть папку" / "Настройки" on a toast can't be reached from the keyboard |

Checked and fine: per-file flip/speed with the live CPU filter switch (no lost filter, zero-copy restored), embedded SYLT/USLT and `.lrc` beating LRCLIB, per-file subtitle size with the word picker across a restart, every new screen under the High contrast and OLED skins (sweeps: 0 medium/high), the banner under a skin at 150 %, corrupt `file-settings.json`, a broken/fixed/deleted skin file, all online features with the network unreachable, rapid toggling of every new setting, restart persistence.

PERF_SUMMARY_PLACEHOLDER

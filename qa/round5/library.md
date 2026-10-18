# Round 5: library hunt (r5-library-eye)

Draft in progress. See the final version for the summary table.

- Exe: `%TEMP%\lumen-qa\r5-v2\Lumen.exe` (not rebuilt).
- Instances: `--automation` via `tools\dev\launch.ps1`, data dirs `%TEMP%\lumen-qa\r5le-<n>`, always with `-Library` on a test folder and onboarding skipped.
- Evidence: `%TEMP%\lumen-qa\r5le\` (`<name>.png`, `<name>.tree.txt`, `<name>.audit.md`, `crop-*.png`).
- Helper: `%TEMP%\lumen-qa\r5le\snap.ps1 -P <pid> -Name <shot> -Cmd "cmd1;;cmd2"` runs `command.ps1` commands (plus `key`, `type`, `in <input.ps1 args>`, `size w,h`, `sleep ms`), then `wait-idle`, capture, dump and audit.

## Findings so far

| ID | Sev | Screen / state | What |
|---|---|---|---|
| LB-01 | Broken | Playlist > Delete… > Delete | The confirm dialog closes and nothing is deleted (use-after-free of `done` in `ShowConfirm`) |
| LB-02 | Ugly | Any list, Back/Forward | Scroll position and selection are reset to the top |
| LB-03 | Ugly | Playlist / folder with a long name | Current crumb capped at 280 DIP although the header has room |
| LB-04 | Ugly | Breadcrumb "…" | The collapsed ancestors are a plain label, not reachable |
| LB-05 | Ugly | Music > Artists / Genres, list | Count sits in a 40 % "subtitle" column; names are cut with 300 DIP free |
| LB-06 | Fragile | Every grid and list | No tooltip for cut titles anywhere in ItemGrid |

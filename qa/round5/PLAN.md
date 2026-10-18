# Round 5: plan and state (for resuming after a break)

The owner's request (2026-10-18, 17:15): find and fix all UI bugs with the method in METHOD.md, and fix the confirmed items of qa/AUDIT-PONYTAIL.md. Only then move on to STATUS.md section 7.

If limits run out, continue on your own; resume jobs are scheduled. **When ALL the work is done** (the owner's request is explicit), put the PC to sleep:

`powershell -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Application]::SetSuspendState('Suspend', $false, $false)"`

Hibernation is off on this PC, so this command sleeps (S3). The sleep is the very last action, after the final commit and the report. Subagents never put the PC to sleep.

## Steps

1. ✅ Research and method: METHOD.md, COMMON.md; tools/dev/hitmap.ps1.
2. ✅ Test hooks merged: tree dump v2, command.ps1, --scale, WM_MOUSELEAVE. Test exe: %TEMP%\lumen-qa\r5-v2\Lumen.exe; rebuild it from main after merges.
3. ✅ Flows hunt, report in flows.md (FL-01…FL-14). FL-01: the side panel was dead in fullscreen, the likely cause of the owner's queue bug.
4. ⏳ Fixer r5-fix-flows: FL-01…FL-14. It also gets audit items 3, 4, 5, 8, 16 and 17, which are in its files.
5. ⏳ Fixer r5-fix-audit: the other confirmed items of AUDIT-PONYTAIL.md.
6. ✅ r5-tools-sweep merged: ui-audit.py, sweep.ps1, scenarios.json, make-worstcase.ps1, audit-baseline.md (289 scenarios in 6 min). The dump clip flag now uses glyph ink (f4bdf30). Test exe with it: %TEMP%\lumen-qa\r5-v3. The baseline's dialog items went to r5-fix-settings and its onboarding items to r5-fix-audit.
7. ✅ Hunters r5-player-eye (player-visual.md, PV-01…PV-18) and r5-settings-eye (settings-dialogs.md, SD-01…SD-22), both merged.
   - ⏳ Fixer r5-fix-settings: SD-01…SD-22 except SD-09/10, which went to r5-fix-audit.
   - ☐ Fixer for PV-01…PV-18: start it AFTER r5-fix-flows is merged, since they share src/app/player/*. It also covers the round 4 leftovers P-06/18, P-07 and P-13, plus the design gaps the owner would care about: a wider bar in fullscreen and a title bar in fullscreen.
8. ⏳ Hunter r5-library-eye: library, home, shell and the worst-case data. It starts after step 6, once the media is ready.
9. ☐ Fixers for the steps 7–8 reports, split by disjoint files: player / library+shell / settings+dialogs+ui.
10. ☐ Final verification: rebuild, the full sweep, the audit is clean (or every remaining finding is justified), tests and checks pass, and a few owner scenarios replayed. Rebuild build\Lumen.exe only when the owner's Lumen is not running.
11. ☐ STATUS.md: a section "Раунд 5", README if needed, memory. Then the report to the owner, then sleep.

Merge each branch with `git merge --no-ff -F .git\MSG`. After each merge, rebuild r5-v2.

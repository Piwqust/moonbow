# Round 5: plan and state (for resuming after a break)

The owner's request (2026-10-18, 17:15): find and fix all UI bugs with the method in METHOD.md, and fix the confirmed items of qa/AUDIT-PONYTAIL.md. Only then move on to STATUS.md section 7.

If limits run out, continue on your own; resume jobs are scheduled. **When ALL the work is done** (the owner's request is explicit), put the PC to sleep:

`powershell -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Application]::SetSuspendState('Suspend', $false, $false)"`

Hibernation is off on this PC, so this command sleeps (S3). The sleep is the very last action, after the final commit and the report. Subagents never put the PC to sleep.

## Steps

1. ✅ Research and method: METHOD.md, COMMON.md; tools/dev/hitmap.ps1.
2. ✅ Test hooks merged: tree dump v2, command.ps1, --scale, WM_MOUSELEAVE. Test exe: %TEMP%\lumen-qa\r5-v2\Lumen.exe; rebuild it from main after merges.
3. ✅ Flows hunt, report in flows.md (FL-01…FL-14). FL-01: the side panel was dead in fullscreen, the likely cause of the owner's queue bug.
4. ✅ Fixer r5-fix-flows merged (3a8f8d5): FL-01…FL-14 (FL-09 rejected) plus audit items 3, 4, 5, 8, 16 and 17 and the player items from "Остальные". Test exe: %TEMP%\lumen-qa\r5-v4.
5. ✅ Fixer r5-fix-audit merged (67222df): report in audit-fixes.md. Long paths rejected; the leftover dead code in Player.*/app/player/* and the unused <sstream> in App.cpp go to final cleanup after r5-fix-player. Test exe: r5-v6. ⏳ r5-fix-library started on top of 67222df; it also takes the window-drag relayout and menu filter highlighting.
6. ✅ r5-tools-sweep merged: ui-audit.py, sweep.ps1, scenarios.json, make-worstcase.ps1, audit-baseline.md (289 scenarios in 6 min). The dump clip flag now uses glyph ink (f4bdf30). Test exe with it: %TEMP%\lumen-qa\r5-v3. The baseline's dialog items went to r5-fix-settings and its onboarding items to r5-fix-audit.
7. ✅ Hunters r5-player-eye (player-visual.md, PV-01…PV-18) and r5-settings-eye (settings-dialogs.md, SD-01…SD-22), both merged.
   - ✅ Fixer r5-fix-settings merged (e9cd441): SD-01…SD-22, W-08, baseline #1/#2/#3/#5/#6/#14-16, the About dialog. Skipped: menu filter highlighting (MenuPopup conflict); this can be done after r5-fix-audit. Test exe: r5-v5.
   - ✅ Fixer r5-fix-player merged (e0edf71): PV-01…PV-18, P-07, and the design gaps. The dead code and icon layer bounds were done by the coordinator (6c3138a). Left for after r5-fix-library: move PanelScroll's resting thumb and the "overflow ≤ 4 DIP doesn't scroll" rule into ui::ScrollView (Controls.cpp) so Settings and dialogs get them, and cache the volume wedge geometry in ControlBar. Owner decision: merge the tracks popover with quick settings, or not.
8. ✅ (library.md merged, LB-01…LB-18 plus gaps; LB-01 = audit #1, done by r5-fix-audit) ☐ Fixer r5-fix-library: start AFTER r5-fix-audit is merged, since they share src/app/library/*, LibraryCommon and MediaLibrary. It covers LB-02…LB-18, re-checks W-12/W-13 and baseline #8/#10/#11, plus the "Продолжить просмотр" tile thumbnail (a frame from about 2:20) noted by r5-fix-flows. Hunter r5-library-eye: library, home, shell and the worst-case data. It starts after step 6, once the media is ready.
8b. ✅ r5-fix-library merged (fe7577d). Test exe with everything: %TEMP%\lumen-qa\r5-v8.
8c. ⏳ r5-leftovers (ScrollView behaviour, volume wedge cache, player tooltips, full sweep and fixing what remains) and r5-verify (regression check of every fix, a fresh hunt for new bugs, the sweep compared with the baseline; report verify.md). After both: merge, fix the VF findings, final build.
9. ✅ Fixers for the steps 7–8 reports, split by disjoint files: player / library+shell / settings+dialogs+ui.
10. ☐ Final verification: rebuild, the full sweep, the audit is clean (or every remaining finding is justified), tests and checks pass, and a few owner scenarios replayed. Rebuild build\Lumen.exe only when the owner's Lumen is not running.
11. ☐ STATUS.md: a section "Раунд 5", README if needed, memory. Then the report to the owner, then sleep.

Merge each branch with `git merge --no-ff -F .git\MSG`. After each merge, rebuild r5-v2.

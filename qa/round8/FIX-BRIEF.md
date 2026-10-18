# Round 8: fixing the ponytail audit, brief for the fixers

Read first:
- qa/round5/COMMON.md: hard rules, build, tests, tools, git;
- qa/round8/AUDIT-BRIEF.md: what the audit looked for;
- your raw findings file in qa/round8/raw/.

The audit was done by reading code only, so every finding must be confirmed before it is fixed:
- **confirm in the code**, by tracing the case;
- **and at runtime where practical**: background `--automation` instances only, -Library with test folders, never the owner's data;
- **if the case doesn't hold**, mark the finding rejected with the reason.

The owner asked for "all possible fixes":
- **Numbered findings:** fix all of them.
- **"Left out" smaller items:** fix them too when they are real and the fix is small and safe.
- **Lean findings:** apply them: delete dead code, merge copies.
- **No new features,** no new layers, no config the problem doesn't need. Prefer fixes that delete code.

Add a test whenever the finding names one or touches a parser or a store. Make sure the test fails on the old code.

Performance stays sacred: idle and paused CPU 0, no UI-thread blocking. Measure where a finding is about speed, and report before/after.

Verify:
- build at /W4 with no warnings;
- tests\build-tests.bat;
- check-icons.py, check-settings.py, i18n_check.py;
- `tools\dev\sweep.ps1 -Filter <your area>`: no new medium or high finding.

Commit after each fix or small group of fixes.

Write qa/round8/fixes-<area>.md, one row per finding (numbered and left-out):
- status: fixed / rejected / partial;
- how you checked it, before and after;
- file:line.

Final message: that table in short form, plus the branch name, plus anything you had to touch outside your scope.

# UI/UX design review 2026-10-20: brief for area reviewers

Method: the `ui-design-review` skill (rknall/claude-skills), installed at
`C:\Users\wheny\.claude\skills\ui-design-review\` (`SKILL.md` and `wcag-checklist.md`). Read the
section of SKILL.md for your area and apply it to a **native Windows app**: UI Automation replaces
ARIA/HTML, DIPs replace px, Windows conventions (Fluent and Windows app guidelines) replace browser ones.
Skip web-only checks (skip links, HTML validity, mobile breakpoints) and say once that you skipped them.

## Read first

1. `qa/REVIEWER-BRIEF.md`: how to drive the app, tools and the **hard rules**. They are mandatory:
   invisible `--automation` instances only (`tools\dev\launch.ps1`); never touch the real mouse, keyboard
   or focus; kill only your own PIDs; no native dialogs, Explorer, registry or file associations; no sleep.
   Onboarding pre-fills the owner's real Videos folder: type the test media path into the folder field
   before pressing Continue.
2. `qa/research/UX-AUDIT-2026-10-19.md` (section 6 and section 11). Do not re-report what it already
   found and fixed. A finding it listed that is **still** present is reported as "still open (audit #N)".
3. `design/DESIGN.md` (tokens), `qa/visual-rules.md`, `qa/ESCAPES.md`.

## What you test

- Executable: `%TEMP%\lumen-qa\uir-build\Lumen.exe` (built from `main` at ff96c7f). Never use or kill
  `build\Lumen.exe`.
- Media: `%TEMP%\lumen-qa\media\` (`Videos`, `Music`, `Mixed`). Worst-case data: `qa/make-worstcase.ps1`.
- A full state sweep (both themes, worst-case data, scale) is at `%TEMP%\lumen-qa\uir-sweep\`
  (`index.html` + PNGs + widget-tree dumps + ui-audit results). Use it before launching your own instance.
- Your data dirs and temp files use the prefix given in your task (`%TEMP%\lumen-qa\uir-<area>-*`).
- Source: `src\` (read only; **never edit the repository** except your own report file).

## Report

Write `qa/research/ui-review-2026-10-20/area-<area>.md`, in English, with:

- **Strengths** (3-6 bullets, concrete).
- **Findings**, most severe first. Severity per the skill: Critical (blocks core use, WCAG A, full block
  for keyboard or screen reader users), High (WCAG AA, major usability), Medium (friction with a
  workaround, AAA, visual inconsistency), Low (polish). Each finding:

  ```
  ### <AREA>-<n>. <one-line title>
  - Severity: High · Standard: WCAG 2.2 1.4.3 / Nielsen #4 / Windows guideline <name>
  - Where: screen + state; code `src/...:line` when found
  - Evidence: what you measured or saw (screenshot path, UIA dump line, contrast ratio with both colors)
  - Impact: who is affected and how
  - Fix: one concrete change in this codebase's terms (widget, Theme token, Host, Actions...)
  - Verify: how to check the fix (tool + expected result)
  ```

- Only what you observed or proved from code. Mark code-only conclusions "(from code)".
  At most 25 findings; a few precise High findings beat many vague nits.
- Last line: what you could not check and why.

Close all your instances before you finish.

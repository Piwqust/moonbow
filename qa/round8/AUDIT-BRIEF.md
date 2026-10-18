# Round 8: ponytail audit, brief for the area reviewers

You audit one area of Lumen with the rules of the `ponytail-audit` skill. You only read code; you never edit files. Lumen is a native Windows media player. It is built with C++20 and Win32, draws its own UI with Direct2D/DirectComposition, and plays through libmpv. Its data is stored as JSON in the data folder.

Assumed load: one person on one PC, with a library of up to tens of thousands of files. The UI thread must never block, and idle CPU must stay at 0.

These were already audited and fixed; don't report them again unless the fix itself is wrong:
- qa/AUDIT-PONYTAIL.md;
- the fixes in qa/round5/audit-fixes.md and qa/round5/flows.md ("Audit items").

Round 7 added a lot of new code (see qa/round7/*.md). Look hardest there:
- src/audio/;
- src/net/;
- src/library/TagReader.*, FileSettings.*;
- src/core/Skin.*, src/app/Skins.*;
- src/app/player/SubWords.*, SubText.h;
- the crossfade in Player.cpp;
- the LiveState vf/hwdec ownership;
- the per-file settings capture.

## What to look for, in order

1. **Bug:** wrong result, crash, missed edge case, callers that disagree with what a function returns, the same rule applied differently in two places.
2. **Risk:** security (input from files or the network: tag parsers, JSON, HTTP responses, URL templates, skin files, path handling), data loss (errors swallowed, writes in the wrong order), threads (data races, use after free from callbacks after an object is gone, joins on the UI thread).
3. **Scale:** fine for one file, wrong for a 20 000-file library: O(n²), a list that only grows, the same work repeated, a per-item disk read on the UI thread.
4. **Missing test:** a risky parser or state machine with no test that fails when it breaks.
5. **Speed:** real slowdowns on hot paths (paint, layout, scan, per-frame work).
6. **Lean:**
   - dead code (grep the whole tree, tests and strings included, before saying unused);
   - two helpers doing the same job;
   - near-copies;
   - a function doing several unrelated jobs.

## Rules

- Every finding has a concrete case: this input or situation leads to this wrong result. No case, no finding.
- A `shortcut:` or `ponytail:` comment that names its limit is a decision, not a finding.
- Propose the smallest fix; prefer fixes that delete code.
- No style taste and no "consider".

## Your output (your final message)

Up to 12 findings, most important first. For each one give:
- **group:** must / should / nice;
- **title;**
- **file:line;**
- **what this is** (one sentence);
- **the problem,** with the concrete case;
- **the fix** (smallest);
- **if we skip it;**
- **how sure you are:** checked in the code, or likely.

Also give how many smaller findings you left out, and what in your area you didn't read.

Write in plain English: short sentences, everyday words.

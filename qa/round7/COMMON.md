COMMON RULES (Lumen: STATUS.md section 7, the next features)

Read qa/round5/COMMON.md first. Its hard rules, build steps, git rules and tools all apply here unchanged. Then read:
- STATUS.md sections 6б and 7;
- qa/research/MARKET.md section 7, the rows for your feature;
- design/DESIGN.md.

What's different in this round:
- **New features, built to the round 5 quality bar.**
  - Run your area's scenarios with tools\dev\sweep.ps1 before and after (`-Filter`). Your change must not add any medium or high ui-audit finding.
  - Add scenarios for your new screens to qa/round5/scenarios.json.
  - Russian and English strings; both themes; scales 1.0 and 1.5; minimum window size.
  - Keyboard access and tooltips for cut text.
- **Performance is sacred.** Measure before and after with LUMEN_TRACE, Get-Process CPU/memory and qa/perf scripts where relevant. Report the numbers. Rules:
  - an inactive feature costs nothing: no thread, no timer, no allocation;
  - idle and paused CPU stays 0;
  - nothing blocks the UI thread.
- **Settings.** A setting already declared with `.Hidden();  // not implemented yet` is implemented by removing `.Hidden()`. New settings go in src/core/SettingsSchema.cpp with Russian and English labels. check-settings.py must stay clean.
- **Tests.** Add unit tests in tests/core_tests.cpp for any logic that can be tested without a window (parsers, matching, state machines).
- **No network.** No downloads, no external binaries. If a feature would need one, stop and say so in your final message.
- **Test libraries.** Always launch test instances with -Library pointing to a test folder: %TEMP%\lumen-qa\media, media2, worst, or your own <prefix>-media.
- **Write-up.** Put a section for your feature in qa/round7/<yourname>.md with:
  - what was built;
  - how to use it;
  - how you verified it (with evidence paths);
  - the perf numbers;
  - what you left out and why.

Final message:
- the summary;
- the branch name;
- any change needed outside your file scope.

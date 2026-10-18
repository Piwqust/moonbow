# Round 5: Settings, dialogs, menus, onboarding, frame (vision + tree hunt)

Hunter: r5-settings-eye. Work in progress; findings are added as they are confirmed.

- **Exe.** `%TEMP%\lumen-qa\r5-v2\Lumen.exe`, prebuilt; nothing was rebuilt.
- **Instances.** `%TEMP%\lumen-qa\r5se-<n>`, all `--automation` via `tools\dev\launch.ps1`.
- **Evidence.** `%TEMP%\lumen-qa\r5se\` (file names below; `*-crop.png` are 2-6x nearest-neighbour zooms).
- **Screen.** 2560x1440 at 100 %; other scales through `--scale`.

## Findings (draft)

- SD-01 EQ preset label lies after a band is moved (confirmed).
- SD-02 Shortcuts dialog drops every binding after the second one (confirmed).
- SD-03 A menu stays open under a dialog opened by a shortcut (confirmed).
- SD-04 Disabled buttons and dropdowns look live in the light theme (confirmed).
- SD-05 "Удалить" in Extensions is enabled with an empty list (confirmed).

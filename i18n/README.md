# Translating Moonbow

Moonbow is written in English and has Russian built in. Any other language is a translation file: a flat JSON
object where each key is an English string of the interface and its value is the translation.

```json
{
  "@language": "Deutsch",
  "Playback": "Wiedergabe",
  "{0} items": "{0} Elemente"
}
```

- `en.json` is the source: every English string, the key equal to the value. `ru.json` is the built-in Russian, for
  reference. Weblate and Crowdin read both as "JSON (flat), monolingual" with `en.json` as the base file.
- Name the file by the language code: `de.json`, `fr.json`, `pt-BR.json`. `@language` is the name the language list
  shows, in the language itself.
- Keep `{0}`, `{1}` and keyboard names as they are. A string you leave out shows in English.
- A Ukrainian translation is not accepted: Moonbow does not load `uk.json` (the owner's decision).

## Try a translation

Put the file in `%APPDATA%\Moonbow\i18n\` (or `Data\i18n\` next to a portable copy), restart Moonbow and pick the
language in **Settings → General → Interface language**.

## Update the source after changing the interface

```bat
build.bat
build\Moonbow.exe --export-strings i18n
```

`--export-strings <folder>` writes `en.json` and `ru.json` from the app itself: the settings schema and every
string in `src/app/strings/*.inc`.

## Shipping a translation

A finished file goes to `i18n/` in the repository. Moonbow also reads translations from an `i18n` folder next to
`Moonbow.exe`; the release packaging does not copy that folder yet, so add it to `tools/package-release.ps1` along
with the first translation that ships.

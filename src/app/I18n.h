#pragma once
#include <string>
#include <string_view>
#include <vector>

// UI strings are written in English (as in the Figma design) and translated at
// runtime. Russian is the default language.
//
//   Tr(L"Continue Watching")          -> L"Продолжить просмотр"
//   Fmt(Tr(L"{0} items"), n)          -> L"110 элементов" (see Plural below)
//
// Translations live in src/app/strings/*.inc, one file per area, as
// `{L"English", L"Русский"},` lines.
namespace app {

enum class Language { Russian, English };

void SetLanguage(Language lang);
Language CurrentLanguage();

// Returns the translation of `english`, or `english` itself. The pointer stays valid.
const wchar_t* Tr(const wchar_t* english);

// Replaces {0}, {1}... with the arguments.
std::wstring Fmt(std::wstring_view pattern, std::initializer_list<std::wstring> args);
std::wstring Fmt(std::wstring_view pattern, long long n);

// --- More languages from translation files ----------------------------------------------------
// <exe>\i18n\<code>.json (shipped) and <data>\i18n\<code>.json (the user's own): a flat JSON object
// {"English text": "translation", "@language": "the language's own name"}, the format Weblate and
// Crowdin read and write (en.json, the source, comes from `Moonbow.exe --export-strings <folder>`).
// A translation shows on top of English: every string it has replaces the English one.
struct Translation {
    std::string code;   // the file name: "de", "fr", "pt-BR"
    std::wstring name;  // "@language", else the code
    std::wstring path;
};
const std::vector<Translation>& Translations();  // found once (the data folder must be set)
void SetTranslation(const std::string& code);    // empty: none; English must be the language
bool TranslationOn();
// The translation of an English string while one is on, else the string itself; the reference
// stays valid.
const std::wstring& TrText(const std::wstring& english);
// Writes en.json (every English string of the interface: the source for translators) and ru.json
// (the built-in Russian) into `folder`. False when a file can't be written.
bool ExportStrings(const std::wstring& folder);

// Russian plural form: Plural(5, L"элемент", L"элемента", L"элементов") -> "элементов".
// In English returns `one` for 1, `many` otherwise.
const wchar_t* Plural(long long n, const wchar_t* one, const wchar_t* few, const wchar_t* many);
// "110 items" / "110 элементов".
std::wstring CountItems(long long n);

}  // namespace app

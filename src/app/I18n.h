#pragma once
#include <string>
#include <string_view>

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

// Russian plural form: Plural(5, L"элемент", L"элемента", L"элементов") -> "элементов".
// In English returns `one` for 1, `many` otherwise.
const wchar_t* Plural(long long n, const wchar_t* one, const wchar_t* few, const wchar_t* many);
// "110 items" / "110 элементов".
std::wstring CountItems(long long n);

}  // namespace app

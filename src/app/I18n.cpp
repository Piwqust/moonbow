#include "app/I18n.h"
#include "ui/Controls.h"

#include <cwchar>
#include <string>
#include <unordered_map>

namespace app {

namespace {

struct Pair {
    const wchar_t* en;
    const wchar_t* ru;
};

const Pair kRussian[] = {
#include "app/strings/shell.inc"
#include "app/strings/player.inc"
#include "app/strings/library.inc"
#include "app/strings/settings.inc"
#include "app/strings/dialogs.inc"
#include "app/strings/onboarding.inc"
#include "app/strings/online.inc"
    {nullptr, nullptr},
};

Language g_language = Language::Russian;

const std::unordered_map<std::wstring_view, const wchar_t*>& RussianMap() {
    static const auto map = [] {
        std::unordered_map<std::wstring_view, const wchar_t*> m;
        for (const Pair& p : kRussian)
            if (p.en) m.emplace(p.en, p.ru);
        return m;
    }();
    return map;
}

}  // namespace

void SetLanguage(Language lang) {
    g_language = lang;
    ui::SetToggleCaptions(Tr(L"ON"), Tr(L"OFF"));
    ui::SetMenuNoMatchCaption(Tr(L"Nothing found"));
    ui::SetTextMenuCaptions(Tr(L"Cut"), Tr(L"Copy"), Tr(L"Paste"), Tr(L"Select all"));
}

Language CurrentLanguage() {
    return g_language;
}

const wchar_t* Tr(const wchar_t* english) {
    if (g_language == Language::English || !english) return english;
    const auto& map = RussianMap();
    auto it = map.find(english);
    return it != map.end() ? it->second : english;
}

std::wstring Fmt(std::wstring_view pattern, std::initializer_list<std::wstring> args) {
    std::wstring out;
    out.reserve(pattern.size() + 16);
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == L'{' && i + 2 < pattern.size() && pattern[i + 2] == L'}' && pattern[i + 1] >= L'0' &&
            pattern[i + 1] <= L'9') {
            const size_t k = static_cast<size_t>(pattern[i + 1] - L'0');
            if (k < args.size()) out += *(args.begin() + k);
            i += 2;
        } else {
            out.push_back(pattern[i]);
        }
    }
    return out;
}

std::wstring Fmt(std::wstring_view pattern, long long n) {
    return Fmt(pattern, {std::to_wstring(n)});
}

const wchar_t* Plural(long long n, const wchar_t* one, const wchar_t* few, const wchar_t* many) {
    if (g_language == Language::English) return n == 1 ? one : many;
    const long long m10 = n % 10, m100 = n % 100;
    if (m10 == 1 && m100 != 11) return one;
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
    return many;
}

std::wstring CountItems(long long n) {
    if (g_language == Language::English) return std::to_wstring(n) + (n == 1 ? L" item" : L" items");
    return std::to_wstring(n) + L" " + Plural(n, L"элемент", L"элемента", L"элементов");
}

}  // namespace app

#include "app/I18n.h"
#include "Util.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "core/SettingsSchema.h"
#include "ui/Controls.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <map>
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
#include "app/strings/setup.inc"
#include "app/strings/updates.inc"
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
    ui::SetMenuNoMatchCaption(Tr(L"Nothing found"));
    ui::SetTextMenuCaptions(Tr(L"Cut"), Tr(L"Copy"), Tr(L"Paste"), Tr(L"Select all"));
    ui::a11y::SetCulture(lang == Language::English ? 0x0409 : 0x0419);  // en-US, ru-RU
}

Language CurrentLanguage() {
    return g_language;
}

// A translation file on top of English (SetTranslation).
std::unordered_map<std::wstring, std::wstring> g_overlay;
bool g_overlayOn = false;

const wchar_t* Tr(const wchar_t* english) {
    if (g_overlayOn && english) {
        const auto it = g_overlay.find(english);
        return it != g_overlay.end() ? it->second.c_str() : english;
    }
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

// --- translation files ----------------------------------------------------------------------

const std::vector<Translation>& Translations() {
    static const std::vector<Translation> list = [] {
        std::map<std::string, Translation> found;  // by code: the user's own file wins over Moonbow's
        for (const std::wstring& dir : {ExeDir() + L"i18n", DataDir().empty() ? std::wstring() : DataDir() + L"\\i18n"}) {
            if (dir.empty()) continue;
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            do {
                std::wstring file = fd.cFileName;
                const std::string code = json::ToUtf8(file.substr(0, file.size() - 5));
                if (code == "en" || code == "ru" || code.empty() || code.size() > 16) continue;  // built in
                if (_stricmp(code.c_str(), "uk") == 0 || _strnicmp(code.c_str(), "uk-", 3) == 0) continue;  // not offered
                std::string text;
                const std::wstring path = dir + L"\\" + file;
                if (!json::ReadFileUtf8(path, &text)) continue;
                const auto doc = json::Parse(text);
                if (!doc || !doc->IsObject()) continue;
                Translation t{code, doc->GetWString("@language"), path};
                if (t.name.empty()) t.name = json::ToWide(code);
                found[code] = std::move(t);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::vector<Translation> out;
        for (auto& [code, t] : found) out.push_back(std::move(t));
        return out;
    }();
    return list;
}

void SetTranslation(const std::string& code) {
    g_overlay.clear();
    g_overlayOn = false;
    for (const Translation& t : Translations()) {
        if (t.code != code) continue;
        std::string text;
        if (!json::ReadFileUtf8(t.path, &text)) return;
        const auto doc = json::Parse(text);
        if (!doc) return;
        for (const json::Member& m : doc->AsObject())
            if (!m.key.empty() && m.key[0] != '@' && m.value.IsString() && !m.value.AsString().empty())
                g_overlay[json::ToWide(m.key)] = m.value.AsWString();
        g_overlayOn = true;
    }
}

bool TranslationOn() {
    return g_overlayOn;
}

const std::wstring& TrText(const std::wstring& english) {
    if (!g_overlayOn) return english;
    const auto it = g_overlay.find(english);
    return it != g_overlay.end() ? it->second : english;
}

bool ExportStrings(const std::wstring& folder) {
    std::map<std::string, std::string> en, ru;  // sorted by the English text
    auto add = [&](const std::wstring& english, const std::wstring& russian) {
        if (english.empty()) return;
        const std::string key = json::ToUtf8(english);
        en[key] = key;
        if (!russian.empty()) ru[key] = json::ToUtf8(russian);
    };
    for (const Pair& p : kRussian)
        if (p.en) add(p.en, p.ru);
    for (const settings::Group& g : settings::Groups()) add(g.label, g.labelRu);
    for (const settings::Category& c : settings::Categories()) {
        add(c.label, c.labelRu);
        for (const settings::Section& s : c.sections) {
            add(s.title, s.titleRu);
            for (const settings::Item& it : s.items) {
                if (it.hidden) continue;
                add(it.label, it.labelRu);
                add(it.description, it.descriptionRu);
                add(it.unit, it.unitRu);
                add(it.placeholder, it.placeholderRu);
                add(it.actionLabel, it.actionLabelRu);
                add(it.summary, it.summaryRu);
                for (const settings::Choice& ch : it.choices) add(ch.label, ch.labelRu);
            }
        }
    }
    add(settings::AdvancedOptionsLabel(settings::Lang::En), settings::AdvancedOptionsLabel(settings::Lang::Ru));
    auto write = [&folder](const wchar_t* name, const std::map<std::string, std::string>& strings, const char* language) {
        json::Value doc = json::Value::MakeObject();
        doc.Set("@language", language);
        for (const auto& [k, v] : strings) doc.Set(k, v);
        return json::WriteFileAtomic(folder + L"\\" + name, json::Serialize(doc, true));
    };
    CreateDirectoryW(folder.c_str(), nullptr);
    return write(L"en.json", en, "English") && write(L"ru.json", ru, "Русский");
}

std::wstring CountItems(long long n) {
    if (g_language == Language::English) return std::to_wstring(n) + (n == 1 ? L" item" : L" items");
    return std::to_wstring(n) + L" " + Plural(n, L"элемент", L"элемента", L"элементов");
}

}  // namespace app

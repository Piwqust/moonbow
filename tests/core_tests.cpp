// Unit tests for src/core, src/library and the LRC parser. Build and run with tests\build-tests.bat.
// A tiny runner: CHECK() records failures and keeps going; the exit code is the failure count.
// Every test works inside a fresh temp folder; SetDataDirForTests keeps %APPDATA%\Lumen untouched.

#include "../src/core/Config.h"
#include "../src/core/Json.h"
#include "../src/core/Paths.h"
#include "../src/core/SettingsSchema.h"
#include "../src/app/player/Lyrics.h"
#include "../src/library/Bookmarks.h"
#include "../src/library/History.h"
#include "../src/library/MediaLibrary.h"
#include "../src/library/MediaTypes.h"
#include "../src/library/Playlists.h"
#include "../src/library/Thumbnails.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;
std::wstring g_temp;  // per-run temp root

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

#define CHECK_MSG(cond, ...)                                                 \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);  \
            std::printf(__VA_ARGS__);                                        \
            std::printf("\n");                                               \
        }                                                                    \
    } while (0)

std::string U8(const std::wstring& w) {
    return json::ToUtf8(w);
}

// --- helpers --------------------------------------------------------------------------------

void DeleteTree(const std::wstring& dir) {
    // Removes only folders this test run created under %TEMP%.
    std::wstring from = dir + L'\0';
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

std::wstring MakeDir(const std::wstring& name) {
    std::wstring dir = g_temp + L"\\" + name;
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return dir;
}

void WriteFile(const std::wstring& path, std::string_view data) {
    size_t slash = path.find_last_of(L'\\');
    SHCreateDirectoryExW(nullptr, path.substr(0, slash).c_str(), nullptr);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    ::WriteFile(f, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(f);
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

size_t CountFiles(const std::wstring& dir, const wchar_t* pattern) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    size_t n = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

// Points the data dir at a fresh, empty folder for the current test.
std::wstring FreshDataDir(const wchar_t* name) {
    std::wstring dir = g_temp + L"\\data-" + name;
    DeleteTree(dir);
    SetDataDirForTests(dir);
    return dir;
}

HWND MessageWindow() {
    static HWND hwnd = [] {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LumenTestsMsg";
        RegisterClassW(&wc);
        return CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    }();
    return hwnd;
}

// Pumps messages until `pred(msg)` is true for a message to our window, or the timeout ends.
template <typename Pred>
bool WaitForMessage(UINT id, DWORD timeoutMs, Pred pred) {
    ULONGLONG end = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < end) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == id && pred(msg)) return true;
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    return false;
}

// --- JSON -----------------------------------------------------------------------------------

void TestJsonBasics() {
    auto v = json::Parse(R"({"a": 1, "b": [true, false, null], "c": {"d": "x"}, "e": -2.5e3})");
    CHECK(v.has_value());
    CHECK(v->IsObject());
    CHECK(v->GetNumber("a") == 1);
    CHECK(v->Find("b")->size() == 3);
    CHECK((*v->Find("b"))[0].AsBool() == true);
    CHECK((*v->Find("b"))[2].IsNull());
    CHECK((*v->Find("b"))[9].IsNull());  // out of range -> null
    CHECK(v->Find("c")->GetString("d") == "x");
    CHECK(v->GetNumber("e") == -2500);
    CHECK(v->GetString("missing", "dflt") == "dflt");
    CHECK(v->GetBool("a", true) == true);  // wrong type -> fallback
    CHECK(v->GetInt("a") == 1);

    // Insertion order is preserved and duplicate keys keep the first position, last value.
    auto o = json::Parse(R"({"z":1,"a":2,"m":3,"a":4})");
    CHECK(o.has_value());
    CHECK(o->size() == 3);
    CHECK(o->AsObject()[0].key == "z");
    CHECK(o->AsObject()[1].key == "a");
    CHECK(o->GetNumber("a") == 4);
    CHECK(json::Serialize(*o) == R"({"z":1,"a":4,"m":3})");

    json::Value built;
    built["x"] = 1;
    built["y"].Push("s");
    built["y"].Push(json::Value());
    built.Set("x", 2);
    CHECK(json::Serialize(built) == R"({"x":2,"y":["s",null]})");
    CHECK(built.Erase("x"));
    CHECK(!built.Erase("x"));
    CHECK(json::Serialize(json::Value::MakeArray()) == "[]");
    CHECK(json::Serialize(json::Value::MakeObject(), true) == "{}\n");

    // Whitespace and a BOM are accepted.
    auto ws = json::Parse("\xEF\xBB\xBF \r\n\t[ 1 ,\n 2 ] \n");
    CHECK(ws.has_value() && ws->size() == 2);
}

void TestJsonStrings() {
    auto s = json::Parse(R"("a\"b\\c\/d\b\f\n\r\t")");
    CHECK(s.has_value() && s->AsString() == "a\"b\\c/d\b\f\n\r\t");

    // \u escapes: BMP, Cyrillic, surrogate pair (U+1F600), lone surrogates -> U+FFFD.
    auto u = json::Parse(R"("\u0041\u00e9\u0416\ud83d\ude00")");
    CHECK(u.has_value() && u->AsString() == "A\xC3\xA9\xD0\x96\xF0\x9F\x98\x80");
    auto lone = json::Parse(R"("x\ud800y\udc00z")");
    CHECK(lone.has_value() && lone->AsString() == "x\xEF\xBF\xBDy\xEF\xBF\xBDz");
    auto hiThenBmp = json::Parse(R"("\ud83d\u0041")");
    CHECK(hiThenBmp.has_value() && hiThenBmp->AsString() == "\xEF\xBF\xBD" "A");
    CHECK(!json::Parse(R"("\u12")"));
    CHECK(!json::Parse(R"("\x41")"));
    CHECK(!json::Parse("\"tab\there\""));  // raw control characters are invalid
    CHECK(!json::Parse("\"unterminated"));

    // Serialization escapes control characters and keeps UTF-8 as is.
    json::Value v(std::string("q\"\\\n\x01 Привет 😀"));
    std::string out = json::Serialize(v);
    CHECK(out == "\"q\\\"\\\\\\n\\u0001 Привет 😀\"");
    auto back = json::Parse(out);
    CHECK(back.has_value() && *back == v);

    // Wide helpers.
    std::wstring w = L"Фильм ё 😀";
    CHECK(json::ToWide(json::ToUtf8(w)) == w);
    CHECK(json::ToWide("").empty());
    CHECK(json::Value(std::string(json::ToUtf8(w))).AsWString() == w);
}

void TestJsonNumbers() {
    struct Case {
        const char* text;
        double value;
    };
    const Case ok[] = {{"0", 0}, {"-0", 0}, {"7", 7}, {"-12", -12}, {"3.25", 3.25}, {"1e3", 1000}, {"1E-2", 0.01},
                       {"2.5e+2", 250}, {"9007199254740991", 9007199254740991.0}};
    for (const Case& c : ok) {
        auto v = json::Parse(c.text);
        CHECK_MSG(v.has_value() && v->IsNumber() && v->AsNumber() == c.value, "%s", c.text);
    }
    for (const char* bad : {"01", "1.", ".5", "+1", "-", "1e", "1e+", "0x10", "NaN", "Infinity", "--1", "1.2.3"})
        CHECK_MSG(!json::Parse(bad).has_value(), "%s", bad);

    CHECK(json::FormatNumber(0) == "0");
    CHECK(json::FormatNumber(-0.0) == "0");
    CHECK(json::FormatNumber(42) == "42");
    CHECK(json::FormatNumber(-7) == "-7");
    CHECK(json::FormatNumber(0.1) == "0.1");
    CHECK(json::FormatNumber(1e300) == "1e+300");
    CHECK(json::FormatNumber(1.0 / 3.0) == "0.3333333333333333");
    CHECK(json::FormatNumber(std::nan("")) == "null");
    CHECK(json::Serialize(json::Value(HUGE_VAL)) == "null");

    // Round trip of awkward doubles is exact.
    for (double d : {0.1, 1.0 / 3.0, 123456.789, -1e-300, 5e-324, 1.7976931348623157e308, 9007199254740993.0}) {
        auto v = json::Parse(json::FormatNumber(d));
        CHECK_MSG(v.has_value() && v->AsNumber() == d, "%.17g", d);
    }
    // Out of range: overflow clamps, underflow becomes zero (no inf in the DOM).
    auto big = json::Parse("1e400");
    CHECK(big.has_value() && std::isfinite(big->AsNumber()) && big->AsNumber() > 1e308);
    auto tiny = json::Parse("1e-400");
    CHECK(tiny.has_value() && tiny->AsNumber() == 0);
    CHECK(json::Value(2.6).AsInt() == 3);
    CHECK(json::Value(DWORD{7}).AsInt() == 7 && json::Value(LONG{-7}).AsInt() == -7);
    CHECK(json::Value("x").AsInt(5) == 5);
}

void TestJsonErrors() {
    for (const char* bad : {"", "   ", "[1,]", "{\"a\":1,}", "{\"a\" 1}", "{a:1}", "[1 2]", "{\"a\":1} x", "nul",
                            "tru", "[", "{", "\"", "[1,", "{\"a\":"}) {
        std::string err;
        CHECK_MSG(!json::Parse(bad, &err).has_value(), "accepted: %s", bad);
        CHECK_MSG(!err.empty(), "no error text for: %s", bad);
    }
    std::string err;
    CHECK(!json::Parse("{\n  \"a\": 1\n  \"b\": 2\n}", &err));
    CHECK_MSG(err.find("line 3") != std::string::npos, "%s", err.c_str());

    // Depth limit protects the stack.
    std::string deep(300, '[');
    deep += std::string(300, ']');
    CHECK(!json::Parse(deep));
    std::string ok(200, '[');
    ok += std::string(200, ']');
    CHECK(json::Parse(ok).has_value());
}

void TestJsonRoundTrip() {
    const char* doc = R"({"name":"Lumen","list":[1,2.5,-3,true,false,null,"","текст"],
        "nested":{"a":{"b":{"c":[[],{}]}}},"esc":"\u0007\u001f","num":1e-7})";
    auto v = json::Parse(doc);
    CHECK(v.has_value());
    for (bool pretty : {false, true}) {
        std::string text = json::Serialize(*v, pretty);
        auto again = json::Parse(text);
        CHECK(again.has_value() && *again == *v);
        CHECK(json::Serialize(*again, pretty) == text);  // stable
    }
    std::string pretty = json::Serialize(*v, true);
    CHECK(pretty.find("\n  \"name\": \"Lumen\"") != std::string::npos);
    CHECK(json::Value(1) == json::Value(1.0));
    CHECK(json::Value(1) != json::Value(true));
    CHECK(json::Value("a") != json::Value("b"));
}

void TestJsonFiles() {
    std::wstring dir = MakeDir(L"json-файлы");
    std::wstring path = dir + L"\\вложенная\\настройки.json";  // parent folder is created on write
    CHECK(json::WriteFileAtomic(path, "{\"a\":1}"));
    std::string text;
    CHECK(json::ReadFileUtf8(path, &text));
    CHECK(text == "{\"a\":1}");
    CHECK(json::WriteFileAtomic(path, "second"));  // replaces
    CHECK(json::ReadFileUtf8(path, &text) && text == "second");
    CHECK(CountFiles(dir + L"\\вложенная", L"*.tmp") == 0);  // no temp leftovers

    WriteFile(dir + L"\\bom.json", "\xEF\xBB\xBF{}");
    CHECK(json::ReadFileUtf8(dir + L"\\bom.json", &text) && text == "{}");
    CHECK(json::WriteFileAtomic(dir + L"\\empty.json", ""));
    CHECK(json::ReadFileUtf8(dir + L"\\empty.json", &text) && text.empty());
    CHECK(!json::ReadFileUtf8(dir + L"\\missing.json", &text));
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
}

// --- Settings schema -------------------------------------------------------------------------

void TestSchema() {
    using namespace settings;
    const char* kExpected[] = {"general", "interface", "privacy", "controls", "playback", "video", "audio",
                               "subtitles", "library", "network", "performance", "recording", "extensions"};
    const auto& cats = Categories();
    CHECK(cats.size() == std::size(kExpected));
    for (size_t i = 0; i < cats.size() && i < std::size(kExpected); ++i) CHECK(cats[i].id == kExpected[i]);
    CHECK(Groups().size() == 3);

    std::set<std::string> keys;
    std::map<std::string, MpvBuilder> builders;  // option -> builder, must agree
    size_t total = 0, mapped = 0;
    std::printf("  settings per category:");
    for (const Category& c : cats) {
        CHECK(!c.label.empty() && !c.labelRu.empty() && !c.title.empty() && !c.titleRu.empty() && !c.icon.empty());
        bool groupOk = false;
        for (const Group& g : Groups()) groupOk = groupOk || g.id == c.group;
        CHECK_MSG(groupOk, "%s", c.id.c_str());
        CHECK(FindCategory(c.id) == &c);
        size_t count = 0;
        for (const Section& s : c.sections) {
            CHECK_MSG(!s.title.empty() && !s.titleRu.empty() && !s.items.empty(), "%s.%s", c.id.c_str(), s.id.c_str());
            for (const Item& it : s.items) {
                ++total;
                if (!it.hidden) ++count;
                const char* k = it.key.c_str();
                CHECK_MSG(keys.insert(it.key).second, "duplicate key %s", k);
                CHECK_MSG(it.key.compare(0, c.id.size() + 1, c.id + ".") == 0, "%s outside %s", k, c.id.c_str());
                CHECK_MSG(it.category == c.id && it.section == s.id, "%s", k);
                CHECK_MSG(FindItem(it.key) == &it, "%s", k);
                CHECK_MSG(!it.label.empty() && !it.labelRu.empty(), "%s label", k);
                CHECK_MSG(it.description.empty() == it.descriptionRu.empty(), "%s description pair", k);
                CHECK_MSG(it.unit.empty() == it.unitRu.empty(), "%s unit pair", k);
                CHECK_MSG(it.placeholder.empty() == it.placeholderRu.empty(), "%s placeholder pair", k);
                CHECK_MSG(it.actionLabel.empty() == it.actionLabelRu.empty(), "%s caption pair", k);
                CHECK_MSG(it.label != it.labelRu || it.label.find_first_of(L"0123456789") != std::wstring::npos ||
                              it.label == L"Replay gain" || it.label == L"User agent",
                          "%s not translated", k);

                if (it.HasValue()) {
                    std::optional<json::Value> v = Config::Validate(it, it.def);
                    CHECK_MSG(v.has_value() && *v == it.def, "%s default does not validate", k);
                } else {
                    CHECK_MSG(it.def.IsNull() && !it.action.empty(), "%s button/info", k);
                    if (it.type == ItemType::Button) CHECK_MSG(!it.actionLabel.empty(), "%s caption", k);
                }
                if (it.HasChoices()) {
                    CHECK_MSG(!it.choices.empty(), "%s choices", k);
                    CHECK_MSG(it.FindChoice(it.def.AsString()) != nullptr, "%s default not a choice", k);
                    std::set<std::string> values;
                    for (const Choice& ch : it.choices) {
                        CHECK_MSG(values.insert(ch.value).second, "%s duplicate choice %s", k, ch.value.c_str());
                        CHECK_MSG(!ch.label.empty() && !ch.labelRu.empty(), "%s choice %s label", k, ch.value.c_str());
                    }
                }
                if (it.type == ItemType::Slider || it.type == ItemType::Number) {
                    CHECK_MSG(it.min < it.max, "%s range", k);
                    CHECK_MSG(it.def.AsNumber() >= it.min && it.def.AsNumber() <= it.max, "%s default in range", k);
                    CHECK_MSG(it.step >= 0, "%s step", k);
                }
                if (it.type == ItemType::Path) CHECK_MSG(!it.actionLabel.empty(), "%s Browse caption", k);
                if (it.enabledWhen) {
                    const Item* dep = FindItem(it.enabledWhen->key);
                    CHECK_MSG(dep != nullptr, "%s depends on missing %s", k, it.enabledWhen->key.c_str());
                    if (dep && dep->type == ItemType::Toggle)
                        CHECK_MSG(it.enabledWhen->value == "true" || it.enabledWhen->value == "false", "%s", k);
                    if (dep && dep->HasChoices())
                        CHECK_MSG(dep->FindChoice(it.enabledWhen->value) != nullptr, "%s condition value", k);
                }
                if (!it.mpv.empty()) ++mapped;
                for (const MpvMap& m : it.mpv) {
                    CHECK_MSG(!m.option.empty(), "%s empty mpv option", k);
                    if (m.conv == MpvConv::Builder) {
                        CHECK_MSG(m.build != nullptr, "%s builder", k);
                        auto [pos, inserted] = builders.emplace(m.option, m.build);
                        CHECK_MSG(inserted || pos->second == m.build, "%s: option %s built two ways", k, m.option.c_str());
                    }
                    if (m.conv == MpvConv::Choice) {
                        bool any = false;
                        for (const Choice& ch : it.choices) any = any || !ch.mpv.empty();
                        CHECK_MSG(any || it.dynamicChoices, "%s choice mapping without mpv values", k);
                    }
                    if (m.conv == MpvConv::Bool) CHECK_MSG(it.type == ItemType::Toggle && !m.on.empty() && !m.off.empty(), "%s", k);
                }
            }
        }
        std::printf(" %s=%zu", c.id.c_str(), count);
    }
    std::printf(" (total %zu items, %zu mapped to mpv)\n", total, mapped);
    CHECK(AllItems().size() == total);
    CHECK(FindItem("nope.nothing") == nullptr);

    // Design-mandated details.
    const Item* accent = FindItem("general.accent");
    CHECK(accent && accent->type == ItemType::AccentPicker && accent->choices.size() == 5);
    if (accent) {
        const char* colors[] = {"#EDEDED", "#D4D4D4", "#B5B5B5", "#8F8F8F", "#6B6B6B"};
        for (size_t i = 0; i < 5 && i < accent->choices.size(); ++i) CHECK(accent->choices[i].extra == colors[i]);
    }
    const Item* theme = FindItem("general.theme");
    CHECK(theme && theme->type == ItemType::Segmented && theme->choices.size() == 3 && theme->def.AsString() == "dark");
    const Item* lang = FindItem("general.language");
    CHECK(lang && lang->def.AsString() == "ru" && lang->FindChoice("en"));
    const Item* hwdec = FindItem("performance.hwdec");
    CHECK(hwdec && hwdec->mpv.size() == 1 && hwdec->mpv[0].option == "hwdec");
    CHECK(FindItem("audio.normalize") && FindItem("audio.normalize")->mpv[0].option == "af");

    // Equalizer presets match the preset dropdown.
    const Item* preset = FindItem("audio.eqPreset");
    CHECK(preset != nullptr);
    if (preset) {
        CHECK(preset->choices.size() == EqPresets().size() + 1);  // + "None"
        for (const EqPreset& p : EqPresets()) CHECK_MSG(preset->FindChoice(p.id), "%s", p.id.c_str());
    }
    for (const char* k : kEqBandKeys) CHECK_MSG(FindItem(k) && FindItem(k)->group == "equalizer", "%s", k);

    // Keyboard actions: unique ids, known groups, no default key used twice.
    std::set<std::string> ids;
    std::set<std::wstring> defaults;
    for (const KeyAction& a : KeyActions()) {
        CHECK_MSG(ids.insert(a.id).second, "%s", a.id.c_str());
        CHECK(!a.label.empty() && !a.labelRu.empty());
        bool known = false;
        for (const KeyActionGroup& g : KeyActionGroups()) known = known || g.id == a.group;
        CHECK_MSG(known, "%s group", a.id.c_str());
        if (!a.defaultKey.empty()) CHECK_MSG(defaults.insert(a.defaultKey).second, "%s key reused", a.id.c_str());
        CHECK(FindKeyAction(a.id) == &a);
    }

    // Settings search works in both languages, case-insensitively.
    auto hits = Search(L"ГРОМКОСТЬ");
    CHECK(!hits.empty());
    bool foundNormalize = false;
    for (const Item* it : Search(L"loudness")) foundNormalize = foundNormalize || it->key == "audio.normalize";
    CHECK(foundNormalize);
    CHECK(Search(L"").empty());
    CHECK(AdvancedOptionsLabel(Lang::Ru) == L"Дополнительные параметры");
}

// --- Config -----------------------------------------------------------------------------------

void TestConfigValues() {
    FreshDataDir(L"config-values");
    Config c;
    CHECK(c.GetString("general.theme") == "dark");
    CHECK(c.GetString("general.language") == "ru");
    CHECK(c.GetBool("privacy.saveHistory"));
    CHECK(c.GetDouble("playback.speed") == 1);
    CHECK(c.GetInt("controls.skipShort") == 10);
    CHECK(c.GetWString("subtitles.color") == L"#FFFFFF");
    CHECK(c.Get("nope").IsNull());
    CHECK(c.IsDefault("general.theme"));
    CHECK(!c.Dirty());

    CHECK(!c.Set("nope", json::Value(1)));
    CHECK(!c.Set("general.theme", json::Value(1)));       // wrong type
    CHECK(!c.Set("general.theme", json::Value("blue")));  // not a choice
    CHECK(!c.Set("privacy.clearHistory", json::Value(true)));  // button
    CHECK(!c.SetString("privacy.saveHistory", "yes"));
    CHECK(!c.Dirty());

    CHECK(c.SetString("general.theme", "light"));
    CHECK(c.GetString("general.theme") == "light");
    CHECK(c.Dirty());
    CHECK(!c.IsDefault("general.theme"));

    // Numbers: clamped and snapped to the step.
    CHECK(c.SetDouble("playback.speed", 7));
    CHECK(c.GetDouble("playback.speed") == 2);
    CHECK(c.SetDouble("playback.speed", 1.1));
    CHECK(c.GetDouble("playback.speed") == 1);
    CHECK(c.SetDouble("playback.speed", 1.2));
    CHECK(c.GetDouble("playback.speed") == 1.25);
    CHECK(c.SetDouble("audio.replayGainPreamp", 0.30000001));
    CHECK(json::Serialize(c.Get("audio.replayGainPreamp")) == "0.3");
    CHECK(c.SetDouble("controls.skipShort", 12.7));
    CHECK(c.GetInt("controls.skipShort") == 13);
    CHECK(!c.Set("controls.skipShort", json::Value(std::nan(""))));

    // Colors.
    CHECK(c.SetString("subtitles.color", "#ffcc00"));
    CHECK(c.GetString("subtitles.color") == "#FFCC00");
    CHECK(!c.SetString("subtitles.color", "red"));
    CHECK(!c.SetString("subtitles.color", "#12345"));

    // Dynamic choices take any non-empty value.
    CHECK(c.SetString("audio.device", "wasapi/{0.0.0.00000000}.{guid}"));
    CHECK(!c.SetString("audio.device", ""));

    // Text accepts UTF-8; wide setter round-trips.
    CHECK(c.SetWString("subtitles.font", L"Шрифт «Тест»"));
    CHECK(c.GetWString("subtitles.font") == L"Шрифт «Тест»");

    // Key bindings keep only known actions and string fields.
    auto kb = json::Parse(R"({"fullscreen":{"key":"Ctrl+F","junk":1},"nope":{"key":"X"},"mute":5})");
    CHECK(c.Set("controls.keyBindings", *kb));
    CHECK(json::Serialize(c.Get("controls.keyBindings")) == R"({"fullscreen":{"key":"Ctrl+F"}})");

    // enabledWhen.
    CHECK(!c.IsEnabled("general.logFile"));
    CHECK(c.SetBool("general.saveLog", true));
    CHECK(c.IsEnabled("general.logFile"));
    CHECK(c.IsEnabled("interface.hideDelay"));
    CHECK(c.SetString("interface.autoHide", "never"));
    CHECK(!c.IsEnabled("interface.hideDelay"));
    CHECK(c.IsEnabled("general.theme"));

    // Equalizer preset copies band values.
    CHECK(c.ApplyEqPreset("rock"));
    CHECK(c.GetString("audio.eqPreset") == "rock");
    CHECK(c.GetDouble("audio.eq60") == 8);
    CHECK(c.GetDouble("audio.eqPreamp") == -7);
    CHECK(!c.ApplyEqPreset("nope"));

    c.ResetAll();
    CHECK(c.GetString("general.theme") == "dark");
    CHECK(c.GetDouble("audio.eq60") == 0);
    CHECK(c.ToJson().Find("values")->size() == 0);
}

void TestConfigSubscribe() {
    FreshDataDir(L"config-subscribe");
    Config c;
    std::vector<std::string> all, audio;
    Config::Token tAll = c.Subscribe("", [&](const std::string& k) { all.push_back(k); });
    Config::Token tAudio = c.Subscribe("audio.", [&](const std::string& k) { audio.push_back(k); });
    CHECK(tAll != tAudio);

    c.SetBool("audio.normalize", true);
    c.SetBool("audio.normalize", true);  // unchanged: no notification
    c.SetString("general.theme", "light");
    c.SetString("general.theme", "nope");  // invalid: no notification
    CHECK(all.size() == 2);
    CHECK(audio.size() == 1 && audio[0] == "audio.normalize");

    c.Unsubscribe(tAudio);
    c.SetBool("audio.normalize", false);
    CHECK(audio.size() == 1);
    CHECK(all.size() == 3);

    // Unsubscribing itself (and adding another) from inside a callback is safe.
    int selfCalls = 0, lateCalls = 0;
    Config::Token self = 0;
    self = c.Subscribe("video.", [&](const std::string&) {
        ++selfCalls;
        c.Unsubscribe(self);
        c.Subscribe("video.", [&](const std::string&) { ++lateCalls; });
    });
    c.SetDouble("video.hue", 10);
    c.SetDouble("video.hue", 20);
    CHECK(selfCalls == 1);
    CHECK(lateCalls == 1);

    // Resets notify changed keys only.
    all.clear();
    c.ResetCategory("video");
    CHECK(all.size() == 1 && all[0] == "video.hue");
    CHECK(c.GetDouble("video.hue") == 0);
    CHECK(c.GetString("general.theme") == "light");  // other categories untouched
    c.Unsubscribe(tAll);
    c.Unsubscribe(12345);  // unknown token: no-op
}

void TestConfigPersistence() {
    std::wstring dir = FreshDataDir(L"config-persist");
    std::wstring file = dir + L"\\settings.json";
    {
        Config c;
        CHECK(c.Load());  // no file yet
        CHECK(c.FilePath() == file);
        CHECK(c.Flush());  // nothing to save
        CHECK(!FileExists(file));
        c.SetString("general.theme", "system");
        c.SetDouble("audio.targetLevel", -16);
        c.SetWString("subtitles.font", L"Ubuntu Кириллица");
        c.SetBool("privacy.saveHistory", false);
        c.SetBool("privacy.saveHistory", true);  // back to default: not stored
        CHECK(c.Dirty());
        CHECK(c.Flush());
        CHECK(!c.Dirty());
        CHECK(FileExists(file));
    }
    std::string text;
    CHECK(json::ReadFileUtf8(file, &text));
    auto doc = json::Parse(text);
    CHECK(doc.has_value());
    const json::Value* values = doc ? doc->Find("values") : nullptr;
    CHECK(values && values->size() == 3);  // only non-default values
    CHECK(values && values->Find("privacy.saveHistory") == nullptr);
    CHECK(doc && doc->GetNumber("version") == 1);

    // Unknown keys survive a round trip; invalid values are dropped on load.
    (*doc)["values"].Set("future.setting", json::Value("keep me"));
    (*doc)["values"].Set("general.language", json::Value("klingon"));
    CHECK(json::WriteFileAtomic(file, json::Serialize(*doc, true)));
    {
        Config c;
        std::vector<std::string> changed;
        c.Subscribe("", [&](const std::string& k) { changed.push_back(k); });
        CHECK(c.Load());
        CHECK(changed.size() == 3);
        CHECK(!c.Dirty());
        CHECK(c.GetString("general.theme") == "system");
        CHECK(c.GetDouble("audio.targetLevel") == -16);
        CHECK(c.GetWString("subtitles.font") == L"Ubuntu Кириллица");
        CHECK(c.GetString("general.language") == "ru");
        CHECK(c.ToJson().Find("values")->GetString("future.setting") == "keep me");
        c.SetBool("video.wallpaper", true);
        // Destructor flushes.
    }
    {
        Config c;
        CHECK(c.Load());
        CHECK(c.GetBool("video.wallpaper"));
        CHECK(c.ToJson().Find("values")->Find("future.setting") != nullptr);
        c.ResetCategory("video");
        CHECK(!c.GetBool("video.wallpaper"));
        CHECK(c.Save());
    }

    // A corrupt file is kept as settings.corrupt-<time>.json, reported once, and defaults are used.
    TakeCorruptFileNotices();
    WriteFile(file, "{ this is not json");
    {
        Config c;
        CHECK(!c.Load());
        CHECK(c.GetString("general.theme") == "dark");
        CHECK(CountFiles(dir, L"settings.corrupt-*.json") == 1);
        const std::vector<std::wstring> notices = TakeCorruptFileNotices();
        CHECK(notices.size() == 1 && notices[0] == L"settings.json");
        CHECK(TakeCorruptFileNotices().empty());
    }
}

void TestHiddenCondition() {
    FreshDataDir(L"hidden-condition");
    Config c;
    // library.enabled is hidden (not implemented); an old stored "false" must not grey out the
    // visible library options that depend on it.
    const settings::Item* parent = settings::FindItem("library.enabled");
    CHECK(parent && parent->hidden);
    c.SetBool("library.enabled", false);
    CHECK(c.IsEnabled("library.readTags"));
    CHECK(c.IsEnabled("library.scanOnStartup"));
    // Visible switches still gate their dependents.
    c.SetBool("general.saveLog", false);
    CHECK(!c.IsEnabled("general.logFile"));
    c.SetBool("general.saveLog", true);
    CHECK(c.IsEnabled("general.logFile"));
}

void TestCorruptBackup() {
    std::wstring dir = FreshDataDir(L"corrupt");
    TakeCorruptFileNotices();
    const std::wstring file = dir + L"\\history.json";
    WriteFile(file, "{\"items\": [ {\"path\": \"C:\\\\a.mkv\"}, {\"pa");
    // Two failures in the same second keep two copies; the original stays untouched.
    const std::wstring a = BackupCorruptFile(file);
    const std::wstring b = BackupCorruptFile(file);
    CHECK(!a.empty() && !b.empty() && a != b);
    CHECK(a.find(L"\\history.corrupt-") != std::wstring::npos);
    CHECK(a.size() > 5 && a.compare(a.size() - 5, 5, L".json") == 0);
    CHECK(CountFiles(dir, L"history.corrupt-*.json") == 2);
    CHECK(FileExists(file));
    std::string copy;
    CHECK(json::ReadFileUtf8(a, &copy) && copy.find("a.mkv") != std::string::npos);
    CHECK(TakeCorruptFileNotices().size() == 2);
    // Missing file: nothing to keep, nothing reported.
    CHECK(BackupCorruptFile(dir + L"\\missing.json").empty());
    CHECK(TakeCorruptFileNotices().empty());
    // A file without an extension still gets a readable name.
    WriteFile(dir + L"\\ui", "x");
    CHECK(BackupCorruptFile(dir + L"\\ui").find(L"\\ui.corrupt-") != std::wstring::npos);
    TakeCorruptFileNotices();

    // The log path hint follows a moved data folder.
    CHECK(DataDirOverridden());
    const std::wstring shown = DataFileDisplayPath(L"lumen.log");
    CHECK(shown.rfind(L"%TEMP%\\", 0) == 0);  // the test data dir lives under %TEMP%
    const std::wstring tail = L"\\data-corrupt\\lumen.log";
    CHECK(shown.size() > tail.size() && shown.compare(shown.size() - tail.size(), tail.size(), tail) == 0);
    SetDataDirForTests({});
    CHECK(!DataDirOverridden());
    CHECK(DataFileDisplayPath(L"lumen.log") == L"%APPDATA%\\Lumen\\lumen.log");
    SetDataDirForTests(dir);
}

std::string FindOption(const std::vector<MpvOption>& opts, const char* name, bool* found = nullptr) {
    for (const MpvOption& o : opts)
        if (o.first == name) {
            if (found) *found = true;
            return o.second;
        }
    if (found) *found = false;
    return {};
}

void TestConfigMpv() {
    FreshDataDir(L"config-mpv");
    Config c;
    auto opts = MpvOptions(c);
    bool found = false;
    // Not auto-safe: its nvdec fallback costs a CUDA context for files d3d11va can't decode.
    CHECK_MSG(FindOption(opts, "hwdec") == "d3d11va,d3d11va-copy", "hwdec=%s", FindOption(opts, "hwdec").c_str());
    // Automatic decoder threads are capped (each one holds full frames: ~40 MB at 4K).
    const int threads = std::atoi(FindOption(opts, "vd-lavc-threads").c_str());
    CHECK(threads >= 2 && threads <= 6);
    c.SetDouble("performance.decodeThreads", 12);
    CHECK(FindOption(MpvOptions(c), "vd-lavc-threads") == "12");
    c.SetDouble("performance.decodeThreads", 0);
    CHECK(FindOption(opts, "sub-color") == "#FFFFFFFF");
    CHECK(FindOption(opts, "deinterlace") == "auto");
    CHECK(FindOption(opts, "save-position-on-quit").empty());  // Lumen saves positions itself
    CHECK(FindOption(opts, "pause") == "no");
    CHECK(FindOption(opts, "cache-secs") == "60");
    CHECK(FindOption(opts, "demuxer-max-bytes") == "150MiB");
    CHECK(FindOption(opts, "screenshot-format") == "png");
    CHECK(!FindOption(opts, "screenshot-dir").empty());
    CHECK(!FindOption(opts, "slang").empty());  // system language
    FindOption(opts, "af", &found);
    CHECK(!found);  // no audio filters by default
    FindOption(opts, "vf", &found);
    CHECK(!found);
    FindOption(opts, "aid", &found);
    CHECK(!found);  // automatic
    std::set<std::string> names;
    for (const MpvOption& o : opts) CHECK_MSG(names.insert(o.first).second, "option twice: %s", o.first.c_str());

    // Live updates.
    c.SetBool("audio.normalize", true);
    auto live = MpvOptionFor(c, "audio.normalize");
    CHECK(live.size() == 1 && live[0].first == "af");
    CHECK(!live.empty() && live[0].second == "@lumen:lavfi=[loudnorm=I=-23:TP=-1.5:LRA=11]");
    c.SetBool("audio.nightMode", true);
    std::string af = FindOption(MpvOptions(c), "af");
    CHECK(af.find("acompressor=") != std::string::npos && af.find("loudnorm") != std::string::npos);
    CHECK(af.find("acompressor") < af.find("loudnorm"));
    c.SetBool("audio.normalize", false);
    c.SetBool("audio.nightMode", false);
    live = MpvOptionFor(c, "audio.nightMode");
    CHECK(live.size() == 1 && live[0].first == "af" && live[0].second.empty());  // clears the chain

    c.SetBool("audio.eq", true);
    c.ApplyEqPreset("classical");
    af = FindOption(MpvOptions(c), "af");
    CHECK(af.find("equalizer=f=6000:t=o:w=1:g=-7.2") != std::string::npos);
    c.SetBool("audio.eqTwoPass", true);
    af = FindOption(MpvOptions(c), "af");
    CHECK(af.find("g=-9.6") != af.rfind("g=-9.6"));  // twice

    c.SetString("playback.repeat", "one");
    live = MpvOptionFor(c, "playback.repeat");
    CHECK(live.size() == 2);
    CHECK(FindOption(live, "loop-file") == "inf" && FindOption(live, "loop-playlist") == "no");

    c.SetBool("subtitles.enabled", false);
    CHECK(FindOption(MpvOptionFor(c, "subtitles.enabled"), "sid") == "no");
    c.SetBool("subtitles.enabled", true);
    CHECK(FindOption(MpvOptionFor(c, "subtitles.enabled"), "sid") == "auto");  // reset value
    c.SetInt("playback.subtitleTrack", 2);
    CHECK(FindOption(MpvOptions(c), "sid") == "2");

    c.SetDouble("subtitles.opacity", 50);
    CHECK(FindOption(MpvOptionFor(c, "subtitles.opacity"), "sub-color") == "#80FFFFFF");
    c.SetString("video.crop", "16:9");
    CHECK(FindOption(MpvOptions(c), "vf").find("crop=w='min(iw,ih*16/9)'") != std::string::npos);
    c.SetString("performance.hwdec", "off");
    CHECK(FindOption(MpvOptionFor(c, "performance.hwdec"), "hwdec") == "no");
    c.SetString("network.httpProxy", "http://me@proxy:3128");
    c.SetString("network.httpProxyPassword", "p4ss");
    CHECK(FindOption(MpvOptions(c), "http-proxy") == "http://me:p4ss@proxy:3128");

    // Startup-only settings are not pushed live.
    c.SetDouble("playback.speed", 1.5);
    CHECK(MpvOptionFor(c, "playback.speed").empty());
    CHECK(FindOption(MpvOptions(c), "speed") == "1.5");
    c.SetString("audio.startupVolumeMode", "fixed");
    c.SetDouble("audio.startupVolume", 70);
    CHECK(FindOption(MpvOptions(c), "volume") == "70");
    CHECK(MpvOptionFor(c, "general.theme").empty());  // not an mpv setting

    // User options go last.
    c.SetString("general.extraOptions", "--deband=yes --title=\"My movie\" --no-osc hwdec=nvdec");
    opts = MpvOptions(c);
    CHECK(opts.size() >= 4);
    if (opts.size() >= 4) {
        CHECK(opts[opts.size() - 4] == MpvOption("deband", "yes"));
        CHECK(opts[opts.size() - 3] == MpvOption("title", "My movie"));
        CHECK(opts[opts.size() - 2] == MpvOption("osc", "no"));
        CHECK(opts.back() == MpvOption("hwdec", "nvdec"));
    }
    CHECK(ParseMpvArgs("").empty());
    CHECK(ParseMpvArgs("  --a  --b=  'c d'=e").size() == 3);
}

// --- History / playlists / bookmarks --------------------------------------------------------------

void TestHistory() {
    std::wstring dir = FreshDataDir(L"history");
    {
        library::History h;
        CHECK(h.Load());
        CHECK(h.size() == 0);
        CHECK(h.RecordPlay(L"D:\\Видео\\Фильм.mkv", L"Фильм", 6000));
        CHECK(h.UpdatePosition(L"d:/видео/фильм.mkv", 3000, 6000));  // same file, other spelling
        CHECK(h.size() == 1);
        const library::HistoryEntry* e = h.Find(L"D:\\ВИДЕО\\ФИЛЬМ.MKV");
        CHECK(e && e->playCount == 1 && e->position == 3000 && e->kind == library::MediaKind::Video);
        CHECK(e && e->title == L"Фильм");

        h.RecordPlay(L"D:\\Видео\\Почти.mkv", {}, 1000);
        h.UpdatePosition(L"D:\\Видео\\Почти.mkv", 990, 1000);  // 99%: finished
        h.RecordPlay(L"D:\\Видео\\Начало.mp4", {}, 1000);
        h.UpdatePosition(L"D:\\Видео\\Начало.mp4", 10, 1000);  // 1%: barely started
        h.RecordPlay(L"E:\\Music\\a.flac", L"Song A", 200);
        h.RecordPlay(L"E:\\Music\\b.mp3");
        h.RecordPlay(L"E:\\Music\\b.mp3");
        CHECK(h.Find(L"E:\\Music\\b.mp3")->playCount == 2);
        CHECK(h.Find(L"E:\\Music\\b.mp3")->title == L"b");

        auto cont = h.ContinueWatching();
        CHECK(cont.size() == 1 && cont[0].title == L"Фильм");
        auto listen = h.ListenAgain();
        CHECK(listen.size() == 2 && listen[0].path == L"E:\\Music\\b.mp3");
        auto recent = h.Recent();
        CHECK(recent.size() == 5 && recent[0].path == L"E:\\Music\\b.mp3");
        CHECK(h.Recent(2).size() == 2);
        CHECK(h.Recent(10, library::MediaKind::Video).size() == 3);
        auto folders = h.FrequentFolders();
        // Both folders have 3 plays; the music folder was played more recently.
        CHECK(folders.size() == 2 && folders[0].folder == L"E:\\Music" && folders[0].plays == 3);
        CHECK(folders.size() == 2 && folders[1].folder == L"D:\\Видео" && folders[1].plays == 3);

        h.SetExcludePatterns(L"*\\Private\\*;*.secret.mkv");
        CHECK(!h.RecordPlay(L"C:\\Users\\me\\Private\\x.mkv"));
        CHECK(!h.RecordPlay(L"C:\\clip.SECRET.mkv"));
        CHECK(h.RecordPlay(L"C:\\clip.mkv"));
        CHECK(h.Remove(L"c:\\CLIP.mkv"));
        CHECK(!h.Remove(L"c:\\CLIP.mkv"));
        CHECK(h.Dirty());
        CHECK(h.Flush());
        CHECK(!h.Dirty());
    }
    {
        library::History h;
        CHECK(h.Load());
        CHECK(h.size() == 5);
        CHECK(h.Entries()[0].path == L"E:\\Music\\b.mp3");
        CHECK(h.ContinueWatching().size() == 1);
        const library::HistoryEntry* e = h.Find(L"D:\\Видео\\Фильм.mkv");
        CHECK(e && e->position == 3000 && e->duration == 6000 && e->title == L"Фильм");

        // Cap at 500 entries: the oldest go first.
        for (int i = 0; i < 520; ++i) h.RecordPlay(L"F:\\bulk\\" + std::to_wstring(i) + L".mkv");
        CHECK(h.size() == library::History::kMaxEntries);
        CHECK(h.Find(L"E:\\Music\\b.mp3") == nullptr);
        CHECK(h.Find(L"F:\\bulk\\519.mkv") != nullptr);
        h.Clear();
        CHECK(h.size() == 0);
    }
    {
        library::History h;
        CHECK(h.Load());
        CHECK(h.size() == 0);
    }
    (void)dir;
}

void TestPlaylists() {
    std::wstring dir = FreshDataDir(L"playlists");
    std::string id;
    {
        library::Playlists p;
        CHECK(p.Load());
        id = p.Create(L"Вечер");
        std::string other = p.Create(L"Other");
        CHECK(!id.empty() && id != other);
        CHECK(p.AddPaths(id, {L"C:\\a.mkv", L"C:\\b.mkv", L"C:\\c.mkv", L"C:\\d.mkv"}));
        CHECK(p.Move(id, 0, 2));  // b c a d
        CHECK(p.Move(id, 3, 0));  // d b c a
        const library::Playlist* pl = p.Find(id);
        CHECK(pl && pl->items.size() == 4);
        if (pl && pl->items.size() == 4) {
            CHECK(pl->items[0].path == L"C:\\d.mkv" && pl->items[1].path == L"C:\\b.mkv");
            CHECK(pl->items[2].path == L"C:\\c.mkv" && pl->items[3].path == L"C:\\a.mkv");
        }
        CHECK(!p.Move(id, 0, 9));
        CHECK(p.Add(id, {{L"C:\\first.mkv", L"First", 61}}, 0));
        CHECK(p.RemoveIndices(id, {4, 1, 1, 77}));  // removes d and a
        pl = p.Find(id);
        CHECK(pl && pl->items.size() == 3 && pl->items[0].title == L"First" && pl->items[2].path == L"C:\\c.mkv");
        CHECK(p.RemoveAt(id, 2));
        CHECK(!p.RemoveAt(id, 5));
        CHECK(p.Rename(id, L"Вечер кино"));
        CHECK(p.Delete(other));
        CHECK(!p.Delete(other));
        CHECK(!p.Rename("nope", L"x"));
        CHECK(p.Flush());
    }
    {
        library::Playlists p;
        CHECK(p.Load());
        CHECK(p.All().size() == 1);
        const library::Playlist* pl = p.Find(id);
        CHECK(pl && pl->name == L"Вечер кино" && pl->items.size() == 2);
        CHECK(pl && pl->items[0].duration == 61);

        // M3U round trip with Cyrillic titles and an URL.
        p.Add(id, {{L"https://example.com/stream.m3u8", L"Радио", 0}, {L"D:\\Музыка\\трек.flac", L"", 185.4}});
        std::wstring m3u = dir + L"\\Экспорт.m3u8";
        CHECK(p.Export(id, m3u));
        std::string text;
        CHECK(json::ReadFileUtf8(m3u, &text));
        CHECK(text.compare(0, 9, "#EXTM3U\r\n") == 0);
        CHECK(text.find(U8(L"#EXTINF:185,трек\r\nD:\\Музыка\\трек.flac")) != std::string::npos);
        std::string imported = p.Import(m3u);
        const library::Playlist* ip = p.Find(imported);
        CHECK(ip && ip->name == L"Экспорт" && ip->items.size() == 4);
        if (ip && ip->items.size() == 4) {
            CHECK(ip->items[0].title == L"First" && ip->items[0].duration == 61);
            CHECK(ip->items[2].path == L"https://example.com/stream.m3u8" && ip->items[2].title == L"Радио");
            CHECK(ip->items[3].path == L"D:\\Музыка\\трек.flac");
        }
    }
    // Hand-written playlist: relative paths, comments, blank lines, ANSI (cp1251) text.
    std::wstring sub = MakeDir(L"m3u\\lists");
    WriteFile(sub + L"\\rel.m3u",
              "#EXTM3U\n\n# comment\n#EXTINF:12,One\nmedia\\one.mkv\n..\\two.mp4\r\n/abs/three.mkv\nfile:///C:/x%20y.mkv\n");
    std::vector<library::PlaylistItem> items;
    CHECK(library::ReadM3u(sub + L"\\rel.m3u", &items));
    CHECK(items.size() == 4);
    if (items.size() == 4) {
        CHECK(items[0].path == sub + L"\\media\\one.mkv" && items[0].title == L"One" && items[0].duration == 12);
        CHECK(items[1].path == g_temp + L"\\m3u\\two.mp4" && items[1].title.empty());
        CHECK(items[2].path == g_temp.substr(0, 2) + L"\\abs\\three.mkv");
        CHECK(items[3].path == L"C:\\x y.mkv");
    }
    if (GetACP() == 1251) {
        WriteFile(sub + L"\\ansi.m3u", "\xCF\xF0\xE8\xE2\xE5\xF2.mkv\n");  // "Привет.mkv" in cp1251
        CHECK(library::ReadM3u(sub + L"\\ansi.m3u", &items) && items.size() == 1 &&
              items[0].path == sub + L"\\Привет.mkv");
    }
    CHECK(!library::ReadM3u(sub + L"\\missing.m3u", &items));
}

void TestBookmarksFavorites() {
    FreshDataDir(L"bookmarks");
    {
        library::Bookmarks b;
        CHECK(b.Load());
        CHECK(b.Add(L"D:\\Фильм.mkv", 120, L"Погоня") == 0);
        CHECK(b.Add(L"D:\\Фильм.mkv", 30) == 0);  // sorted by time
        CHECK(b.Add(L"d:\\фильм.MKV", 60, L"Middle") == 1);
        auto marks = b.For(L"D:\\Фильм.mkv");
        CHECK(marks.size() == 3 && marks[0].time == 30 && marks[1].name == L"Middle" && marks[2].name == L"Погоня");
        CHECK(b.Rename(L"D:\\Фильм.mkv", 0, L"Начало"));
        CHECK(!b.Rename(L"D:\\Фильм.mkv", 7, L"x"));
        b.Add(L"E:\\x.mp3", 1);
        CHECK(b.Remove(L"E:\\x.mp3", 0));
        CHECK(b.FileCount() == 1);

        library::Favorites f;
        CHECK(f.Load());
        CHECK(f.Add(L"D:\\Фильм.mkv"));
        CHECK(!f.Add(L"d:/фильм.mkv"));
        CHECK(f.Toggle(L"E:\\Песня.flac"));
        CHECK(!f.Toggle(L"E:\\Песня.flac"));
        CHECK(f.Add(L"E:\\Folder"));
        CHECK(f.Contains(L"e:\\FOLDER\\"));
        CHECK(f.All().size() == 2);
    }  // destructors flush
    {
        library::Bookmarks b;
        CHECK(b.Load());
        auto marks = b.For(L"D:\\ФИЛЬМ.mkv");
        CHECK(marks.size() == 3 && marks[0].name == L"Начало" && marks[2].time == 120);
        CHECK(b.For(L"E:\\x.mp3").empty());
        b.ClearFile(L"D:\\Фильм.mkv");
        CHECK(b.FileCount() == 0);

        library::Favorites f;
        CHECK(f.Load());
        CHECK(f.All().size() == 2 && f.All()[0] == L"D:\\Фильм.mkv");
        CHECK(f.Remove(L"D:\\Фильм.mkv"));
        CHECK(!f.Remove(L"D:\\Фильм.mkv"));
    }
}

// --- Media library ------------------------------------------------------------------------------

void TestMediaTypes() {
    using library::KindOf;
    using library::MediaKind;
    CHECK(KindOf(L"a.MKV") == MediaKind::Video);
    CHECK(KindOf(L"C:\\x.y\\song.Flac") == MediaKind::Audio);
    CHECK(KindOf(L"C:\\x.mkv\\readme") == MediaKind::Other);
    CHECK(KindOf(L"notes.txt") == MediaKind::Other);
    CHECK(KindOf(L"noext") == MediaKind::Other);
    CHECK(library::IsSubtitlePath(L"a.ru.srt") && library::IsPlaylistPath(L"a.m3u8"));
    CHECK(library::NormalizePath(L"C:/a/b/") == L"C:\\a\\b");
    CHECK(library::NormalizePath(L"C:\\") == L"C:\\");
    CHECK(library::ParentFolder(L"C:\\a.mkv") == L"C:\\");
    CHECK(library::ParentFolder(L"C:\\d\\a.mkv") == L"C:\\d");
    CHECK(library::StemOf(L"C:\\d\\a.b.mkv") == L"a.b");
    CHECK(library::StemOf(L".hidden") == L".hidden");
    CHECK(library::PathKey(L"c:/Видео/Ёж.mkv") == library::PathKey(L"C:\\ВИДЕО\\ЁЖ.MKV"));
    CHECK(library::SearchFold(L"ЁЛКА Ёж") == L"елка еж");
    CHECK(library::UnixMsNow() > 1700000000000LL);
}

void TestEpisodesAndOrder() {
    library::EpisodeInfo ep;
    CHECK(library::ParseEpisode(L"Series S01E02", &ep) && ep.show == L"Series" && ep.season == 1 && ep.episode == 2);
    CHECK(library::ParseEpisode(L"The.Show.s2e10.720p", &ep) && ep.show == L"The Show" && ep.season == 2 &&
          ep.episode == 10);
    CHECK(library::ParseEpisode(L"Show 1x03 - Pilot", &ep) && ep.show == L"Show" && ep.season == 1 && ep.episode == 3);
    CHECK(library::ParseEpisode(L"S03E01E02", &ep) && ep.show.empty() && ep.season == 3 && ep.episode == 1);
    CHECK(library::ParseEpisode(L"Шоу — S01E05", &ep) && ep.show == L"Шоу" && ep.episode == 5);
    CHECK(!library::ParseEpisode(L"Clip 1920x1080", nullptr));
    CHECK(!library::ParseEpisode(L"Seasons Greetings", nullptr));
    CHECK(!library::ParseEpisode(L"Series S01", nullptr));
    CHECK(!library::ParseEpisode(L"Chaptered Demo (1080p)", nullptr));
    CHECK(!library::ParseEpisode(L"BS01E02", nullptr));  // glued to a word

    const unsigned char mp4[] = {0, 0, 0, 0x20, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm', 0, 0, 2, 0};
    const unsigned char mkv[] = {0x1A, 0x45, 0xDF, 0xA3, 0x9F, 0x42, 0x86, 0x81, 1, 0x42, 0xF7, 0x81, 1, 0x42};
    const unsigned char text[] = {'n', 'o', 't', ' ', 'a', ' ', 'v', 'i', 'd', 'e', 'o', '!', '\n', 0};
    CHECK(library::HeaderMatchesExtension(L"a.MP4", mp4, sizeof(mp4)));
    CHECK(!library::HeaderMatchesExtension(L"a.mkv", mp4, sizeof(mp4)));
    CHECK(library::HeaderMatchesExtension(L"a.webm", mkv, sizeof(mkv)));
    CHECK(!library::HeaderMatchesExtension(L"fake.mkv", text, 12));
    CHECK(!library::HeaderMatchesExtension(L"empty.mp4", mp4, 0));
    CHECK(library::HeaderMatchesExtension(L"notes.txt", text, 3));     // not media: nothing to judge
    CHECK(library::HeaderMatchesExtension(L"clip.rmvb", text, sizeof(text)));  // no rule for this container

    // Album order: album first, then disc and track; untagged albums and unnumbered tracks last.
    std::vector<library::MediaItem> v(6);
    auto set = [&](size_t i, const wchar_t* album, int disc, int track, const wchar_t* title) {
        v[i].album = album;
        v[i].disc = disc;
        v[i].track = track;
        v[i].title = title;
        v[i].path = std::wstring(L"C:\\m\\") + title + L".mp3";
    };
    set(0, L"B", 0, 1, L"b1");
    set(1, L"A", 0, 2, L"a2");
    set(2, L"A", 0, 1, L"a1");
    set(3, L"", 0, 1, L"u1");
    set(4, L"A", 2, 1, L"a-d2-1");
    set(5, L"A", 0, 0, L"a-none");
    std::vector<const library::MediaItem*> order;
    for (const auto& m : v) order.push_back(&m);
    library::SortItems(order, library::SortKey::Track);
    std::wstring got;
    for (const auto* m : order) got += m->title + L",";
    CHECK(got == L"a1,a2,a-none,a-d2-1,b1,u1,");
}

std::shared_ptr<const library::Snapshot> ScanAndWait(library::MediaLibrary& lib, DWORD timeoutMs = 20000) {
    constexpr UINT kMsg = WM_APP + 41;
    lib.SetNotifyWindow(MessageWindow(), kMsg);
    uint64_t gen = lib.Rescan();
    CHECK(gen != 0);
    bool done = WaitForMessage(kMsg, timeoutMs, [&](const MSG& m) {
        return m.wParam == 1 && static_cast<uint64_t>(m.lParam) == gen;
    });
    CHECK_MSG(done, "scan did not complete");
    auto snap = lib.Current();
    CHECK(snap && snap->generation == gen && snap->complete);
    return snap;
}

void TestLibraryScan() {
    FreshDataDir(L"library");
    std::wstring root = MakeDir(L"lib\\Медиа");
    WriteFile(root + L"\\Фильмы\\Фильм 10.mkv", std::string(3000, 'x'));
    WriteFile(root + L"\\Фильмы\\Фильм 2.mkv", std::string(1000, 'x'));
    WriteFile(root + L"\\Фильмы\\Ящик ёлок.mp4", std::string(2000, 'x'));
    WriteFile(root + L"\\Фильмы\\Фильм 2.ru.srt", "1");
    WriteFile(root + L"\\Музыка\\Альбом\\01 Song.mp3", "x");
    WriteFile(root + L"\\Музыка\\Альбом\\02 Song.flac", "x");
    WriteFile(root + L"\\Музыка\\cover.jpg", "x");
    WriteFile(root + L"\\Deep\\A\\B\\C\\deep.webm", "x");
    WriteFile(root + L"\\notes.txt", "x");
    WriteFile(root + L"\\Hidden dir\\secret.mkv", "x");
    WriteFile(root + L"\\hidden.mkv", "x");
    MakeDir(L"lib\\Медиа\\Empty\\Inner");
    SetFileAttributesW((root + L"\\Hidden dir").c_str(), FILE_ATTRIBUTE_HIDDEN);
    SetFileAttributesW((root + L"\\hidden.mkv").c_str(), FILE_ATTRIBUTE_HIDDEN);

    {
        library::MediaLibrary lib;
        CHECK(lib.Load());
        CHECK(lib.Roots().empty());
        CHECK(lib.Current() && lib.Current()->items.empty());
        CHECK(lib.AddRoot(root));
        CHECK(!lib.AddRoot(root + L"\\"));          // same root
        CHECK(!lib.AddRoot(root + L"\\Фильмы"));    // already covered
        CHECK(!lib.AddRoot(root + L"\\missing"));   // does not exist
        CHECK(lib.Roots().size() == 1);
    }
    library::MediaLibrary lib;
    CHECK(lib.Load());
    CHECK(lib.Roots().size() == 1 && lib.Roots()[0] == root);

    library::ScanOptions opt;
    opt.readMetadata = true;
    lib.SetOptions(opt);
    auto snap = ScanAndWait(lib);
    if (!snap) return;
    CHECK(!lib.Scanning());
    CHECK_MSG(snap->items.size() == 6, "items=%zu", snap->items.size());
    CHECK(snap->Videos().size() == 4);
    CHECK(snap->Music().size() == 2);
    CHECK(snap->FindItem(root + L"\\hidden.mkv") == nullptr);
    CHECK(snap->Folder(root + L"\\Hidden dir") == nullptr);
    CHECK(snap->Folder(root + L"\\Empty") == nullptr);  // pruned: no media

    auto roots = snap->Roots();
    CHECK(roots.size() == 1 && roots[0]->path == root && roots[0]->name == L"Медиа");
    auto children = snap->Children(root);
    CHECK(children.size() == 3);
    if (children.size() == 3) {
        CHECK(children[0]->name == L"Deep" && children[1]->name == L"Музыка" && children[2]->name == L"Фильмы");
        CHECK(children[2]->videos == 3 && children[2]->totalVideos == 3);
        CHECK(children[1]->audios == 0 && children[1]->totalAudios == 2);
    }
    CHECK(roots.size() == 1 && roots[0]->totalVideos == 4 && roots[0]->totalAudios == 2);
    CHECK(snap->ItemsIn(root, false).empty());
    CHECK(snap->ItemsIn(root, true).size() == 6);
    CHECK(snap->ItemsIn(root + L"\\deep\\a", true).size() == 1);  // case-insensitive lookup

    auto films = snap->ItemsIn(root + L"\\Фильмы", false);
    CHECK(films.size() == 3);
    if (films.size() == 3) {  // natural order inside a folder
        CHECK(films[0]->name == L"Фильм 2" && films[1]->name == L"Фильм 10" && films[2]->name == L"Ящик ёлок");
        CHECK(films[0]->folder == root + L"\\Фильмы" && films[0]->size == 1000 && films[0]->mtime > 0);
    }

    // Search: case-insensitive Cyrillic, ё = е, kind filter.
    CHECK(snap->Search(L"ФИЛЬМ").size() == 2);
    CHECK(snap->Search(L"елок").size() == 1);
    CHECK(snap->Search(L"ЁЛОК").size() == 1);
    CHECK(snap->Search(L"ящик").size() == 1);
    CHECK(snap->Search(L"song", library::MediaKind::Audio).size() == 2);
    CHECK(snap->Search(L"song", library::MediaKind::Video).empty());
    CHECK(snap->Search(L"").size() == 6);
    CHECK(snap->Search(L"zzz").empty());

    auto videos = snap->Videos();
    library::SortItems(videos, library::SortKey::Size, true);
    CHECK(videos.size() == 4 && videos[0]->name == L"Фильм 10");
    library::SortItems(videos, library::SortKey::Name);
    CHECK(videos.size() == 4 && videos[0]->name == L"deep" && videos[1]->name == L"Фильм 2" &&
          videos[2]->name == L"Фильм 10");
    library::SortItems(videos, library::SortKey::Name, true);
    CHECK(videos[0]->name == L"Ящик ёлок");
    library::SortItems(videos, library::SortKey::Duration);  // all unknown: falls back to name
    CHECK(videos[0]->name == L"deep");
    library::SortItems(videos, library::SortKey::Date);
    CHECK(videos.size() == 4);

    // Hidden files on request; snapshots stay immutable for whoever holds them.
    opt.includeHidden = true;
    lib.SetOptions(opt);
    auto snap2 = ScanAndWait(lib);
    CHECK(snap2 && snap2->items.size() == 8);
    CHECK(snap->items.size() == 6);

    // Cancelling a scan leaves the last snapshot in place.
    lib.Rescan();
    lib.Cancel();
    CHECK(!lib.Scanning());
    CHECK(lib.Current() != nullptr);

    CHECK(lib.RemoveRoot(root));
    CHECK(!lib.RemoveRoot(root));
    auto empty = ScanAndWait(lib);
    CHECK(empty && empty->items.empty() && empty->folders.empty());
}

// Real media from the Windows install, to measure property-store cost and check durations.
void TestLibraryMetadata() {
    FreshDataDir(L"library-meta");
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    std::wstring media = std::wstring(win) + L"\\Media";
    if (!FileExists(media)) {
        std::printf("  (skipped: %ls not found)\n", media.c_str());
        return;
    }
    library::MediaLibrary lib;
    lib.SetRoots({media});
    auto snap = ScanAndWait(lib, 60000);
    if (!snap) return;
    size_t withDuration = 0;
    for (const library::MediaItem& it : snap->items)
        if (it.duration > 0) ++withDuration;
    std::printf("  %ls: %zu files, enumerate %.1f ms, metadata %zu files in %.1f ms (%.2f ms/file, %s)\n",
                media.c_str(), snap->items.size(), snap->stats.enumerateMs, snap->stats.metadataFiles,
                snap->stats.metadataMs, snap->stats.metadataMsPerFile,
                snap->stats.metadataSecondPass ? "second pass" : "inline");
    CHECK(!snap->items.empty());
    CHECK_MSG(withDuration > 0, "no durations read");

    // Second scan uses library-cache.json: no property reads, same durations.
    auto again = ScanAndWait(lib, 60000);
    CHECK(again && again->stats.metadataFiles == 0);
    size_t withDuration2 = 0;
    if (again)
        for (const library::MediaItem& it : again->items)
            if (it.duration > 0) ++withDuration2;
    CHECK(withDuration2 == withDuration);
}

// --- Thumbnails -------------------------------------------------------------------------------

// 24-bit BMP with a gradient: the shell's image thumbnail provider handles it everywhere.
void WriteBmp(const std::wstring& path, int w, int h) {
    int stride = (w * 3 + 3) & ~3;
    std::string data(54 + static_cast<size_t>(stride) * h, '\0');
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&data[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&data[at], &v, 2); };
    data[0] = 'B';
    data[1] = 'M';
    put32(2, static_cast<uint32_t>(data.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<uint32_t>(w));
    put32(22, static_cast<uint32_t>(h));
    put16(26, 1);
    put16(28, 24);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t p = 54 + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 3;
            data[p] = static_cast<char>(x * 255 / w);
            data[p + 1] = static_cast<char>(y * 255 / h);
            data[p + 2] = static_cast<char>(128);
        }
    WriteFile(path, data);
}

void TestThumbnails() {
    std::wstring dir = MakeDir(L"thumbs\\Картинки");
    std::wstring bmp = dir + L"\\градиент.bmp";
    std::wstring txt = dir + L"\\notes.txt";
    WriteBmp(bmp, 320, 160);
    WriteFile(txt, "no thumbnail here");

    constexpr UINT kMsg = WM_APP + 42;
    library::Thumbnails thumbs(2);
    thumbs.SetNotifyWindow(MessageWindow(), kMsg);
    thumbs.Request(bmp, 64, 1);
    thumbs.Request(bmp, 64, 2);  // same job
    thumbs.Request(txt, 64, 3);
    CHECK(thumbs.PendingCount() <= 2);

    std::map<uint64_t, library::ThumbnailResult> results;
    ULONGLONG end = GetTickCount64() + 20000;
    while (results.size() < 3 && GetTickCount64() < end) {
        if (WaitForMessage(kMsg, 500, [](const MSG&) { return true; }))
            for (auto& r : thumbs.TakeCompleted()) results[r.cookie] = r;
    }
    CHECK_MSG(results.size() == 3, "got %zu results", results.size());
    if (results.size() == 3) {
        const auto& r1 = results[1];
        CHECK(r1.bitmap != nullptr);
        if (r1.bitmap) {
            CHECK(r1.bitmap->width <= 64 && r1.bitmap->height <= 64);
            CHECK(r1.bitmap->width == 64 && r1.bitmap->height == 32);  // aspect kept
            CHECK(r1.bitmap->pixels.size() == size_t{r1.bitmap->width} * r1.bitmap->height * 4);
            CHECK(r1.bitmap->pixels[3] == 255);  // opaque source -> opaque alpha
        }
        CHECK(results[2].bitmap == r1.bitmap);  // deduplicated: the very same bitmap
        CHECK(results[3].bitmap == nullptr && results[3].path == txt);
    }
    CHECK(thumbs.PendingCount() == 0);
    CHECK(thumbs.Cached(bmp, 64) != nullptr);
    CHECK(thumbs.Cached(bmp, 128) == nullptr);
    CHECK(thumbs.KnownMissing(txt, 64));
    CHECK(thumbs.MemoryUsed() == 64 * 32 * 4);

    // Cached requests complete immediately.
    thumbs.Request(bmp, 64, 9);
    auto now = thumbs.TakeCompleted();
    CHECK(now.size() == 1 && now[0].cookie == 9 && now[0].bitmap);

    // Cancellation: queued jobs that nobody wants are dropped.
    for (int i = 0; i < 40; ++i) {
        std::wstring p = dir + L"\\c" + std::to_wstring(i) + L".bmp";
        WriteBmp(p, 16 + i, 16);
        thumbs.Request(p, 32, 100 + static_cast<uint64_t>(i));
    }
    thumbs.CancelAllExcept({100, 101});
    end = GetTickCount64() + 20000;
    while (thumbs.PendingCount() > 0 && GetTickCount64() < end) Sleep(20);
    CHECK(thumbs.PendingCount() == 0);
    std::set<uint64_t> got;
    for (auto& r : thumbs.TakeCompleted()) got.insert(r.cookie);
    CHECK(got.count(100) && got.count(101));
    CHECK(got.size() <= 4);  // at most the two kept plus jobs already running

    // LRU memory cap.
    thumbs.SetMemoryLimit(64 * 32 * 4);
    CHECK(thumbs.MemoryUsed() <= 64 * 32 * 4);
    thumbs.SetMemoryLimit(0);
    CHECK(thumbs.MemoryUsed() == 0);
    CHECK(thumbs.Cached(bmp, 64) == nullptr);

    // Synchronous helper (needs COM on this thread).
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto direct = library::ExtractThumbnail(bmp, 100);
    CHECK(direct && direct->width == 100 && direct->height == 50);
    CHECK(library::ExtractThumbnail(txt, 100) == nullptr);
    if (SUCCEEDED(com)) CoUninitialize();
}

void TestLyricsLrc() {
    // Several stamps per line, offset, ID tags, word stamps, CRLF, out-of-order lines.
    const app::Lyrics l = app::ParseLyrics(
        L"[ar:Someone]\r\n[ti:Song]\r\n[offset:+500]\r\n[00:01.00]\r\n[00:12.50][01:02.25]Chorus line\r\n"
        L"[00:05.5]First <00:05.80>words <00:06.10>here\r\n[1:00:00.00]Late\r\n[00:20:50]Old style\r\n"
        L"[00:30.00]\r\n[00:31.00]\r\n[00:40.123]Last  \r\n[00:50.00]\r\n");
    CHECK(l.synced);
    CHECK_MSG(l.lines.size() == 8, "%zu lines", l.lines.size());
    if (l.lines.size() == 8) {
        CHECK(std::abs(l.lines[0].time - 5.0) < 1e-6 && l.lines[0].text == L"First words here");
        CHECK(std::abs(l.lines[1].time - 12.0) < 1e-6 && l.lines[1].text == L"Chorus line");
        CHECK(std::abs(l.lines[2].time - 20.0) < 1e-6 && l.lines[2].text == L"Old style");
        CHECK(l.lines[3].text.empty());  // one blank stamp kept as a pause, the repeat dropped
        CHECK(std::abs(l.lines[4].time - 39.623) < 1e-6 && l.lines[4].text == L"Last");
        CHECK(std::abs(l.lines[5].time - 49.5) < 1e-6 && l.lines[5].text.empty());
        CHECK(std::abs(l.lines[6].time - 61.75) < 1e-6 && l.lines[6].text == L"Chorus line");
        CHECK(std::abs(l.lines[7].time - 3599.5) < 1e-6 && l.lines[7].text == L"Late");
    }
    CHECK(l.LineAt(0) == -1);
    CHECK(l.LineAt(4.99) == -1);
    CHECK(l.LineAt(5.0) == 0);
    CHECK(l.LineAt(13) == 1);
    CHECK(l.LineAt(1e6) == 7);

    // No stamps: unsynced text, stanza breaks kept once, ID tags and blank edges dropped.
    const app::Lyrics u = app::ParseLyrics(L"[ar:X]\n\nLine one\n[Chorus]\nLine two\n\n\n\nLine three\n\n");
    CHECK(!u.synced);
    CHECK_MSG(u.lines.size() == 5, "%zu lines", u.lines.size());
    if (u.lines.size() == 5) {
        CHECK(u.lines[0].text == L"Line one" && u.lines[1].text == L"[Chorus]" && u.lines[3].text.empty());
        CHECK(u.lines[4].text == L"Line three");
    }
    CHECK(u.LineAt(10) == -1);

    // Broken stamps are text; empty input is empty.
    const app::Lyrics b = app::ParseLyrics(L"[00:1x.00]not a stamp\n[12]\n");
    CHECK(!b.synced && b.lines.size() == 2);
    CHECK(app::ParseLyrics(L"").Empty());
    CHECK(app::ParseLyrics(L"[00:10.00]\n[00:20.00]\n").Empty());

    // File decoding: UTF-8 with and without BOM, UTF-16 LE/BE, legacy code page.
    CHECK(app::DecodeLyricsFile("\xEF\xBB\xBF[00:01.00]\xD0\x9F\xD1\x80\xD0\xB8") == L"[00:01.00]При");
    CHECK(app::DecodeLyricsFile("[00:01.00]ok") == L"[00:01.00]ok");
    CHECK(app::DecodeLyricsFile(std::string("\xFF\xFE" "a\0b\0", 6)) == L"ab");
    CHECK(app::DecodeLyricsFile(std::string("\xFE\xFF\0a\0b", 6)) == L"ab");
    CHECK(!app::DecodeLyricsFile("caf\xE9").empty());
}

struct TestCase {
    const char* name;
    void (*fn)();
};

const TestCase kTests[] = {
    {"json_basics", TestJsonBasics},
    {"json_strings", TestJsonStrings},
    {"json_numbers", TestJsonNumbers},
    {"json_errors", TestJsonErrors},
    {"json_roundtrip", TestJsonRoundTrip},
    {"json_files", TestJsonFiles},
    {"schema", TestSchema},
    {"config_values", TestConfigValues},
    {"config_subscribe", TestConfigSubscribe},
    {"config_persistence", TestConfigPersistence},
    {"config_mpv", TestConfigMpv},
    {"corrupt_backup", TestCorruptBackup},
    {"hidden_condition", TestHiddenCondition},
    {"history", TestHistory},
    {"playlists", TestPlaylists},
    {"bookmarks_favorites", TestBookmarksFavorites},
    {"media_types", TestMediaTypes},
    {"episodes_order", TestEpisodesAndOrder},
    {"library_scan", TestLibraryScan},
    {"library_metadata", TestLibraryMetadata},
    {"thumbnails", TestThumbnails},
    {"lyrics_lrc", TestLyricsLrc},
};

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    const char* filter = argc > 1 ? argv[1] : nullptr;

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_temp = std::wstring(tmp) + L"lumen-tests-" + std::to_wstring(GetCurrentProcessId());
    DeleteTree(g_temp);
    SHCreateDirectoryExW(nullptr, g_temp.c_str(), nullptr);
    SetDataDirForTests(g_temp + L"\\data");

    int ran = 0;
    for (const TestCase& t : kTests) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = g_failures;
        auto start = std::chrono::steady_clock::now();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("[ %s ] %s (%.0f ms)\n", g_failures == before ? " OK " : "FAIL", t.name, ms);
        ++ran;
    }
    SetDataDirForTests({});
    DeleteTree(g_temp);
    std::printf("\n%d tests, %d checks, %d failures\n", ran, g_checks, g_failures);
    return g_failures;
}

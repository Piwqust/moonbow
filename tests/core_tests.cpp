// Unit tests for src/core, src/library and the LRC parser. Build and run with tests\build-tests.bat.
// A tiny runner: CHECK() records failures and keeps going; the exit code is the failure count.
// Every test works inside a fresh temp folder; SetDataDirForTests keeps %APPDATA%\Lumen untouched.

#include "../src/core/Config.h"
#include "../src/core/Json.h"
#include "../src/core/Paths.h"
#include "../src/core/SettingsSchema.h"
#include "../src/core/ShellText.h"
#include "../src/core/Skin.h"
#include "../src/app/player/Lyrics.h"
#include "../src/library/Bookmarks.h"
#include "../src/library/FileSettings.h"
#include "../src/library/History.h"
#include "../src/library/MediaLibrary.h"
#include "../src/library/MediaTypes.h"
#include "../src/library/Playlists.h"
#include "../src/library/TagReader.h"
#include "../src/library/Thumbnails.h"
#include "../src/app/player/Episodes.h"
#include "../src/app/player/SubText.h"
#include "../src/app/PlayQueue.h"
#include "../src/audio/CrossfadeMath.h"
#include "../src/audio/LoopbackSupport.h"
#include "../src/audio/Spectrum.h"
#include "../src/net/Cache.h"
#include "../src/net/Http.h"
#include "../src/net/OnlineData.h"
#include "../src/net/Secret.h"

#include <windows.h>
#include <winsock2.h>
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
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")  // the local HTTP server of the client test

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

    // The same in a large object, without the O(n^2) key check: 200 000 keys parse in well under
    // a second (the old scan took minutes).
    {
        std::string big = "{";
        for (int i = 0; i < 200000; ++i) big += "\"k" + std::to_string(i) + "\":" + std::to_string(i) + ",";
        big += "\"k5\":-5,\"k199999\":-1,\"k3\":-3}";
        const auto t0 = std::chrono::steady_clock::now();
        auto b = json::Parse(big);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK_MSG(ms < 1500, "%.0f ms", ms);
        CHECK(b && b->size() == 200000);
        CHECK(b && b->AsObject()[5].key == "k5" && b->GetInt("k5") == -5 && b->GetInt("k3") == -3);
        CHECK(b && b->AsObject()[199999].key == "k199999" && b->GetInt("k199999") == -1 && b->GetInt("k7") == 7);
        auto mid = json::Parse(R"({"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9,"j":10,"k":11,"l":12,)"
                               R"("m":13,"n":14,"o":15,"p":16,"q":17,"a":0,"q":0,"r":18})");
        CHECK(mid && mid->size() == 18 && mid->GetInt("a") == 0 && mid->GetInt("q") == 0 &&
              mid->AsObject()[0].key == "a" && mid->AsObject()[16].key == "q" && mid->AsObject()[17].key == "r");
    }

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
        CHECK(!c.label.empty() && !c.labelRu.empty() && !c.icon.empty());
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
                // Rows moved to another page keep their keys (saved values, main.cpp).
                static const std::map<std::string, std::string> kMoved = {
                    {"library.singleInstance", "interface"}, {"library.singleInstanceFileManager", "interface"},
                    {"library.enqueue", "interface"},        {"privacy.rememberPosition", "playback"},
                    {"general.theme", "interface"},          {"general.accent", "interface"}};
                const auto moved = kMoved.find(it.key);
                CHECK_MSG(moved != kMoved.end() ? c.id == moved->second
                                                : it.key.compare(0, c.id.size() + 1, c.id + ".") == 0,
                          "%s outside %s", k, c.id.c_str());
                CHECK_MSG(it.category == c.id && it.section == s.id, "%s", k);
                CHECK_MSG(FindItem(it.key) == &it, "%s", k);
                CHECK_MSG(!it.label.empty() && !it.labelRu.empty(), "%s label", k);
                CHECK_MSG(it.description.empty() == it.descriptionRu.empty(), "%s description pair", k);
                CHECK_MSG(it.unit.empty() == it.unitRu.empty(), "%s unit pair", k);
                CHECK_MSG(it.placeholder.empty() == it.placeholderRu.empty(), "%s placeholder pair", k);
                CHECK_MSG(it.actionLabel.empty() == it.actionLabelRu.empty(), "%s caption pair", k);
                CHECK_MSG(it.label != it.labelRu || it.label.find_first_of(L"0123456789") != std::wstring::npos ||
                              it.label == L"Replay gain" || it.label == L"User agent" ||
                              it.key.rfind("audio.spdif", 0) == 0,  // format names: DTS, Dolby TrueHD
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
    CHECK(theme && theme->type == ItemType::Segmented && theme->choices.size() == 3 && theme->def.AsString() == "system");
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
    // The old table repeats Lumen's own keys (README.md): S stops, V cycles subtitles.
    CHECK(FindKeyAction("stop") && FindKeyAction("stop")->defaultKey == L"S");
    CHECK(FindKeyAction("cycleSubtitles") && FindKeyAction("cycleSubtitles")->defaultKey == L"V");

    // A section heading is never repeated as the label of its first visible row (R3-09).
    for (const Category& c : cats)
        for (const Section& s : c.sections)
            for (const Item& it : s.items) {
                if (it.hidden || it.advanced) continue;
                if (it.type != ItemType::Info)  // Info widgets draw no row label
                    CHECK_MSG(it.label != s.title && it.labelRu != s.titleRu, "%s repeats its heading", it.key.c_str());
                break;
            }

    // Settings search works in both languages, case-insensitively.
    auto hits = Search(L"ГРОМКОСТЬ");
    CHECK(!hits.empty());
    bool foundNormalize = false;
    for (const Item* it : Search(L"loudness")) foundNormalize = foundNormalize || it->key == "audio.normalize";
    CHECK(foundNormalize);
    CHECK(Search(L"").empty());
    CHECK(Search(L"  ").empty());
    // Words in any order, from the page and section titles too.
    auto online = Search(L"конфиденциальность сеть");
    CHECK(online.size() == Search(L"СЕТЬ конфиденциальность").size());
    bool foundLyrics = false;
    for (const Item* it : online) foundLyrics = foundLyrics || it->key == "privacy.fetchLyrics";
    CHECK(foundLyrics);
    CHECK(Search(L"конфиденциальность zzz").empty());
    CHECK(AdvancedOptionsLabel(Lang::Ru) == L"Дополнительные параметры");
}

// --- Config -----------------------------------------------------------------------------------

void TestConfigValues() {
    FreshDataDir(L"config-values");
    Config c;
    CHECK(c.GetString("general.theme") == "system");
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
    // Files written before the action ids still load: ShortcutStore maps the old ids.
    kb = json::Parse(R"({"stop":{"key":"Ctrl+S"},"cycleSubtitles":{"key":"S"}})");
    CHECK(c.Set("controls.keyBindings", *kb));
    CHECK(json::Serialize(c.Get("controls.keyBindings")) == R"({"stop":{"key":"Ctrl+S"},"cycleSubtitles":{"key":"S"}})");

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
    // "none" flattens the bands (the player panel relies on it; it used to do nothing).
    CHECK(c.ApplyEqPreset("none"));
    CHECK(c.GetString("audio.eqPreset") == "none");
    CHECK(c.GetDouble("audio.eq60") == 0);
    CHECK(c.GetDouble("audio.eqPreamp") == 0);

    c.ResetAll();
    CHECK(c.GetString("general.theme") == "system");
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
        c.SetString("general.theme", "light");
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
    CHECK(doc && doc->GetNumber("version") == 2);

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
        CHECK(c.GetString("general.theme") == "light");
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

    // Version 1 files did not store the old default theme, Dark: they keep it; newer ones don't.
    for (int version : {1, 2}) {
        WriteFile(file, "{\"version\": " + std::to_string(version) + ", \"values\": {\"audio.targetLevel\": -16}}");
        Config c;
        CHECK(c.Load());
        CHECK(c.GetString("general.theme") == (version == 1 ? "dark" : "system"));
    }

    // A corrupt file is kept as settings.corrupt-<time>.json, reported once, and defaults are used.
    TakeCorruptFileNotices();
    WriteFile(file, "{ this is not json");
    {
        Config c;
        CHECK(!c.Load());
        CHECK(c.GetString("general.theme") == "system");
        CHECK(CountFiles(dir, L"settings.corrupt-*.json") == 1);
        const std::vector<std::wstring> notices = TakeCorruptFileNotices();
        CHECK(notices.size() == 1 && notices[0] == L"settings.json");
        CHECK(TakeCorruptFileNotices().empty());
    }

    // Renamed keys: the old value moves to the new key, unless the file has the new key too.
    WriteFile(file, R"({"version":1,"values":{"performance.interpolation":true}})");
    {
        Config c;
        CHECK(c.Load());
        CHECK(c.GetBool("video.smoothMotion"));
        CHECK(c.ToJson().Find("values")->Find("performance.interpolation") == nullptr);
    }
    WriteFile(file, R"({"version":1,"values":{"performance.interpolation":true,"video.smoothMotion":false}})");
    {
        Config c;
        CHECK(c.Load());
        CHECK(!c.GetBool("video.smoothMotion"));
    }
    // Buffering moved from milliseconds to seconds.
    WriteFile(file, R"({"version":2,"values":{"network.cacheLocal":500,"network.cacheNetwork":90000}})");
    {
        Config c;
        CHECK(c.Load());
        CHECK(c.GetDouble("network.cacheLocalSecs") == 0.5);
        CHECK(c.GetDouble("network.cacheNetworkSecs") == 90);
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

    // library.json of the wrong shape is kept like broken JSON and reads as no library.
    WriteFile(dir + L"\\library.json", R"({"version":1,"roots":"not-an-array"})");
    {
        library::MediaLibrary lib;
        CHECK(!lib.Load());
        CHECK(lib.Roots().empty());
    }
    CHECK(CountFiles(dir, L"library.corrupt-*.json") == 1);
    WriteFile(dir + L"\\library.json", R"([1,2])");
    {
        library::MediaLibrary lib;
        CHECK(!lib.Load());
    }
    CHECK(CountFiles(dir, L"library.corrupt-*.json") == 2);
    WriteFile(dir + L"\\library.json", R"({"version":1})");  // no roots yet: fine
    {
        library::MediaLibrary lib;
        CHECK(lib.Load() && lib.Roots().empty());
    }
    CHECK(TakeCorruptFileNotices().size() == 2);

    // The same shape check for the small stores: [] is kept, not loaded as empty and overwritten.
    WriteFile(dir + L"\\history.json", "[]");
    WriteFile(dir + L"\\playlists.json", R"({"playlists":{}})");
    WriteFile(dir + L"\\bookmarks.json", R"({"files":"x"})");
    WriteFile(dir + L"\\favorites.json", "[1]");
    {
        library::History h;
        library::Playlists pl;
        library::Bookmarks bm;
        library::Favorites fav;
        CHECK(!h.Load() && !pl.Load() && !bm.Load() && !fav.Load());
    }
    CHECK(CountFiles(dir, L"history.corrupt-*.json") == 3 && CountFiles(dir, L"playlists.corrupt-*.json") == 1 &&
          CountFiles(dir, L"bookmarks.corrupt-*.json") == 1 && CountFiles(dir, L"favorites.corrupt-*.json") == 1);
    TakeCorruptFileNotices();

    // A file that is locked (by a backup tool) while it loads is never overwritten afterwards.
    const std::string locked = R"({"version":1,"items":[{"path":"C:\\keep.mkv","lastPlayed":1}]})";
    WriteFile(dir + L"\\history.json", locked);
    WriteFile(dir + L"\\playlists.json", R"({"version":1,"playlists":[{"id":"p1","name":"Keep"}]})");
    {
        library::History h;
        library::Playlists pl;
        HANDLE lockH = CreateFileW((dir + L"\\history.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        HANDLE lockP = CreateFileW((dir + L"\\playlists.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(lockH != INVALID_HANDLE_VALUE && lockP != INVALID_HANDLE_VALUE);
        CHECK(!h.Load() && h.Entries().empty());
        CHECK(!pl.Load());
        CloseHandle(lockH);
        CloseHandle(lockP);
        h.UpdatePosition(L"C:\\new.mkv", 5, 100);
        pl.Create(L"New");
        CHECK(!h.Flush() && !pl.Flush());
    }  // the destructors flush too
    std::string after;
    CHECK(json::ReadFileUtf8(dir + L"\\history.json", &after) && after == locked);
    CHECK(json::ReadFileUtf8(dir + L"\\playlists.json", &after) && after.find("Keep") != std::string::npos);
    CHECK(TakeCorruptFileNotices().empty());

    // The same for settings.json and library.json: a change made after they were locked at load
    // never writes defaults (or an empty folder list) over them.
    const std::string lockedSettings = R"({"version":1,"values":{"audio.startupVolume":42}})";
    const std::string lockedLibrary = R"({"version":1,"roots":["C:\\Keep"]})";
    WriteFile(dir + L"\\settings.json", lockedSettings);
    WriteFile(dir + L"\\library.json", lockedLibrary);
    {
        Config c;
        library::MediaLibrary lib;
        HANDLE lockS = CreateFileW((dir + L"\\settings.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        HANDLE lockL = CreateFileW((dir + L"\\library.json").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(lockS != INVALID_HANDLE_VALUE && lockL != INVALID_HANDLE_VALUE);
        CHECK(!c.Load() && !lib.Load() && lib.Roots().empty());
        CloseHandle(lockS);
        CloseHandle(lockL);
        CHECK(c.SetDouble("audio.startupVolume", 10));
        CHECK(!c.Flush() && !c.Save());
        CHECK(lib.AddRoot(dir));
        CHECK(!lib.Save());
    }  // the Config destructor flushes too
    CHECK(json::ReadFileUtf8(dir + L"\\settings.json", &after) && after == lockedSettings);
    CHECK(json::ReadFileUtf8(dir + L"\\library.json", &after) && after == lockedLibrary);
    {
        Config c;  // unlocked again: it loads and saves as usual
        CHECK(c.Load() && c.GetDouble("audio.startupVolume") == 42);
        CHECK(c.SetDouble("audio.startupVolume", 11) && c.Save());
    }
    CHECK(json::ReadFileUtf8(dir + L"\\settings.json", &after) && after.find("11") != std::string::npos);
    DeleteFileW((dir + L"\\settings.json").c_str());
    DeleteFileW((dir + L"\\library.json").c_str());
    CHECK(TakeCorruptFileNotices().empty());

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
    // Sound and subtitles prefer the language Lumen speaks, not Windows'; a language picked stays.
    CHECK(FindOption(opts, "slang") == "ru,rus" && FindOption(opts, "alang") == "ru,rus");
    c.SetString("general.language", "en");
    CHECK(FindOption(MpvOptions(c), "slang") == "en,eng" && FindOption(MpvOptions(c), "alang") == "en,eng");
    c.SetString("subtitles.language", "de");
    CHECK(FindOption(MpvOptions(c), "slang") == "de,ger,deu");
    c.SetString("general.language", "ru");
    c.SetString("subtitles.language", "app");
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
    // Zero-copy video: crop, sharpening and default deinterlacing never put a CPU filter in vf.
    c.SetString("video.crop", "16:9");
    c.SetDouble("video.sharpen", 5);
    FindOption(MpvOptions(c), "vf", &found);
    CHECK(!found);
    CHECK(FindOption(MpvOptions(c), "glsl-shaders").find("cache\\shaders\\lumen-cas.glsl") != std::string::npos);
    CHECK(FindOption(MpvOptionFor(c, "video.sharpen"), "glsl-shader-opts") == "lumen_cas_sharpness=0.5");
    c.SetString("performance.vo", "gpu");  // legacy renderer: no shader parameters, CPU fallback
    CHECK(FindOption(MpvOptions(c), "vf") == "@lumen:lavfi=[cas=strength=0.5]");
    FindOption(MpvOptions(c), "glsl-shaders", &found);
    CHECK(!found);
    c.SetString("performance.vo", "gpu-next");
    c.SetDouble("video.sharpen", 0);
    CHECK(FindOption(MpvOptionFor(c, "video.sharpen"), "glsl-shaders").empty());  // reset clears the list
    c.SetString("video.deinterlaceMethod", "bwdif");
    CHECK(FindOption(MpvOptions(c), "vf") == "@lumen:lavfi=[bwdif=deint=interlaced]");
    CHECK(FindOption(MpvOptions(c), "deinterlace") == "no");
    c.SetString("video.deinterlaceMethod", "auto");
    // User shaders: only plain names of files that exist in <data>\shaders.
    {
        const std::wstring shaders = DataDir() + L"\\shaders";
        CreateDirectoryW(shaders.c_str(), nullptr);
        json::WriteFileAtomic(shaders + L"\\FSRCNNX.glsl", "//!HOOK LUMA\n");
        c.SetString("video.shaders", "FSRCNNX.glsl|missing.glsl|..\\settings.json");
        const std::string list = FindOption(MpvOptionFor(c, "video.shaders"), "glsl-shaders");
        CHECK_MSG(list.size() > 12 && list.compare(list.size() - 12, 12, "FSRCNNX.glsl") == 0, "%s", list.c_str());
        CHECK(list.find(';') == std::string::npos && list.find("missing") == std::string::npos);
        c.SetString("video.shaders", "");
    }
    // Quality presets and their way back to mpv's defaults.
    c.SetString("video.quality", "fast");
    opts = MpvOptions(c);
    CHECK(FindOption(opts, "scale") == "bilinear" && FindOption(opts, "dither") == "no");
    CHECK(FindOption(opts, "hdr-compute-peak") == "no");
    c.SetString("video.upscaler", "ewa_lanczos");
    CHECK(FindOption(MpvOptionFor(c, "video.upscaler"), "scale") == "ewa_lanczos");
    c.SetString("video.upscaler", "auto");
    c.SetString("video.quality", "balanced");
    live = MpvOptionFor(c, "video.quality");
    CHECK(FindOption(live, "scale") == "lanczos" && FindOption(live, "dscale") == "hermite");
    CHECK(FindOption(live, "correct-downscaling") == "yes" && FindOption(live, "hdr-compute-peak") == "auto");
    c.SetString("video.quality", "high");
    CHECK(FindOption(MpvOptions(c), "scale") == "ewa_lanczossharp");
    c.SetString("video.quality", "balanced");
    FindOption(MpvOptions(c), "scale", &found);
    CHECK(!found);  // mpv's own default
    c.SetDouble("video.targetPeak", 400);
    CHECK(FindOption(MpvOptions(c), "target-peak") == "400");
    c.SetBool("video.deband", true);
    CHECK(FindOption(MpvOptions(c), "deband") == "yes" && FindOption(MpvOptions(c), "deband-range") == "16");
    // Smooth motion overrides the clock sync choice.
    c.SetString("performance.clockSync", "vdrop");
    CHECK(FindOption(MpvOptions(c), "video-sync") == "display-vdrop");
    c.SetBool("video.smoothMotion", true);
    live = MpvOptionFor(c, "video.smoothMotion");
    CHECK(FindOption(live, "video-sync") == "display-resample" && FindOption(live, "interpolation") == "yes");
    CHECK(FindOption(live, "tscale") == "oversample");
    c.SetBool("video.smoothMotion", false);
    CHECK(FindOption(MpvOptionFor(c, "video.smoothMotion"), "video-sync") == "display-vdrop");
    c.SetString("performance.hwdec", "off");
    CHECK(FindOption(MpvOptionFor(c, "performance.hwdec"), "hwdec") == "no");
    c.SetString("network.httpProxy", "http://me@proxy:3128");
    c.SetString("network.httpProxyPassword", "p4ss");
    CHECK(FindOption(MpvOptions(c), "http-proxy") == "http://me:p4ss@proxy:3128");
    c.SetString("network.httpProxyPassword", "p@ss:w/rd %");  // encoded, or "@" ends the user part
    CHECK(FindOption(MpvOptions(c), "http-proxy") == "http://me:p%40ss%3Aw%2Frd%20%25@proxy:3128");
    c.SetString("network.httpProxyPassword", "p4ss");

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

void TestAudioSubtitleOptions() {
    FreshDataDir(L"config-audio-subs");
    Config c;
    auto opts = MpvOptions(c);
    bool found = false;
    FindOption(opts, "audio-spdif", &found);
    CHECK(!found);  // passthrough is off by default
    CHECK(FindOption(opts, "gapless-audio") == "weak");
    CHECK(FindOption(opts, "prefetch-playlist") == "yes");
    CHECK(FindOption(opts, "audio-exclusive") == "no");
    CHECK(FindOption(opts, "audio-file-auto") == "fuzzy");
    CHECK(FindOption(opts, "sub-border-style") == "outline-and-shadow");
    CHECK(FindOption(opts, "sub-back-color") == "#99000000");  // 60% black
    CHECK(FindOption(opts, "sub-use-margins") == "yes" && FindOption(opts, "sub-ass-force-margins") == "yes");
    CHECK(FindOption(opts, "sub-margin-y") == "22" && FindOption(opts, "secondary-sub-pos") == "0");
    FindOption(opts, "demuxer-readahead-secs", &found);
    CHECK(!found);  // mpv's own read-ahead unless buffering is custom
    CHECK(FindOption(opts, "ytdl") == (YtDlpPath().empty() ? "no" : "yes"));

    // Passthrough: the ticked formats, in a fixed order; turning it off clears the list live.
    c.SetBool("audio.passthrough", true);
    CHECK(FindOption(MpvOptionFor(c, "audio.passthrough"), "audio-spdif") == "ac3,dts");
    c.SetBool("audio.spdifTrueHd", true);
    c.SetBool("audio.spdifAc3", false);
    CHECK(FindOption(MpvOptionFor(c, "audio.spdifTrueHd"), "audio-spdif") == "dts,truehd");
    c.SetBool("audio.passthrough", false);
    auto live = MpvOptionFor(c, "audio.passthrough");
    CHECK(FindOption(live, "audio-spdif", &found).empty() && found);

    // Normalization modes share the equalizer's filter chain and come after it.
    c.SetBool("audio.eq", true);
    c.ApplyEqPreset("rock");
    c.SetBool("audio.normalize", true);
    c.SetString("audio.normalizeMode", "dynamic");
    std::string af = FindOption(MpvOptionFor(c, "audio.normalizeMode"), "af");
    CHECK(af.find("equalizer=") != std::string::npos && af.find("dynaudnorm=") != std::string::npos);
    CHECK(af.find("equalizer=") < af.find("dynaudnorm=") && af.find("loudnorm") == std::string::npos);
    c.SetString("audio.normalizeMode", "ebu");
    af = FindOption(MpvOptions(c), "af");
    CHECK(af.find("loudnorm=I=-23") != std::string::npos && af.find("dynaudnorm") == std::string::npos);
    c.SetString("audio.replayGain", "album");
    CHECK(FindOption(MpvOptions(c), "replaygain") == "album");

    c.SetString("subtitles.background", "box");
    c.SetString("subtitles.backColor", "#202020");
    c.SetDouble("subtitles.backOpacity", 100);
    CHECK(FindOption(MpvOptionFor(c, "subtitles.backOpacity"), "sub-back-color") == "#FF202020");
    CHECK(FindOption(MpvOptionFor(c, "subtitles.background"), "sub-border-style") == "background-box");
    c.SetBool("subtitles.useBlackBars", false);
    live = MpvOptionFor(c, "subtitles.useBlackBars");
    CHECK(FindOption(live, "sub-use-margins") == "no" && FindOption(live, "sub-ass-force-margins") == "no");
    c.SetDouble("subtitles.blur", 1.5);
    CHECK(FindOption(MpvOptionFor(c, "subtitles.blur"), "sub-blur") == "1.5");

    c.SetString("network.buffering", "custom");
    c.SetDouble("network.cacheLocalSecs", 0.3);
    CHECK(FindOption(MpvOptionFor(c, "network.cacheLocalSecs"), "demuxer-readahead-secs") == "0.3");
    c.SetString("network.buffering", "normal");
    CHECK(FindOption(MpvOptionFor(c, "network.cacheLocalSecs"), "demuxer-readahead-secs") == "1");  // reset

    c.SetDouble("audio.maxVolume", 300);
    CHECK(c.GetDouble("audio.maxVolume") == 200);
    CHECK(FindOption(MpvOptions(c), "volume-max") == "200");

    // yt-dlp's path and the user's script options share one list; the user's come last.
    c.SetString("extensions.scriptOpts", "osc-visibility=always");
    const std::string so = FindOption(MpvOptionFor(c, "extensions.scriptOpts"), "script-opts");
    if (YtDlpPath().empty()) {
        CHECK(so == "osc-visibility=always");
    } else {
        CHECK(so.rfind("ytdl_hook-ytdl_path=%", 0) == 0);
        CHECK(so.size() > 22 && so.substr(so.size() - 22) == ",osc-visibility=always");
    }
    c.SetBool("network.ytdl", false);
    CHECK(FindOption(MpvOptionFor(c, "network.ytdl"), "ytdl") == "no");
    CHECK(FindOption(MpvOptions(c), "script-opts") == "osc-visibility=always");
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
        CHECK(h.UpdatePosition(L"C:\\clip.mkv", 30, 100));
        const library::HistoryEntry removed = *h.Find(L"C:\\clip.mkv");
        CHECK(h.Remove(L"c:\\CLIP.mkv"));
        CHECK(!h.Remove(L"c:\\CLIP.mkv"));
        // Undo puts the entry (and its resume point) back at its place, once.
        CHECK(h.Restore(removed, 0) && !h.Restore(removed, 0));
        CHECK(h.Entries().front().path == L"C:\\clip.mkv" && h.Entries().front().position == 30);
        CHECK(h.Remove(L"C:\\clip.mkv"));
        CHECK(h.Dirty());
        CHECK(h.Flush());
        CHECK(!h.Dirty());

        // Paused: the 3 s save repeats the same position and must not rewrite the file.
        CHECK(h.UpdatePosition(L"E:\\Music\\b.mp3", 42, 200) && h.Dirty() && h.Flush());
        CHECK(h.UpdatePosition(L"e:\\music\\B.mp3", 42.4, 200.2) && !h.Dirty());
        CHECK(h.UpdatePosition(L"E:\\Music\\b.mp3", 43.5, 200) && h.Dirty() && h.Flush());
        CHECK(h.Find(L"E:\\Music\\b.mp3")->position == 43.5);
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

void TestFileSettings() {
    std::wstring dir = FreshDataDir(L"filesettings");
    const std::wstring file = dir + L"\\file-settings.json";
    library::FileOptions a;
    a.aid = 2;
    a.sid = 0;  // subtitles off
    a.subDelay = 0.5;
    {
        library::FileSettings fs;
        CHECK(fs.Load() && fs.size() == 0 && !fs.Dirty());
        CHECK(fs.Find(L"D:\\Видео\\Фильм.mkv") == nullptr);
        CHECK(fs.Put(L"D:\\Видео\\Фильм.mkv", a) && fs.Dirty());
        CHECK(!fs.Put(L"d:/видео/фильм.MKV", a));  // same file, same values: nothing to write
        const library::FileOptions* f = fs.Find(L"D:\\ВИДЕО\\ФИЛЬМ.MKV");
        CHECK(f && f->aid == 2 && f->sid == 0 && f->subDelay == 0.5 && !f->audioDelay && f->HasBasic());
        CHECK(fs.Find(L"D:\\Видео\\Другой.mkv") == nullptr);  // nothing leaks to another file

        library::FileOptions all;
        all.audioFile = L"E:\\Сериал\\s01e01.rus.mka";
        all.subFile = L"E:\\Сериал\\s01e01.eng.srt";
        all.secondarySid = 3;
        all.audioDelay = -0.25;
        all.speed = 1.25;
        all.aspect = "2.35:1";
        all.zoomMode = "half";
        all.panX = 0.1;
        all.panY = -0.2;
        all.rotate = 90;
        all.flip = 1;
        all.deinterlace = "yes";
        all.abA = 12.5;
        all.abB = 30;
        all.subPos = 80;
        all.subScale = 1.5;
        CHECK(fs.Put(L"E:\\Сериал\\s01e01.mkv", all));
        CHECK(fs.size() == 2);
        CHECK(fs.Flush() && !fs.Dirty());

        // Setting everything back to the defaults removes the file.
        CHECK(fs.Put(L"C:\\gone.mkv", a) && fs.size() == 3);
        CHECK(fs.Put(L"c:\\GONE.mkv", library::FileOptions{}) && fs.size() == 2 && !fs.Find(L"C:\\gone.mkv"));
        CHECK(!fs.Put(L"C:\\never.mkv", library::FileOptions{}));
        CHECK(fs.Remove(L"E:\\СЕРИАЛ\\S01E01.MKV") && !fs.Remove(L"E:\\Сериал\\s01e01.mkv"));
        CHECK(fs.Put(L"E:\\Сериал\\s01e01.mkv", all));
    }  // the destructor saves
    {
        library::FileSettings fs;
        CHECK(fs.Load() && fs.size() == 2);
        const library::FileOptions* f = fs.Find(L"E:\\Сериал\\s01e01.mkv");
        CHECK(f && f->audioFile == L"E:\\Сериал\\s01e01.rus.mka" && f->subFile == L"E:\\Сериал\\s01e01.eng.srt");
        CHECK(f && f->secondarySid == 3 && f->audioDelay == -0.25 && f->speed == 1.25 && f->aspect == "2.35:1");
        CHECK(f && f->zoomMode == "half" && f->panX == 0.1 && f->panY == -0.2 && f->rotate == 90 && f->flip == 1);
        CHECK(f && f->deinterlace == "yes" && f->abA == 12.5 && f->abB == 30 && f->subPos == 80 && f->subScale == 1.5);
        CHECK(f && !f->aid && !f->sid && !f->zoom);
        const library::FileOptions* g = fs.Find(L"D:\\Видео\\Фильм.mkv");
        CHECK(g && *g == a);
        fs.Clear();
        CHECK(fs.size() == 0 && fs.Dirty() && fs.Flush());
    }
    {
        library::FileSettings fs;
        CHECK(fs.Load() && fs.size() == 0);
    }

    // A series' track languages outlive the session (the next episode opened from Explorer).
    {
        library::FileSettings fs;
        CHECK(fs.Load() && !fs.FindShow(L"show"));
        const library::ShowLanguages ru{L"rus", L"Русский 660 Hz", L"rus", L"", false};
        CHECK(fs.PutShow(L"show", ru) && !fs.PutShow(L"SHOW", ru) && !fs.PutShow(L"", ru));
        CHECK(fs.PutShow(L"D:\\Сериал", {L"", L"", L"", L"", true}));  // subtitles off, sound as is
        CHECK(fs.Flush() && fs.size() == 0);
    }
    {
        library::FileSettings fs;
        CHECK(fs.Load());
        const library::ShowLanguages* s = fs.FindShow(L"Show");
        CHECK(s && s->audioLang == L"rus" && s->audioTitle == L"Русский 660 Hz" && s->subLang == L"rus" && !s->subsOff);
        const library::ShowLanguages* o = fs.FindShow(L"d:\\сериал");
        CHECK(o && o->subsOff && o->audioLang.empty());
        CHECK(fs.PutShow(L"D:\\Сериал", {}) && !fs.FindShow(L"D:\\Сериал"));
        fs.Clear();  // History > Clear forgets the shows too
        CHECK(!fs.FindShow(L"show") && fs.Flush());
    }

    // Values that would break mpv's "a=1,b=2" loadfile options are dropped; bad numbers too.
    WriteFile(file, R"({"version":1,"files":[
        {"path":"C:\\a.mkv","used":5,"aspect":"16:9,script=x.lua","zoomMode":"huge","deinterlace":"yes,vf=x",
         "speed":0,"subScale":-1,"aid":-4,"flip":7,"sid":1},
        {"path":"C:\\b.mkv","used":6},
        {"path":"","used":7,"aid":1},
        {"path":"C:\\c.mkv","used":8,"aid":"two","audioDelay":"x","rotate":180}]})");
    {
        library::FileSettings fs;
        CHECK(fs.Load() && fs.size() == 2);  // b has nothing stored, the unnamed one has no file
        const library::FileOptions* f = fs.Find(L"C:\\a.mkv");
        CHECK(f && f->sid == 1 && !f->aspect && !f->zoomMode && !f->deinterlace && !f->speed && !f->subScale &&
              !f->aid && !f->flip);
        const library::FileOptions* c = fs.Find(L"C:\\c.mkv");
        CHECK(c && !c->aid && !c->audioDelay && c->rotate == 180);
    }

    // The wrong shape is kept as a backup, not loaded as empty and overwritten.
    WriteFile(file, R"({"files":{}})");
    {
        library::FileSettings fs;
        CHECK(!fs.Load() && fs.size() == 0);
    }
    CHECK(CountFiles(dir, L"file-settings.corrupt-*.json") == 1);
    WriteFile(file, "{not json");
    {
        library::FileSettings fs;
        CHECK(!fs.Load());
        fs.Put(L"C:\\new.mkv", a);  // saved over the broken file: its copy is the backup
    }
    CHECK(CountFiles(dir, L"file-settings.corrupt-*.json") == 2);
    TakeCorruptFileNotices();

    // A file that is locked while it loads is never overwritten afterwards.
    const std::string locked = R"({"version":1,"files":[{"path":"C:\\keep.mkv","used":1,"aid":2}]})";
    WriteFile(file, locked);
    {
        library::FileSettings fs;
        HANDLE lock = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(lock != INVALID_HANDLE_VALUE);
        CHECK(!fs.Load() && fs.size() == 0);
        CloseHandle(lock);
        fs.Put(L"C:\\other.mkv", a);
        CHECK(!fs.Flush());
    }
    std::string after;
    CHECK(json::ReadFileUtf8(file, &after) && after == locked);

    // At most 5000 files; the least recently used (changed or opened) go first.
    DeleteFileW(file.c_str());
    {
        library::FileSettings fs;
        CHECK(fs.Load());
        library::FileOptions o;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5100; ++i) {
            o.audioDelay = 0.001 * (i + 1);
            fs.Put(L"F:\\bulk\\" + std::to_wstring(i) + L".mkv", o);
            if (i == 4000) fs.Touch(L"F:\\bulk\\0.mkv");  // opened again: newer than 1..4000
        }
        const double putMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(fs.size() == library::FileSettings::kMaxEntries);
        CHECK(fs.Find(L"F:\\bulk\\0.mkv") != nullptr);
        CHECK(fs.Find(L"F:\\bulk\\1.mkv") == nullptr && fs.Find(L"F:\\bulk\\100.mkv") == nullptr);
        CHECK(fs.Find(L"F:\\bulk\\101.mkv") != nullptr);
        CHECK(fs.Find(L"F:\\bulk\\5099.mkv") != nullptr);
        // The lookup a file open makes stays negligible with a full store.
        t0 = std::chrono::steady_clock::now();
        int found = 0;
        for (int i = 0; i < 10000; ++i) found += fs.Find(L"F:\\bulk\\" + std::to_wstring(i % 5100) + L".mkv") != nullptr;
        const double findUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 10000;
        CHECK(found > 9000);
        CHECK_MSG(findUs < 50, "%.2f us per lookup", findUs);
        t0 = std::chrono::steady_clock::now();
        CHECK(fs.Save());
        const double saveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        library::FileSettings again;
        t0 = std::chrono::steady_clock::now();
        CHECK(again.Load() && again.size() == library::FileSettings::kMaxEntries);
        const double loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(again.Find(L"F:\\bulk\\0.mkv") && again.Find(L"F:\\bulk\\5099.mkv")->audioDelay == 5.1);
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa);
        std::printf("  5000 files: %u KB, put x5100 %.1f ms, lookup %.2f us, save %.1f ms, load %.1f ms\n",
                    static_cast<unsigned>(fa.nFileSizeLow / 1024), putMs, findUs, saveMs, loadMs);
    }
    // A hand-edited file far over the cap keeps the newest 5000 in one pass (the old eviction
    // scanned the whole store once per extra entry).
    {
        std::string text = R"({"version":1,"files":[)";
        for (int i = 0; i < 30000; ++i)
            text += (i ? "," : "") + std::string(R"({"path":"F:\\hand\\)") + std::to_string(i) + R"(.mkv","used":)" +
                    std::to_string(i + 1) + R"(,"aid":2})";
        text += "]}";
        WriteFile(file, text);
        library::FileSettings fs;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(fs.Load());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(fs.size() == library::FileSettings::kMaxEntries);
        CHECK(fs.Find(L"F:\\hand\\29999.mkv") && fs.Find(L"F:\\hand\\25000.mkv"));
        CHECK(!fs.Find(L"F:\\hand\\24999.mkv") && !fs.Find(L"F:\\hand\\0.mkv"));
        CHECK_MSG(ms < 1000, "%.0f ms", ms);
        std::printf("  hand-edited 30000 files: load + evict %.1f ms\n", ms);
    }

    // History > Clear clears this store too (LiveState sets History::onCleared), even with no history.
    {
        library::History h;
        CHECK(h.Load());
        int cleared = 0;
        h.onCleared = [&cleared] { ++cleared; };
        h.Clear();
        CHECK(cleared == 1);
        h.RecordPlay(L"C:\\x.mkv");
        h.Clear();
        CHECK(cleared == 2 && h.size() == 0);
    }
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
        const library::Playlist deleted = *p.Find(other);
        CHECK(p.Delete(other));
        CHECK(!p.Delete(other));
        CHECK(p.Restore(deleted, 0) && !p.Restore(deleted, 0));
        CHECK(p.All().front().id == other && p.All().front().name == deleted.name);
        CHECK(p.Delete(other));
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
    // Lumen.exe's manifest makes CP_ACP UTF-8 (this exe has none), so the code page is explicit.
    WriteFile(sub + L"\\ansi.m3u", "\xCF\xF0\xE8\xE2\xE5\xF2.mkv\n");  // "Привет.mkv" in cp1251
    CHECK(library::ReadM3u(sub + L"\\ansi.m3u", &items, 1251) && items.size() == 1 &&
          items[0].path == sub + L"\\Привет.mkv");
    if (GetACP() != CP_UTF8) CHECK(json::LegacyCodePage() == GetACP());  // no manifest: the legacy one
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
    // One release-tag list for episode titles and online lookups: case and dashes don't matter.
    CHECK(library::IsReleaseTag(L"WEB-DL") && library::IsReleaseTag(L"webdl") && library::IsReleaseTag(L"Remastered"));
    CHECK(library::IsReleaseTag(L"INTERNAL") && library::IsReleaseTag(L"x-264") && library::IsReleaseTag(L"DDP5"));
    CHECK(!library::IsReleaseTag(L"Pilot") && !library::IsReleaseTag(L"") && !library::IsReleaseTag(L"-"));
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
    // Search words: each must appear, in any order and any field; extra spaces are ignored.
    CHECK(library::FoldedContains(L"01 track number 3\nartist a", L"track 3"));
    CHECK(library::FoldedContains(L"01 track number 3\nartist a", L"artist  3 "));
    CHECK(library::FoldedContains(L"01 track number 3\nartist a", L""));
    CHECK(library::FoldedContains(L"01 track number 3\nartist a", L"umber 3"));
    CHECK(!library::FoldedContains(L"01 track number 3\nartist a", L"track 4"));
    CHECK(!library::FoldedContains(L"abc", L"abcd"));
    CHECK(library::UnixMsNow() > 1700000000000LL);

    // Subtitle matching (Add Subtitles): a prefix whose number goes on is another file.
    CHECK(library::SubtitleMatchScore(L"D:\\L\\Lecture 1.mp4", L"D:\\L\\lecture1.srt") == 100);
    CHECK(library::SubtitleMatchScore(L"D:\\L\\Show_E01.mkv", L"D:\\L\\Show_E01_English.srt") == 90);
    CHECK(library::SubtitleMatchScore(L"D:\\L\\Movie (2020).mkv", L"D:\\L\\Movie.srt") == 70);
    CHECK(library::SubtitleMatchScore(L"D:\\L\\Lecture 1.mp4", L"D:\\L\\Lecture 10.srt") == 0);
    CHECK(library::SubtitleMatchScore(L"D:\\L\\Lecture 10.mp4", L"D:\\L\\Lecture 1.srt") == 0);
    CHECK(library::SubtitleMatchScore(L"D:\\L\\Show.S01E02.mkv", L"D:\\L\\show 1x02.srt") == 60);
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
    CHECK(library::EpisodeLabel(L"Show.Name.S01E01.Pilot.1080p.WEB-DL") == L"S01E01 · Pilot");
    CHECK(library::EpisodeLabel(L"Show.Name.S01E02.The.Second.One") == L"S01E02 · The Second One");
    CHECK(library::EpisodeLabel(L"Show Name - S01E03") == L"S01E03 · Show Name");
    CHECK(library::EpisodeLabel(L"Show 1x03 - Pilot") == L"S01E03 · Pilot");
    CHECK(library::EpisodeLabel(L"S03E01E02 [1080p]") == L"S03E01");
    CHECK(library::EpisodeLabel(L"The.Show.s2e10.720p") == L"S02E10 · The Show");
    CHECK(library::EpisodeLabel(L"Chaptered Demo (1080p)").empty());

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
    // A mislabelled file (an MP3 named .mkv) is still media; text and empty files are not.
    const unsigned char id3[] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0x22, 'T', 'S'};
    CHECK(!library::HeaderMatchesExtension(L"song.mkv", id3, sizeof(id3)));
    CHECK(library::HeaderLooksLikeMedia(id3, sizeof(id3)));
    CHECK(library::HeaderLooksLikeMedia(mp4, sizeof(mp4)));
    CHECK(library::HeaderLooksLikeMedia(mkv, sizeof(mkv)));
    CHECK(!library::HeaderLooksLikeMedia(text, 12));
    CHECK(!library::HeaderLooksLikeMedia(mp4, 0));

    CHECK(library::ParseDiscNumber(L"2/2") == 2);
    CHECK(library::ParseDiscNumber(L" 1 / 3") == 1);
    CHECK(library::ParseDiscNumber(L"10") == 10);
    CHECK(library::ParseDiscNumber(L"") == 0);
    CHECK(library::ParseDiscNumber(L"/2") == 0);
    CHECK(library::ParseDiscNumber(L"CD1") == 0);

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

// Videos › Shows: season folders, numbered files in them, grouping and "Next: S01E03".
void TestShows() {
    std::wstring show;
    CHECK(library::ParseSeasonFolder(L"Season 2", &show) == 2 && show.empty());
    CHECK(library::ParseSeasonFolder(L"Series S01", &show) == 1 && show == L"Series");
    CHECK(library::ParseSeasonFolder(L"The.Show.Season.03.1080p", &show) == 3 && show == L"The Show");
    CHECK(library::ParseSeasonFolder(L"Сезон 4", &show) == 4);
    CHECK(library::ParseSeasonFolder(L"Шоу - 2 сезон", &show) == 2 && show == L"Шоу");
    CHECK(library::ParseSeasonFolder(L"Simpsons Season 5 (2019)", &show) == 5 && show == L"Simpsons");
    CHECK(library::ParseSeasonFolder(L"Seasons", nullptr) < 0);
    CHECK(library::ParseSeasonFolder(L"Best S1 Clips", nullptr) < 0);
    CHECK(library::ParseSeasonFolder(L"Videos", nullptr) < 0);

    library::EpisodeInfo ep;
    CHECK(library::ParseEpisodePath(L"D:\\TV\\Show\\Season 2\\03 - Pilot.mkv", &ep) && ep.show == L"Show" &&
          ep.season == 2 && ep.episode == 3 && ep.title == L"Pilot");
    CHECK(library::ParseEpisodePath(L"D:\\TV\\Show\\Season 1\\Episode 7.mkv", &ep) && ep.episode == 7);
    CHECK(library::ParseEpisodePath(L"D:\\TV\\Шоу\\Сезон 1\\Серия 4.mkv", &ep) && ep.show == L"Шоу" && ep.episode == 4);
    CHECK(library::ParseEpisodePath(L"D:\\TV\\Show S02\\E05.mkv", &ep) && ep.show == L"Show" && ep.season == 2 &&
          ep.episode == 5);
    CHECK(library::ParseEpisodePath(L"D:\\TV\\Show\\Season 1\\S01E02.mkv", &ep) && ep.show == L"Show" && ep.episode == 2);
    CHECK(library::ParseEpisodePath(L"D:\\TV\\Show\\S01E02.mkv", &ep) && ep.show == L"Show");
    CHECK(library::ParseEpisodePath(L"D:\\Video\\Series S01E03.mp4", &ep) && ep.show == L"Series");
    CHECK(!library::ParseEpisodePath(L"D:\\TV\\Show\\Season 1\\Making of.mkv", nullptr));
    CHECK(!library::ParseEpisodePath(L"D:\\TV\\Show\\Season 1\\1080p.mkv", nullptr));
    CHECK(!library::ParseEpisodePath(L"D:\\Video\\2001 A Space Odyssey.mkv", nullptr));
    CHECK(!library::ParseEpisodePath(L"D:\\Video\\Movie (1080p).mkv", nullptr));

    // Two season folders and loose "1x02" names are one show; episodes in order, movies apart.
    const wchar_t* paths[] = {
        L"D:\\TV\\Show\\Season 2\\01 Return.mkv", L"D:\\TV\\Show\\Season 1\\Show S01E03.mkv",
        L"D:\\TV\\Show\\Season 1\\Show S01E02.mkv", L"D:\\Video\\Movie.mkv", L"D:\\TV\\show 1x01.mkv",
        L"D:\\TV\\Other.Show.S01E01.mkv", L"D:\\Video\\Clip.mp4",
    };
    std::vector<library::MediaItem> items(std::size(paths));
    std::vector<const library::MediaItem*> videos;
    for (size_t i = 0; i < items.size(); ++i) {
        items[i].path = paths[i];
        items[i].folder = library::ParentFolder(paths[i]);
        videos.push_back(&items[i]);
    }
    std::vector<const library::MediaItem*> movies;
    const auto shows = library::GroupShows(videos, &movies);
    CHECK(shows.size() == 2 && movies.size() == 2 && movies[0] == &items[3] && movies[1] == &items[6]);
    if (shows.size() == 2) {
        CHECK(shows[0].name == L"Other Show" && shows[0].episodes.size() == 1);
        const library::Show& s = shows[1];
        std::wstring order;
        for (const library::Episode& e : s.episodes)
            order += std::to_wstring(e.info.season) + L"x" + std::to_wstring(e.info.episode) + L",";
        CHECK(order == L"1x1,1x2,1x3,2x1,");
        CHECK(s.folder == L"D:\\TV\\Show");
        CHECK(s.key == library::SearchFold(s.name));
    }

    CHECK(library::NextUp({}) == -1);
    CHECK(library::NextUp({false, false}) == 0);
    CHECK(library::NextUp({true, false, false}) == 1);
    CHECK(library::NextUp({true, false, true, false}) == 3);  // after the last watched, not the skipped one
    CHECK(library::NextUp({false, true}) == 0);                // the end is watched: the first gap
    CHECK(library::NextUp({true, true}) == -1);
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

// The player's "Next episode" card and "Skip intro" button (src/app/player/Episodes.h).
void TestPlayerEpisodes() {
    namespace ep = app::episodes;
    CHECK(ep::IsNext(L"D:\\TV\\Show.S01E02.1080p.mkv", L"D:\\TV\\Show.S01E03.1080p.mkv"));
    CHECK(ep::IsNext(L"D:\\TV\\Show.S01E09.mkv", L"D:\\TV\\Show.S02E01.mkv"));
    CHECK(ep::IsNext(L"D:\\TV\\Show 1x02.mkv", L"D:\\TV\\Show 1x03.mkv"));
    CHECK(ep::IsNext(L"D:\\TV\\S01E01.mkv", L"D:\\TV\\S01E02.mkv"));        // no show name: same folder
    CHECK(!ep::IsNext(L"D:\\TV\\S01E01.mkv", L"D:\\Other\\S01E02.mkv"));
    CHECK(!ep::IsNext(L"D:\\TV\\Show.S01E02.mkv", L"D:\\TV\\Other.S01E03.mkv"));
    CHECK(!ep::IsNext(L"D:\\TV\\Show.S01E02.mkv", L"D:\\TV\\Show.S01E04.mkv"));
    CHECK(!ep::IsNext(L"D:\\Clips\\Clip 1920x1080.mkv", L"D:\\Clips\\Clip 1920x1081.mkv"));
    const std::vector<std::wstring> folder = {L"D:\\TV\\Show.S01E01.mkv", L"D:\\TV\\Show.S02E01.mkv",
                                              L"D:\\TV\\Show.S01E02.mkv", L"D:\\TV\\Show.S01E03.mkv"};
    CHECK(ep::FindNext(folder[0], folder) == 2);   // S01E02 before S02E01
    CHECK(ep::FindNext(folder[3], folder) == 1);   // no S01E04: the season ended, S02E01 follows
    CHECK(ep::FindNext(folder[1], folder) == -1);
    CHECK(ep::FindNext(L"D:\\TV\\Movie.mkv", folder) == -1);
    CHECK(ep::Label(ep::Parse(L"D:\\TV\\Show.s1e3.mkv")) == L"S01E03");
    CHECK(ep::DisplayName(L"D:\\TV\\Series S01E02.mp4") == L"Series");
    CHECK(ep::DisplayName(L"D:\\TV\\Show.Name.S01E02.The.Title.mkv") == L"Show Name · The Title");
    CHECK(ep::DisplayName(L"D:\\TV\\Show - 1x02 - Pilot.mkv") == L"Show · Pilot");
    CHECK(ep::DisplayName(L"D:\\TV\\S01E02.mkv").empty());
    CHECK(ep::DisplayName(L"D:\\TV\\Movie.mkv") == L"Movie");
    // The window title of an episode.
    CHECK(ep::Title(L"D:\\TV\\Show S01\\Show S01E02.mkv") == L"Show — S01E02");
    CHECK(ep::Title(L"D:\\TV\\S01E02.mkv") == L"S01E02");
    CHECK(ep::Title(L"D:\\TV\\Movie.mkv").empty());

    CHECK(ep::KindOf(L"Opening") == ep::ChapterKind::Intro);
    CHECK(ep::KindOf(L"OP1") == ep::ChapterKind::Intro);
    CHECK(ep::KindOf(L"Opening Credits") == ep::ChapterKind::Intro);
    CHECK(ep::KindOf(L"Заставка") == ep::ChapterKind::Intro);
    CHECK(ep::KindOf(L"End Credits") == ep::ChapterKind::Credits);
    CHECK(ep::KindOf(L"ED") == ep::ChapterKind::Credits);
    CHECK(ep::KindOf(L"ТИТРЫ") == ep::ChapterKind::Credits);
    CHECK(ep::KindOf(L"Operation") == ep::ChapterKind::Other);
    CHECK(ep::KindOf(L"Chapter 2") == ep::ChapterKind::Other);
    CHECK(ep::KindOf(L"Edward") == ep::ChapterKind::Other);
    // "OP" and "ED" are whole titles, maybe numbered; inside other words they mean something else.
    CHECK(ep::KindOf(L"OP 2") == ep::ChapterKind::Intro && ep::KindOf(L"02 - OP") == ep::ChapterKind::Intro);
    CHECK(ep::KindOf(L"[ED]") == ep::ChapterKind::Credits);
    CHECK(ep::KindOf(L"Sonata Op. 27") == ep::ChapterKind::Other);
    CHECK(ep::KindOf(L"Ed's Story") == ep::ChapterKind::Other);
    CHECK(ep::KindOf(L"Ed Sheeran") == ep::ChapterKind::Other);
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
    CHECK(snap->Search(L"10 фильм").size() == 1);
    CHECK(snap->Search(L"фильм ёлок").empty());

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

// Genres and discs come from library-cache.json (version 5) on a warm start, without reading
// the files; an older cache is read again.
void TestLibraryCacheTags() {
    const std::wstring dir = FreshDataDir(L"library-cache");
    const std::wstring root = MakeDir(L"lib-cache");
    const std::wstring song = root + L"\\Album\\01 Song.mp3";
    WriteFile(song, std::string(64, 'x'));
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    CHECK(GetFileAttributesExW(song.c_str(), GetFileExInfoStandard, &fa));
    const int64_t mtime = library::FileTimeToUnixMs((static_cast<uint64_t>(fa.ftLastWriteTime.dwHighDateTime) << 32) |
                                                    fa.ftLastWriteTime.dwLowDateTime);
    auto writeCache = [&](int version) {
        json::Value e = json::Value::MakeObject();
        e.Set("p", json::ToUtf8(song));
        e.Set("s", 64);
        e.Set("m", mtime);
        e.Set("t", "Song");
        e.Set("g", "Rock");
        e.Set("k", 2);
        json::Value items = json::Value::MakeArray();
        items.Push(std::move(e));
        json::Value doc = json::Value::MakeObject();
        doc.Set("version", version);
        doc.Set("items", std::move(items));
        CHECK(json::WriteFileAtomic(dir + L"\\library-cache.json", json::Serialize(doc)));
    };
    library::MediaLibrary lib;
    lib.SetRoots({root});
    writeCache(5);
    auto snap = ScanAndWait(lib);
    const library::MediaItem* m = snap ? snap->FindItem(song) : nullptr;
    CHECK(m && m->genre == L"Rock" && m->disc == 2 && m->title == L"Song");
    CHECK(snap && snap->stats.metadataFiles == 0);
    writeCache(3);
    snap = ScanAndWait(lib);
    m = snap ? snap->FindItem(song) : nullptr;
    CHECK(snap && snap->stats.metadataFiles == 1);
    CHECK(m && m->genre.empty() && m->title.empty());  // read from the (untagged) file

    // A root that is unplugged during a scan keeps its cached tags for when it comes back.
    const std::wstring usb = MakeDir(L"lib-cache-usb");
    const std::wstring usbSong = usb + L"\\02 Away.mp3";
    WriteFile(usbSong, std::string(64, 'y'));
    WIN32_FILE_ATTRIBUTE_DATA ua{};
    CHECK(GetFileAttributesExW(usbSong.c_str(), GetFileExInfoStandard, &ua));
    lib.SetRoots({root, usb});
    {
        json::Value e = json::Value::MakeObject();
        e.Set("p", json::ToUtf8(usbSong));
        e.Set("s", 64);
        e.Set("m", library::FileTimeToUnixMs((static_cast<uint64_t>(ua.ftLastWriteTime.dwHighDateTime) << 32) |
                                             ua.ftLastWriteTime.dwLowDateTime));
        e.Set("t", "Away");
        json::Value items = json::Value::MakeArray();
        items.Push(std::move(e));
        json::Value doc = json::Value::MakeObject();
        doc.Set("version", 5);
        doc.Set("items", std::move(items));
        CHECK(json::WriteFileAtomic(dir + L"\\library-cache.json", json::Serialize(doc)));
    }
    const std::wstring unplugged = usb + L"-off";
    CHECK(MoveFileW(usb.c_str(), unplugged.c_str()));
    WriteFile(root + L"\\Album\\03 New.mp3", std::string(64, 'z'));  // read now, so the cache is saved
    snap = ScanAndWait(lib);
    CHECK(snap && snap->stats.metadataFiles >= 1);
    // Meanwhile its files stay in the library from the cache, offline, under the root's name.
    m = snap ? snap->FindItem(usbSong) : nullptr;
    CHECK(m && m->offline && m->title == L"Away" && m->folder == usb);
    CHECK(snap && snap->missingRoots.size() == 1 && snap->missingRoots[0] == usb && snap->Folder(usb));
    CHECK(MoveFileW(unplugged.c_str(), usb.c_str()));
    snap = ScanAndWait(lib);
    m = snap ? snap->FindItem(usbSong) : nullptr;
    CHECK(m && m->title == L"Away" && !m->offline);  // from the cache, not the untagged file
    CHECK(snap && snap->missingRoots.empty());

    // Cloud-only files (OneDrive "free up space") are never read; they keep their cached tags and
    // their first-seen time across scans that save the cache.
    const std::wstring cloud = MakeDir(L"lib-cache-cloud");
    const std::wstring tagged = cloud + L"\\Tagged.mp3", fresh = cloud + L"\\Fresh.mp3";
    WriteFile(tagged, std::string(64, 'c'));
    WriteFile(fresh, std::string(64, 'f'));
    WIN32_FILE_ATTRIBUTE_DATA ca{};
    CHECK(GetFileAttributesExW(tagged.c_str(), GetFileExInfoStandard, &ca));
    {
        json::Value e = json::Value::MakeObject();
        e.Set("p", json::ToUtf8(tagged));
        e.Set("s", 64);
        e.Set("m", library::FileTimeToUnixMs((static_cast<uint64_t>(ca.ftLastWriteTime.dwHighDateTime) << 32) |
                                             ca.ftLastWriteTime.dwLowDateTime));
        e.Set("t", "Cloud");
        e.Set("f", 1000);
        json::Value items = json::Value::MakeArray();
        items.Push(std::move(e));
        json::Value doc = json::Value::MakeObject();
        doc.Set("version", 5);
        doc.Set("items", std::move(items));
        CHECK(json::WriteFileAtomic(dir + L"\\library-cache.json", json::Serialize(doc)));
    }
    CHECK(SetFileAttributesW(tagged.c_str(), FILE_ATTRIBUTE_OFFLINE));
    CHECK(SetFileAttributesW(fresh.c_str(), FILE_ATTRIBUTE_OFFLINE));
    lib.SetRoots({cloud});
    WriteFile(cloud + L"\\Local.mp3", std::string(64, 'l'));  // read now, so the cache is saved
    snap = ScanAndWait(lib);
    m = snap ? snap->FindItem(tagged) : nullptr;
    CHECK(m && m->offline && m->title == L"Cloud" && m->firstSeen == 1000);
    const library::MediaItem* f = snap ? snap->FindItem(fresh) : nullptr;
    const int64_t freshSeen = f ? f->firstSeen : -1;
    CHECK(f && f->offline && freshSeen > 1000);  // found now in a known folder: new
    Sleep(20);
    snap = ScanAndWait(lib);
    m = snap ? snap->FindItem(tagged) : nullptr;
    CHECK(m && m->title == L"Cloud" && m->firstSeen == 1000);
    f = snap ? snap->FindItem(fresh) : nullptr;
    CHECK(f && f->firstSeen == freshSeen);  // not "new" again on every scan
    // Back on the disk: the file is read then.
    CHECK(SetFileAttributesW(fresh.c_str(), FILE_ATTRIBUTE_NORMAL));
    snap = ScanAndWait(lib);
    f = snap ? snap->FindItem(fresh) : nullptr;
    CHECK(f && !f->offline && f->firstSeen == freshSeen && f->damaged);  // 64 bytes of "f": read, no MP3
    SetFileAttributesW(tagged.c_str(), FILE_ATTRIBUTE_NORMAL);
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

// --- Tag reader ---------------------------------------------------------------------------------

namespace tagbytes {

std::string Be16(uint32_t v) { return {static_cast<char>(v >> 8), static_cast<char>(v)}; }
std::string Be32(uint32_t v) {
    return {static_cast<char>(v >> 24), static_cast<char>(v >> 16), static_cast<char>(v >> 8), static_cast<char>(v)};
}
std::string Le16(uint32_t v) { return {static_cast<char>(v), static_cast<char>(v >> 8)}; }
std::string Le32(uint32_t v) {
    return {static_cast<char>(v), static_cast<char>(v >> 8), static_cast<char>(v >> 16), static_cast<char>(v >> 24)};
}
std::string Le64(uint64_t v) { return Le32(static_cast<uint32_t>(v)) + Le32(static_cast<uint32_t>(v >> 32)); }
std::string Syncsafe(uint32_t v) {
    return {static_cast<char>((v >> 21) & 0x7F), static_cast<char>((v >> 14) & 0x7F), static_cast<char>((v >> 7) & 0x7F),
            static_cast<char>(v & 0x7F)};
}
std::string Utf16Le(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += Le16(c);
    return s;
}

std::string Frame(const char* id, const std::string& body, int major = 3, bool plainSize = false) {
    if (major == 2) return std::string(id, 3) + Be32(static_cast<uint32_t>(body.size())).substr(1) + body;
    const std::string size = major == 4 && !plainSize ? Syncsafe(static_cast<uint32_t>(body.size())) : Be32(static_cast<uint32_t>(body.size()));
    return std::string(id, 4) + size + std::string(2, '\0') + body;
}
std::string Tag(int major, const std::string& frames, uint8_t flags = 0) {
    const std::string body = frames + std::string(32, '\0');
    return std::string("ID3") + static_cast<char>(major) + '\0' + static_cast<char>(flags) +
           Syncsafe(static_cast<uint32_t>(body.size())) + body;
}
std::string Text(int enc, const std::string& bytes) { return static_cast<char>(enc) + bytes; }

// MPEG-1 Layer III, 128 kbit/s, 44.1 kHz, stereo: 417-byte frames. The first one is a LAME
// "Info" frame (frame count, encoder delay 576 and padding 1000) when `info` is set.
std::string Mpeg(int frames, bool info) {
    std::string out;
    for (int i = 0; i < frames + (info ? 1 : 0); ++i) {
        std::string f = std::string("\xFF\xFB\x90\x00", 4) + std::string(413, '\0');
        if (info && i == 0) {
            std::string x = "Info" + Be32(1) + Be32(static_cast<uint32_t>(frames));
            x += "LAME3.100" + std::string(12, '\0');
            x += std::string{static_cast<char>(576 >> 4), static_cast<char>(((576 & 0xF) << 4) | (1000 >> 8)), static_cast<char>(1000 & 0xFF)};
            f.replace(36, x.size(), x);
        }
        out += f;
    }
    return out;
}

std::string V1(const std::string& title, const std::string& artist, const std::string& album, const std::string& year,
               int track, int genre) {
    auto field = [](std::string s, size_t n) {
        s.resize(n, '\0');
        return s;
    };
    return "TAG" + field(title, 30) + field(artist, 30) + field(album, 30) + field(year, 4) + std::string(28, '\0') + '\0' +
           static_cast<char>(track) + static_cast<char>(genre);
}

std::string Ape(const std::vector<std::pair<std::string, std::string>>& items) {
    std::string body;
    for (const auto& [k, v] : items) body += Le32(static_cast<uint32_t>(v.size())) + Le32(0) + k + '\0' + v;
    auto header = [&](uint32_t flags) {
        return "APETAGEX" + Le32(2000) + Le32(static_cast<uint32_t>(body.size() + 32)) + Le32(static_cast<uint32_t>(items.size())) +
               Le32(flags) + std::string(8, '\0');
    };
    return header(0xA0000000u) + body + header(0x80000000u);
}

std::string Comments(const std::vector<std::string>& entries) {
    std::string s = Le32(6) + "Lumen!" + Le32(static_cast<uint32_t>(entries.size()));
    for (const std::string& e : entries) s += Le32(static_cast<uint32_t>(e.size())) + e;
    return s;
}

std::string FlacPicture(const std::string& image) {
    return Be32(3) + Be32(10) + "image/jpeg" + Be32(0) + Be32(1) + Be32(1) + Be32(24) + Be32(0) +
           Be32(static_cast<uint32_t>(image.size())) + image;
}

std::string Flac(const std::vector<std::string>& comments, const std::string& picture) {
    std::string info = std::string(10, '\0');
    const uint64_t v = (uint64_t{44100} << 44) | (uint64_t{1} << 41) | (uint64_t{15} << 36) | uint64_t{44100 * 3};
    for (int i = 7; i >= 0; --i) info += static_cast<char>(v >> (i * 8));
    info += std::string(16, '\0');  // MD5
    auto block = [](int type, bool last, const std::string& body) {
        return static_cast<char>(type | (last ? 0x80 : 0)) + Be32(static_cast<uint32_t>(body.size())).substr(1) + body;
    };
    const std::string vc = Comments(comments);
    return "fLaC" + block(0, false, info) + block(4, picture.empty(), vc) +
           (picture.empty() ? std::string() : block(6, true, FlacPicture(picture))) + std::string(64, '\0');
}

std::string OggPage(uint8_t type, uint64_t granule, uint32_t seq, const std::string& packet) {
    std::string lacing;
    size_t n = packet.size();
    while (n >= 255) lacing += '\xFF', n -= 255;
    lacing += static_cast<char>(n);
    return "OggS" + std::string(1, '\0') + static_cast<char>(type) + Le64(granule) + Le32(0x1234) + Le32(seq) + Le32(0) +
           static_cast<char>(lacing.size()) + lacing + packet;
}

std::string Opus(const std::vector<std::string>& comments) {
    const std::string head = "OpusHead" + std::string(1, '\1') + '\2' + Le16(312) + Le32(48000) + Le16(0) + '\0';
    return OggPage(2, 0, 0, head) + OggPage(0, 0, 1, "OpusTags" + Comments(comments)) +
           OggPage(0, 48000 * 2, 2, std::string(300, 'a')) + OggPage(4, 48000 * 5 + 312, 3, std::string(300, 'b'));
}

std::string Atom(const char* type, const std::string& body) { return Be32(static_cast<uint32_t>(8 + body.size())) + type + body; }
std::string Item(const char* key, uint32_t type, const std::string& value) {
    return Atom(key, Atom("data", Be32(type) + Be32(0) + value));
}
std::string Mp4(const std::string& items, bool withMoov = true) {
    std::string mvhd = std::string(4, '\0') + Be32(0) + Be32(0) + Be32(1000) + Be32(7500) + std::string(80, '\0');
    std::string hdlr = Atom("hdlr", std::string(8, '\0') + "mdirappl" + std::string(9, '\0'));
    std::string meta = Atom("meta", std::string(4, '\0') + hdlr + Atom("ilst", items));
    std::string moov = Atom("moov", Atom("mvhd", mvhd) + Atom("udta", meta));
    return Atom("ftyp", "M4A " + Be32(0) + "M4A isom") + (withMoov ? moov : std::string()) + Atom("mdat", std::string(256, 'm'));
}

std::string Wav(const std::string& extraChunks) {
    std::string fmt = Le16(1) + Le16(2) + Le32(44100) + Le32(176400) + Le16(4) + Le16(16);
    std::string body = "WAVE" + std::string("fmt ") + Le32(16) + fmt + "data" + Le32(17640) + std::string(17640, '\0') + extraChunks;
    return "RIFF" + Le32(static_cast<uint32_t>(body.size())) + body;
}

std::string Aiff(const std::string& id3) {
    // 44100 Hz as an 80-bit extended float: exponent 16398, mantissa 0xAC44 << 48.
    std::string rate = std::string("\x40\x0E\xAC\x44", 4) + std::string(6, '\0');
    std::string comm = Be16(2) + Be32(88200) + Be16(16) + rate;
    std::string body = "AIFF" + std::string("COMM") + Be32(18) + comm + "SSND" + Be32(16) + std::string(16, '\0') + "ID3 " +
                       Be32(static_cast<uint32_t>(id3.size())) + id3 + (id3.size() % 2 ? std::string(1, '\0') : std::string());
    return "FORM" + Be32(static_cast<uint32_t>(body.size())) + body;
}

}  // namespace tagbytes

bool ParseBytes(const std::string& bytes, library::TagInfo* t, bool all = true) {
    library::MemorySource src(bytes.data(), bytes.size());
    library::TagOptions o;
    o.lyrics = all;
    o.cover = all;
    o.codePage = 1251;
    return library::ReadTags(src, t, o);
}

// One sample of every format, with what the reader must find (the fuzz test mutates them).
std::vector<std::string> TagSamples() {
    using namespace tagbytes;
    const std::string jpeg("\xFF\xD8\xFF\xE0 fake jpeg \xFF\x00\xFF\xD9", 19);
    std::vector<std::string> s;
    // 0: ID3v2.3, every text encoding, TXXX album artist, cover, USLT, plus ID3v1 below it.
    s.push_back(Tag(3, Frame("TIT2", Text(1, "\xFF\xFE" + Utf16Le(L"Песня"))) + Frame("TPE1", Text(0, "\xCA\xE8\xED\xEE")) +
                           Frame("TALB", Text(3, "Альбом")) + Frame("TRCK", Text(0, "3/12")) + Frame("TPOS", Text(0, "2/3")) +
                           Frame("TCON", Text(0, "(17)")) + Frame("TYER", Text(0, "1998")) +
                           Frame("TXXX", Text(0, std::string("ALBUM ARTIST\0Various", 20))) +
                           Frame("APIC", Text(0, std::string("image/jpeg\0\x03\0", 13) + jpeg)) +
                           Frame("USLT", Text(3, std::string("eng\0", 4) + "[00:01.00]one\n[00:02.00]two"), 3)) +
                Mpeg(40, true) + V1("v1 title", "v1 artist", "v1 album", "1990", 7, 0));
    // 1: ID3v2.4 with NUL-separated values, TPE2, TDRC, SYLT in milliseconds.
    std::string sylt = Text(3, std::string("eng\x02\x01", 5) + '\0');
    const std::pair<uint32_t, const char*> kLines[] = {{1000, "first"}, {2500, "second"}, {4000, "third"}};
    for (const auto& [ms, line] : kLines) sylt += std::string(line) + '\0' + Be32(ms);
    s.push_back(Tag(4, Frame("TIT2", Text(3, "Title 2.4"), 4) + Frame("TPE1", Text(3, std::string("One\0Two", 7)), 4) +
                           Frame("TPE2", Text(2, std::string("\0A\0A", 4)), 4) + Frame("TDRC", Text(3, "2021-12-31"), 4) +
                           Frame("TCON", Text(3, "13"), 4) + Frame("SYLT", sylt, 4)) +
                Mpeg(20, false));
    // 2: ID3v2.2 (3-character ids, PIC).
    s.push_back(Tag(2, Frame("TT2", Text(0, "Old"), 2) + Frame("TP1", Text(0, "Tagger"), 2) + Frame("TRK", Text(0, "5"), 2) +
                           Frame("PIC", Text(0, "JPG" + std::string("\x03\0", 2) + jpeg), 2)) +
                Mpeg(10, false));
    // 3: legacy CP-1251 ID3v1 only, with an APE tag in front of it (APE wins).
    s.push_back(Mpeg(10, false) + Ape({{"Title", "APE title"}, {"Album Artist", "APE AA"}, {"Track", "4/9"}}) +
                V1("\xC7\xE2\xE5\xE7\xE4\xE0", "\xCA\xE8\xED\xEE", "x", "1989", 8, 17));
    // 4: iTunes-style 2.4 with plain 32-bit frame sizes over 127 bytes.
    s.push_back(Tag(4, Frame("TIT2", Text(3, std::string(200, 'L')), 4, true) + Frame("TPE1", Text(3, "iTunes"), 4, true)) +
                Mpeg(10, false));
    // 5: whole-tag unsynchronisation (2.3).
    {
        std::string frames = Frame("TIT2", Text(0, "Unsync")) + Frame("APIC", Text(0, std::string("image/jpeg\0\x03\0", 13) + jpeg));
        std::string uns;
        for (size_t i = 0; i < frames.size(); ++i) {
            uns += frames[i];
            if (static_cast<uint8_t>(frames[i]) == 0xFF) uns += '\0';
        }
        s.push_back(Tag(3, uns, 0x80) + Mpeg(10, false));
    }
    // 6: FLAC with comments and a picture.
    s.push_back(Flac({"TITLE=Flac title", "ARTIST=A", "ARTIST=B", "ALBUMARTIST=Flac AA", "TRACKNUMBER=2", "TRACKTOTAL=10",
                      "DISCNUMBER=1/2", "DATE=2004-01-01", "GENRE=Jazz", "LYRICS=plain words"},
                     jpeg));
    // 7: Ogg Opus, 5 s after pre-skip.
    s.push_back(Opus({"title=Opus title", "album=Opus album", "tracknumber=7"}));
    // 8: MP4 ilst.
    s.push_back(Mp4(Item("\xA9nam", 1, "Mp4 title") + Item("\xA9" "ART", 1, "Mp4 artist") + Item("aART", 1, "Mp4 AA") +
                    Item("\xA9" "alb", 1, "Mp4 album") + Item("trkn", 0, std::string("\0\0\0\x06\0\x0B\0\0", 8)) +
                    Item("disk", 0, std::string("\0\0\0\x02\0\x03", 6)) + Item("\xA9" "day", 1, "2019-04-01") +
                    Item("gnre", 0, std::string("\0\x10", 2)) + Item("\xA9lyr", 1, "mp4 words") + Item("covr", 13, jpeg)));
    // 9: MP4 without its index.
    s.push_back(Mp4({}, false));
    // 10: WAV with LIST INFO and an id3 chunk (ID3 wins), 0.1 s.
    {
        const std::string info = "INFO" + std::string("INAM") + Le32(10) + "Info name" + '\0' + "IART" + Le32(8) + "Info ar" + '\0';
        const std::string id3 = Tag(3, Frame("TIT2", Text(0, "Id3 name")));
        s.push_back(Wav("LIST" + Le32(static_cast<uint32_t>(info.size())) + info + "id3 " + Le32(static_cast<uint32_t>(id3.size())) + id3));
    }
    // 11: AIFF, 2 s, ID3 chunk.
    s.push_back(Aiff(Tag(3, Frame("TIT2", Text(0, "Aiff title")))));
    // 12: Matroska: EBML header, a Segment of unknown size, Void, Info (scale 1 ms, 12 345 ms), Cluster.
    {
        double ms = 12345.0;
        uint64_t bits;
        memcpy(&bits, &ms, 8);
        std::string dur;
        for (int i = 7; i >= 0; --i) dur += static_cast<char>(bits >> (i * 8));
        const std::string info = std::string("\x2A\xD7\xB1\x83\x0F\x42\x40", 7) + std::string("\x44\x89\x88", 3) + dur;
        s.push_back(std::string("\x1A\x45\xDF\xA3\x84\x42\x82\x81w", 9) + std::string("\x18\x53\x80\x67\x01\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 12) +
                    std::string("\xEC\x83\0\0\0", 5) + std::string("\x15\x49\xA9\x66", 4) + static_cast<char>(0x80 | info.size()) + info +
                    std::string("\x1F\x43\xB6\x75\x84", 5) + std::string(4, 'c'));
    }
    return s;
}

void TestTagReader() {
    const auto s = TagSamples();
    library::TagInfo t;

    CHECK(ParseBytes(s[0], &t) && t.format == library::TagFormat::Mp3);
    CHECK(t.title == L"Песня" && t.artist == L"Кино" && t.album == L"Альбом");
    CHECK(t.track == 3 && t.trackTotal == 12 && t.disc == 2 && t.discTotal == 3);
    CHECK(t.genre == L"Rock" && t.year == 1998 && t.albumArtist == L"Various");
    CHECK(t.hasCover && t.coverType == 3 && t.coverMime == "image/jpeg" && t.cover.size() == 19);
    CHECK(t.lyrics == L"[00:01.00]one\n[00:02.00]two");
    // 40 frames of 1152 samples less LAME's delay and padding.
    CHECK_MSG(std::fabs(t.duration - (40.0 * 1152 - 1576) / 44100) < 1e-6, "d=%f", t.duration);
    // Without options: no lyrics, no cover bytes, but the cover is known.
    library::TagInfo bare;
    CHECK(ParseBytes(s[0], &bare, false) && bare.lyrics.empty() && bare.cover.empty() && bare.hasCover && bare.title == L"Песня");

    CHECK(ParseBytes(s[1], &t) && t.title == L"Title 2.4" && t.artist == L"One; Two" && t.albumArtist == L"AA");
    CHECK(t.year == 2021 && t.genre == L"Pop");
    CHECK(t.syncedLyrics.size() == 3);
    if (t.syncedLyrics.size() == 3) CHECK(t.syncedLyrics[1].time == 2.5 && t.syncedLyrics[1].text == L"second");
    CHECK_MSG(std::fabs(t.duration - 20 * 417 * 8 / 128000.0) < 1e-6, "cbr d=%f", t.duration);  // CBR estimate

    CHECK(ParseBytes(s[2], &t) && t.title == L"Old" && t.artist == L"Tagger" && t.track == 5 && t.hasCover && t.cover.size() == 19);
    CHECK(ParseBytes(s[3], &t) && t.title == L"APE title" && t.albumArtist == L"APE AA" && t.track == 4 && t.trackTotal == 9);
    CHECK(t.artist == L"Кино" && t.year == 1989 && t.genre == L"Rock");  // the rest from CP-1251 ID3v1
    CHECK(ParseBytes(s[4], &t) && t.title == std::wstring(200, L'L') && t.artist == L"iTunes");
    CHECK(ParseBytes(s[5], &t) && t.title == L"Unsync" && t.cover.size() == 19 && t.cover[0] == 0xFF && t.cover[1] == 0xD8);

    CHECK(ParseBytes(s[6], &t) && t.format == library::TagFormat::Flac && t.duration == 3.0);
    CHECK(t.title == L"Flac title" && t.artist == L"A; B" && t.albumArtist == L"Flac AA" && t.track == 2 && t.trackTotal == 10);
    CHECK(t.disc == 1 && t.discTotal == 2 && t.year == 2004 && t.genre == L"Jazz" && t.lyrics == L"plain words");
    CHECK(t.hasCover && t.cover.size() == 19 && t.coverMime == "image/jpeg");

    CHECK(ParseBytes(s[7], &t) && t.format == library::TagFormat::Opus && std::fabs(t.duration - 5.0) < 1e-9);
    CHECK(t.title == L"Opus title" && t.album == L"Opus album" && t.track == 7);

    CHECK(ParseBytes(s[8], &t) && t.format == library::TagFormat::Mp4 && !t.damaged && t.duration == 7.5);
    CHECK(t.title == L"Mp4 title" && t.artist == L"Mp4 artist" && t.albumArtist == L"Mp4 AA" && t.album == L"Mp4 album");
    CHECK(t.track == 6 && t.trackTotal == 11 && t.disc == 2 && t.discTotal == 3 && t.year == 2019 && t.genre == L"Rap");
    CHECK(t.lyrics == L"mp4 words" && t.hasCover && t.cover.size() == 19);
    CHECK(ParseBytes(s[9], &t) && t.damaged);
    // The index cut off by an interrupted download.
    CHECK(ParseBytes(s[8].substr(0, s[8].size() - 300), &t) && t.damaged);

    CHECK(ParseBytes(s[10], &t) && t.format == library::TagFormat::Wav && std::fabs(t.duration - 0.1) < 1e-9);
    CHECK(t.title == L"Id3 name" && t.artist == L"Info ar");
    CHECK(ParseBytes(s[11], &t) && t.format == library::TagFormat::Aiff && std::fabs(t.duration - 2.0) < 1e-9 && t.title == L"Aiff title");

    CHECK(ParseBytes(s[12], &t) && t.format == library::TagFormat::Matroska && std::fabs(t.duration - 12.345) < 1e-9);
    // FLAC metadata cut off before the audio: no length (the property system decides).
    CHECK(ParseBytes(s[6].substr(0, 60), &t) && t.format == library::TagFormat::Flac && t.duration == 0);

    // Not a tagged format: the property system takes over.
    CHECK(!ParseBytes("\x1A\x45\xDF matroska", &t) && t.format == library::TagFormat::Unknown);
    CHECK(!ParseBytes(std::string("RIFF\x10\0\0\0AVI LIST", 16), &t) && t.format == library::TagFormat::Unknown);
    CHECK(!ParseBytes("", &t));
    CHECK(!ParseBytes("hello world, plain text", &t));

    // Legacy text and genres.
    CHECK(library::DecodeLegacyText(reinterpret_cast<const uint8_t*>("\xCA\xE8\xED\xEE"), 4, 1251) == L"Кино");
    CHECK(library::DecodeLegacyText(reinterpret_cast<const uint8_t*>("\xD0\x9A\xD0\xB8"), 4, 1251) == L"Ки");  // UTF-8 wins
    CHECK(library::DecodeLegacyText(reinterpret_cast<const uint8_t*>("Caf\xE9"), 4, 1252) == L"Café");
    CHECK(library::Id3GenreName(0) == L"Blues" && library::Id3GenreName(191) == L"Psybient" && library::Id3GenreName(192).empty());
}

// Bytes of a file that is far larger than anything a parse may read, made up on the fly.
class EndlessSource : public library::ByteSource {
public:
    EndlessSource(std::string prefix, uint32_t seed) : prefix_(std::move(prefix)), seed_(seed) {}
    uint64_t Size() const override { return 1ull << 40; }
    size_t Read(uint64_t offset, void* buf, size_t n) override {
        ++reads;
        bytes += n;
        auto* out = static_cast<uint8_t*>(buf);
        for (size_t i = 0; i < n; ++i) {
            const uint64_t at = offset + i;
            uint64_t x = (at ^ seed_) * 0x9E3779B97F4A7C15ull;
            out[i] = at < prefix_.size() ? static_cast<uint8_t>(prefix_[static_cast<size_t>(at)]) : static_cast<uint8_t>(x >> 56);
        }
        return n;
    }
    uint64_t reads = 0, bytes = 0;

private:
    std::string prefix_;
    uint32_t seed_;
};

// Truncated, mutated and garbage files: every parse ends, without a crash and within its budget.
void TestTagReaderFuzz() {
    const auto samples = TagSamples();
    uint64_t rng = 12345;
    auto next = [&rng] {
        rng = rng * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(rng >> 33);
    };
    const auto start = std::chrono::steady_clock::now();
    size_t parses = 0;
    library::TagInfo t;
    for (const std::string& seed : samples) {
        const size_t step = std::max<size_t>(1, seed.size() / 4000);
        for (size_t n = 0; n <= seed.size(); n += step) {
            ParseBytes(seed.substr(0, n), &t);
            ++parses;
        }
        for (int m = 0; m < 4000; ++m) {
            std::string b = seed;
            const int edits = 1 + static_cast<int>(next() % 6);
            for (int e = 0; e < edits && !b.empty(); ++e) {
                const size_t at = next() % b.size();
                switch (next() % 4) {
                    case 0: b[at] = static_cast<char>(next()); break;
                    case 1: b.replace(at, std::min<size_t>(4, b.size() - at), std::string(4, '\xFF')); break;
                    case 2: b.replace(at, std::min<size_t>(4, b.size() - at), std::string("\x7F\xFF\xFF\xFF", 4)); break;
                    default: b.replace(at, std::min<size_t>(4, b.size() - at), std::string(4, '\0')); break;
                }
            }
            b.resize(b.size() - std::min<size_t>(b.size(), next() % 3 == 0 ? next() % 64 : 0));
            ParseBytes(b, &t);
            ++parses;
        }
    }
    for (int i = 0; i < 3000; ++i) {  // garbage behind each magic
        static const std::string kMagic[] = {std::string("ID3\x03\0\0", 6), std::string("ID3\x04\0\0", 6), "fLaC", "OggS",
                                             std::string("RIFF\0\0\0\0WAVE", 12), std::string("FORM\0\0\0\0" "AIFF", 12),
                                             std::string("\0\0\0\x18" "ftypM4A ", 12), "MAC ", "wvpk",
                                             std::string("\xFF\xFB\x90\x00", 4), "\x1A\x45\xDF\xA3"};
        std::string b = kMagic[i % std::size(kMagic)];
        const size_t n = next() % 3000;
        for (size_t k = 0; k < n; ++k) b += static_cast<char>(next());
        ParseBytes(b, &t);
        ++parses;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  %zu parses in %.0f ms\n", parses, ms);
    CHECK(ms < 60000);

    // A terabyte of garbage behind each magic: the reads stay bounded (the budget is 4096 reads
    // and 48 MB; the whole "file" is never read).
    const std::string kEndless[] = {std::string("ID3\x03\0\0\x7F\x7F\x7F\x7F", 10), "fLaC", "OggS", "RIFF\xFF\xFF\xFF\xFFWAVE",
                                    std::string("\0\0\0\x10" "ftypM4A ", 12), "FORM\xFF\xFF\xFF\xFF" "AIFF", "MAC ", "wvpk",
                                    std::string("\x1A\x45\xDF\xA3\x80\x18\x53\x80\x67\x01\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 17)};
    for (const std::string& magic : kEndless) {
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            EndlessSource src(magic, seed);
            library::TagOptions o;
            o.cover = o.lyrics = true;
            library::ReadTags(src, &t, o);
            CHECK_MSG(src.reads <= 4200 && src.bytes <= (64ull << 20), "%s: %llu reads, %llu bytes", magic.c_str() + (magic[0] ? 0 : 4),
                      static_cast<unsigned long long>(src.reads), static_cast<unsigned long long>(src.bytes));
        }
    }
}

// The scan uses the tag reader: album artists, gapless MP3 length, and the "new" badge times.
void TestLibraryTagReader() {
    const std::wstring dir = FreshDataDir(L"library-tags");
    const std::wstring root = MakeDir(L"lib-tags");
    const auto s = TagSamples();
    WriteFile(root + L"\\Album\\01 id3.mp3", s[0]);
    WriteFile(root + L"\\Album\\02 flac.flac", s[6]);
    WriteFile(root + L"\\Album\\03 mp4.m4a", s[8]);
    WriteFile(root + L"\\Album\\04 cut.m4a", s[9]);
    WriteFile(root + L"\\Clips\\clip.mp4", s[8]);  // a video: duration only
    library::MediaLibrary lib;
    CHECK(lib.Load());
    lib.SetRoots({root});
    auto snap = ScanAndWait(lib);
    if (!snap) return;
    const library::MediaItem* mp3 = snap->FindItem(root + L"\\Album\\01 id3.mp3");
    CHECK(mp3 && mp3->title == L"Песня" && mp3->albumArtist == L"Various" && mp3->disc == 2 && mp3->track == 3);
    CHECK(mp3 && std::fabs(mp3->duration - (40.0 * 1152 - 1576) / 44100) < 1e-6);
    const library::MediaItem* flac = snap->FindItem(root + L"\\Album\\02 flac.flac");
    CHECK(flac && flac->albumArtist == L"Flac AA" && flac->duration == 3.0 && flac->genre == L"Jazz");
    const library::MediaItem* m4a = snap->FindItem(root + L"\\Album\\03 mp4.m4a");
    CHECK(m4a && m4a->title == L"Mp4 title" && m4a->duration == 7.5 && !m4a->damaged);
    const library::MediaItem* cut = snap->FindItem(root + L"\\Album\\04 cut.m4a");
    CHECK(cut && cut->damaged);
    const library::MediaItem* clip = snap->FindItem(root + L"\\Clips\\clip.mp4");
    CHECK(clip && clip->duration == 7.5 && clip->title.empty());
    // The first scan of a folder: nothing is new.
    for (const library::MediaItem& it : snap->items) CHECK(it.firstSeen == 0);
    CHECK(snap->newestFirstSeen == 0);

    // The cache keeps album artists (version 5); a file added later is new until it is seen.
    std::string cache;
    CHECK(json::ReadFileUtf8(dir + L"\\library-cache.json", &cache));
    CHECK(cache.find("\"version\":5") != std::string::npos && cache.find("\"aa\":\"Flac AA\"") != std::string::npos);
    Sleep(20);  // the new file's first-seen time must be later than the session start
    const int64_t before = library::UnixMsNow();
    WriteFile(root + L"\\Album\\05 new.flac", s[6]);
    snap = ScanAndWait(lib);
    const library::MediaItem* fresh = snap ? snap->FindItem(root + L"\\Album\\05 new.flac") : nullptr;
    CHECK(fresh && fresh->firstSeen >= before && lib.IsNew(*fresh));
    CHECK(snap && snap->FindItem(root + L"\\Album\\02 flac.flac")->firstSeen == 0);
    CHECK(snap && snap->newestFirstSeen == fresh->firstSeen);
    snap = ScanAndWait(lib);  // still new on a rescan: the time comes from the cache
    fresh = snap ? snap->FindItem(root + L"\\Album\\05 new.flac") : nullptr;
    CHECK(fresh && fresh->firstSeen >= before && lib.IsNew(*fresh));
    lib.MarkSeen();  // the user looked at the library
    {
        library::MediaLibrary next;  // the next session
        CHECK(next.Load() && next.NewSince() >= fresh->firstSeen);
        CHECK(!next.IsNew(*fresh));
        CHECK(next.Roots().size() == 1);
    }
    CHECK(!lib.Progress().scanning);
}

// Embedded lyrics for the music view: SYLT is synced, USLT holding LRC text stays synced, a .lrc
// next to the file wins.
void TestEmbeddedLyrics() {
    const std::wstring dir = MakeDir(L"lyrics-embedded");
    const auto s = TagSamples();
    WriteFile(dir + L"\\sylt.mp3", s[1]);
    WriteFile(dir + L"\\uslt.mp3", s[0]);
    WriteFile(dir + L"\\plain.flac", s[6]);
    app::Lyrics l = app::LoadEmbeddedLyrics(dir + L"\\sylt.mp3");
    CHECK(l.synced && l.lines.size() == 3 && l.lines[0].text == L"first" && l.lines[2].time == 4.0);
    l = app::LoadLyricsFor(dir + L"\\uslt.mp3");
    CHECK(l.synced && l.lines.size() == 2 && l.lines[1].text == L"two");
    l = app::LoadLyricsFor(dir + L"\\plain.flac");
    CHECK(!l.synced && l.lines.size() == 1 && l.lines[0].text == L"plain words");
    WriteFile(dir + L"\\uslt.lrc", "[00:05.00]from the lrc");
    l = app::LoadLyricsFor(dir + L"\\uslt.mp3");
    CHECK(l.synced && l.lines.size() == 1 && l.lines[0].text == L"from the lrc");
    CHECK(app::LoadLyricsFor(L"https://example.com/a.mp3").Empty());
    CHECK(app::LoadEmbeddedLyrics(dir + L"\\missing.mp3").Empty());
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

    // A cover the shell can't show (Opus METADATA_BLOCK_PICTURE) comes from the tag reader.
    {
        std::string image;  // small: one Ogg page holds the comment packet
        const std::wstring smallBmp = dir + L"\\small.bmp";
        WriteBmp(smallBmp, 32, 16);
        CHECK(json::ReadFileUtf8(smallBmp, &image) && image.size() > 54);
        const std::string block = tagbytes::Be32(3) + tagbytes::Be32(9) + "image/bmp" + tagbytes::Be32(0) + std::string(16, '\0') +
                                  tagbytes::Be32(static_cast<uint32_t>(image.size())) + image;
        static const char* const kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string b64;
        for (size_t i = 0; i < block.size(); i += 3) {
            uint32_t v = static_cast<uint8_t>(block[i]) << 16;
            if (i + 1 < block.size()) v |= static_cast<uint8_t>(block[i + 1]) << 8;
            if (i + 2 < block.size()) v |= static_cast<uint8_t>(block[i + 2]);
            b64 += kB64[(v >> 18) & 63];
            b64 += kB64[(v >> 12) & 63];
            b64 += i + 1 < block.size() ? kB64[(v >> 6) & 63] : '=';
            b64 += i + 2 < block.size() ? kB64[v & 63] : '=';
        }
        const std::wstring opus = dir + L"\\cover.opus";
        WriteFile(opus, tagbytes::Opus({"title=With cover", "METADATA_BLOCK_PICTURE=" + b64}));
        auto cover = library::ExtractThumbnail(opus, 64);
        CHECK_MSG(cover && cover->width == 32 && cover->height == 16, "cover %ux%u", cover ? cover->width : 0, cover ? cover->height : 0);
        auto smaller = library::ExtractThumbnail(opus, 16);  // scaled to fit
        CHECK(smaller && smaller->width == 16 && smaller->height == 8);
    }
    if (SUCCEEDED(com)) CoUninitialize();
}

void TestFolderOrder() {
    using library::FileEntry;
    const std::vector<FileEntry> files = {{L"C:/m/10.mp3", 300, 1, 5}, {L"C:/m/2.mp3", 100, 3, 9}, {L"C:/m/1.mp3", 200, 2, 9}};
    auto order = [&](std::string_view key) {
        std::vector<FileEntry> v = files;
        library::SortFileEntries(v, key);
        std::wstring out;
        for (const FileEntry& f : v) out += f.path.substr(5, f.path.size() - 9) + L";";
        return out;
    };
    CHECK(order("name") == L"1;2;10;");
    CHECK(order("name:desc") == L"10;2;1;");
    CHECK(order("date") == L"2;1;10;");
    CHECK(order("date:desc") == L"10;1;2;");
    CHECK(order("created") == L"10;1;2;");
    CHECK(order("size:desc") == L"2;1;10;");  // a reversed sort reverses ties too, as Explorer does
    CHECK(order("size") == L"10;1;2;");
    CHECK(order("bogus") == L"1;2;10;");
}

void TestLyricsFill() {
    const app::Lyrics l = app::ParseLyrics(L"[00:10.00]0123456789\n[00:11.00]next line\n[01:00.00]last");
    CHECK(l.SungFraction(0, 9) == 0);
    CHECK(std::abs(l.SungFraction(0, 10.5) - 0.5) < 1e-9);  // ends at the next line's stamp
    CHECK(l.SungFraction(0, 12) == 1);
    // A long pause after the line: the fill ends after 0.9 s + 0.075 s per character.
    CHECK(std::abs(l.SungFraction(1, 11 + (0.9 + 0.675) / 2) - 0.5) < 1e-9);
    CHECK(l.SungFraction(2, 1000) == 1);  // the last line
    CHECK(l.SungFraction(-1, 10) == 0 && l.SungFraction(3, 10) == 0);
    CHECK(app::ParseLyrics(L"plain words").SungFraction(0, 5) == 0);
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
    CHECK(app::DecodeLyricsFile("\xCF\xF0\xE8", 1251) == L"При");

    // Where the .lrc is looked for, which tags carry lyrics.
    CHECK(app::LyricsPathFor(L"C:\\Music\\A.B\\01 Song.mp3") == L"C:\\Music\\A.B\\01 Song.lrc");
    CHECK(app::LyricsPathFor(L"C:\\Music\\A.B\\noext") == L"C:\\Music\\A.B\\noext.lrc");
    CHECK(app::LyricsPathFor(L"https://example.com/a.mp3").empty());
    CHECK(app::LyricsPathFor(L"C:\\Music\\").empty());
    CHECK(app::IsLyricsTag("LYRICS") && app::IsLyricsTag("lyrics-eng") && app::IsLyricsTag("UNSYNCEDLYRICS"));
    CHECK(!app::IsLyricsTag("lyricist") && !app::IsLyricsTag("lyrics-") && !app::IsLyricsTag("title"));
    CHECK(app::LoadLyricsFile(L"Z:\\definitely\\missing.lrc").Empty());
}

// The play queue and the gapless hand-off to mpv. FakeMpv models what App::SyncNextInPlayer and
// Player::QueueNext do to mpv's playlist: [0] plays, [1] is the appended next file.
void TestPlayQueue() {
    using app::PlayQueue;
    using app::QueueItem;
    auto make = [](int n) {
        std::vector<QueueItem> v;
        for (int i = 0; i < n; ++i) v.push_back({L"C:\\m\\" + std::to_wstring(i) + L".mp4", {}, 0});
        return v;
    };
    struct FakeMpv {
        std::vector<std::wstring> playlist;
        std::wstring Next() const { return playlist.size() > 1 ? playlist[1] : L""; }
    };
    PlayQueue q;
    FakeMpv mpv;
    int queued = -1;
    bool settingCurrent = false;
    auto sync = [&] {
        queued = q.GaplessNextIndex();
        mpv.playlist.resize(std::min<size_t>(mpv.playlist.size(), 1));
        if (queued >= 0) mpv.playlist.push_back(q.Items()[static_cast<size_t>(queued)].path);
    };
    q.changed.Connect([&] {
        if (!settingCurrent) sync();
    });
    auto play = [&](int i) {  // App::PlayIndex
        settingCurrent = true;
        q.SetCurrent(i);
        settingCurrent = false;
        mpv.playlist = {q.CurrentItem()->path};
        sync();
    };
    auto advance = [&] {  // mpv moved on by itself: App::OnAdvanced
        mpv.playlist.erase(mpv.playlist.begin());
        const int i = mpv.playlist.empty() ? -1 : q.IndexOfStarted(mpv.playlist[0], queued);
        settingCurrent = true;
        if (i >= 0) q.SetCurrent(i);
        settingCurrent = false;
        sync();
        return i;
    };
    auto path = [&](int i) { return q.Items()[static_cast<size_t>(i)].path; };

    // Round 4 P-14: two drags of row 5 to row 2; the next file is the new row 2.
    q.Set(make(5), 0);
    play(0);
    CHECK(mpv.Next() == L"C:\\m\\1.mp4");
    q.Move(4, 1);
    q.Move(4, 1);
    CHECK(path(1) == L"C:\\m\\3.mp4" && path(2) == L"C:\\m\\4.mp4" && path(3) == L"C:\\m\\1.mp4");
    CHECK(mpv.Next() == L"C:\\m\\3.mp4");
    CHECK(advance() == 1);
    CHECK(mpv.Next() == L"C:\\m\\4.mp4");

    // P-15: a row dropped onto the playing row, from above (lands after it) and from below (before it).
    q.Set(make(5), 3);
    play(3);
    q.Move(0, 3);
    CHECK(q.Current() == 2 && path(2) == L"C:\\m\\3.mp4");
    CHECK(mpv.Next() == L"C:\\m\\0.mp4");
    q.Move(4, 2);
    CHECK(q.Current() == 3 && path(2) == L"C:\\m\\4.mp4");
    CHECK(mpv.Next() == L"C:\\m\\0.mp4");
    q.Move(q.Current(), 0);  // the playing row itself
    CHECK(q.Current() == 0 && mpv.Next() == L"C:\\m\\1.mp4");

    // Remove and insert re-sync the next file.
    q.Set(make(4), 0);
    play(0);
    q.Remove(1);
    CHECK(mpv.Next() == L"C:\\m\\2.mp4");
    q.InsertNext({{L"C:\\m\\x.mp4", {}, 0}});
    CHECK(path(1) == L"C:\\m\\x.mp4" && mpv.Next() == L"C:\\m\\x.mp4");
    q.Remove(0);  // the playing item: its successor becomes current
    CHECK(q.Current() == 0 && path(0) == L"C:\\m\\x.mp4");
    q.Set(make(3), 2);
    q.Remove(2);  // the last one: nothing after it
    CHECK(q.Current() == 1);
    q.Add({{L"C:\\m\\y.mp4", {}, 0}});
    q.Remove(0);
    CHECK(q.Current() == 0 && path(1) == L"C:\\m\\y.mp4");

    // mpv took the old next entry (1.mp4) just before an edit moved it: found by path, not index.
    q.Set(make(5), 0);
    play(0);
    const int hint = queued;
    q.Move(1, 4);
    CHECK(q.IndexOfStarted(L"C:\\m\\1.mp4", hint) == 4);
    CHECK(q.IndexOfStarted(L"C:\\m\\2.mp4", hint) == 1);
    CHECK(q.IndexOfStarted(L"C:\\m\\gone.mp4", hint) == -1);

    // Repeat: one = no gapless entry (seek back), all = wrap to the first item.
    q.Set(make(3), 2);
    q.SetRepeat(app::Repeat::One);
    CHECK(q.GaplessNextIndex() == -1);
    q.SetRepeat(app::Repeat::All);
    CHECK(q.GaplessNextIndex() == 0);
    q.SetRepeat(app::Repeat::Off);
    CHECK(q.GaplessNextIndex() == -1);

    // Shuffle + Repeat All: Previous on the first item of the play order wraps to its last one,
    // the way Next wraps from the last to the first (audit: it did nothing).
    q.Set(make(4), 1);
    q.SetShuffle(true);  // the current item heads the shuffled order
    CHECK(q.PrevIndex() == -1);
    q.SetRepeat(app::Repeat::All);
    {
        const int prev = q.PrevIndex();
        CHECK(prev >= 0 && prev != q.Current());
        settingCurrent = true;
        q.SetCurrent(prev);
        settingCurrent = false;
        CHECK(q.NextIndex(true) == 1);  // and Next from there comes back round to the start
    }
    q.SetRepeat(app::Repeat::Off);
    q.SetShuffle(false);

    // Shuffled: edits keep the play order of what is queued; "play next" plays next.
    q.Set(make(8), 0);
    q.SetShuffle(true);
    const std::wstring next = path(q.NextIndex(false));
    q.Move(7, 6);
    q.Move(6, 7);
    q.Move(5, 2);
    CHECK(path(q.NextIndex(false)) == next);
    q.InsertNext({{L"C:\\m\\next.mp4", {}, 0}});
    CHECK(path(q.NextIndex(false)) == L"C:\\m\\next.mp4");
    q.Add(make(3));
    q.Remove(static_cast<int>(q.Items().size()) - 1);
    std::set<int> seen;  // the walk visits every item once
    for (int i = q.Current(), k = 0; i >= 0 && k < 32; i = q.NextIndex(false), ++k) {
        CHECK(seen.insert(i).second);
        q.SetCurrent(i);
    }
    CHECK(seen.size() == q.Items().size());
    q.SetShuffle(false);

    // Shuffled Add: what was still to play keeps its order, every new item lands after the
    // current one, and a large add stays linear.
    q.Set(make(6), 2);
    q.SetShuffle(true);
    std::vector<int> before;
    for (int i = q.NextIndex(false), k = 0; i >= 0 && k < 8; ++k) {
        before.push_back(i);
        const int cur = q.Current();
        q.SetCurrent(i);
        i = q.NextIndex(false);
        q.SetCurrent(cur);
    }
    q.Add(make(4000));
    std::vector<int> walk;
    for (int i = q.Current(), k = 0; i >= 0 && k < 5000; i = q.NextIndex(false), ++k) {
        walk.push_back(i);
        q.SetCurrent(i);
    }
    CHECK(walk.size() == q.Items().size());
    std::vector<int> oldOnes;
    for (size_t k = 1; k < walk.size(); ++k)
        if (walk[k] < 6) oldOnes.push_back(walk[k]);
    CHECK(oldOnes == before);
    q.SetShuffle(false);

    // What a played file said about itself sticks to its row.
    q.Set(make(2), 0);
    q.SetInfo(0, L"Tagged", 90);
    q.SetInfo(0, L"", 0);
    CHECK(q.Items()[0].title == L"Tagged" && q.Items()[0].duration == 90);
}

void TestShellText() {
    const std::wstring suffix = L" — Lumen";
    CHECK(FitShellText(L"Short", suffix, 128) == L"Short — Lumen");
    // Exactly full: 127 units + NUL.
    const std::wstring exact(127 - suffix.size(), L'a');
    CHECK(FitShellText(exact, suffix, 128) == exact + suffix);
    // Too long: cut with an ellipsis, the suffix kept, the result fits.
    const std::wstring longTitle(300, L'b');
    std::wstring r = FitShellText(longTitle, suffix, 128);
    CHECK(r.size() == 127);
    CHECK(r.compare(r.size() - suffix.size(), suffix.size(), suffix) == 0);
    CHECK(r[r.size() - suffix.size() - 1] == L'…');
    // An emoji (surrogate pair) at the cut is dropped whole, never split.
    std::wstring emoji(127 - suffix.size() - 2, L'c');
    emoji += L"\U0001F600\U0001F600tail";
    r = FitShellText(emoji, suffix, 128);
    CHECK(r.size() <= 127);
    for (size_t i = 0; i < r.size(); ++i) {
        const bool high = r[i] >= 0xD800 && r[i] <= 0xDBFF;
        if (high) CHECK(i + 1 < r.size() && r[i + 1] >= 0xDC00 && r[i + 1] <= 0xDFFF);
    }
    // No trailing space before the ellipsis.
    r = FitShellText(std::wstring(118, L'd') + L"   " + std::wstring(50, L'e'), suffix, 128);
    CHECK(r.find(L" …") == std::wstring::npos);
    // A suffix that leaves no room: the text alone, cut to fit.
    CHECK(FitShellText(L"abcdef", L"0123456789", 8).size() <= 7);
    CHECK(FitShellText(L"abc", L"0123456789", 8) == L"abc");
    CHECK(FitShellText(L"abc", L"x", 0).empty());
    CHECK(FitShellText(L"abc", L"x", 1).empty());  // room for the terminator only
    CHECK(FitShellText(L"abc", L"x", 2) == L"…");
}

void TestCrossfadeMath() {
    using namespace audio;
    // The fade: off below a second, capped at 12 s and at half a song minus a second.
    CHECK(EffectiveFade(0, 200) == 0);
    CHECK(EffectiveFade(0.5, 200) == 0);
    CHECK(EffectiveFade(5, 200) == 5);
    CHECK(EffectiveFade(30, 200) == kMaxFade);
    CHECK(EffectiveFade(12, 10) == 4);  // 10 s song: 5 - 1
    CHECK(EffectiveFade(5, 3) == 0);    // too short for any fade
    CHECK(EffectiveFade(5, 0) == 0);    // unknown length (a stream)
    // Arming: wait until a few seconds before the helper starts, then arm; a seek into the last
    // seconds is too late and plays gaplessly.
    const double d = 180, f = 6, s = HelperStart(f, d);
    CHECK(std::fabs(s - (d - f - kHandoff - kPreroll)) < 1e-9);
    CHECK(DecideArm(10, d, f) == ArmDecision::Wait);
    CHECK(DecideArm(s - kArmLead - 0.01, d, f) == ArmDecision::Wait);
    CHECK(DecideArm(s - kArmLead + 0.01, d, f) == ArmDecision::Arm);
    CHECK(DecideArm(s - 0.5, d, f) == ArmDecision::Arm);
    CHECK(DecideArm(s - 0.3, d, f) == ArmDecision::TooLate);
    CHECK(DecideArm(d - 1, d, f) == ArmDecision::TooLate);
    CHECK(DecideArm(10, d, 0) == ArmDecision::TooLate);
    // The length isn't known yet (mpv reports it after the file loads): wait for it, never give up.
    CHECK(DecideArm(0, 0, EffectiveFade(3, 0)) == ArmDecision::Wait);
    CHECK(DecideArm(5, -1, 0) == ArmDecision::Wait);
    // Albums: tags win (case and spaces ignored); without them the folder is the album.
    CHECK(SameAlbum(L"Kind of Blue", L"kind of  blue", L"D:\\a\\1.mp3", L"E:\\b\\2.mp3"));
    CHECK(!SameAlbum(L"Kind of Blue", L"Blue Train", L"D:\\a\\1.mp3", L"D:\\a\\2.mp3"));
    CHECK(SameAlbum(L"", L"", L"D:\\Music\\X\\1.mp3", L"d:\\music\\x\\2.flac"));
    CHECK(SameAlbum(L"Kind of Blue", L"", L"D:\\Music\\X\\1.mp3", L"D:\\Music\\X\\2.flac"));
    CHECK(!SameAlbum(L"", L"", L"D:\\Music\\X\\1.mp3", L"D:\\Music\\Y\\2.mp3"));
    CHECK(!SameAlbum(L"", L"", L"1.mp3", L"2.mp3"));  // no folder to compare
    // Equal power: the two songs' powers add up to 1 all through the fade.
    for (int i = 0; i <= 20; ++i) {
        const double u = i / 20.0;
        const double a = FadeInGain(u), b = FadeOutGain(u);
        CHECK_MSG(std::fabs(a * a + b * b - 1) < 1e-9, "u=%.2f in=%.4f out=%.4f", u, a, b);
    }
    CHECK(FadeInGain(0) == 0 && FadeInGain(1) == 1 && FadeOutGain(0) == 1);
    CHECK(FadeInGain(-1) == 0 && FadeInGain(2) == 1);
    CHECK(GainToDb(1) == 0 && GainToDb(0) == kMinDb && GainToDb(-1) == kMinDb);
    CHECK(std::fabs(GainToDb(0.5) + 6.0206) < 1e-3);
    CHECK(GainToDb(1e-12) == kMinDb);
}

void TestSpectrum() {
    using namespace audio;
    // Bit reversal and butterflies: an impulse has a flat spectrum, a constant only DC.
    std::vector<float> re(16, 0), im(16, 0);
    re[0] = 1;
    Fft(re.data(), im.data(), 16);
    for (int i = 0; i < 16; ++i) CHECK(std::fabs(re[i] - 1) < 1e-5 && std::fabs(im[i]) < 1e-5);
    std::fill(re.begin(), re.end(), 1.0f);
    std::fill(im.begin(), im.end(), 0.0f);
    Fft(re.data(), im.data(), 16);
    CHECK(std::fabs(re[0] - 16) < 1e-4);
    for (int i = 1; i < 16; ++i) CHECK(std::fabs(re[i]) < 1e-4 && std::fabs(im[i]) < 1e-4);

    // A 1 kHz sine at half scale reads about -6 dBFS in its band and far less elsewhere.
    SpectrumAnalyzer a;
    a.Configure(48000);
    std::vector<float> sine(kFftSize);
    for (int i = 0; i < kFftSize; ++i) sine[i] = 0.5f * static_cast<float>(std::sin(2 * 3.14159265358979 * 1000 * i / 48000));
    float db[kBands];
    a.Analyze(sine.data(), db);
    int loudest = 0;
    for (int b = 1; b < kBands; ++b)
        if (db[b] > db[loudest]) loudest = b;
    const double binHz = 48000.0 / kFftSize;
    CHECK_MSG(a.BandLow(loudest) * binHz <= 1000 + binHz && a.BandHigh(loudest) * binHz >= 1000 - binHz,
              "band %d covers %.0f-%.0f Hz", loudest, a.BandLow(loudest) * binHz, a.BandHigh(loudest) * binHz);
    CHECK_MSG(db[loudest] > -7.5f && db[loudest] < -5.0f, "peak %.2f dB", db[loudest]);
    CHECK_MSG(db[0] < -60 && db[kBands - 1] < -60, "low %.1f high %.1f", db[0], db[kBands - 1]);
    // Bands rise in frequency and never run past Nyquist.
    for (int b = 1; b < kBands; ++b) CHECK(a.BandLow(b) >= a.BandLow(b - 1) && a.BandHigh(b) <= kFftSize / 2 - 1);
    // Silence is the floor, not NaN.
    std::vector<float> zero(kFftSize, 0.0f);
    a.Analyze(zero.data(), db);
    CHECK(db[10] < kFloorDb && BarLevel(db[10]) == 0);
    CHECK(BarLevel(kTopDb) == 1 && BarLevel(0) == 1 && BarLevel((kFloorDb + kTopDb) / 2) == 0.5f);
    // Smoothing: up fast, down slow, never overshooting.
    const float up = Smooth(0, 1, 0.016f, 0.025f, 0.22f), down = Smooth(1, 0, 0.016f, 0.025f, 0.22f);
    CHECK(up > 0.4f && up < 1 && down > 0.9f && down < 1);
    // The scope starts at the steepest rising zero crossing.
    float wave[64];
    for (int i = 0; i < 64; ++i) wave[i] = static_cast<float>(std::sin(2 * 3.14159265358979 * (i - 5) / 16.0));
    const int trig = ScopeTrigger(wave, 64);
    CHECK_MSG(trig == 5 || trig == 21 || trig == 37 || trig == 53, "trigger %d", trig);
    CHECK(wave[trig] >= 0 && wave[trig - 1] < 0);
    float flat[8] = {};
    CHECK(ScopeTrigger(flat, 8) == 0);
}

void TestMusicSettings() {
    FreshDataDir(L"music-settings");
    Config c;
    // Crossfade reaches Player as one pseudo option: seconds, 0 = off; the album rule is on.
    auto opts = MpvOptions(c);
    CHECK(FindOption(opts, "user-data/lumen/crossfade") == "0");
    CHECK(FindOption(opts, "user-data/lumen/crossfade-albums") == "yes");
    c.SetBool("audio.crossfade", true);
    auto live = MpvOptionFor(c, "audio.crossfade");
    CHECK(live.size() == 1 && live[0].first == "user-data/lumen/crossfade" && live[0].second == "5");
    c.SetDouble("audio.crossfadeSeconds", 40);  // clamped to the slider's 12
    CHECK(FindOption(MpvOptionFor(c, "audio.crossfadeSeconds"), "user-data/lumen/crossfade") == "12");
    c.SetBool("audio.crossfade", false);
    CHECK(FindOption(MpvOptionFor(c, "audio.crossfade"), "user-data/lumen/crossfade") == "0");
    CHECK(c.IsEnabled("audio.crossfadeSeconds") == false);
    c.SetBool("audio.crossfadeAlbums", false);
    CHECK(FindOption(MpvOptionFor(c, "audio.crossfadeAlbums"), "user-data/lumen/crossfade-albums") == "no");
    // The visualizer is a visible setting now, with the four styles of the design.
    const settings::Item* viz = settings::FindItem("audio.visualizer");
    CHECK(viz && viz->choices.size() == 5);
    if (viz) CHECK(viz->hidden == !audio::ProcessLoopbackSupported());
    CHECK((settings::FindItem("audio.visualizerUnsupported") != nullptr) == !audio::ProcessLoopbackSupported());
    CHECK(c.GetString("audio.visualizer") == "disabled");
}

// The subtitle word picker: ASS tags out, events split, words found, lookup links built.
void TestSubtitleText() {
    using namespace app::subtext;
    int align = -1;
    CHECK(StripAssTags(LR"({\i1}Hello{\i0}, {\c&H00FFFF&\b1}world{\r}!)", &align) == L"Hello, world!");
    CHECK(align == 0);
    CHECK(StripAssTags(LR"({\an8}Top line)", &align) == L"Top line" && align == 8);
    CHECK(StripAssTags(LR"({\a6}Legacy top)", &align) == L"Legacy top" && align == 8);
    CHECK(StripAssTags(LR"({\a10}Legacy middle)", &align) == L"Legacy middle" && align == 5);
    CHECK(StripAssTags(LR"({\pos(10,20)\an7}Sign)", &align) == L"Sign" && align == 7);
    CHECK(StripAssTags(LR"(First line\NSecond line)") == L"First line\nSecond line");
    CHECK(StripAssTags(LR"(soft\nbreak and\hhard)") == std::wstring(L"soft break and hard"));
    CHECK(StripAssTags(LR"(  padded \N\N  lines  )") == L"padded\nlines");
    // Vector drawings are not text; \pbo (baseline offset) is not \p.
    CHECK(StripAssTags(LR"({\p1}m 0 0 l 100 0 100 100{\p0}Caption)") == L"Caption");
    CHECK(StripAssTags(LR"({\pbo5}Kept)") == L"Kept");
    CHECK(StripAssTags(LR"({\p1}m 0 0 l 1 1)").empty());
    CHECK(StripAssTags(L"unclosed {brace") == L"unclosed {brace");
    CHECK(StripAssTags(L"{comment only}").empty());
    CHECK(StripAssTags(LR"({\k20}Ka{\k30}ra{\k25}o{\k40}ke)") == L"Karaoke");
    // Italic and bold per character, through the trimming: SRT's <i> arrives as {\i1}.
    std::vector<uint8_t> marks{9};
    CHECK(StripAssTags(LR"(plain {\i1}slanted{\i0} {\b700}heavy{\r} end)", nullptr, &marks) ==
          L"plain slanted heavy end");
    CHECK(marks.size() == 23 && marks[0] == 0 && marks[6] == kItalic && marks[12] == kItalic && marks[13] == 0 &&
          marks[14] == kBold && marks[18] == kBold && marks[20] == 0);
    CHECK(StripAssTags(LR"(  {\i1}one\N\N two{\i0}  )", nullptr, &marks) == L"one\ntwo");
    CHECK(marks.size() == 7 && marks[0] == kItalic && marks[3] == 0 && marks[4] == kItalic);
    CHECK(StripAssTags(LR"({\b1}bold{\b0} {\i1\b1}both)", nullptr, &marks) == L"bold both");
    CHECK(marks.size() == 9 && marks[0] == kBold && marks[4] == 0 && marks[5] == (kItalic | kBold));
    CHECK(StripAssTags(L"no styling", nullptr, &marks) == L"no styling" && marks.empty());
    CHECK(StripAssTags(LR"({\b400}normal weight)", nullptr, &marks) == L"normal weight" && marks.empty());

    // One event per line: a sign at the top, a line with a hard break (\N), a drawing only.
    const auto events =
        ParseEvents(LR"({\an8}Sign text)" L"\n" LR"({\i1}Привет,\Nмир!{\i0})" L"\n" LR"({\p1}m 0 0 l 5 5{\p0})");
    CHECK(events.size() == 2);
    CHECK(events.size() == 2 && events[0].align == 8 && events[0].text == L"Sign text");
    CHECK(events.size() == 2 && events[1].align == 2 && events[1].text == L"Привет,\nмир!");
    CHECK(ParseEvents(L"").empty());

    // Native .ass: the header (sub-ass-extradata) and whole event lines (sub-text/ass-full).
    const AssHeader header = ParseAssHeader(
        L"[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 1280\r\nPlayResY: 720\r\n\r\n[V4+ Styles]\r\n"
        L"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
        L"Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
        L"MarginR, MarginV, Encoding\r\n"
        L"Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,3,1,2,30,30,30,1\r\n"
        L"Style: Sign,Times New Roman,36.5,&H0000FFFF,&H000000FF,&H00102030,&H40000000,-1,1,0,0,100,100,0,0,3,2,0,8,10,"
        L"10,40,1\r\n");
    CHECK(header.playResX == 1280 && header.playResY == 720 && header.styles.size() == 2);
    if (header.styles.size() == 2) {
        const AssStyle& d = header.styles[0];
        CHECK(d.name == L"Default" && d.font == L"Arial" && d.size == 48 && !d.bold && d.primary == 0xFFFFFF &&
              d.outline == 3 && d.shadow == 1 && d.align == 2 && d.marginV == 30 && d.borderStyle == 1);
        CHECK(std::abs(d.backAlpha - (1 - 128 / 255.0f)) < 0.01f);
        const AssStyle& s = header.styles[1];
        CHECK(s.font == L"Times New Roman" && s.size == 36.5f && s.bold && s.italic && s.primary == 0xFFFF00 &&
              s.outlineColor == 0x302010 && s.borderStyle == 3 && s.align == 8 && s.marginV == 40);
    }
    CHECK(header.Find(L"sign") == &header.styles[1]);
    CHECK(header.Find(L"Missing") == &header.styles[0]);
    CHECK(ParseAssHeader(L"[Script Info]\nPlayResX: 1280\n").playResY == 1024);
    CHECK(ParseAssHeader(L"").playResY == 288 && ParseAssHeader(L"").styles.empty());
    const auto full = ParseFullEvents(
        LR"(Dialogue: 0,0:00:00.50,0:00:10.00,Default,,0,0,0,,{\i1}Styled{\i0}, with commas\Nsecond)" L"\n"
        LR"(Dialogue: 1,0:00:00.50,0:00:10.00,Sign,Actor,5,6,77,,{\an8}A sign)" L"\n"
        LR"(Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\p1}m 0 0 l 1 1)");
    CHECK(full.size() == 2);
    if (full.size() == 2) {
        CHECK(full[0].text == L"Styled, with commas\nsecond" && full[0].style == L"Default" && full[0].align == 0 &&
              full[0].marginV == 0 && full[0].marks.size() == full[0].text.size() && full[0].marks[0] == kItalic);
        CHECK(full[1].text == L"A sign" && full[1].style == L"Sign" && full[1].align == 8 && full[1].marginL == 5 &&
              full[1].marginR == 6 && full[1].marginV == 77);
    }
    CHECK(ParseFullEvents(L"0,0:00:00.00,0:00:01.00,Default,,0,0,0,,No prefix").size() == 1);
    CHECK(PlainText(L"  second\r\n track  ") == L"second\ntrack");

    auto words = [](std::wstring_view s) {
        std::vector<std::wstring> out;
        for (const Span& w : SegmentWords(s)) out.emplace_back(s.substr(w.start, w.length));
        return out;
    };
    using V = std::vector<std::wstring>;
    CHECK(words(L"Hello, world!") == V({L"Hello", L"world"}));
    CHECK(words(L"Don't stop — it's 1984.") == V({L"Don't", L"stop", L"it's", L"1984"}));
    CHECK(words(L"don\u2019t") == V({L"don\u2019t"}));
    CHECK(words(L"Кое-как, ёжик... «Вперёд»!") == V({L"Кое-как", L"ёжик", L"Вперёд"}));
    CHECK(words(L"- Dash at start -") == V({L"Dash", L"at", L"start"}));
    CHECK(words(L"line one\nline two") == V({L"line", L"one", L"line", L"two"}));
    CHECK(words(L"Cafe\u0301 naïve") == V({L"Cafe\u0301", L"naïve"}));  // combining accent stays inside
    CHECK(words(L"l\u00B7lengua") == V({L"l\u00B7lengua"}));
    CHECK(words(L"Wow 😀 ok") == V({L"Wow", L"ok"}));
    CHECK(words(L"").empty());
    CHECK(words(L" ,.!? ").empty());
    // CJK: one character per Chinese word, kana runs together, CJK punctuation separates.
    CHECK(words(L"我爱你。") == V({L"我", L"爱", L"你"}));
    CHECK(words(L"日本語のテキスト") == V({L"日", L"本", L"語", L"の", L"テキスト"}));
    CHECK(words(L"コーヒーをください") == V({L"コーヒー", L"をください"}));
    CHECK(words(L"𠀋字") == V({L"𠀋", L"字"}));  // a surrogate pair is one character
    CHECK(words(L"안녕하세요 세계") == V({L"안녕하세요", L"세계"}));
    // RTL: logical order, punctuation separates.
    CHECK(words(L"שלום, עולם") == V({L"שלום", L"עולם"}));
    CHECK(words(L"مرحبا بالعالم!") == V({L"مرحبا", L"بالعالم"}));
    CHECK(IsRtl(L"«שלום»") && IsRtl(L"مرحبا") && !IsRtl(L"Hello שלום") && !IsRtl(L"123"));
    CHECK(!IsRtl(L"\xFEFFHello") && IsRtl(L"\xFEFC"));  // a BOM is no letter; U+FEFC is Arabic

    const std::wstring t = L"New York,\nNew York";
    const auto spans = SegmentWords(t);
    CHECK(spans.size() == 4);
    CHECK(WordRange(t, spans, 0, 1) == L"New York");
    CHECK(WordRange(t, spans, 1, 2) == L"York, New");  // across the line break
    CHECK(WordRange(t, spans, 3, 0) == L"New York, New York");  // reversed range, the break as a space
    CHECK(WordRange(t, spans, 9, 9).empty());
    CHECK(Phrase({L"Sign text", L"Привет,\nмир!"}) == L"Sign text Привет, мир!");
    CHECK(Phrase({}).empty());

    CHECK(IsBitmapCodec(L"hdmv_pgs_subtitle") && IsBitmapCodec(L"DVD_SUBTITLE") && IsBitmapCodec(L"dvb_subtitle"));
    CHECK(!IsBitmapCodec(L"subrip") && !IsBitmapCodec(L"ass") && !IsBitmapCodec(L"mov_text") && !IsBitmapCodec(L""));

    CHECK(UrlEncode(L"a b&c=d/é") == L"a%20b%26c%3Dd%2F%C3%A9");
    CHECK(UrlEncode(L"ёж") == L"%D1%91%D0%B6");
    CHECK(UrlEncode(L"-_.~AZaz09") == L"-_.~AZaz09");
    CHECK(LookupUrl(L"https://{lang}.wiktionary.org/w/index.php?search={text}", L"don't", L"ru") ==
          L"https://ru.wiktionary.org/w/index.php?search=don%27t");
    CHECK(LookupUrl(L"  https://translate.google.com/?sl=auto&tl={lang}&text={text}&op=translate ", L"Привет мир", L"en") ==
          L"https://translate.google.com/?sl=auto&tl=en&text=%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82%20%D0%BC%D0%B8%D1%80&op=translate");
    CHECK(LookupUrl(L"HTTP://example.com/?q=", L"x", L"en") == L"HTTP://example.com/?q=x");  // no {text}: appended
    CHECK(LookupUrl(L"calc.exe", L"x", L"en").empty());
    CHECK(LookupUrl(L"file:///C:/Windows/System32/calc.exe?{text}", L"x", L"en").empty());
    CHECK(LookupUrl(L"javascript:alert({text})", L"x", L"en").empty());
    CHECK(LookupUrl(L"https://", L"x", L"en").empty());
    CHECK(LookupUrl(L"https://a.b/{text} \" -x", L"x", L"en").empty());
    CHECK(LookupUrl(L"", L"x", L"en").empty());
}

// --- skins (core/Skin.h, design/SKINS.md) ----------------------------------------------------

std::wstring RepoPath(const wchar_t* rel) {
    std::wstring self = json::ToWide(__FILE__);  // ...\tests\core_tests.cpp, as given to cl
    self = self.substr(0, self.find_last_of(L"\\/") + 1);
    return self + L"..\\" + rel;
}

bool HasWarning(const skin::ParseResult& r, const char* part) {
    for (const std::string& w : r.warnings)
        if (w.find(part) != std::string::npos) return true;
    return false;
}

void TestSkinParse() {
    using namespace skin;
    // Every kind of key at once.
    ParseResult r = Parse(R"({
        "lumenSkin": 1, "_comment": "ignored", "$schema": "x",
        "name": {"en": "Test", "ru": "Тест"},
        "colors": {"accent": "#FF8800", "bg.base": "#101010"},
        "dark": {"bg.base": "#000", "text.primary": "#FFFFFFFF", "bg.hover": "#FFFFFF20"},
        "light": {"text.primary": "#202020"},
        "radius": {"control": 0, "window": 20},
        "controlBar": {"opacity": 0.8}
    })");
    CHECK(r.ok);
    CHECK_MSG(r.warnings.empty(), "%s", r.warnings.empty() ? "" : r.warnings[0].c_str());
    const Skin& s = r.skin;
    CHECK(s.name == L"Test" && s.nameRu == L"Тест" && s.Name(true) == L"Тест" && s.Name(false) == L"Test");
    CHECK(s.mode == Mode::Any);
    CHECK(s.dark[kBgBase] && s.dark[kBgBase]->Rgb() == 0x000000);   // "dark" wins over "colors"
    CHECK(s.light[kBgBase] && s.light[kBgBase]->Rgb() == 0x101010);
    CHECK(s.dark[kAccent] && s.light[kAccent] && s.dark[kAccent]->Rgb() == 0xFF8800);
    CHECK(s.dark[kTextPrimary] && s.dark[kTextPrimary]->a == 1.0f);
    CHECK(s.dark[kHover] && std::fabs(s.dark[kHover]->a - 0x20 / 255.0f) < 1e-6f);
    CHECK(s.light[kTextPrimary]->Rgb() == 0x202020 && !s.dark[kTextSecondary]);
    CHECK(s.radius[kRadiusControl] == 0.0f && s.radius[kRadiusWindow] == 20.0f && !s.radius[kRadiusCard]);
    CHECK(s.barOpacity && *s.barOpacity == 0.8f);
    CHECK(!s.Empty());
    CHECK(FindToken("bg.deep") == kBgDeep && FindToken("danger") == kDanger && FindToken("video.bg") < 0);
    for (int i = 0; i < kTokenCount; ++i) CHECK(FindToken(TokenName(i)) == i);

    // A plain string name, one mode.
    r = Parse(R"({"name": "Solo", "mode": "light"})");
    CHECK(r.ok && r.warnings.empty() && r.skin.name == L"Solo" && r.skin.Name(true) == L"Solo");
    CHECK(r.skin.mode == Mode::Light && !r.skin.Empty());
    CHECK(Parse(R"({"name": "Only a name"})").skin.Empty());
    CHECK(Parse("{}").skin.Empty() && Parse("{}").ok);
}

void TestSkinFallback() {
    using namespace skin;
    // Bad values fall back per key; the good keys around them still apply.
    ParseResult r = Parse(R"({
        "lumenSkin": 7,
        "colour": {}, "font": {"family": "Comic Sans MS"},
        "name": 5, "mode": "sideways",
        "colors": {"accent": "#12", "bg.panel": "#00FF00", "bg.nonsense": "#FFFFFF", "line": 12},
        "dark": {"text.primary": "red", "text.secondary": "#GGGGGG", "bg.base": "#11223380", "scrim": "#00000080",
                 "accent.dim": null},
        "light": "white",
        "radius": {"control": -1, "card": 30, "popup": "x", "window": 10, "huge": 3},
        "controlBar": {"opacity": 0.2, "blur": 1}
    })");
    CHECK(r.ok);
    const Skin& s = r.skin;
    CHECK(HasWarning(r, "lumenSkin"));
    CHECK(HasWarning(r, "colour: unknown key"));
    CHECK(HasWarning(r, "font: fonts cannot be changed"));
    CHECK(HasWarning(r, "name:"));
    CHECK(HasWarning(r, "mode:") && s.mode == Mode::Any);
    CHECK(HasWarning(r, "colors.accent:") && !s.dark[kAccent] && !s.light[kAccent]);
    CHECK(s.dark[kBgPanel] && s.light[kBgPanel] && s.dark[kBgPanel]->Rgb() == 0x00FF00);
    CHECK(HasWarning(r, "colors.bg.nonsense: unknown colour token"));
    CHECK(HasWarning(r, "colors.line:") && !s.dark[kLine]);
    CHECK(HasWarning(r, "dark.text.primary:") && !s.dark[kTextPrimary]);
    CHECK(HasWarning(r, "dark.text.secondary:") && !s.dark[kTextSecondary]);
    // Backgrounds stay opaque; scrim may be translucent.
    CHECK(HasWarning(r, "dark.bg.base: this token is always opaque") && s.dark[kBgBase] && s.dark[kBgBase]->a == 1.0f &&
          s.dark[kBgBase]->Rgb() == 0x112233);
    CHECK(s.dark[kScrim] && s.dark[kScrim]->a < 0.6f);
    CHECK(HasWarning(r, "dark.accent.dim:") && !s.dark[kAccentDim]);
    CHECK(HasWarning(r, "light: expected an object"));
    CHECK(HasWarning(r, "radius.control:") && !s.radius[kRadiusControl]);
    CHECK(HasWarning(r, "radius.card:") && !s.radius[kRadiusCard]);
    CHECK(HasWarning(r, "radius.popup:") && !s.radius[kRadiusPopup]);
    CHECK(HasWarning(r, "radius.huge: unknown radius"));
    CHECK(s.radius[kRadiusWindow] == 10.0f);
    CHECK(HasWarning(r, "controlBar.opacity:") && !s.barOpacity);
    CHECK(HasWarning(r, "controlBar.blur: unknown key"));
    CHECK_MSG(r.warnings.size() == 19, "%zu", r.warnings.size());

    // Not a skin at all: no crash, nothing applied, one warning.
    for (const char* text : {"", "not json", "[1, 2]", "\"#000000\"", "{\"colors\": ", "null"}) {
        ParseResult bad = Parse(text);
        CHECK_MSG(!bad.ok && bad.skin.Empty() && bad.warnings.size() == 1, "%s", text);
    }
    // Containers of the wrong type inside are skipped, not fatal.
    r = Parse(R"({"colors": 5, "dark": [1], "radius": [], "controlBar": true})");
    CHECK(r.ok && r.skin.Empty() && r.warnings.size() == 4);
    // Upper and lower case, #RGB.
    r = Parse(R"({"colors": {"accent": "#aBc"}})");
    CHECK(r.ok && r.warnings.empty() && r.skin.dark[kAccent]->Rgb() == 0xAABBCC);
}

void TestSkinResolve() {
    using namespace skin;
    // No skin: the brand palette of DESIGN.md, the accent family from the tone.
    Colors dark = Resolve(true, 0, nullptr);
    CHECK(dark[kBgBase].Rgb() == 0x0E0E0E && dark[kTextPrimary].Rgb() == 0xEAEAEA && dark[kAccent].Rgb() == 0xEDEDED);
    CHECK(dark[kOnAccent] == dark[kBgBase]);
    CHECK(dark[kAccentMid] == Mix(dark[kAccent], dark[kBgBase], 0.25f));
    Colors light = Resolve(false, 4, nullptr);
    CHECK(light[kBgBase].Rgb() == 0xF2F2F2 && light[kAccent].Rgb() == 0x808080);
    CHECK(Resolve(true, 99, nullptr)[kAccent].Rgb() == 0x6B6B6B);  // tone clamped
    Skin empty;
    CHECK(Resolve(true, 2, &empty) == Resolve(true, 2, nullptr));

    // A skin accent replaces the tone; the family follows unless set.
    ParseResult r = Parse(R"({"colors": {"accent": "#3366FF", "bg.base": "#202020"}, "dark": {"accent.dim": "#001133"}})");
    Colors c = Resolve(true, 3, &r.skin);
    CHECK(c[kAccent].Rgb() == 0x3366FF && c[kBgBase].Rgb() == 0x202020);
    CHECK(c[kAccentDim].Rgb() == 0x001133);
    CHECK(c[kAccentMid] == Mix(c[kAccent], c[kBgBase], 0.25f));
    CHECK(c[kOnAccent].Rgb() == 0x202020);
    CHECK(Resolve(false, 3, &r.skin)[kAccentDim] == Mix(Rgba::Hex(0x202020), Rgba::Hex(0x3366FF), 0.12f));
    CHECK(c[kTextPrimary].Rgb() == 0xEAEAEA);  // untouched tokens keep the brand value
}

void TestSkinContrast() {
    using namespace skin;
    CHECK(std::fabs(Contrast(Rgba::Hex(0x000000), Rgba::Hex(0xFFFFFF)) - 21.0) < 1e-9);
    CHECK(std::fabs(Contrast(Rgba::Hex(0x777777), Rgba::Hex(0x777777)) - 1.0) < 1e-9);
    // The brand passes in both modes with every accent tone.
    for (int tone = 0; tone < kAccentTones; ++tone) {
        const auto issues = CheckContrast(nullptr, tone);
        CHECK_MSG(issues.empty(), "tone %d: %s", tone, issues.empty() ? "" : Describe(issues[0]).c_str());
    }
    // A dim skin: text.secondary #555 on #222 is ~2.2:1, the accent the same as the page.
    ParseResult r = Parse(R"({"mode": "dark", "dark": {"text.secondary": "#555555", "bg.base": "#222222",
                              "accent": "#222222", "on.accent": "#222222"}})");
    const auto issues = CheckContrast(&r.skin, 0);
    bool secondary = false, accent = false, onAccent = false, anyLight = false;
    for (const ContrastIssue& i : issues) {
        secondary |= i.fg == kTextSecondary && i.bg == kBgBase && i.need == 4.5 && !i.icon;
        accent |= i.fg == kAccent && i.bg == kBgBase && i.need == 3.0 && i.icon;
        onAccent |= i.fg == kOnAccent && i.bg == kAccent;
        anyLight |= !i.dark;
    }
    CHECK(secondary && accent && onAccent);
    CHECK(!anyLight);  // a dark-only skin is not checked in light
    // The focus ring is the accent where it is 3:1 on every background, else text.primary:
    // Graphite (#6B6B6B) is 2.9:1 on bg.selected in dark, #808080 2.9:1 on it in light.
    for (const bool dark : {true, false}) {
        CHECK(FocusRing(Resolve(dark, 0, nullptr)) == AccentTone(dark, 0));
        const Colors graphite = Resolve(dark, 4, nullptr);
        CHECK(FocusRing(graphite) == graphite[kTextPrimary]);
    }
    // A ring that fails 3:1 is reported (here text.primary is as dim as the accent).
    ParseResult dim = Parse(R"({"mode": "dark", "dark": {"text.primary": "#3A3A3A", "accent": "#3A3A3A"}})");
    bool ring = false;
    for (const ContrastIssue& i : CheckContrast(&dim.skin, 0)) ring |= i.fg == kFocusRingFg && i.need == 3.0;
    CHECK(ring);
    CHECK(Describe({true, kFocusRingFg, kBgCard, 1.45, 3.0, true}) == "dark: focus ring on bg.card 1.4:1 (needs 3.0:1)");
    ContrastIssue sample{true, kTextSecondary, kBgPanel, 3.97, 4.5, false};
    CHECK(Describe(sample) == "dark: text.secondary on bg.panel 3.9:1 (needs 4.5:1)");
    // #8A8A8A on bg.selected #242424 is 4.497:1: reported, and shown as 4.4, not a rounded 4.5.
    ParseResult edge = Parse(R"({"mode": "dark", "dark": {"text.secondary": "#8A8A8A"}})");
    std::vector<std::string> edgeLines;
    for (const ContrastIssue& i : CheckContrast(&edge.skin, 0)) edgeLines.push_back(Describe(i));
    CHECK_MSG(edgeLines.size() == 1 && edgeLines[0] == "dark: text.secondary on bg.selected 4.4:1 (needs 4.5:1)", "%zu %s",
              edgeLines.size(), edgeLines.empty() ? "" : edgeLines[0].c_str());
}

void TestBuiltinSkins() {
    using namespace skin;
    // Every built-in skin in res/skins parses without a warning and passes the contrast check
    // with every accent tone (a skin accent replaces the tone, so most repeat the same check).
    const settings::Item* item = settings::FindItem("interface.skin");
    CHECK(item && item->def.AsString() == "lumen");
    if (!item) return;
    int files = 0;
    for (const settings::Choice& ch : item->choices) {
        if (ch.value == "lumen") continue;
        std::string text;
        const std::wstring path = RepoPath((L"res\\skins\\" + json::ToWide(ch.value) + L".json").c_str());
        CHECK_MSG(json::ReadFileUtf8(path, &text), "%s", ch.value.c_str());
        ParseResult r = Parse(text);
        CHECK_MSG(r.ok && r.warnings.empty(), "%s: %s", ch.value.c_str(), r.warnings.empty() ? "" : r.warnings[0].c_str());
        CHECK_MSG(!r.skin.name.empty() && !r.skin.nameRu.empty(), "%s", ch.value.c_str());
        CHECK(!r.skin.barOpacity);  // built-in panels stay opaque (round 5)
        for (int tone = 0; tone < kAccentTones; ++tone) {
            const auto issues = CheckContrast(&r.skin, tone);
            CHECK_MSG(issues.empty(), "%s tone %d: %zu issues, first %s", ch.value.c_str(), tone, issues.size(),
                      issues.empty() ? "" : Describe(issues[0]).c_str());
        }
        ++files;
    }
    CHECK(files == 3);
    const settings::Item* custom = settings::FindItem("interface.customSkin");
    const settings::Item* file = settings::FindItem("interface.skinFile");
    CHECK(custom && !custom->hidden && file && !file->hidden && file->enabledWhen);
}

// --- online features (src/net) -----------------------------------------------------------------

void TestMovieHash() {
    // Expected values from the reference implementation (Python, OpenSubtitles "Hash Source Codes").
    auto make = [](size_t size) {
        std::string data(size, '\0');
        for (size_t i = 0; i < size; ++i) data[i] = static_cast<char>((i * 31 + 7) & 0xFF);
        return data;
    };
    const struct {
        size_t size;
        const char* hash;
    } cases[] = {{131072, "5f9fe02060a2c000"}, {200000, "5f9fe02060a3cd40"}, {1000003, "003f7fc0004fa243"}};
    for (const auto& c : cases) {
        const std::wstring file = g_temp + L"\\hash-" + std::to_wstring(c.size) + L".bin";
        WriteFile(file, make(c.size));
        uint64_t h = 0, size = 0;
        CHECK(net::MovieHash(file, &h, &size));
        CHECK(size == c.size);
        CHECK_MSG(net::HashHex(h) == c.hash, "%zu: %s", c.size, net::HashHex(h).c_str());
    }
    // Under 128 KiB there is no hash (the server would not know such a file anyway).
    const std::wstring tiny = g_temp + L"\\hash-tiny.bin";
    WriteFile(tiny, make(131071));
    uint64_t h = 0;
    CHECK(!net::MovieHash(tiny, &h));
    CHECK(!net::MovieHash(g_temp + L"\\missing.bin", &h));
    CHECK(net::HashHex(0x1ull) == "0000000000000001");
}

void TestGuessVideo() {
    using net::VideoGuess;
    auto g = [](const wchar_t* p) { return net::GuessVideo(p); };
    VideoGuess v = g(L"D:\\Movies\\Inception.2010.1080p.BluRay.x264-GRP.mkv");
    CHECK(v.kind == VideoGuess::Movie && v.title == L"Inception" && v.year == 2010);
    v = g(L"D:\\Movies\\The Matrix (1999).mkv");
    CHECK(v.kind == VideoGuess::Movie && v.title == L"The Matrix" && v.year == 1999);
    v = g(L"D:\\Movies\\Blade.Runner.2049.2017.2160p.mkv");
    CHECK_MSG(v.title == L"Blade Runner 2049" && v.year == 2017, "%s %d", U8(v.title).c_str(), v.year);
    v = g(L"D:\\Movies\\1917 (2019).mp4");
    CHECK(v.title == L"1917" && v.year == 2019);
    v = g(L"D:\\Movies\\2012.mkv");  // the year is the title
    CHECK(v.kind == VideoGuess::Movie && v.title == L"2012" && v.year == 0);
    v = g(L"D:\\Movies\\Амели [2001] WEB-DL.mkv");
    CHECK(v.title == L"Амели" && v.year == 2001);
    v = g(L"D:\\Home\\Holiday at the lake.mp4");  // no year: a movie guess without one
    CHECK(v.kind == VideoGuess::Movie && v.year == 0);
    v = g(L"D:\\TV\\Show.Name.S01E02.720p.mkv");
    CHECK(v.kind == VideoGuess::Episode && v.title == L"Show Name" && v.season == 1 && v.episode == 2);
    v = g(L"D:\\TV\\Doctor.Who.2005.S03E07.mkv");
    CHECK_MSG(v.title == L"Doctor Who" && v.year == 2005 && v.season == 3 && v.episode == 7, "%s", U8(v.title).c_str());
    v = g(L"D:\\TV\\The Office\\Season 2\\S02E05.mkv");  // the show from the folder above the season
    CHECK_MSG(v.kind == VideoGuess::Episode && v.title == L"The Office" && v.episode == 5, "%s", U8(v.title).c_str());
    v = g(L"D:\\TV\\Lost\\1x04 Walkabout.mkv");
    CHECK(v.kind == VideoGuess::Episode && v.title == L"Lost" && v.season == 1 && v.episode == 4);
    CHECK(net::CleanTitle(L"Mad_Max_Fury_Road_2015_REMUX") == L"Mad Max Fury Road");
}

void TestOnlineCaches() {
    const std::wstring dir = MakeDir(L"cache");
    net::DiskCache disk(dir);
    const int64_t now = 1700000000;
    CHECK(disk.Put("a", 200, "body-a", 60, now));
    CHECK(disk.Put("b", 404, "", 10, now));
    net::CacheEntry e;
    CHECK(disk.Get("a", &e, now + 59) && e.status == 200 && e.body == "body-a");
    CHECK(disk.Get("b", &e, now + 5) && e.status == 404 && e.body.empty());
    CHECK(!disk.Get("b", &e, now + 10));  // expired: gone, and the file with it
    CHECK(!FileExists(disk.FileFor("b")));
    CHECK(!disk.Get("missing", &e, now));
    // A body with newlines and binary bytes survives; another key never reads it.
    const std::string binary("line1\nline2\n\0\xFF", 14);
    CHECK(disk.Put("bin", 200, binary, 60, now));
    CHECK(disk.Get("bin", &e, now) && e.body == binary);
    CHECK(!disk.Put("zero-ttl", 200, "x", 0, now));
    CHECK(disk.Prune(now + 61) == 2);  // a and bin
    CHECK(CountFiles(dir, L"*.bin") == 0);
    disk.Put("c", 200, "c", 60, now);
    disk.Clear();
    CHECK(CountFiles(dir, L"*.bin") == 0);

    net::MemoryCache mem(100);
    mem.Put("k1", {200, now + 60, std::string(40, 'x')});
    mem.Put("k2", {200, now + 60, std::string(40, 'y')});
    CHECK(mem.Get("k1", &e, now));  // k1 is now the most recent
    mem.Put("k3", {200, now + 60, std::string(40, 'z')});  // over budget: k2 goes
    CHECK(mem.Get("k1", &e, now) && mem.Get("k3", &e, now) && !mem.Get("k2", &e, now));
    CHECK(mem.Bytes() <= 100 && mem.Count() == 2);
    CHECK(!mem.Get("k1", &e, now + 60));  // expired
    mem.Put("huge", {200, now + 60, std::string(500, 'h')});  // larger than the budget: not kept
    CHECK(!mem.Get("huge", &e, now));
    mem.Clear();
    CHECK(mem.Count() == 0 && mem.Bytes() == 0);
}

void TestOnlineJson() {
    // URLs
    CHECK(net::UrlEncode(L"AC/DC & Кино") == L"AC%2FDC%20%26%20%D0%9A%D0%B8%D0%BD%D0%BE");
    CHECK(net::UrlEncode(L"a b", true) == L"a+b");
    CHECK(net::HostOf(L"https://API.themoviedb.org:443/3/x?y") == L"api.themoviedb.org");
    CHECK(net::HostOf(L"https://user@evil.com/") == L"");
    CHECK(net::UrlAllowed(L"https://lrclib.net/api/get", false));
    CHECK(!net::UrlAllowed(L"http://lrclib.net/api/get", true));
    CHECK(net::UrlAllowed(L"http://127.0.0.1:8765/x", true));
    CHECK(!net::UrlAllowed(L"http://127.0.0.1:8765/x", false));
    CHECK(!net::UrlAllowed(L"ftp://x/", true));

    // LRCLIB
    auto doc = json::Parse(R"({"id":1,"trackName":"T","artistName":"A","duration":233.0,"instrumental":false,
        "plainLyrics":"one\ntwo","syncedLyrics":"[00:01.00]one\n[00:02.50]two"})");
    CHECK(doc.has_value());
    net::LyricsAnswer a = net::ParseLrclibRecord(*doc);
    CHECK(a.found && a.synced && a.text.rfind(L"[00:01.00]", 0) == 0 && a.duration == 233);
    doc = json::Parse(R"({"plainLyrics":"just text","syncedLyrics":null,"instrumental":false})");
    a = net::ParseLrclibRecord(*doc);
    CHECK(a.found && !a.synced && a.text == L"just text");
    doc = json::Parse(R"({"plainLyrics":null,"syncedLyrics":null,"instrumental":true})");
    CHECK(!net::ParseLrclibRecord(*doc).found);
    doc = json::Parse(R"([{"duration":200,"plainLyrics":"far"},{"duration":231,"plainLyrics":"plain"},
        {"duration":234,"syncedLyrics":"[00:01.00]x"},{"duration":233,"instrumental":true}])");
    CHECK(net::PickLrclibRecord(*doc, 233) == 2);  // synced within 3 s beats a closer plain one
    CHECK(net::PickLrclibRecord(*doc, 500) == -1);
    CHECK(net::PickLrclibRecord(*doc, 0) == 2);
    CHECK(net::LrclibGetPath(L"Кино", L"Звезда по имени Солнце", L"", 225.6) ==
          L"/api/get?artist_name=%D0%9A%D0%B8%D0%BD%D0%BE&track_name=%D0%97%D0%B2%D0%B5%D0%B7%D0%B4%D0%B0%20%D0%BF%D0%BE%20"
          L"%D0%B8%D0%BC%D0%B5%D0%BD%D0%B8%20%D0%A1%D0%BE%D0%BB%D0%BD%D1%86%D0%B5&duration=226");
    net::LyricsAnswer save;
    save.found = save.synced = true;
    save.text = L"[00:01.00]one\n[00:02.00]two";
    const std::string lrc = net::LrcFileText(save, L"A", L"T", L"");
    CHECK(lrc.rfind("\xEF\xBB\xBF[ar:A]\r\n[ti:T]\r\n[re:LRCLIB]\r\n[00:01.00]one\r\n[00:02.00]two\r\n", 0) == 0);
    // The saved file reads back as the same synced lyrics.
    app::Lyrics back = app::ParseLyrics(app::DecodeLyricsFile(lrc));
    CHECK(back.synced && back.lines.size() == 2 && back.lines[1].text == L"two");

    // OpenSubtitles: sorted parameters, lower case, "+" for spaces, sorted languages.
    net::SubtitleQuery q;
    q.moviehash = "8e245d9679d31e12";
    q.query = L"The Office";
    q.season = 2;
    q.episode = 5;
    q.languages = {"RU", "en", "ru"};
    CHECK(net::SubtitleQueryString(q) ==
          L"?episode_number=5&languages=en,ru&moviehash=8e245d9679d31e12&query=the+office&season_number=2");
    CHECK(net::SubtitleQueryString({}) == L"");
    doc = json::Parse(R"({"total_count":2,"data":[
        {"id":"1","type":"subtitle","attributes":{"language":"en","download_count":1200,"ratings":7.5,"votes":3,
          "hearing_impaired":true,"foreign_parts_only":false,"moviehash_match":false,"release":"Show.S02E05.WEB",
          "feature_details":{"title":"Episode","movie_name":"The Office - S02E05","year":2005},
          "files":[{"file_id":111,"file_name":"Show.S02E05.WEB.srt"}]}},
        {"id":"2","type":"subtitle","attributes":{"language":"ru","download_count":5,"moviehash_match":true,
          "foreign_parts_only":true,"release":"","files":[{"file_id":222,"file_name":"exact.srt"}]}},
        {"id":"3","type":"subtitle","attributes":{"language":"de","files":[]}}]})");
    const auto subs = net::ParseSubtitleSearch(*doc);
    CHECK(subs.size() == 2);
    CHECK(subs[0].fileId == 222 && subs[0].hashMatch && subs[0].foreignOnly && subs[0].release == L"exact.srt");
    CHECK(subs[1].fileId == 111 && subs[1].hearingImpaired && subs[1].downloads == 1200 && subs[1].rating == 7.5 &&
          subs[1].title == L"The Office - S02E05" && subs[1].year == 2005);
    doc = json::Parse(R"({"link":"https://www.opensubtitles.com/download/abc/x.srt","file_name":"x.srt","requests":3,
        "remaining":17,"message":"ok","reset_time":"23 hours"})");
    const net::SubtitleDownload d = net::ParseSubtitleDownload(*doc);
    CHECK(d.remaining == 17 && d.requests == 3 && d.fileName == L"x.srt" && d.resetTime == L"23 hours");
    doc = json::Parse(R"({"user":{"allowed_downloads":100,"vip":true},"base_url":"vip-api.opensubtitles.com","token":"t0k"})");
    const net::SubtitleLogin l = net::ParseSubtitleLogin(*doc);
    CHECK(l.token == "t0k" && l.vip && l.allowed == 100 && l.baseUrl == L"vip-api.opensubtitles.com");
    CHECK(net::ParseSubtitleQuota(*json::Parse(R"({"data":{"remaining_downloads":4}})")) == 4);
    CHECK(net::ParseSubtitleQuota(*json::Parse(R"({"data":{}})")) == -1);
    CHECK(net::SubtitleFileName(L"D:\\TV\\Show.S02E05.mkv", L"en", L"Whatever.SRT") == L"Show.S02E05.en.srt");
    CHECK(net::SubtitleFileName(L"D:\\TV\\a.mkv", L"pt-BR", L"noext") == L"a.pt-br.srt");
    CHECK(net::SubtitleFileName(L"D:\\TV\\a.mkv", L"en", L"x.ass") == L"a.en.ass");
    // The server's extension is kept only when it is a subtitle one.
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"en", L"Movie.en.lnk") == L"Movie.en.srt");
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"en", L"run.hta") == L"Movie.en.srt");
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"en", L"x.url") == L"Movie.en.srt");
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"en", L"x.a\\b") == L"Movie.en.srt");   // no subfolders
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"en", L"x.s/rt") == L"Movie.en.srt");
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"en", L"x.:abc") == L"Movie.en.srt");   // no NTFS stream
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"en", L"x.srt:s") == L"Movie.en.srt");
    CHECK(net::SubtitleFileName(L"D:\\M\\Movie.mkv", L"e\\n:", L"x.srt") == L"Movie.en.srt");
    // Stream URLs: characters a file name can't hold become "_".
    CHECK(net::SubtitleFileName(L"https://host/watch?v=a:b", L"en", L"x.srt") == L"watch_v=a_b.en.srt");
    CHECK(net::SubtitleFileName(L"rtsp://cam:554", L"en", L"x.srt") == L"cam_554.en.srt");
    CHECK(net::SubtitleFileName(L"https://host/a*b|c\"d<e>f", L"", L"x.vtt") == L"a_b_c_d_e_f.vtt");
    for (const wchar_t* hostile : {L"x.a\\b", L"x.:abc", L"..\\..\\evil.srt", L"a/b.lnk", L"x.srt\\..\\y.exe"}) {
        const std::wstring n = net::SubtitleFileName(L"https://h/v?x=1", L"en", hostile);
        CHECK_MSG(n.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos && library::IsSubtitlePath(n), "%ls -> %ls",
                  hostile, n.c_str());
    }

    // TMDB
    doc = json::Parse(R"({"results":[
        {"id":10,"title":"Inception","release_date":"2011-01-01","vote_average":5,"poster_path":"/p1.jpg"},
        {"id":27205,"title":"Inception","release_date":"2010-07-15","overview":"Dreams.","vote_average":8.4,
         "poster_path":"/abc.jpg","backdrop_path":"/bd.jpg"},
        {"id":0,"title":"broken"}]})");
    const auto movies = net::ParseTmdbSearch(*doc, false);
    CHECK(movies.size() == 2 && movies[1].year == 2010 && movies[1].poster == "/abc.jpg" && movies[1].rating == 8.4);
    net::VideoGuess guess;
    guess.year = 2010;
    CHECK(net::PickTmdbMatch(movies, guess) == 1);  // the same year wins over the first result
    guess.year = 2012;
    CHECK(net::PickTmdbMatch(movies, guess) == 0);  // ±1 year
    guess.year = 0;
    CHECK(net::PickTmdbMatch(movies, guess) == 0);
    CHECK(net::PickTmdbMatch({}, guess) == -1);
    doc = json::Parse(R"({"results":[{"id":2316,"name":"The Office","first_air_date":"2005-03-24","poster_path":null}]})");
    const auto shows = net::ParseTmdbSearch(*doc, true);
    CHECK(shows.size() == 1 && shows[0].tv && shows[0].title == L"The Office" && shows[0].year == 2005 &&
          shows[0].poster.empty());
    doc = json::Parse(R"({"name":"Halloween","overview":"Scary.","still_path":"/st.jpg","vote_average":7.9,"air_date":"2005-10-18"})");
    const net::EpisodeMeta ep = net::ParseTmdbEpisode(*doc);
    CHECK(ep.name == L"Halloween" && ep.still == "/st.jpg" && ep.year == 2005 && ep.rating == 7.9);
    CHECK(net::TmdbKeyIsBearer("eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJ4In0.sig"));
    CHECK(!net::TmdbKeyIsBearer("0123456789abcdef0123456789abcdef"));
    CHECK(net::YearOf("") == 0 && net::YearOf("1999-03-31") == 1999);
    CHECK(net::ParseIsoUtc("2026-10-19T12:00:00.000Z") == 1792411200);
    CHECK(net::ParseIsoUtc("soon") == 0);
    doc = json::Parse(R"({"remaining":0,"reset_time_utc":"1970-01-02T00:00:00Z"})");
    CHECK(net::ParseSubtitleDownload(*doc).resetAt == 86400);
}

void TestSecretSettings() {
    // DPAPI round trip and the plain-text fallback of older files.
    const std::string sealed = net::ProtectSecret("s3cr3t-key");
    CHECK(net::IsProtectedSecret(sealed) && sealed.find("s3cr3t") == std::string::npos);
    std::string plain;
    CHECK(net::UnprotectSecret(sealed, &plain) && plain == "s3cr3t-key");
    CHECK(net::UnprotectSecret("old-plain", &plain) && plain == "old-plain");
    CHECK(!net::UnprotectSecret("dpapi:bm90IGEgYmxvYg==", &plain));
    CHECK(net::ProtectSecret("").empty());

    // Secret settings never reach settings.json in plain text and come back on load.
    std::wstring dir = FreshDataDir(L"secrets");
    std::wstring file = dir + L"\\settings.json";
    {
        Config c;
        c.Load();
        CHECK(c.SetString("privacy.tmdbKey", "tmdb-key-123"));
        CHECK(c.SetString("privacy.opensubtitlesPassword", "pa55word"));
        CHECK(c.SetString("privacy.opensubtitlesUser", "someone"));  // not a secret
        CHECK(c.Save());
    }
    std::string text;
    CHECK(json::ReadFileUtf8(file, &text));
    CHECK(text.find("tmdb-key-123") == std::string::npos && text.find("pa55word") == std::string::npos);
    CHECK(text.find("dpapi:") != std::string::npos && text.find("someone") != std::string::npos);
    {
        Config c;
        CHECK(c.Load());
        CHECK(c.GetString("privacy.tmdbKey") == "tmdb-key-123");
        CHECK(c.GetString("privacy.opensubtitlesPassword") == "pa55word");
    }
    // A blob from another account reads as empty, not as the blob, and is written back as it was
    // (the file may go back to that account) until a new key replaces it.
    WriteFile(file, R"({"version":1,"values":{"privacy.tmdbKey":"dpapi:bm90IGEgYmxvYg=="}})");
    {
        Config c;
        CHECK(c.Load());
        CHECK(c.GetString("privacy.tmdbKey").empty());
        CHECK(c.SetString("privacy.opensubtitlesUser", "other") && c.Save());
    }
    CHECK(json::ReadFileUtf8(file, &text) && text.find("dpapi:bm90IGEgYmxvYg==") != std::string::npos);
    {
        Config c;
        CHECK(c.Load() && c.SetString("privacy.tmdbKey", "new-key") && c.Save());
    }
    CHECK(json::ReadFileUtf8(file, &text) && text.find("bm90IGEgYmxvYg==") == std::string::npos);
    {
        Config c;
        CHECK(c.Load() && c.GetString("privacy.tmdbKey") == "new-key");
    }
    // Every online feature is off by default.
    {
        Config c;
        CHECK(!c.GetBool("privacy.fetchLyrics") && !c.GetBool("privacy.onlineSubtitles") &&
              !c.GetBool("privacy.fetchMetadata") && c.GetString("library.metadata") == "file");
    }
    SetDataDirForTests(g_temp + L"\\data");
}

// The async client without the internet: refused URLs, a refused local port (offline state)
// and an answer from the disk cache, all delivered through the message loop.
void TestHttpClient() {
    auto wait = [](const std::shared_ptr<bool>& done) {
        const ULONGLONG end = GetTickCount64() + 15000;
        while (!*done && GetTickCount64() < end) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
        }
        return *done;
    };
    auto done = std::make_shared<bool>(false);
    net::Response got;
    bool inside = true;
    net::Request r;
    r.url = L"http://example.com/";  // plain http to the internet: refused before any I/O
    net::Send(r, [&, done](const net::Response& resp) {
        got = resp;
        *done = true;
        CHECK(!inside);  // never called from inside Send
    });
    inside = false;
    CHECK(wait(done) && got.error == net::Error::Blocked);

    // Port 1 on loopback refuses the connection: a network error, then the client is offline and
    // background requests stop without trying.
    const uint64_t before = net::NetworkRequests();
    *done = false;
    r.url = L"http://127.0.0.1:1/x";
    r.allowLoopbackHttp = true;
    r.timeoutMs = 3000;
    net::Send(r, [&, done](const net::Response& resp) {
        got = resp;
        *done = true;
    });
    CHECK(wait(done) && (got.error == net::Error::Network || got.error == net::Error::Timeout));
    CHECK(net::NetworkRequests() == before + 1 && net::Offline());
    *done = false;
    net::Send(r, [&, done](const net::Response& resp) {
        got = resp;
        *done = true;
    });
    CHECK(wait(done) && got.error == net::Error::Offline && net::NetworkRequests() == before + 1);

    // A fresh disk-cache entry answers without the network (even while offline).
    net::SharedDisk().Put("test:key", 200, "{\"cached\":true}", 60);
    *done = false;
    r.cacheKey = "test:key";
    net::Send(r, [&, done](const net::Response& resp) {
        got = resp;
        *done = true;
    });
    CHECK(wait(done) && got.Ok() && got.fromCache && got.body == "{\"cached\":true}");
    // The second time the memory cache answers.
    *done = false;
    const uint64_t hits = net::CacheHits();
    net::Send(r, [&, done](const net::Response& resp) {
        got = resp;
        *done = true;
    });
    CHECK(wait(done) && got.fromCache && net::CacheHits() == hits + 1 && net::NetworkRequests() == before + 1);

    // Cancelled requests never call back; Async runs work then done on this thread.
    *done = false;
    r.cacheKey.clear();
    bool called = false;
    const uint64_t id = net::Send(r, [&](const net::Response&) { called = true; });
    net::Cancel(id);
    const DWORD mainThread = GetCurrentThreadId();
    DWORD workThread = 0, doneThread = 0;
    net::Async([&] { workThread = GetCurrentThreadId(); },
               [&, done] {
                   doneThread = GetCurrentThreadId();
                   *done = true;
               });
    CHECK(wait(done));
    CHECK(!called && workThread != mainThread && doneThread == mainThread);

    // A 200 answer that isn't JSON (a captive portal's page) is not cached; a JSON one is.
    SOCKET srv = INVALID_SOCKET;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int addrLen = sizeof(addr);
    const bool listening = srv != INVALID_SOCKET && bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
                           listen(srv, 4) == 0 && getsockname(srv, reinterpret_cast<sockaddr*>(&addr), &addrLen) == 0;
    CHECK(listening);
    if (!listening) return;
    std::thread server([srv] {
        for (int served = 0; served < 3; ++served) {
            SOCKET c = accept(srv, nullptr, nullptr);
            if (c == INVALID_SOCKET) return;
            std::string req;
            char buf[1024];
            int n = 0;
            while (req.find("\r\n\r\n") == std::string::npos && (n = recv(c, buf, sizeof(buf), 0)) > 0) req.append(buf, n);
            const std::string body = req.find("/json") != std::string::npos ? "{\"ok\":1}" : "<html>Sign in</html>";
            const std::string out = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
                                    "\r\nConnection: close\r\n\r\n" + body;
            send(c, out.data(), static_cast<int>(out.size()), 0);
            shutdown(c, SD_SEND);
            closesocket(c);
        }
    });
    const std::wstring base = L"http://127.0.0.1:" + std::to_wstring(ntohs(addr.sin_port));
    auto fetch = [&](const std::wstring& path, const std::string& key) {
        net::Request q;
        q.url = base + path;
        q.allowLoopbackHttp = true;
        q.user = true;  // the refused port above left the client offline
        q.cacheKey = key;
        q.cacheTtl = 600;
        *done = false;
        net::Send(q, [&, done](const net::Response& resp) {
            got = resp;
            *done = true;
        });
        return wait(done);
    };
    const uint64_t sent = net::NetworkRequests();
    net::CacheEntry e;
    CHECK(fetch(L"/html", "test:html") && got.Ok() && !got.fromCache && got.body == "<html>Sign in</html>");
    CHECK(!net::SharedDisk().Get("test:html", &e));
    CHECK(fetch(L"/html", "test:html") && got.Ok() && !got.fromCache);  // asked again, not from a cache
    CHECK(fetch(L"/json", "test:json") && got.Ok() && !got.fromCache);
    CHECK(net::SharedDisk().Get("test:json", &e) && e.body == "{\"ok\":1}");
    CHECK(net::NetworkRequests() == sent + 3);
    closesocket(srv);  // ends the server's accept() even when a fetch above never arrived
    server.join();
    WSACleanup();
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
    {"audio_subtitle_options", TestAudioSubtitleOptions},
    {"corrupt_backup", TestCorruptBackup},
    {"hidden_condition", TestHiddenCondition},
    {"history", TestHistory},
    {"file_settings", TestFileSettings},
    {"playlists", TestPlaylists},
    {"bookmarks_favorites", TestBookmarksFavorites},
    {"media_types", TestMediaTypes},
    {"episodes_order", TestEpisodesAndOrder},
    {"shows", TestShows},
    {"player_episodes", TestPlayerEpisodes},
    {"library_scan", TestLibraryScan},
    {"library_metadata", TestLibraryMetadata},
    {"library_cache_tags", TestLibraryCacheTags},
    {"tag_reader", TestTagReader},
    {"tag_reader_fuzz", TestTagReaderFuzz},
    {"library_tag_reader", TestLibraryTagReader},
    {"lyrics_embedded", TestEmbeddedLyrics},
    {"thumbnails", TestThumbnails},
    {"folder_order", TestFolderOrder},
    {"lyrics_fill", TestLyricsFill},
    {"lyrics_lrc", TestLyricsLrc},
    {"play_queue", TestPlayQueue},
    {"shell_text", TestShellText},
    {"crossfade_math", TestCrossfadeMath},
    {"spectrum", TestSpectrum},
    {"music_settings", TestMusicSettings},
    {"subtitle_text", TestSubtitleText},
    {"skin_parse", TestSkinParse},
    {"skin_fallback", TestSkinFallback},
    {"skin_resolve", TestSkinResolve},
    {"skin_contrast", TestSkinContrast},
    {"builtin_skins", TestBuiltinSkins},
    {"movie_hash", TestMovieHash},
    {"guess_video", TestGuessVideo},
    {"online_caches", TestOnlineCaches},
    {"online_json", TestOnlineJson},
    {"secret_settings", TestSecretSettings},
    {"http_client", TestHttpClient},
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

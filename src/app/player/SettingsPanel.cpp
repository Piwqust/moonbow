// Playback Settings popover: Playback / Video / Audio / Subtitles tabs with live controls,
// the preset field ("Default Preset") and "Open Advanced Settings".
#include "Util.h"
#include "app/Actions.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/ControlBar.h"
#include "app/player/LiveState.h"
#include "app/player/PanelKit.h"
#include "app/player/PanelTabs.h"
#include "core/Paths.h"
#include "core/SettingsSchema.h"
#include "gfx/Text.h"

#include <algorithm>

namespace app::panel {

using namespace ui;

namespace {

const PlayerState& PS() {
    return TheApp().player().State();
}

Config& Cfg() {
    return TheApp().config();
}

settings::Lang Lang() {
    return CurrentLanguage() == Language::Russian ? settings::Lang::Ru : settings::Lang::En;
}

void Later(std::function<void()> fn) {
    if (MainWindow* w = TheApp().main()) w->Post(std::move(fn));
}

// --- control factories ----------------------------------------------------------------------

std::unique_ptr<Dropdown> MakeDropdown(std::vector<ui::Choice> choices, float w) {
    auto d = std::make_unique<Dropdown>(std::move(choices), 0);
    d->width = Length::Px(w);
    d->height = Length::Px(48);
    return d;
}

int IndexOf(const std::vector<ui::Choice>& choices, const std::string& value) {
    for (size_t i = 0; i < choices.size(); ++i)
        if (choices[i].value == value) return static_cast<int>(i);
    return -1;
}

// Dropdown bound to string state: `get` returns the current value, `set` applies a new one.
Dropdown* AddChoice(SyncPage* page, Widget* section, const std::wstring& label, std::vector<ui::Choice> choices,
                    std::function<std::string()> get, std::function<void(const std::string&)> set, bool fullWidth = false) {
    auto d = MakeDropdown(choices, fullWidth ? 360.0f : 188.0f);
    Dropdown* raw = d.get();
    if (fullWidth) {
        if (!label.empty()) AddCaption(section, label);
        d->width = Length::Grow();
        section->AddChild(std::move(d));
    } else {
        AddRow(section, label, std::move(d), 68);
    }
    raw->onChange = [raw, set](int i) {
        if (i >= 0 && i < static_cast<int>(raw->Choices().size())) set(raw->Choices()[static_cast<size_t>(i)].value);
    };
    // Syncers only run on changes: start from the current value, not the first choice.
    raw->SetSelected(IndexOf(raw->Choices(), get()));
    page->OnSync([raw, get] {
        const int i = IndexOf(raw->Choices(), get());
        if (i != raw->Selected()) raw->SetSelected(i);
    });
    return raw;
}

bool IsIconName(const std::string& s) {
    return s.rfind("eq_", 0) == 0 || s.find("_twotone") != std::string::npos;
}

// Dropdown over a schema choice setting (labels in the UI language).
Dropdown* AddConfigChoice(SyncPage* page, Widget* section, const std::wstring& label, const char* key,
                          std::function<void(const std::string&)> set = nullptr, bool fullWidth = false) {
    const settings::Item* item = settings::FindItem(key);
    if (!item) return nullptr;
    std::vector<ui::Choice> choices;
    for (const settings::Choice& c : item->choices)
        choices.push_back({c.Label(Lang()), c.value, IsIconName(c.extra) ? c.extra : std::string()});
    const std::string k = key;
    if (!set) set = [k](const std::string& v) { Cfg().SetString(k, v); };
    return AddChoice(page, section, label, std::move(choices), [k] { return Cfg().GetString(k); }, std::move(set),
                     fullWidth);
}

Toggle* AddToggle(SyncPage* page, Widget* section, const std::wstring& label, std::function<bool()> get,
                  std::function<void(bool)> set, float h = 52) {
    auto t = std::make_unique<Toggle>(get());
    Toggle* raw = t.get();
    raw->onChange = std::move(set);
    AddRow(section, label, std::move(t), h);
    page->OnSync([raw, get] {
        if (raw->Value() != get()) raw->SetValue(get());
    });
    return raw;
}

Toggle* AddConfigToggle(SyncPage* page, Widget* section, const std::wstring& label, const char* key, float h = 52) {
    const std::string k = key;
    return AddToggle(page, section, label, [k] { return Cfg().GetBool(k); }, [k](bool v) { Cfg().SetBool(k, v); }, h);
}

struct RangeSpec {
    float min = 0, max = 1, step = 0;
    float origin = NAN;
    std::vector<float> ticks;
    std::vector<std::pair<float, std::wstring>> labels;
    bool liveDrag = true;  // apply while dragging (mpv properties) or only on release (filter chains)
};

// "Label ........ value" row with a range slider under it.
Range* AddRange(SyncPage* page, Widget* section, const std::wstring& label, const RangeSpec& spec,
                std::function<double()> get, std::function<void(double)> set, std::function<std::wstring(double)> fmt) {
    Label* value = AddValueRow(section, label);
    auto* r = section->Add<Range>(spec.min, spec.max, static_cast<float>(get()), spec.step);
    r->origin = spec.origin;
    r->ticks = spec.ticks;
    r->labels = spec.labels;
    r->trackH = 16;
    auto* spacer = section->Add<Widget>();
    spacer->height = Length::Px(spec.labels.empty() ? 12.0f : 6.0f);
    r->onChange = [value, fmt, set, live = spec.liveDrag](float v) {
        value->SetText(fmt(v));
        if (live) set(v);
    };
    r->onCommit = [set](float v) { set(v); };
    page->OnSync([r, value, get, fmt] {
        if (r->Dragging()) return;
        r->SetValue(static_cast<float>(get()));
        value->SetText(fmt(get()));
    });
    value->SetText(fmt(get()));
    return r;
}

// Range bound to a numeric schema setting; written on release (they rebuild filter chains).
Range* AddConfigRange(SyncPage* page, Widget* section, const char* key, std::wstring label = {}) {
    const settings::Item* item = settings::FindItem(key);
    if (!item) return nullptr;
    const std::string k = key;
    RangeSpec spec;
    spec.min = static_cast<float>(item->min);
    spec.max = static_cast<float>(item->max);
    spec.step = static_cast<float>(item->step);
    if (item->min < 0 && item->max > 0) spec.origin = 0;
    spec.liveDrag = false;
    const std::wstring unit = item->Unit(Lang());
    const int decimals = item->step > 0 && item->step < 1 ? (item->step < 0.1 ? 2 : 1) : 0;
    auto fmt = [unit, decimals](double v) {
        wchar_t buf[48];
        swprintf_s(buf, L"%.*f", decimals, std::abs(v) < 0.0005 ? 0.0 : v);
        std::wstring s = LocalDecimal(buf);
        if (unit.empty()) return s;
        return s + L" " + unit;
    };
    return AddRange(page, section, label.empty() ? item->Label(Lang()) : label, spec, [k] { return Cfg().GetDouble(k); },
                    [k](double v) { Cfg().SetDouble(k, v); }, fmt);
}

// Label + value box on one row, slider below (delays).
Range* AddBoxRange(SyncPage* page, Widget* section, const std::wstring& label, const RangeSpec& spec,
                   std::function<double()> get, std::function<void(double)> set, std::function<std::wstring(double)> fmt) {
    auto box = std::make_unique<ValueBox>();
    ValueBox* b = box.get();
    b->text = [get, fmt] { return fmt(get()); };
    b->onValue = set;
    AddRow(section, label, std::move(box), 56);
    auto* r = section->Add<Range>(spec.min, spec.max, static_cast<float>(get()), spec.step);
    r->origin = spec.origin;
    r->ticks = spec.ticks;
    r->labels = spec.labels;
    r->trackH = 21;
    auto* spacer = section->Add<Widget>();
    spacer->height = Length::Px(14);
    r->onChange = [set](float v) { set(v); };
    page->OnSync([r, b, get] {
        if (!r->Dragging()) r->SetValue(static_cast<float>(get()));
        b->Refresh();
    });
    b->Refresh();
    return r;
}

ValueBox* AddBox(SyncPage* page, Widget* section, const std::wstring& label, std::function<std::wstring()> text,
                 std::function<void(double)> onValue, std::function<void(const std::wstring&)> onText = nullptr) {
    auto box = std::make_unique<ValueBox>();
    ValueBox* b = box.get();
    b->text = std::move(text);
    b->onValue = std::move(onValue);
    b->onText = std::move(onText);
    AddRow(section, label, std::move(box), 52);
    page->OnSync([b] { b->Refresh(); });
    b->Refresh();
    return b;
}

std::wstring Signed(double v, int decimals, const wchar_t* unit) {
    wchar_t buf[48];
    swprintf_s(buf, L"%.*f %s", decimals, std::abs(v) < 0.0005 ? 0.0 : v, unit);
    return LocalDecimal(buf);
}

// Tracks of one type as dropdown choices ("0" = none).
std::vector<ui::Choice> TrackChoices(const wchar_t* type) {
    std::vector<ui::Choice> out{{Tr(L"None"), "0", ""}};
    int n = 0;
    for (const Track& t : PS().tracks) {
        if (t.type != type) continue;
        ++n;
        out.push_back({TrackName(t, n), std::to_string(t.id), ""});
    }
    return out;
}

std::string SelectedTrack(const wchar_t* type) {
    for (const Track& t : PS().tracks)
        if (t.type == type && t.selected) return std::to_string(t.id);
    return "0";
}

// Dropdown over the tracks of one type. The list follows the file: tracks arrive after the
// panel opens, external subtitles get added.
Dropdown* AddTracks(SyncPage* page, Widget* section, const std::wstring& label, const wchar_t* type,
                    std::function<std::string()> get, std::function<void(const std::string&)> set) {
    auto d = MakeDropdown(TrackChoices(type), 188.0f);
    Dropdown* raw = d.get();
    AddRow(section, label, std::move(d), 68);
    raw->onChange = [raw, set](int i) {
        if (i >= 0 && i < static_cast<int>(raw->Choices().size())) set(raw->Choices()[static_cast<size_t>(i)].value);
    };
    auto sync = [raw, type, get] {
        auto choices = TrackChoices(type);
        bool same = choices.size() == raw->Choices().size();
        for (size_t i = 0; same && i < choices.size(); ++i)
            same = choices[i].value == raw->Choices()[i].value && choices[i].label == raw->Choices()[i].label;
        const int i = IndexOf(choices, get());
        if (!same) raw->SetChoices(std::move(choices), i);
        else if (i != raw->Selected()) raw->SetSelected(i);
    };
    sync();
    page->OnSync(sync);
    return raw;
}

Dropdown* AddTrackChoice(SyncPage* page, Widget* section, const std::wstring& label, const wchar_t* type,
                         const char* prop) {
    return AddTracks(page, section, label, type, [type] { return SelectedTrack(type); },
                     [prop](const std::string& v) { TheApp().player().SetTrack(prop, std::atoll(v.c_str())); });
}

// Swatch + "#RRGGBB" field bound to a color setting.
class ColorBox : public ValueBox {
public:
    explicit ColorBox(std::string key) : key_(std::move(key)) {
        text = [this] { return Utf8ToWide(Cfg().GetString(key_)); };
        onText = [this](const std::wstring& t) {
            std::wstring s = t;
            while (!s.empty() && iswspace(s.back())) s.pop_back();
            while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
            if (!s.empty() && s[0] != L'#') s = L"#" + s;
            for (wchar_t& ch : s) ch = static_cast<wchar_t>(towupper(ch));
            Cfg().SetString(key_, WideToUtf8(s));  // the schema rejects malformed colors
        };
    }
    void Paint(Painter& p) override {
        ValueBox::Paint(p);
        const std::string v = Cfg().GetString(key_);
        if (v.size() == 7) {
            const uint32_t rgb = static_cast<uint32_t>(std::strtoul(v.c_str() + 1, nullptr, 16));
            const Rect sw{rect.Right() - 30, rect.Cy() - 11, 22, 22};
            p.Fill(sw, Color::Hex(rgb), 2);
            p.Stroke(sw, P().lineStrong, 2);
        }
    }

private:
    std::string key_;
};

// Three or more equal buttons in a dark well (Flip: None | Horizontal | Vertical).
Segmented* AddSegments(SyncPage* page, Widget* section, const std::wstring& caption, std::vector<ui::Choice> choices,
                       std::function<int()> get, std::function<void(int)> set) {
    if (!caption.empty()) AddCaption(section, caption);
    auto* s = section->Add<Segmented>(std::move(choices), get());
    s->width = Length::Grow();
    s->height = Length::Px(44);
    s->onChange = std::move(set);
    page->OnSync([s, get] {
        if (s->Selected() != get()) s->SetSelected(get());
    });
    auto* spacer = section->Add<Widget>();
    spacer->height = Length::Px(10);
    return s;
}

Expander* AddExpander(Widget* section, const std::string& key, const std::wstring& caption,
                      const std::wstring& title = {}) {
    if (!caption.empty()) AddCaption(section, caption);
    auto* e = section->Add<Expander>(key);
    e->title = title;
    auto* spacer = section->Add<Widget>();
    spacer->height = Length::Px(14);
    return e;
}

// --- custom shaders (<data>\shaders, enabled ones in video.shaders as "a.glsl|b.hook") ----------

std::wstring ShadersFolder() {
    return DataDir() + L"\\shaders";
}

// .glsl / .hook files in the shaders folder, sorted. A few directory entries, read when the
// Video tab opens (local folder; no recursion).
std::vector<std::wstring> ShaderFiles() {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((ShadersFolder() + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = fd.cFileName;
        const size_t dot = name.rfind(L'.');
        if (dot == std::wstring::npos) continue;
        const wchar_t* ext = name.c_str() + dot;
        if (_wcsicmp(ext, L".glsl") == 0 || _wcsicmp(ext, L".hook") == 0) out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(),
              [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return out;
}

std::vector<std::string> EnabledShaderList() {
    std::vector<std::string> out;
    const std::string list = Cfg().GetString("video.shaders");
    size_t start = 0;
    while (start < list.size()) {
        size_t bar = list.find('|', start);
        if (bar == std::string::npos) bar = list.size();
        if (bar > start) out.push_back(list.substr(start, bar - start));
        start = bar + 1;
    }
    return out;
}

// Toggling rewrites glsl-shaders live (Config pushes it to mpv); mpv keeps playing.
void SetShaderEnabled(const std::string& name, bool on) {
    std::vector<std::string> list = EnabledShaderList();
    list.erase(std::remove(list.begin(), list.end(), name), list.end());
    if (on) list.push_back(name);
    std::string joined;
    for (const std::string& n : list) joined += (joined.empty() ? "" : "|") + n;
    Cfg().SetString("video.shaders", joined);
}

std::unique_ptr<Widget> Page(SyncPage** out) {
    auto sv = std::make_unique<ScrollView>();
    sv->width = Length::Grow();
    sv->height = Length::Grow();
    *out = sv->SetContent<SyncPage>();
    return sv;
}

// --- tabs ------------------------------------------------------------------------------------

std::unique_ptr<Widget> PlaybackTab() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    auto* top = page->Add<Widget>();
    top->height = Length::Px(12);
    Widget* s = AddSection(page);

    AddSegments(
        page, s, Tr(L"Repeat"), {{Tr(L"Off"), "off", ""}, {Tr(L"All"), "all", ""}, {Tr(L"One"), "one", ""}},
        [] { return static_cast<int>(TheApp().queue().RepeatMode()); },
        // Through the settings, like the L and Shuffle keys: they persist, drive mpv's loop-file
        // and the queue, and the keys then toggle from the value shown here.
        [](int i) {
            const Repeat r = static_cast<Repeat>(i);
            Cfg().SetString("playback.repeat", r == Repeat::One ? "one" : r == Repeat::All ? "all" : "off");
        });
    AddToggle(page, s, Tr(L"Shuffle"), [] { return TheApp().queue().Shuffle(); },
              [](bool v) { Cfg().SetBool("playback.shuffle", v); });
    AddToggle(page, s, Tr(L"Stop after this item"), [] { return Live().stopAfter; },
              [](bool v) { Live().SetStopAfter(v); });
    std::vector<ui::Choice> sleep{{Tr(L"Off"), "0", ""}};
    for (int m : {15, 30, 45, 60, 90, 120}) sleep.push_back({Fmt(Tr(L"{0} min"), m), std::to_string(m), ""});
    AddChoice(page, s, Tr(L"Sleep Timer"), std::move(sleep), [] { return std::to_string(Live().sleepMinutes); },
              [](const std::string& v) { Live().SetSleepTimer(std::atoi(v.c_str())); });

    // A-B loop: set the two points from the current position.
    {
        auto* row = s->Add<Widget>();
        row->dir = Dir::Row;
        row->align = Align::Center;
        row->width = Length::Grow();
        row->height = Length::Px(52);
        row->gap = 8;
        auto* l = row->Add<Label>(Tr(L"A-B Loop"), TextStyle::Body, Tone::Tertiary);
        l->width = Length::Grow();
        l->pad = {4, 0, 0, 0};
        auto* a = row->Add<Button>(L"A", ButtonKind::Secondary);
        a->tooltip = Tr(L"Set the loop start");
        a->onClick = [] { TheApp().player().SetProperty("ab-loop-a", PS().position); };
        auto* b = row->Add<Button>(L"B", ButtonKind::Secondary);
        b->tooltip = Tr(L"Set the loop end");
        b->onClick = [] {
            if (std::isnan(Live().V().abA)) TheApp().player().SetProperty("ab-loop-a", 0.0);
            TheApp().player().SetProperty("ab-loop-b", PS().position);
        };
        auto* clear = row->Add<IconButton>("close", Tr(L"Clear the loop"), 32, 20);
        clear->onClick = [] {
            TheApp().player().SetProperty("ab-loop-a", std::string("no"));
            TheApp().player().SetProperty("ab-loop-b", std::string("no"));
        };
        auto* info = s->Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
        info->pad = {4, 0, 0, 0};
        info->height = Length::Px(18);
        page->OnSync([a, b, clear, info] {
            const LiveValues& v = Live().V();
            a->kind = std::isnan(v.abA) ? ButtonKind::Secondary : ButtonKind::Primary;
            b->kind = std::isnan(v.abB) ? ButtonKind::Secondary : ButtonKind::Primary;
            clear->enabled = !std::isnan(v.abA) || !std::isnan(v.abB);
            std::wstring t;
            if (!std::isnan(v.abA)) t = L"A " + FormatTime(v.abA, true);
            if (!std::isnan(v.abB)) t += L"  –  B " + FormatTime(v.abB, true);
            if (t.empty()) t = Tr(L"Loop a part of the file: set A, then B.");
            info->SetText(t);
        });
    }
    AddSectionLine(page);
    Widget* sp = AddSection(page);
    auto* gap = sp->Add<Widget>();
    gap->height = Length::Px(8);
    RangeSpec speed;
    speed.min = 0.25f;
    speed.max = 2.0f;
    speed.step = 0.05f;
    speed.origin = 0.25f;
    for (float t = 0.5f; t < 2.0f; t += 0.25f) speed.ticks.push_back(t);
    for (float t = 0.25f; t <= 2.001f; t += 0.25f) speed.labels.push_back({t, FormatNumber(t)});
    AddBoxRange(page, sp, Tr(L"Speed"), speed, [] { return PS().speed; },
                [](double v) { TheApp().player().SetSpeed(std::clamp(v, 0.05, 8.0)); },
                [](double v) { return FormatNumber(v) + L"x"; });
    return root;
}

std::unique_ptr<Widget> VideoTab() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    AddSectionTitle(page, Tr(L"Defaults"));
    Widget* s = AddSection(page);
    auto* top = s->Add<Widget>();
    top->height = Length::Px(8);
    AddSegments(
        page, s, Tr(L"Flip"), {{Tr(L"None"), "0", ""}, {Tr(L"Horizontal"), "1", ""}, {Tr(L"Vertical"), "2", ""}},
        [] { return Live().flip; }, [](int i) { Live().SetFlip(i); });
    AddBox(
        page, s, Tr(L"Rotate"), [] { return FormatNumber(Live().V().rotate) + L"°"; },
        [](double v) {
            const int r = ((static_cast<int>(std::lround(v)) % 360) + 360) % 360;
            TheApp().player().SetProperty("video-rotate", static_cast<double>(r));
        });
    AddChoice(page, s, Tr(L"Zoom"),
              {{Tr(L"Fit to Window"), "fit", ""},
               {Tr(L"1:4 Quarter"), "quarter", ""},
               {Tr(L"1:2 Half"), "half", ""},
               {Tr(L"1:1 Original"), "original", ""},
               {Tr(L"2:1 Double"), "double", ""}},
              [] { return Live().zoomMode; }, [](const std::string& v) { Live().SetZoomMode(v); });
    AddConfigChoice(page, s, Tr(L"HDR"), "video.hdr");
    AddConfigChoice(page, s, Tr(L"Tone Mapping"), "video.toneMapping");
    AddConfigChoice(page, s, Tr(L"Resolution"), "playback.videoQuality");
    AddChoice(page, s, Tr(L"Aspect Ratio"),
              {{Tr(L"Default"), "auto", ""},
               {L"16:9", "16:9", ""},
               {L"4:3", "4:3", ""},
               {L"21:9", "21:9", ""},
               {L"2.35:1", "2.35:1", ""},
               {L"16:10", "16:10", ""},
               {L"5:4", "5:4", ""},
               {L"1:1", "1:1", ""}},
              [] { return Live().aspect; }, [](const std::string& v) { Live().SetAspect(v); });
    AddChoice(page, s, Tr(L"Crop"),
              {{Tr(L"Default"), "none", ""},
               {L"16:9", "16:9", ""},
               {L"4:3", "4:3", ""},
               {L"21:9", "21:9", ""},
               {L"2.35:1", "2.35:1", ""},
               {Tr(L"Custom"), "custom", ""}},
              [] { return Live().crop; }, [](const std::string& v) { Live().SetCrop(v); });
    AddChoice(page, s, Tr(L"Deinterlace"), {{Tr(L"Auto"), "auto", ""}, {Tr(L"On"), "yes", ""}, {Tr(L"Off"), "no", ""}},
              [] { return Live().deinterlace; }, [](const std::string& v) { Live().SetDeinterlace(v); });
    {
        // The rule for the next file (Player.cpp reset-on-next-file, LiveState's file change).
        auto* note = s->Add<Label>(Tr(L"Flip, rotation, zoom and aspect ratio apply to the current file only."),
                                   TextStyle::Small, Tone::Tertiary);
        note->pad = {4, 4, 0, 0};
        note->wrap = true;
    }
    auto* bottom = s->Add<Widget>();
    bottom->height = Length::Px(8);

    AddSectionLine(page);
    AddSectionTitle(page, Tr(L"Quality"));
    Widget* q = AddSection(page);
    AddConfigChoice(page, q, Tr(L"Image Quality"), "video.quality");
    AddChoice(page, q, Tr(L"Upscaler"),
              {{Tr(L"Auto"), "auto", ""},
               {Tr(L"Bilinear"), "bilinear", ""},
               {L"Spline36", "spline36", ""},
               {L"Lanczos", "lanczos", ""},
               {L"EWA Lanczos", "ewa_lanczos", ""},
               {L"EWA Lanczos Sharp", "ewa_lanczossharp", ""}},
              [] { return Cfg().GetString("video.upscaler"); },
              [](const std::string& v) { Cfg().SetString("video.upscaler", v); });
    AddConfigToggle(page, q, Tr(L"Smooth Motion"), "video.smoothMotion");
    AddConfigToggle(page, q, Tr(L"AI Upscaling"), "video.superResolution");
    {
        // Why the upscaler isn't running (no D3D11 decoding, CPU filters on, video big enough...).
        auto* status = q->Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
        status->pad = {4, 0, 0, 0};
        status->wrap = true;
        auto sync = [status] {
            std::wstring t = Live().UpscalerStatus();
            if (t.empty() && Live().UpscalerActive()) t = Tr(L"Upscaling on the graphics card");
            if (t != status->Text()) status->SetText(t);
        };
        sync();
        page->OnSync(sync);
    }
    auto* qbottom = q->Add<Widget>();
    qbottom->height = Length::Px(10);

    AddSectionLine(page);
    AddSectionTitle(page, Tr(L"Filters"));
    Widget* f = AddSection(page);
    Expander* ess = AddExpander(f, "video.essential", Tr(L"Essential"));
    {
        Widget* b = ess->Body();
        RangeSpec pic;
        pic.min = -100;
        pic.max = 100;
        pic.step = 1;
        pic.origin = 0;
        auto prop = [&](const wchar_t* label, const char* name, double LiveValues::*field) {
            const std::string n = name;
            AddRange(page, b, Tr(label), pic, [field] { return Live().V().*field; },
                     [n](double v) { TheApp().player().SetProperty(n.c_str(), std::round(v)); },
                     [](double v) { return FormatNumber(std::round(v)); });
        };
        prop(L"Hue", "hue", &LiveValues::hue);
        prop(L"Brightness", "brightness", &LiveValues::brightness);
        prop(L"Contrast", "contrast", &LiveValues::contrast);
        prop(L"Saturation", "saturation", &LiveValues::saturation);
        prop(L"Gamma", "gamma", &LiveValues::gamma);
        AddConfigRange(page, b, "video.sharpen", Tr(L"Sharpen Sigma"));
        AddConfigToggle(page, b, Tr(L"Debanding"), "video.deband", 48);
        AddConfigRange(page, b, "video.debandRange", Tr(L"Banding Removal Radius"));
        AddConfigRange(page, b, "video.filmGrain", Tr(L"Film Grain Variance"));
        auto* reset = b->Add<FlatButton>(Tr(L"Reset picture"), "refresh");
        reset->onClick = [] { Live().ResetPicture(); };
    }
    AddSectionLine(page);
    Widget* c = AddSection(page);
    auto* cgap = c->Add<Widget>();
    cgap->height = Length::Px(10);
    Expander* crop = AddExpander(c, "video.crop", Tr(L"Crop"));
    {
        Widget* b = crop->Body();
        auto field = [page](Widget* row, const wchar_t* label, double LiveState::*value) {
            auto* cell = row->Add<Widget>();
            cell->dir = Dir::Row;
            cell->align = Align::Center;
            cell->width = Length::Grow();
            auto* l = cell->Add<Label>(Tr(label), TextStyle::Body, Tone::Tertiary);
            l->width = Length::Px(56);
            auto* box = cell->Add<ValueBox>();
            box->width = Length::Grow();
            box->text = [value] { return FormatNumber(Live().*value, 0) + L" " + Tr(L"px"); };
            box->onValue = [value](double v) {
                Live().*value = std::max(0.0, std::round(v));
                Live().SetCrop("custom");
            };
            page->OnSync([box] { box->Refresh(); });
            box->Refresh();
        };
        for (int r = 0; r < 2; ++r) {
            auto* row = b->Add<Widget>();
            row->dir = Dir::Row;
            row->align = Align::Center;
            row->width = Length::Grow();
            row->height = Length::Px(48);
            row->gap = 10;
            if (r == 0) {
                field(row, L"Top", &LiveState::cropT);
                field(row, L"Bottom", &LiveState::cropB);
            } else {
                field(row, L"Left", &LiveState::cropL);
                field(row, L"Right", &LiveState::cropR);
            }
        }
        auto* reset = b->Add<FlatButton>(Tr(L"Reset crop"), "refresh");
        reset->onClick = [] {
            LiveState& l = Live();
            l.cropT = l.cropB = l.cropL = l.cropR = 0;
            l.SetCrop("none");
        };
    }
    AddSectionLine(page);
    Widget* col = AddSection(page);
    auto* colgap = col->Add<Widget>();
    colgap->height = Length::Px(10);
    Expander* colors = AddExpander(col, "video.colors", Tr(L"Colors"));
    {
        Widget* b = colors->Body();
        AddToggle(page, b, Tr(L"Negate Colors"), [] { return Live().negate; },
                  [](bool v) { Live().SetColorFx(v, Live().grayscale); });
        AddToggle(page, b, Tr(L"Grayscale"), [] { return Live().grayscale; },
                  [](bool v) { Live().SetColorFx(Live().negate, v); });
    }
    AddSectionLine(page);
    Widget* sh = AddSection(page);
    auto* shgap = sh->Add<Widget>();
    shgap->height = Length::Px(10);
    Expander* shaders = AddExpander(sh, "video.shaders", Tr(L"Shaders"));
    {
        Widget* b = shaders->Body();
        const std::vector<std::wstring> files = ShaderFiles();
        if (files.empty()) {
            auto* hint = b->Add<Label>(Tr(L"Put .glsl or .hook shader files (FSRCNNX, Anime4K...) in the shaders folder."),
                                       TextStyle::Small, Tone::Tertiary);
            hint->wrap = true;
            hint->pad = {4, 4, 0, 10};
        }
        for (const std::wstring& file : files) {
            const std::string name = WideToUtf8(file);
            AddToggle(
                page, b, file,
                [name] {
                    const auto list = EnabledShaderList();
                    return std::find(list.begin(), list.end(), name) != list.end();
                },
                [name](bool v) { SetShaderEnabled(name, v); }, 48);
        }
        auto* open = b->Add<FlatButton>(Tr(L"Open shaders folder"), "folder");
        open->onClick = [] {
            const std::wstring dir = ShadersFolder();
            CreateDirectoryW(dir.c_str(), nullptr);
            ui::Host::ShellOpen(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        };
    }
    return root;
}

std::unique_ptr<Widget> AudioTab() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    AddSectionTitle(page, Tr(L"Defaults"));
    Widget* s = AddSection(page);
    AddTrackChoice(page, s, Tr(L"Audio Track"), L"audio", "aid");
    AddConfigChoice(page, s, Tr(L"Audio Language"), "audio.language");
    AddChoice(page, s, Tr(L"Stereo Mode"),
              {{Tr(L"Stereo"), "stereo", ""},
               {Tr(L"Mono"), "mono", ""},
               {Tr(L"Reverse stereo"), "reverse", ""},
               {Tr(L"Left channel"), "left", ""},
               {Tr(L"Right channel"), "right", ""}},
              [] { return Live().stereoMode; }, [](const std::string& v) { Live().SetStereoMode(v); });
    auto* sgap = s->Add<Widget>();
    sgap->height = Length::Px(20);

    AddSectionLine(page);
    AddSectionTitle(page, Tr(L"Synchronization"));
    Widget* sync = AddSection(page);
    RangeSpec delay;
    delay.min = -10;
    delay.max = 10;
    delay.step = 0.05f;
    delay.origin = 0;
    delay.labels = {{-10.0f, L"-10 " + std::wstring(Tr(L"s"))}, {0.0f, L"0 " + std::wstring(Tr(L"s"))},
                    {10.0f, L"+10 " + std::wstring(Tr(L"s"))}};
    AddBoxRange(page, sync, Tr(L"Audio Track Delay"), delay, [] { return Live().V().audioDelay; },
                [](double v) { TheApp().player().SetProperty("audio-delay", v); },
                [](double v) { return FormatSeconds(v); });

    AddSectionLine(page);
    AddSectionTitle(page, Tr(L"Filters"));
    Widget* eq = AddSection(page);
    AddConfigToggle(page, eq, Tr(L"Equalizer"), "audio.eq");
    AddConfigChoice(page, eq, L"", "audio.eqPreset",
                    [](const std::string& id) {
                        Cfg().ApplyEqPreset(id);
                        if (id != "none") Cfg().SetBool("audio.eq", true);
                    },
                    true);
    auto* eqgap = eq->Add<Widget>();
    eqgap->height = Length::Px(10);
    Expander* bands = AddExpander(eq, "audio.eq", L"", Tr(L"Equalizer bands"));
    // Bands keep working while the EQ is off (moving one turns it on), but look inactive.
    class DimWhenOff : public Widget {
    public:
        DimWhenOff() {
            dir = Dir::Column;
            width = Length::Grow();
        }
        void Paint(Painter& p) override {
            dimmed_ = !Cfg().GetBool("audio.eq");
            if (dimmed_) p.PushOpacity(0.45f);
        }
        void PaintOver(Painter& p) override {
            if (dimmed_) p.PopOpacity();
        }

    private:
        bool dimmed_ = false;
    };
    auto* dim = bands->Body()->Add<DimWhenOff>();
    page->OnSync([dim] { dim->Invalidate(); });
    AddConfigRange(page, dim, settings::kEqPreampKey);
    for (const char* key : settings::kEqBandKeys) {
        Range* r = AddConfigRange(page, dim, key);
        if (!r) continue;
        r->onCommit = [prev = r->onCommit](float v) {
            prev(v);
            if (!Cfg().GetBool("audio.eq")) Cfg().SetBool("audio.eq", true);
        };
    }
    AddConfigToggle(page, eq, Tr(L"2 Pass"), "audio.eqTwoPass", 48);

    struct Effect {
        const wchar_t* label;
        const wchar_t* settings;
        const char* toggle;
        const char* group;
    };
    for (const Effect& fx : {Effect{L"Compressor", L"Compressor settings", "audio.nightMode", "compressor"},
                             Effect{L"Spatializer", L"Spatializer settings", "audio.spatial", "spatializer"},
                             Effect{L"Stereo Widener", L"Stereo widener settings", "audio.stereoWiden",
                                    "stereoWidener"}}) {
        AddSectionLine(page);
        Widget* sec = AddSection(page);
        AddConfigToggle(page, sec, Tr(fx.label), fx.toggle, 58);
        Expander* e = AddExpander(sec, std::string("audio.") + fx.group, L"", Tr(fx.settings));
        if (std::string(fx.group) == "spatializer") AddConfigChoice(page, e->Body(), L"", "audio.spatialMode", nullptr, true);
        for (const settings::Item* item : settings::AllItems())
            if (item->group == fx.group && item->type == settings::ItemType::Slider) AddConfigRange(page, e->Body(), item->key.c_str());
    }
    AddSectionLine(page);
    Widget* norm = AddSection(page);
    AddConfigToggle(page, norm, Tr(L"Normalize Loudness"), "audio.normalize", 58);
    return root;
}

std::unique_ptr<Widget> SubtitlesTab() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    AddSectionTitle(page, Tr(L"Defaults"));
    Widget* s = AddSection(page);
    AddTrackChoice(page, s, Tr(L"Subtitle Track"), L"sub", "sid");
    AddTracks(
        page, s, Tr(L"Secondary Subtitles"), L"sub", [] { return std::to_string(Live().V().secondarySid); },
        [](const std::string& v) {
            TheApp().player().SetProperty("secondary-sid", std::string(v == "0" ? "no" : v));
        });
    AddConfigChoice(page, s, Tr(L"Subtitle Language"), "subtitles.language");
    AddToggle(page, s, Tr(L"Show Subtitles"), [] { return Live().V().subVisible; },
              [](bool v) { TheApp().player().SetProperty("sub-visibility", v); });

    AddSectionLine(page);
    AddSectionTitle(page, Tr(L"Synchronization"));
    Widget* sync = AddSection(page);
    RangeSpec delay;
    delay.min = -10;
    delay.max = 10;
    delay.step = 0.05f;
    delay.origin = 0;
    delay.labels = {{-10.0f, L"-10 " + std::wstring(Tr(L"s"))}, {0.0f, L"0 " + std::wstring(Tr(L"s"))},
                    {10.0f, L"+10 " + std::wstring(Tr(L"s"))}};
    AddBoxRange(page, sync, Tr(L"Subtitle Track Delay"), delay, [] { return Live().V().subDelay; },
                [](double v) { TheApp().player().SetProperty("sub-delay", v); },
                [](double v) { return FormatSeconds(v); });
    RangeSpec speed;
    speed.min = 0.5f;
    speed.max = 2;
    speed.step = 0.001f;
    speed.origin = 1;
    AddBoxRange(page, sync, Tr(L"Subtitle Speed"), speed, [] { return Live().V().subSpeed; },
                [](double v) { TheApp().player().SetProperty("sub-speed", std::clamp(v, 0.1, 10.0)); },
                [](double v) { return Signed(v, 3, L"×"); });

    AddSectionLine(page);
    AddSectionTitle(page, Tr(L"Appearance"));
    Widget* a = AddSection(page);
    RangeSpec size;
    size.min = 0.5f;
    size.max = 3;
    size.step = 0.05f;
    size.origin = 0.5f;
    AddRange(page, a, Tr(L"Subtitle Size"), size, [] { return Live().V().subScale; },
             [](double v) { TheApp().player().SetProperty("sub-scale", v); },
             [](double v) { return FormatNumber(v * 100, 0) + L"%"; });
    RangeSpec pos;
    pos.min = 0;
    pos.max = 150;
    pos.step = 1;
    pos.origin = 0;
    AddRange(page, a, Tr(L"Vertical Position"), pos, [] { return Live().V().subPos; },
             [](double v) { TheApp().player().SetProperty("sub-pos", std::round(v)); },
             [](double v) { return FormatNumber(std::round(v), 0) + L"%"; });
    AddConfigToggle(page, a, Tr(L"Override Embedded Style"), "subtitles.assOverride");
    AddBox(
        page, a, Tr(L"Subtitle Font"),
        [] {
            const std::wstring f = Cfg().GetWString("subtitles.font");
            return f.empty() ? std::wstring(L"Sans-serif") : f;
        },
        nullptr, [](const std::wstring& t) { Cfg().SetWString("subtitles.font", t == L"Sans-serif" ? L"" : t); });
    auto color = [&](const wchar_t* label, const char* key) {
        auto box = std::make_unique<ColorBox>(key);
        ValueBox* b = box.get();
        AddRow(a, Tr(label), std::move(box), 52);
        page->OnSync([b] { b->Refresh(); });
        b->Refresh();
    };
    color(L"Text Color", "subtitles.color");
    color(L"Outline Color", "subtitles.outlineColor");
    AddToggle(page, a, Tr(L"Background Box"), [] { return Live().subBackground; },
              [](bool v) { Live().SetSubBackground(v); });
    return root;
}

// --- footer ----------------------------------------------------------------------------------

// "Default Preset ⌃": opens the preset menu above itself.
class PresetField : public Widget {
public:
    PresetField() {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(42);
        onClick = [this] { Open(); };
        conn_ = Connect(Live().changed, [this] { Invalidate(); });
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool open = menu_ && GetHost() && GetHost()->IsPopupOpen(menu_);
        p.Fill(rect, c.bgDeep, 6);
        if (IsHot() || open) p.Fill(rect.Inset(2), c.hover, 4);
        PaintFocusRing(p, rect, 6);
        const auto presets = Live().Presets();
        const int a = Live().activePreset;
        const std::wstring name =
            a >= 0 && a < static_cast<int>(presets.size()) ? presets[static_cast<size_t>(a)].name : Tr(L"Default Preset");
        const Color fg = IsHot() ? c.textPrimary : c.textSecondary;
        p.Text(name, {rect.x + 16, rect.y, rect.w - 60, rect.h}, TextStyle::Body, fg);
        p.Icon(open ? "chevron_down" : "chevron_up", {rect.Right() - 40, rect.Cy() - 12, 24, 24}, fg);
    }
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key == VK_UP || e.key == VK_DOWN) {
            Open();
            return true;
        }
        return false;
    }

private:
    void Open() {
        Host* h = GetHost();
        if (!h) return;
        if (menu_ && h->IsPopupOpen(menu_)) {
            h->ClosePopup(menu_);
            return;
        }
        const auto presets = Live().Presets();
        std::vector<MenuItem> items;
        MenuItem add{Tr(L"New Preset"), L"", "add"};
        add.action = [n = presets.size()] { Live().SavePreset(Fmt(Tr(L"Preset {0}"), static_cast<long long>(n + 1))); };
        items.push_back(add);
        for (size_t i = 0; i < presets.size(); ++i) {
            MenuItem m{presets[i].name, L"", "more"};
            m.checked = static_cast<int>(i) == Live().activePreset;
            m.action = [i] { Live().ApplyPreset(static_cast<int>(i)); };
            items.push_back(std::move(m));
        }
        if (Live().activePreset >= 0) {
            MenuItem del{Tr(L"Delete This Preset"), L"", "remove"};
            del.action = [] { Live().DeletePreset(Live().activePreset); };
            items.push_back(std::move(del));
        }
        items.push_back(MenuItem::Separator());
        MenuItem def{Tr(L"Default Preset")};
        def.checked = Live().activePreset < 0;
        def.action = [] { Live().ResetToDefaults(); };
        items.push_back(def);
        menu_ = ShowMenu(h, rect, std::move(items), Place::Above, rect.w);
    }
    Widget* menu_ = nullptr;
    ScopedConnection conn_;
};

class SettingsFooter : public Widget {
public:
    explicit SettingsFooter(Popover* owner) {
        width = Length::Grow();
        Add<PresetField>();
        adv_ = Add<FlatButton>(Tr(L"Open Advanced Settings"), "settings", false);
        adv_->onClick = [owner] {
            static const char* kCats[] = {"playback", "video", "audio", "subtitles"};
            const std::string cat = kCats[std::clamp(owner->CurrentTab(), 0, 3)];
            Later([cat] {
                ClosePlayerPanels();
                TheApp().OpenSettings(cat);
            });
        };
        SetCompact(false);
        owner->fitFooter = [this](float h) { SetCompact(h < 560); };
    }
    void Paint(Painter& p) override {
        p.Fill({rect.x, rect.y, rect.w, 1}, SectionLine());
    }

private:
    // Short popover (an 800x500 window): one row, the settings button shows its icon only, so
    // the footer takes 62 DIP instead of 125 and leaves the room to the scrolling rows.
    void SetCompact(bool on) {
        if (compact_ == (on ? 1 : 0)) return;
        compact_ = on ? 1 : 0;
        dir = on ? Dir::Row : Dir::Column;
        height = Length::Px(on ? 62.0f : 125.0f);
        pad = on ? Insets{20, 10, 20, 10} : Insets{20, 21, 20, 20};
        gap = on ? 8.0f : 10.0f;
        adv_->text = on ? L"" : Tr(L"Open Advanced Settings");
        adv_->tooltip = on ? Tr(L"Open Advanced Settings") : L"";
        adv_->width = on ? Length::Px(42) : Length::Grow();
        adv_->height = Length::Px(on ? 42.0f : 32.0f);
        adv_->filled = on;
    }
    FlatButton* adv_ = nullptr;
    int compact_ = -1;
};

}  // namespace

std::unique_ptr<Widget> BuildSettingsTab(int tab) {
    switch (tab) {
        case 1: return VideoTab();
        case 2: return AudioTab();
        case 3: return SubtitlesTab();
        default: return PlaybackTab();
    }
}

std::unique_ptr<Widget> BuildSettingsFooter(Popover* owner) {
    return std::make_unique<SettingsFooter>(owner);
}

}  // namespace app::panel

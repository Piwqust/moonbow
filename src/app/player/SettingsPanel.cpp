// The player's settings popovers, one per control bar button: playback (the gear and the speed
// button), video, audio and subtitles (those two start with the track lists of TracksPanel.cpp).
// Each is short; its footer opens the matching page of the Settings window.
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
#include "library/MediaTypes.h"

#include <algorithm>
#include <cmath>

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
    d->height = Length::Px(36);
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
    auto d = MakeDropdown(choices, fullWidth ? 360.0f : 196.0f);
    Dropdown* raw = d.get();
    if (fullWidth) {
        if (!label.empty()) AddCaption(section, label);
        d->width = Length::Grow();
        section->AddChild(std::move(d));
    } else {
        AddRow(section, label, std::move(d), 52);
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
                  std::function<void(bool)> set, float h = 48) {
    auto t = std::make_unique<Toggle>(get());
    Toggle* raw = t.get();
    raw->onChange = std::move(set);
    AddRow(section, label, std::move(t), h);
    page->OnSync([raw, get] {
        if (raw->Value() != get()) raw->SetValue(get());
    });
    return raw;
}

Toggle* AddConfigToggle(SyncPage* page, Widget* section, const std::wstring& label, const char* key, float h = 48) {
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
    r->format = [fmt](float v) { return fmt(v); };
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

// "Label   −  0.000 s  +" on one row: nudged a step at a time or typed (the delays).
ValueBox* AddStepper(SyncPage* page, Widget* section, const std::wstring& label, double step, std::function<double()> get,
                     std::function<void(double)> set, std::function<std::wstring(double)> fmt) {
    auto group = std::make_unique<Widget>();
    group->dir = Dir::Row;
    group->align = Align::Center;
    group->gap = 4;
    auto* minus = group->Add<IconButton>("remove", L"−" + fmt(step), 32, 20);
    auto* box = group->Add<ValueBox>();
    box->text = [get, fmt] { return fmt(get()); };
    box->onValue = set;
    box->a11yName = label;
    auto* plus = group->Add<IconButton>("add", L"+" + fmt(step), 32, 20);
    minus->onClick = [get, set, step] { set(std::round((get() - step) / step) * step); };
    plus->onClick = [get, set, step] { set(std::round((get() + step) / step) * step); };
    AddRow(section, label, std::move(group), 52);
    page->OnSync([box] { box->Refresh(); });
    box->Refresh();
    return box;
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

// Dropdown over the tracks of one type. The list follows the file: tracks arrive after the
// panel opens, external subtitles get added.
Dropdown* AddTracks(SyncPage* page, Widget* section, const std::wstring& label, const wchar_t* type,
                    std::function<std::string()> get, std::function<void(const std::string&)> set) {
    auto d = MakeDropdown(TrackChoices(type), 196.0f);
    Dropdown* raw = d.get();
    AddRow(section, label, std::move(d), 52);
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

// Three or more equal buttons in a dark well (Flip: None | Horizontal | Vertical).
Segmented* AddSegments(SyncPage* page, Widget* section, const std::wstring& caption, std::vector<ui::Choice> choices,
                       std::function<int()> get, std::function<void(int)> set) {
    if (!caption.empty()) AddCaption(section, caption);
    auto* s = section->Add<Segmented>(std::move(choices), get());
    s->width = Length::Grow();
    s->height = Length::Px(36);
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

// --- per-file settings ------------------------------------------------------------------------

// What is remembered for the playing file, one "Label: value" line each, or what to expect.
std::wstring FileSettingsText() {
    if (!Live().FileSettingsOn()) return Tr(L"Track choices and delays are not remembered.");
    const library::FileOptions o = Live().FileSettingsStored();
    if (o.Empty())
        return Live().FileSettingsAll() ? Tr(L"Nothing is remembered for this file yet. What you change here is kept for it.")
                                        : Tr(L"Nothing is remembered for this file yet. Tracks and delays you change are kept for it.");
    std::wstring t;
    auto line = [&t](const wchar_t* label, const std::wstring& value) {
        if (!t.empty()) t += L'\n';
        t += Tr(label);
        t += L": ";
        t += value;
    };
    auto track = [](const wchar_t* type, int64_t id) -> std::wstring {
        if (id <= 0) return Tr(L"Off");
        int n = 0;
        for (const Track& tr : PS().tracks) {
            if (tr.type != type) continue;
            ++n;
            if (tr.id == id) return TrackName(tr, n);
        }
        return L"#" + std::to_wstring(id);
    };
    if (o.audioFile) line(L"Audio Track", library::FileNameOf(*o.audioFile));
    else if (o.aid) line(L"Audio Track", track(L"audio", *o.aid));
    if (o.subFile) line(L"Subtitle Track", library::FileNameOf(*o.subFile));
    else if (o.sid) line(L"Subtitle Track", track(L"sub", *o.sid));
    if (o.secondarySid) line(L"Secondary Subtitles", track(L"sub", *o.secondarySid));
    if (o.audioDelay) line(L"Audio Track Delay", FormatSeconds(*o.audioDelay));
    if (o.subDelay) line(L"Subtitle Track Delay", FormatSeconds(*o.subDelay));
    if (o.lyricsDelay) line(L"Lyrics Shift", FormatSeconds(*o.lyricsDelay, 1));
    if (o.speed) line(L"Speed", FormatNumber(*o.speed) + L"x");
    if (o.aspect) line(L"Aspect Ratio", Utf8ToWide(*o.aspect));
    if (o.zoomMode) {
        const std::string& m = *o.zoomMode;
        line(L"Zoom", Tr(m == "quarter" ? L"1:4 Quarter" : m == "half" ? L"1:2 Half" : m == "double" ? L"2:1 Double" : L"1:1 Original"));
    } else if (o.zoom) {
        line(L"Zoom", FormatNumber(std::pow(2.0, *o.zoom) * 100, 0) + L"%");
    }
    if (o.panX || o.panY) line(L"Pan", L"x " + FormatNumber(o.panX.value_or(0)) + L", y " + FormatNumber(o.panY.value_or(0)));
    if (o.rotate) line(L"Rotate", FormatNumber(*o.rotate) + L"°");
    if (o.flip) line(L"Flip", Tr(*o.flip == 2 ? L"Vertical" : L"Horizontal"));
    if (o.deinterlace) line(L"Deinterlace", Tr(*o.deinterlace == "yes" ? L"On" : *o.deinterlace == "no" ? L"Off" : L"Auto"));
    if (o.abA || o.abB) {
        std::wstring ab;
        if (o.abA) ab = L"A " + FormatTime(*o.abA, true);
        if (o.abB) ab += (ab.empty() ? L"" : L" – ") + std::wstring(L"B ") + FormatTime(*o.abB, true);
        line(L"A-B Loop", ab);
    }
    if (o.subPos) line(L"Subtitle Position", FormatNumber(std::round(*o.subPos), 0) + L"%");
    if (o.subScale) line(L"Subtitle Size", FormatNumber(*o.subScale * 100, 0) + L"%");
    return t;
}

std::unique_ptr<Widget> Page(SyncPage** out) {
    auto sv = std::make_unique<ScrollView>();
    sv->width = Length::Grow();
    sv->height = Length::Grow();
    *out = sv->SetContent<SyncPage>();
    return sv;
}

// --- panels: one short popover per bar button ------------------------------------------------
// What is used while watching sits here; the rest (quality and HDR, equalizer bands, effects,
// subtitle style, shaders) is in the Settings window, one link away in each footer.

// Quick speeds as segments over the exact value box.
constexpr double kQuickSpeeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};

int QuickSpeedIndex() {
    for (size_t i = 0; i < std::size(kQuickSpeeds); ++i)
        if (std::abs(PS().speed - kQuickSpeeds[i]) < 1e-3) return static_cast<int>(i);
    return -1;
}

std::unique_ptr<Widget> PlaybackPanel() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    auto* top = page->Add<Widget>();
    top->height = Length::Px(8);
    Widget* sp = AddSection(page);
    AddBox(
        page, sp, Tr(L"Speed"), [] { return FormatNumber(PS().speed) + L"×"; },
        [](double v) { TheApp().player().SetSpeed(std::clamp(v, 0.05, 8.0)); });
    std::vector<ui::Choice> speeds;
    for (double v : kQuickSpeeds) speeds.push_back({FormatNumber(v) + L"×", "", ""});
    AddSegments(page, sp, L"", std::move(speeds), [] { return QuickSpeedIndex(); },
                [](int i) { TheApp().player().SetSpeed(kQuickSpeeds[static_cast<size_t>(std::clamp(i, 0, 5))]); });

    AddSectionLine(page);
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

    // What is remembered for this file, only when there is something (playback.perFile is in
    // Settings > Playback).
    Widget* fs = AddSection(page);
    auto* stored = fs->Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
    stored->pad = {4, 8, 0, 0};
    stored->wrap = true;
    auto* reset = fs->Add<FlatButton>(Tr(L"Reset for This File"), "refresh");
    reset->tooltip = Tr(L"Forget what is remembered for this file and go back to the defaults");
    reset->onClick = [] { Live().ResetFileSettings(); };
    auto* fsbottom = fs->Add<Widget>();
    fsbottom->height = Length::Px(12);
    auto syncStored = [stored, reset, fsbottom] {
        const bool any = Live().FileSettingsOn() && !Live().FileSettingsStored().Empty();
        const std::wstring t = any ? Tr(L"Remembered for this file:") + std::wstring(L"\n") + FileSettingsText() : L"";
        if (t != stored->Text()) stored->SetText(t);
        if (reset->visible != any) {
            reset->visible = stored->visible = fsbottom->visible = any;
            reset->InvalidateLayout();
        }
    };
    reset->visible = stored->visible = fsbottom->visible = false;
    syncStored();
    page->OnSync(syncStored);
    return root;
}

std::unique_ptr<Widget> VideoPanel() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    // The track list only when there is a choice (most files have one picture).
    int videoTracks = 0;
    for (const Track& t : PS().tracks) videoTracks += t.type == L"video";
    if (videoTracks > 1) {
        AddTrackLists(page, L"video");
        AddSectionLine(page);
    }
    auto* top = page->Add<Widget>();
    top->height = Length::Px(8);
    Widget* s = AddSection(page);
    // Rotate by quarter turns; the exact angle shows between the buttons.
    {
        auto* row = s->Add<Widget>();
        row->dir = Dir::Row;
        row->align = Align::Center;
        row->width = Length::Grow();
        row->height = Length::Px(52);
        row->gap = 8;
        auto* l = row->Add<Label>(Tr(L"Rotate"), TextStyle::Body, Tone::Tertiary);
        l->width = Length::Grow();
        l->pad = {4, 0, 0, 0};
        auto turn = [](int by) {
            const int r = ((static_cast<int>(std::lround(Live().V().rotate)) + by) % 360 + 360) % 360;
            TheApp().player().SetProperty("video-rotate", std::to_string(r));  // a choice option: mpv rejects a double
        };
        auto* left = row->Add<Button>(L"−90°", ButtonKind::Secondary);
        left->tooltip = Tr(L"Rotate left");
        left->onClick = [turn] { turn(-90); };
        auto* angle = row->Add<Label>(L"", TextStyle::Mono, Tone::Secondary);
        angle->width = Length::Px(44);
        angle->halign = HAlign::Center;
        auto* right = row->Add<Button>(L"+90°", ButtonKind::Secondary);
        right->tooltip = Tr(L"Rotate right");
        right->onClick = [turn] { turn(90); };
        auto sync = [angle] { angle->SetText(FormatNumber(Live().V().rotate) + L"°"); };
        sync();
        page->OnSync(sync);
    }
    AddChoice(page, s, Tr(L"Flip"), {{Tr(L"None"), "0", ""}, {Tr(L"Horizontal"), "1", ""}, {Tr(L"Vertical"), "2", ""}},
              [] { return std::to_string(Live().flip); }, [](const std::string& v) { Live().SetFlip(std::atoi(v.c_str())); });
    AddChoice(page, s, Tr(L"Zoom"),
              {{Tr(L"Fit to Window"), "fit", ""},
               {Tr(L"1:4 Quarter"), "quarter", ""},
               {Tr(L"1:2 Half"), "half", ""},
               {Tr(L"1:1 Original"), "original", ""},
               {Tr(L"2:1 Double"), "double", ""}},
              [] { return Live().zoomMode; }, [](const std::string& v) { Live().SetZoomMode(v); });
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
    // Custom crop: the four edges in pixels, only while "Custom" is chosen.
    {
        auto* edges = s->Add<Widget>();
        edges->dir = Dir::Column;
        edges->width = Length::Grow();
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
            auto* row = edges->Add<Widget>();
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
        auto sync = [edges] {
            const bool on = Live().crop == "custom";
            if (edges->visible != on) {
                edges->visible = on;
                edges->InvalidateLayout();
            }
        };
        edges->visible = false;
        sync();
        page->OnSync(sync);
    }
    AddConfigChoice(page, s, Tr(L"Quality"), "video.quality");
    {
        auto* note = s->Add<Label>(L"", TextStyle::Small, Tone::Tertiary);
        note->pad = {4, 4, 0, 0};
        note->wrap = true;
        auto sync = [note] {
            const std::wstring t =
                Live().FileSettingsAll()
                    ? Tr(L"Flip, rotation, zoom and aspect ratio apply to the current file only and are remembered for it.")
                    : Tr(L"Flip, rotation, zoom and aspect ratio apply to the current file only.");
            if (t != note->Text()) note->SetText(t);
        };
        sync();
        page->OnSync(sync);
    }
    auto* gap = s->Add<Widget>();
    gap->height = Length::Px(12);

    // Picture adjustments, folded: brightness and the like, the colour effects and a reset.
    AddSectionLine(page);
    Widget* f = AddSection(page);
    auto* fgap = f->Add<Widget>();
    fgap->height = Length::Px(10);
    Expander* pic = AddExpander(f, "video.picture", L"", Tr(L"Picture adjustments"));
    {
        Widget* b = pic->Body();
        RangeSpec spec;
        spec.min = -100;
        spec.max = 100;
        spec.step = 1;
        spec.origin = 0;
        auto prop = [&](const wchar_t* label, const char* name, double LiveValues::*field) {
            const std::string n = name;
            AddRange(page, b, Tr(label), spec, [field] { return Live().V().*field; },
                     [n](double v) { TheApp().player().SetProperty(n.c_str(), std::round(v)); },
                     [](double v) { return FormatNumber(std::round(v)); });
        };
        prop(L"Brightness", "brightness", &LiveValues::brightness);
        prop(L"Contrast", "contrast", &LiveValues::contrast);
        prop(L"Saturation", "saturation", &LiveValues::saturation);
        prop(L"Gamma", "gamma", &LiveValues::gamma);
        prop(L"Hue", "hue", &LiveValues::hue);
        AddToggle(page, b, Tr(L"Negate Colors"), [] { return Live().negate; },
                  [](bool v) { Live().SetColorFx(v, Live().grayscale); }, 48);
        AddToggle(page, b, Tr(L"Grayscale"), [] { return Live().grayscale; },
                  [](bool v) { Live().SetColorFx(Live().negate, v); }, 48);
        auto* reset = b->Add<FlatButton>(Tr(L"Reset picture"), "refresh");
        reset->onClick = [] {
            Live().ResetPicture();
            Live().SetColorFx(false, false);
        };
    }
    return root;
}

std::unique_ptr<Widget> AudioPanel() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    AddTrackLists(page, L"audio");
    AddSectionLine(page);
    Widget* s = AddSection(page);
    auto* top = s->Add<Widget>();
    top->height = Length::Px(8);
    AddStepper(page, s, Tr(L"Audio Track Delay"), 0.05, [] { return Live().V().audioDelay; },
               [](double v) { TheApp().player().SetProperty("audio-delay", v); }, [](double v) { return FormatSeconds(v); });
    // The equalizer as its presets; the bands are in Settings > Audio.
    AddConfigChoice(page, s, Tr(L"Equalizer"), "audio.eqPreset", [](const std::string& id) {
        Cfg().ApplyEqPreset(id);
        Cfg().SetBool("audio.eq", id != "none");
    });
    AddConfigToggle(page, s, Tr(L"Normalize Loudness"), "audio.normalize");
    AddChoice(page, s, Tr(L"Stereo Mode"),
              {{Tr(L"Stereo"), "stereo", ""},
               {Tr(L"Mono"), "mono", ""},
               {Tr(L"Reverse stereo"), "reverse", ""},
               {Tr(L"Left channel"), "left", ""},
               {Tr(L"Right channel"), "right", ""}},
              [] { return Live().stereoMode; }, [](const std::string& v) { Live().SetStereoMode(v); });
    auto* bottom = s->Add<Widget>();
    bottom->height = Length::Px(12);
    return root;
}

std::unique_ptr<Widget> SubtitlesPanel() {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    AddTrackLists(page, L"sub");
    AddSectionLine(page);
    Widget* s = AddSection(page);
    auto* top = s->Add<Widget>();
    top->height = Length::Px(8);
    AddToggle(page, s, Tr(L"Show Subtitles"), [] { return Live().V().subVisible; },
              [](bool v) { TheApp().player().SetProperty("sub-visibility", v); });
    AddTracks(
        page, s, Tr(L"Secondary Subtitles"), L"sub", [] { return std::to_string(Live().V().secondarySid); },
        [](const std::string& v) {
            TheApp().player().SetProperty("secondary-sid", std::string(v == "0" ? "no" : v));
        });
    AddStepper(page, s, Tr(L"Subtitle Track Delay"), 0.05, [] { return Live().V().subDelay; },
               [](double v) { TheApp().player().SetProperty("sub-delay", v); }, [](double v) { return FormatSeconds(v); });
    RangeSpec size;
    size.min = 0.5f;
    size.max = 3;
    size.step = 0.05f;
    size.origin = 0.5f;
    AddRange(page, s, Tr(L"Subtitle Size"), size, [] { return Live().V().subScale; },
             [](double v) { TheApp().player().SetProperty("sub-scale", v); },
             [](double v) { return FormatNumber(v * 100, 0) + L"%"; });
    RangeSpec pos;
    pos.min = 0;
    pos.max = 150;
    pos.step = 1;
    pos.origin = 0;
    AddRange(page, s, Tr(L"Vertical Position"), pos, [] { return Live().V().subPos; },
             [](double v) { TheApp().player().SetProperty("sub-pos", std::round(v)); },
             [](double v) { return FormatNumber(std::round(v), 0) + L"%"; });
    // For subtitles made for another frame rate; one row, typed.
    AddBox(
        page, s, Tr(L"Subtitle Speed"), [] { return Signed(Live().V().subSpeed, 3, L"×"); },
        [](double v) { TheApp().player().SetProperty("sub-speed", std::clamp(v, 0.1, 10.0)); });
    auto* bottom = s->Add<Widget>();
    bottom->height = Length::Px(12);
    return root;
}

// --- footer ----------------------------------------------------------------------------------

// "Settings preset: standard ⌃": opens the preset menu above itself.
class PresetField : public Widget {
public:
    PresetField() {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(42);
        onClick = [this] { Open(); };
        conn_ = Connect(Live().changed, [this] {
            ReadName();
            Invalidate();
        });
        ReadName();
    }
    void Paint(Painter& p) override {
        const Palette& c = P();
        const bool open = menu_ && GetHost() && GetHost()->IsPopupOpen(menu_);
        p.Fill(rect, c.bgDeep, 6);
        if (IsHot() || open) p.Fill(rect.Inset(2), c.hover, 4);
        PaintFocusRing(p, rect, 6);
        const Color fg = IsHot() ? c.textPrimary : c.textSecondary;
        // "Settings preset: standard": the bare "Default Preset" with a chevron didn't say what it was.
        p.Text(Fmt(Tr(L"Settings preset: {0}"), {name_}), {rect.x + 16, rect.y, rect.w - 60, rect.h}, TextStyle::Body, fg);
        tooltip = Tr(L"Save these settings as a preset or switch to another one");
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
    // On a change, not on every paint: Presets() copies the list.
    void ReadName() {
        const auto presets = Live().Presets();
        const int a = Live().activePreset;
        name_ = a >= 0 && a < static_cast<int>(presets.size()) ? presets[static_cast<size_t>(a)].name : Tr(L"standard");
    }
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
    std::wstring name_;
    ScopedConnection conn_;
};

// The bottom row: "All audio settings" opening that page of the Settings window; the playback
// panel has the preset field beside a settings button instead.
class PanelFooter : public Widget {
public:
    PanelFooter(std::string category, const wchar_t* label, bool presets) {
        width = Length::Grow();
        dir = Dir::Row;
        align = Align::Center;
        height = Length::Px(62);
        pad = {20, 10, 20, 10};
        gap = 8;
        if (presets) Add<PresetField>();
        auto* open = Add<FlatButton>(presets ? L"" : Tr(label), "settings", presets);
        open->tooltip = presets ? Tr(label) : L"";
        open->width = presets ? Length::Px(42) : Length::Grow();
        open->height = Length::Px(42);
        open->onClick = [category] {
            Later([category] {
                ClosePlayerPanels();
                TheApp().OpenSettings(category);
            });
        };
    }
    void Paint(Painter& p) override { p.Fill({rect.x, rect.y, rect.w, 1}, SectionLine()); }
};

}  // namespace

std::unique_ptr<Widget> BuildSettingsPanel(PanelKind kind) {
    switch (kind) {
        case PanelKind::Tracks: return AudioPanel();
        case PanelKind::Subtitles: return SubtitlesPanel();
        case PanelKind::Video: return VideoPanel();
        default: return PlaybackPanel();
    }
}

std::unique_ptr<Widget> BuildSettingsFooter(PanelKind kind) {
    switch (kind) {
        case PanelKind::Tracks: return std::make_unique<PanelFooter>("audio", L"All audio settings", false);
        case PanelKind::Subtitles: return std::make_unique<PanelFooter>("subtitles", L"All subtitle settings", false);
        case PanelKind::Video: return std::make_unique<PanelFooter>("video", L"All video settings", false);
        default: return std::make_unique<PanelFooter>("playback", L"All playback settings", true);
    }
}

}  // namespace app::panel

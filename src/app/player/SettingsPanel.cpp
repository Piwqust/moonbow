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
#include "app/player/SubStyle.h"
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

// --- panels: the quiet list -------------------------------------------------------------------
// Each panel is a short list: the tracks as radio rows, then rows with an icon, a name and the
// current value in plain words; a row opens its page in the same popover (a back arrow returns).
// The fine settings (EQ bands, effects, subtitle style, shaders) are in the Settings window.

// A row opening page `id` of this panel; its value follows the player.
ListRow* NavRow(SyncPage* page, const char* icon, const wchar_t* label, std::function<std::wstring()> value, int id) {
    auto* r = page->Add<ListRow>(icon, Tr(label), std::move(value), true);
    r->onClick = [id] { ShowPanelPage(id); };
    page->OnSync([r] { r->Invalidate(); });
    return r;
}

struct Opt {
    std::wstring label, detail;
    std::string value;
};

// An inner page's choices as a radio list; a choice applies at once and goes back to the list,
// where the row now shows it (as YouTube's menu does).
void AddOptions(SyncPage* page, std::vector<Opt> opts, std::function<std::string()> get,
                std::function<void(const std::string&)> set, std::wstring name, bool back = true) {
    AddChoiceList(
        page,
        [opts, get, set, back] {
            std::vector<ChoiceRow> rows;
            const std::string cur = get();
            for (const Opt& o : opts) {
                ChoiceRow r;
                r.label = o.label;
                r.detail = o.detail;
                r.checked = o.value == cur;
                r.select = [set, v = o.value, back] {
                    set(v);
                    if (back) ShowPanelPage(0);
                };
                rows.push_back(std::move(r));
            }
            return rows;
        },
        std::move(name));
}

std::wstring LabelOf(const std::vector<Opt>& opts, const std::string& value) {
    for (const Opt& o : opts)
        if (o.value == value) return o.label;
    return {};
}

void Gap(Widget* page, float h) {
    page->Add<Widget>()->height = Length::Px(h);
}

// A hint in small type under a control.
Label* AddHint(Widget* section, const std::wstring& text) {
    auto* l = section->Add<Label>(text, TextStyle::Small, Tone::Tertiary);
    l->wrap = true;
    l->pad = {4, 2, 0, 6};
    return l;
}

// "in sync", or the shift in milliseconds ("+120 ms").
std::wstring SyncText(double seconds) {
    if (std::abs(seconds) < 0.0005) return Tr(L"in sync");
    const long long ms = std::llround(seconds * 1000);
    return std::wstring(ms > 0 ? L"+" : L"−") + std::to_wstring(std::llabs(ms)) + L" " + Tr(L"ms");
}

// A sync page: the shift between − and +, what the buttons do, and a reset while shifted.
void AddSyncPage(SyncPage* page, const wchar_t* label, const char* prop, double LiveValues::*field, const wchar_t* hint) {
    Gap(page, 8);
    Widget* s = AddSection(page);
    const std::string pr = prop;
    AddStepper(page, s, Tr(label), 0.05, [field] { return Live().V().*field; },
               [pr](double v) { TheApp().player().SetProperty(pr.c_str(), v); }, [](double v) { return FormatSeconds(v); });
    AddHint(s, Tr(hint));
    auto* reset = page->Add<ListRow>("refresh", Tr(L"Reset"), nullptr, false);
    reset->onClick = [pr] { TheApp().player().SetProperty(pr.c_str(), 0.0); };
    auto sync = [reset, field] {
        const bool on = std::abs(Live().V().*field) >= 0.0005;
        if (reset->visible != on) {
            reset->visible = on;
            reset->InvalidateLayout();
        }
    };
    reset->visible = false;
    sync();
    page->OnSync(sync);
}

// --- Audio ---

enum AudioPage { kAudioLoudness = 1, kAudioEq, kAudioSync, kAudioOutput, kAudioChannels };

std::vector<Opt> LoudnessOpts() {
    return {{Tr(L"As in the film"), Tr(L"Quiet and loud scenes as they were mixed"), "film"},
            {Tr(L"Even"), Tr(L"Quiet scenes louder, loud ones softer"), "even"},
            {Tr(L"At night"), Tr(L"No sudden bangs, speech stays clear"), "night"}};
}
std::string LoudnessMode() {
    return Cfg().GetBool("audio.nightMode") ? "night" : Cfg().GetBool("audio.normalize") ? "even" : "film";
}
void SetLoudnessMode(const std::string& m) {
    Cfg().SetBool("audio.normalize", m == "even");
    Cfg().SetBool("audio.nightMode", m == "night");
}

std::vector<Opt> SchemaOpts(const char* key) {
    std::vector<Opt> out;
    if (const settings::Item* item = settings::FindItem(key))
        for (const settings::Choice& c : item->choices) out.push_back({c.Label(Lang()), L"", c.value});
    return out;
}
std::string EqValue() {
    return Cfg().GetBool("audio.eq") ? Cfg().GetString("audio.eqPreset") : "none";
}

std::vector<Opt> ChannelOpts() {
    return {{Tr(L"Stereo"), L"", "stereo"},
            {Tr(L"Mono"), Tr(L"Both speakers play the same"), "mono"},
            {Tr(L"Reverse stereo"), Tr(L"Left and right swapped"), "reverse"},
            {Tr(L"Left channel"), L"", "left"},
            {Tr(L"Right channel"), L"", "right"}};
}

std::wstring DeviceName() {
    for (const ChoiceRow& r : OutputDeviceRows())
        if (r.checked) return r.label;
    return Tr(L"System default");
}

std::unique_ptr<Widget> AudioPanel(int id) {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    switch (id) {
        case kAudioLoudness: AddOptions(page, LoudnessOpts(), LoudnessMode, SetLoudnessMode, Tr(L"Loudness")); break;
        case kAudioEq: {
            AddOptions(
                page, SchemaOpts("audio.eqPreset"), EqValue,
                [](const std::string& v) {
                    Cfg().ApplyEqPreset(v);
                    Cfg().SetBool("audio.eq", v != "none");
                },
                Tr(L"Equalizer"));
            auto* bands = page->Add<ListRow>("settings", Tr(L"Adjust the bands in Settings"), nullptr, false);
            bands->onClick = [] {
                Later([] {
                    ClosePlayerPanels();
                    TheApp().OpenSettings("audio");
                });
            };
            break;
        }
        case kAudioSync:
            AddSyncPage(page, L"Audio Track Delay", "audio-delay", &LiveValues::audioDelay,
                        L"Lips move before you hear the words? Press −. After them? Press +.");
            break;
        case kAudioOutput:
            Live().RefreshDevices();
            AddChoiceList(
                page,
                [] {
                    std::vector<ChoiceRow> rows = OutputDeviceRows();
                    for (ChoiceRow& r : rows)
                        r.select = [sel = r.select] {
                            if (sel) sel();
                            ShowPanelPage(0);
                        };
                    return rows;
                },
                Tr(L"Output Device"));
            break;
        case kAudioChannels:
            AddOptions(page, ChannelOpts(), [] { return Live().stereoMode; },
                       [](const std::string& v) { Live().SetStereoMode(v); }, Tr(L"Channels"));
            break;
        default:
            AddTrackLists(page, L"audio");
            AddSectionLine(page);
            Gap(page, 4);
            NavRow(page, "volume_value-high", L"Loudness", [] { return LabelOf(LoudnessOpts(), LoudnessMode()); }, kAudioLoudness);
            NavRow(page, "equalizer", L"Equalizer", [] {
                const std::string v = EqValue();
                return v == "none" ? std::wstring(Tr(L"Off")) : LabelOf(SchemaOpts("audio.eqPreset"), v);
            }, kAudioEq);
            NavRow(page, "recent", L"Synchronization", [] { return SyncText(Live().V().audioDelay); }, kAudioSync);
            NavRow(page, "audio-tracks", L"Output", DeviceName, kAudioOutput);
            NavRow(page, "swap", L"Channels", [] { return LabelOf(ChannelOpts(), Live().stereoMode); }, kAudioChannels);
            break;
    }
    Gap(page, 8);
    return root;
}

// --- Subtitles ---

enum SubPage { kSubSync = 1, kSubSize, kSubPosition, kSubSecond, kSubStyle };

std::wstring SecondName() {
    const int64_t id = Live().V().secondarySid;
    if (id <= 0) return Tr(L"None");
    for (const ui::Choice& c : TrackChoices(L"sub"))
        if (c.value == std::to_string(id)) return c.label;
    return Tr(L"None");
}

std::unique_ptr<Widget> SubtitlesPanel(int id) {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    switch (id) {
        case kSubSync: {
            AddSyncPage(page, L"Subtitle Track Delay", "sub-delay", &LiveValues::subDelay,
                        L"Words show before they are said? Press +. After? Press −.");
            Widget* s = AddSection(page);
            // For subtitles made for another frame rate; typed.
            AddBox(
                page, s, Tr(L"Subtitle Speed"), [] { return Signed(Live().V().subSpeed, 3, L"×"); },
                [](double v) { TheApp().player().SetProperty("sub-speed", std::clamp(v, 0.1, 10.0)); });
            break;
        }
        case kSubSize: {
            Gap(page, 8);
            Widget* s = AddSection(page);
            RangeSpec size;
            size.min = 0.5f;
            size.max = 3;
            size.step = 0.05f;
            size.origin = 0.5f;
            AddRange(page, s, Tr(L"Subtitle Size"), size, [] { return Live().V().subScale; },
                     [](double v) { TheApp().player().SetProperty("sub-scale", v); },
                     [](double v) { return FormatNumber(v * 100, 0) + L"%"; });
            break;
        }
        case kSubPosition: {
            Gap(page, 8);
            Widget* s = AddSection(page);
            RangeSpec pos;
            pos.min = 0;
            pos.max = 150;
            pos.step = 1;
            pos.origin = 0;
            AddRange(page, s, Tr(L"Vertical Position"), pos, [] { return Live().V().subPos; },
                     [](double v) { TheApp().player().SetProperty("sub-pos", std::round(v)); },
                     [](double v) { return FormatNumber(std::round(v), 0) + L"%"; });
            AddHint(s, Tr(L"100 % is the bottom of the picture."));
            break;
        }
        case kSubStyle: {
            // The video is the example: a pick restyles it at once.
            AddChoiceList(
                page,
                [] {
                    std::vector<ChoiceRow> rows;
                    const SubtitlePreset* now = MatchSubtitlePreset(Cfg());
                    for (const SubtitlePreset& p : SubtitlePresets()) {
                        ChoiceRow r;
                        r.label = Tr(p.name);
                        r.detail = Tr(p.detail);
                        r.checked = now == &p;
                        r.select = [&p] { ApplySubtitlePreset(Cfg(), p); };
                        rows.push_back(std::move(r));
                    }
                    return rows;
                },
                Tr(L"Subtitle style"));
            break;
        }
        case kSubSecond:
            AddChoiceList(
                page,
                [] {
                    std::vector<ChoiceRow> rows;
                    const std::string cur = std::to_string(std::max<int64_t>(0, Live().V().secondarySid));
                    for (const ui::Choice& c : TrackChoices(L"sub")) {
                        ChoiceRow r;
                        r.label = c.label;
                        r.checked = c.value == cur;
                        r.select = [v = c.value] {
                            TheApp().player().SetProperty("secondary-sid", std::string(v == "0" ? "no" : v));
                            ShowPanelPage(0);
                        };
                        rows.push_back(std::move(r));
                    }
                    return rows;
                },
                Tr(L"Secondary Subtitles"));
            break;
        default:
            AddTrackLists(page, L"sub");
            AddSectionLine(page);
            Gap(page, 4);
            NavRow(page, "edit", L"Style", [] {
                const SubtitlePreset* p = MatchSubtitlePreset(Cfg());
                return std::wstring(Tr(p ? p->name : L"Your own"));
            }, kSubStyle);
            NavRow(page, "recent", L"Synchronization", [] { return SyncText(Live().V().subDelay); }, kSubSync);
            NavRow(page, "subtitle", L"Size", [] { return FormatNumber(Live().V().subScale * 100, 0) + L"%"; }, kSubSize);
            NavRow(page, "arrow_up", L"Position", [] { return FormatNumber(std::round(Live().V().subPos), 0) + L"%"; },
                   kSubPosition);
            NavRow(page, "subtitles", L"Secondary Subtitles", SecondName, kSubSecond);
            break;
    }
    Gap(page, 8);
    return root;
}

// --- Video ---

enum VideoPage { kVideoQuality = 1, kVideoZoom, kVideoAspect, kVideoCrop, kVideoTurn, kVideoPicture };

std::vector<Opt> QualityOpts() {
    return {{Tr(L"Fast"), Tr(L"For laptops and older graphics cards"), "fast"},
            {Tr(L"Balanced"), Tr(L"A good picture without strain"), "balanced"},
            {Tr(L"Best"), Tr(L"The finest picture; needs a strong graphics card"), "high"}};
}
std::vector<Opt> ZoomOpts() {
    return {{Tr(L"Fit to Window"), L"", "fit"},
            {Tr(L"1:4 Quarter"), L"", "quarter"},
            {Tr(L"1:2 Half"), L"", "half"},
            {Tr(L"1:1 Original"), L"", "original"},
            {Tr(L"2:1 Double"), L"", "double"}};
}
std::vector<Opt> AspectOpts() {
    std::vector<Opt> o{{Tr(L"As in the file"), L"", "auto"}};
    for (const wchar_t* a : {L"16:9", L"4:3", L"21:9", L"2.35:1", L"16:10", L"5:4", L"1:1"}) o.push_back({a, L"", WideToUtf8(a)});
    return o;
}
std::vector<Opt> CropOpts() {
    std::vector<Opt> o{{Tr(L"None"), L"", "none"}};
    for (const wchar_t* a : {L"16:9", L"4:3", L"21:9", L"2.35:1"}) o.push_back({a, L"", WideToUtf8(a)});
    o.push_back({Tr(L"Custom"), Tr(L"Set each edge in pixels"), "custom"});
    return o;
}
std::wstring TurnText() {
    std::wstring t = FormatNumber(Live().V().rotate) + L"°";
    if (Live().flip == 1) t += L" · " + std::wstring(Tr(L"mirrored"));
    if (Live().flip == 2) t += L" · " + std::wstring(Tr(L"upside down"));
    return t;
}
bool PictureChanged() {
    const LiveValues& v = Live().V();
    return v.brightness != 0 || v.contrast != 0 || v.saturation != 0 || v.gamma != 0 || v.hue != 0 || Live().negate ||
           Live().grayscale;
}

void AddCropEdges(SyncPage* page, Widget* s) {
    auto* edges = s->Add<Widget>();
    edges->dir = Dir::Column;
    edges->width = Length::Grow();
    auto field = [page](Widget* row, const wchar_t* label, double LiveState::*value) {
        auto* cell = row->Add<Widget>();
        cell->dir = Dir::Row;
        cell->align = Align::Center;
        cell->width = Length::Grow();
        auto* l = cell->Add<Label>(Tr(label), TextStyle::Body, Tone::Secondary);
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

std::unique_ptr<Widget> VideoPanel(int id) {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    switch (id) {
        case kVideoQuality:
            AddOptions(page, QualityOpts(), [] { return Cfg().GetString("video.quality"); },
                       [](const std::string& v) { Cfg().SetString("video.quality", v); }, Tr(L"Quality"));
            break;
        case kVideoZoom:
            AddOptions(page, ZoomOpts(), [] { return Live().zoomMode; }, [](const std::string& v) { Live().SetZoomMode(v); },
                       Tr(L"Zoom"));
            break;
        case kVideoAspect:
            AddOptions(page, AspectOpts(), [] { return Live().aspect; }, [](const std::string& v) { Live().SetAspect(v); },
                       Tr(L"Aspect Ratio"));
            break;
        case kVideoCrop: {
            // Custom stays on the page: its edges appear under the list.
            AddOptions(
                page, CropOpts(), [] { return Live().crop; },
                [](const std::string& v) {
                    Live().SetCrop(v);
                    if (v != "custom") ShowPanelPage(0);
                },
                Tr(L"Crop"), false);
            Widget* s = AddSection(page);
            AddCropEdges(page, s);
            break;
        }
        case kVideoTurn: {
            Gap(page, 8);
            Widget* s = AddSection(page);
            auto row = [&](const wchar_t* label) {
                auto* r = s->Add<Widget>();
                r->dir = Dir::Row;
                r->align = Align::Center;
                r->width = Length::Grow();
                r->height = Length::Px(48);
                r->gap = 4;
                auto* l = r->Add<Label>(Tr(label), TextStyle::Body, Tone::Secondary);
                l->width = Length::Grow();
                l->pad = {4, 0, 0, 0};
                return r;
            };
            Widget* rot = row(L"Rotate");
            auto turn = [](int by) {
                const int r = ((static_cast<int>(std::lround(Live().V().rotate)) + by) % 360 + 360) % 360;
                TheApp().player().SetProperty("video-rotate", std::to_string(r));  // a choice option: mpv rejects a double
            };
            auto* left = rot->Add<IconButton>("rotate-left", Tr(L"Rotate left"), 36, 22);
            left->onClick = [turn] { turn(-90); };
            auto* angle = rot->Add<Label>(L"", TextStyle::Mono, Tone::Primary);
            angle->width = Length::Px(44);
            angle->halign = HAlign::Center;
            auto* right = rot->Add<IconButton>("rotate-right", Tr(L"Rotate right"), 36, 22);
            right->onClick = [turn] { turn(90); };
            Widget* flip = row(L"Flip");
            auto* h = flip->Add<IconButton>("flip-horizontal", Tr(L"Horizontal"), 36, 22);
            h->onClick = [] { Live().SetFlip(Live().flip == 1 ? 0 : 1); };
            auto* v = flip->Add<IconButton>("flip-vertical", Tr(L"Vertical"), 36, 22);
            v->onClick = [] { Live().SetFlip(Live().flip == 2 ? 0 : 2); };
            auto sync = [angle, h, v] {
                angle->SetText(FormatNumber(Live().V().rotate) + L"°");
                h->active = Live().flip == 1;
                v->active = Live().flip == 2;
                h->Invalidate();
            };
            sync();
            page->OnSync(sync);
            break;
        }
        case kVideoPicture: {
            Gap(page, 8);
            Widget* b = AddSection(page);
            RangeSpec spec;
            spec.min = -100;
            spec.max = 100;
            spec.step = 1;
            spec.origin = 0;
            auto prop = [&](const wchar_t* label, const char* name, double LiveValues::*field) {
                const std::string n = name;
                AddRange(page, b, Tr(label), spec, [field] { return Live().V().*field; },
                         [n](double val) { TheApp().player().SetProperty(n.c_str(), std::round(val)); },
                         [](double val) { return FormatNumber(std::round(val)); });
            };
            prop(L"Brightness", "brightness", &LiveValues::brightness);
            prop(L"Contrast", "contrast", &LiveValues::contrast);
            prop(L"Saturation", "saturation", &LiveValues::saturation);
            prop(L"Gamma", "gamma", &LiveValues::gamma);
            prop(L"Hue", "hue", &LiveValues::hue);
            AddToggle(page, b, Tr(L"Negate Colors"), [] { return Live().negate; },
                      [](bool on) { Live().SetColorFx(on, Live().grayscale); });
            AddToggle(page, b, Tr(L"Grayscale"), [] { return Live().grayscale; },
                      [](bool on) { Live().SetColorFx(Live().negate, on); });
            auto* reset = page->Add<ListRow>("refresh", Tr(L"Reset picture"), nullptr, false);
            reset->onClick = [] {
                Live().ResetPicture();
                Live().SetColorFx(false, false);
            };
            break;
        }
        default: {
            int videoTracks = 0;
            for (const Track& t : PS().tracks) videoTracks += t.type == L"video";
            if (videoTracks > 1) {
                AddTrackLists(page, L"video");
                AddSectionLine(page);
            }
            Gap(page, 6);
            NavRow(page, "hdr", L"Quality", [] { return LabelOf(QualityOpts(), Cfg().GetString("video.quality")); },
                   kVideoQuality);
            NavRow(page, "zoom", L"Zoom", [] { return LabelOf(ZoomOpts(), Live().zoomMode); }, kVideoZoom);
            NavRow(page, "aspect-ratio", L"Aspect Ratio", [] { return LabelOf(AspectOpts(), Live().aspect); }, kVideoAspect);
            NavRow(page, "crop", L"Crop", [] { return LabelOf(CropOpts(), Live().crop); }, kVideoCrop);
            NavRow(page, "rotate-right", L"Rotate and flip", TurnText, kVideoTurn);
            NavRow(page, "paint", L"Picture", [] { return std::wstring(Tr(PictureChanged() ? L"Changed" : L"As in the file")); },
                   kVideoPicture);
            Widget* s = AddSection(page);
            auto* note = AddHint(s, L"");
            auto sync = [note] {
                const std::wstring t =
                    Live().FileSettingsAll()
                        ? Tr(L"Flip, rotation, zoom and aspect ratio apply to the current file only and are remembered for it.")
                        : Tr(L"Flip, rotation, zoom and aspect ratio apply to the current file only.");
                if (t != note->Text()) note->SetText(t);
            };
            sync();
            page->OnSync(sync);
            break;
        }
    }
    Gap(page, 8);
    return root;
}

// --- Playback ---

constexpr double kSpeeds[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};

std::wstring SpeedText(double v) {
    return std::abs(v - 1.0) < 1e-3 ? std::wstring(Tr(L"Normal")) : FormatNumber(v) + L"×";
}
std::vector<Opt> RepeatOpts() {
    return {{Tr(L"Off"), L"", "off"}, {Tr(L"Whole queue"), L"", "all"}, {Tr(L"This file"), L"", "one"}};
}
std::string RepeatValue() {
    const Repeat r = TheApp().queue().RepeatMode();
    return r == Repeat::One ? "one" : r == Repeat::All ? "all" : "off";
}
std::vector<Opt> SleepOpts() {
    std::vector<Opt> o{{Tr(L"Off"), L"", "0"}};
    for (int m : {15, 30, 45, 60, 90, 120}) o.push_back({Fmt(Tr(L"{0} min"), m), L"", std::to_string(m)});
    return o;
}
std::wstring LoopText() {
    const LiveValues& v = Live().V();
    if (std::isnan(v.abA) && std::isnan(v.abB)) return Tr(L"Off");
    std::wstring t = std::isnan(v.abA) ? std::wstring(L"…") : FormatTime(v.abA, false);
    t += L" – ";
    t += std::isnan(v.abB) ? std::wstring(L"…") : FormatTime(v.abB, false);
    return t;
}
bool AnythingRemembered() {
    return Live().FileSettingsOn() && !Live().FileSettingsStored().Empty();
}

std::unique_ptr<Widget> PlaybackPanel(int id) {
    SyncPage* page = nullptr;
    auto root = Page(&page);
    switch (id) {
        case kPlaySpeed: {
            std::vector<Opt> opts;
            for (double v : kSpeeds) opts.push_back({SpeedText(v), L"", std::to_string(static_cast<int>(std::lround(v * 100)))});
            AddOptions(
                page, opts,
                [] {
                    for (double v : kSpeeds)
                        if (std::abs(PS().speed - v) < 1e-3) return std::to_string(static_cast<int>(std::lround(v * 100)));
                    return std::string();
                },
                [](const std::string& v) { TheApp().player().SetSpeed(std::atoi(v.c_str()) / 100.0); }, Tr(L"Speed"));
            AddSectionLine(page);
            Widget* s = AddSection(page);
            // Anything between the presets: fine steps, or typed.
            AddStepper(page, s, Tr(L"Exact"), 0.05, [] { return PS().speed; },
                       [](double v) { TheApp().player().SetSpeed(std::clamp(v, 0.05, 8.0)); },
                       [](double v) { return FormatNumber(v) + L"×"; });
            break;
        }
        case kPlayRepeat:
            AddOptions(page, RepeatOpts(), RepeatValue,
                       // Through the settings, like the L key: it persists and drives mpv's loop-file and the queue.
                       [](const std::string& v) { Cfg().SetString("playback.repeat", v); }, Tr(L"Repeat"));
            break;
        case kPlaySleep:
            AddOptions(page, SleepOpts(), [] { return std::to_string(Live().sleepMinutes); },
                       [](const std::string& v) { Live().SetSleepTimer(std::atoi(v.c_str())); }, Tr(L"Sleep Timer"));
            break;
        case kPlayLoop: {
            Gap(page, 8);
            Widget* s = AddSection(page);
            auto* row = s->Add<Widget>();
            row->dir = Dir::Row;
            row->align = Align::Center;
            row->width = Length::Grow();
            row->height = Length::Px(52);
            row->gap = 8;
            auto* info = row->Add<Label>(L"", TextStyle::Body, Tone::Secondary);
            info->width = Length::Grow();
            info->pad = {4, 0, 0, 0};
            auto* a = row->Add<Button>(L"A", ButtonKind::Secondary);
            a->tooltip = Tr(L"Set the loop start");
            a->onClick = [] { TheApp().player().SetProperty("ab-loop-a", PS().position); };
            auto* b = row->Add<Button>(L"B", ButtonKind::Secondary);
            b->tooltip = Tr(L"Set the loop end");
            b->onClick = [] {
                if (std::isnan(Live().V().abA)) TheApp().player().SetProperty("ab-loop-a", 0.0);
                TheApp().player().SetProperty("ab-loop-b", PS().position);
            };
            AddHint(s, Tr(L"Press A where the part starts and B where it ends; it then plays over and over."));
            auto* clear = page->Add<ListRow>("close", Tr(L"Clear the loop"), nullptr, false);
            clear->onClick = [] {
                TheApp().player().SetProperty("ab-loop-a", std::string("no"));
                TheApp().player().SetProperty("ab-loop-b", std::string("no"));
            };
            auto sync = [a, b, clear, info] {
                const LiveValues& v = Live().V();
                a->kind = std::isnan(v.abA) ? ButtonKind::Secondary : ButtonKind::Primary;
                b->kind = std::isnan(v.abB) ? ButtonKind::Secondary : ButtonKind::Primary;
                const bool on = !std::isnan(v.abA) || !std::isnan(v.abB);
                info->SetText(on ? LoopText() : std::wstring(Tr(L"Not set")));
                if (clear->visible != on) {
                    clear->visible = on;
                    clear->InvalidateLayout();
                }
            };
            sync();
            page->OnSync(sync);
            break;
        }
        case kPlayRemembered: {
            Gap(page, 8);
            Widget* s = AddSection(page);
            auto* stored = AddHint(s, L"");
            stored->style = TextStyle::Body;
            stored->tone = Tone::Secondary;
            auto* reset = page->Add<ListRow>("refresh", Tr(L"Reset for This File"), nullptr, false);
            reset->tooltip = Tr(L"Forget what is remembered for this file and go back to the defaults");
            reset->onClick = [] {
                Live().ResetFileSettings();
                ShowPanelPage(0);
            };
            auto sync = [stored] {
                const std::wstring t = FileSettingsText();
                if (t != stored->Text()) stored->SetText(t);
            };
            sync();
            page->OnSync(sync);
            break;
        }
        default: {
            Gap(page, 6);
            NavRow(page, "speed", L"Speed", [] { return SpeedText(PS().speed); }, kPlaySpeed);
            NavRow(page, "repeat_right", L"Repeat", [] { return LabelOf(RepeatOpts(), RepeatValue()); }, kPlayRepeat);
            Widget* s = AddSection(page);
            AddToggle(page, s, Tr(L"Shuffle"), [] { return TheApp().queue().Shuffle(); },
                      [](bool v) { Cfg().SetBool("playback.shuffle", v); }, 44);
            AddToggle(page, s, Tr(L"Stop after this item"), [] { return Live().stopAfter; },
                      [](bool v) { Live().SetStopAfter(v); }, 44);
            NavRow(page, "sleep", L"Sleep Timer", [] { return LabelOf(SleepOpts(), std::to_string(Live().sleepMinutes)); },
                   kPlaySleep);
            NavRow(page, "replay", L"A-B Loop", LoopText, kPlayLoop);
            ListRow* remembered = NavRow(page, "bookmark", L"Remembered for this file", nullptr, kPlayRemembered);
            auto sync = [remembered] {
                const bool on = AnythingRemembered();
                if (remembered->visible != on) {
                    remembered->visible = on;
                    remembered->InvalidateLayout();
                }
            };
            remembered->visible = false;
            sync();
            page->OnSync(sync);
            break;
        }
    }
    Gap(page, 8);
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

std::unique_ptr<Widget> BuildSettingsPanel(PanelKind kind, int page) {
    switch (kind) {
        case PanelKind::Tracks: return AudioPanel(page);
        case PanelKind::Subtitles: return SubtitlesPanel(page);
        case PanelKind::Video: return VideoPanel(page);
        default: return PlaybackPanel(page);
    }
}

std::wstring SpeedValue() {
    return SpeedText(PS().speed);
}

std::wstring SleepValue() {
    return LabelOf(SleepOpts(), std::to_string(Live().sleepMinutes));
}

std::wstring SettingsPageTitle(PanelKind kind, int page) {
    const wchar_t* t = L"";
    switch (kind) {
        case PanelKind::Tracks: {
            static const wchar_t* k[] = {L"Audio", L"Loudness", L"Equalizer", L"Synchronization", L"Output Device", L"Channels"};
            t = k[std::clamp(page, 0, 5)];
            break;
        }
        case PanelKind::Subtitles: {
            static const wchar_t* k[] = {L"Subtitles", L"Synchronization", L"Size", L"Position", L"Secondary Subtitles",
                                         L"Subtitle style"};
            t = k[std::clamp(page, 0, 5)];
            break;
        }
        case PanelKind::Video: {
            static const wchar_t* k[] = {L"Video", L"Quality", L"Zoom", L"Aspect Ratio", L"Crop", L"Rotate and flip", L"Picture"};
            t = k[std::clamp(page, 0, 6)];
            break;
        }
        default: {
            static const wchar_t* k[] = {L"Playback", L"Speed", L"Repeat", L"Sleep Timer", L"A-B Loop", L"Remembered for this file"};
            t = k[std::clamp(page, 0, 5)];
            break;
        }
    }
    return Tr(t);
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

#include "app/player/MetersView.h"

#include "app/App.h"
#include "app/I18n.h"
#include "app/player/VizPaint.h"
#include "audio/LoopbackCapture.h"
#include "core/SettingsSchema.h"
#include "gfx/Gpu.h"
#include "ui/Anim.h"
#include "ui/Controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <memory>
#include <vector>

namespace app {

using namespace ui;

namespace {

constexpr unsigned kMetersTap = 2;       // Player::SetSignalTap listener of the meters window
constexpr size_t kHistoryMax = 3000;     // loudness history points: 5 min at 10 per second
constexpr float kHistoryTop = 0, kHistoryBottom = -50;  // LUFS
// The window's own options (Settings schema, hidden: set from the right-click menu).
constexpr const char* kCardKeys[] = {"interface.metersVu", "interface.metersLoudness", "interface.metersLevels",
                                     "interface.metersStereo", "interface.metersSpectrum"};
constexpr const char* kOptionKeys[] = {"interface.metersOnTop",   "interface.metersVuPeak",
                                       "interface.metersTarget",  "interface.metersHistory",
                                       "interface.metersFloor",   "interface.metersPeakHold",
                                       "interface.metersGonio",   "interface.metersBalance",
                                       "interface.metersSpectrumStyle", "interface.metersSpectrumPeaks"};

Config& Settings() {
    return TheApp().config();
}

float Db(float v) {
    return v > 1e-9f ? 20.0f * std::log10(v) : -200.0f;
}

// "-14.2" with a real minus; "−∞" for no signal.
std::wstring Number(float v, int decimals = 1, bool plus = false) {
    if (v <= -99) return L"−∞";
    wchar_t t[32];
    swprintf_s(t, plus ? L"%+.*f" : L"%.*f", decimals, v);
    if (t[0] == L'-') return L"−" + std::wstring(t + 1);
    return t;
}

bool Light() {
    return Theme::Get().Mode() == ThemeMode::Light;
}
// Monochrome like the player; red only where the sound goes over full scale (clipping) or out of
// phase. Darker on the light theme for 3:1.
Color Red() {
    return Color::Hex(Light() ? 0xD93025 : 0xFF5A4E);
}
Color LevelColor(float db) {
    return db > -0.1f ? Red() : P().accent;
}

// A polyline (or, with `fill`, the area between it and `baseY`) as one geometry: one draw call.
gfx::ComPtr<ID2D1PathGeometry> Polyline(ID2D1DeviceContext5* ctx, const D2D1_POINT_2F* pts, size_t n, bool fill,
                                        float baseY = 0) {
    gfx::ComPtr<ID2D1Factory> factory;
    ctx->GetFactory(&factory);
    gfx::ComPtr<ID2D1PathGeometry> g;
    gfx::ComPtr<ID2D1GeometrySink> sink;
    if (n < 2 || !factory || FAILED(factory->CreatePathGeometry(&g)) || FAILED(g->Open(&sink))) return {};
    if (fill) {
        sink->BeginFigure({pts[0].x, baseY}, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(pts, static_cast<UINT32>(n));
        sink->AddLine({pts[n - 1].x, baseY});
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    } else {
        sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(pts + 1, static_cast<UINT32>(n - 1));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    return SUCCEEDED(sink->Close()) ? g : nullptr;
}

settings::Lang SchemaLang() {
    return CurrentLanguage() == Language::Russian ? settings::Lang::Ru : settings::Lang::En;
}

// "0 VU = −10 dBFS (loud modern masters)" without its explanation; "(EBU)" and the like stay.
std::wstring ShortLabel(std::wstring label) {
    const size_t open = label.find(L" (");
    if (open != std::wstring::npos && label.find(L' ', open + 2) != std::wstring::npos) label.resize(open);
    return label;
}

// Menu entries straight from a setting: its label from the schema, its value from the config.
MenuItem CheckItem(const char* key, bool enabled = true) {
    const settings::Item* item = settings::FindItem(key);
    MenuItem m;
    m.label = item ? item->Label(SchemaLang()) : std::wstring();
    m.checked = Settings().GetBool(key);
    m.enabled = enabled;
    const std::string k = key;
    m.action = [k] { Settings().SetBool(k, !Settings().GetBool(k)); };
    return m;
}
MenuItem RadioItem(const char* key) {
    const settings::Item* item = settings::FindItem(key);
    MenuItem m;
    if (!item) return m;
    m.label = item->Label(SchemaLang());
    const std::string cur = Settings().GetString(key), k = key;
    for (const settings::Choice& c : item->choices) {
        MenuItem sub;
        sub.label = c.Label(SchemaLang());
        sub.radio = true;
        sub.checked = c.value == cur;
        sub.action = [k, v = c.value] { Settings().SetString(k, v); };
        if (sub.checked) m.shortcut = ShortLabel(sub.label);  // the current value, beside the arrow
        m.submenu.push_back(std::move(sub));
    }
    return m;
}
MenuItem ActionItem(std::wstring label, std::function<void()> fn, std::string icon = {}, bool enabled = true) {
    MenuItem m;
    m.label = std::move(label);
    m.icon = std::move(icon);
    m.enabled = enabled;
    m.action = std::move(fn);
    return m;
}

// What every card reads; the view refreshes it once per frame, before the cards paint.
struct Feed {
    // The menu's choices, read when a setting changes.
    struct Options {
        bool cards[5] = {true, true, true, true, true};  // kCardKeys order
        bool onTop = false, balance = true, gonioDots = false, spectrumLine = false, spectrumPeaks = true;
        float target = audio::kLoudnessNone;  // LUFS, none: no target line
        size_t historyN = 600;                // points the graph shows
        float floor = -60;                    // level bars, dBFS
        double holdMs = 1500;                 // 0: no hold, < 0: until reset
    } opt;

    void Load() {
        Config& c = Settings();
        for (int i = 0; i < 5; ++i) opt.cards[i] = c.GetBool(kCardKeys[i]);
        opt.onTop = c.GetBool("interface.metersOnTop");
        viz.vuPeak = c.GetBool("interface.metersVuPeak");
        opt.balance = c.GetBool("interface.metersBalance");
        opt.gonioDots = c.GetString("interface.metersGonio") == "dots";
        opt.spectrumLine = c.GetString("interface.metersSpectrumStyle") == "line";
        opt.spectrumPeaks = c.GetBool("interface.metersSpectrumPeaks");
        const std::string target = c.GetString("interface.metersTarget");
        opt.target = target == "off" ? audio::kLoudnessNone : static_cast<float>(std::atof(target.c_str()));
        opt.historyN = std::clamp<size_t>(static_cast<size_t>(std::atoi(c.GetString("interface.metersHistory").c_str())) * 10,
                                          100, kHistoryMax);
        opt.floor = std::min(-20.0f, static_cast<float>(std::atof(c.GetString("interface.metersFloor").c_str())));
        const std::string peakHold = c.GetString("interface.metersPeakHold");
        opt.holdMs = peakHold == "hold" ? -1.0 : peakHold == "off" ? 0.0 : std::atof(peakHold.c_str());
    }

    struct Hold {
        float db = -200;
        double at = 0;
    };
    std::unique_ptr<audio::LoopbackCapture> capture;
    audio::VisualFrame frame;
    VizPaint viz;
    bool live = false;
    float reference = -10;
    Hold hold[2], max[2];
    std::vector<float> history;  // short-term loudness, oldest first
    double historyAt = 0;

    std::function<void(std::vector<MenuItem>&)> common;  // the view's entries under each card's own

    void ResetLoudness() {
        history.clear();
        if (capture) capture->ResetLoudness();
        frame.integrated = frame.range = audio::kLoudnessNone;
    }
    void ResetClips() {
        for (Hold& h : max) h = {};
    }
    void Reset() {
        ResetClips();
        for (Hold& h : hold) h = {};
        ResetLoudness();
    }
};

// A card of the window: the panel colour, a title and an optional note on the right.
class Card : public Widget {
public:
    Card(Feed& feed, const wchar_t* title) : feed_(feed), title_(title) {
        width = Length::Grow();
        height = Length::Grow();
        // Right click: this card's options, then the window's.
        onContextMenu = [this](const MouseEvent& e) {
            std::vector<MenuItem> items;
            Menu(items);
            if (!items.empty()) items.push_back(MenuItem::Separator());
            if (feed_.common) feed_.common(items);
            ShowContextMenu(GetHost(), {e.x, e.y}, std::move(items));
        };
    }

    void Paint(Painter& p) override {
        p.Fill(rect, P().bgPanel, kRadiusCard);
        const Rect head{rect.x + 14, rect.y + 10, rect.w - 28, 18};
        const std::wstring note = Note();
        const float noteW = note.empty() ? 0 : p.Measure(note, TextStyle::Small).width + 12;
        p.Text(Tr(title_), {head.x, head.y, head.w - noteW, head.h}, TextStyle::SmallMedium, P().textSecondary);
        if (!note.empty()) p.Text(note, head, TextStyle::Small, P().textTertiary, HAlign::Trailing);
        PaintBody(p, {rect.x + 14, rect.y + 38, rect.w - 28, rect.h - 38 - 14});
    }

protected:
    virtual std::wstring Note() const { return {}; }
    virtual void PaintBody(Painter& p, const Rect& r) = 0;
    virtual void Menu(std::vector<MenuItem>& /*items*/) {}
    Feed& feed_;

private:
    const wchar_t* title_;
};

// --- the VU pair --------------------------------------------------------------------------------

// The calibration sits on the meters it moves, at the card's top right.
class VuCard : public Card {
public:
    explicit VuCard(Feed& f) : Card(f, L"VU meters") {
        const settings::Item* item = settings::FindItem("audio.vuReference");
        if (!item) return;
        std::vector<ui::Choice> choices;
        for (const settings::Choice& c : item->choices) {
            choices.push_back({ShortLabel(c.Label(SchemaLang())), c.value, {}});
        }
        calib_ = Add<Dropdown>(std::move(choices), 0);
        calib_->tooltip = Tr(L"VU meter calibration");
        calib_->onChange = [this](int i) {
            if (i >= 0 && i < static_cast<int>(calib_->Choices().size()))
                Settings().SetString("audio.vuReference", calib_->Choices()[static_cast<size_t>(i)].value);
        };
        Sync();
        token_ = Settings().Subscribe("audio.vuReference", [this](const std::string&) {
            Sync();
            Invalidate();
        });
    }
    ~VuCard() override { Settings().Unsubscribe(token_); }

    void OnLayout() override {
        if (calib_) calib_->Arrange({rect.Right() - 8 - 196, rect.y + 6, 196, 28});
    }

protected:
    void PaintBody(Painter& p, const Rect& r) override { feed_.viz.VuPair(p, r, false, P().textPrimary); }
    void Menu(std::vector<MenuItem>& items) override {
        items.push_back(RadioItem("audio.vuReference"));
        items.push_back(CheckItem("interface.metersVuPeak"));
    }

private:
    void Sync() {
        const std::string cur = Settings().GetString("audio.vuReference");
        for (size_t i = 0; i < calib_->Choices().size(); ++i)
            if (calib_->Choices()[i].value == cur && calib_->Selected() != static_cast<int>(i))
                calib_->SetSelected(static_cast<int>(i));
    }

    Dropdown* calib_ = nullptr;
    Config::Token token_ = 0;
};

// --- EBU R128 loudness: integrated large, M / S / LRA, and the last minute as a graph ------------

class LoudnessCard : public Card {
public:
    explicit LoudnessCard(Feed& f) : Card(f, L"Loudness") {}

protected:
    std::wstring Note() const override { return L"EBU R128"; }

    void PaintBody(Painter& p, const Rect& r) override {
        const audio::VisualFrame& f = feed_.frame;
        const Color primary = P().textPrimary, tertiary = P().textTertiary;
        // Integrated: the whole song so far.
        const std::wstring big = Number(f.integrated);
        const float bigW = p.Measure(big, TextStyle::MonoLarge).width;
        p.Text(big, {r.x, r.y - 4, bigW + 2, 36}, TextStyle::MonoLarge, primary);
        p.Text(L"LUFS", {r.x + bigW + 8, r.y + 10, 60, 18}, TextStyle::Small, tertiary);
        p.Text(Tr(L"Integrated loudness"), {r.x, r.y + 32, r.w, 18}, TextStyle::Small, P().textSecondary);
        // Against the chosen target: + louder, − quieter.
        const float target = feed_.opt.target;
        if (target > audio::kLoudnessNone) {
            const bool known = f.integrated > audio::kLoudnessNone;
            p.Text(known ? Number(f.integrated - target, 1, true) + L" LU" : L"—", {r.x, r.y + 6, r.w, 22}, TextStyle::Mono,
                   known ? primary : tertiary, HAlign::Trailing);
            p.Text(Tr(L"Target") + std::wstring(L" ") + Number(target, 0), {r.x, r.y + 32, r.w, 18}, TextStyle::Small,
                   tertiary, HAlign::Trailing);
        }

        // Momentary (400 ms), short-term (3 s), loudness range.
        const float y = r.y + 60, colW = r.w / 3;
        const struct {
            const wchar_t* head;
            float value;
            const wchar_t* unit;
        } cols[] = {{L"M", feed_.live ? f.momentary : audio::kLoudnessNone, L"LUFS"},
                    {L"S", feed_.live ? f.shortTerm : audio::kLoudnessNone, L"LUFS"},
                    {L"LRA", f.range, L"LU"}};
        for (int i = 0; i < 3; ++i) {
            const Rect c{r.x + colW * i, y, colW - 6, 16};
            p.Text(std::wstring(cols[i].head) + L" · " + cols[i].unit, c, TextStyle::Micro, tertiary);
            p.Text(Number(cols[i].value), {c.x, c.y + 16, c.w, 22}, TextStyle::Mono, primary);
        }
        PaintHistory(p, {r.x, y + 50, r.w, r.Bottom() - y - 50});
    }

    void Menu(std::vector<MenuItem>& items) override {
        items.push_back(RadioItem("interface.metersTarget"));
        items.push_back(RadioItem("interface.metersHistory"));
        items.push_back(ActionItem(Tr(L"Reset loudness"), [this] {
            feed_.ResetLoudness();
            Invalidate();
        }));
    }

private:
    // Short-term loudness over the chosen span against a LUFS grid, with the target as a line.
    void PaintHistory(Painter& p, const Rect& r) {
        if (r.h < 40) return;
        const float labelW = 32;
        const Rect g{r.x + labelW, r.y + 6, r.w - labelW, r.h - 12};
        auto yOf = [&](float lufs) {
            return g.y + g.h * (kHistoryTop - std::clamp(lufs, kHistoryBottom, kHistoryTop)) / (kHistoryTop - kHistoryBottom);
        };
        for (float l = -10; l > kHistoryBottom; l -= 10) {
            const float y = std::round(yOf(l));
            p.Fill({g.x, y, g.w, 1}, P().line);
            p.Text(Number(l, 0), {r.x, y - 8, labelW - 6, 16}, TextStyle::Micro, P().textTertiary, HAlign::Trailing);
        }
        if (feed_.opt.target > audio::kLoudnessNone) {
            const float y = std::round(yOf(feed_.opt.target));
            p.Fill({g.x, y, g.w, 1}, P().accent.Fade(0.6f));
            p.Text(Tr(L"target"), {g.x, y - 15, g.w - 2, 14}, TextStyle::Micro, P().textSecondary, HAlign::Trailing);
        }
        const std::vector<float>& all = feed_.history;
        const size_t n = feed_.opt.historyN, skip = all.size() > n ? all.size() - n : 0;
        const std::vector<float> h(all.begin() + static_cast<std::ptrdiff_t>(skip), all.end());
        if (h.size() < 2) return;
        ID2D1DeviceContext5* ctx = p.Ctx();
        const float step = g.w / static_cast<float>(n - 1);
        std::vector<D2D1_POINT_2F> run;
        auto flush = [&] {
            if (run.size() >= 2) {
                if (auto area = Polyline(ctx, run.data(), run.size(), true, g.Bottom()))
                    p.FillGeometry(area.Get(), P().accent.Fade(0.10f));
                if (auto line = Polyline(ctx, run.data(), run.size(), false))
                    if (ID2D1SolidColorBrush* b = feed_.viz.LineBrush(ctx, P().accent, 0.9f * p.Opacity()))
                        ctx->DrawGeometry(line.Get(), b, 1.5f, feed_.viz.RoundStroke(ctx));
            }
            run.clear();
        };
        for (size_t i = 0; i < h.size(); ++i) {
            if (h[i] <= kHistoryBottom - 20) {  // no signal: a gap in the line
                flush();
                continue;
            }
            run.push_back({g.Right() - step * static_cast<float>(h.size() - 1 - i), yOf(h[i])});
        }
        flush();
    }
};

// --- level bars: RMS and peak per channel in dBFS, a held peak mark and a clip light -------------

class LevelsCard : public Card {
public:
    explicit LevelsCard(Feed& f) : Card(f, L"Levels") {
        height = Length::Px(132);
        interactive = true;
        tooltip = Tr(L"Click a clip light to reset it");
    }

    bool OnMouseDown(const MouseEvent& e) override {
        if (e.button != MouseButton::Left) return false;
        for (int ch = 0; ch < 2; ++ch)
            if (lights_[ch].Inset(-4).Contains(e.x, e.y)) {
                feed_.max[ch] = {};
                Invalidate();
                return true;
            }
        return false;
    }

protected:
    std::wstring Note() const override { return L"dBFS"; }

    void Menu(std::vector<MenuItem>& items) override {
        items.push_back(RadioItem("interface.metersFloor"));
        items.push_back(RadioItem("interface.metersPeakHold"));
        const bool clipped = feed_.max[0].db >= 0 || feed_.max[1].db >= 0;
        items.push_back(ActionItem(Tr(L"Turn off the clip lights"), [this] {
            feed_.ResetClips();
            Invalidate();
        }, {}, clipped));
    }

    void PaintBody(Painter& p, const Rect& body) override {
        constexpr float contentH = 86, numW = 62, letterW = 18, lightW = 8;
        // Taller than its content (the spectrum hidden): the bars in the middle.
        const Rect r{body.x, body.y + std::max(0.0f, std::floor((body.h - contentH) / 2)), body.w, contentH};
        const float floor = feed_.opt.floor;
        const float barX = r.x + letterW, barW = r.w - letterW - lightW - 10 - 3 * numW - 8;
        const float numX = r.Right() - 3 * numW;
        const wchar_t* heads[] = {L"RMS", L"Peak", L"Max"};
        for (int c = 0; c < 3; ++c)
            p.Text(Tr(heads[c]), {numX + numW * c, r.y, numW, 14}, TextStyle::Micro, P().textTertiary, HAlign::Trailing);
        for (int ch = 0; ch < 2; ++ch) {
            const float y = r.y + 18 + ch * 24;
            p.Text(ch == 0 ? L"L" : L"R", {r.x, y, letterW, 18}, TextStyle::SmallMedium, P().textSecondary);
            const float rms = feed_.live ? Db(feed_.frame.rms[ch]) : -200.0f;
            const float peak = feed_.live ? Db(feed_.frame.peak[ch]) : -200.0f;
            PaintBar(p, {barX, y + 3, barW, 12}, rms, peak, feed_.opt.holdMs != 0 ? feed_.hold[ch].db : -200.0f, floor);
            // The clip light: lit once the song went over full scale, until clicked.
            const bool clipped = feed_.max[ch].db >= 0;
            lights_[ch] = {barX + barW + 6, y + 3, lightW, 12};
            p.Fill(lights_[ch], clipped ? Red() : P().bgDeep, kRadiusSmall);
            const float values[] = {rms, feed_.hold[ch].db, feed_.max[ch].db};
            for (int c = 0; c < 3; ++c) {
                const Color col = c > 0 && values[c] >= 0 ? Red() : c == 2 ? P().textSecondary : P().textPrimary;
                p.Text(Number(values[c]), {numX + numW * c, y, numW, 18}, TextStyle::MonoSmall, col, HAlign::Trailing);
            }
        }
        // The scale under the bars.
        const float sy = r.y + 18 + 2 * 24 + 2;
        // Spaced so neighbours never touch at the narrowest bar; -18 and -3 when there is room.
        std::vector<float> ticks = floor <= -90   ? std::vector<float>{-90, -72, -54, -36, -24, -12, -6, 0}
                                   : floor <= -60 ? std::vector<float>{-60, -48, -36, -24, -12, -6, 0}
                                                  : std::vector<float>{-40, -30, -20, -12, -6, -3, 0};
        if (floor > -90 && floor <= -60 && barW >= 360) ticks.insert(ticks.end(), {-18, -3});
        for (float db : ticks) {
            const float x = std::round(barX + barW * (db - floor) / -floor);
            p.Fill({x, sy, 1, 4}, P().lineStrong);
            p.Text(Number(db, 0), {x - 20, sy + 4, 40, 14}, TextStyle::Micro, P().textTertiary, HAlign::Center);
        }
    }

private:
    // Segments lit to the RMS level, dimmer up to the peak, and the held peak as one bright mark.
    static void PaintBar(Painter& p, const Rect& r, float rms, float peak, float hold, float floor) {
        constexpr float pitch = 4, seg = 3;
        const int n = std::max(1, static_cast<int>(r.w / pitch));
        const int holdAt = hold > floor ? std::min(n - 1, static_cast<int>((hold - floor) / -floor * n)) : -1;
        for (int i = 0; i < n; ++i) {
            const float db = floor + (i + 1) * -floor / n;  // the segment's top
            const float lo = db - -floor / n;
            const Rect s{r.x + i * pitch, r.y, seg, r.h};
            if (rms > lo) p.Fill(s, LevelColor(db).Fade(0.9f));
            else if (peak > lo) p.Fill(s, LevelColor(db).Fade(0.4f));
            else if (i == holdAt) p.Fill(s, LevelColor(db));
            else p.Fill(s, P().textPrimary.Fade(0.07f));
        }
    }

    Rect lights_[2];
};

// --- stereo: goniometer, phase correlation and balance ----------------------------------------

class StereoCard : public Card {
public:
    explicit StereoCard(Feed& f) : Card(f, L"Stereo") {}

protected:
    void Menu(std::vector<MenuItem>& items) override {
        items.push_back(RadioItem("interface.metersGonio"));
        items.push_back(CheckItem("interface.metersBalance"));
    }

    void PaintBody(Painter& p, const Rect& r) override {
        const float meters = feed_.opt.balance ? 92.0f : 42.0f;
        const float side = std::floor(std::min(r.w, r.h - meters - 8));
        if (side > 40) PaintGonio(p, {std::round(r.Cx() - side / 2), r.y, side, side});
        const audio::VisualFrame& f = feed_.frame;
        const float y = r.Bottom() - meters;
        // +1: the channels move together (mono); 0: unrelated; below 0: out of phase, which
        // cancels out on a mono speaker.
        const float corr = f.correlation;
        PaintScale(p, {r.x, y, r.w, 40}, Tr(L"Correlation"), feed_.live ? Number(corr, 2, true) : L"—",
                   feed_.live ? corr : 0, feed_.live, corr < 0 ? Red() : P().accent, L"−1", L"0", L"+1");
        if (!feed_.opt.balance) return;
        // Balance: how much louder one side is, ±6 dB across the bar.
        const float l = Db(f.rms[0]), rr = Db(f.rms[1]);
        const bool balanced = feed_.live && l > -90 && rr > -90;
        const float bal = balanced ? std::clamp(rr - l, -6.0f, 6.0f) : 0;
        std::wstring text = L"—";
        if (balanced)
            text = std::fabs(rr - l) < 0.05f ? L"0.0 dB" : (rr > l ? L"R " : L"L ") + Number(std::fabs(rr - l)) + L" dB";
        PaintScale(p, {r.x, y + 50, r.w, 40}, Tr(L"Balance"), text, bal / 6, balanced, P().accent, L"L", L"0", L"R");
    }

private:
    // L and R on the diagonals, mono straight up (M), the stereo difference across (S).
    void PaintGonio(Painter& p, const Rect& r) {
        const float cx = r.Cx(), cy = r.Cy(), rad = r.w / 2;
        const Color grid = P().line;
        p.StrokeCircle({cx, cy}, rad - 0.5f, grid);
        p.StrokeCircle({cx, cy}, rad * 0.5f, grid.Fade(0.6f));
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (ID2D1SolidColorBrush* b = feed_.viz.LineBrush(ctx, grid, p.Opacity())) {
            const float d = rad * 0.7071f;
            ctx->DrawLine({cx, cy - rad}, {cx, cy + rad}, b, 1);
            ctx->DrawLine({cx - rad, cy}, {cx + rad, cy}, b, 1);
            ctx->DrawLine({cx - d, cy - d}, {cx + d, cy + d}, b, 1);
            ctx->DrawLine({cx + d, cy - d}, {cx - d, cy + d}, b, 1);
        }
        const Color label = P().textTertiary;
        const float d = rad * 0.7071f + 10;
        p.Text(L"L", {cx - d - 8, cy - d - 8, 16, 16}, TextStyle::Micro, label, HAlign::Center);
        p.Text(L"R", {cx + d - 8, cy - d - 8, 16, 16}, TextStyle::Micro, label, HAlign::Center);
        p.Text(L"M", {cx + 4, r.y - 2, 16, 16}, TextStyle::Micro, label);
        p.Text(L"S", {r.Right() - 14, cy + 2, 16, 16}, TextStyle::Micro, label);
        if (!feed_.live) {
            p.FillCircle({cx, cy}, 2, P().accent.Fade(0.5f));
            return;
        }
        // The newest stereo samples as one trace, like a phosphor screen.
        std::vector<D2D1_POINT_2F> pts(audio::kGonioPoints);
        const float k = rad * 0.62f;
        for (int i = 0; i < audio::kGonioPoints; ++i) {
            const float l = feed_.frame.gonio[i * 2], rr = feed_.frame.gonio[i * 2 + 1];
            pts[static_cast<size_t>(i)] = {cx + (rr - l) * k, cy - (l + rr) * k};
        }
        p.PushRoundClip(r, rad);  // inside the circle: loud corners would draw the clamp's square
        if (feed_.opt.gonioDots) {
            const Color dot = P().accent.Fade(0.6f);
            for (const D2D1_POINT_2F& pt : pts) p.Fill({pt.x - 0.75f, pt.y - 0.75f, 1.5f, 1.5f}, dot);
        } else if (auto trace = Polyline(ctx, pts.data(), pts.size(), false)) {
            if (ID2D1SolidColorBrush* b = feed_.viz.LineBrush(ctx, P().accent, 0.55f * p.Opacity()))
                ctx->DrawGeometry(trace.Get(), b, 1.0f, feed_.viz.RoundStroke(ctx));
        }
        p.PopClip();
    }

    // A label and its value over a bar from -1 to +1 that fills from the middle.
    static void PaintScale(Painter& p, const Rect& r, const wchar_t* name, const std::wstring& value, float v,
                           bool on, Color fill, const wchar_t* left, const wchar_t* mid, const wchar_t* right) {
        p.Text(name, {r.x, r.y, r.w - 80, 16}, TextStyle::Small, P().textSecondary);
        p.Text(value, {r.Right() - 80, r.y, 80, 16}, TextStyle::MonoSmall, on ? P().textPrimary : P().textTertiary,
               HAlign::Trailing);
        const Rect bar{r.x, r.y + 20, r.w, 6};
        p.Fill(bar, P().bgDeep, 3);
        const float cx = std::round(bar.Cx());
        if (on) {
            const float x = bar.x + bar.w * (std::clamp(v, -1.0f, 1.0f) + 1) / 2;
            p.Fill({std::min(cx, x), bar.y, std::fabs(x - cx), bar.h}, fill.Fade(0.85f));
            p.Fill({std::round(x) - 1, bar.y - 3, 2, bar.h + 6}, fill, 1);
        }
        p.Fill({cx, bar.y - 2, 1, bar.h + 4}, P().lineStrong);
        p.Text(left, {bar.x, bar.Bottom() + 2, 30, 12}, TextStyle::Micro, P().textTertiary);
        p.Text(mid, {cx - 15, bar.Bottom() + 2, 30, 12}, TextStyle::Micro, P().textTertiary, HAlign::Center);
        p.Text(right, {bar.Right() - 30, bar.Bottom() + 2, 30, 12}, TextStyle::Micro, P().textTertiary, HAlign::Trailing);
    }
};

// --- the spectrum with a frequency and dB grid --------------------------------------------------

class SpectrumCard : public Card {
public:
    explicit SpectrumCard(Feed& f) : Card(f, L"Spectrum") {}

protected:
    std::wstring Note() const override { return std::wstring(L"40 ") + Tr(L"Hz") + L" – 16 " + Tr(L"kHz"); }

    void Menu(std::vector<MenuItem>& items) override {
        items.push_back(RadioItem("interface.metersSpectrumStyle"));
        items.push_back(CheckItem("interface.metersSpectrumPeaks"));
    }

    void PaintBody(Painter& p, const Rect& r) override {
        const float labelH = 16, dbW = 28;
        const Rect bars{r.x, r.y + 4, r.w - dbW, r.h - labelH - 4};
        // Bars stand 90 % of the height at full level (VizPaint::Spectrum): the grid follows.
        for (float db : {-12.f, -24.f, -36.f, -48.f, -60.f}) {
            const float y = std::round(bars.Bottom() - audio::BarLevel(db) * bars.h * 0.9f);
            p.Fill({bars.x, y, bars.w, 1}, P().line);
            p.Text(Number(db, 0), {bars.Right() + 4, y - 8, dbW - 4, 16}, TextStyle::Micro, P().textTertiary,
                   HAlign::Trailing);
        }
        if (feed_.opt.spectrumLine) PaintLine(p, bars);
        else feed_.viz.Spectrum(p, bars, feed_.frame, P().accent, false, false, !feed_.live, feed_.opt.spectrumPeaks);
        // Bands are log-spaced from 40 Hz to 16 kHz (audio::SpectrumAnalyzer).
        const float step = bars.w / audio::kBands;
        for (int hz : {50, 100, 200, 500, 1000, 2000, 5000, 10000}) {
            const float x = bars.x + step * audio::kBands * std::log(hz / 40.0f) / std::log(400.0f) + step * 0.39f;
            const std::wstring t = hz >= 1000 ? std::to_wstring(hz / 1000) + Tr(L"k") : std::to_wstring(hz);
            p.Text(t, {x - 20, bars.Bottom() + 4, 40, 14}, TextStyle::Micro, P().textTertiary, HAlign::Center);
        }
    }

private:
    // The bands as one smooth curve over a soft area, the peaks as dots above it.
    void PaintLine(Painter& p, const Rect& r) {
        const audio::VisualFrame& f = feed_.frame;
        const float step = r.w / audio::kBands;
        auto yOf = [&](float level) { return r.Bottom() - std::clamp(level, 0.0f, 1.0f) * r.h * 0.9f; };
        std::vector<D2D1_POINT_2F> pts;
        for (int i = 0; i < audio::kBands; ++i)
            pts.push_back({r.x + step * (i + 0.39f), yOf(feed_.live ? f.bands[i] : 0.0f)});
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (auto area = Polyline(ctx, pts.data(), pts.size(), true, r.Bottom())) p.FillGeometry(area.Get(), P().accent.Fade(0.14f));
        if (auto line = Polyline(ctx, pts.data(), pts.size(), false))
            if (ID2D1SolidColorBrush* b = feed_.viz.LineBrush(ctx, P().accent, 0.9f * p.Opacity()))
                ctx->DrawGeometry(line.Get(), b, 2.0f, feed_.viz.RoundStroke(ctx));
        if (feed_.opt.spectrumPeaks && feed_.live)
            for (int i = 0; i < audio::kBands; ++i)
                if (f.peaks[i] > 0.02f) p.FillCircle({pts[static_cast<size_t>(i)].x, yOf(f.peaks[i])}, 1.5f, P().accent);
    }
};

// --- the view: two rows of cards, the right column a fixed share of the width -------------------

class MetersView : public Widget {
public:
    MetersView() {
        width = Length::Grow();
        height = Length::Grow();
        dir = Dir::Column;
        pad = {8, 8, 8, 8};
        gap = 8;
        top_ = Add<Widget>();
        top_->dir = Dir::Row;
        top_->gap = 8;
        top_->width = Length::Grow();
        cards_[0] = top_->Add<VuCard>(feed_);
        cards_[1] = top_->Add<LoudnessCard>(feed_);
        bottom_ = Add<Widget>();
        bottom_->dir = Dir::Row;
        bottom_->gap = 8;
        bottom_->width = Length::Grow();
        left_ = bottom_->Add<Widget>();
        left_->dir = Dir::Column;
        left_->gap = 8;
        left_->width = Length::Grow();
        left_->height = Length::Grow();
        cards_[2] = left_->Add<LevelsCard>(feed_);
        cards_[4] = left_->Add<SpectrumCard>(feed_);
        cards_[3] = bottom_->Add<StereoCard>(feed_);
        // Right click between the cards: the window's entries alone.
        feed_.common = [this](std::vector<MenuItem>& items) { CommonMenu(items); };
        onContextMenu = [this](const MouseEvent& e) {
            std::vector<MenuItem> items;
            CommonMenu(items);
            ShowContextMenu(GetHost(), {e.x, e.y}, std::move(items));
        };
        feed_.Load();
        Arrange();
        options_ = Settings().Subscribe("interface.meters", [this](const std::string&) {
            feed_.Load();
            Arrange();
            ApplyOnTop();
            InvalidateLayout();
            Invalidate();
        });

        TheApp().player().SetSignalTap(true, kMetersTap);
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeTime | kChangeState | kChangeFile | kChangeVolume)) Invalidate();
        });
    }
    ~MetersView() override {
        Settings().Unsubscribe(options_);
        TheApp().player().SetSignalTap(false, kMetersTap);
    }

    void Reset() {
        feed_.Reset();
        Invalidate();
    }

    void OnLayout() override {
        // The right column keeps a share of the width, or takes the row when its neighbour is hidden.
        const float side = std::round(std::clamp(rect.w * 0.32f, 240.0f, 340.0f));
        cards_[1]->width = cards_[0]->visible ? Length::Px(side) : Length::Grow();
        cards_[3]->width = left_->visible ? Length::Px(side) : Length::Grow();
        Widget::OnLayout();
    }

    void DebugInfo(std::string& out) const override {
        Widget::DebugInfo(out);
        const audio::VisualFrame& f = feed_.frame;
        float gonio = 0;
        for (float v : f.gonio) gonio = std::max(gonio, std::fabs(v));
        char text[256];
        snprintf(text, sizeof text,
                 " frames=%llu rms=%.3f/%.3f peak=%.3f/%.3f lufs=%.1f/%.1f/%.1f lra=%.1f corr=%.2f gonio=%.2f history=%zu%s",
                 static_cast<unsigned long long>(f.serial), f.rms[0], f.rms[1], f.peak[0], f.peak[1], f.momentary,
                 f.shortTerm, f.integrated, f.range, f.correlation, gonio, feed_.history.size(),
                 feed_.capture && feed_.capture->Failed() ? " failed" : "");
        out += text;
    }

    void Paint(Painter& p) override {
        Feed& f = feed_;
        if (!onTopApplied_) ApplyOnTop();
        const PlayerState& s = TheApp().player().State();
        const bool playing = s.fileLoaded && !s.paused && !s.eof && !s.idle;
        if (playing && !f.capture && GetHost())
            f.capture = std::make_unique<audio::LoopbackCapture>(GetHost()->Hwnd(), Host::kRepaintMessage);
        if (s.loadSerial != serial_) {  // a new song: its own loudness and loudest moments
            serial_ = s.loadSerial;
            f.Reset();
        }
        f.live = false;
        if (f.capture) {
            // Windows' "Animation effects" off: the first frame with sound in it stays (WCAG 2.3.3).
            const bool still = AnimFloat::ReducedMotion() && f.frame.level > 0.05f;
            f.capture->SetGain(TheApp().player().OutputGain());
            f.capture->SetRunning(playing && !still);
            if (!still) f.capture->Take(f.frame);
            f.live = playing && !f.capture->Failed();
        }
        f.reference = static_cast<float>(std::atof(Settings().GetString("audio.vuReference").c_str()));
        if (f.viz.UpdateNeedles(f.frame, f.live, f.reference) && !f.live) Invalidate();  // falling to the stop
        const double now = AnimFloat::Now();
        for (int ch = 0; ch < 2; ++ch) {
            const float db = f.live ? Db(f.frame.peak[ch]) : -200.0f;
            // The peak readout holds the highest value for a moment, like a desk's peak meter.
            const double holdMs = f.opt.holdMs;
            if (db >= f.hold[ch].db || (holdMs >= 0 && now - f.hold[ch].at > holdMs)) f.hold[ch] = {db, now};
            if (db > f.max[ch].db) f.max[ch] = {db, now};
        }
        // The loudness graph takes a point every 100 ms while the sound plays.
        if (f.live && now - f.historyAt >= 100) {
            f.historyAt = now;
            f.history.push_back(f.frame.shortTerm > audio::kLoudnessNone ? f.frame.shortTerm : f.frame.momentary);
            if (f.history.size() > kHistoryMax) f.history.erase(f.history.begin());
        }
        p.Fill(rect, P().bgBase);
    }

private:
    // Shows the chosen cards; a row or column with none of its cards goes too.
    void Arrange() {
        const Feed::Options& o = feed_.opt;
        for (int i = 0; i < 5; ++i) cards_[i]->visible = o.cards[i];
        top_->visible = o.cards[0] || o.cards[1];
        left_->visible = o.cards[2] || o.cards[4];
        bottom_->visible = left_->visible || o.cards[3];
        // The level bars alone below: a strip of their own height, not half the window.
        const bool barsOnly = o.cards[2] && !o.cards[4] && !o.cards[3];
        bottom_->height = barsOnly && top_->visible ? Length::Px(132) : Length::Grow();
        cards_[2]->height = o.cards[4] ? Length::Px(132) : Length::Grow();
        top_->height = Length::Grow(1.1f);
    }

    void ApplyOnTop() {
        Host* host = GetHost();
        if (!host || !host->Hwnd()) return;
        onTopApplied_ = true;
        SetWindowPos(host->Hwnd(), feed_.opt.onTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void CommonMenu(std::vector<MenuItem>& items) {
        MenuItem show;
        show.label = Tr(L"Show");
        int shown = 0;
        for (bool on : feed_.opt.cards) shown += on;
        // Display order, not kCardKeys' order; the last card shown can't go.
        for (int i : {0, 1, 2, 3, 4}) show.submenu.push_back(CheckItem(kCardKeys[i], !(feed_.opt.cards[i] && shown == 1)));
        items.push_back(std::move(show));
        items.push_back(CheckItem("interface.metersOnTop"));
        items.push_back(MenuItem::Separator());
        items.push_back(ActionItem(Tr(L"Reset peaks and loudness"), [this] { Reset(); }, "refresh"));
        items.push_back(ActionItem(Tr(L"Restore the default meter settings"), [] {
            for (const char* k : kCardKeys) Settings().Reset(k);
            for (const char* k : kOptionKeys) Settings().Reset(k);
        }));
        items.push_back(MenuItem::Separator());
        items.push_back(ActionItem(Tr(L"Close the meters"), [] { Settings().SetBool("interface.metersWindow", false); },
                                   "close"));
    }

    Feed feed_;
    Widget* top_ = nullptr;
    Widget* bottom_ = nullptr;
    Widget* left_ = nullptr;
    Widget* cards_[5] = {};  // kCardKeys order: VU, loudness, levels, stereo, spectrum
    uint64_t serial_ = 0;
    bool onTopApplied_ = false;
    Config::Token options_ = 0;
    ScopedConnection conn_;
};

}  // namespace

Widget* AddMetersView(Widget* parent) {
    return parent->Add<MetersView>();
}

void AddMetersTools(Widget* header, Widget* view) {
    auto* meters = static_cast<MetersView*>(view);
    auto* reset = header->Add<IconButton>("refresh", Tr(L"Reset peaks and loudness"), 40, 20);
    reset->height = Length::Px(40);
    reset->onClick = [meters] { meters->Reset(); };
}

}  // namespace app

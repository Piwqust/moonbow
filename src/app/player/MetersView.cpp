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
#include <memory>
#include <vector>

namespace app {

using namespace ui;

namespace {

constexpr unsigned kMetersTap = 2;       // Player::SetSignalTap listener of the meters window
constexpr int kHistory = 600;            // loudness history points: 60 s at 10 per second
constexpr float kHistoryTop = 0, kHistoryBottom = -50;  // LUFS
constexpr float kBarFloor = -60;         // level bars, dBFS

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

// What every card reads; the view refreshes it once per frame, before the cards paint.
struct Feed {
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

    void Reset() {
        for (Hold& h : max) h = {};
        for (Hold& h : hold) h = {};
        history.clear();
        if (capture) capture->ResetLoudness();
        frame.integrated = frame.range = audio::kLoudnessNone;
    }
};

// A card of the window: the panel colour, a title and an optional note on the right.
class Card : public Widget {
public:
    Card(Feed& feed, const wchar_t* title) : feed_(feed), title_(title) {
        width = Length::Grow();
        height = Length::Grow();
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
    Feed& feed_;

private:
    const wchar_t* title_;
};

// --- the analog VU pair -------------------------------------------------------------------------

settings::Lang SchemaLang() {
    return CurrentLanguage() == Language::Russian ? settings::Lang::Ru : settings::Lang::En;
}

// The calibration sits on the meters it moves, at the card's top right.
class VuCard : public Card {
public:
    explicit VuCard(Feed& f) : Card(f, L"VU meters") {
        const settings::Item* item = settings::FindItem("audio.vuReference");
        if (!item) return;
        std::vector<ui::Choice> choices;
        for (const settings::Choice& c : item->choices) {
            // "0 VU = −10 dBFS (loud modern masters)" is Settings' explanation; here the value and
            // a standard's name are enough.
            std::wstring label = c.Label(SchemaLang());
            const size_t open = label.find(L" (");
            if (open != std::wstring::npos && label.find(L' ', open + 2) != std::wstring::npos) label.resize(open);
            choices.push_back({label, c.value, {}});
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

private:
    // Short-term loudness over the last minute against a LUFS grid.
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
        const std::vector<float>& h = feed_.history;
        if (h.size() < 2) return;
        ID2D1DeviceContext5* ctx = p.Ctx();
        const float step = g.w / (kHistory - 1);
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

    void PaintBody(Painter& p, const Rect& r) override {
        constexpr float numW = 62, letterW = 18, lightW = 8;
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
            PaintBar(p, {barX, y + 3, barW, 12}, rms, peak, feed_.hold[ch].db);
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
        for (float db : {-60.f, -48.f, -36.f, -24.f, -18.f, -12.f, -6.f, -3.f, 0.f}) {
            if (barW < 360 && (db == -18 || db == -3)) continue;
            const float x = std::round(barX + barW * (db - kBarFloor) / -kBarFloor);
            p.Fill({x, sy, 1, 4}, P().lineStrong);
            p.Text(Number(db, 0), {x - 20, sy + 4, 40, 14}, TextStyle::Micro, P().textTertiary, HAlign::Center);
        }
    }

private:
    // Segments lit to the RMS level, dimmer up to the peak, and the held peak as one bright mark.
    static void PaintBar(Painter& p, const Rect& r, float rms, float peak, float hold) {
        constexpr float pitch = 4, seg = 3;
        const int n = std::max(1, static_cast<int>(r.w / pitch));
        const int holdAt = hold > kBarFloor ? std::min(n - 1, static_cast<int>((hold - kBarFloor) / -kBarFloor * n)) : -1;
        for (int i = 0; i < n; ++i) {
            const float db = kBarFloor + (i + 1) * -kBarFloor / n;  // the segment's top
            const float lo = db - -kBarFloor / n;
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
    void PaintBody(Painter& p, const Rect& r) override {
        const float meters = 92;
        const float side = std::floor(std::min(r.w, r.h - meters - 8));
        if (side > 40) PaintGonio(p, {std::round(r.Cx() - side / 2), r.y, side, side});
        const audio::VisualFrame& f = feed_.frame;
        const float y = r.Bottom() - meters;
        // +1: the channels move together (mono); 0: unrelated; below 0: out of phase, which
        // cancels out on a mono speaker.
        const float corr = f.correlation;
        PaintScale(p, {r.x, y, r.w, 40}, Tr(L"Correlation"), feed_.live ? Number(corr, 2, true) : L"—",
                   feed_.live ? corr : 0, feed_.live, corr < 0 ? Red() : P().accent, L"−1", L"0", L"+1");
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
        if (auto trace = Polyline(ctx, pts.data(), pts.size(), false))
            if (ID2D1SolidColorBrush* b = feed_.viz.LineBrush(ctx, P().accent, 0.55f * p.Opacity()))
                ctx->DrawGeometry(trace.Get(), b, 1.0f, feed_.viz.RoundStroke(ctx));
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
        feed_.viz.Spectrum(p, bars, feed_.frame, P().accent, false, false, !feed_.live);
        // Bands are log-spaced from 40 Hz to 16 kHz (audio::SpectrumAnalyzer).
        const float step = bars.w / audio::kBands;
        for (int hz : {50, 100, 200, 500, 1000, 2000, 5000, 10000}) {
            const float x = bars.x + step * audio::kBands * std::log(hz / 40.0f) / std::log(400.0f) + step * 0.39f;
            const std::wstring t = hz >= 1000 ? std::to_wstring(hz / 1000) + Tr(L"k") : std::to_wstring(hz);
            p.Text(t, {x - 20, bars.Bottom() + 4, 40, 14}, TextStyle::Micro, P().textTertiary, HAlign::Center);
        }
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
        auto* top = Add<Widget>();
        top->dir = Dir::Row;
        top->gap = 8;
        top->width = Length::Grow();
        top->height = Length::Grow(1.1f);
        top->Add<VuCard>(feed_);
        loudness_ = top->Add<LoudnessCard>(feed_);
        auto* bottom = Add<Widget>();
        bottom->dir = Dir::Row;
        bottom->gap = 8;
        bottom->width = Length::Grow();
        bottom->height = Length::Grow();
        auto* left = bottom->Add<Widget>();
        left->dir = Dir::Column;
        left->gap = 8;
        left->width = Length::Grow();
        left->height = Length::Grow();
        left->Add<LevelsCard>(feed_);
        left->Add<SpectrumCard>(feed_);
        stereo_ = bottom->Add<StereoCard>(feed_);

        TheApp().player().SetSignalTap(true, kMetersTap);
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeTime | kChangeState | kChangeFile | kChangeVolume)) Invalidate();
        });
    }
    ~MetersView() override {
        TheApp().player().SetSignalTap(false, kMetersTap);
    }

    void Reset() {
        feed_.Reset();
        Invalidate();
    }

    void OnLayout() override {
        const float side = std::round(std::clamp(rect.w * 0.32f, 240.0f, 340.0f));
        loudness_->width = stereo_->width = Length::Px(side);
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
            if (db >= f.hold[ch].db || now - f.hold[ch].at > 1500) f.hold[ch] = {db, now};
            if (db > f.max[ch].db) f.max[ch] = {db, now};
        }
        // The loudness graph takes a point every 100 ms while the sound plays.
        if (f.live && now - f.historyAt >= 100) {
            f.historyAt = now;
            f.history.push_back(f.frame.shortTerm > audio::kLoudnessNone ? f.frame.shortTerm : f.frame.momentary);
            if (f.history.size() > kHistory) f.history.erase(f.history.begin());
        }
        p.Fill(rect, P().bgBase);
    }

private:
    Feed feed_;
    Widget* loudness_ = nullptr;
    Widget* stereo_ = nullptr;
    uint64_t serial_ = 0;
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

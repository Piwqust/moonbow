#include "app/player/VizPaint.h"

#include "gfx/Gpu.h"
#include "ui/Anim.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace app {

using namespace ui;

bool VizPaint::UpdateNeedles(const audio::VisualFrame& frame, bool live, float referenceDb) {
    // The needles swing by the meter's own physics between frames; silence lets them fall.
    const double now = AnimFloat::Now();
    const float dt = vuAt_ > 0 ? static_cast<float>((now - vuAt_) / 1000) : 0.0f;
    vuAt_ = now;
    bool moving = false;
    for (int ch = 0; ch < 2; ++ch) {
        audio::UpdateVu(needles_[ch], live ? audio::VuTarget(frame.rms[ch], referenceDb) : 0.0f, dt);
        moving = moving || needles_[ch].pos > 0.001f;
        Hold& h = holds_[ch];
        if (needles_[ch].pos >= h.pos || now - h.at > 1500) h = {needles_[ch].pos, now};
    }
    return moving;
}

ID2D1StrokeStyle* VizPaint::RoundStroke(ID2D1DeviceContext5* ctx) {
    const uint32_t gen = gfx::Gpu::Get().Generation();
    if (roundStroke_ && strokeGen_ == gen) return roundStroke_.Get();
    roundStroke_.Reset();
    strokeGen_ = gen;
    gfx::ComPtr<ID2D1Factory> factory;
    ctx->GetFactory(&factory);
    const auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                   D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    if (factory) factory->CreateStrokeStyle(props, nullptr, 0, &roundStroke_);
    return roundStroke_.Get();
}

// Lines go straight to the context (Painter has no line call); one brush, made once per device.
ID2D1SolidColorBrush* VizPaint::LineBrush(ID2D1DeviceContext5* ctx, Color c, float opacity) {
    const uint32_t gen = gfx::Gpu::Get().Generation();
    if (!lineBrush_ || lineGen_ != gen) {
        lineBrush_.Reset();
        lineGen_ = gen;
        if (FAILED(ctx->CreateSolidColorBrush(c.D2D(), &lineBrush_))) return nullptr;
    }
    lineBrush_->SetColor(c.Fade(opacity).D2D());
    return lineBrush_.Get();
}

// AIMP-style bars: stacked segments that brighten towards the top, and a peak drop over each
// bar that hangs a moment, then falls. Bars rise from the band's bottom edge; `wing` centres
// them on the band's middle, growing both ways and fading away from the cover. `reverse`
// puts the low notes on the right.
void VizPaint::Spectrum(Painter& p, const Rect& r, const audio::VisualFrame& frame, Color white, bool reverse,
                        bool wing, bool rest) {
    constexpr int n = audio::kBands;
    const float step = r.w / n;
    const float seg = std::max(2.0f, std::round(r.h / 110)), space = std::max(1.0f, std::round(seg * 0.6f));
    const float pitch = seg + space;
    const int rows = std::max(1, static_cast<int>(r.h * (wing ? 0.45f : 0.9f) / pitch));
    const float base = wing ? std::round(r.Cy() + pitch / 2) : r.Bottom();
    for (int i = 0; i < n; ++i) {
        const int slot = reverse ? n - 1 - i : i;
        const float x0 = std::round(r.x + slot * step), x1 = std::round(r.x + (slot + 0.78f) * step);
        const float fade = wing ? 1.0f - 0.7f * i / (n - 1) : 1.0f;
        const int lit = rest ? 1 : std::max(1, static_cast<int>(std::round(frame.bands[i] * rows)));
        for (int k = 0; k < lit; ++k) {
            const Color c = white.Fade((0.38f + 0.5f * k / rows) * fade);
            p.Fill({x0, base - (k + 1) * pitch + space, x1 - x0, seg}, c);
            if (wing && k > 0) p.Fill({x0, base + space + (k - 1) * pitch, x1 - x0, seg}, c);
        }
        if (!rest && frame.peaks[i] > 0.02f) {
            // A drop sits on its bar, never inside it.
            const int at = std::max(static_cast<int>(std::round(frame.peaks[i] * rows)), lit);
            const Color c = white.Fade(0.95f * fade);
            p.Fill({x0, base - (at + 1) * pitch + space, x1 - x0, seg}, c);
            if (wing) p.Fill({x0, base + space + (at - 1) * pitch, x1 - x0, seg}, c);
        }
    }
}

// The recent loudness scrolling to the left, mirrored around the middle; older columns fade.
void VizPaint::Waveform(Painter& p, const Rect& r, const audio::VisualFrame& frame, Color white) {
    constexpr int n = audio::kWaveColumns;
    const float step = r.w / n;
    const float bw = std::max(2.0f, std::round(step * 0.5f));
    const float cy = r.Cy();
    for (int i = 0; i < n; ++i) {
        const float x = std::round(r.x + i * step + (step - bw) / 2);
        const float h = std::max(2.0f, std::round(frame.wave[i] * r.h * 0.9f / 2) * 2);
        const float k = static_cast<float>(i) / (n - 1);
        p.Fill({x, std::round(cy - h / 2), bw, h}, white.Fade(0.22f + 0.64f * k * k), bw / 2);
    }
}

// An oscilloscope trace across the band, over a faint centre line.
void VizPaint::Scope(Painter& p, const Rect& r, const audio::VisualFrame& frame, Color fg, Color dim) {
    ID2D1DeviceContext5* ctx = p.Ctx();
    const float cy = r.Cy(), amp = r.h * 0.42f;
    p.Fill({r.x, std::round(cy), r.w, 1}, dim.Fade(0.5f));
    ID2D1StrokeStyle* stroke = RoundStroke(ctx);
    ID2D1SolidColorBrush* brush = LineBrush(ctx, fg, p.Opacity());
    if (!brush) return;
    constexpr int n = audio::kScopePoints;
    D2D1_POINT_2F prev{r.x, cy - frame.scope[0] * amp};
    for (int i = 1; i < n; ++i) {
        const D2D1_POINT_2F pt{r.x + r.w * static_cast<float>(i) / (n - 1), cy - frame.scope[i] * amp};
        ctx->DrawLine(prev, pt, brush, 2.0f, stroke);
        prev = pt;
    }
}

// --- VU meters in the player's own look: the scale is a track like the seek bar's, filled up to
// the needle, with a held peak mark and the reading in numbers. One ink colour at a few strengths,
// so they sit on the music view's dark ground and on the meters window's cards alike.
// The largest meter that fits `r`, centered in it.
Rect VizPaint::FitVu(const Rect& r) {
    const float w = std::floor(std::min(r.w, r.h * kVuAspect)), h = std::floor(w / kVuAspect);
    return {std::round(r.Cx() - w / 2), std::round(r.Cy() - h / 2), w, h};
}

// Left and right meters side by side, or one above the other when `r` is tall.
void VizPaint::VuPair(Painter& p, const Rect& r, bool rest, Color ink) {
    constexpr float space = 24;
    const float wSide = std::min((r.w - space) / 2, r.h * kVuAspect);
    const float wStack = std::min(r.w, (r.h - space) / 2 * kVuAspect);
    const float w = std::floor(std::max(wSide, wStack)), h = std::floor(w / kVuAspect);
    if (w < 40) return;
    if (wSide >= wStack) {
        const float x = std::round(r.Cx() - (2 * w + space) / 2), y = std::round(r.Cy() - h / 2);
        VuMeter(p, {x, y, w, h}, 0, rest, ink);
        VuMeter(p, {x + w + space, y, w, h}, 1, rest, ink);
    } else {
        const float x = std::round(r.Cx() - w / 2), y = std::round(r.Cy() - (2 * h + space) / 2);
        VuMeter(p, {x, y, w, h}, 0, rest, ink);
        VuMeter(p, {x, y + h + space, w, h}, 1, rest, ink);
    }
}

namespace {

constexpr float kSwing = 0.96f;  // radians each side of upright, from the stop to +3 VU

// Where `vu` sits between the stop (0) and +3 VU (1): the needle's swing is linear in voltage.
float VuPos(float vu) {
    return std::pow(10.0f, (vu - 3.0f) / 20.0f);
}

// A meter's geometry: the needle turns about a pivot low in the middle; the scale is an arc.
struct VuGeom {
    D2D1_POINT_2F pivot;
    float R;
    static VuGeom Of(const Rect& m) { return {{m.Cx(), m.y + m.h * 0.9f}, m.h * 0.74f}; }
    D2D1_POINT_2F At(float pos, float rad) const {
        const float a = -kSwing + 2 * kSwing * std::clamp(pos, 0.0f, 1.04f);
        return {pivot.x + std::sin(a) * rad, pivot.y - std::cos(a) * rad};
    }
};

void Arc(ID2D1DeviceContext* c, const VuGeom& g, float from, float to, float rad, ID2D1Brush* b, float width,
         ID2D1StrokeStyle* stroke) {
    if (to - from < 1e-4f) return;
    gfx::ComPtr<ID2D1PathGeometry> path;
    gfx::ComPtr<ID2D1Factory> factory;
    c->GetFactory(&factory);
    gfx::ComPtr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
    const int steps = std::max(2, static_cast<int>(48 * (to - from)));
    sink->BeginFigure(g.At(from, rad), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i <= steps; ++i) sink->AddLine(g.At(from + (to - from) * i / steps, rad));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (SUCCEEDED(sink->Close())) c->DrawGeometry(path.Get(), b, width, stroke);
}

struct Mark {
    int vu;
    const wchar_t* text;  // nullptr: a tick without a number
};
constexpr Mark kMarks[] = {{-20, L"−20"}, {-10, L"−10"}, {-7, nullptr}, {-5, L"−5"}, {-3, L"−3"}, {-2, nullptr},
                           {-1, L"−1"},   {0, L"0"},     {1, L"+1"},    {2, nullptr}, {3, L"+3"}};

}  // namespace

void VizPaint::VuMeter(Painter& p, const Rect& m, int ch, bool rest, Color ink) {
    ID2D1DeviceContext5* ctx = p.Ctx();
    ID2D1StrokeStyle* round = RoundStroke(ctx);
    const float o = p.Opacity();
    const VuGeom g = VuGeom::Of(m);
    const bool big = m.h >= 150;
    const float track = std::max(4.0f, std::round(m.h * 0.04f));
    const float zero = VuPos(0);
    const float pos = rest ? 0.0f : needles_[ch].pos;
    auto line = [&](float alpha) { return LineBrush(ctx, ink, alpha * o); };

    // The track, a little stronger past 0 VU where a desk meter turns red; then the level.
    if (ID2D1SolidColorBrush* b = line(0.10f)) Arc(ctx, g, 0, zero, g.R, b, track, round);
    if (ID2D1SolidColorBrush* b = line(0.26f)) Arc(ctx, g, zero, 1, g.R, b, track, round);
    if (pos > 0.002f)
        if (ID2D1SolidColorBrush* b = line(0.8f)) Arc(ctx, g, 0, std::min(pos, 1.0f), g.R, b, track, round);

    // Ticks outside the track, numbers outside the ticks, upright in the player's type.
    const TextStyle labelStyle = big ? TextStyle::Small : TextStyle::Micro;
    const float tick0 = g.R + track / 2 + 3, tickLen = std::max(3.0f, m.h * 0.035f);
    const float labelR = tick0 + tickLen + (big ? 12.0f : 9.0f);
    for (const Mark& mk : kMarks) {
        const float at = VuPos(static_cast<float>(mk.vu));
        const bool hot = mk.vu >= 0;
        if (ID2D1SolidColorBrush* b = line(mk.text ? (hot ? 0.7f : 0.45f) : 0.3f))
            ctx->DrawLine(g.At(at, tick0), g.At(at, tick0 + (mk.text ? tickLen : tickLen * 0.6f)), b, 1.5f, round);
        if (!mk.text || (!big && (mk.vu == -1 || mk.vu == 1 || mk.vu == -3))) continue;
        const D2D1_POINT_2F c = g.At(at, labelR);
        p.Text(mk.text, {c.x - 20, c.y - 8, 40, 16}, labelStyle, ink.Fade(mk.vu == 0 ? 0.9f : hot ? 0.7f : 0.5f),
               HAlign::Center);
    }

    // The held peak: a bright mark across the track that stays a moment, as on a desk.
    if (!rest && holds_[ch].pos > zero * 0.15f)
        if (ID2D1SolidColorBrush* b = line(1.0f))
            ctx->DrawLine(g.At(holds_[ch].pos, g.R - track / 2 - 2), g.At(holds_[ch].pos, g.R + track / 2 + 2), b, 2.0f,
                          round);

    // The needle from the pivot through the track, and the pivot.
    if (ID2D1SolidColorBrush* b = line(1.0f)) {
        ctx->DrawLine(g.At(pos, m.h * 0.06f), g.At(pos, g.R + track / 2 + 1), b, std::max(1.5f, m.h * 0.012f), round);
        ctx->DrawEllipse(D2D1::Ellipse(g.pivot, m.h * 0.035f, m.h * 0.035f), b, 1.5f);
    }
    p.FillCircle({g.pivot.x, g.pivot.y}, m.h * 0.018f, ink);

    // The channel at the bottom left, the reading in VU at the bottom right.
    const float rowH = big ? 18.0f : 14.0f, rowY = m.Bottom() - rowH;
    p.Text(ch == 0 ? L"L" : L"R", {m.x, rowY, 40, rowH}, big ? TextStyle::SmallMedium : TextStyle::Micro, ink.Fade(0.6f));
    std::wstring value = L"—";
    if (!rest && pos > 0.01f) {
        wchar_t t[32];
        swprintf_s(t, L"%+.1f", 20 * std::log10(pos) + 3);
        value = t[0] == L'-' ? L"−" + std::wstring(t + 1) : t;
    }
    p.Text(value + L" VU", {m.Right() - 90, rowY, 90, rowH}, big ? TextStyle::MonoSmall : TextStyle::Micro,
           ink.Fade(pos > zero && !rest ? 1.0f : 0.6f), HAlign::Trailing);
}

}  // namespace app

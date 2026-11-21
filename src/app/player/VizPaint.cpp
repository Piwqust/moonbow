#include "app/player/VizPaint.h"

#include "gfx/Gpu.h"
#include "ui/Anim.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string_view>

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

ID2D1StrokeStyle* VizPaint::CapStroke(ID2D1DeviceContext5* ctx, bool roundStart, bool roundEnd) {
    const uint32_t gen = gfx::Gpu::Get().Generation();
    if (capGen_ != gen) {
        for (auto& st : capStrokes_) st.Reset();
        capGen_ = gen;
    }
    gfx::ComPtr<ID2D1StrokeStyle>& st = capStrokes_[(roundStart ? 1 : 0) | (roundEnd ? 2 : 0)];
    if (!st) {
        gfx::ComPtr<ID2D1Factory> factory;
        ctx->GetFactory(&factory);
        const auto props = D2D1::StrokeStyleProperties(roundStart ? D2D1_CAP_STYLE_ROUND : D2D1_CAP_STYLE_FLAT,
                                                       roundEnd ? D2D1_CAP_STYLE_ROUND : D2D1_CAP_STYLE_FLAT,
                                                       D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND);
        if (factory) factory->CreateStrokeStyle(props, nullptr, 0, &st);
    }
    return st.Get();
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
                        bool wing, bool rest, bool peaks) {
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
        if (peaks && !rest && frame.peaks[i] > 0.02f) {
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

// Left and right meters side by side, or one above the other when that makes them larger. The gap
// between them follows the space, so a small pair stays tight and a large one breathes.
bool VizPaint::PlaceVuPair(const Rect& r, Rect& left, Rect& right) {
    const float space = std::clamp(std::min(r.w, r.h) * 0.04f, 12.0f, 48.0f);
    const float wSide = std::min((r.w - space) / 2, r.h * kVuAspect);
    const float wStack = std::min(r.w, (r.h - space) / 2 * kVuAspect);
    const float w = std::floor(std::max(wSide, wStack)), h = std::floor(w / kVuAspect);
    if (w < 40) return false;
    if (wSide >= wStack) {
        const float x = std::round(r.Cx() - (2 * w + space) / 2), y = std::round(r.Cy() - h / 2);
        left = {x, y, w, h};
        right = {std::round(x + w + space), y, w, h};
    } else {
        const float x = std::round(r.Cx() - w / 2), y = std::round(r.Cy() - (2 * h + space) / 2);
        left = {x, y, w, h};
        right = {x, std::round(y + h + space), w, h};
    }
    return true;
}

Rect VizPaint::VuPairBox(const Rect& r) {
    Rect a, b;
    if (!PlaceVuPair(r, a, b)) return {};
    return {a.x, a.y, b.Right() - a.x, b.Bottom() - a.y};
}

void VizPaint::VuPair(Painter& p, const Rect& r, bool rest, Color ink) {
    Rect a, b;
    if (!PlaceVuPair(r, a, b)) return;
    VuMeter(p, a, 0, rest, ink);
    VuMeter(p, b, 1, rest, ink);
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

// The meter's numbers at any size: a 12 DIP style drawn under a scale about `at`, so they grow
// with the meter instead of stepping between the fixed styles. `at` is the text's centre, or the
// middle of its left (Leading) or right (Trailing) edge.
constexpr float kBaseText = 12;  // Small, SmallMedium and MonoSmall
void SizedText(Painter& p, std::wstring_view text, D2D1_POINT_2F at, HAlign align, TextStyle style, float size,
               Color c) {
    const gfx::TextMetrics tm = p.Measure(text, style);
    const float w = tm.width + 4, h = tm.height;
    const float x = align == HAlign::Center ? at.x - w / 2 : align == HAlign::Leading ? at.x : at.x - w;
    ID2D1DeviceContext5* ctx = p.Ctx();
    D2D1_MATRIX_3X2_F old;
    ctx->GetTransform(&old);
    ctx->SetTransform(D2D1::Matrix3x2F::Scale(size / kBaseText, size / kBaseText, at) * old);
    p.Text(text, {x, at.y - h / 2, w, h}, style, c, align);
    ctx->SetTransform(old);
}

}  // namespace

void VizPaint::VuMeter(Painter& p, const Rect& m, int ch, bool rest, Color ink) {
    ID2D1DeviceContext5* ctx = p.Ctx();
    const float o = p.Opacity();
    const VuGeom g = VuGeom::Of(m);
    // Everything follows the meter's height, from a wing beside a small cover to a 4K screen; the
    // numbers grow too, and drop away where they would crowd: the in-between ones under 150 DIP,
    // all of them under 70, the channel and the reading under 56.
    const float u = m.h;
    const float track = std::max(3.0f, std::round(u * 0.03f));
    const float zero = VuPos(0);
    const float pos = rest ? 0.0f : needles_[ch].pos;
    const float labelSize = std::clamp(7.0f + u * 0.022f, 9.0f, 28.0f);
    const bool labels = vuScale && u >= 70, allLabels = u >= 150, row = vuCaption && u >= 56;
    auto line = [&](float alpha) { return LineBrush(ctx, ink, alpha * o); };

    // The track in one piece, then the part past 0 VU over it, where a desk meter turns red: its
    // flat start is a clean edge at 0, with no cap bulging into the quiet part.
    if (ID2D1SolidColorBrush* b = line(0.10f)) Arc(ctx, g, 0, 1, g.R, b, track, CapStroke(ctx, true, true));
    if (ID2D1SolidColorBrush* b = line(0.18f)) Arc(ctx, g, zero, 1, g.R, b, track, CapStroke(ctx, false, true));
    // The level, from the stop to the needle: flat at the needle's end, so it stops on the needle.
    if (pos > 0.002f)
        if (ID2D1SolidColorBrush* b = line(0.75f))
            Arc(ctx, g, 0, std::min(pos, 1.0f), g.R, b, track, CapStroke(ctx, true, false));

    // Ticks outside the track; each number outside its tick at the same gap whatever its width:
    // pushed out along the tick by the half of the label that faces the arc.
    const float tick0 = g.R + track / 2 + std::max(2.0f, u * 0.012f), tickLen = std::max(3.0f, u * 0.04f);
    const float tickW = std::max(1.25f, u * 0.0035f);
    const float labelGap = labelSize * 0.45f;
    ID2D1StrokeStyle* flat = CapStroke(ctx, false, false);
    for (const Mark& mk : kMarks) {
        const float at = VuPos(static_cast<float>(mk.vu));
        const bool hot = mk.vu >= 0;
        const float len = mk.text ? tickLen : tickLen * 0.55f;
        if (ID2D1SolidColorBrush* b = line(mk.text ? (hot ? 0.7f : 0.45f) : 0.3f))
            ctx->DrawLine(g.At(at, tick0), g.At(at, tick0 + len), b, mk.vu == 0 ? tickW * 1.4f : tickW, flat);
        if (!mk.text || !labels || (!allLabels && (mk.vu == -1 || mk.vu == 1 || mk.vu == -3))) continue;
        const gfx::TextMetrics tm = p.Measure(mk.text, TextStyle::Small);
        const float k = labelSize / kBaseText;
        const D2D1_POINT_2F a = g.At(at, 0), b = g.At(at, 1);
        const float dx = b.x - a.x, dy = b.y - a.y;  // the tick's direction, unit length
        const float half = (std::fabs(dx) * tm.width / 2 + std::fabs(dy) * tm.height / 2) * k;
        SizedText(p, mk.text, g.At(at, tick0 + tickLen + labelGap + half), HAlign::Center, TextStyle::Small,
                  labelSize, ink.Fade(mk.vu == 0 ? 0.9f : hot ? 0.7f : 0.5f));
    }

    // The held peak: a bright bar across the track that stays a moment, as on a desk.
    // On the track, never past its end: the needle may rest on the pin beyond +3, the mark stays at +3.
    const float held = std::min(holds_[ch].pos, 1.0f);
    if (vuPeak && !rest && held > zero * 0.15f)
        if (ID2D1SolidColorBrush* b = line(1.0f)) {
            const float over = std::max(2.0f, u * 0.006f);
            ctx->DrawLine(g.At(held, g.R - track / 2 - over), g.At(held, g.R + track / 2 + over), b,
                          std::max(2.0f, u * 0.005f), flat);
        }

    // The needle: one tapered blade from a short tail behind the hub into the ticks, so it reads
    // the scale like a real pointer; the hub sits over its root.
    const float hub = std::max(2.5f, std::round(u * 0.022f));
    const float w0 = std::max(2.0f, u * 0.011f), w1 = std::max(1.0f, u * 0.0035f);
    const float r0 = -hub * 2.2f, r1 = tick0 + tickLen * 0.6f;
    gfx::ComPtr<ID2D1Factory> factory;
    ctx->GetFactory(&factory);
    gfx::ComPtr<ID2D1PathGeometry> blade;
    gfx::ComPtr<ID2D1GeometrySink> sink;
    if (factory && SUCCEEDED(factory->CreatePathGeometry(&blade)) && SUCCEEDED(blade->Open(&sink))) {
        const D2D1_POINT_2F a = g.At(pos, 0), b = g.At(pos, 1);
        const float dx = b.x - a.x, dy = b.y - a.y, nx = -dy, ny = dx;  // along and across the needle
        auto pt = [&](float r, float w) {
            return D2D1::Point2F(g.pivot.x + dx * r + nx * w / 2, g.pivot.y + dy * r + ny * w / 2);
        };
        const D2D1_POINT_2F pts[] = {pt(r0, w0 * 0.8f), pt(0, w0), pt(r1, w1), pt(r1, -w1), pt(0, -w0)};
        sink->BeginFigure(pt(r0, -w0 * 0.8f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(pts, static_cast<UINT32>(std::size(pts)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (SUCCEEDED(sink->Close())) p.FillGeometry(blade.Get(), ink);
    }
    p.FillCircle({g.pivot.x, g.pivot.y}, hub, ink);

    // The channel under the scale's left end and the reading under its right end, on the hub's line.
    if (!row) return;
    const float rowSize = std::clamp(7.5f + u * 0.024f, 9.0f, 30.0f);
    const float rowY = std::min(g.pivot.y, m.Bottom() - rowSize * 0.75f);
    const float left = g.At(0, g.R).x - track / 2, right = g.At(1, g.R).x + track / 2;
    SizedText(p, ch == 0 ? L"L" : L"R", {left, rowY}, HAlign::Leading, TextStyle::SmallMedium, rowSize, ink.Fade(0.6f));
    std::wstring value = L"—";
    if (!rest && pos > 1.0f) {
        value = L">+3";  // past the scale (the swing over it, or on the pin): the level is not known
    } else if (!rest && pos > 0.01f) {
        wchar_t t[32];
        swprintf_s(t, L"%+.1f", 20 * std::log10(pos) + 3);
        value = t[0] == L'-' ? L"−" + std::wstring(t + 1) : t;
    }
    SizedText(p, value + L" VU", {right, rowY}, HAlign::Trailing, TextStyle::MonoSmall, rowSize,
              ink.Fade(pos > zero && !rest ? 1.0f : 0.6f));
}

}  // namespace app

#include "app/player/VizPaint.h"

#include "gfx/Gpu.h"
#include "ui/Anim.h"

#include <algorithm>
#include <cmath>

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

// --- VU meters: a backlit dial, a scale from -20 to +3 VU with the red zone past 0, and a
// needle rising out of a cap at the bottom (AIMP's analog meters).
// The largest meter that fits `r`, centered in it.
Rect VizPaint::FitVu(const Rect& r) {
    const float w = std::floor(std::min(r.w, r.h * kVuAspect)), h = std::floor(w / kVuAspect);
    return {std::round(r.Cx() - w / 2), std::round(r.Cy() - h / 2), w, h};
}

// Left and right meters side by side, or one above the other when `r` is tall.
void VizPaint::VuPair(Painter& p, const Rect& r, bool rest) {
    constexpr float space = 24;
    const float wSide = std::min((r.w - space) / 2, r.h * kVuAspect);
    const float wStack = std::min(r.w, (r.h - space) / 2 * kVuAspect);
    const float w = std::floor(std::max(wSide, wStack)), h = std::floor(w / kVuAspect);
    if (w < 40) return;
    if (wSide >= wStack) {
        const float x = std::round(r.Cx() - (2 * w + space) / 2), y = std::round(r.Cy() - h / 2);
        VuMeter(p, {x, y, w, h}, 0, rest);
        VuMeter(p, {x + w + space, y, w, h}, 1, rest);
    } else {
        const float x = std::round(r.Cx() - w / 2), y = std::round(r.Cy() - (2 * h + space) / 2);
        VuMeter(p, {x, y, w, h}, 0, rest);
        VuMeter(p, {x, y + h + space, w, h}, 1, rest);
    }
}

void VizPaint::VuMeter(Painter& p, const Rect& m, int ch, bool rest) {
    ID2D1DeviceContext5* ctx = p.Ctx();
    const Color white = Color::Hex(0xFFFFFF);
    const Color red = Color::Hex(0xFF5A4E);
    const Color scale = Color::Hex(0xFFE7C2);  // warm, like a lamp behind the dial
    const float radius = std::round(m.h * 0.07f);
    p.Fill(m, white.Fade(0.06f), radius);
    p.Stroke(m, white.Fade(0.12f), radius);
    const Point pivot{m.Cx(), m.Bottom() - m.h * 0.02f};
    const float R = m.h * 0.7f;
    constexpr float kSwing = 0.82f;  // radians each side of upright
    auto at = [&](float pos, float rad) {
        const float a = -kSwing + 2 * kSwing * pos;
        return D2D1_POINT_2F{pivot.x + std::sin(a) * rad, pivot.y - std::cos(a) * rad};
    };
    auto vuPos = [](float vu) { return std::pow(10.0f, (vu - 3.0f) / 20.0f); };
    ID2D1StrokeStyle* round = RoundStroke(ctx);
    const float o = p.Opacity();
    const float line = std::max(1.0f, std::round(m.h * 0.008f * 2) / 2);
    // The arc, then the red band just outside it from 0 VU on.
    auto arc = [&](float from, float to, float rad, Color c, float thick) {
        ID2D1SolidColorBrush* b = LineBrush(ctx, c, o);
        if (!b) return;
        constexpr int steps = 40;
        D2D1_POINT_2F prev = at(from, rad);
        for (int i = 1; i <= steps; ++i) {
            const D2D1_POINT_2F pt = at(from + (to - from) * i / steps, rad);
            ctx->DrawLine(prev, pt, b, thick, round);
            prev = pt;
        }
    };
    arc(vuPos(-20), 1, R, scale.Fade(0.75f), line);
    const float band = std::max(3.0f, m.h * 0.03f);
    arc(vuPos(0), 1, R + 2 + band / 2, red.Fade(0.85f), band);
    struct Mark {
        int vu;
        const wchar_t* text;
        bool minor;  // left out on a small meter, where the labels would touch
    };
    static const Mark kMarks[] = {{-20, L"-20", false}, {-10, L"-10", false}, {-7, L"-7", true},
                                  {-5, L"-5", false},   {-3, L"-3", true},    {-2, L"-2", true},
                                  {-1, L"-1", true},    {0, L"0", false},     {1, L"+1", true},
                                  {2, L"+2", true},     {3, L"+3", false}};
    const bool big = m.w >= 260;
    const TextStyle labelStyle = m.h >= 240 ? TextStyle::Body : TextStyle::Small;
    for (const Mark& mk : kMarks) {
        const float pos = vuPos(static_cast<float>(mk.vu));
        const Color c = mk.vu >= 0 ? red : scale;
        if (ID2D1SolidColorBrush* b = LineBrush(ctx, c.Fade(0.9f), o))
            ctx->DrawLine(at(pos, R - m.h * 0.05f), at(pos, R), b, line * 1.5f, round);
        if (!big && mk.minor) continue;
        const D2D1_POINT_2F t = at(pos, R + band + m.h * 0.09f);
        p.Text(mk.text, {t.x - 20, t.y - 10, 40, 20}, labelStyle, c.Fade(0.9f), HAlign::Center);
    }
    const TextStyle tagStyle = m.h >= 240 ? TextStyle::BodyMedium : TextStyle::SmallMedium;
    const float inset = std::round(m.h * 0.06f);
    p.Text(L"VU", {m.x + inset, m.y + inset, 60, 20}, tagStyle, scale.Fade(0.6f));
    p.Text(ch == 0 ? L"L" : L"R", {m.Right() - inset - 60, m.y + inset, 60, 20}, tagStyle, scale.Fade(0.6f),
           HAlign::Trailing);
    // The needle, from under the cap to just past the scale.
    const float pos = rest ? 0.0f : needles_[ch].pos;
    p.PushClip(m);
    if (ID2D1SolidColorBrush* b = LineBrush(ctx, red, o))
        ctx->DrawLine(at(pos, m.h * 0.1f), at(pos, R + band + m.h * 0.04f), b, std::max(1.5f, line * 2), round);
    const float cap = m.h * 0.17f;
    p.FillCircle({m.Cx(), m.Bottom()}, cap, Color::Hex(0x0B0C10));
    p.StrokeCircle({m.Cx(), m.Bottom()}, cap, white.Fade(0.12f));
    p.FillCircle({m.Cx(), m.Bottom() - cap * 0.45f}, cap * 0.22f, white.Fade(0.8f));
    p.PopClip();
}

}  // namespace app

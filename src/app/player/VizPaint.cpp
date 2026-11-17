#include "app/player/VizPaint.h"

#include "gfx/Gpu.h"
#include "ui/Anim.h"

#include <d2d1effects.h>

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

// --- VU meters, after AIMP's analog ones: a grey face with a backlit scale from -20 to +3 VU
// that glows yellow, red past 0, and a red needle out of a black hump at the bottom.
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

namespace {

// CLSID_D2D1GaussianBlur (defined here so we don't need dxguid.lib).
constexpr GUID kVuBlur = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
// CLSID_D2D1ColorMatrix: scales the glow's light.
constexpr GUID kVuColorMatrix = {0x921f03d6, 0x641c, 0x47df, {0x85, 0x2d, 0xb4, 0xbb, 0x61, 0x53, 0xae, 0x11}};

constexpr float kSwing = 0.82f;  // radians each side of upright, from the stop to +3 VU

// Where `vu` sits between the stop (0) and +3 VU (1): the needle's swing is linear in voltage.
float VuPos(float vu) {
    return std::pow(10.0f, (vu - 3.0f) / 20.0f);
}

// A meter's geometry in its own DIPs: the needle turns about a pivot at the middle of the
// bottom edge, under the black hump; the scale is an arc around it.
struct VuGeom {
    float w, h, R;
    D2D1_POINT_2F pivot;
    static VuGeom Of(float w, float h) { return {w, h, h * 0.72f, {w / 2, h}}; }
    float Angle(float pos) const { return -kSwing + 2 * kSwing * pos; }
    D2D1_POINT_2F At(float pos, float rad) const {
        const float a = Angle(pos);
        return {pivot.x + std::sin(a) * rad, pivot.y - std::cos(a) * rad};
    }
};

D2D1_COLOR_F Rgb(uint32_t hex, float a = 1) {
    return {((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, a};
}

constexpr uint32_t kFace = 0x3A3B3E, kYellow = 0xFFD54F, kYellowText = 0xFFE38A, kRed = 0xFF2E2E,
                   kRedText = 0xFFE6E0, kInk = 0x8C8D91, kHump = 0x111214;

struct Mark {
    int vu;
    const wchar_t* text;
    bool minor;  // left out on a small meter, where the labels would touch
};
constexpr Mark kMarks[] = {{-20, L"−20", false}, {-10, L"−10", false}, {-7, L"−7", true},
                           {-5, L"−5", false},   {-3, L"−3", true},    {-2, L"−2", true},
                           {-1, L"−1", true},    {0, L"0", false},          {1, L"+1", true},
                           {2, L"+2", true},          {3, L"+3", false}};

// A condensed gauge face (Bahnschrift, in Windows since 10), else the system font.
gfx::ComPtr<IDWriteTextFormat> GaugeFont(float size, DWRITE_FONT_WEIGHT weight) {
    gfx::ComPtr<IDWriteTextFormat> f;
    IDWriteFactory5* dw = gfx::Gpu::Get().DWrite();
    if (!dw || FAILED(dw->CreateTextFormat(L"Bahnschrift", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_CONDENSED, size, L"", &f)))
        return nullptr;
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return f;
}

void Polyline(ID2D1DeviceContext* c, const VuGeom& g, float from, float to, float rad, ID2D1Brush* b, float width,
              ID2D1StrokeStyle* stroke) {
    gfx::ComPtr<ID2D1PathGeometry> path;
    gfx::ComPtr<ID2D1Factory> factory;
    c->GetFactory(&factory);
    if (!factory || FAILED(factory->CreatePathGeometry(&path))) return;
    gfx::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return;
    constexpr int steps = 64;
    sink->BeginFigure(g.At(from, rad), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i <= steps; ++i) sink->AddLine(g.At(from + (to - from) * i / steps, rad));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    c->DrawGeometry(path.Get(), b, width, stroke);
}

}  // namespace

// The printed, backlit scale alone, on transparent: drawn sharp and, blurred, as its glow.
void VizPaint::DrawVuScale(ID2D1DeviceContext5* c, float w, float h, bool big) {
    const VuGeom g = VuGeom::Of(w, h);
    gfx::ComPtr<ID2D1SolidColorBrush> b;
    if (FAILED(c->CreateSolidColorBrush(Rgb(kYellow), &b))) return;
    ID2D1StrokeStyle* round = RoundStroke(c);
    const float line = std::max(1.5f, h * 0.012f);
    // The arc to 0 VU, hooked down at -20; past 0 a thick red band under the line.
    Polyline(c, g, VuPos(-20), VuPos(0), g.R, b.Get(), line, round);
    b->SetColor(Rgb(kRed));
    Polyline(c, g, VuPos(0), 1, g.R, b.Get(), line, round);
    const float band = h * 0.05f;
    Polyline(c, g, VuPos(0), 1, g.R - band / 2, b.Get(), band, nullptr);
    gfx::ComPtr<IDWriteTextFormat> font = GaugeFont(std::max(9.0f, h * (big ? 0.078f : 0.1f)), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    for (const Mark& mk : kMarks) {
        const float pos = VuPos(static_cast<float>(mk.vu));
        const bool red = mk.vu >= 0;
        b->SetColor(Rgb(red ? kRed : kYellow));
        const bool end = mk.vu == -20 || mk.vu == 3;  // the ends turn in, toward the pivot
        if (end) c->DrawLine(g.At(pos, g.R), g.At(pos, g.R - h * 0.07f), b.Get(), line, round);
        else c->DrawLine(g.At(pos, g.R), g.At(pos, g.R + h * 0.065f), b.Get(), line * 1.15f, round);
        if ((!big && mk.minor) || !font) continue;
        // Upright along the radius, like a printed dial.
        const D2D1_POINT_2F at = g.At(pos, g.R + h * (end ? 0.06f : 0.13f));
        const float deg = g.Angle(pos) * 57.29578f;
        c->SetTransform(D2D1::Matrix3x2F::Rotation(deg, at));
        b->SetColor(Rgb(red ? kRedText : kYellowText));
        const float box = h * 0.3f;
        c->DrawText(mk.text, static_cast<UINT32>(wcslen(mk.text)), font.Get(),
                    D2D1::RectF(at.x - box, at.y - box / 2, at.x + box, at.y + box / 2), b.Get());
        c->SetTransform(D2D1::Matrix3x2F::Identity());
    }
}

// The dial behind the needle (face, logo, glowing scale, lamps and print) and the hump with the
// knob in front of it, for a meter of w×h DIPs, made once per size and device.
VizPaint::VuDial* VizPaint::DialFor(ID2D1DeviceContext5* ctx, float w, float h, int ch) {
    const uint32_t gen = gfx::Gpu::Get().Generation();
    float dpiX = 96, dpiY = 96;
    ctx->GetDpi(&dpiX, &dpiY);
    const UINT32 pxW = static_cast<UINT32>(std::lround(w * dpiX / 96)), pxH = static_cast<UINT32>(std::lround(h * dpiY / 96));
    for (VuDial& d : dials_)
        if (d.gen == gen && d.pxW == pxW && d.pxH == pxH && d.ch == ch) return &d;
    if (!pxW || !pxH) return nullptr;
    // An own context: the frame's one is mid-draw on the window, with its clips pushed.
    if (!off_ || offGen_ != gen) {
        off_.Reset();
        offGen_ = gen;
        gfx::ComPtr<ID2D1Device> dev;
        ctx->GetDevice(&dev);
        gfx::ComPtr<ID2D1DeviceContext> c;
        if (!dev || FAILED(dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &c)) || FAILED(c.As(&off_)))
            return nullptr;
    }
    ID2D1DeviceContext5* c = off_.Get();
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX,
        dpiY);
    VuDial d{pxW, pxH, ch, gen};
    gfx::ComPtr<ID2D1Bitmap1> scale;
    if (FAILED(c->CreateBitmap({pxW, pxH}, nullptr, 0, props, &scale)) ||
        FAILED(c->CreateBitmap({pxW, pxH}, nullptr, 0, props, &d.back)) ||
        FAILED(c->CreateBitmap({pxW, pxH}, nullptr, 0, props, &d.front)))
        return nullptr;
    c->SetDpi(dpiX, dpiY);
    c->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    const bool big = w >= 260;
    const VuGeom g = VuGeom::Of(w, h);

    c->SetTarget(scale.Get());
    c->BeginDraw();
    c->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawVuScale(c, w, h, big);
    c->EndDraw();

    c->SetTarget(d.back.Get());
    c->BeginDraw();
    c->Clear(D2D1::ColorF(0, 0, 0, 0));
    {
        Painter pp(c);
        const float radius = std::round(h * 0.06f);
        const Rect face{0, 0, w, h};
        pp.Fill(face, Color::Hex(kFace), radius);
        // The lamp behind the dial: a warm light strongest under the scale, gone at the corners;
        // and the bezel's shadow falling on the top of the face.
        {
            const D2D1_GRADIENT_STOP warm[] = {{0, Rgb(0xFFB45A, 0.16f)}, {0.55f, Rgb(0xFFB45A, 0.05f)}, {1, Rgb(0xFFB45A, 0)}};
            const D2D1_GRADIENT_STOP shade[] = {{0, Rgb(0x000000, 0.45f)}, {1, Rgb(0x000000, 0)}};
            gfx::ComPtr<ID2D1GradientStopCollection> warmStops, shadeStops;
            gfx::ComPtr<ID2D1RadialGradientBrush> lamp;
            gfx::ComPtr<ID2D1LinearGradientBrush> top;
            const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), radius, radius);
            if (SUCCEEDED(c->CreateGradientStopCollection(warm, 3, &warmStops)) &&
                SUCCEEDED(c->CreateRadialGradientBrush(
                    D2D1::RadialGradientBrushProperties({w / 2, h * 0.5f}, {0, 0}, w * 0.55f, h * 0.6f), warmStops.Get(), &lamp)))
                c->FillRoundedRectangle(rr, lamp.Get());
            if (SUCCEEDED(c->CreateGradientStopCollection(shade, 2, &shadeStops)) &&
                SUCCEEDED(c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, h * 0.1f}),
                                                       shadeStops.Get(), &top)))
                c->FillRoundedRectangle(rr, top.Get());
        }
        pp.Stroke(face, Color::Hex(0x000000, 0.35f), radius, std::max(1.0f, h * 0.006f));
        const float logo = h * 0.32f;
        pp.Icon("moonbow", {g.pivot.x - logo / 2, h * 0.47f, logo, logo}, Color::Hex(0x56575B), Color::Hex(0x56575B));
        if (gfx::ComPtr<IDWriteTextFormat> vu = GaugeFont(h * 0.085f, DWRITE_FONT_WEIGHT_REGULAR)) {
            gfx::ComPtr<ID2D1SolidColorBrush> ink;
            c->CreateSolidColorBrush(Rgb(kInk), &ink);
            const float s = h * 0.09f;
            c->DrawText(L"VU", 2, vu.Get(), D2D1::RectF(w * 0.04f, s * 0.6f, w * 0.04f + s * 2.2f, s * 1.9f), ink.Get());
            const wchar_t* side = ch == 0 ? L"L" : L"R";
            c->DrawText(side, 1, vu.Get(), D2D1::RectF(w * 0.96f - s * 2.2f, s * 0.6f, w * 0.96f, s * 1.9f), ink.Get());
            // Two lamps low on the face with the maker's print under them, on meters big enough to read it.
            const float lamp = h * 0.034f;
            for (int i = 0; i < 2; ++i) {
                const float x = i == 0 ? w * 0.14f : w * 0.86f, y = h * 0.7f;
                pp.FillCircle({x, y}, lamp, Color::Hex(0x9A9B9F));
                pp.FillCircle({x, y - lamp * 0.25f}, lamp * 0.6f, Color::Hex(0xB9BABD));
                pp.StrokeCircle({x, y}, lamp, Color::Hex(0x000000, 0.4f));
                if (!big) continue;
                gfx::ComPtr<IDWriteTextFormat> print = GaugeFont(h * 0.036f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                if (!print) continue;
                const wchar_t* l1 = L"MOONBOW MODEL VU-1";
                const wchar_t* l2 = i == 0 ? L"LEFT CHANNEL" : L"RIGHT CHANNEL";
                const float tw = w * 0.3f;
                c->DrawText(l1, static_cast<UINT32>(wcslen(l1)), print.Get(),
                            D2D1::RectF(x - tw / 2, y + lamp * 1.6f, x + tw / 2, y + lamp * 1.6f + h * 0.05f), ink.Get());
                c->DrawText(l2, static_cast<UINT32>(wcslen(l2)), print.Get(),
                            D2D1::RectF(x - tw / 2, y + lamp * 1.6f + h * 0.05f, x + tw / 2, y + lamp * 1.6f + h * 0.1f),
                            ink.Get());
            }
        }
    }
    // The glow: the scale blurred wide and tight, dimmed, and added to the face as light; then the
    // scale itself, sharp on top.
    gfx::ComPtr<ID2D1Effect> blur, dim;
    if (SUCCEEDED(c->CreateEffect(kVuBlur, &blur)) && SUCCEEDED(c->CreateEffect(kVuColorMatrix, &dim))) {
        blur->SetInput(0, scale.Get());
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
        dim->SetInputEffect(0, blur.Get());
        auto glow = [&](float spread, float k) {
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, h * spread);
            const D2D1_MATRIX_5X4_F m{k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, 0, 0, 0, k, 0, 0, 0, 0};
            dim->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
            c->DrawImage(dim.Get(), D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
        };
        glow(0.04f, 0.6f);
        glow(0.012f, 0.4f);
    }
    c->DrawBitmap(scale.Get());
    c->EndDraw();

    c->SetTarget(d.front.Get());
    c->BeginDraw();
    c->Clear(D2D1::ColorF(0, 0, 0, 0));
    {
        // The hump: a bell rising from the bottom edge over the pivot.
        gfx::ComPtr<ID2D1Factory> factory;
        c->GetFactory(&factory);
        gfx::ComPtr<ID2D1PathGeometry> hump;
        gfx::ComPtr<ID2D1GeometrySink> sink;
        const float cx = g.pivot.x, top = h * 0.77f, half = w * 0.25f;
        if (factory && SUCCEEDED(factory->CreatePathGeometry(&hump)) && SUCCEEDED(hump->Open(&sink))) {
            sink->BeginFigure({cx - half, h + 1}, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddBezier(D2D1::BezierSegment({cx - half * 0.45f, h + 1}, {cx - half * 0.55f, top}, {cx, top}));
            sink->AddBezier(D2D1::BezierSegment({cx + half * 0.55f, top}, {cx + half * 0.45f, h + 1}, {cx + half, h + 1}));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            gfx::ComPtr<ID2D1SolidColorBrush> b;
            c->CreateSolidColorBrush(Rgb(kHump), &b);
            // Only inside the face's rounded corners.
            c->PushAxisAlignedClip(D2D1::RectF(0, 0, w, h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            c->FillGeometry(hump.Get(), b.Get());
            b->SetColor(Rgb(0xFFFFFF, 0.07f));
            c->DrawGeometry(hump.Get(), b.Get(), 1.0f);
            c->PopAxisAlignedClip();
        }
        Painter pp(c);
        const float knob = h * 0.06f;
        const Point k{cx, h * 0.89f};
        pp.FillCircle(k, knob, Color::Hex(0x8E8F93));
        pp.FillCircle({k.x, k.y - knob * 0.12f}, knob * 0.86f, Color::Hex(0xC4C5C8));
        pp.FillCircle({k.x - knob * 0.2f, k.y - knob * 0.3f}, knob * 0.35f, Color::Hex(0xFFFFFF, 0.35f));
        // The glass over it all: a soft diagonal sheen from the top left and a bright top edge.
        const float radius = std::round(h * 0.06f);
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), radius, radius);
        const D2D1_GRADIENT_STOP sheen[] = {{0, Rgb(0xFFFFFF, 0.10f)}, {0.42f, Rgb(0xFFFFFF, 0.03f)},
                                            {0.43f, Rgb(0xFFFFFF, 0)}, {1, Rgb(0xFFFFFF, 0)}};
        gfx::ComPtr<ID2D1GradientStopCollection> stops;
        gfx::ComPtr<ID2D1LinearGradientBrush> glass;
        if (SUCCEEDED(c->CreateGradientStopCollection(sheen, 4, &stops)) &&
            SUCCEEDED(c->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {w * 0.55f, h}),
                                                   stops.Get(), &glass)))
            c->FillRoundedRectangle(rr, glass.Get());
        pp.Fill({radius, 1, w - 2 * radius, 1}, Color::Hex(0xFFFFFF, 0.14f));
    }
    c->EndDraw();
    c->SetTarget(nullptr);

    if (dials_.size() >= 4) dials_.erase(dials_.begin());
    dials_.push_back(std::move(d));
    return &dials_.back();
}

void VizPaint::VuMeter(Painter& p, const Rect& m, int ch, bool rest) {
    ID2D1DeviceContext5* ctx = p.Ctx();
    VuDial* d = DialFor(ctx, m.w, m.h, ch);
    if (!d) return;
    const float o = p.Opacity();
    const D2D1_RECT_F dest = D2D1::RectF(m.x, m.y, m.Right(), m.Bottom());
    ctx->DrawBitmap(d->back.Get(), dest, o, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    // The needle: a thin red blade with a soft shadow on the face and a little glow of its own.
    const VuGeom g = VuGeom::Of(m.w, m.h);
    const float pos = rest ? 0.0f : needles_[ch].pos;
    auto at = [&](float rad, float dx = 0, float dy = 0) {
        const D2D1_POINT_2F pt = g.At(pos, rad);
        return D2D1_POINT_2F{m.x + pt.x + dx, m.y + pt.y + dy};
    };
    const float h = m.h, inner = h * 0.08f, outer = g.R + h * 0.09f;
    ID2D1StrokeStyle* round = RoundStroke(ctx);
    p.PushRoundClip(m, std::round(h * 0.06f));
    if (ID2D1SolidColorBrush* b = LineBrush(ctx, Color::Hex(0x000000, 0.4f), o))
        ctx->DrawLine(at(inner, h * 0.012f, h * 0.018f), at(outer, h * 0.012f, h * 0.018f), b, h * 0.014f, round);
    if (ID2D1SolidColorBrush* b = LineBrush(ctx, Color::Hex(0xFF2A2A, 0.22f), o))
        ctx->DrawLine(at(inner), at(outer), b, h * 0.038f, round);
    if (ID2D1SolidColorBrush* b = LineBrush(ctx, Color::Hex(0xFF2626), o))
        ctx->DrawLine(at(inner), at(outer), b, std::max(1.5f, h * 0.011f), round);
    p.PopClip();
    ctx->DrawBitmap(d->front.Get(), dest, o, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

}  // namespace app

#include "ui/Painter.h"

#include "gfx/Icons.h"

namespace ui {

Painter::Painter(ID2D1DeviceContext5* ctx) : ctx_(ctx) {
    ctx_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_);
    D2D1_SIZE_F size = ctx_->GetSize();
    clips_.push_back({Rect{0, 0, size.width, size.height}, false});
}

Painter::~Painter() {
    while (clips_.size() > 1) PopClip();
}

ID2D1SolidColorBrush* Painter::Brush(Color c) {
    c.a *= opacity_;
    brush_->SetColor(c.D2D());
    return brush_.Get();
}

void Painter::Fill(const Rect& r, Color c, float radius) {
    FillRaw(r, c, Theme::Get().Radius(radius));
}

void Painter::FillRaw(const Rect& r, Color c, float radius) {
    if (r.Empty() || c.a <= 0) return;
    if (radius > 0)
        ctx_->FillRoundedRectangle(D2D1::RoundedRect(r.D2D(), radius, radius), Brush(c));
    else
        ctx_->FillRectangle(r.D2D(), Brush(c));
}

void Painter::Stroke(const Rect& r, Color c, float radius, float width) {
    if (r.Empty() || c.a <= 0) return;
    radius = Theme::Get().Radius(radius);
    const Rect in = r.Inset(width / 2);
    if (radius > 0) {
        const float rr = std::max(0.0f, radius - width / 2);
        ctx_->DrawRoundedRectangle(D2D1::RoundedRect(in.D2D(), rr, rr), Brush(c), width);
    } else {
        ctx_->DrawRectangle(in.D2D(), Brush(c), width);
    }
}

void Painter::FillCircle(Point center, float radius, Color c) {
    ctx_->FillEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), Brush(c));
}

void Painter::FillGeometry(ID2D1Geometry* g, Color c) {
    ctx_->FillGeometry(g, Brush(c));
}

void Painter::Text(std::wstring_view text, const Rect& r, TextStyle style, Color c, HAlign h, VAlign v,
                   uint8_t flags) {
    if (textSink_ && !text.empty())
        textSink_->push_back({textOwner_, std::wstring(text), r, Clip(), style, h, v, flags, c.a * opacity_ < 0.05f});
    if (text.empty() || r.w <= 0 || c.a <= 0) return;
    IDWriteTextLayout* layout = gfx::Text::Get().Layout(text, style, r.w, r.h, h, v, flags);
    if (!layout) return;
    ctx_->DrawTextLayout({r.x, r.y}, layout, Brush(c), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

gfx::TextMetrics Painter::Measure(std::wstring_view text, TextStyle style, float maxWidth, uint8_t flags) const {
    return gfx::Text::Get().Measure(text, style, maxWidth, flags);
}

void Painter::Icon(std::string_view name, const Rect& box, Color c) {
    Icon(name, box, c, P().accentMid);
}

void Painter::Icon(std::string_view name, const Rect& box, Color c, Color secondary) {
    c.a *= opacity_;
    gfx::Icons::Draw(ctx_, name, box, c, secondary);
}

void Painter::Image(ID2D1Bitmap* bitmap, const Rect& r, float radius, bool cover, float opacity) {
    if (!bitmap || r.Empty()) return;
    D2D1_SIZE_F bs = bitmap->GetSize();
    if (bs.width <= 0 || bs.height <= 0) return;
    const float sx = r.w / bs.width, sy = r.h / bs.height;
    const float s = cover ? std::max(sx, sy) : std::min(sx, sy);
    const float w = bs.width * s, h = bs.height * s;
    const float x = r.x + (r.w - w) / 2, y = r.y + (r.h - h) / 2;
    opacity *= opacity_;
    radius = Theme::Get().Radius(radius);

    if (radius <= 0 && !cover) {
        ctx_->DrawBitmap(bitmap, D2D1::RectF(x, y, x + w, y + h), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        return;
    }
    // A bitmap brush clipped by the (rounded) destination handles both cover
    // cropping and rounded corners in one fill.
    gfx::ComPtr<ID2D1BitmapBrush> brush;
    if (FAILED(ctx_->CreateBitmapBrush(bitmap, &brush))) return;
    brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    brush->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(x, y));
    brush->SetOpacity(opacity);
    const Rect dst = cover ? r : Rect{x, y, w, h};
    if (radius > 0)
        ctx_->FillRoundedRectangle(D2D1::RoundedRect(dst.D2D(), radius, radius), brush.Get());
    else
        ctx_->FillRectangle(dst.D2D(), brush.Get());
}

void Painter::Shadow(const Rect& r, float radius, float blur, float dy, float alpha) {
    if (alpha < 0) alpha = P().shadow.a;
    radius = Theme::Get().Radius(radius);  // once: radius + grow is not a design radius
    constexpr int kSteps = 6;
    for (int i = kSteps; i >= 1; --i) {
        const float t = static_cast<float>(i) / kSteps;
        const float grow = blur * t * 0.6f;
        Rect s{r.x - grow, r.y - grow + dy * t, r.w + grow * 2, r.h + grow * 2};
        FillRaw(s, Color{0, 0, 0, alpha * 0.9f / kSteps * (1.2f - t)}, radius + grow);
    }
}

void Painter::PushClip(const Rect& r) {
    Rect c = r.Intersect(clips_.back().rect);
    ctx_->PushAxisAlignedClip(c.D2D(), D2D1_ANTIALIAS_MODE_ALIASED);
    clips_.push_back({c, false});
}

void Painter::PushRoundClip(const Rect& r, float radius) {
    radius = Theme::Get().Radius(radius);
    gfx::ComPtr<ID2D1RoundedRectangleGeometry> g;
    gfx::Gpu::Get().D2DFactory()->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r.D2D(), radius, radius), &g);
    D2D1_LAYER_PARAMETERS1 params = D2D1::LayerParameters1(r.D2D(), g.Get());
    ctx_->PushLayer(params, nullptr);
    clips_.push_back({r.Intersect(clips_.back().rect), true});
}

void Painter::PopClip() {
    if (clips_.size() <= 1) return;
    if (clips_.back().layer)
        ctx_->PopLayer();
    else
        ctx_->PopAxisAlignedClip();
    clips_.pop_back();
}

void Painter::PushOpacity(float k) {
    opacities_.push_back(opacity_);
    opacity_ *= k;
}

void Painter::PopOpacity() {
    if (opacities_.empty()) return;
    opacity_ = opacities_.back();
    opacities_.pop_back();
}

}  // namespace ui

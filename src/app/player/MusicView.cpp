// Music view shown instead of the video for audio files (design/ref/Player-Music_*.png):
// a large cover over a blurred, darkened copy of itself, and "title - artist" below.
// Files without artwork get generated monochrome art (concentric grooves).
#include "Util.h"
#include "app/App.h"
#include "app/player/PlayerView.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"

#include <d2d1_1.h>
#include <d2d1effects.h>

namespace app {

using namespace ui;

namespace {

// CLSID_D2D1GaussianBlur (defined here so we don't need dxguid.lib).
constexpr GUID kGaussianBlur = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

uint32_t Hash(const std::wstring& s) {
    uint32_t h = 2166136261u;
    for (wchar_t c : s) h = (h ^ static_cast<uint32_t>(c)) * 16777619u;
    return h;
}

class MusicView : public Widget {
public:
    MusicView() {
        conn_ = Connect(TheApp().playerChanged, [this](unsigned m) {
            if (m & (kChangeMeta | kChangeFile | kChangeState)) Invalidate();
        });
        imagesConn_ = Connect(TheApp().images().ready, [this] { Invalidate(); });
    }

    void Paint(Painter& p) override {
        const Palette& c = P();
        p.Fill(rect, c.videoBg);
        const PlayerState& s = TheApp().player().State();
        const QueueItem* item = TheApp().queue().CurrentItem();
        const std::wstring path = item ? item->path : s.path;

        // Layout: cover centered, text 88 DIP under it (1920×1080: 738 cover, 73 top margin).
        const float side = std::floor(std::max(96.0f, std::min({rect.w - 96, rect.h - 180, 738.0f})));
        const float block = side + 88 + 30;
        const float top = rect.y + std::max(24.0f, (rect.h - block) / 2);
        const Rect cover{std::round(rect.Cx() - side / 2), std::round(top), side, side};

        ID2D1Bitmap* bmp = nullptr;
        if (!path.empty() && GetHost()) {
            const int px = static_cast<int>(side * GetHost()->Scale());
            bmp = TheApp().images().Get(path, px);
        }

        p.PushClip(rect);
        if (bmp) PaintBackdrop(p, bmp, cover);
        else PaintGlow(p, cover);
        p.PopClip();

        p.Shadow(cover, 16, 40, 12, 0.55f);
        if (bmp) p.Image(bmp, cover, 16, true);
        else PaintGeneratedArt(p, cover, Hash(path));

        std::wstring title = s.title.empty() ? (item ? FileNameOf(item->path) : L"") : s.title;
        if (!s.artist.empty()) title += L" · " + s.artist;
        // The view is always on the (dark) video background, also in the light theme, so the
        // text is light rather than the palette's text colors.
        const Color white = Color::Hex(0xFFFFFF);
        p.Text(title, {rect.x + 48, cover.Bottom() + 70, rect.w - 96, 36}, TextStyle::Title,
               Color::Mix(c.videoBg, white, 0.8f), HAlign::Center);
        if (!s.album.empty())
            p.Text(s.album, {rect.x + 48, cover.Bottom() + 106, rect.w - 96, 22}, TextStyle::Body,
                   Color::Mix(c.videoBg, white, 0.5f), HAlign::Center);
    }

private:
    // The cover, blurred and scaled to fill the view, then darkened toward the edges.
    void PaintBackdrop(Painter& p, ID2D1Bitmap* bmp, const Rect& cover) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (bmp != blurFor_ || gfx::Gpu::Get().Generation() != generation_) {
            blur_.Reset();
            blurFor_ = bmp;
            generation_ = gfx::Gpu::Get().Generation();
            if (SUCCEEDED(ctx->CreateEffect(kGaussianBlur, &blur_))) {
                blur_->SetInput(0, bmp);
                blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 18.0f);
                blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
                blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION, D2D1_GAUSSIANBLUR_OPTIMIZATION_SPEED);
            }
        }
        const D2D1_SIZE_F bs = bmp->GetSize();
        if (blur_ && bs.width > 0 && bs.height > 0) {
            // Cover the view with the image, centered on the cover (slightly larger, like the design).
            const float k = std::max(rect.w / bs.width, rect.h / bs.height) * 1.15f;
            const float w = bs.width * k, h = bs.height * k;
            D2D1_MATRIX_3X2_F old;
            ctx->GetTransform(&old);
            ctx->SetTransform(D2D1::Matrix3x2F::Scale(k, k) *
                              D2D1::Matrix3x2F::Translation(cover.Cx() - w / 2, cover.Cy() - h / 2) * old);
            gfx::ComPtr<ID2D1Image> out;
            blur_->GetOutput(&out);
            ctx->DrawImage(out.Get(), D2D1_INTERPOLATION_MODE_LINEAR);
            ctx->SetTransform(old);
        }
        const Palette& c = P();
        p.Fill(rect, c.videoBg.WithAlpha(0.62f));
        Vignette(p, cover, 0.0f, 0.85f);
    }

    // No artwork: a soft light halo behind the generated art.
    void PaintGlow(Painter& p, const Rect& cover) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        const Palette& c = P();
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, c.textPrimary.WithAlpha(0.07f).D2D()},
                                            {0.55f, c.textPrimary.WithAlpha(0.025f).D2D()},
                                            {1.0f, c.textPrimary.WithAlpha(0.0f).D2D()}};
        gfx::ComPtr<ID2D1GradientStopCollection> coll;
        gfx::ComPtr<ID2D1RadialGradientBrush> brush;
        if (FAILED(ctx->CreateGradientStopCollection(stops, 3, &coll))) return;
        const float r = cover.w * 0.95f;
        if (FAILED(ctx->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties({cover.Cx(), cover.Cy()}, {0, 0}, r, r), coll.Get(), &brush)))
            return;
        ctx->FillRectangle(rect.D2D(), brush.Get());
    }

    void Vignette(Painter& p, const Rect& cover, float inner, float outer) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        const Color bg = P().videoBg;
        const D2D1_GRADIENT_STOP stops[] = {{0.0f, bg.WithAlpha(inner).D2D()}, {1.0f, bg.WithAlpha(outer).D2D()}};
        gfx::ComPtr<ID2D1GradientStopCollection> coll;
        gfx::ComPtr<ID2D1RadialGradientBrush> brush;
        if (FAILED(ctx->CreateGradientStopCollection(stops, 2, &coll))) return;
        const float rx = std::max(rect.w, rect.h) * 0.75f;
        if (FAILED(ctx->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties({cover.Cx(), cover.Cy()}, {0, 0}, rx, rx), coll.Get(), &brush)))
            return;
        brush->SetOpacity(p.Opacity());
        ctx->FillRectangle(rect.D2D(), brush.Get());
    }

    // Monochrome stand-in art: dark square, record-like grooves and a center label.
    void PaintGeneratedArt(Painter& p, const Rect& r, uint32_t seed) {
        const Palette& c = P();
        p.Fill(r, c.bgCard, 16);
        p.PushRoundClip(r, 16);
        const Point center{r.Cx(), r.Cy()};
        const float maxR = r.w * 0.42f;
        ID2D1DeviceContext5* ctx = p.Ctx();
        gfx::ComPtr<ID2D1SolidColorBrush> brush;
        ctx->CreateSolidColorBrush(c.textPrimary.WithAlpha(0.05f).D2D(), &brush);
        if (brush) {
            int k = 0;
            for (float rad = maxR * 0.38f; rad < maxR; rad += r.w * 0.012f, ++k) {
                const float a = 0.025f + 0.035f * static_cast<float>((seed >> (k % 24)) & 1);
                brush->SetColor(c.textPrimary.WithAlpha(a * p.Opacity()).D2D());
                ctx->DrawEllipse(D2D1::Ellipse({center.x, center.y}, rad, rad), brush.Get(), 1.0f);
            }
            brush->SetColor(c.textPrimary.WithAlpha(0.14f * p.Opacity()).D2D());
            ctx->DrawEllipse(D2D1::Ellipse({center.x, center.y}, maxR, maxR), brush.Get(), 1.5f);
        }
        p.FillCircle(center, maxR * 0.30f, c.bgSelected);
        const float icon = maxR * 0.22f;
        p.Icon("music", {center.x - icon / 2, center.y - icon / 2, icon, icon}, c.textTertiary);
        p.PopClip();
        p.Stroke(r, c.line, 16);
    }

    ScopedConnection conn_, imagesConn_;
    gfx::ComPtr<ID2D1Effect> blur_;
    ID2D1Bitmap* blurFor_ = nullptr;
    uint32_t generation_ = 0;
};

}  // namespace

std::unique_ptr<Widget> CreateMusicView() {
    return std::make_unique<MusicView>();
}

}  // namespace app

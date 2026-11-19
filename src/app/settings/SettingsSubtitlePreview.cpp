// Settings > Subtitles: the subtitle style on a real frame (a night lake under a moonbow, dark water
// on the left and moonlight on the right, so readability shows on both), with the ready-made styles
// as a row of names under it: hovering one shows it, a click picks it (design/explorations/
// 2026-11-19-subtitles.html, layout B). An approximation of libass in Direct2D: the outline is the
// text stamped around a circle, the shadow an offset copy, the blur a Gaussian blur effect. The
// text is recorded into a command list once per style, so repaints only replay it.
#include "app/I18n.h"
#include "app/player/SubStyle.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"

#include <d2d1effects.h>
#include <wincodec.h>

#include <cmath>
#include <functional>

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

// CLSID_D2D1GaussianBlur (defined here so we don't need dxguid.lib).
constexpr GUID kGaussianBlur = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

// Everything that changes the picture.
std::wstring StyleKey(const SubtitleStyle& s, float position) {
    wchar_t buf[200];
    swprintf_s(buf, L"|%g|%d|%d|%08X%02X|%08X|%08X%02X|%g|%d|%hs|%g|%g", s.size, s.bold ? 1 : 0, s.weight, s.text.Rgb(),
               static_cast<unsigned>(s.text.a * 255), s.outlineColor.Rgb(), s.back.Rgb(),
               static_cast<unsigned>(s.back.a * 255), s.outline, s.shadow ? 1 : 0, s.background.c_str(), s.blur,
               position);
    return s.font + buf;
}

// res/images/subtitle-frame.jpg, decoded once to premultiplied BGRA.
struct FramePixels {
    UINT w = 0, h = 0;
    std::vector<uint8_t> data;
};

const FramePixels& Frame() {
    static const FramePixels px = [] {
        FramePixels out;
        HRSRC res = FindResourceW(nullptr, L"MOONBOW_SUB_FRAME", RT_RCDATA);
        HGLOBAL mem = res ? LoadResource(nullptr, res) : nullptr;
        auto* bytes = mem ? static_cast<BYTE*>(LockResource(mem)) : nullptr;
        if (!bytes) return out;
        gfx::ComPtr<IWICImagingFactory> wic;
        gfx::ComPtr<IWICStream> stream;
        gfx::ComPtr<IWICBitmapDecoder> decoder;
        gfx::ComPtr<IWICBitmapFrameDecode> frame;
        gfx::ComPtr<IWICFormatConverter> converter;
        UINT w = 0, h = 0;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
            FAILED(wic->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(bytes, SizeofResource(nullptr, res))) ||
            FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
            FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) ||
            FAILED(wic->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)))
            return out;
        out.data.resize(static_cast<size_t>(w) * h * 4);
        if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.data.size()), out.data.data()))) {
            out.data.clear();
            return out;
        }
        out.w = w;
        out.h = h;
        return out;
    }();
    return px;
}

// A line (or two) of subtitles drawn with a style over the frame, sized as mpv sizes them: the
// style's size is pixels of a 720-line picture.
class SubtitleSample {
public:
    explicit SubtitleSample(std::wstring text) : text_(std::move(text)) {}

    // True when the picture changed.
    bool SetStyle(const SubtitleStyle& s, float position) {
        std::wstring key = StyleKey(s, position);
        if (key == key_) return false;
        key_ = std::move(key);
        style_ = s;
        position_ = position;
        scene_.Reset();  // recorded again on the next paint
        layout_.Reset();
        return true;
    }

    void Paint(Painter& p, const Rect& f, float radius) {
        const float scale = std::round(f.h / 720.0f * 1000) / 1000;
        if (scale != scale_) {
            scale_ = scale;
            scene_.Reset();
            layout_.Reset();
        }
        p.PushRoundClip(f, radius);
        PaintScene(p, f);
        PaintText(p, f);
        p.PopClip();
    }

private:
    void PaintScene(Painter& p, const Rect& f) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        const FramePixels& px = Frame();
        const uint32_t gen = gfx::Gpu::Get().Generation();
        if (ctx && px.w && (!frame_ || frameGen_ != gen)) {
            frame_.Reset();
            frameGen_ = gen;
            const D2D1_BITMAP_PROPERTIES props =
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            ctx->CreateBitmap(D2D1::SizeU(px.w, px.h), px.data.data(), px.w * 4, &props, &frame_);
        }
        if (frame_) p.Image(frame_.Get(), f, 0, true);
        else p.Fill(f, Color::Hex(0x121A26));  // the frame's own night blue
    }

    bool Prepare(ID2D1DeviceContext5* ctx) {
        const uint32_t gen = gfx::Gpu::Get().Generation();
        if (gen != generation_) {
            generation_ = gen;
            scene_.Reset();
            blur_.Reset();
        }
        if (!layout_) {
            IDWriteFactory5* dw = gfx::Gpu::Get().DWrite();
            if (!dw || scale_ <= 0) return false;
            gfx::ComPtr<IDWriteTextFormat> format;
            if (FAILED(dw->CreateTextFormat(style_.font.c_str(), gfx::Text::Get().CollectionFor(style_.font),
                                            static_cast<DWRITE_FONT_WEIGHT>(style_.weight), DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, style_.size * scale_, L"", &format)))
                return false;
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            if (FAILED(dw->CreateTextLayout(text_.c_str(), static_cast<UINT32>(text_.size()), format.Get(), 4000, 400,
                                            &layout_)))
                return false;
            // Centered lines need the layout as wide as the longest one.
            DWRITE_TEXT_METRICS m{};
            layout_->GetMetrics(&m);
            layout_->SetMaxWidth(std::ceil(m.widthIncludingTrailingWhitespace));
            layout_->GetMetrics(&m);
            textW_ = std::ceil(m.widthIncludingTrailingWhitespace);
            textH_ = m.height;
        }
        if (!scene_ && !Record(ctx)) return false;
        if (style_.blur > 0 && !blur_ && FAILED(ctx->CreateEffect(kGaussianBlur, &blur_))) blur_.Reset();
        return true;
    }

    // Rectangles of the lines (relative to the layout), for the boxes behind each line.
    std::vector<Rect> LineRects() const {
        std::vector<Rect> out;
        UINT32 n = 0;
        layout_->GetLineMetrics(nullptr, 0, &n);
        std::vector<DWRITE_LINE_METRICS> lines(n);
        if (n == 0 || FAILED(layout_->GetLineMetrics(lines.data(), n, &n))) return out;
        UINT32 start = 0;
        float y = 0;
        for (const DWRITE_LINE_METRICS& l : lines) {
            DWRITE_HIT_TEST_METRICS hit{};
            UINT32 count = 0;
            const UINT32 len = l.length - l.newlineLength;
            if (len > 0 && SUCCEEDED(layout_->HitTestTextRange(start, len, 0, 0, &hit, 1, &count)) && count > 0)
                out.push_back({hit.left, y, hit.width, l.height});
            start += l.length;
            y += l.height;
        }
        return out;
    }

    // Shadow and outline at the origin, on a separate context of the same device (the window's
    // context is mid-frame). The letters themselves are drawn on top, after any blur: libass
    // blurs only the outline when there is one.
    bool Record(ID2D1DeviceContext5* ctx) {
        gfx::ComPtr<ID2D1Device> device;
        ctx->GetDevice(&device);
        gfx::ComPtr<ID2D1DeviceContext> rec;
        if (!device || FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rec))) return false;
        gfx::ComPtr<ID2D1CommandList> list;
        if (FAILED(rec->CreateCommandList(&list))) return false;
        float dpiX = 96, dpiY = 96;
        ctx->GetDpi(&dpiX, &dpiY);
        rec->SetDpi(dpiX, dpiY);
        rec->SetTarget(list.Get());
        rec->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        rec->BeginDraw();

        const bool lineBoxes = style_.background == "lines";  // outline and shadow become the box
        const float r = lineBoxes ? 0.0f : style_.outline * scale_;
        gfx::ComPtr<ID2D1SolidColorBrush> brush;
        rec->CreateSolidColorBrush(style_.text.D2D(), &brush);
        auto stamp = [&](float ox, float oy, Color c) {
            brush->SetColor(c.D2D());
            if (r > 0) {
                // Two rings fill thick outlines without gaps between the copies.
                const int n = r > 2 ? 24 : 16;
                for (float rr : {r, r * 0.55f})
                    for (int i = 0; i < n; ++i) {
                        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
                        rec->DrawTextLayout({ox + rr * std::cos(a), oy + rr * std::sin(a)}, layout_.Get(), brush.Get());
                    }
            }
            rec->DrawTextLayout({ox, oy}, layout_.Get(), brush.Get());
        };
        // mpv draws the shadow in the box color (sub-back-color), 2 px of a 720-line picture away.
        if (style_.shadow && !lineBoxes && style_.background != "box") {
            const float s = std::max(1.0f, 2 * scale_);
            stamp(s, s, style_.back);
        }
        if (r > 0) stamp(0, 0, style_.outlineColor);
        if (r <= 0 || style_.blur <= 0) {  // otherwise drawn sharp on top of the blurred outline
            brush->SetColor(style_.text.D2D());
            rec->DrawTextLayout({0, 0}, layout_.Get(), brush.Get());
        }

        const HRESULT hr = rec->EndDraw();
        rec->SetTarget(nullptr);
        if (FAILED(hr) || FAILED(list->Close())) return false;
        scene_ = list;
        return true;
    }

    void PaintText(Painter& p, const Rect& frame) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (!ctx || !Prepare(ctx)) return;
        const float x = std::round(frame.Cx() - textW_ / 2);
        // 100% at the bottom edge, as in mpv, above its 22-pixel margin of a 720-line picture.
        const float margin = 22 * scale_;
        const float travel = std::max(0.0f, frame.h - 2 * margin - textH_);
        const float y = std::round(frame.y + margin + travel * std::clamp(position_, 0.0f, 100.0f) / 100);

        const Rect textRect{x, y, textW_, textH_};
        if (style_.background == "lines") {
            const float boxPad = std::max(2.0f, style_.outline * scale_);
            for (const Rect& line : LineRects()) {
                const Rect box = line.Offset(x, y).Inset(-boxPad);
                if (style_.shadow) {
                    const float s = std::max(1.0f, 2 * scale_);
                    p.Fill({box.x + s, box.y + s, box.w, box.h}, style_.back);
                }
                p.Fill(box, style_.outlineColor);
            }
        } else if (style_.background == "box") {
            const float o = std::max(2.0f, style_.outline * scale_);
            p.Fill(textRect.Inset(Insets::Sym(-o * 2, -o)), style_.back);
        }

        if (style_.blur > 0 && blur_) {
            blur_->SetInput(0, scene_.Get());
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, style_.blur * scale_);
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
            gfx::ComPtr<ID2D1Image> out;
            blur_->GetOutput(&out);
            ctx->DrawImage(out.Get(), D2D1::Point2F(x, y));
        } else {
            ctx->DrawImage(scene_.Get(), D2D1::Point2F(x, y));
        }
        if (style_.outline > 0 && style_.blur > 0 && style_.background != "lines") {
            gfx::ComPtr<ID2D1SolidColorBrush> brush;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(style_.text.D2D(), &brush)))
                ctx->DrawTextLayout(D2D1::Point2F(x, y), layout_.Get(), brush.Get());
        }
    }

    std::wstring text_;
    float scale_ = 0;
    SubtitleStyle style_;
    float position_ = 100;
    std::wstring key_;
    gfx::ComPtr<IDWriteTextLayout> layout_;
    gfx::ComPtr<ID2D1CommandList> scene_;
    gfx::ComPtr<ID2D1Effect> blur_;
    gfx::ComPtr<ID2D1Bitmap> frame_;
    uint32_t generation_ = 0, frameGen_ = 0;
    float textW_ = 0, textH_ = 0;
};

// The example and the styles under it: hovering a name shows that style in the example, a click
// (or Enter) picks it. One widget, so the keyboard walks the names with the arrows.
class SubtitleStylePicker : public Widget, public Syncable {
public:
    explicit SubtitleStylePicker(const Item& it) : sample_(Tr(L"— Did you see the sky glow?\n— It's a moonbow.")) {
        width = Length::Grow();
        id = it.key;
        interactive = true;
        focusable = true;
        Sync();
    }

    void Sync() override {
        const SubtitlePreset* now = MatchSubtitlePreset(Cfg());
        current_ = now ? static_cast<int>(now - SubtitlePresets().data()) : -1;
        Show(shown_ >= 0 ? shown_ : current_);
        Invalidate();
    }

    Size MeasureContent(float availW, float) override {
        Place(availW, 0);
        return {availW, height_};
    }
    void OnLayout() override { Place(rect.w, rect.y, rect.x); }

    void Paint(Painter& p) override {
        const Palette& c = P();
        sample_.Paint(p, preview_, 10);
        p.Stroke(preview_, c.line, 10);
        const auto& presets = SubtitlePresets();
        for (size_t i = 0; i < chips_.size(); ++i) {
            const Rect& r = chips_[i];
            const bool on = static_cast<int>(i) == current_;
            const bool hot = static_cast<int>(i) == hover_ || (IsFocused() && static_cast<int>(i) == focus_);
            p.Fill(r, on ? c.textPrimary : hot ? c.bgSelected : c.bgCard, r.h / 2);
            p.Text(Tr(presets[i].name), r, TextStyle::Body, on ? c.bgBase : hot ? c.textPrimary : c.textSecondary,
                   HAlign::Center, VAlign::Center, 0);
            if (IsFocused() && static_cast<int>(i) == focus_) PaintFocusRing(p, r, r.h / 2);
        }
        const int noteFor = hover_ >= 0 ? hover_ : current_;
        const std::wstring note = noteFor >= 0 ? std::wstring(Tr(presets[static_cast<size_t>(noteFor)].detail))
                                               : std::wstring(Tr(L"Your own style: changed in the settings below."));
        p.Text(note, note_, TextStyle::Body, c.textTertiary, HAlign::Leading, VAlign::Center, gfx::kTextEllipsis);
    }

    bool OnMouseMove(const MouseEvent& e) override {
        SetHover(ChipAt(e.x, e.y));
        return true;
    }
    void OnMouseLeave() override { SetHover(-1); }
    void OnClick() override {
        const Point m = GetHost()->MousePos();
        if (const int i = ChipAt(m.x, m.y); i >= 0) Pick(i);
    }
    bool OnKeyDown(const KeyEvent& e) override {
        const int n = static_cast<int>(chips_.size());
        if (focus_ < 0) focus_ = std::max(0, current_);
        switch (e.key) {
            case VK_LEFT: case VK_RIGHT:
                focus_ = (focus_ + (e.key == VK_LEFT ? n - 1 : 1)) % n;
                Show(focus_);
                Invalidate();
                return true;
            case VK_RETURN: case VK_SPACE: Pick(focus_); return true;
            default: return false;
        }
    }
    void OnFocusChanged(bool focused) override {
        focus_ = focused ? std::max(0, current_) : -1;
        Show(focused ? focus_ : current_);
        Invalidate();
    }
    void Describe(A11y& a) override {
        a.role = Role::List;
        a.name = Tr(L"Subtitle style");
        a.items = static_cast<int>(chips_.size());
        a.item = [this](int k, A11y::Item& it) {
            const SubtitlePreset& p = SubtitlePresets()[static_cast<size_t>(k)];
            it.role = Role::ListItem;
            it.name = std::wstring(Tr(p.name)) + L". " + Tr(p.detail);
            it.rect = chips_[static_cast<size_t>(k)];
            it.selected = k == current_;
        };
        a.selectItem = a.invokeItem = [this](int k) { Pick(k); };
        if (IsFocused() && focus_ >= 0) a.focusedItem = focus_;
    }

private:
    static constexpr float kChipH = 34, kChipGap = 8, kGap = 14;

    // The example 16:9 up to 400 high, the names wrapped under it, then the line about the style.
    void Place(float w, float y0, float x0 = 0) {
        const float ph = std::round(std::min(w * 9 / 16, 400.0f));
        preview_ = {x0, y0, w, ph};
        chips_.clear();
        float x = x0, y = y0 + ph + kGap;
        for (const SubtitlePreset& p : SubtitlePresets()) {
            const float cw = std::ceil(gfx::Text::Get().Measure(Tr(p.name), TextStyle::Body).width) + 32;
            if (x > x0 && x + cw > x0 + w) {
                x = x0;
                y += kChipH + kChipGap;
            }
            chips_.push_back({x, y, cw, kChipH});
            x += cw + kChipGap;
        }
        note_ = {x0 + 2, y + kChipH + 8, w - 4, 22};
        height_ = note_.Bottom() - y0 + 4;
    }

    int ChipAt(float x, float y) const {
        for (size_t i = 0; i < chips_.size(); ++i)
            if (chips_[i].Contains(x, y)) return static_cast<int>(i);
        return -1;
    }

    void SetHover(int i) {
        if (i == hover_) return;
        hover_ = i;
        Show(i >= 0 ? i : (IsFocused() ? focus_ : current_));
        Invalidate();
    }

    // The example in preset i, or in the settings as they are (-1).
    void Show(int i) {
        shown_ = i == current_ ? -1 : i;
        const float pos = static_cast<float>(Cfg().GetDouble("subtitles.position"));
        if (i >= 0) sample_.SetStyle(PresetSubtitleStyle(Cfg(), SubtitlePresets()[static_cast<size_t>(i)]), pos);
        else sample_.SetStyle(ReadSubtitleStyle(Cfg()), pos);
    }

    void Pick(int i) {
        if (i < 0 || i >= static_cast<int>(SubtitlePresets().size())) return;
        focus_ = i;
        ApplySubtitlePreset(Cfg(), SubtitlePresets()[static_cast<size_t>(i)]);
        Sync();
    }

    SubtitleSample sample_;
    Rect preview_, note_;
    std::vector<Rect> chips_;
    float height_ = 0;
    int current_ = -1, hover_ = -1, focus_ = -1, shown_ = -1;
};

}  // namespace

std::unique_ptr<Widget> MakeSubtitlePreview(const Item& item) {
    return std::make_unique<SubtitleStylePicker>(item);
}

}  // namespace app::sui

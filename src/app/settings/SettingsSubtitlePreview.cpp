// Subtitle samples drawn with a style, so a change shows without a playing video (R3-08): the
// example on top of Settings > Subtitles and the cards of the ready-made styles (here and in the
// player's Subtitles panel). An approximation of libass in Direct2D: the outline is the text
// stamped around a circle, the shadow an offset copy, the blur a Gaussian blur effect. The text
// is recorded into a command list once per style change, so repaints only replay it.
#include "app/I18n.h"
#include "app/player/SubStyle.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"
#include "gfx/Gpu.h"
#include "gfx/Text.h"

#include <d2d1effects.h>

#include <cmath>
#include <functional>

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

// CLSID_D2D1GaussianBlur (defined here so we don't need dxguid.lib).
constexpr GUID kGaussianBlur = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

// Everything that changes the picture; compared on every settings change.
std::wstring StyleKey(const SubtitleStyle& s, float position) {
    wchar_t buf[200];
    swprintf_s(buf, L"|%g|%d|%d|%08X%02X|%08X|%08X%02X|%g|%d|%hs|%g|%g", s.size, s.bold ? 1 : 0, s.weight, s.text.Rgb(),
               static_cast<unsigned>(s.text.a * 255), s.outlineColor.Rgb(), s.back.Rgb(),
               static_cast<unsigned>(s.back.a * 255), s.outline, s.shadow ? 1 : 0, s.background.c_str(), s.blur,
               position);
    return s.font + buf;
}

// The "frame" behind the text: a night half and a daylight half, so readability shows on both.
// They stand for video content, not UI, so they are not theme tokens.
void PaintScene(Painter& p, const Rect& f) {
    ID2D1DeviceContext5* ctx = p.Ctx();
    const float half = std::round(f.w / 2);
    auto gradient = [&](const Rect& r, uint32_t top, uint32_t bottom) {
        if (!ctx) return p.Fill(r, Color::Hex(bottom));
        const D2D1_GRADIENT_STOP stops[] = {{0, Color::Hex(top).D2D()}, {1, Color::Hex(bottom).D2D()}};
        gfx::ComPtr<ID2D1GradientStopCollection> coll;
        gfx::ComPtr<ID2D1LinearGradientBrush> brush;
        if (FAILED(ctx->CreateGradientStopCollection(stops, 2, &coll)) ||
            FAILED(ctx->CreateLinearGradientBrush({{r.x, r.y}, {r.x, r.Bottom()}}, D2D1::BrushProperties(), coll.Get(), &brush)))
            return p.Fill(r, Color::Hex(bottom));
        ctx->FillRectangle(D2D1::RectF(r.x, r.y, r.Right(), r.Bottom()), brush.Get());
    };
    gradient({f.x, f.y, half, f.h}, 0x2A3556, 0x0E121D);
    gradient({f.x + half, f.y, f.w - half, f.h}, 0xF3E3C3, 0xB8C6D1);
    // A moon in the night and a sun haze in the day, faint: a picture, not a test card.
    const Point moon{f.x + half * 0.26f, f.y + f.h * 0.28f};
    p.FillCircle(moon, f.h * 0.16f, Color::Hex(0xDCE6FF, 0.06f));
    p.FillCircle(moon, f.h * 0.085f, Color::Hex(0xEEF2FF, 0.55f));
    p.FillCircle({f.x + half * 1.72f, f.y + f.h * 0.26f}, f.h * 0.3f, Color::Hex(0xFFFFFF, 0.28f));
}

// A line (or two) of subtitles drawn with a style over the scene.
class SubtitleSample {
public:
    SubtitleSample(std::wstring text, float scale) : text_(std::move(text)), scale_(scale) {}

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
        p.PushRoundClip(f, radius);
        PaintScene(p, f);
        PaintText(p, f);
        p.PopClip();
    }

private:
    bool Prepare(ID2D1DeviceContext5* ctx) {
        const uint32_t gen = gfx::Gpu::Get().Generation();
        if (gen != generation_) {
            generation_ = gen;
            scene_.Reset();
            blur_.Reset();
        }
        if (!layout_) {
            IDWriteFactory5* dw = gfx::Gpu::Get().DWrite();
            if (!dw) return false;
            gfx::ComPtr<IDWriteTextFormat> format;
            if (FAILED(dw->CreateTextFormat(style_.font.c_str(), gfx::Text::Get().CollectionFor(style_.font),
                                            static_cast<DWRITE_FONT_WEIGHT>(style_.weight),
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style_.size * scale_,
                                            L"", &format)))
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

    // Shadow, outline and text at the origin, on a separate context of the same device (the
    // window's context is mid-frame).
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
        // mpv draws the shadow in the box color (sub-back-color).
        if (style_.shadow && !lineBoxes && style_.background != "box") {
            const float s = std::max(1.0f, 2 * scale_);
            stamp(s, s, style_.back);
        }
        if (r > 0) stamp(0, 0, style_.outlineColor);
        brush->SetColor(style_.text.D2D());
        rec->DrawTextLayout({0, 0}, layout_.Get(), brush.Get());

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
        // Vertical position: 100% at the bottom edge, as in mpv (with a small margin).
        const float margin = std::min(12.0f, frame.h * 0.12f);
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
            const float o = style_.outline * scale_;
            p.Fill(textRect.Inset(Insets::Sym(-8 * scale_ / 0.6f - o, -3 - o)), style_.back, 3);
        }

        if (style_.blur > 0 && blur_) {
            blur_->SetInput(0, scene_.Get());
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, style_.blur * scale_);
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
            gfx::ComPtr<ID2D1Image> out;
            blur_->GetOutput(&out);
            ctx->DrawImage(out.Get(), D2D1::Point2F(x, y));
            // libass blurs only the outline when there is one: the letters stay sharp on top.
            if (style_.outline > 0 && style_.background != "lines") {
                gfx::ComPtr<ID2D1SolidColorBrush> brush;
                if (SUCCEEDED(ctx->CreateSolidColorBrush(style_.text.D2D(), &brush)))
                    ctx->DrawTextLayout(D2D1::Point2F(x, y), layout_.Get(), brush.Get());
            }
        } else {
            ctx->DrawImage(scene_.Get(), D2D1::Point2F(x, y));
        }
    }

    std::wstring text_;
    float scale_;
    SubtitleStyle style_;
    float position_ = 100;
    std::wstring key_;
    gfx::ComPtr<IDWriteTextLayout> layout_;
    gfx::ComPtr<ID2D1CommandList> scene_;
    gfx::ComPtr<ID2D1Effect> blur_;
    uint32_t generation_ = 0;
    float textW_ = 0, textH_ = 0;
};

// mpv sizes are pixels of a 720-line frame; 0.6 keeps two lines readable in the strip while the
// steps between sizes stay visible.
constexpr float kPreviewScale = 0.6f;
constexpr float kPreviewH = 150;

class SubtitlePreview : public Widget, public Syncable {
public:
    explicit SubtitlePreview(const Item& it)
        : sample_(Tr(L"— Did you see the sky glow?\n— It's a moonbow."), kPreviewScale) {
        width = Length::Grow();
        height = Length::Px(kPreviewH + 4);
        id = it.key;
        tooltip = it.Label(Lang());
        Sync();
    }

    void Sync() override {
        if (sample_.SetStyle(ReadSubtitleStyle(Cfg()), static_cast<float>(Cfg().GetDouble("subtitles.position"))))
            Invalidate();
    }

    void Paint(Painter& p) override {
        const Rect f{rect.x, rect.y + 2, rect.w, kPreviewH};
        sample_.Paint(p, f, 8);
        p.Stroke(f, P().line, 8);
    }

private:
    SubtitleSample sample_;
};

// One ready-made style: its sample over the scene and its name; the current one has a ring.
class StyleCard : public Widget {
public:
    StyleCard(const SubtitlePreset& preset, float frameH)
        : preset_(preset), frameH_(frameH), sample_(Tr(L"A moonbow"), frameH / 150.0f) {
        interactive = true;
        focusable = true;
        width = Length::Grow();
        height = Length::Px(frameH + 30);
        tooltip = Tr(preset.name);
        Sync();
    }

    void Sync() {
        const bool on = MatchSubtitlePreset(Cfg()) == &preset_;
        const bool changed = sample_.SetStyle(PresetSubtitleStyle(Cfg(), preset_), 82);
        if (changed || on != on_) {
            on_ = on;
            Invalidate();
        }
    }

    void Paint(Painter& p) override {
        const Palette& c = P();
        const Rect f{rect.x, rect.y, rect.w, frameH_};
        sample_.Paint(p, f, 8);
        if (on_) p.Stroke(f.Inset(-1), c.accent, 9, 2);
        else p.Stroke(f, IsHot() ? c.lineStrong : c.line, 8);
        PaintFocusRing(p, on_ ? f.Inset(-1) : f, 8);
        const Rect cap{rect.x + 2, f.Bottom() + 6, rect.w - 4, 20};
        float tx = cap.x;
        if (on_) {
            p.Icon("check_selected", {cap.x - 2, cap.Cy() - 9, 18, 18}, c.accent);
            tx += 20;
        }
        p.Text(Tr(preset_.name), {tx, cap.y, cap.Right() - tx, cap.h}, TextStyle::Body,
               on_ || IsHot() ? c.textPrimary : c.textSecondary, HAlign::Leading, VAlign::Center, gfx::kTextEllipsis);
    }

    void OnClick() override { ApplySubtitlePreset(Cfg(), preset_); }
    bool OnKeyDown(const KeyEvent& e) override {
        if (e.key != VK_SPACE && e.key != VK_RETURN) return false;
        OnClick();
        return true;
    }
    void Describe(A11y& a) override {
        a.role = Role::ListItem;
        a.name = Tr(preset_.name);
        a.selected = on_ ? 1 : 0;
    }

private:
    const SubtitlePreset& preset_;
    float frameH_;
    SubtitleSample sample_;
    bool on_ = false;
};

// The ready-made styles in rows of `columns`, and a line for a style of one's own.
class StyleCards : public Widget, public Syncable {
public:
    StyleCards(int columns, float frameH) {
        dir = Dir::Column;
        width = Length::Grow();
        gap = 12;
        pad.t = 12;
        const auto& presets = SubtitlePresets();
        Widget* row = nullptr;
        for (size_t i = 0; i < presets.size(); ++i) {
            if (i % static_cast<size_t>(columns) == 0) {
                row = Add<Widget>();
                row->dir = Dir::Row;
                row->width = Length::Grow();
                row->gap = 12;
            }
            cards_.push_back(row->Add<StyleCard>(presets[i], frameH));
        }
        // Keeps the last row's cards as wide as the others.
        for (size_t i = presets.size(); i % static_cast<size_t>(columns) != 0; ++i) row->Add<Widget>()->width = Length::Grow();
        own_ = Add<Label>(Tr(L"Your own style: changed in the settings below."), TextStyle::Small, Tone::Tertiary);
        own_->wrap = true;
        own_->width = Length::Grow();
        Sync();
    }

    void Sync() override {
        for (StyleCard* c : cards_) c->Sync();
        const bool own = MatchSubtitlePreset(Cfg()) == nullptr;
        if (own_->visible != own) {
            own_->visible = own;
            InvalidateLayout();
        }
    }
    void Describe(A11y& a) override {
        a.role = Role::List;
        a.name = Tr(L"Subtitle style");
    }
    Label* OwnLine() const { return own_; }

private:
    std::vector<StyleCard*> cards_;
    Label* own_ = nullptr;
};

}  // namespace

std::unique_ptr<Widget> MakeSubtitlePreview(const Item& item) {
    return std::make_unique<SubtitlePreview>(item);
}

std::unique_ptr<Widget> MakeSubtitleStyles(const Item& item) {
    auto cards = std::make_unique<StyleCards>(3, 84.0f);
    cards->id = item.key;
    return cards;
}

std::unique_ptr<Widget> MakeSubtitleStyleCards(std::function<void(const std::function<void()>&)> onSync) {
    auto cards = std::make_unique<StyleCards>(2, 64.0f);
    cards->OwnLine()->SetText(Tr(L"Your own style: changed in Settings."));
    StyleCards* raw = cards.get();
    if (onSync) onSync([raw] { raw->Sync(); });
    return cards;
}

}  // namespace app::sui

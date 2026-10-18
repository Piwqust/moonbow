// Settings > Subtitles: a sample line drawn with the current style, so a change shows without a
// playing video (R3-08). An approximation of libass in Direct2D: the outline is the text stamped
// around a circle, the shadow an offset copy, the blur a Gaussian blur effect. The text is recorded
// into a command list once per style change, so repaints (hover, scrolling) only replay it.
#include "app/I18n.h"
#include "app/settings/SettingsUi.h"
#include "core/Config.h"
#include "gfx/Gpu.h"

#include <d2d1effects.h>

#include <cmath>

namespace app::sui {

using namespace ui;
using settings::Item;

namespace {

// CLSID_D2D1GaussianBlur (defined here so we don't need dxguid.lib).
constexpr GUID kGaussianBlur = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

// mpv sizes are pixels of a 720-line frame; 0.6 keeps the sample readable in a 128-high strip
// while the steps between sizes stay visible.
constexpr float kScale = 0.6f;
constexpr float kHeight = 128;

// Colors of the sample "frame" (dark and bright halves, so readability shows on both). They stand
// for video content, not UI, so they are not theme tokens.
constexpr uint32_t kSceneDark = 0x2B3138, kSceneBright = 0xCBD5DD;

Color HexColor(const std::string& s, uint32_t fallback, float alpha) {
    uint32_t v = fallback;
    if (s.size() == 7 && s[0] == '#') {
        uint32_t parsed = 0;
        bool ok = true;
        for (size_t i = 1; i < 7 && ok; ++i) {
            const char c = s[i];
            parsed <<= 4;
            if (c >= '0' && c <= '9') parsed |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') parsed |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') parsed |= static_cast<uint32_t>(c - 'A' + 10);
            else ok = false;
        }
        if (ok) v = parsed;
    }
    return Color::Hex(v, std::clamp(alpha, 0.0f, 1.0f));
}

// The mpv value of a choice setting ("3" for outline "normal").
double ChoiceNumber(const char* key, double fallback) {
    const Item* it = settings::FindItem(key);
    if (!it) return fallback;
    const settings::Choice* c = it->FindChoice(Cfg().GetString(key));
    if (!c || c->mpv.empty()) return fallback;
    return std::atof(c->mpv.c_str());
}

struct Style {
    std::wstring font;
    float size = 38;
    bool bold = false;
    Color text, outlineColor, back;
    float outline = 3;
    bool shadow = true;
    std::string background;  // none / lines / box
    float blur = 0;
    float position = 100;  // % of the frame height

    static Style Read() {
        Style s;
        s.font = Cfg().GetWString("subtitles.font");
        // mpv's generic families; libass resolves them like this on Windows.
        if (s.font.empty() || _wcsicmp(s.font.c_str(), L"sans-serif") == 0) s.font = L"Arial";
        else if (_wcsicmp(s.font.c_str(), L"serif") == 0) s.font = L"Times New Roman";
        else if (_wcsicmp(s.font.c_str(), L"monospace") == 0) s.font = L"Consolas";
        s.size = static_cast<float>(ChoiceNumber("subtitles.size", 38));
        if (s.size <= 0) s.size = 38;  // "Automatic" has no mpv value: mpv's default size
        s.bold = Cfg().GetBool("subtitles.bold");
        s.text = HexColor(Cfg().GetString("subtitles.color"), 0xFFFFFF,
                          static_cast<float>(Cfg().GetDouble("subtitles.opacity") / 100.0));
        s.outlineColor = HexColor(Cfg().GetString("subtitles.outlineColor"), 0x000000, 1);
        s.back = HexColor(Cfg().GetString("subtitles.backColor"), 0x000000,
                          static_cast<float>(Cfg().GetDouble("subtitles.backOpacity") / 100.0));
        s.outline = static_cast<float>(ChoiceNumber("subtitles.outline", 3));
        s.shadow = Cfg().GetBool("subtitles.shadow");
        s.background = Cfg().GetString("subtitles.background");
        s.blur = static_cast<float>(Cfg().GetDouble("subtitles.blur"));
        s.position = static_cast<float>(Cfg().GetDouble("subtitles.position"));
        return s;
    }

    // Everything that changes the picture; compared on every settings change.
    std::wstring Key() const {
        wchar_t buf[200];
        swprintf_s(buf, L"|%g|%d|%08X%02X|%08X|%08X%02X|%g|%d|%hs|%g|%g", size, bold ? 1 : 0, text.Rgb(),
                   static_cast<unsigned>(text.a * 255), outlineColor.Rgb(), back.Rgb(),
                   static_cast<unsigned>(back.a * 255), outline, shadow ? 1 : 0, background.c_str(), blur, position);
        return font + buf;
    }
};

class SubtitlePreview : public Widget, public Syncable {
public:
    explicit SubtitlePreview(const Item& it) {
        width = Length::Grow();
        height = Length::Px(kHeight);
        id = it.key;
        tooltip = it.Label(Lang());
        Sync();
    }

    void Sync() override {
        Style s = Style::Read();
        std::wstring key = s.Key();
        if (key == key_) return;
        key_ = std::move(key);
        style_ = std::move(s);
        scene_.Reset();  // recorded again on the next paint
        layout_.Reset();
        Invalidate();
    }

    void Paint(Painter& p) override {
        p.PushRoundClip(rect, 6);
        const float half = std::round(rect.w / 2);
        p.Fill({rect.x, rect.y, half, rect.h}, Color::Hex(kSceneDark));
        p.Fill({rect.x + half, rect.y, rect.w - half, rect.h}, Color::Hex(kSceneBright));
        PaintLine(p);
        p.PopClip();
        p.Stroke(rect, P().line, 6);
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
            if (FAILED(dw->CreateTextFormat(style_.font.c_str(), nullptr,
                                            style_.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style_.size * kScale,
                                            L"", &format)))
                return false;
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            const std::wstring text = Tr(L"Subtitle example");
            if (FAILED(dw->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), 4000, 400,
                                            &layout_)))
                return false;
            DWRITE_TEXT_METRICS m{};
            layout_->GetMetrics(&m);
            textW_ = m.width;
            textH_ = m.height;
        }
        if (!scene_ && !Record(ctx)) return false;
        if (style_.blur > 0 && !blur_ && FAILED(ctx->CreateEffect(kGaussianBlur, &blur_))) blur_.Reset();
        return true;
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
        const float r = lineBoxes ? 0.0f : style_.outline * kScale;
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
        if (style_.shadow && !lineBoxes) {
            const float s = std::max(1.0f, 2 * kScale);
            stamp(s, s, Color::Hex(0x000000, 0.5f));  // mpv's default shadow color
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

    void PaintLine(Painter& p) {
        ID2D1DeviceContext5* ctx = p.Ctx();
        if (!ctx || !Prepare(ctx)) return;
        const float x = std::round(rect.Cx() - textW_ / 2);
        // Vertical position: 100% at the bottom edge, as in mpv (with a small margin).
        const float margin = 12;
        const float travel = std::max(0.0f, rect.h - 2 * margin - textH_);
        const float y = std::round(rect.y + margin + travel * std::clamp(style_.position, 0.0f, 100.0f) / 100);

        const Rect textRect{x, y, textW_, textH_};
        if (style_.background == "lines") {
            const float boxPad = std::max(2.0f, style_.outline * kScale);
            const Rect box = textRect.Inset(-boxPad);
            if (style_.shadow) {
                const float s = std::max(1.0f, 2 * kScale);
                p.Fill({box.x + s, box.y + s, box.w, box.h}, Color::Hex(0x000000, 0.5f));
            }
            p.Fill(box, style_.outlineColor);
        } else if (style_.background == "box") {
            const float o = style_.outline * kScale;
            p.Fill(textRect.Inset(Insets::Sym(-8 - o, -3 - o)), style_.back, 3);
        }

        if (style_.blur > 0 && blur_) {
            blur_->SetInput(0, scene_.Get());
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, style_.blur * kScale * 0.5f);
            blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
            gfx::ComPtr<ID2D1Image> out;
            blur_->GetOutput(&out);
            ctx->DrawImage(out.Get(), D2D1::Point2F(x, y));
        } else {
            ctx->DrawImage(scene_.Get(), D2D1::Point2F(x, y));
        }
    }

    Style style_;
    std::wstring key_;
    gfx::ComPtr<IDWriteTextLayout> layout_;
    gfx::ComPtr<ID2D1CommandList> scene_;
    gfx::ComPtr<ID2D1Effect> blur_;
    uint32_t generation_ = 0;
    float textW_ = 0, textH_ = 0;
};

}  // namespace

std::unique_ptr<Widget> MakeSubtitlePreview(const Item& item) {
    return std::make_unique<SubtitlePreview>(item);
}

}  // namespace app::sui

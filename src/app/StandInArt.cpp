#include "app/StandInArt.h"

#include <windows.h>

namespace app {

using namespace ui;

uint32_t ArtSeed(const std::wstring& s) {
    uint32_t h = 2166136261u;
    for (wchar_t c : s) h = (h ^ static_cast<uint32_t>(c)) * 16777619u;
    return h;
}

void PaintStandInArt(Painter& p, const Rect& r, float radius, uint32_t seed, const StandInTones& tones) {
    p.Fill(r, tones.ground, radius);
    p.PushRoundClip(r, radius);
    const Point center{r.Cx(), r.Cy()};
    const float maxR = r.w * 0.42f;
    int k = 0;
    // The seed picks which grooves are a little brighter: every record has its own pattern.
    for (float rad = maxR * 0.38f; rad < maxR; rad += r.w * 0.012f, ++k) {
        const float a = 0.025f + 0.035f * static_cast<float>((seed >> (k % 24)) & 1);
        p.StrokeCircle(center, rad, tones.ink.Fade(a));
    }
    p.StrokeCircle(center, maxR, tones.ink.Fade(0.14f), 1.5f);
    p.FillCircle(center, maxR * 0.30f, tones.ink.Fade(0.03f));
    const float icon = maxR * 0.22f;
    p.Icon("music", {center.x - icon / 2, center.y - icon / 2, icon, icon}, tones.note);
    p.PopClip();
    if (tones.border.a > 0) p.Stroke(r, tones.border, radius);
}

std::wstring Initials(const std::wstring& name) {
    std::wstring out;
    bool wordStart = true;
    for (wchar_t ch : name) {
        if (!IsCharAlphaNumericW(ch)) {
            wordStart = true;
            continue;
        }
        if (wordStart) out += ch;
        wordStart = false;
        if (out.size() == 2) break;
    }
    CharUpperBuffW(out.data(), static_cast<DWORD>(out.size()));  // Cyrillic too, unlike the C locale
    return out;
}

void PaintInitials(Painter& p, const Rect& r, const std::wstring& initials, Color ground, Color ink) {
    p.FillCircle({r.Cx(), r.Cy()}, r.w / 2, ground);
    // The largest face is 32 DIP: scaled about the centre, the letters take about a quarter of the circle.
    const float k = r.w * 0.26f / 32;
    ID2D1DeviceContext5* ctx = p.Ctx();
    D2D1_MATRIX_3X2_F old;
    ctx->GetTransform(&old);
    ctx->SetTransform(D2D1::Matrix3x2F::Scale(k, k, {r.Cx(), r.Cy()}) * old);
    const Rect box{r.Cx() - r.w / (2 * k), r.Cy() - r.h / (2 * k), r.w / k, r.h / k};
    p.Text(initials, box, gfx::TextStyle::Display, ink, ui::HAlign::Center, ui::VAlign::Center, 0);
    ctx->SetTransform(old);
}

}  // namespace app

#include "app/StandInArt.h"

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

}  // namespace app

#pragma once
// Drawing of the audio visualizer (spectrum bars, scrolling loudness, oscilloscope, analog VU
// meters) from an audio::VisualFrame. Shared by the music view and the audio meters window;
// light on a dark background, as both always are.

#include "audio/LoopbackCapture.h"
#include "gfx/Gpu.h"
#include "ui/Painter.h"

#include <d2d1_1.h>

#include <vector>

namespace app {

class VizPaint {
public:
    static constexpr float kVuAspect = 1.6f;

    // Moves the VU needles toward the frame's levels (`live` false: back to the stop). True while
    // a needle is off the stop, so the caller keeps painting.
    bool UpdateNeedles(const audio::VisualFrame& frame, bool live, float referenceDb);

    // AIMP-style segmented bars with falling peak marks. `reverse` puts the low notes on the right;
    // `wing` grows them both ways from the middle, fading away from the cover; `rest`: silence.
    void Spectrum(ui::Painter& p, const ui::Rect& r, const audio::VisualFrame& frame, ui::Color white, bool reverse,
                  bool wing, bool rest = false);
    void Waveform(ui::Painter& p, const ui::Rect& r, const audio::VisualFrame& frame, ui::Color white);
    void Scope(ui::Painter& p, const ui::Rect& r, const audio::VisualFrame& frame, ui::Color fg, ui::Color dim);
    // Left and right meters side by side, or stacked when `r` is tall; `rest`: needles on the stop.
    // `ink`: the one colour they are drawn in (white on the music view, the theme's text colour
    // on the meters window's cards).
    void VuPair(ui::Painter& p, const ui::Rect& r, bool rest, ui::Color ink);
    void VuMeter(ui::Painter& p, const ui::Rect& m, int ch, bool rest, ui::Color ink);
    static ui::Rect FitVu(const ui::Rect& r);  // the largest meter that fits, centered

    // Lines go straight to the context (Painter has no line call); made once per device.
    ID2D1StrokeStyle* RoundStroke(ID2D1DeviceContext5* ctx);
    ID2D1SolidColorBrush* LineBrush(ID2D1DeviceContext5* ctx, ui::Color c, float opacity);

private:
    struct Hold {
        float pos = 0;
        double at = 0;
    };
    audio::VuNeedle needles_[2];  // left, right
    Hold holds_[2];               // the highest needle position of the last moment
    double vuAt_ = 0;             // when the needles last moved (AnimFloat::Now)
    gfx::ComPtr<ID2D1StrokeStyle> roundStroke_;
    gfx::ComPtr<ID2D1SolidColorBrush> lineBrush_;
    uint32_t strokeGen_ = 0, lineGen_ = 0;
};

}  // namespace app

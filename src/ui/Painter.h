#pragma once
#include "gfx/Gpu.h"
#include "gfx/Text.h"
#include "ui/Theme.h"
#include "ui/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace ui {

class Widget;

using gfx::HAlign;
using gfx::TextStyle;
using gfx::VAlign;

// One Painter::Text call seen during a tree dump (Host::DumpTree): what a widget shows and
// the box it gave the text. Never collected during normal frames.
struct TextRecord {
    const Widget* owner = nullptr;  // the widget whose Paint/PaintOver drew it
    std::wstring text;
    Rect box;
    Rect clip;  // Painter::Clip() at the time
    TextStyle style = TextStyle::Body;
    HAlign halign = HAlign::Leading;
    uint8_t flags = 0;
    bool faded = false;  // drawn (nearly) fully transparent
};

// Thin drawing API over the shared D2D device context. All coordinates are
// window DIPs. A painter lives for one frame of one window.
class Painter {
public:
    explicit Painter(ID2D1DeviceContext5* ctx);
    ~Painter();

    ID2D1DeviceContext5* Ctx() const { return ctx_; }

    void Fill(const Rect& r, Color c, float radius = 0);
    // Stroke drawn inside `r` (crisp 1 px borders on integer rects).
    void Stroke(const Rect& r, Color c, float radius = 0, float width = 1);
    void Line(Point a, Point b, Color c, float width = 1);
    void FillCircle(Point center, float radius, Color c);
    void FillGeometry(ID2D1Geometry* g, Color c);

    void Text(std::wstring_view text, const Rect& r, TextStyle style, Color c, HAlign h = HAlign::Leading,
              VAlign v = VAlign::Center, uint8_t flags = gfx::kTextEllipsis);
    gfx::TextMetrics Measure(std::wstring_view text, TextStyle style, float maxWidth = 100000.0f,
                             uint8_t flags = 0) const;

    // 24×24 design icon fitted into `box`. `secondary` colors two-tone parts.
    void Icon(std::string_view name, const Rect& box, Color c);
    void Icon(std::string_view name, const Rect& box, Color c, Color secondary);

    // Bitmap scaled to cover (crop) or contain `r`, with rounded corners.
    void Image(ID2D1Bitmap* bitmap, const Rect& r, float radius = 0, bool cover = true, float opacity = 1);

    // Soft drop shadow under a rounded rect (approximated with stacked fills).
    void Shadow(const Rect& r, float radius, float blur = 15, float dy = 8, float alpha = -1);

    void PushClip(const Rect& r);
    void PushRoundClip(const Rect& r, float radius);
    void PopClip();
    const Rect& Clip() const { return clips_.back().rect; }

    // Tree dumps only: every Text call is appended to `sink`, tagged with the widget being painted.
    void RecordText(std::vector<TextRecord>* sink) { textSink_ = sink; }
    bool RecordingText() const { return textSink_ != nullptr; }
    void SetTextOwner(const Widget* w) { textOwner_ = w; }

    // Multiplies the alpha of everything painted until PopOpacity.
    void PushOpacity(float k);
    void PopOpacity();
    float Opacity() const { return opacity_; }

private:
    ID2D1SolidColorBrush* Brush(Color c);

    struct ClipEntry {
        Rect rect;
        bool layer;
    };
    ID2D1DeviceContext5* ctx_;
    gfx::ComPtr<ID2D1SolidColorBrush> brush_;
    std::vector<ClipEntry> clips_;
    std::vector<float> opacities_;
    float opacity_ = 1.0f;
    std::vector<TextRecord>* textSink_ = nullptr;
    const Widget* textOwner_ = nullptr;
};

}  // namespace ui

#pragma once
#include "gfx/Gpu.h"
#include "ui/Types.h"

#include <string_view>

namespace gfx {

// The 24×24 icon set exported from Figma (design/icons, packed into IconData.inc).
// Icons are drawn as SVG documents with their two colors swapped in:
// `primary` replaces the design's #8F8F8F, `secondary` the orange accent.
class Icons {
public:
    static bool Exists(std::string_view name);
    // Draws icon `name` scaled to fit `box` (24 DIP is the native size).
    static void Draw(ID2D1DeviceContext5* ctx, std::string_view name, const ui::Rect& box, ui::Color primary,
                     ui::Color secondary);
    // Releases cached documents (after device loss or a theme change).
    static void Reset();
};

}  // namespace gfx

#pragma once
#include "core/Skin.h"
#include "ui/Types.h"

#include <cstdint>
#include <memory>

namespace ui {

// Color tokens, see design/DESIGN.md section 1. The values live in core/Skin.cpp (the brand
// palette) and may be overridden by a skin (design/SKINS.md).
struct Palette {
    Color bgDeep, bgBase, bgRaised, bgPanel, bgChrome, bgCard, bgSelected, bgMenu;
    Color hover, press;
    Color lineSubtle, line, lineStrong;
    Color textPrimary, textSecondary, textTertiary, textDisabled;
    Color accent, accentMid, accentDim, accentFaint, onAccent;
    Color scrim, shadow, danger;
    Color videoBg;  // around the video and behind letterboxing; mpv paints black, so never skinned
};

enum class ThemeMode : uint8_t { Dark, Light };

inline constexpr int kAccentCount = skin::kAccentTones;

class Theme {
public:
    static Theme& Get();

    const Palette& P() const { return p_; }
    // The mode in effect: general.theme, unless the skin has only one mode.
    ThemeMode Mode() const { return mode_; }
    ThemeMode RequestedMode() const { return requested_; }  // general.theme
    int Accent() const { return accent_; }

    void Set(ThemeMode mode, int accent);
    // nullptr (or an empty skin) = the Lumen look. Callers then repaint every Host.
    void SetSkin(std::shared_ptr<const skin::Skin> s);
    const skin::Skin* Skin() const { return skin_.get(); }

    // Swatch shown in accent pickers for tone `index` in the current mode.
    Color AccentSwatch(int index) const;
    static const wchar_t* AccentName(int index);  // English, for i18n lookup

    // A design radius (2, 4, 6, 12, 14: DESIGN.md section 3) as the skin sets it; any other value
    // (pills, circles, thumbs) is geometry and passes unchanged. Painter applies it.
    float Radius(float design) const { return radii_ ? MapRadius(design) : design; }
    // Opacity of the floating control bar's panel; 1 unless the skin asks for less.
    float BarOpacity() const { return barOpacity_; }

private:
    void Rebuild();
    float MapRadius(float design) const;
    Palette p_{};
    ThemeMode requested_ = ThemeMode::Dark;
    ThemeMode mode_ = ThemeMode::Dark;
    int accent_ = 0;
    std::shared_ptr<const skin::Skin> skin_;
    bool radii_ = false;  // the skin changes at least one radius
    float barOpacity_ = 1.0f;
};

inline const Palette& P() {
    return Theme::Get().P();
}

}  // namespace ui

#pragma once
#include "ui/Types.h"

#include <cstdint>

namespace ui {

// Color tokens, see design/DESIGN.md section 1.
struct Palette {
    Color bgDeep, bgBase, bgRaised, bgPanel, bgChrome, bgCard, bgSelected, bgMenu;
    Color hover, press;
    Color lineSubtle, line, lineStrong;
    Color textPrimary, textSecondary, textTertiary, textDisabled;
    Color accent, accentMid, accentDim, accentFaint, onAccent;
    Color scrim, shadow, danger;
    Color videoBg;  // around the video and behind letterboxing
};

enum class ThemeMode : uint8_t { Dark, Light };

inline constexpr int kAccentCount = 5;

class Theme {
public:
    static Theme& Get();

    const Palette& P() const { return p_; }
    ThemeMode Mode() const { return mode_; }
    int Accent() const { return accent_; }

    void Set(ThemeMode mode, int accent);
    // Swatch shown in accent pickers for tone `index` in the current mode.
    Color AccentSwatch(int index) const;
    static const wchar_t* AccentName(int index);  // English, for i18n lookup

private:
    void Rebuild();
    Palette p_{};
    ThemeMode mode_ = ThemeMode::Dark;
    int accent_ = 0;
};

inline const Palette& P() {
    return Theme::Get().P();
}

}  // namespace ui

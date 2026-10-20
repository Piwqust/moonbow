#pragma once
// Skins: JSON files of design tokens over design/DESIGN.md (format in design/SKINS.md).
//
// This file holds the brand palette, the parser and the contrast check, with no window code, so
// tests/core_tests.cpp can check them. ui::Theme turns a resolved palette into its Palette.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace skin {

struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
    static constexpr Rgba Hex(uint32_t rgb, float alpha = 1.0f) {
        return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
    }
    uint32_t Rgb() const;  // 0xRRGGBB
    bool operator==(const Rgba& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

Rgba Mix(const Rgba& x, const Rgba& y, float t);

// Colour tokens, in DESIGN.md order. Names in skin files: TokenName().
enum Token : int {
    kBgDeep, kBgBase, kBgRaised, kBgPanel, kBgChrome, kBgCard, kBgSelected, kBgMenu,
    kHover, kPress,
    kLineSubtle, kLine, kLineStrong, kLineControl,
    kTextPrimary, kTextSecondary, kTextTertiary, kTextDisabled,
    kAccent, kAccentMid, kAccentDim, kAccentFaint, kOnAccent,
    kScrim, kShadow, kDanger,
    kTokenCount,
};
const char* TokenName(int token);  // "bg.deep"
int FindToken(std::string_view name);  // -1 when unknown
// Tokens that may be translucent. Every other colour is drawn opaque: a skin cannot make panels
// see-through by accident (round 5 rule); only controlBar.opacity asks for it explicitly.
bool TokenAllowsAlpha(int token);

using Colors = std::array<Rgba, kTokenCount>;

inline constexpr int kAccentTones = 5;
Rgba AccentTone(bool dark, int index);  // general.accent tones: White, Pearl, Silver, Steel, Graphite

// Radius tokens: the design's five corner radii (DESIGN.md section 3).
enum Radius : int { kRadiusSmall, kRadiusControl, kRadiusCard, kRadiusPopup, kRadiusWindow, kRadiusCount };
inline constexpr float kDesignRadii[kRadiusCount] = {2, 4, 6, 12, 14};
const char* RadiusName(int radius);  // "small", "control", "card", "popup", "window"
inline constexpr float kMaxRadius = 24;

inline constexpr float kMinBarOpacity = 0.5f;  // below it the controls get lost in the picture

enum class Mode : uint8_t { Any, Dark, Light };  // Dark / Light: the skin has one mode and forces it

struct Skin {
    std::wstring name, nameRu;  // empty: the caller names it after the file
    Mode mode = Mode::Any;
    std::array<std::optional<Rgba>, kTokenCount> dark, light;
    std::array<std::optional<float>, kRadiusCount> radius;
    std::optional<float> barOpacity;

    // True when it changes nothing (the Lumen look).
    bool Empty() const;
    const std::wstring& Name(bool russian) const { return russian && !nameRu.empty() ? nameRu : name; }
};

struct ParseResult {
    Skin skin;
    std::vector<std::string> warnings;  // one line each, English (the log is for skin authors)
    bool ok = false;                    // false: not a JSON object; `skin` is then empty
};

// Never fails on a bad value: unknown keys and bad values are skipped with a warning, so every
// other key still applies.
ParseResult Parse(std::string_view text);

// Effective colours: the brand palette for the mode, the skin's colours over it, then the accent
// family (accent.mid, .dim, .faint, on.accent) derived from the accent unless the skin sets them.
// `accentTone` is general.accent; a skin accent replaces it.
Colors Resolve(bool dark, int accentTone, const Skin* skin);

// The colours of a Windows contrast theme (GetSysColor), which replace every skin while high
// contrast is on: the user picked them for the whole system.
struct SystemColors {
    Rgba window, windowText, highlight, highlightText, hotlight, grayText, buttonFace;
};
// A one-mode skin (dark when the window colour is dark) painted only in those colours.
Skin FromSystemColors(const SystemColors& sys);

// WCAG 2.x contrast ratio of two opaque colours (alpha ignored), 1..21.
double Contrast(const Rgba& a, const Rgba& b);

// The keyboard focus ring: the accent where it stands out 3:1 on every background, else
// text.primary (the darker accents and accent.mid did not, WCAG 1.4.11).
Rgba FocusRing(const Colors& c);
inline constexpr int kFocusRingFg = kTokenCount;  // ContrastIssue::fg of the focus ring

struct ContrastIssue {
    bool dark = true;
    int fg = 0, bg = 0;  // tokens
    double ratio = 0, need = 0;
    bool icon = false;  // 3:1 rule (icons, captions, the accent) rather than 4.5:1 for text
};

// Text tokens below 4.5:1 and icon/accent tokens below 3:1 on the backgrounds they are drawn on,
// for each mode the skin is used in. Empty when everything passes.
std::vector<ContrastIssue> CheckContrast(const Skin* skin, int accentTone);
std::string Describe(const ContrastIssue& issue);  // "dark: text.secondary on bg.panel 3.9:1 (needs 4.5:1)"

}  // namespace skin

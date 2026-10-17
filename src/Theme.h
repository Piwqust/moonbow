#pragma once
#include <cstdint>
#include <d2d1.h>

// Design tokens. See PLAN.md, section 2.3 and 2.4. All sizes are in DIPs.
namespace theme {

constexpr D2D1_COLOR_F Rgb(uint32_t rgb, float a = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a};
}

inline constexpr D2D1_COLOR_F kBg            = Rgb(0x0F0F0F);
inline constexpr D2D1_COLOR_F kChrome        = Rgb(0x171717);
inline constexpr D2D1_COLOR_F kChromeOverlay = Rgb(0x171717, 0.92f);
inline constexpr D2D1_COLOR_F kLine          = Rgb(0x262626);
inline constexpr D2D1_COLOR_F kHover         = Rgb(0xFFFFFF, 0.08f);
inline constexpr D2D1_COLOR_F kPress         = Rgb(0xFFFFFF, 0.14f);
inline constexpr D2D1_COLOR_F kText          = Rgb(0xE8E8E8);
inline constexpr D2D1_COLOR_F kTextDim       = Rgb(0x8F8F8F);
inline constexpr D2D1_COLOR_F kAccent        = Rgb(0xF28C28);
inline constexpr D2D1_COLOR_F kAccentHover   = Rgb(0xF59D45);
inline constexpr D2D1_COLOR_F kAccentDim     = Rgb(0xF28C28, 0.32f);
inline constexpr D2D1_COLOR_F kOnAccent      = Rgb(0x141414);
inline constexpr D2D1_COLOR_F kTrack         = Rgb(0x2B2B2B);
inline constexpr D2D1_COLOR_F kDanger        = Rgb(0xC42B1C);

inline constexpr float kTitleH        = 32.0f;
inline constexpr float kCaptionBtnW   = 46.0f;
inline constexpr float kBarH          = 48.0f;
inline constexpr float kBtn           = 32.0f;
inline constexpr float kPlayW         = 40.0f;
inline constexpr float kPlayH         = 28.0f;
inline constexpr float kRadius        = 6.0f;
inline constexpr float kPad           = 12.0f;
inline constexpr float kGap           = 4.0f;
inline constexpr float kSeekHit       = 20.0f;
inline constexpr float kSeekTrack     = 4.0f;
inline constexpr float kSeekTrackHot  = 6.0f;
inline constexpr float kThumbW        = 4.0f;
inline constexpr float kThumbH        = 14.0f;
inline constexpr float kVolW          = 56.0f;
inline constexpr float kVolH          = 14.0f;
inline constexpr float kResizeBorder  = 6.0f;
inline constexpr float kMenuW         = 280.0f;
inline constexpr float kMenuItemH     = 28.0f;

inline constexpr float kFontUi    = 12.0f;
inline constexpr float kFontEmpty = 14.0f;
inline constexpr float kFontIcon  = 16.0f;
inline constexpr float kFontGlyphSmall = 10.0f;

// Segoe Fluent Icons / Segoe MDL2 Assets code points.
namespace glyph {
inline constexpr wchar_t kPlay[]         = L"";
inline constexpr wchar_t kPause[]        = L"";
inline constexpr wchar_t kPrev[]         = L"";
inline constexpr wchar_t kNext[]         = L"";
inline constexpr wchar_t kVolume[]       = L"";
inline constexpr wchar_t kMute[]         = L"";
inline constexpr wchar_t kFullscreen[]   = L"";
inline constexpr wchar_t kExitFull[]     = L"";
inline constexpr wchar_t kSubtitles[]    = L"";
inline constexpr wchar_t kAudio[]        = L"";
inline constexpr wchar_t kMinimize[]     = L"";
inline constexpr wchar_t kMaximize[]     = L"";
inline constexpr wchar_t kRestore[]      = L"";
inline constexpr wchar_t kClose[]        = L"";
inline constexpr wchar_t kOpenFile[]     = L"";
inline constexpr wchar_t kCheck[]        = L"";
}  // namespace glyph

}  // namespace theme

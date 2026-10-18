#include "ui/Theme.h"

#include <algorithm>

namespace ui {

namespace {

constexpr uint32_t kDarkAccents[kAccentCount] = {0xEDEDED, 0xD4D4D4, 0xB5B5B5, 0x8F8F8F, 0x6B6B6B};
constexpr uint32_t kLightAccents[kAccentCount] = {0x141414, 0x2E2E2E, 0x4A4A4A, 0x666666, 0x808080};
constexpr const wchar_t* kAccentNames[kAccentCount] = {L"White", L"Pearl", L"Silver", L"Steel", L"Graphite"};

}  // namespace

Theme& Theme::Get() {
    static Theme theme = [] {
        Theme t;
        t.Rebuild();
        return t;
    }();
    return theme;
}

void Theme::Set(ThemeMode mode, int accent) {
    accent = std::clamp(accent, 0, kAccentCount - 1);
    if (mode == mode_ && accent == accent_) return;
    mode_ = mode;
    accent_ = accent;
    Rebuild();
}

Color Theme::AccentSwatch(int index) const {
    index = std::clamp(index, 0, kAccentCount - 1);
    return Color::Hex(mode_ == ThemeMode::Dark ? kDarkAccents[index] : kLightAccents[index]);
}

const wchar_t* Theme::AccentName(int index) {
    return kAccentNames[std::clamp(index, 0, kAccentCount - 1)];
}

void Theme::Rebuild() {
    Palette& p = p_;
    if (mode_ == ThemeMode::Dark) {
        p.bgDeep = Color::Hex(0x090909);
        p.bgBase = Color::Hex(0x0E0E0E);
        p.bgRaised = Color::Hex(0x151515);
        p.bgPanel = Color::Hex(0x191919);
        p.bgChrome = Color::Hex(0x1B1B1B);
        p.bgCard = Color::Hex(0x1E1E1E);
        p.bgSelected = Color::Hex(0x242424);
        p.bgMenu = Color::Hex(0x222222);
        p.hover = Color::Hex(0xFFFFFF, 0.06f);
        p.press = Color::Hex(0xFFFFFF, 0.10f);
        p.lineSubtle = Color::Hex(0x171717);
        p.line = Color::Hex(0x262626);
        p.lineStrong = Color::Hex(0x404040);
        p.textPrimary = Color::Hex(0xEAEAEA);
        p.textSecondary = Color::Hex(0x8F8F8F);
        p.textTertiary = Color::Hex(0x6B6B6B);
        p.textDisabled = Color::Hex(0x5C5C5C);  // ~3:1 on bgBase: readable, clearly off
        p.scrim = Color::Hex(0x000000, 0.60f);
        p.shadow = Color::Hex(0x000000, 0.46f);
        p.videoBg = Color::Hex(0x000000);
    } else {
        p.bgDeep = Color::Hex(0xE6E6E6);
        p.bgBase = Color::Hex(0xF2F2F2);
        p.bgRaised = Color::Hex(0xFAFAFA);
        p.bgPanel = Color::Hex(0xFFFFFF);
        p.bgChrome = Color::Hex(0xEBEBEB);
        p.bgCard = Color::Hex(0xE4E4E4);
        p.bgSelected = Color::Hex(0xDCDCDC);
        p.bgMenu = Color::Hex(0xFFFFFF);
        p.hover = Color::Hex(0x000000, 0.05f);
        p.press = Color::Hex(0x000000, 0.09f);
        p.lineSubtle = Color::Hex(0xE3E3E3);
        p.line = Color::Hex(0xD6D6D6);
        p.lineStrong = Color::Hex(0xBDBDBD);
        p.textPrimary = Color::Hex(0x141414);
        p.textSecondary = Color::Hex(0x5C5C5C);
        p.textTertiary = Color::Hex(0x8A8A8A);
        p.textDisabled = Color::Hex(0x9A9A9A);
        p.scrim = Color::Hex(0x000000, 0.32f);
        p.shadow = Color::Hex(0x000000, 0.18f);
        p.videoBg = Color::Hex(0x000000);
    }
    p.accent = AccentSwatch(accent_);
    p.accentMid = Color::Mix(p.accent, p.bgBase, 0.25f);
    p.accentDim = Color::Mix(p.bgBase, p.accent, mode_ == ThemeMode::Dark ? 0.15f : 0.12f);
    p.accentFaint = Color::Mix(p.bgBase, p.accent, 0.07f);
    p.onAccent = p.bgBase;
    p.danger = Color::Hex(0xC42B1C);
    ++version_;
}

}  // namespace ui

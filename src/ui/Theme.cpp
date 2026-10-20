#include "ui/Theme.h"

#include <algorithm>

namespace ui {

namespace {

constexpr const wchar_t* kAccentNames[kAccentCount] = {L"White", L"Pearl", L"Silver", L"Steel", L"Graphite"};

Color ToColor(const skin::Rgba& c) {
    return {c.r, c.g, c.b, c.a};
}

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
    if (mode == requested_ && accent == accent_) return;
    requested_ = mode;
    accent_ = accent;
    Rebuild();
}

void Theme::SetSkin(std::shared_ptr<const skin::Skin> s) {
    if (s && s->Empty()) s.reset();
    skin_ = std::move(s);
    Rebuild();
}

Color Theme::AccentSwatch(int index) const {
    return ToColor(skin::AccentTone(mode_ == ThemeMode::Dark, index));
}

const wchar_t* Theme::AccentName(int index) {
    return kAccentNames[std::clamp(index, 0, kAccentCount - 1)];
}

float Theme::MapRadius(float design) const {
    for (int i = 0; i < skin::kRadiusCount; ++i)
        if (design == skin::kDesignRadii[i]) return skin_->radius[i].value_or(design);
    return design;
}

void Theme::Rebuild() {
    mode_ = requested_;
    if (skin_ && skin_->mode != skin::Mode::Any)
        mode_ = skin_->mode == skin::Mode::Dark ? ThemeMode::Dark : ThemeMode::Light;
    const skin::Colors c = skin::Resolve(mode_ == ThemeMode::Dark, accent_, skin_.get());
    Palette& p = p_;
    Color* const fields[skin::kTokenCount] = {
        &p.bgDeep, &p.bgBase, &p.bgRaised, &p.bgPanel, &p.bgChrome, &p.bgCard, &p.bgSelected, &p.bgMenu,
        &p.hover, &p.press,
        &p.lineSubtle, &p.line, &p.lineStrong, &p.lineControl,
        &p.textPrimary, &p.textSecondary, &p.textTertiary, &p.textDisabled,
        &p.accent, &p.accentMid, &p.accentDim, &p.accentFaint, &p.onAccent,
        &p.scrim, &p.shadow, &p.danger,
    };
    for (int i = 0; i < skin::kTokenCount; ++i) *fields[i] = ToColor(c[i]);
    p.videoBg = Color::Hex(0x000000);
    p.focusRing = ToColor(skin::FocusRing(c));

    radii_ = false;
    barOpacity_ = 1.0f;
    if (skin_) {
        for (const auto& r : skin_->radius) radii_ |= r.has_value();
        barOpacity_ = skin_->barOpacity.value_or(1.0f);
    }
}

}  // namespace ui

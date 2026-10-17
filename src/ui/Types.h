#pragma once
#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace ui {

inline constexpr float kInf = std::numeric_limits<float>::infinity();

struct Point {
    float x = 0, y = 0;
};

struct Size {
    float w = 0, h = 0;
};

struct Insets {
    float l = 0, t = 0, r = 0, b = 0;
    static constexpr Insets All(float v) { return {v, v, v, v}; }
    static constexpr Insets Sym(float h, float v) { return {h, v, h, v}; }
    constexpr float H() const { return l + r; }
    constexpr float V() const { return t + b; }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;

    constexpr float Right() const { return x + w; }
    constexpr float Bottom() const { return y + h; }
    constexpr float Cx() const { return x + w / 2; }
    constexpr float Cy() const { return y + h / 2; }
    constexpr bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    constexpr bool Empty() const { return w <= 0 || h <= 0; }
    constexpr Rect Inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    constexpr Rect Inset(const Insets& i) const { return {x + i.l, y + i.t, w - i.H(), h - i.V()}; }
    constexpr Rect Offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    bool Intersects(const Rect& o) const { return x < o.Right() && o.x < Right() && y < o.Bottom() && o.y < Bottom(); }
    Rect Intersect(const Rect& o) const {
        float l = std::max(x, o.x), t = std::max(y, o.y);
        float r = std::min(Right(), o.Right()), b = std::min(Bottom(), o.Bottom());
        return {l, t, std::max(0.0f, r - l), std::max(0.0f, b - t)};
    }
    // Rect of the given size centered in this one.
    constexpr Rect Center(float cw, float ch) const { return {x + (w - cw) / 2, y + (h - ch) / 2, cw, ch}; }
    D2D1_RECT_F D2D() const { return {x, y, x + w, y + h}; }
};

struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static constexpr Color Hex(uint32_t rgb, float alpha = 1.0f) {
        return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
    }
    static constexpr Color Clear() { return {0, 0, 0, 0}; }
    constexpr Color WithAlpha(float alpha) const { return {r, g, b, alpha}; }
    constexpr Color Fade(float k) const { return {r, g, b, a * k}; }
    static Color Mix(const Color& x, const Color& y, float t) {
        return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
    }
    // Packed 0xRRGGBB, used for cache keys and SVG recoloring.
    uint32_t Rgb() const {
        auto c = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
        return (c(r) << 16) | (c(g) << 8) | c(b);
    }
    D2D1_COLOR_F D2D() const { return {r, g, b, a}; }
};

}  // namespace ui

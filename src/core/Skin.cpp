#include "Skin.h"

#include "Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace skin {

namespace {

constexpr const char* kTokenNames[kTokenCount] = {
    "bg.deep", "bg.base", "bg.raised", "bg.panel", "bg.chrome", "bg.card", "bg.selected", "bg.menu",
    "bg.hover", "bg.press",
    "line.subtle", "line", "line.strong",
    "text.primary", "text.secondary", "text.tertiary", "text.disabled",
    "accent", "accent.mid", "accent.dim", "accent.faint", "on.accent",
    "scrim", "shadow", "danger",
};

constexpr const char* kRadiusNames[kRadiusCount] = {"small", "control", "card", "popup", "window"};

constexpr uint32_t kDarkAccents[kAccentTones] = {0xEDEDED, 0xD4D4D4, 0xB5B5B5, 0x8F8F8F, 0x6B6B6B};
constexpr uint32_t kLightAccents[kAccentTones] = {0x141414, 0x2E2E2E, 0x4A4A4A, 0x666666, 0x808080};

// DESIGN.md section 1. The accent family is derived in Resolve().
Colors BrandColors(bool dark) {
    Colors c{};
    if (dark) {
        c[kBgDeep] = Rgba::Hex(0x090909);
        c[kBgBase] = Rgba::Hex(0x0E0E0E);
        c[kBgRaised] = Rgba::Hex(0x151515);
        c[kBgPanel] = Rgba::Hex(0x191919);
        c[kBgChrome] = Rgba::Hex(0x1B1B1B);
        c[kBgCard] = Rgba::Hex(0x1E1E1E);
        c[kBgSelected] = Rgba::Hex(0x242424);
        c[kBgMenu] = Rgba::Hex(0x222222);
        c[kHover] = Rgba::Hex(0xFFFFFF, 0.06f);
        c[kPress] = Rgba::Hex(0xFFFFFF, 0.10f);
        c[kLineSubtle] = Rgba::Hex(0x171717);
        c[kLine] = Rgba::Hex(0x262626);
        c[kLineStrong] = Rgba::Hex(0x404040);
        c[kTextPrimary] = Rgba::Hex(0xEAEAEA);
        c[kTextSecondary] = Rgba::Hex(0x8F8F8F);
        c[kTextTertiary] = Rgba::Hex(0x6B6B6B);
        c[kTextDisabled] = Rgba::Hex(0x646464);  // ~3:1 on bgBase: readable, clearly off
        c[kScrim] = Rgba::Hex(0x000000, 0.60f);
        c[kShadow] = Rgba::Hex(0x000000, 0.46f);
    } else {
        c[kBgDeep] = Rgba::Hex(0xE6E6E6);
        c[kBgBase] = Rgba::Hex(0xF2F2F2);
        c[kBgRaised] = Rgba::Hex(0xFAFAFA);
        c[kBgPanel] = Rgba::Hex(0xFFFFFF);
        c[kBgChrome] = Rgba::Hex(0xEBEBEB);
        c[kBgCard] = Rgba::Hex(0xE4E4E4);
        c[kBgSelected] = Rgba::Hex(0xDCDCDC);
        c[kBgMenu] = Rgba::Hex(0xFFFFFF);
        c[kHover] = Rgba::Hex(0x000000, 0.05f);
        c[kPress] = Rgba::Hex(0x000000, 0.09f);
        c[kLineSubtle] = Rgba::Hex(0xE3E3E3);
        c[kLine] = Rgba::Hex(0xD6D6D6);
        c[kLineStrong] = Rgba::Hex(0xBDBDBD);
        c[kTextPrimary] = Rgba::Hex(0x141414);
        c[kTextSecondary] = Rgba::Hex(0x5C5C5C);
        c[kTextTertiary] = Rgba::Hex(0x8A8A8A);
        c[kTextDisabled] = Rgba::Hex(0x848484);  // ~3.3:1 on bgBase, ~3.7:1 on white
        c[kScrim] = Rgba::Hex(0x000000, 0.32f);
        c[kShadow] = Rgba::Hex(0x000000, 0.18f);
    }
    c[kDanger] = Rgba::Hex(0xC42B1C);
    return c;
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// "#RGB", "#RRGGBB" or "#RRGGBBAA".
std::optional<Rgba> ParseColor(const std::string& s) {
    if (s.size() < 4 || s[0] != '#') return std::nullopt;
    std::string hex = s.substr(1);
    if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
    if (hex.size() != 6 && hex.size() != 8) return std::nullopt;
    uint32_t v = 0;
    for (char ch : hex) {
        const int d = HexDigit(ch);
        if (d < 0) return std::nullopt;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    if (hex.size() == 6) return Rgba::Hex(v);
    return Rgba::Hex(v >> 8, static_cast<float>(v & 0xFF) / 255.0f);
}

std::string Quote(const json::Value& v) {
    std::string s = json::Serialize(v);
    if (s.size() > 40) s = s.substr(0, 37) + "...";
    return s;
}

bool Ignored(const std::string& key) {
    return !key.empty() && (key[0] == '_' || key[0] == '$');  // comments, "$schema"
}

void ParseColors(const json::Value& obj, const std::string& path, std::array<std::optional<Rgba>, kTokenCount>* a,
                 std::array<std::optional<Rgba>, kTokenCount>* b, std::vector<std::string>* warnings) {
    if (!obj.IsObject()) {
        warnings->push_back(path + ": expected an object of colour tokens; ignored");
        return;
    }
    for (const json::Member& m : obj.AsObject()) {
        if (Ignored(m.key)) continue;
        const std::string where = path + "." + m.key;
        const int token = FindToken(m.key);
        if (token < 0) {
            warnings->push_back(where + ": unknown colour token; ignored");
            continue;
        }
        std::optional<Rgba> c = m.value.IsString() ? ParseColor(m.value.AsString()) : std::nullopt;
        if (!c) {
            warnings->push_back(where + ": " + Quote(m.value) + " is not a colour (#RRGGBB); the default is kept");
            continue;
        }
        if (c->a < 1.0f && !TokenAllowsAlpha(token)) {
            warnings->push_back(where + ": this token is always opaque; the alpha is ignored");
            c->a = 1.0f;
        }
        if (a) (*a)[token] = c;
        if (b) (*b)[token] = c;
    }
}

std::wstring Wide(const std::string& s) {
    return json::ToWide(s);
}

double Luminance(const Rgba& c) {
    auto lin = [](float v) {
        const double d = std::clamp(v, 0.0f, 1.0f);
        return d <= 0.04045 ? d / 12.92 : std::pow((d + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

}  // namespace

uint32_t Rgba::Rgb() const {
    auto c = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

Rgba Mix(const Rgba& x, const Rgba& y, float t) {
    return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
}

const char* TokenName(int token) {
    return token >= 0 && token < kTokenCount ? kTokenNames[token] : "";
}

int FindToken(std::string_view name) {
    for (int i = 0; i < kTokenCount; ++i)
        if (name == kTokenNames[i]) return i;
    return -1;
}

bool TokenAllowsAlpha(int token) {
    return token == kHover || token == kPress || token == kScrim || token == kShadow;
}

Rgba AccentTone(bool dark, int index) {
    index = std::clamp(index, 0, kAccentTones - 1);
    return Rgba::Hex(dark ? kDarkAccents[index] : kLightAccents[index]);
}

const char* RadiusName(int radius) {
    return radius >= 0 && radius < kRadiusCount ? kRadiusNames[radius] : "";
}

bool Skin::Empty() const {
    for (int i = 0; i < kTokenCount; ++i)
        if (dark[i] || light[i]) return false;
    for (const auto& r : radius)
        if (r) return false;
    return mode == Mode::Any && !barOpacity;
}

ParseResult Parse(std::string_view text) {
    ParseResult out;
    std::string error;
    std::optional<json::Value> doc = json::Parse(text, &error);
    if (!doc) {
        out.warnings.push_back("not valid JSON (" + error + "); the skin is not used");
        return out;
    }
    if (!doc->IsObject()) {
        out.warnings.push_back("the file must hold a JSON object; the skin is not used");
        return out;
    }
    out.ok = true;
    Skin& s = out.skin;
    std::vector<std::string>& w = out.warnings;
    // "colors" applies to both modes; "dark" and "light" come after it whatever the key order.
    const json::Value* both = doc->Find("colors");
    if (both) ParseColors(*both, "colors", &s.dark, &s.light, &w);
    for (const json::Member& m : doc->AsObject()) {
        const std::string& k = m.key;
        const json::Value& v = m.value;
        if (Ignored(k) || k == "colors") continue;
        if (k == "lumenSkin") {
            if (!v.IsNumber() || v.AsNumber() != 1) w.push_back("lumenSkin: this Lumen reads version 1; trying anyway");
        } else if (k == "name") {
            if (v.IsString()) {
                s.name = Wide(v.AsString());
            } else if (v.IsObject()) {
                s.name = Wide(v.GetString("en"));
                s.nameRu = Wide(v.GetString("ru"));
                if (s.name.empty()) s.name = s.nameRu;
            } else {
                w.push_back("name: expected a string or {\"en\": ..., \"ru\": ...}; ignored");
            }
        } else if (k == "mode") {
            const std::string mode = v.AsString();
            if (mode == "dark") s.mode = Mode::Dark;
            else if (mode == "light") s.mode = Mode::Light;
            else if (mode != "any") w.push_back("mode: " + Quote(v) + " is not \"any\", \"dark\" or \"light\"; using \"any\"");
        } else if (k == "dark") {
            ParseColors(v, "dark", &s.dark, nullptr, &w);
        } else if (k == "light") {
            ParseColors(v, "light", nullptr, &s.light, &w);
        } else if (k == "radius") {
            if (!v.IsObject()) {
                w.push_back("radius: expected an object; ignored");
                continue;
            }
            for (const json::Member& r : v.AsObject()) {
                if (Ignored(r.key)) continue;
                int idx = -1;
                for (int i = 0; i < kRadiusCount; ++i)
                    if (r.key == kRadiusNames[i]) idx = i;
                if (idx < 0) {
                    w.push_back("radius." + r.key + ": unknown radius; ignored");
                    continue;
                }
                const double d = r.value.AsNumber(-1);
                if (!r.value.IsNumber() || !(d >= 0 && d <= kMaxRadius)) {
                    w.push_back("radius." + r.key + ": " + Quote(r.value) + " is not a number from 0 to 24; the default is kept");
                    continue;
                }
                s.radius[idx] = static_cast<float>(d);
            }
        } else if (k == "controlBar") {
            if (!v.IsObject()) {
                w.push_back("controlBar: expected an object; ignored");
                continue;
            }
            for (const json::Member& b : v.AsObject()) {
                if (Ignored(b.key)) continue;
                if (b.key != "opacity") {
                    w.push_back("controlBar." + b.key + ": unknown key; ignored");
                    continue;
                }
                const double d = b.value.AsNumber(-1);
                if (!b.value.IsNumber() || !(d >= kMinBarOpacity && d <= 1.0)) {
                    w.push_back("controlBar.opacity: " + Quote(b.value) + " is not a number from 0.5 to 1; the bar stays opaque");
                    continue;
                }
                s.barOpacity = static_cast<float>(d);
            }
        } else if (k == "font" || k == "fonts") {
            w.push_back(k + ": fonts cannot be changed by a skin yet; Inter is kept");
        } else {
            w.push_back(k + ": unknown key; ignored");
        }
    }
    return out;
}

Colors Resolve(bool dark, int accentTone, const Skin* skin) {
    Colors c = BrandColors(dark);
    const std::array<std::optional<Rgba>, kTokenCount>* over = nullptr;
    if (skin) over = dark ? &skin->dark : &skin->light;
    if (over)
        for (int i = 0; i < kTokenCount; ++i)
            if ((*over)[i]) c[i] = *(*over)[i];
    auto derived = [&](int token, const Rgba& value) {
        if (!over || !(*over)[token]) c[token] = value;
    };
    derived(kAccent, AccentTone(dark, accentTone));
    derived(kAccentMid, Mix(c[kAccent], c[kBgBase], 0.25f));
    derived(kAccentDim, Mix(c[kBgBase], c[kAccent], dark ? 0.15f : 0.12f));
    derived(kAccentFaint, Mix(c[kBgBase], c[kAccent], 0.07f));
    derived(kOnAccent, c[kBgBase]);
    return c;
}

double Contrast(const Rgba& a, const Rgba& b) {
    const double la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

std::vector<ContrastIssue> CheckContrast(const Skin* skin, int accentTone) {
    struct Pair {
        int fg;
        std::vector<int> bgs;
        double need;
    };
    // Where each foreground is drawn. text.disabled is exempt (WCAG excludes inactive controls);
    // text.tertiary marks captions and inactive items and is held to the 3:1 of non-text.
    static const Pair kPairs[] = {
        {kTextPrimary, {kBgBase, kBgDeep, kBgRaised, kBgPanel, kBgChrome, kBgCard, kBgSelected, kBgMenu}, 4.5},
        {kTextSecondary, {kBgBase, kBgDeep, kBgRaised, kBgPanel, kBgChrome, kBgCard, kBgSelected, kBgMenu}, 4.5},
        {kTextTertiary, {kBgBase, kBgPanel}, 3.0},
        {kAccent, {kBgBase, kBgDeep, kBgChrome}, 3.0},
        {kOnAccent, {kAccent}, 3.0},
    };
    std::vector<ContrastIssue> out;
    for (int m = 0; m < 2; ++m) {
        const bool dark = m == 0;
        if (skin && skin->mode == (dark ? Mode::Light : Mode::Dark)) continue;
        const Colors c = Resolve(dark, accentTone, skin);
        for (const Pair& p : kPairs)
            for (int bg : p.bgs) {
                const double r = Contrast(c[p.fg], c[bg]);
                if (r < p.need) out.push_back({dark, p.fg, bg, r, p.need, p.need < 4.5});
            }
    }
    return out;
}

std::string Describe(const ContrastIssue& issue) {
    char buf[160];
    // Rounded down, so 4.497 reads "4.4:1" next to "needs 4.5:1" rather than "4.5:1".
    std::snprintf(buf, sizeof(buf), "%s: %s on %s %.1f:1 (needs %.1f:1)", issue.dark ? "dark" : "light",
                  TokenName(issue.fg), TokenName(issue.bg), std::floor(issue.ratio * 10) / 10, issue.need);
    return buf;
}

}  // namespace skin

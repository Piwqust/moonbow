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
    "line.subtle", "line", "line.strong", "line.control",
    "text.primary", "text.secondary", "text.tertiary", "text.disabled",
    "accent", "accent.mid", "accent.dim", "accent.faint", "on.accent",
    "scrim", "shadow", "danger",
};

constexpr const char* kRadiusNames[kRadiusCount] = {"small", "control", "card", "popup", "window"};

// Even steps; the last one still reads as text (4.5:1) on bg.base and bg.panel: rows mark the playing
// item with accent-coloured text.
constexpr uint32_t kDarkAccents[kAccentTones] = {0xEDEDED, 0xD2D2D2, 0xB7B7B7, 0x9C9C9C, 0x828282};
constexpr uint32_t kLightAccents[kAccentTones] = {0x141414, 0x2A2A2A, 0x404040, 0x565656, 0x6C6C6C};

// DESIGN.md section 1. The accent family is derived in Resolve().
Colors BrandColors(bool dark) {
    Colors c{};
    if (dark) {
        c[kBgDeep] = Rgba::Hex(0x08090C);
        c[kBgBase] = Rgba::Hex(0x0B0C10);
        c[kBgRaised] = Rgba::Hex(0x131519);
        c[kBgPanel] = Rgba::Hex(0x17191E);
        c[kBgChrome] = Rgba::Hex(0x191B20);
        c[kBgCard] = Rgba::Hex(0x1C1E24);
        c[kBgSelected] = Rgba::Hex(0x22252B);
        c[kBgMenu] = Rgba::Hex(0x202329);
        c[kHover] = Rgba::Hex(0xFFFFFF, 0.06f);
        c[kPress] = Rgba::Hex(0xFFFFFF, 0.10f);
        c[kLineSubtle] = Rgba::Hex(0x15171C);
        c[kLine] = Rgba::Hex(0x23262E);
        c[kLineStrong] = Rgba::Hex(0x3D4048);
        c[kLineControl] = Rgba::Hex(0x6F737B);  // >= 3:1 on every background a field sits on
        c[kTextPrimary] = Rgba::Hex(0xEEF1F6);
        c[kTextSecondary] = Rgba::Hex(0xA7ABB3);
        c[kTextTertiary] = Rgba::Hex(0x8E939C);
        c[kTextDisabled] = Rgba::Hex(0x63676F);  // ~3:1 on bgBase: readable, clearly off
        c[kScrim] = Rgba::Hex(0x000000, 0.60f);
        c[kShadow] = Rgba::Hex(0x000000, 0.46f);
    } else {
        c[kBgDeep] = Rgba::Hex(0xE7E6E1);
        c[kBgBase] = Rgba::Hex(0xF4F3EF);
        c[kBgRaised] = Rgba::Hex(0xFAF9F6);
        c[kBgPanel] = Rgba::Hex(0xFFFFFF);
        c[kBgChrome] = Rgba::Hex(0xECEBE6);
        c[kBgCard] = Rgba::Hex(0xE5E4DF);
        c[kBgSelected] = Rgba::Hex(0xDDDCD7);
        c[kBgMenu] = Rgba::Hex(0xFFFFFF);
        c[kHover] = Rgba::Hex(0x000000, 0.05f);
        c[kPress] = Rgba::Hex(0x000000, 0.09f);
        c[kLineSubtle] = Rgba::Hex(0xE4E3DE);
        c[kLine] = Rgba::Hex(0xD7D6D1);
        c[kLineStrong] = Rgba::Hex(0xBEBDB8);
        c[kLineControl] = Rgba::Hex(0x7E7D79);
        c[kTextPrimary] = Rgba::Hex(0x121317);
        c[kTextSecondary] = Rgba::Hex(0x404349);
        c[kTextTertiary] = Rgba::Hex(0x5C5F65);
        c[kTextDisabled] = Rgba::Hex(0x83858B);  // ~3.3:1 on bgBase, ~3.7:1 on white
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
        if (k == "moonbowSkin" || k == "lumenSkin") {  // lumenSkin: skins written before the rename
            if (!v.IsNumber() || v.AsNumber() != 1) w.push_back(k + ": this Moonbow reads version 1; trying anyway");
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
            w.push_back(k + ": fonts cannot be changed by a skin yet; Onest is kept");
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

Skin FromSystemColors(const SystemColors& sys) {
    Skin s;
    s.name = L"Windows contrast theme";
    s.nameRu = L"Контрастная тема Windows";
    const bool dark = Luminance(sys.window) < 0.5;
    s.mode = dark ? Mode::Dark : Mode::Light;
    auto& c = dark ? s.dark : s.light;
    auto opaque = [](Rgba x) {
        x.a = 1;
        return x;
    };
    const Rgba window = opaque(sys.window), text = opaque(sys.windowText), gray = opaque(sys.grayText);
    for (int t : {kBgDeep, kBgBase, kBgRaised, kBgPanel, kBgMenu}) c[t] = window;
    c[kBgChrome] = c[kBgCard] = opaque(sys.buttonFace);
    c[kBgSelected] = Mix(window, text, 0.2f);  // a step the theme's text still reads on
    c[kHover] = Rgba{text.r, text.g, text.b, 0.14f};
    c[kPress] = Rgba{text.r, text.g, text.b, 0.24f};
    c[kLineSubtle] = c[kLine] = gray;
    c[kLineStrong] = c[kLineControl] = text;
    c[kTextPrimary] = c[kTextSecondary] = c[kTextTertiary] = text;  // no dimmed greys in a contrast theme
    c[kTextDisabled] = gray;
    c[kAccent] = opaque(sys.highlight);
    c[kOnAccent] = opaque(sys.highlightText);
    c[kAccentMid] = opaque(sys.hotlight);  // focus rings and selection borders, like links
    return s;
}

double Contrast(const Rgba& a, const Rgba& b) {
    const double la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

namespace {
constexpr int kBackgrounds[] = {kBgBase, kBgDeep, kBgRaised, kBgPanel, kBgChrome, kBgCard, kBgSelected, kBgMenu};
}  // namespace

Rgba FocusRing(const Colors& c) {
    for (int bg : kBackgrounds)
        if (Contrast(c[kAccent], c[bg]) < 3.0) return c[kTextPrimary];
    return c[kAccent];
}

std::vector<ContrastIssue> CheckContrast(const Skin* skin, int accentTone) {
    struct Pair {
        int fg;
        std::vector<int> bgs;
        double need;
    };
    // Where each foreground is drawn. text.disabled is exempt (WCAG excludes inactive controls).
    // text.tertiary is small text too (captions, "6:00 left", menu shortcuts, placeholders).
    static const Pair kPairs[] = {
        {kTextPrimary, {kBgBase, kBgDeep, kBgRaised, kBgPanel, kBgChrome, kBgCard, kBgSelected, kBgMenu}, 4.5},
        {kTextSecondary, {kBgBase, kBgDeep, kBgRaised, kBgPanel, kBgChrome, kBgCard, kBgSelected, kBgMenu}, 4.5},
        {kTextTertiary, {kBgBase, kBgDeep, kBgRaised, kBgPanel, kBgChrome, kBgCard, kBgSelected, kBgMenu}, 4.5},
        {kAccent, {kBgBase, kBgDeep, kBgChrome}, 3.0},
        {kAccent, {kBgBase, kBgPanel}, 4.5},  // as text: the playing row, "Press a key…"
        {kOnAccent, {kAccent}, 4.5},          // button and switch labels
        // Edges of fields, sliders, checkboxes and switches (WCAG 1.4.11).
        {kLineControl, {kBgBase, kBgDeep, kBgRaised, kBgPanel, kBgChrome, kBgMenu}, 3.0},
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
        // The ring is drawn around controls on any of them.
        const Rgba ring = FocusRing(c);
        for (int bg : kBackgrounds) {
            const double r = Contrast(ring, c[bg]);
            if (r < 3.0) out.push_back({dark, kFocusRingFg, bg, r, 3.0, true});
        }
    }
    return out;
}

std::string Describe(const ContrastIssue& issue) {
    char buf[160];
    // Rounded down, so 4.497 reads "4.4:1" next to "needs 4.5:1" rather than "4.5:1".
    std::snprintf(buf, sizeof(buf), "%s: %s on %s %.1f:1 (needs %.1f:1)", issue.dark ? "dark" : "light",
                  issue.fg == kFocusRingFg ? "focus ring" : TokenName(issue.fg), TokenName(issue.bg),
                  std::floor(issue.ratio * 10) / 10, issue.need);
    return buf;
}

}  // namespace skin

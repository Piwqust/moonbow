#include "app/player/SubStyle.h"

#include "core/Config.h"
#include "core/SettingsSchema.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace app {

namespace {

ui::Color HexColor(const std::string& s, uint32_t fallback, float alpha) {
    uint32_t v = fallback;
    if (s.size() == 7 && s[0] == '#') {
        uint32_t parsed = 0;
        bool ok = true;
        for (size_t i = 1; i < 7 && ok; ++i) {
            const char c = s[i];
            parsed <<= 4;
            if (c >= '0' && c <= '9') parsed |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') parsed |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') parsed |= static_cast<uint32_t>(c - 'A' + 10);
            else ok = false;
        }
        if (ok) v = parsed;
    }
    return ui::Color::Hex(v, std::clamp(alpha, 0.0f, 1.0f));
}

// The mpv value of a choice ("3" for outline "normal").
double ChoiceNumber(const char* key, const std::string& value, double fallback) {
    const settings::Item* it = settings::FindItem(key);
    if (!it) return fallback;
    const settings::Choice* choice = it->FindChoice(value);
    if (!choice || choice->mpv.empty()) return fallback;
    return std::atof(choice->mpv.c_str());
}

// mpv's generic families as libass resolves them on Windows.
std::wstring ResolveFont(std::wstring font) {
    if (font.empty() || _wcsicmp(font.c_str(), L"sans-serif") == 0) return L"Arial";
    if (_wcsicmp(font.c_str(), L"serif") == 0) return L"Times New Roman";
    if (_wcsicmp(font.c_str(), L"monospace") == 0) return L"Consolas";
    return font;
}

// "Inter SemiBold" is how libass finds that face; DirectWrite knows it as Inter at 600.
void SplitWeight(SubtitleStyle& s) {
    static const std::pair<const wchar_t*, int> kWeights[] = {
        {L" Light", 300}, {L" Medium", 500}, {L" SemiBold", 600}, {L" Semibold", 600}, {L" Bold", 700}};
    s.weight = s.bold ? 700 : 400;
    for (const auto& [suffix, w] : kWeights) {
        const size_t n = std::wcslen(suffix);
        if (s.font.size() > n && _wcsicmp(s.font.c_str() + s.font.size() - n, suffix) == 0) {
            s.font.resize(s.font.size() - n);
            s.weight = s.bold ? std::max(w, 700) : w;
            return;
        }
    }
}

std::wstring Widen(const char* s) {
    return std::wstring(s, s + std::strlen(s));  // the preset fonts are ASCII
}

}  // namespace

float SubtitleFontSize(const Config& c) {
    const float size = static_cast<float>(ChoiceNumber("subtitles.size", c.GetString("subtitles.size"), 38));
    return size > 0 ? size : 38;
}

SubtitleStyle ReadSubtitleStyle(const Config& c) {
    SubtitleStyle s;
    s.font = ResolveFont(c.GetWString("subtitles.font"));
    s.size = SubtitleFontSize(c);
    s.bold = c.GetBool("subtitles.bold");
    s.text = HexColor(c.GetString("subtitles.color"), 0xFFFFFF, static_cast<float>(c.GetDouble("subtitles.opacity") / 100.0));
    s.outlineColor = HexColor(c.GetString("subtitles.outlineColor"), 0x000000, 1);
    s.back = HexColor(c.GetString("subtitles.backColor"), 0x000000,
                      static_cast<float>(c.GetDouble("subtitles.backOpacity") / 100.0));
    s.outline = static_cast<float>(ChoiceNumber("subtitles.outline", c.GetString("subtitles.outline"), 3));
    s.shadow = c.GetBool("subtitles.shadow");
    s.background = c.GetString("subtitles.background");
    s.blur = static_cast<float>(c.GetDouble("subtitles.blur"));
    SplitWeight(s);
    return s;
}

const std::vector<SubtitlePreset>& SubtitlePresets() {
    // Inter is bundled (Player.cpp puts it in mpv's fonts folder); the others come with Windows.
    // The shadow takes the box color in mpv, so it is set too.
    static const std::vector<SubtitlePreset> presets = {
        // The player's font (Inter SemiBold), a light outline softened a little
        // and a shadow: crisp on dark and bright scenes alike, as DCMP and Netflix ask.
        {"moonbow", L"Moonbow", "Inter SemiBold", false, "#FFFFFF", "thin", "#000000", true, "none", "#000000", 60, 0.5},
        // White on a translucent box in the player's night color, like its on-screen messages
        // (Apple's "Transparent Background").
        {"box", L"Box", "Inter Medium", false, "#FFFFFF", "thin", "#0F121A", false, "box", "#0F121A", 70, 0},
        // mpv's own look.
        {"classic", L"Classic", "", false, "#FFFFFF", "normal", "#000000", true, "none", "#000000", 60, 0},
        // The DVD yellow.
        {"yellow", L"Yellow", "", true, "#FFE14D", "normal", "#000000", true, "none", "#000000", 60, 0},
        // No hard edge: a wide blurred outline reads as a soft dark halo.
        {"soft", L"Soft", "Inter Medium", false, "#FFFFFF", "thick", "#000000", false, "none", "#000000", 60, 2.5},
        // A book face in warm white.
        {"serif", L"Serif", "Georgia", false, "#F6F1E7", "thin", "#000000", true, "none", "#000000", 60, 0.5},
    };
    return presets;
}

const SubtitlePreset* MatchSubtitlePreset(const Config& c) {
    const std::wstring font = c.GetWString("subtitles.font");
    auto same = [](double a, double b) { return std::abs(a - b) < 0.01; };
    for (const SubtitlePreset& p : SubtitlePresets()) {
        const bool fontMatch = *p.font ? _wcsicmp(font.c_str(), Widen(p.font).c_str()) == 0
                                       : (font.empty() || _wcsicmp(font.c_str(), L"sans-serif") == 0);
        if (fontMatch && c.GetBool("subtitles.bold") == p.bold && _stricmp(c.GetString("subtitles.color").c_str(), p.color) == 0 &&
            same(c.GetDouble("subtitles.opacity"), 100) && c.GetString("subtitles.outline") == p.outline &&
            _stricmp(c.GetString("subtitles.outlineColor").c_str(), p.outlineColor) == 0 &&
            c.GetBool("subtitles.shadow") == p.shadow && c.GetString("subtitles.background") == p.background &&
            _stricmp(c.GetString("subtitles.backColor").c_str(), p.backColor) == 0 &&
            same(c.GetDouble("subtitles.backOpacity"), p.backOpacity) && same(c.GetDouble("subtitles.blur"), p.blur))
            return &p;
    }
    return nullptr;
}

void ApplySubtitlePreset(Config& c, const SubtitlePreset& p) {
    c.SetString("subtitles.font", p.font);
    c.SetBool("subtitles.bold", p.bold);
    c.SetString("subtitles.color", p.color);
    c.SetDouble("subtitles.opacity", 100);
    c.SetString("subtitles.outline", p.outline);
    c.SetString("subtitles.outlineColor", p.outlineColor);
    c.SetBool("subtitles.shadow", p.shadow);
    c.SetString("subtitles.background", p.background);
    c.SetString("subtitles.backColor", p.backColor);
    c.SetDouble("subtitles.backOpacity", p.backOpacity);
    c.SetDouble("subtitles.blur", p.blur);
}

SubtitleStyle PresetSubtitleStyle(const Config& c, const SubtitlePreset& p) {
    SubtitleStyle s;
    s.font = ResolveFont(Widen(p.font));
    s.size = SubtitleFontSize(c);
    s.bold = p.bold;
    s.text = HexColor(p.color, 0xFFFFFF, 1);
    s.outlineColor = HexColor(p.outlineColor, 0x000000, 1);
    s.back = HexColor(p.backColor, 0x000000, static_cast<float>(p.backOpacity / 100.0));
    s.outline = static_cast<float>(ChoiceNumber("subtitles.outline", p.outline, 3));
    s.shadow = p.shadow;
    s.background = p.background;
    s.blur = static_cast<float>(p.blur);
    SplitWeight(s);
    return s;
}

}  // namespace app

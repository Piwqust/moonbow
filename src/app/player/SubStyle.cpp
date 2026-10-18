#include "app/player/SubStyle.h"

#include "core/Config.h"
#include "core/SettingsSchema.h"

#include <algorithm>
#include <cstdlib>
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

// The mpv value of a choice setting ("3" for outline "normal").
double ChoiceNumber(const Config& c, const char* key, double fallback) {
    const settings::Item* it = settings::FindItem(key);
    if (!it) return fallback;
    const settings::Choice* choice = it->FindChoice(c.GetString(key));
    if (!choice || choice->mpv.empty()) return fallback;
    return std::atof(choice->mpv.c_str());
}

}  // namespace

float SubtitleFontSize(const Config& c) {
    const float size = static_cast<float>(ChoiceNumber(c, "subtitles.size", 38));
    return size > 0 ? size : 38;
}

SubtitleStyle ReadSubtitleStyle(const Config& c) {
    SubtitleStyle s;
    s.font = c.GetWString("subtitles.font");
    if (s.font.empty() || _wcsicmp(s.font.c_str(), L"sans-serif") == 0) s.font = L"Arial";
    else if (_wcsicmp(s.font.c_str(), L"serif") == 0) s.font = L"Times New Roman";
    else if (_wcsicmp(s.font.c_str(), L"monospace") == 0) s.font = L"Consolas";
    s.size = SubtitleFontSize(c);
    s.bold = c.GetBool("subtitles.bold");
    s.text = HexColor(c.GetString("subtitles.color"), 0xFFFFFF, static_cast<float>(c.GetDouble("subtitles.opacity") / 100.0));
    s.outlineColor = HexColor(c.GetString("subtitles.outlineColor"), 0x000000, 1);
    s.back = HexColor(c.GetString("subtitles.backColor"), 0x000000,
                      static_cast<float>(c.GetDouble("subtitles.backOpacity") / 100.0));
    s.outline = static_cast<float>(ChoiceNumber(c, "subtitles.outline", 3));
    s.shadow = c.GetBool("subtitles.shadow");
    s.background = c.GetString("subtitles.background");
    return s;
}

}  // namespace app

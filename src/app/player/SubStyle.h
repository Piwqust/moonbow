#pragma once
// The Moonbow subtitle style of Settings > Subtitles as Direct2D draws it: the word overlay
// (SubWords.cpp), the Settings preview and the player's subtitle band read it here, once.
#include "ui/Types.h"

#include <string>
#include <vector>

class Config;

namespace app {

struct SubtitleStyle {
    // mpv's generic families resolved as libass does on Windows, as a DirectWrite family: a weight
    // in the name ("Inter SemiBold", which libass matches by its legacy family) moves to `weight`.
    std::wstring font;
    int weight = 400;
    float size = 38;    // mpv's sub-font-size: line height in pixels of a 720-line frame
    bool bold = false;
    ui::Color text, outlineColor, back;  // back: the box, and in the other styles mpv's shadow color
    float outline = 3;
    bool shadow = true;
    std::string background;  // none / lines / box
    float blur = 0;          // sub-blur
};

// subtitles.size in mpv pixels: 38, mpv's default, for "Automatic".
float SubtitleFontSize(const Config& c);
SubtitleStyle ReadSubtitleStyle(const Config& c);

// A whole look for text subtitles: every style setting but the size and the position, which
// stay as the person set them. Picked in Settings > Subtitles and in the player's Subtitles panel.
struct SubtitlePreset {
    const char* id;
    const wchar_t* name;  // English; Tr()
    const char* font;     // "" = the default sans-serif
    bool bold;
    const char* color;
    const char* outline;  // a subtitles.outline choice
    const char* outlineColor;
    bool shadow;
    const char* background;  // a subtitles.background choice
    const char* backColor;
    double backOpacity;
    double blur;
};

// The first one is the default style of the schema.
const std::vector<SubtitlePreset>& SubtitlePresets();
// The preset the style settings match; nullptr for a style of one's own.
const SubtitlePreset* MatchSubtitlePreset(const Config& c);
void ApplySubtitlePreset(Config& c, const SubtitlePreset& p);
// How the preset looks at the current size.
SubtitleStyle PresetSubtitleStyle(const Config& c, const SubtitlePreset& p);

}  // namespace app

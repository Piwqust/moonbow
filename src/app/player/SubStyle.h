#pragma once
// The Moonbow subtitle style of Settings > Subtitles as Direct2D draws it: the word overlay
// (SubWords.cpp), the Settings preview and the player's subtitle band read it here, once.
#include "ui/Types.h"

#include <string>

class Config;

namespace app {

struct SubtitleStyle {
    std::wstring font;  // mpv's generic families resolved as libass does on Windows
    float size = 38;    // mpv's sub-font-size: line height in pixels of a 720-line frame
    bool bold = false;
    ui::Color text, outlineColor, back;
    float outline = 3;
    bool shadow = true;
    std::string background;  // none / lines / box
};

// subtitles.size in mpv pixels: 38, mpv's default, for "Automatic".
float SubtitleFontSize(const Config& c);
SubtitleStyle ReadSubtitleStyle(const Config& c);

}  // namespace app

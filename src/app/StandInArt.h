#pragma once
// The monochrome stand-in for a song or album without artwork: record-like grooves seeded by
// the file or album, a label and a note. The music view and the library draw the same art, so
// a track looks the same on its card and in the player.
#include "ui/Painter.h"

#include <cstdint>
#include <string>

namespace app {

struct StandInTones {
    ui::Color ground;  // the square
    ui::Color ink;     // grooves, ring and label are this colour at low alpha
    ui::Color note;    // the glyph on the label
    ui::Color border = ui::Color::Clear();
};

uint32_t ArtSeed(const std::wstring& s);
void PaintStandInArt(ui::Painter& p, const ui::Rect& r, float radius, uint32_t seed, const StandInTones& tones);

}  // namespace app

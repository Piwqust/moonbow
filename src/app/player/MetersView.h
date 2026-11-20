#pragma once
// The audio meters: Moonbow's own sound measured like on a studio desk, in the player's cards:
// VU meters, EBU R128 loudness, level bars with clip lights, a goniometer with phase correlation
// and balance, and the spectrum. Each card also lives alone in a window of its own
// (app/player/DetachedWindows.h); all of them draw from the shared app::AudioFeed.

namespace ui {
class Widget;
}

namespace app {

// All the cards in a grid (the "Audio meters" window); right click chooses which show.
ui::Widget* AddMetersView(ui::Widget* parent);

enum class MeterCard { Vu, VuLeft, VuRight, Loudness, Levels, Stereo, Spectrum };
// One card, to fill a window by itself.
ui::Widget* AddMeterCard(ui::Widget* parent, MeterCard card);

// The reset button (peaks, clip lights, loudness) for a window's caption row.
void AddMetersTools(ui::Widget* header);

}  // namespace app

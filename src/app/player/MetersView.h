#pragma once
// The audio meters window's content (Settings > Interface > Separate windows): Moonbow's own sound
// measured like on a studio desk, in the player's cards: VU meters, EBU R128 loudness, level bars
// with clip lights, a goniometer with phase correlation and balance, and the spectrum.

namespace ui {
class Widget;
}

namespace app {

// Adds the meters to `parent` and returns them.
ui::Widget* AddMetersView(ui::Widget* parent);
// The reset button (peaks, clip lights, loudness) for the window's caption row; `view` from AddMetersView.
void AddMetersTools(ui::Widget* header, ui::Widget* view);

}  // namespace app

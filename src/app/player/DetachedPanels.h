#pragma once
// The player's parts that live in windows of their own (app/player/DetachedWindows.h) besides the
// audio meters (MetersView.h): each is a copy, the main window keeps its own.

#include <functional>
#include <string>

namespace ui {
class Widget;
}

namespace app {

// What plays, the seek bar, the buttons and the volume. `caption` gets what plays, for the title.
ui::Widget* AddControlsPanel(ui::Widget* parent, std::function<void(const std::wstring&)> caption);
// The seek bar (the song's wave for music) with the time on both sides.
ui::Widget* AddSeekPanel(ui::Widget* parent);
// The cover of what plays (the stand-in art without one); `withText` adds the title and artist.
ui::Widget* AddCoverPanel(ui::Widget* parent, bool withText);
// The music view's visualization: bars, a scrolling wave or the oscilloscope (chosen in its menu).
ui::Widget* AddVizPanel(ui::Widget* parent);
// The song's lyrics, the sung line in the middle; a click on a line goes there.
ui::Widget* AddLyricsPanel(ui::Widget* parent);

}  // namespace app

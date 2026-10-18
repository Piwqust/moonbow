#pragma once
#include "ui/Painter.h"

#include <string>

namespace app {

// Frame previews for the seek bar tooltip. A worker thread keeps one paused libmpv instance
// (like mpv's thumbfast) that renders frames of the hovered time into a 192×108 buffer with the
// software render API: keyframes while the cursor moves, the exact frame once it rests.
// Paints the preview above the time tooltip of `track`, clamped into `bounds` (the window);
// returns its height or 0.
float PaintSeekPreview(ui::Painter& p, const ui::Rect& track, float x, double seconds, const ui::Rect& bounds);
// Left edge of a box `w` wide centred on `x`: kept over `track` while the track is wider than the
// box, else inside `bounds` 8 DIP from its edges (small windows).
float ClampedLeft(float x, float w, const ui::Rect& track, const ui::Rect& bounds);
// Stops the worker (at exit).
void ShutdownSeekPreview();

}  // namespace app

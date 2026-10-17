#pragma once
#include "ui/Painter.h"

#include <string>

namespace app {

// Frame previews for the seek bar tooltip. A worker thread decodes single
// frames (192×108, letterboxed) with a separate, short-lived libmpv encoder
// instance — like mpv's thumbfast — and only for the latest hovered time.
// Paint the preview above `anchor` (the time tooltip); returns its height or 0.
float PaintSeekPreview(ui::Painter& p, const ui::Rect& track, float x, double seconds);
// Stops the worker (at exit).
void ShutdownSeekPreview();

}  // namespace app

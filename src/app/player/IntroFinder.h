#pragma once
// Intros found by sound (playback.findIntros, on by default): for an episode without an intro
// chapter, the opening minutes of it and of a neighbouring episode (the next one, else the one
// before) are decoded by a helper mpv on a worker, fingerprinted and compared
// (audio/IntroPrint.h). The stretch they share is the intro of both, and "Skip intro" offers it.
//
// - Everything stays on this computer. Results are kept in <data>\intros.json (per file, with its
//   size, so a replaced file is looked at again); "no intro" is kept too.
// - One comparison at a time, at below-normal priority; a file's print is reused for the next
//   episode's comparison.
//
// UI thread only (the worker is internal).
#include <optional>
#include <string>

namespace app::intros {

struct Range {
    double start = 0, end = 0;  // seconds
};

void Init();
void Shutdown();  // at exit, while libmpv is still there: stops the worker
// The intro of a local episode, or nothing (none, not known yet, or the feature is off). The
// first ask starts the work; the skip button asks again as playback goes on.
std::optional<Range> Find(const std::wstring& path);

}  // namespace app::intros

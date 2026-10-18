#pragma once
#include "app/Actions.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Persistence of keyboard shortcuts edited in Settings > Controls.
//
// Source of truth for the UI is app/Actions.h (AllActions / Bindings / SetBindings). The overrides
// are stored in the hidden `controls.keyBindings` setting as
//   {"playback.play_pause": {"key": "Space; MediaPlayPause"}, "video.fullscreen": {"key": ""}}
// keyed by ActionInfo::id; "key" lists the bindings separated by ';' ("" = unassigned). Actions
// at their defaults are not written. Ids of the older schema table (settings::KeyActions(), e.g.
// "fullscreen") are still read and mapped to the matching action.
namespace app {

using KeyBinding = std::pair<UINT, ui::Mods>;

// Applies controls.keyBindings to the action table and keeps it applied when the setting changes
// (reset, another window). Call once after Config::Load().
void LoadKeyBindings();
// Writes the current overrides into controls.keyBindings.
void SaveKeyBindings();

// The bindings of `a` without the "unassigned" marker.
std::vector<KeyBinding> ActiveBindings(Action a);
bool IsDefaultBinding(Action a);
bool SameBinding(const KeyBinding& a, const KeyBinding& b);
// Replaces the bindings of `a` (empty = unassigned) and saves.
void AssignBindings(Action a, std::vector<KeyBinding> keys);
void ResetBindings(Action a);
void ResetAllBindings();

// Key presets: a whole layout applied at once on top of the defaults. "youtube": K / Space play
// and pause, J / L seek 10 s, 0-9 jump to 0-90%, Shift+, / Shift+. slower / faster, C subtitles,
// Shift+N / Shift+P next / previous, I picture in picture; the actions whose keys it takes move
// to free ones (audio delay to Shift+J / Shift+K, repeat to R). Returns false for an unknown id.
bool ApplyKeyPreset(std::string_view id);

// Stable text form used in settings.json: "Ctrl+Shift+Right", "Space", "F11".
std::string BindingToken(const KeyBinding& k);
bool ParseBinding(std::string_view text, KeyBinding* out);

}  // namespace app

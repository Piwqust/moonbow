#pragma once
#include <string>

namespace app {

// Runs the `action` of a settings Button row (settings::Item::action), e.g.
// "clear-history", "reset-picture", "file-associations". Returns false for
// actions the app doesn't know (the settings window handles its own ones such
// as "edit-shortcuts" / "reset-shortcuts").
bool RunSettingsAction(const std::string& action);

}  // namespace app

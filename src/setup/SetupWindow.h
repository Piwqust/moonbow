#pragma once
#include "setup/Installer.h"

#include <string>

namespace setup {

enum class Op { Install, Switch, Uninstall };

struct Request {
    Op op = Op::Install;
    const Package* package = nullptr;  // Install
    std::wstring version;              // Switch: the kept version to make current
    Target target;
    std::wstring previewPage;          // --automation --setup-page <name>: that page, nothing runs
    // Moonbow's own "Restart to update": no question, the work starts at once, and Moonbow opens
    // again when it is done (also after a failure: the old version is still in place then).
    bool autoUpdate = false;
};

// The setup window, drawn with Moonbow's UI: welcome, options, update with what's new, rollback,
// repair or remove, progress, done. Runs its own message loop; 0 when the work was done.
int RunWindow(const Request& request);

}  // namespace setup

#pragma once
#include <windows.h>

#include <memory>

namespace app {

// Windows shell integration for the main window:
//  - taskbar progress and the previous / play-pause / next thumbnail buttons;
//  - System Media Transport Controls (media keys, the volume flyout card).
class SystemIntegration {
public:
    SystemIntegration();
    ~SystemIntegration();

    void Attach(HWND window);
    // Window messages the integration needs (taskbar button creation, thumb
    // button clicks, media key presses marshalled from the SMTC thread).
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
    // Call with PlayerChange masks.
    void Update(unsigned changes);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace app

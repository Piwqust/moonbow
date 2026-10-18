#include "app/SettingsActions.h"
#include "ui/Host.h"

#include "app/App.h"
#include "app/FileAssociations.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "core/Paths.h"
#include "ui/Controls.h"

#include <shellapi.h>

namespace app {

bool RunSettingsAction(const std::string& action) {
    App& app = TheApp();
    auto toast = [&](const wchar_t* text, const char* icon) {
        for (ui::Host* h : ui::Host::All())
            if (h->IsActive()) {
                ui::ShowToast(h, Tr(text), icon);
                return;
            }
        if (app.main()) ui::ShowToast(app.main(), Tr(text), icon);
    };
    if (action == "clear-history") {
        app.history().Clear();
        app.history().Flush();
        toast(L"History cleared", "recent");
        return true;
    }
    if (action == "reset-picture") {
        Player& p = app.player();
        for (const char* prop : {"brightness", "contrast", "saturation", "gamma", "hue"}) p.SetProperty(prop, 0.0);
        p.SetProperty("video-zoom", 0.0);
        p.SetProperty("video-pan-x", 0.0);
        p.SetProperty("video-pan-y", 0.0);
        p.SetProperty("video-rotate", 0.0);
        p.SetProperty("video-aspect-override", std::string("no"));
        toast(L"Picture reset", "video");
        return true;
    }
    if (action == "open-scripts-folder" || action == "open-data-folder") {
        std::wstring dir = DataDir();
        if (action == "open-scripts-folder") {
            dir += L"\\scripts";
            CreateDirectoryW(dir.c_str(), nullptr);
        }
        ui::Host::ShellOpen(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    if (action == "file-associations") {
        // Background test instances never touch the registry.
        if (ui::Host::Automation()) {
            toast(L"Couldn't register file types", "info");
            return true;
        }
        if (RegisterFileAssociations()) {
            OpenDefaultAppsSettings();
            toast(L"Lumen is registered. Pick it in Default apps.", "check_selected");
        } else {
            toast(L"Couldn't register file types", "info");
        }
        return true;
    }
    if (action == "rescan-library") {
        app.library().Rescan();
        toast(L"Rescanning the library…", "refresh");
        return true;
    }
    return false;
}

}  // namespace app

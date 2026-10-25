#include "app/SettingsActions.h"
#include "ui/Host.h"

#include "app/App.h"
#include "app/FileAssociations.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/library/LibraryCommon.h"
#include "app/player/LiveState.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "net/Online.h"
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
        lib::ClearHistory();
        toast(L"History cleared", "recent");
        return true;
    }
    if (action == "clear-online-data") {
        net::online::ClearAll();
        toast(L"Online data cleared", "privacy");
        return true;
    }
    if (action == "reset-picture") {
        Player& p = app.player();
        p.SetProperty("video-pan-x", 0.0);
        p.SetProperty("video-pan-y", 0.0);
        p.SetProperty("video-rotate", std::string("0"));
        // Through LiveState: it owns zoom, aspect, flip and the colour effects, and remembers
        // them per file, so setting mpv directly left its copies stale.
        LiveState& live = Live();
        live.flip = 0;          // applied with the colour effects below
        live.ResetPicture();    // brightness... hue, negate, grayscale
        live.SetZoomMode("fit");  // video-zoom 0, video-unscaled no
        live.SetAspect("auto");
        toast(L"Picture reset", "video");
        return true;
    }
    if (action == "open-logs-folder") {
        ui::Host::ShellOpen(nullptr, L"open", LogDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    if (action == "open-scripts-folder" || action == "open-data-folder" || action == "open-shaders-folder") {
        std::wstring dir = DataDir();
        if (action != "open-data-folder") {
            dir += action == "open-scripts-folder" ? L"\\scripts" : L"\\shaders";
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
        if (!RegisterFileAssociations()) {
            toast(L"Couldn't register file types", "info");
        } else if (SetDefaultPlayer()) {
            toast(L"Moonbow is now the default video player", "check_selected");
        } else {
            OpenDefaultAppsSettings();
            toast(L"Moonbow is registered. Pick it in Default apps.", "check_selected");
        }
        return true;
    }
    if (action == "rescan-library") {
        lib::RescanLibrary();
        toast(L"Rescanning the library…", "refresh");
        return true;
    }
    return false;
}

}  // namespace app

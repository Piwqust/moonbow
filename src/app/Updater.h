#pragma once
// Moonbow's own updates, on top of the setup it already has (src/setup).
//
// - Once a day (general.checkUpdates, on by default) Moonbow reads the signed update feed
//   (setup/UpdateFeed.h) from the latest GitHub release, else from the mirror. Nothing but the
//   request itself is sent: no id, no settings, the version only in the User-Agent.
// - An installed Moonbow downloads a newer setup in the background to %LOCALAPPDATA%\Moonbow\updates
//   and checks its size and SHA-256 against the signed manifest.
// - Then a toast offers "Restart now": the setup runs in its --update mode (progress, then the new
//   Moonbow opens). Otherwise the update installs silently when Moonbow is closed
//   (general.installUpdates); the next start shows "What's new".
// - A portable or developer copy (no versions folder next to the exe) only says that a new version
//   is out and opens its page.
// - A version the user went back from (About › Versions) is not installed again by itself.
//
// UI thread only.
#include "app/Signal.h"
#include "setup/UpdateFeed.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ui {
class Host;
}

namespace app {

class Updater {
public:
    enum class Status {
        Idle,         // nothing known yet (or the check is off)
        Checking,
        UpToDate,
        Downloading,
        Ready,        // a checked setup waits: InstallNow, or on exit
        Available,    // newer version, but this copy cannot install it (portable): OpenPage
        Failed,       // the last check or download failed (no network...): tried again later
    };

    static Updater& Get();

    // After the main window is up (App::Run): the daily check, and what an earlier update left.
    void Start(ui::Host* main);
    // The "Check for updates" buttons: says the result in a toast, tries even while offline.
    void Check();
    // "Restart now": starts the setup's update window and quits Moonbow.
    void InstallNow();
    // The release page of the newer version (or the releases list).
    void OpenPage() const;
    // App::Run after the message loop: installs a ready update silently when allowed.
    void OnExit();
    // Windows is shutting down: no setup is started then.
    void SessionEnding() { sessionEnding_ = true; }

    Status status() const { return status_; }
    const setup::UpdateInfo& Latest() const { return latest_; }
    // This copy is installed by the setup (and can update itself).
    bool CanInstall() const;
    // One line for Settings and About: "Moonbow is up to date", "Moonbow 0.2.0 is ready to install"...
    std::wstring StatusText() const;
    // The button that fits the status: "Check now", "Restart now", "Download"; its action.
    std::wstring ActionText() const;
    void RunAction();

    Signal<> changed;

private:
    Updater() = default;
    void Schedule();
    void StartCheck(bool user);
    void TryFeed(size_t index, bool user, bool sawAnswer);
    void Found(const setup::UpdateInfo& info, bool user);
    void Download(size_t urlIndex, bool user);
    void Verify(bool user, bool downloaded);
    void SetStatus(Status s);
    void Announce();
    void Report(bool user, const std::wstring& text, const char* icon);
    bool AutoInstallAllowed() const;
    bool LaunchSetup(const wchar_t* mode);
    std::wstring SetupPath() const;
    std::wstring InstallDir() const;

    ui::Host* main_ = nullptr;
    Status status_ = Status::Idle;
    setup::UpdateInfo latest_;
    std::vector<std::wstring> feeds_;
    std::wstring heldBack_;       // the version the user went back from: not installed by itself
    std::wstring failedSilent_;   // a silent install of this version failed: not tried silently again
    std::wstring announced_;      // toast shown for this version in this session
    std::wstring error_;          // last failure, for the status line
    uint64_t request_ = 0;
    uintptr_t timer_ = 0, announceTimer_ = 0;
    uint64_t configToken_ = 0;
    bool started_ = false, launched_ = false, sessionEnding_ = false;
};

}  // namespace app

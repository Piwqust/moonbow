#include "app/Updater.h"

#include "Util.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "core/Config.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "net/Http.h"
#include "setup/Installer.h"
#include "ui/Controls.h"
#include "ui/Host.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <ctime>
#include <memory>

namespace app {
namespace {

constexpr int64_t kCheckEvery = 20 * 3600;        // seconds between checks that found an answer
constexpr UINT kFirstCheckMs = 20000;             // after the start, when the film is already playing
constexpr UINT kRetryMs = 3600 * 1000;            // the timer that looks again (and retries failures)
constexpr wchar_t kReleases[] = L"https://github.com/Piwqust/moonbow/releases/latest";

int64_t Now() {
    return static_cast<int64_t>(time(nullptr));
}

std::wstring Version() {
    return net::AppVersion();
}

std::string U8(const std::wstring& s) {
    return json::ToUtf8(s);
}

Config& Cfg() {
    return TheApp().config();
}

bool FileExists(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// %LOCALAPPDATA%\Moonbow\updates (not the roaming data folder: setups are 50 MB); inside the data
// folder when --data-dir moved it, so test instances keep to themselves.
std::wstring UpdatesDir() {
    if (DataDirOverridden()) {
        const std::wstring dir = DataDir();
        if (dir.empty()) return {};
        CreateDirectoryW((dir + L"\\updates").c_str(), nullptr);
        return dir + L"\\updates";
    }
    return KnownFolderPath(FOLDERID_LocalAppData, L"Moonbow\\updates");
}

// "Moonbow-0.2.0-setup.exe" -> "0.2.0"; empty for other names.
std::wstring VersionOfSetupName(const std::wstring& name) {
    constexpr std::wstring_view prefix = L"Moonbow-", suffix = L"-setup.exe";
    if (name.size() <= prefix.size() + suffix.size() || _wcsnicmp(name.c_str(), prefix.data(), prefix.size()) != 0 ||
        _wcsicmp(name.c_str() + name.size() - suffix.size(), suffix.data()) != 0)
        return {};
    return name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
}

// Setups of versions this Moonbow already is (or passed), and leftovers of broken downloads.
void DeleteOldSetups(const std::wstring& dir, const std::wstring& keep) {
    if (dir.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring name = fd.cFileName;
        if (!keep.empty() && _wcsicmp(name.c_str(), keep.c_str()) == 0) continue;
        DeleteFileW((dir + L"\\" + name).c_str());  // the running setup's own file stays: fine
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// A newer setup waits in the folder (downloaded by an earlier session).
bool PendingSetup(const std::wstring& dir) {
    if (dir.empty()) return false;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\Moonbow-*-setup.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        const std::wstring v = VersionOfSetupName(fd.cFileName);
        found = found || (!v.empty() && setup::CompareVersions(v, Version()) > 0);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

std::wstring Describe(const net::Response& r) {
    switch (r.error) {
        case net::Error::None: return L"HTTP " + std::to_wstring(r.status);
        case net::Error::Offline:
        case net::Error::Network: return Tr(L"no connection");
        case net::Error::Timeout: return Tr(L"the server did not answer");
        case net::Error::File: return Tr(L"could not save the file");
        case net::Error::TooLarge: return Tr(L"the file is larger than announced");
        default: return Tr(L"the address was refused");
    }
}

}  // namespace

Updater& Updater::Get() {
    static Updater u;
    return u;
}

bool Updater::CanInstall() const {
    if (ui::Host::Automation()) return false;  // test instances never replace the installed Moonbow
    const std::wstring dir = InstallDir();
    return !dir.empty() && !setup::StoredVersions(dir).empty();
}

std::wstring Updater::InstallDir() const {
    std::wstring dir = ExeDir();
    if (!dir.empty()) dir.pop_back();
    return dir;
}

std::wstring Updater::SetupPath() const {
    const std::wstring dir = UpdatesDir();
    return dir.empty() || latest_.version.empty() ? std::wstring()
                                                  : dir + L"\\Moonbow-" + latest_.version + L"-setup.exe";
}

void Updater::Start(ui::Host* main) {
    if (started_) return;
    started_ = true;
    main_ = main;
    App& app = TheApp();
    const std::wstring now = Version();

    // Went back to an older version (About › Versions): the one left is not installed again by itself.
    const std::string last = app.UiValue("lastVersion");
    if (!last.empty() && setup::CompareVersions(json::ToWide(last), now) > 0) app.SetUiValue("update.heldBack", last);
    heldBack_ = json::ToWide(app.UiValue("update.heldBack"));
    if (!heldBack_.empty() && setup::CompareVersions(now, heldBack_) >= 0) {
        heldBack_.clear();
        app.SetUiValue("update.heldBack", "");
    }
    // The silent install started at the last exit did not happen: no second silent try of it.
    if (const std::wstring attempt = json::ToWide(app.UiValue("update.attempt")); !attempt.empty()) {
        if (setup::CompareVersions(now, attempt) < 0) {
            failedSilent_ = attempt;
            Log("update: Moonbow " + U8(attempt) + " was not installed at the last exit");
        } else {
            Log("update: now on Moonbow " + U8(now));
        }
        app.SetUiValue("update.attempt", "");
    }
    if (!PendingSetup(UpdatesDir())) DeleteOldSetups(UpdatesDir(), {});

    configToken_ = Cfg().Subscribe("general.", [this](const std::string& key) {
        if (key == "general.checkUpdates") {
            Schedule();
            if (Cfg().GetBool(key) && main_)
                main_->StartTimer(1500, [this] {
                    if (status_ == Status::Idle || status_ == Status::Failed) StartCheck(false);
                });
            changed.Emit();
        } else if (key == "general.installUpdates") {
            changed.Emit();
        }
    });
    Schedule();
}

void Updater::Schedule() {
    if (!main_) return;
    if (timer_) main_->CancelTimer(timer_);
    timer_ = 0;
    if (!Cfg().GetBool("general.checkUpdates")) return;
    // Background test instances go online only for a feed a test points them to.
    if (ui::Host::Automation() && setup::UpdateFeedUrls().size() != 1) return;
    auto due = [this] {
        if (status_ == Status::Checking || status_ == Status::Downloading || launched_) return;
        const int64_t checked = _atoi64(TheApp().UiValue("update.checked").c_str());
        // A setup downloaded by an earlier session is picked up at the start, without waiting a day.
        if (Now() - checked >= kCheckEvery || (status_ == Status::Idle && PendingSetup(UpdatesDir())))
            StartCheck(false);
    };
    main_->StartTimer(kFirstCheckMs, due);
    timer_ = main_->StartTimer(kRetryMs, due, true);
}

void Updater::Check() {
    if (status_ == Status::Checking || status_ == Status::Downloading) return;
    if (status_ == Status::Ready) {
        Report(true, StatusText(), "check_selected");
        return;
    }
    StartCheck(true);
}

void Updater::StartCheck(bool user) {
    feeds_ = setup::UpdateFeedUrls();
    error_.clear();
    SetStatus(Status::Checking);
    TryFeed(0, user, false);
}

// Feeds in order; a 404 is an answer ("this release has no feed": nothing newer to offer), a
// network failure is not.
void Updater::TryFeed(size_t index, bool user, bool sawAnswer) {
    if (index >= feeds_.size()) {
        if (sawAnswer) {
            TheApp().SetUiValue("update.checked", std::to_string(Now()));
            SetStatus(Status::UpToDate);
            Report(user, Tr(L"Moonbow is up to date"), "check_selected");
        } else {
            SetStatus(Status::Failed);
            Report(user, Fmt(Tr(L"Couldn't check for updates: {0}"), {error_}), "info");
        }
        return;
    }
    net::Request r;
    r.url = feeds_[index];
    r.maxBytes = 64 * 1024;
    r.user = user;
    r.allowLoopbackHttp = ui::Host::Automation();
    request_ = net::Send(std::move(r), [this, index, user, sawAnswer](const net::Response& resp) {
        request_ = 0;
        setup::UpdateInfo info;
        std::string why;
        if (resp.Ok() && setup::ParseUpdateFeed(resp.body, &info, &why)) {
            Found(info, user);
            return;
        }
        if (resp.Ok()) Log("update: feed " + U8(feeds_[index]) + " refused: " + why);
        const bool notFound = resp.error == net::Error::None && resp.status == 404;
        if (!notFound) error_ = resp.Ok() ? std::wstring(Tr(L"the answer was not signed by Moonbow")) : Describe(resp);
        TryFeed(index + 1, user, sawAnswer || notFound);
    });
}

void Updater::Found(const setup::UpdateInfo& info, bool user) {
    TheApp().SetUiValue("update.checked", std::to_string(Now()));
    if (setup::CompareVersions(info.version, Version()) <= 0) {
        SetStatus(Status::UpToDate);
        Report(user, Tr(L"Moonbow is up to date"), "check_selected");
        return;
    }
    latest_ = info;
    Log("update: Moonbow " + U8(info.version) + " is out");
    if (!CanInstall()) {
        SetStatus(Status::Available);
        if (user) announced_.clear();
        Announce();
        return;
    }
    DeleteOldSetups(UpdatesDir(), L"Moonbow-" + info.version + L"-setup.exe");
    if (FileExists(SetupPath())) {
        SetStatus(Status::Downloading);
        Verify(user, false);
        return;
    }
    if (user) Report(true, Fmt(Tr(L"Downloading Moonbow {0}…"), {info.version}), "arrow_down");
    Download(0, user);
}

void Updater::Download(size_t urlIndex, bool user) {
    if (urlIndex >= latest_.urls.size()) {
        SetStatus(Status::Failed);
        Report(user, Fmt(Tr(L"Couldn't download Moonbow {0}: {1}"), {latest_.version, error_}), "info");
        return;
    }
    SetStatus(Status::Downloading);
    net::Request r;
    r.url = latest_.urls[urlIndex];
    r.saveTo = SetupPath();
    r.maxBytes = static_cast<size_t>(latest_.setupSize);
    r.timeoutMs = 30000;
    r.user = user;
    r.allowLoopbackHttp = ui::Host::Automation();
    request_ = net::Send(std::move(r), [this, urlIndex, user](const net::Response& resp) {
        request_ = 0;
        if (resp.Ok()) {
            Verify(user, true);
            return;
        }
        error_ = Describe(resp);
        Log("update: download from " + U8(latest_.urls[urlIndex]) + " failed: " + U8(error_));
        Download(urlIndex + 1, user);
    });
}

// Size and SHA-256 against the signed manifest, on a net worker (50 MB take a moment to hash).
void Updater::Verify(bool user, bool downloaded) {
    const std::wstring path = SetupPath();
    auto ok = std::make_shared<bool>(false);
    net::Async([path, info = latest_, ok] { *ok = setup::SetupMatches(path, info); },
               [this, path, ok, user, downloaded] {
                   if (*ok) {
                       Log("update: Moonbow " + U8(latest_.version) + " downloaded and checked");
                       SetStatus(Status::Ready);
                       if (user) announced_.clear();
                       Announce();
                       return;
                   }
                   DeleteFileW(path.c_str());
                   if (!downloaded) {  // a leftover from an earlier session: fetch it again
                       Download(0, user);
                       return;
                   }
                   Log("update: the downloaded setup does not match the manifest");
                   error_ = Tr(L"the file did not pass the check");
                   SetStatus(Status::Failed);
                   Report(user, Fmt(Tr(L"Couldn't download Moonbow {0}: {1}"), {latest_.version, error_}), "info");
               });
}

void Updater::SetStatus(Status s) {
    status_ = s;
    changed.Emit();
}

bool Updater::AutoInstallAllowed() const {
    return Cfg().GetBool("general.checkUpdates") && Cfg().GetBool("general.installUpdates") && CanInstall() &&
           latest_.version != failedSilent_ &&
           (heldBack_.empty() || setup::CompareVersions(latest_.version, heldBack_) > 0);
}

// The toast, once per version and session, and not over a film that is playing.
void Updater::Announce() {
    if ((status_ != Status::Ready && status_ != Status::Available) || !main_ || announced_ == latest_.version) return;
    if (!heldBack_.empty() && setup::CompareVersions(latest_.version, heldBack_) <= 0) return;
    const PlayerState& s = TheApp().player().State();
    if (s.fileLoaded && !s.paused && s.hasVideo) {
        if (!announceTimer_)
            announceTimer_ = main_->StartTimer(30000, [this] {
                announceTimer_ = 0;
                Announce();
            });
        return;
    }
    announced_ = latest_.version;
    if (status_ == Status::Available) {
        ui::ShowToast(main_, Fmt(Tr(L"Moonbow {0} is out."), {latest_.version}), "arrow_down", 12000, Tr(L"Download"),
                      [this] { OpenPage(); });
        return;
    }
    const wchar_t* text = AutoInstallAllowed() ? Tr(L"Moonbow {0} is downloaded. It installs when you close Moonbow.")
                                               : Tr(L"Moonbow {0} is ready to install.");
    ui::ShowToast(main_, Fmt(text, {latest_.version}), "arrow_down", 12000, Tr(L"Restart now"), [this] { InstallNow(); });
}

void Updater::Report(bool user, const std::wstring& text, const char* icon) {
    if (!user || !main_) return;
    for (ui::Host* h : ui::Host::All())
        if (h->IsActive()) {
            ui::ShowToast(h, text, icon, 5000);
            return;
        }
    ui::ShowToast(main_, text, icon, 5000);
}

// `mode` is --update or --silent; the setup gets the install folder and waits for this process to end
// before it replaces anything.
bool Updater::LaunchSetup(const wchar_t* mode) {
    const std::wstring exe = SetupPath();
    std::wstring cmd = L"\"" + exe + L"\" " + mode + L" --dir \"" + InstallDir() + L"\" --wait-pid " +
                       std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    const std::wstring dir = UpdatesDir();
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        Log("update: could not start the setup (" + std::to_string(GetLastError()) + ")");
        return false;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    launched_ = true;
    return true;
}

void Updater::InstallNow() {
    if (status_ != Status::Ready || !CanInstall() || launched_ || TheApp().Quitting()) return;
    if (!LaunchSetup(L"--update")) {
        Report(true, Tr(L"Couldn't start the update."), "info");
        return;
    }
    Log("update: installing Moonbow " + U8(latest_.version) + " now");
    TheApp().Quit();
}

void Updater::OnExit() {
    if (launched_ || sessionEnding_ || status_ != Status::Ready || !AutoInstallAllowed()) return;
    if (!FileExists(SetupPath())) return;
    if (!LaunchSetup(L"--silent")) return;
    Log("update: installing Moonbow " + U8(latest_.version) + " after the exit");
    TheApp().SetUiValue("update.attempt", U8(latest_.version));
}

void Updater::OpenPage() const {
    const std::wstring& url = latest_.page.empty() ? std::wstring(kReleases) : latest_.page;
    ui::Host::ShellOpen(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring Updater::StatusText() const {
    switch (status_) {
        case Status::Checking: return Tr(L"Checking for updates…");
        case Status::UpToDate: return Tr(L"Moonbow is up to date");
        case Status::Downloading: return Fmt(Tr(L"Downloading Moonbow {0}…"), {latest_.version});
        case Status::Ready:
            return AutoInstallAllowed()
                       ? Fmt(Tr(L"Moonbow {0} is ready. It installs when you close Moonbow."), {latest_.version})
                       : Fmt(Tr(L"Moonbow {0} is ready to install."), {latest_.version});
        case Status::Available: return Fmt(Tr(L"Moonbow {0} is out."), {latest_.version});
        case Status::Failed:
            return error_.empty() ? std::wstring(Tr(L"Couldn't check for updates"))
                                  : Fmt(Tr(L"Couldn't check for updates: {0}"), {error_});
        case Status::Idle: break;
    }
    return Cfg().GetBool("general.checkUpdates") ? Tr(L"Not checked yet") : Tr(L"Automatic checks are off");
}

std::wstring Updater::ActionText() const {
    switch (status_) {
        case Status::Checking:
        case Status::Downloading: return {};
        case Status::Ready: return Tr(L"Restart now");
        case Status::Available: return Tr(L"Download");
        default: return Tr(L"Check now");
    }
}

void Updater::RunAction() {
    if (status_ == Status::Ready) InstallNow();
    else if (status_ == Status::Available) OpenPage();
    else Check();
}

}  // namespace app

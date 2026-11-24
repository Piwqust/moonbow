#pragma once
#include "Player.h"
#include "app/ImageCache.h"
#include "app/PlayQueue.h"
#include "app/Signal.h"
#include "app/SystemIntegration.h"
#include "core/Config.h"
#include "library/Bookmarks.h"
#include "library/History.h"
#include "library/MediaLibrary.h"
#include "library/Playlists.h"
#include "library/Thumbnails.h"
#include "ui/Types.h"

#include <windows.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app {

class MainWindow;

inline constexpr UINT kLibraryMessage = WM_APP + 80;     // MediaLibrary scan results
inline constexpr UINT kThumbnailsMessage = WM_APP + 81;  // Thumbnails completions

// --- navigation --------------------------------------------------------------------

enum class Section : uint8_t { Home, Videos, Music, Network, Playlists, Recent };

// A place in the library. `path` is a folder for Videos/Music, a sub-page key
// for Music ("songs", "albums", "artists", "genres", "album:<name>",
// "artist:<name>"), a playlist id for Playlists, or empty for the section root.
struct Route {
    Section section = Section::Home;
    std::wstring path;
    bool operator==(const Route& o) const { return section == o.section && path == o.path; }
};

// Where a library page was left, kept with its history entry so Back and Forward return to the
// same scroll offset and selection. `within`: select the entry that contains `selPath` (going up
// from a folder selects the folder you came from).
struct ViewState {
    float scroll = -1;  // < 0 = none saved
    std::wstring selPath, selTitle;
    bool within = false;
    bool Empty() const { return scroll < 0 && selPath.empty() && selTitle.empty(); }
};

const wchar_t* SectionName(Section s);  // English, translate with Tr()
const char* SectionIcon(Section s);

// --- application ---------------------------------------------------------------------

// Owns the player, the main window and the app-wide state. UI thread only.
class App {
public:
    static App& Get();

    int Run(HINSTANCE instance, std::vector<std::wstring> args);

    Player& player() { return player_; }
    PlayQueue& queue() { return queue_; }
    MainWindow* main() const { return main_.get(); }
    HINSTANCE instance() const { return instance_; }

    // --- services (UI thread) ------------------------------------------------------
    Config& config() { return config_; }
    library::MediaLibrary& library() { return library_; }
    library::History& history() { return history_; }
    library::Playlists& playlists() { return playlists_; }
    library::Bookmarks& bookmarks() { return bookmarks_; }
    library::Favorites& favorites() { return favorites_; }
    library::Thumbnails& thumbnails() { return thumbnails_; }
    ImageCache& images() { return images_; }
    SystemIntegration& system() { return system_; }

    // Small UI state that is not a user setting (ui.json): control bar layout,
    // per-page view modes, sidebar state, window placement.
    std::string UiValue(const std::string& key, const std::string& fallback = {}) const;
    void SetUiValue(const std::string& key, const std::string& value);

    // --- playback ---------------------------------------------------------------
    // Plays files/URLs; a single local file pulls its folder siblings into the queue.
    void Open(const std::vector<std::wstring>& paths);
    // Adds files, folders and playlists to the end of the queue (starts it when nothing plays).
    void Enqueue(const std::vector<std::wstring>& paths);
    // `showPlayer` off: songs started from the library keep the page and play in the mini player.
    void PlayQueueItems(std::vector<QueueItem> items, int index, std::wstring name = {}, bool showPlayer = true);
    // `showPlayer` = switch to the player screen (off for next/previous, so music
    // keeps playing in the mini player while the user browses the library).
    void PlayIndex(int index, bool showPlayer = true);
    bool Next(bool userAction = true);  // false at the end of the queue
    void Previous();
    void StopPlayback();
    // The Stop command: back to 0:00, paused; History and the resume point keep where it stopped.
    void StopAtStart();
    // Hands the item that plays after the current one to mpv (gapless, prefetch), or none when
    // the queue stops there. Called whenever the queue or its options change.
    void SyncNextInPlayer(bool force = false);
    // Takes an item out of the queue; removing the one that plays goes on to its successor
    // (or stops at the end), so the player never keeps a file the queue no longer has.
    void RemoveFromQueue(int index);
    void OpenFileDialog();
    void OpenFolderDialog();
    void OpenSubtitleDialog();
    void OpenUrl(const std::wstring& url);
    bool HasMedia() const;

    // --- navigation ---------------------------------------------------------------
    const Route& route() const { return nav_[static_cast<size_t>(historyPos_)].route; }
    void Navigate(Route r);
    bool CanGoBack() const { return historyPos_ > 0; }
    bool CanGoForward() const { return historyPos_ + 1 < static_cast<int>(nav_.size()); }
    void GoBack();
    void GoForward();
    // View state of the current history entry; [compact] = the player's side panel pane.
    ViewState& CurrentViewState(bool compact) { return nav_[static_cast<size_t>(historyPos_)].state[compact ? 1 : 0]; }

    // Library vs. player screen. Showing the library while media plays keeps the mini player.
    void ShowPlayer(bool show);
    bool PlayerShown() const { return playerShown_; }
    // A file was opened to be shown in the player: the current screen stays until mpv shows its
    // first frame (and a song's cover is decoded), at most kPlayerHoldMs, then the player comes
    // in one step instead of an empty black player first. The player is laid out meanwhile.
    bool PlayerPending() const { return playerPending_; }
    // The file the pending player opens, once the wait is long enough to be noticed (150 ms): the
    // clicked card or row shows it pressed until the player comes. Empty otherwise.
    std::wstring SlowOpeningPath() const;
    // The pending player has what its first frame needs: shows it (also called every frame).
    void CheckPlayerHold();
    // MainWindow::ContentReady: the first screen is complete (stores loaded, its images
    // decoded, a file given at launch showing).
    bool FirstScreenReady() const;
    bool OpensFileAtStart() const { return !pendingOpen_.empty() || pendingIndex_ >= 0 || playerPending_; }
    // AnimFloat::Now() when the file being opened was asked for (at launch: the process start),
    // until its first frame is up or it fails; -1 when nothing is opening. The seek bar shows a
    // loading line once this is kLoadingLineMs old, and the player's picture fades in after it.
    double OpeningSince() const { return openAt_; }
    static constexpr UINT kLoadingLineMs = 300;  // a file at launch is up in ~0.27 s here
    // The file that failed to open (shown as a card in the player); empty when none.
    const std::wstring& FailedPath() const { return failedPath_; }
    const std::wstring& FailedError() const { return failedError_; }

    // --- windows --------------------------------------------------------------------
    // `section`: see ShowSettingsWindow (Dialogs.h).
    void OpenSettings(const std::string& category = {}, const std::string& section = {});
    void ShowOnboarding();
    void Quit();
    // Set once the main window starts closing (mpv shuts down on another thread): from then on
    // nothing may touch the player, and files sent by a new instance are refused.
    void BeginQuit();  // also saves the window placement while the window is still shown
    bool Quitting() const { return quitting_; }
    // WM_ENDSESSION: Windows logs off or shuts down and may end the process as soon as this
    // returns. Closes as usual and waits (up to 3 s) until the stores and watch-later are written.
    void EndSession();

    // --- signals --------------------------------------------------------------------
    Signal<unsigned> playerChanged;  // PlayerChange mask
    Signal<> routeLeaving;           // before the route changes: panes save their ViewState
    Signal<> routeChanged;
    Signal<> screenChanged;          // ShowPlayer toggled
    Signal<> libraryChanged;         // a scan published new results
    Signal<> languageChanged;        // UI must rebuild its strings

    // Called by MainWindow.
    void OnPlayerReady(bool ok);
    void OnPlayerEvents();
    void OnCopyData(const std::wstring& payload);
    // --automation only: one test command (tools/dev/command.ps1 lists them). Returns false when
    // the command is unknown or could not be done; `reply` says what happened.
    bool AutomationCommand(const std::wstring& command, std::string* reply);
    void OnMainDestroyed();
    void OnLibraryScanned();
    void OnThumbnails();
    void FlushStores();
    // general.theme / accent to the palette and every window; also when Windows switches
    // light/dark (WM_SETTINGCHANGE) under the System theme.
    void ApplyTheme();
    // Laptops on battery: the Fast picture preset (video.batteryQuality). At start and on
    // WM_POWERBROADCAST; re-applies the preset's mpv options when the answer changes.
    void UpdatePowerSaving();
    void ApplyLanguage();

private:
    App();
    void BuildFolderQueue(const std::wstring& file);
    std::vector<std::wstring> SavedFolderOrder(const std::wstring& folder) const;
    void RememberFolderOrder();
    void OnConfigChanged(const std::string& key);
    void SaveHistoryPosition();
    double AudioResumePoint(const std::wstring& path) const;
    void SaveWindowPlacement();
    void RestoreWindowPlacement();
    void OnPlaybackEnded();
    void OnAdvanced();
    void FitWindowToVideo();
    void FitIfPending();  // once per file, when the video size is known and the player shows
    void HoldPlayer();
    void ApplyWindowOptions();
    void LoadUiState();
    void SaveUiState();

    HINSTANCE instance_ = nullptr;
    Config config_;
    library::MediaLibrary library_;
    library::History history_;
    std::wstring folderQueue_;  // the folder the queue was built from (its order is remembered), or empty
    library::Playlists playlists_;
    library::Bookmarks bookmarks_;
    library::Favorites favorites_;
    library::Thumbnails thumbnails_;
    ImageCache images_{thumbnails_};
    SystemIntegration system_;
    std::map<std::string, std::string> ui_;
    bool uiDirty_ = false;
    double lastHistorySave_ = 0;
    Player player_;
    PlayQueue queue_;
    std::unique_ptr<MainWindow> main_;
    struct NavEntry {
        Route route;
        ViewState state[2];
    };
    std::vector<NavEntry> nav_{NavEntry{}};
    bool DeadRoute(const Route& r) const;
    int historyPos_ = 0;
    bool playerShown_ = false;
    bool playerPending_ = false;
    uint64_t holdSerial_ = 0;      // PlayerState::loadSerial when the hold began: a newer file ends it
    UINT_PTR holdTimer_ = 0;
    double holdStart_ = 0;         // AnimFloat::Now() when the hold began
    double openAt_ = -1;           // OpeningSince
    double launchAt_ = -1;         // Run's start while a file given at launch waits for PlayIndex
    uint64_t openSerial_ = 0;      // PlayerState::loadSerial when that open was sent
    bool started_ = false;         // Run has loaded the stores
    bool libraryShown_ = false;    // the pages got the first scan results
    std::wstring failedPath_, failedError_;
    double lastRestart_ = 0;      // Previous restarted the file at this time (ms)
    uint64_t recordedSerial_ = 0;  // PlayerState::loadSerial already counted in history
    RECT preFitRect_{};  // window rect before FitWindowToVideo grew it; empty when not fitted
    std::vector<std::wstring> pendingOpen_;
    std::vector<std::wstring> forwarded_;  // files other launches handed over together (OnCopyData)
    double forwardedAt_ = 0;
    int pendingIndex_ = -1;  // PlayIndex before mpv was ready: the queue is built, play this item
    bool pendingShowPlayer_ = true;  // that PlayIndex's showPlayer
    bool engineReady_ = false;
    bool fitPending_ = false;
    bool fittedOnce_ = false;
    bool powerSaving_ = false;
    bool quitting_ = false;
    int queuedIndex_ = -1;      // queue index of the file handed to Player::QueueNext
    uint64_t infoSerial_ = 0;   // PlayerState::loadSerial whose title/duration the queue item has
    bool settingCurrent_ = false;  // PlayIndex/OnAdvanced move the queue; they sync mpv themselves
};

inline App& TheApp() {
    return App::Get();
}

}  // namespace app

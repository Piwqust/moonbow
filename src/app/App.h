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
    void PlayQueueItems(std::vector<QueueItem> items, int index, std::wstring name = {});
    // `showPlayer` = switch to the player screen (off for next/previous, so music
    // keeps playing in the mini player while the user browses the library).
    void PlayIndex(int index, bool showPlayer = true);
    bool Next(bool userAction = true);  // false at the end of the queue
    void Previous();
    void StopPlayback();
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
    const Route& route() const { return nav_[static_cast<size_t>(historyPos_)]; }
    void Navigate(Route r);
    bool CanGoBack() const { return historyPos_ > 0; }
    bool CanGoForward() const { return historyPos_ + 1 < static_cast<int>(nav_.size()); }
    void GoBack();
    void GoForward();

    // Library vs. player screen. Showing the library while media plays keeps the mini player.
    void ShowPlayer(bool show);
    bool PlayerShown() const { return playerShown_; }
    // The file that failed to open (shown as a card in the player); empty when none.
    const std::wstring& FailedPath() const { return failedPath_; }
    const std::wstring& FailedError() const { return failedError_; }

    // --- windows --------------------------------------------------------------------
    void OpenSettings(const std::string& category = {});
    void ShowOnboarding();
    void Quit();
    // Set once the main window starts closing (mpv shuts down on another thread): from then on
    // nothing may touch the player, and files sent by a new instance are refused.
    void BeginQuit() { quitting_ = true; }
    bool Quitting() const { return quitting_; }

    // --- signals --------------------------------------------------------------------
    Signal<unsigned> playerChanged;  // PlayerChange mask
    Signal<> routeChanged;
    Signal<> screenChanged;          // ShowPlayer toggled
    Signal<> libraryChanged;         // a scan published new results
    Signal<> languageChanged;        // UI must rebuild its strings

    // Called by MainWindow.
    void OnPlayerReady(bool ok);
    void OnPlayerEvents();
    void OnCopyData(const std::wstring& payload);
    void OnMainDestroyed();
    void OnLibraryScanned();
    void OnThumbnails();
    void FlushStores();

private:
    App();
    void BuildFolderQueue(const std::wstring& file);
    void ApplyTheme();
    void OnConfigChanged(const std::string& key);
    void SaveHistoryPosition();
    void SaveWindowPlacement();
    void RestoreWindowPlacement();
    void OnPlaybackEnded();
    void OnAdvanced();
    void FitWindowToVideo();
    void ApplyWindowOptions();
    void LoadUiState();
    void SaveUiState();

    HINSTANCE instance_ = nullptr;
    Config config_;
    library::MediaLibrary library_;
    library::History history_;
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
    std::vector<Route> nav_{Route{}};
    int historyPos_ = 0;
    bool playerShown_ = false;
    std::wstring failedPath_, failedError_;
    double lastRestart_ = 0;
    uint64_t recordedSerial_ = 0;
    RECT preFitRect_{};  // window rect before FitWindowToVideo grew it; empty when not fitted  // PlayerState::loadSerial already counted in history  // Previous restarted the file at this time (ms)
    std::vector<std::wstring> pendingOpen_;
    int pendingIndex_ = -1;  // PlayIndex before mpv was ready: the queue is built, play this item
    bool engineReady_ = false;
    bool fitPending_ = false;
    bool fittedOnce_ = false;
    bool quitting_ = false;
    int queuedIndex_ = -1;      // queue index of the file handed to Player::QueueNext
    uint64_t infoSerial_ = 0;   // PlayerState::loadSerial whose title/duration the queue item has
    bool settingCurrent_ = false;  // PlayIndex/OnAdvanced move the queue; they sync mpv themselves
};

inline App& TheApp() {
    return App::Get();
}

}  // namespace app

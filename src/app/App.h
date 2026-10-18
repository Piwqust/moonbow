#pragma once
#include "Player.h"
#include "app/ImageCache.h"
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

// --- play queue --------------------------------------------------------------------

struct QueueItem {
    std::wstring path;   // file path or URL
    std::wstring title;  // optional display name
    double duration = 0; // seconds, 0 if unknown
};

enum class Repeat : uint8_t { Off, All, One };

class PlayQueue {
public:
    const std::vector<QueueItem>& Items() const { return items_; }
    int Current() const { return current_; }
    const QueueItem* CurrentItem() const;
    bool Shuffle() const { return shuffle_; }
    Repeat RepeatMode() const { return repeat_; }
    std::wstring Name() const { return name_; }  // e.g. folder or playlist name

    void Set(std::vector<QueueItem> items, int current, std::wstring name = {});
    void Add(std::vector<QueueItem> items);           // append
    void InsertNext(std::vector<QueueItem> items);    // after the current item
    void Remove(int index);
    void Move(int from, int to);
    void Clear();
    void SetCurrent(int index);
    void SetShuffle(bool on);
    void SetRepeat(Repeat r);

    // Index of the item that plays after/before the current one, or -1.
    int NextIndex(bool userAction) const;
    int PrevIndex() const;
    double TotalDuration() const;

    Signal<> changed;

private:
    void Reshuffle();
    std::vector<QueueItem> items_;
    std::vector<int> order_;  // play order when shuffled
    int current_ = -1;
    bool shuffle_ = false;
    Repeat repeat_ = Repeat::Off;
    std::wstring name_;
};

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
    bool engineReady_ = false;
    bool fitPending_ = false;
    bool fittedOnce_ = false;
    bool quitting_ = false;
    int queuedIndex_ = -1;      // queue index of the file handed to Player::QueueNext
    bool settingCurrent_ = false;  // PlayIndex/OnAdvanced move the queue; they sync mpv themselves
};

inline App& TheApp() {
    return App::Get();
}

}  // namespace app

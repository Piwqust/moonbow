#pragma once
#include "Player.h"
#include "app/Signal.h"
#include "ui/Types.h"

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace app {

class MainWindow;

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

    // --- playback ---------------------------------------------------------------
    // Plays files/URLs; a single local file pulls its folder siblings into the queue.
    void Open(const std::vector<std::wstring>& paths);
    void PlayQueueItems(std::vector<QueueItem> items, int index, std::wstring name = {});
    void PlayIndex(int index);
    void Next(bool userAction = true);
    void Previous();
    void StopPlayback();
    void OpenFileDialog();
    void OpenFolderDialog();
    void OpenSubtitleDialog();
    void OpenUrl(const std::wstring& url);
    bool HasMedia() const;

    // --- navigation ---------------------------------------------------------------
    const Route& route() const { return history_[static_cast<size_t>(historyPos_)]; }
    void Navigate(Route r);
    bool CanGoBack() const { return historyPos_ > 0; }
    bool CanGoForward() const { return historyPos_ + 1 < static_cast<int>(history_.size()); }
    void GoBack();
    void GoForward();

    // Library vs. player screen. Showing the library while media plays keeps the mini player.
    void ShowPlayer(bool show);
    bool PlayerShown() const { return playerShown_; }

    // --- windows --------------------------------------------------------------------
    void OpenSettings(const std::string& category = {});
    void ShowOnboarding();
    void Quit();

    // --- signals --------------------------------------------------------------------
    Signal<unsigned> playerChanged;  // PlayerChange mask
    Signal<> routeChanged;
    Signal<> screenChanged;          // ShowPlayer toggled

    // Called by MainWindow.
    void OnPlayerReady(bool ok);
    void OnPlayerEvents();
    void OnCopyData(const std::wstring& payload);
    void OnMainDestroyed();

private:
    App() = default;
    void BuildFolderQueue(const std::wstring& file);

    HINSTANCE instance_ = nullptr;
    Player player_;
    PlayQueue queue_;
    std::unique_ptr<MainWindow> main_;
    std::vector<Route> history_{Route{}};
    int historyPos_ = 0;
    bool playerShown_ = false;
    std::vector<std::wstring> pendingOpen_;
    bool engineReady_ = false;
    bool quitting_ = false;
};

inline App& TheApp() {
    return App::Get();
}

}  // namespace app

#pragma once
// Helpers shared by the library pages: formatting, queue building, item menus,
// file pickers and small prompts. UI thread only.
#include "app/App.h"
#include "ui/Controls.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app::lib {

// --- formatting ----------------------------------------------------------------------
std::wstring FormatDuration(double seconds);   // "4:05", "1:02:03"; empty when unknown
std::wstring FormatSize(uint64_t bytes);       // "350 МБ", "1,2 ГБ"
std::wstring FormatDate(int64_t unixMs);       // short date in the UI language
std::wstring FormatClock(int64_t unixMs);      // "14:05"
// 0 = today, 1 = yesterday, 2 = earlier (local time).
int DayBucket(int64_t unixMs);
bool IsUrl(const std::wstring& path);
// "C:\..." or "\\server\...": a folder route or path, not a sub-page key or a URL.
bool LooksLikeFolder(const std::wstring& path);
// "1 284" (a no-break space) in Russian, "1,284" in English.
std::wstring Grouped(long long n);
// "12 songs" / "12 песен", "3 plays" / "3 прослушивания", "5 albums" / "5 альбомов".
std::wstring CountSongs(long long n);
std::wstring CountPlays(long long n);
std::wstring CountFiles(long long n);  // "12 files" / "12 файлов"
std::wstring CountAlbums(long long n);
std::wstring CountStations(long long n);  // "5 станций"
std::wstring CountEpisodes(long long n);  // "1 выпуск": a podcast's
std::wstring CountShowEpisodes(long long n);  // "1 серия": a show's, as "Next episode" says it
// The header counts with the page's noun (UX-19): "3 альбома", "2 сериала", "5 плейлистов".
std::wstring CountArtists(long long n);
std::wstring CountGenres(long long n);
std::wstring CountMovies(long long n);
std::wstring CountShows(long long n);
std::wstring CountPlaylists(long long n);
// Tag labels for untagged songs: "Unknown artist" / "Unknown album".
std::wstring ArtistLabel(const std::wstring& artist);
std::wstring AlbumLabel(const std::wstring& album);
// A search query quoted in a message: long pastes end with "…" after about 40 characters.
std::wstring ShortQuery(const std::wstring& query);
// False only for a local file that is gone. Network paths (\\server\share, mapped drives)
// count as present without a look: with the NAS off, GetFileAttributesW blocks the UI thread
// for the network timeout on every page rebuild.
bool MaybeExists(const std::wstring& path);
// A 0-byte file on a local drive; false for network paths, without a look (as above).
bool IsEmptyLocalFile(const std::wstring& path);
// Albums are told apart by name and folder ("Greatest Hits" by Queen and by ABBA are two). A
// "CD1" / "Disc 2" subfolder counts as its parent, so a multi-disc album stays one.
std::wstring AlbumFolderKey(const library::MediaItem& m);  // PathKey of that folder
// Route argument of an album: "<name>\n<folder key>" for a tagged album. A plain "<name>" (old
// routes, "nav music album:X") still finds every album of that name.
std::wstring AlbumRouteArg(const library::MediaItem& m);
// The library search rebuilds a page once typing pauses (150 ms), not on every key: a page of
// 20 000 songs takes a few hundred milliseconds to rebuild. Clearing the box applies at once.
class SearchDebounce {
public:
    SearchDebounce() = default;
    SearchDebounce(const SearchDebounce&) = delete;
    SearchDebounce& operator=(const SearchDebounce&) = delete;
    ~SearchDebounce() { Cancel(); }
    void Run(ui::Widget* owner, const std::wstring& text, std::function<void()> apply);
    void Cancel();

private:
    ui::Host* host_ = nullptr;
    UINT_PTR timer_ = 0;
};

// Title shown for a file or stream: tags for songs, the file name without its extension for
// videos and unknown files, the stored title (history, playlist) or the address for streams.
// Library items: the title tag of songs, "S01E02 · Title" for episodes, else the file name.
std::wstring MediaTitle(const std::wstring& path, const library::MediaItem* item, const std::wstring& storedTitle = {});
bool SamePath(const std::wstring& a, const std::wstring& b);
// Current media of the player (empty when nothing is loaded).
std::wstring NowPlayingPath();
// The "New" chip: a library file found since the user last looked at the library, and never
// played (any history entry counts: a file played already is not news).
bool ShowsNew(const library::MediaItem& m, bool played);

// --- queue / playback -------------------------------------------------------------------
QueueItem MakeQueueItem(const std::wstring& path, const std::wstring& title = {}, double duration = 0);
void PlayQueue(std::vector<QueueItem> items, int index, const std::wstring& name);
void PlayNext(std::vector<QueueItem> items);
void AddToQueue(std::vector<QueueItem> items);

// --- library roots ----------------------------------------------------------------------
// Folder picker; adds the folder as a library root, saves and rescans.
void AddFolderToLibrary();
// With an Undo toast in `toastHost` (the main window when null).
void RemoveLibraryRoot(const std::wstring& folder, ui::Host* toastHost = nullptr);
// A folder dropped on the window: added to the library when no root covers it yet, then opened
// as a page (Music when it holds more songs than videos) to sort and pick from.
void ShowFolderInLibrary(const std::wstring& folder);
void RescanLibrary();
// Forgets the history, saved positions, per-file settings and what the online services sent.
void ClearHistory();
// One entry (and its resume point), with an Undo toast.
void RemoveFromHistory(const std::wstring& path);

// --- shell ------------------------------------------------------------------------------
void ShowInExplorer(const std::wstring& path);
std::wstring PickPlaylistFile();               // .m3u/.m3u8 open dialog, empty if cancelled
std::wstring PickPlaylistSaveFile(const std::wstring& suggestedName);

// --- menus ------------------------------------------------------------------------------
// The items a menu acts on, shared by its actions: a page's queue can hold 20 000 songs, and
// menus copy their items (and so the actions' captures) several times.
using SharedQueue = std::shared_ptr<const std::vector<QueueItem>>;
SharedQueue ShareQueue(std::vector<QueueItem> items);
// "Add to playlist" submenu: existing playlists + "New playlist…".
std::vector<ui::MenuItem> AddToPlaylistMenu(SharedQueue items);
ui::MenuItem Item(const wchar_t* label, const char* icon, std::function<void()> action, bool enabled = true);
// "Library Folders…" for the pages' "More" menus: Settings › Library, where roots are listed.
ui::MenuItem LibraryFoldersItem();

// Standard menu for media files (Play, Play next, Add to queue, Add to playlist, Watched,
// Favorite, Show in Explorer, Go to folder). `queue`/`index` are what "Play" starts.
std::vector<ui::MenuItem> MediaMenu(const std::wstring& path, SharedQueue queue, int index,
                                    const std::wstring& queueName, bool goToFolder = true);

// --- prompts ----------------------------------------------------------------------------
// Both use the dialogs' frame (dlg::Frame): Cancel on the left, the action on the right. Every
// confirm here removes something, so the keyboard starts on Cancel and Enter alone deletes nothing.
// `other` adds a second action left of the main one.
void ShowTextPrompt(const std::wstring& title, const std::wstring& label, const std::wstring& initial,
                    const std::wstring& okText, std::function<void(const std::wstring&)> done,
                    const std::wstring& placeholder = {});
void ShowConfirm(const std::wstring& title, const std::wstring& text, const std::wstring& okText,
                 std::function<void()> done, const std::wstring& otherText = {}, std::function<void()> other = {});
// With an action ("Undo") the toast stays 5 s and takes the click on its button.
void Toast(const std::wstring& text, const char* icon = "info", const std::wstring& actionLabel = {},
           std::function<void()> action = {});

// --- small UI-state stores (ui.json) ------------------------------------------------------
struct SavedStream {
    std::wstring url;
    std::wstring title;
};
// group: "saved", "radio", "podcasts"
std::vector<SavedStream> SavedStreams(const std::string& group);
void SetSavedStreams(const std::string& group, const std::vector<SavedStream>& list);
void CountPlaylistPlay(const std::string& id);
int PlaylistPlays(const std::string& id);
// Card image of a playlist: the cover picked in "New playlist", else the first local item.
std::wstring PlaylistImage(const library::Playlist& p);

// "view.<key>" = grid|list, "sort.<key>" = <name>[:desc]
bool ListModeFor(const std::string& key, bool fallbackList);
void SetListModeFor(const std::string& key, bool list);
// The Music tab opened last ("albums", "artists", "songs", "genres"): the route path that "Music"
// leads to, so Back never lands on a page that shows whichever tab was used since.
std::wstring LastMusicTab();

}  // namespace app::lib

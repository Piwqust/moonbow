#pragma once
// Helpers shared by the library pages: formatting, queue building, item menus,
// file pickers and small prompts. UI thread only.
#include "app/App.h"
#include "ui/Controls.h"

#include <functional>
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
// "12 songs" / "12 песен", "3 plays" / "3 прослушивания", "5 albums" / "5 альбомов".
std::wstring CountSongs(long long n);
std::wstring CountPlays(long long n);
std::wstring CountAlbums(long long n);
std::wstring CountArtists(long long n);   // "3 исполнителя"
std::wstring CountGenres(long long n);    // "2 жанра"
std::wstring CountStations(long long n);  // "5 станций"
std::wstring CountEpisodes(long long n);  // "1 выпуск"
// Tag labels for untagged songs: "Unknown artist" / "Unknown album".
std::wstring ArtistLabel(const std::wstring& artist);
std::wstring AlbumLabel(const std::wstring& album);
// False only for a local file that is gone. Network paths (\\server\share, mapped drives)
// count as present without a look: with the NAS off, GetFileAttributesW blocks the UI thread
// for the network timeout on every page rebuild.
bool MaybeExists(const std::wstring& path);
// Albums are told apart by name and folder ("Greatest Hits" by Queen and by ABBA are two). A
// "CD1" / "Disc 2" subfolder counts as its parent, so a multi-disc album stays one.
std::wstring AlbumFolderKey(const library::MediaItem& m);  // PathKey of that folder
// Route argument of an album: "<name>\n<folder key>" for a tagged album. A plain "<name>" (old
// routes, "nav music album:X") still finds every album of that name.
std::wstring AlbumRouteArg(const library::MediaItem& m);
// Title shown for a file or stream: tags for songs, the file name without its extension for
// videos and unknown files, the stored title (history, playlist) or the address for streams.
// Library items: the title tag of songs, "S01E02 · Title" for episodes, else the file name.
std::wstring MediaTitle(const std::wstring& path, const library::MediaItem* item, const std::wstring& storedTitle = {});
bool SamePath(const std::wstring& a, const std::wstring& b);
// Current media of the player (empty when nothing is loaded).
std::wstring NowPlayingPath();

// --- queue / playback -------------------------------------------------------------------
QueueItem MakeQueueItem(const std::wstring& path, const std::wstring& title = {}, double duration = 0);
void PlayQueue(std::vector<QueueItem> items, int index, const std::wstring& name);
void PlayNext(std::vector<QueueItem> items);
void AddToQueue(std::vector<QueueItem> items);

// --- library roots ----------------------------------------------------------------------
// Folder picker; adds the folder as a library root, saves and rescans.
void AddFolderToLibrary();
void RemoveLibraryRoot(const std::wstring& folder);
void RescanLibrary();

// --- shell ------------------------------------------------------------------------------
void ShowInExplorer(const std::wstring& path);
std::wstring PickPlaylistFile();               // .m3u/.m3u8 open dialog, empty if cancelled
std::wstring PickPlaylistSaveFile(const std::wstring& suggestedName);

// --- menus ------------------------------------------------------------------------------
// "Add to playlist" submenu: existing playlists + "New playlist…".
std::vector<ui::MenuItem> AddToPlaylistMenu(std::vector<QueueItem> items);
ui::MenuItem Item(const wchar_t* label, const char* icon, std::function<void()> action, bool enabled = true);

// Standard menu for media files (Play, Play next, Add to queue, Add to playlist, Watched,
// Favorite, Show in Explorer, Go to folder). `queue`/`index` are what "Play" starts.
std::vector<ui::MenuItem> MediaMenu(const std::wstring& path, std::vector<QueueItem> queue, int index,
                                    const std::wstring& queueName, bool goToFolder = true);

// --- prompts ----------------------------------------------------------------------------
void ShowTextPrompt(const std::wstring& title, const std::wstring& label, const std::wstring& initial,
                    const std::wstring& okText, std::function<void(const std::wstring&)> done,
                    const std::wstring& placeholder = {});
void ShowConfirm(const std::wstring& title, const std::wstring& text, const std::wstring& okText,
                 std::function<void()> done);
void Toast(const std::wstring& text, const char* icon = "info");

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

}  // namespace app::lib

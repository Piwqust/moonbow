#pragma once
// Media library: root folders, a background scan and an immutable snapshot of the results.
//
// Threading: every method is meant for the UI thread. The scan runs on its own thread and
// publishes a new `Snapshot` (std::shared_ptr<const Snapshot>), then posts the notify message.
// A snapshot never changes after it is published, so the UI may keep one as long as it likes
// and read it without locks; Current() swaps under a short mutex.
//
// Scan order: (1) enumerate files (FindFirstFileExW + FIND_FIRST_EX_LARGE_FETCH, recursive,
// hidden/system skipped) and publish; (2) read duration and tags (title, artist, album, album
// artist, genre, track, disc, year) with Lumen's own reader (TagReader.h) and publish again. The
// Windows property system is the fallback for the formats the reader doesn't cover (MKV, AVI,
// WMA...) and for durations it can't get cheaply. Step 2 is skipped
// for files whose size and modification time match library-cache.json. If the first files show
// that reading properties costs <= 3 ms per file, steps 1 and 2 run back to back with a single
// publish.

#include "MediaTypes.h"

#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace library {

struct MediaItem {
    std::wstring path;
    std::wstring name;    // display name: file name without extension
    std::wstring folder;  // parent folder path
    MediaKind kind = MediaKind::Other;
    uint64_t size = 0;      // bytes
    int64_t mtime = 0;      // last write, Unix ms
    double duration = 0;    // seconds; 0 = unknown (yet)
    std::wstring title;     // tags, may be empty
    std::wstring artist;
    std::wstring album;
    std::wstring albumArtist;  // may be empty
    int track = 0;  // track number tag, 0 = none
    int year = 0;   // release year tag, 0 = none
    int disc = 0;   // disc number tag, 0 = none
    std::wstring genre;  // first genre tag, may be empty
    bool offline = false;    // cloud placeholder (OneDrive etc.): content not on this disk
    // The metadata pass found the file unplayable: its header isn't the promised container, or
    // an MP4-family file has no readable length (truncated download). Never set for offline files.
    bool damaged = false;
    // When the scanner first found the file (Unix ms); 0 = before the library kept this (and the
    // whole first scan of a folder), so a new library doesn't call everything new.
    int64_t firstSeen = 0;
    std::wstring searchKey;  // SearchFold(name + title + artist + album + genre)

    // Title tag when present, else the file name.
    const std::wstring& DisplayTitle() const { return title.empty() ? name : title; }
};

struct FolderNode {
    std::wstring path;
    std::wstring name;
    int parent = -1;            // index in Snapshot::folders, -1 for library roots
    std::vector<int> children;  // folder indices, natural name order
    std::vector<int> items;     // item indices directly inside, natural name order
    uint32_t videos = 0, audios = 0;            // directly inside
    uint32_t totalVideos = 0, totalAudios = 0;  // including subfolders
};

struct ScanStats {
    size_t files = 0;
    size_t folders = 0;
    double enumerateMs = 0;
    double metadataMs = 0;
    size_t metadataFiles = 0;       // files whose properties were read (not from cache)
    double metadataMsPerFile = 0;   // measured average
    bool metadataSecondPass = false;  // true when the per-file cost was > 3 ms or the rest > 300 ms
};

// Track = album order: album (untagged last), disc, track number (numbered first), then title.
enum class SortKey { Name, Date, Size, Duration, Track };

class Snapshot {
public:
    uint64_t generation = 0;
    bool complete = false;  // metadata pass done (or disabled)
    std::vector<std::wstring> roots;
    // Roots that were not reachable: their files are the cached ones, offline (a drive unplugged).
    std::vector<std::wstring> missingRoots;
    std::vector<MediaItem> items;
    std::vector<FolderNode> folders;
    ScanStats stats;
    int64_t newestFirstSeen = 0;  // max MediaItem::firstSeen

    int FindFolder(std::wstring_view path) const;  // index or -1
    const FolderNode* Folder(std::wstring_view path) const;
    const MediaItem* FindItem(std::wstring_view path) const;

    std::vector<const FolderNode*> Roots() const;  // library roots with files (or cached ones)
    std::vector<const FolderNode*> Children(std::wstring_view folder) const;
    std::vector<const MediaItem*> ItemsIn(std::wstring_view folder, bool recursive) const;
    std::vector<const MediaItem*> Videos() const;
    std::vector<const MediaItem*> Music() const;
    // Case-insensitive (Cyrillic-aware, "ё" = "е") search in names and tags: every word of the
    // query must appear (FoldedContains).
    // kind = Other searches everything.
    std::vector<const MediaItem*> Search(std::wstring_view query, MediaKind kind = MediaKind::Other) const;

    void BuildIndex();  // called by the scanner after filling the vectors

private:
    void CollectItems(int folder, bool recursive, std::vector<const MediaItem*>& out) const;

    std::unordered_map<std::wstring, int> folderIndex_;  // PathKey -> folder
    std::unordered_map<std::wstring, int> itemIndex_;    // PathKey -> item
};

// Natural name order uses StrCmpLogicalW (like Explorer); ties fall back to name.
// Duration sorts unknown durations last.
void SortItems(std::vector<const MediaItem*>& items, SortKey key, bool descending = false);

// A TV show: the episodes ParseEpisodePath finds, grouped by show name (case-insensitive), so
// "Show\Season 1" and "Show\Season 2" are one show.
struct Episode {
    const MediaItem* item = nullptr;
    EpisodeInfo info;
};
struct Show {
    std::wstring name;
    std::wstring key;     // SearchFold(name): identity, route argument
    std::wstring folder;  // the first episode's folder, or the one above it when that is a season
    std::vector<Episode> episodes;  // season, then episode order
};
// Shows among `videos` in name order; the videos that aren't episodes go to *movies (in input order).
std::vector<Show> GroupShows(const std::vector<const MediaItem*>& videos, std::vector<const MediaItem*>* movies = nullptr);
// The episode to watch next: the one after the last watched, else the first unwatched; -1 when
// every episode is watched.
int NextUp(const std::vector<bool>& watched);

// A file as a folder listing gives it, for the queue built from a folder.
struct FileEntry {
    std::wstring path;
    uint64_t modified = 0, created = 0, size = 0;  // FILETIME ticks, bytes
};
// order: "name", "date" (modified), "created" or "size", with ":desc" for the reverse; anything
// else is "name". Ties and names go in natural order (Explorer's "file 2" before "file 10").
void SortFileEntries(std::vector<FileEntry>& files, std::string_view order);

// What a running scan has done, for the progress banner. Read from any thread.
struct ScanProgress {
    bool scanning = false;
    bool counting = false;  // still enumerating: `total` = files found so far
    size_t done = 0;        // files whose metadata is known (cached or read)
    size_t total = 0;       // files found
    double elapsedMs = 0;   // since the scan started
};

struct ScanOptions {
    bool includeHidden = false;
    bool readMetadata = true;
};

class MediaLibrary {
public:
    MediaLibrary();
    ~MediaLibrary();  // cancels and joins the scan thread
    MediaLibrary(const MediaLibrary&) = delete;
    MediaLibrary& operator=(const MediaLibrary&) = delete;

    // Root folders, persisted in %APPDATA%\Lumen\library.json.
    bool Load();
    bool Save() const;
    const std::vector<std::wstring>& Roots() const { return roots_; }
    bool AddRoot(const std::wstring& folder);     // false if missing or already covered
    bool RemoveRoot(const std::wstring& folder);  // false if not a root
    void SetRoots(std::vector<std::wstring> folders);

    void SetOptions(const ScanOptions& options) { options_ = options; }

    // After each publish: PostMessageW(hwnd, msg, complete ? 1 : 0, generation).
    void SetNotifyWindow(HWND hwnd, UINT msg);

    // Starts a scan of the current roots on a background thread, cancelling a running one.
    // Returns the generation of the new scan (0 if the thread could not be started).
    uint64_t Rescan();
    void Cancel();
    bool Scanning() const { return scanning_.load(); }
    ScanProgress Progress() const;
    uint64_t Generation() const { return generation_.load(); }

    // "New" badges: files first found after the user last looked at the library. The threshold
    // is fixed for the session (the time saved in library.json); MarkSeen, called when a library
    // page shows the current snapshot, saves "now" so these files aren't new next session.
    int64_t NewSince() const { return newSince_; }
    bool IsNew(const MediaItem& m) const { return m.firstSeen > newSince_; }
    void MarkSeen();

    // Latest published snapshot; never null (empty before the first scan).
    std::shared_ptr<const Snapshot> Current() const;

private:
    void ScanThread(uint64_t generation, std::vector<std::wstring> roots, ScanOptions options,
                    const std::atomic<bool>& cancel);
    void Publish(std::shared_ptr<const Snapshot> snapshot);

    std::vector<std::wstring> roots_;
    ScanOptions options_;
    std::atomic<HWND> notifyWnd_{nullptr};
    std::atomic<UINT> notifyMsg_{0};

    mutable std::mutex mutex_;  // guards current_
    std::shared_ptr<const Snapshot> current_;
    std::thread thread_;                          // the latest scan; it joins the one before it
    std::shared_ptr<std::atomic<bool>> cancel_;   // the latest scan's cancel flag
    std::atomic<bool> scanning_{false};
    std::atomic<uint64_t> generation_{0};
    int64_t newSince_ = 0;  // Unix ms
    int64_t seenSaved_ = 0;  // "seen" in library.json
    bool readOnly_ = false;  // library.json exists but could not be read: never overwrite it

    // Progress of the latest scan.
    std::atomic<int64_t> scanStartMs_{0};  // steady clock
    std::atomic<bool> counting_{false};
    std::atomic<size_t> progressDone_{0}, progressTotal_{0};
};

}  // namespace library

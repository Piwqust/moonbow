#pragma once
// Media library: root folders, a background scan and an immutable snapshot of the results.
//
// Threading: every method is meant for the UI thread. The scan runs on its own thread and
// publishes a new `Snapshot` (std::shared_ptr<const Snapshot>), then posts the notify message.
// A snapshot never changes after it is published, so the UI may keep one as long as it likes
// and read it without locks; Current() swaps under a short mutex.
//
// Scan order: (1) enumerate files (FindFirstFileExW + FIND_FIRST_EX_LARGE_FETCH, recursive,
// hidden/system skipped) and publish; (2) read duration / title / artist / album through the
// Windows property system and publish again. Step 2 is skipped for files whose size and
// modification time match library-cache.json. If the first files show that reading properties
// costs <= 3 ms per file, steps 1 and 2 run back to back with a single publish.

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
    bool offline = false;    // cloud placeholder (OneDrive etc.): content not on this disk
    std::wstring searchKey;  // SearchFold(name + title + artist + album)

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
    bool metadataSecondPass = false;  // true when the per-file cost was > 3 ms
};

enum class SortKey { Name, Date, Size, Duration };

class Snapshot {
public:
    uint64_t generation = 0;
    bool complete = false;  // metadata pass done (or disabled)
    std::vector<std::wstring> roots;
    std::vector<MediaItem> items;
    std::vector<FolderNode> folders;
    ScanStats stats;

    int FindFolder(std::wstring_view path) const;  // index or -1
    const FolderNode* Folder(std::wstring_view path) const;
    const MediaItem* FindItem(std::wstring_view path) const;

    std::vector<const FolderNode*> Roots() const;  // library roots that exist
    std::vector<const FolderNode*> Children(std::wstring_view folder) const;
    std::vector<const MediaItem*> ItemsIn(std::wstring_view folder, bool recursive) const;
    std::vector<const MediaItem*> Videos() const;
    std::vector<const MediaItem*> Music() const;
    // Case-insensitive (Cyrillic-aware, "ё" = "е") substring search in names and tags.
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

    // Latest published snapshot; never null (empty before the first scan).
    std::shared_ptr<const Snapshot> Current() const;

private:
    void ScanThread(uint64_t generation, std::vector<std::wstring> roots, ScanOptions options);
    void Publish(std::shared_ptr<const Snapshot> snapshot);

    std::vector<std::wstring> roots_;
    ScanOptions options_;
    std::atomic<HWND> notifyWnd_{nullptr};
    std::atomic<UINT> notifyMsg_{0};

    mutable std::mutex mutex_;  // guards current_
    std::shared_ptr<const Snapshot> current_;
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> scanning_{false};
    uint64_t generation_ = 0;
};

}  // namespace library

#pragma once
// Recently played items: %APPDATA%\Lumen\history.json, newest first, at most 500 entries.
//
// UI thread only. Changes mark the store dirty; call Flush() from a timer / at exit (the
// destructor flushes when Load() was called).

#include "MediaTypes.h"

#include <string>
#include <vector>

namespace library {

struct HistoryEntry {
    std::wstring path;
    std::wstring title;
    MediaKind kind = MediaKind::Other;
    int64_t lastPlayed = 0;  // Unix ms
    double position = 0;     // seconds
    double duration = 0;     // seconds; 0 = unknown
    uint32_t playCount = 0;

    double Progress() const { return duration > 0 ? position / duration : 0; }
};

struct FolderStat {
    std::wstring folder;
    uint32_t plays = 0;
    int64_t lastPlayed = 0;
};

class History {
public:
    static constexpr size_t kMaxEntries = 500;

    History() = default;
    ~History();
    History(const History&) = delete;
    History& operator=(const History&) = delete;

    bool Load();  // missing file = empty history
    bool Save();
    bool Flush() { return !dirty_ || Save(); }
    void MarkDirty() { dirty_ = true; }
    bool Dirty() const { return dirty_; }

    // privacy.excludePatterns: "*\Private\*;*.tmp.mkv" (PathMatchSpecEx, ';'-separated).
    void SetExcludePatterns(std::wstring patterns) { exclude_ = std::move(patterns); }
    bool IsExcluded(const std::wstring& path) const;

    // A file started playing: moves it to the front, counts the play. Empty title = file name.
    // Returns false (and records nothing) for excluded paths.
    bool RecordPlay(const std::wstring& path, const std::wstring& title = {}, double duration = 0);
    // Periodic / on-close position update. Creates the entry if needed (without counting a play).
    bool UpdatePosition(const std::wstring& path, double position, double duration);

    const HistoryEntry* Find(const std::wstring& path) const;
    const std::vector<HistoryEntry>& Entries() const { return entries_; }  // newest first
    size_t size() const { return entries_.size(); }

    // kind = Other returns every kind.
    std::vector<HistoryEntry> Recent(size_t max = 50, MediaKind kind = MediaKind::Other) const;
    // Videos stopped between 2% and 95%, newest first.
    std::vector<HistoryEntry> ContinueWatching(size_t max = 20) const;
    // Audio, newest first.
    std::vector<HistoryEntry> ListenAgain(size_t max = 20) const;
    // Parent folders by total play count, then by recency.
    std::vector<FolderStat> FrequentFolders(size_t max = 10) const;

    bool Remove(const std::wstring& path);
    void Clear();

private:
    HistoryEntry* Touch(const std::wstring& path);  // find or create, move to front

    std::vector<HistoryEntry> entries_;
    std::wstring exclude_;
    bool dirty_ = false;
    bool loaded_ = false;
};

}  // namespace library

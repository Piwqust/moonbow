#pragma once
// Per-file bookmarks (%APPDATA%\Lumen\bookmarks.json) and favorite paths
// (%APPDATA%\Lumen\favorites.json). Paths compare case-insensitively.
//
// UI thread only. Changes mark the store dirty; call Flush() from a timer / at exit (the
// destructors flush when Load() was called).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace library {

struct Bookmark {
    double time = 0;  // seconds
    std::wstring name;    // may be empty: the UI shows "Bookmark N" / "Закладка N"
    int64_t created = 0;  // Unix ms
};

class Bookmarks {
public:
    Bookmarks() = default;
    ~Bookmarks();
    Bookmarks(const Bookmarks&) = delete;
    Bookmarks& operator=(const Bookmarks&) = delete;

    bool Load();
    bool Save();
    bool Flush() { return !dirty_ || Save(); }
    bool Dirty() const { return dirty_; }

    // Sorted by time. Empty when the file has none.
    std::vector<Bookmark> For(const std::wstring& path) const;
    // Returns the index of the new bookmark in For(path).
    size_t Add(const std::wstring& path, double time, const std::wstring& name = {});
    bool Rename(const std::wstring& path, size_t index, const std::wstring& name);
    bool Remove(const std::wstring& path, size_t index);
    void ClearFile(const std::wstring& path);
    size_t FileCount() const { return files_.size(); }

private:
    struct FileMarks {
        std::wstring path;
        std::vector<Bookmark> marks;  // sorted by time
    };
    std::unordered_map<std::wstring, FileMarks> files_;  // PathKey -> marks
    bool dirty_ = false;
    bool loaded_ = false;
};

class Favorites {
public:
    Favorites() = default;
    ~Favorites();
    Favorites(const Favorites&) = delete;
    Favorites& operator=(const Favorites&) = delete;

    bool Load();
    bool Save();
    bool Flush() { return !dirty_ || Save(); }
    bool Dirty() const { return dirty_; }

    bool Contains(const std::wstring& path) const;
    bool Add(const std::wstring& path);     // false if already there
    bool Remove(const std::wstring& path);  // false if not there
    bool Toggle(const std::wstring& path);  // returns the new state
    const std::vector<std::wstring>& All() const { return paths_; }  // in the order added

private:
    std::vector<std::wstring> paths_;
    bool dirty_ = false;
    bool loaded_ = false;
};

}  // namespace library

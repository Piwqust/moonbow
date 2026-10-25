#pragma once
// User playlists: %APPDATA%\Moonbow\playlists.json, plus .m3u / .m3u8 import and export.
//
// UI thread only. Changes mark the store dirty; call Flush() from a timer / at exit (the
// destructor flushes when Load() was called).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace library {

struct PlaylistItem {
    std::wstring path;   // file path or URL
    std::wstring title;  // may be empty: show the file name
    double duration = 0; // seconds; 0 = unknown
};

struct Playlist {
    std::string id;  // stable, generated: "pl-..."
    std::wstring name;
    int64_t created = 0;   // Unix ms
    int64_t modified = 0;  // Unix ms
    std::vector<PlaylistItem> items;
};

class Playlists {
public:
    Playlists() = default;
    ~Playlists();
    Playlists(const Playlists&) = delete;
    Playlists& operator=(const Playlists&) = delete;

    bool Load();
    bool Save();
    bool Flush() { return !dirty_ || Save(); }
    bool Dirty() const { return dirty_; }

    const std::vector<Playlist>& All() const { return lists_; }  // creation order
    const Playlist* Find(std::string_view id) const;

    std::string Create(const std::wstring& name);  // returns the new id
    bool Rename(std::string_view id, const std::wstring& name);
    bool Delete(std::string_view id);
    // Undo of Delete: the playlist comes back with its id at `index`. False if the id exists.
    bool Restore(Playlist p, size_t index);

    // insertAt < 0 or past the end appends.
    bool Add(std::string_view id, const std::vector<PlaylistItem>& items, int insertAt = -1);
    bool AddPaths(std::string_view id, const std::vector<std::wstring>& paths, int insertAt = -1);
    bool RemoveAt(std::string_view id, size_t index);
    bool RemoveIndices(std::string_view id, std::vector<size_t> indices);
    // Moves the item at `from` so that it ends up at index `to` (drag and drop).
    bool Move(std::string_view id, size_t from, size_t to);
    bool Clear(std::string_view id);

    // Creates a playlist from an .m3u/.m3u8 file (named after the file when `name` is empty).
    // Returns the new id, or "" when the file cannot be read.
    std::string Import(const std::wstring& m3uPath, const std::wstring& name = {});
    bool Export(std::string_view id, const std::wstring& m3uPath) const;

private:
    Playlist* Mutable(std::string_view id);
    void Touch(Playlist& p);

    std::vector<Playlist> lists_;
    bool dirty_ = false;
    bool loaded_ = false;
    bool readOnly_ = false;  // the file exists but could not be read: never overwrite it
    uint32_t counter_ = 0;
};

// Extended M3U. Relative entries are resolved against the playlist's folder; URLs are kept.
// .m3u files without a BOM that are not valid UTF-8 are read in `codePage` (0: the system's
// legacy ANSI code page, json::LegacyCodePage).
bool ReadM3u(const std::wstring& path, std::vector<PlaylistItem>* items, unsigned codePage = 0);
// "#EXTM3U" + "#EXTINF:<seconds>,<title>" lines, UTF-8 without BOM, CRLF.
std::string FormatM3u(const std::vector<PlaylistItem>& items);

}  // namespace library

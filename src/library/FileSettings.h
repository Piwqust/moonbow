#pragma once
// Per-file settings (playback.perFile): what the user changed for one file (tracks, delays and,
// with playback.perFileAll, speed, picture transforms, deinterlacing, the A-B loop and the
// subtitle position and size), restored the next time that file opens.
// %APPDATA%\Lumen\file-settings.json, at most 5000 files, the least recently used go first.
//
// UI thread only. A file is identified by its path (PathKey: case-insensitive). Changes mark the
// store dirty; call Flush() from a timer / at exit (the destructor flushes when Load() was called).
// Like the other stores: a file that can't be read is never overwritten, one of the wrong shape
// is backed up as file-settings.corrupt-<time>.json.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace library {

// Every field is optional: unset = the file opens with Lumen's defaults for it.
struct FileOptions {
    // mpv track ids, 0 = off. A sidecar track is kept by its file instead (its id is only the
    // order it was added in).
    std::optional<int64_t> aid, sid, secondarySid;
    std::optional<std::wstring> audioFile, subFile;
    std::optional<double> audioDelay, subDelay;  // seconds
    // playback.perFileAll
    std::optional<double> speed;
    std::optional<std::string> aspect;    // "16:9" ... (video-aspect-override)
    std::optional<std::string> zoomMode;  // quarter, half, original, double (LiveState::zoomMode)
    std::optional<double> zoom;           // free zoom (video-zoom, log2) when zoomMode is unset
    std::optional<double> panX, panY;
    std::optional<double> rotate;  // degrees
    std::optional<int> flip;       // 1 horizontal, 2 vertical
    std::optional<std::string> deinterlace;  // mpv's yes / no / auto
    std::optional<double> abA, abB;          // A-B loop points, seconds
    std::optional<double> subPos, subScale;  // sub-pos (%), sub-scale (1 = 100%)

    bool Empty() const { return *this == FileOptions{}; }
    bool HasTracks() const { return aid || sid || secondarySid || audioFile || subFile; }
    bool HasBasic() const { return HasTracks() || audioDelay || subDelay; }  // what playback.perFile covers
    bool operator==(const FileOptions&) const = default;
};

// The audio and subtitle languages last chosen in a series, so its other episodes open with
// them, also in a later session (app/player NextEpisodeCard). Languages as the file names them
// ("rus"); the track title tells two tracks of one language apart.
struct ShowLanguages {
    std::wstring audioLang, audioTitle, subLang, subTitle;
    bool subsOff = false;
    bool Empty() const { return audioLang.empty() && subLang.empty() && !subsOff; }
    bool operator==(const ShowLanguages&) const = default;
};

class FileSettings {
public:
    static constexpr size_t kMaxEntries = 5000;
    static constexpr size_t kMaxShows = 500;

    FileSettings() = default;
    ~FileSettings();
    FileSettings(const FileSettings&) = delete;
    FileSettings& operator=(const FileSettings&) = delete;

    bool Load();  // missing file = empty store
    bool Save();
    bool Flush();
    bool Loaded() const { return loaded_; }
    bool Dirty() const { return dirty_; }

    // O(1). nullptr when nothing is stored for the file.
    const FileOptions* Find(const std::wstring& path) const;
    // Stores `options` for the file (Empty() removes it) and makes it the most recently used.
    // Returns true when the stored value changed.
    bool Put(const std::wstring& path, const FileOptions& options);
    // The file was opened: it becomes the most recently used (saved with the next write).
    void Touch(const std::wstring& path);
    bool Remove(const std::wstring& path);
    void Clear();  // the files and the shows
    size_t size() const { return entries_.size(); }

    // By the show's key (its name or folder, as the caller builds it; case-insensitive).
    const ShowLanguages* FindShow(const std::wstring& show) const;
    // Empty() removes it. Returns true when the stored value changed.
    bool PutShow(const std::wstring& show, const ShowLanguages& langs);

private:
    struct Entry {
        std::wstring path;
        int64_t used = 0;  // Unix ms, strictly increasing within the store
        FileOptions options;
    };
    int64_t NextStamp();
    void Evict();

    struct Show {
        int64_t used = 0;
        ShowLanguages langs;
    };

    std::unordered_map<std::wstring, Entry> entries_;  // by PathKey
    std::unordered_map<std::wstring, Show> shows_;     // by PathKey of the show key
    int64_t lastStamp_ = 0;
    bool dirty_ = false;
    bool loaded_ = false;
    bool readOnly_ = false;  // the file exists but could not be read: never overwrite it
};

}  // namespace library

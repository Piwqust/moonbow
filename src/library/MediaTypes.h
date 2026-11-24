#pragma once
// File classification and path helpers shared by the library services.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace library {

enum class MediaKind : uint8_t { Other, Video, Audio };

// By extension. The lists extend IsMediaFile() in src/Util.cpp (same video/audio set plus
// less common containers such as .mka, .ape, .wv, .divx, .mxf).
MediaKind KindOf(std::wstring_view path);
bool IsMediaPath(std::wstring_view path);  // Video or Audio
bool IsSubtitlePath(std::wstring_view path);
bool IsPlaylistPath(std::wstring_view path);
// A DVD or Blu-ray to play as a disc (menus included): an .iso image, a disc folder (one with
// VIDEO_TS or BDMV in it, or that folder itself) or the disc's VIDEO_TS.IFO / MovieObject.bdmv.
// Returns what to give mpv, which recognizes these and plays the disc; empty otherwise. Looks at
// the file system for folders.
std::wstring DiscEntry(const std::wstring& path);

// "video" / "audio" / "other" for JSON files, and back.
const char* KindName(MediaKind kind);
MediaKind KindFromName(std::string_view name);

// Forward slashes become backslashes and trailing separators are dropped ("C:\" keeps its
// backslash). Nothing else is touched: no case change, no resolving of "..".
std::wstring NormalizePath(std::wstring_view path);

// Case-insensitive identity of a path (normalized + upper-cased like NTFS compares names).
std::wstring PathKey(std::wstring_view path);

std::wstring ParentFolder(std::wstring_view path);  // "D:\Video\a.mkv" -> "D:\Video"
std::wstring FileNameOf(std::wstring_view path);    // "a.mkv"
std::wstring StemOf(std::wstring_view path);        // "a"

// Folding for search: linguistic lower case (Cyrillic included) and "ё" -> "е".
std::wstring SearchFold(std::wstring_view text);
// Search match on already folded strings: every space-separated word of the query is a substring
// of the text, in any order, so "artist song" and "song 3" find what people mean.
inline bool FoldedContains(const std::wstring& foldedText, const std::wstring& foldedQuery) {
    for (size_t i = 0, end = 0; i < foldedQuery.size(); i = end + 1) {
        end = foldedQuery.find(L' ', i);
        if (end == std::wstring::npos) end = foldedQuery.size();
        if (end > i && foldedText.find(foldedQuery.data() + i, 0, end - i) == std::wstring::npos) return false;
    }
    return true;
}

// Where the words of a folded query occur in `text` (every occurrence, merged and sorted), as
// {start, length}: what a result list underlines. Empty when folding changes the text length.
std::vector<std::pair<size_t, size_t>> MatchRanges(std::wstring_view text, const std::wstring& foldedQuery);

// "Show S01E02", "Show.1x02", "s1e2 Title" -> show "Show", season 1, episode 2. The show is the
// text before the marker with dots/underscores read as spaces (empty when the name starts with it).
struct EpisodeInfo {
    std::wstring show;
    int season = 0;
    int episode = 0;
    std::wstring title;  // text after the marker up to release tags ("Pilot" in "Show.S01E01.Pilot.1080p")
};
bool ParseEpisode(std::wstring_view name, EpisodeInfo* out);
// A word of a release name that ends the title: "1080p", "WEB-DL", "x264", "Remastered"...
// Case and dashes are ignored.
bool IsReleaseTag(std::wstring_view word);
// Card title of an episode file: "S01E02 · Pilot", "S01E02 · Show" when the name has no episode
// title, or empty when the name is not an episode.
std::wstring EpisodeLabel(std::wstring_view name);
// Season of a folder name: "Season 2", "S02", "Сезон 2", "2 сезон", "Show.Season.2", "Show S01 1080p"
// -> 2 / 1, with the text before the marker in *show ("Show", may be empty). -1 when the name is
// not a season.
int ParseSeasonFolder(std::wstring_view name, std::wstring* show);
// An episode by its path: ParseEpisode of the file name, else a numbered file in a season folder
// ("Show\Season 2\03 Title.mkv", "E03", "Episode 3", "Серия 3"). When the name has no show, the
// show is the folder's: the text before a season folder's marker, the folder above a season
// folder, or the file's folder.
bool ParseEpisodePath(std::wstring_view path, EpisodeInfo* out);

// The user's word about a folder (the library's "Series settings"), which ParseEpisodePath follows
// before reading names: its videos are episodes of `show`, season `season` (the episode number
// from the name, else the first number in it), or with `notSeries` films. Thread-safe.
struct SeriesOverride {
    std::wstring show;
    int season = 1;
    bool notSeries = false;
};
void SetSeriesOverride(const std::wstring& folder, const SeriesOverride* o);  // null removes it
bool SeriesOverrideFor(std::wstring_view folder, SeriesOverride* out);
std::vector<std::pair<std::wstring, SeriesOverride>> SeriesOverrides();  // folder paths as given

// How well a subtitle file name fits a video (letters and digits only, case-insensitive): 100 same
// name, 90 the subtitle adds a suffix ("Show_E01_English"), 70 the video adds one, 60 same
// SxxEyy / NxNN episode, 0 no match. "lecture1" never matches "lecture10".
int SubtitleMatchScore(const std::wstring& video, const std::wstring& sub);

// Disc number of a "part of set" tag: "2/3" -> 2, "1" -> 1; 0 when there is no number.
int ParseDiscNumber(std::wstring_view partOfSet);

// False when the first bytes of a file clearly aren't the container its extension promises
// (a text file named .mkv, an empty .mp4). Unknown extensions pass.
bool HeaderMatchesExtension(std::wstring_view path, const unsigned char* head, size_t size);
// True when the first bytes are those of a container or stream mpv plays whatever the file is
// called (an MP3 named .mkv): such a file is mislabelled, not damaged.
bool HeaderLooksLikeMedia(const unsigned char* head, size_t size);

// Milliseconds since 1970-01-01 UTC.
int64_t UnixMsNow();
int64_t FileTimeToUnixMs(uint64_t fileTime100ns);

}  // namespace library

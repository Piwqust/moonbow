#pragma once
#include <string>
#include <string_view>
#include <vector>

// Song lyrics for the music view: an LRC file next to the track or an embedded LYRICS /
// USLT tag. Parsed once when a track loads, never while painting.
namespace app {

struct LyricLine {
    double time = -1;  // seconds; -1 in unsynced lyrics
    std::wstring text;
};

struct Lyrics {
    bool synced = false;
    std::vector<LyricLine> lines;  // sorted by time when synced

    bool Empty() const { return lines.empty(); }
    // The line sung at `t` (the last one that started), -1 before the first or when unsynced.
    int LineAt(double t) const;
};

// LRC: "[mm:ss.xx]text" with any number of stamps per line ("[00:12.00][01:30.5]chorus"),
// "[offset:+250]" in milliseconds, ID tags ("[ar:...]") skipped, enhanced-LRC word stamps
// ("<00:12.30>") removed. Text without a single time stamp becomes unsynced lyrics.
Lyrics ParseLyrics(std::wstring_view text);

// The bytes of a .lrc file as text: UTF-8 (with or without BOM), UTF-16 with a BOM, otherwise
// `codePage` (0: the system's legacy ANSI code page; older Windows tools wrote CP-1251 files).
std::wstring DecodeLyricsFile(std::string_view bytes, unsigned codePage = 0);

// "<folder>\<name>.lrc" for a local media file, empty for URLs and paths without a name.
std::wstring LyricsPathFor(const std::wstring& mediaPath);

// Reads and parses a .lrc file (up to 1 MB); empty lyrics when it is missing or unreadable.
// Blocking file I/O: call it off the UI thread.
Lyrics LoadLyricsFile(const std::wstring& lrcPath);

// True for the tag names that carry lyrics: "lyrics", "lyrics-eng" (ID3 USLT as FFmpeg names
// it), "UNSYNCEDLYRICS" (foobar2000 / Mp3tag in Vorbis comments), any case.
bool IsLyricsTag(std::string_view key);

}  // namespace app

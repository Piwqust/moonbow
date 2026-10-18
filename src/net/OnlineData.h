#pragma once
// The pure parts of the online features, unit-tested in tests/core_tests.cpp: the OpenSubtitles
// moviehash, guessing a movie or an episode from a file name, request query strings and the
// mapping of each service's JSON to Lumen's records. No I/O except MovieHash().
#include "../core/Json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace net {

// --- OpenSubtitles moviehash ---------------------------------------------------------------
// size + the 64-bit little-endian words of the first and the last 64 KiB, modulo 2^64.
// `head` and `tail` are those blocks (each 65536 bytes; a file needs at least 131072).
uint64_t MovieHashOf(uint64_t size, const unsigned char* head, const unsigned char* tail);
// Reads the file (blocking: call it on a worker). False for files under 128 KiB or unreadable ones.
bool MovieHash(const std::wstring& path, uint64_t* hash, uint64_t* size = nullptr);
std::string HashHex(uint64_t hash);  // 16 lower-case hex digits

// --- what a video file is -------------------------------------------------------------------
struct VideoGuess {
    enum Kind : uint8_t { None, Movie, Episode };
    Kind kind = None;
    std::wstring title;  // movie title or show name, words separated by spaces
    int year = 0;        // movies: release year from the name (0 = none)
    int season = 0, episode = 0;
};
// "Inception.2010.1080p.BluRay.x264.mkv" -> Movie "Inception" 2010; "The Matrix (1999).mkv" ->
// Movie "The Matrix" 1999; "Show.Name.S01E02.mkv" -> Episode "Show Name" 1x2 (a name without the
// show takes it from the folder, skipping "Season 1" folders). A name without a year is a Movie
// with year 0 (looked up only on request).
VideoGuess GuessVideo(const std::wstring& path);
// The title part of a release name: dots and underscores as spaces, cut at the year or the first
// release tag ("1080p", "BluRay", "x264", "WEB-DL"...). `year` receives the year when found.
std::wstring CleanTitle(const std::wstring& name, int* year = nullptr);

// --- LRCLIB -----------------------------------------------------------------------------------
struct LyricsAnswer {
    bool found = false;
    bool instrumental = false;
    bool synced = false;
    std::wstring text;  // LRC when synced, else plain lines
    double duration = 0;
};
// One record of /api/get (or of the /api/search array).
LyricsAnswer ParseLrclibRecord(const json::Value& v);
// The best record of a /api/search answer: synced lyrics first, then the closest duration (within
// 3 s when `duration` > 0). -1 when none fits.
int PickLrclibRecord(const json::Value& array, double duration);
// "/api/get?artist_name=...&track_name=...&album_name=...&duration=..." (album/duration optional).
std::wstring LrclibGetPath(const std::wstring& artist, const std::wstring& title, const std::wstring& album,
                           double duration);
// An .lrc file for a saved result: ID tags, then the text.
std::string LrcFileText(const LyricsAnswer& a, const std::wstring& artist, const std::wstring& title,
                        const std::wstring& album);

// --- OpenSubtitles ----------------------------------------------------------------------------
struct SubtitleQuery {
    std::string moviehash;          // 16 hex, optional
    std::wstring query;             // file name or title, optional
    int season = 0, episode = 0;    // 0 = not given
    std::vector<std::string> languages;  // "en", "ru"... (any order; sent sorted)
};
// The query string for GET /subtitles: parameters sorted, values lower case, "+" for spaces, no
// defaults (their redirect-free form). Starts with "?" (empty when there is nothing to send).
std::wstring SubtitleQueryString(const SubtitleQuery& q);

struct SubtitleResult {
    int64_t fileId = 0;
    std::wstring release, fileName, language, title;
    int year = 0;
    int downloads = 0;
    double rating = 0;  // 0..10, 0 = no votes
    int votes = 0;
    bool hearingImpaired = false, foreignOnly = false, hashMatch = false, trusted = false, machine = false,
         ai = false;
};
std::vector<SubtitleResult> ParseSubtitleSearch(const json::Value& doc);

struct SubtitleDownload {
    std::wstring link, fileName, message, resetTime;
    int remaining = -1, requests = -1;
};
SubtitleDownload ParseSubtitleDownload(const json::Value& doc);

struct SubtitleLogin {
    std::string token;
    std::wstring baseUrl;  // "vip-api.opensubtitles.com" for VIP accounts, may be empty
    int allowed = -1;
    bool vip = false;
};
SubtitleLogin ParseSubtitleLogin(const json::Value& doc);
// /infos/user: remaining downloads today, -1 when unknown.
int ParseSubtitleQuota(const json::Value& doc);

// "<video stem>.<lang>.<ext of fileName>" ("srt" when it has none).
std::wstring SubtitleFileName(const std::wstring& videoPath, const std::wstring& language, const std::wstring& fileName);

// --- TMDB -------------------------------------------------------------------------------------
struct MetaMatch {
    bool tv = false;
    int64_t id = 0;
    std::wstring title;
    int year = 0;
    std::wstring overview;
    double rating = 0;  // vote_average
    std::string poster, backdrop;  // "/abc.jpg" image paths
};
std::vector<MetaMatch> ParseTmdbSearch(const json::Value& doc, bool tv);
// The result to take for a guess: the first with the same year (±1) when the guess has one,
// else the first. -1 for an empty list.
int PickTmdbMatch(const std::vector<MetaMatch>& results, const VideoGuess& guess);

struct EpisodeMeta {
    std::wstring name, overview;
    std::string still;
    double rating = 0;
    int year = 0;  // of air_date
};
EpisodeMeta ParseTmdbEpisode(const json::Value& doc);

// A TMDB authentication: v4 read access tokens (JWT, "eyJ...") go in an Authorization header,
// v3 API keys (32 hex) in the api_key parameter.
bool TmdbKeyIsBearer(const std::string& key);

int YearOf(const std::string& isoDate);  // "2010-07-15" -> 2010, 0 when empty

}  // namespace net

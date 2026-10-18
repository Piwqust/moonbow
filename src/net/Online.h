#pragma once
// Lumen's online features on top of net/Http.h, each off by default and opt-in in
// Settings › Privacy › Network, with the user's own API key where the service needs one:
//
// - LRCLIB (lrclib.net, no key): synced lyrics for songs that have none of their own.
// - OpenSubtitles (api.opensubtitles.com, key + optional login): subtitle search by moviehash,
//   name, season/episode and language; downloads saved next to the video.
// - TMDB (api.themoviedb.org / image.tmdb.org, key): posters, backdrops, overview, year and rating
//   for videos named like a movie ("Title 2010") or an episode ("Show S01E02").
//
// UI thread only. Nothing here starts a thread, a timer or a request until a feature is on and
// something asks for it; background lookups fail silently, user actions report their errors.
#include "app/Signal.h"
#include "net/OnlineData.h"

#include <functional>
#include <string>
#include <vector>

namespace ui {
class Host;
}

namespace net::online {

// Called once at startup (InitDialogs): keeps privacy.fetchMetadata and library.metadata in step.
void Init();

// --- settings -----------------------------------------------------------------------------------
bool LyricsOn();
bool SubtitlesOn();      // the toggle (the key may still be missing)
bool SubtitlesKeySet();
bool MetadataOn();       // toggle and key
bool MetadataToggle();   // the toggle alone
bool MetadataKeyRejected();  // TMDB answered 401 to the current key

// --- lyrics (LRCLIB) ----------------------------------------------------------------------------
struct TrackInfo {
    std::wstring path, artist, title, album;
    double duration = 0;
};
// Looks the song up when the feature is on and the track has an artist and a title; `done` runs
// on the UI thread (found = false when there is nothing). Returns 0 when nothing was asked.
uint64_t FetchLyrics(const TrackInfo& track, std::function<void(const LyricsAnswer&)> done);
// Writes "<song>.lrc" next to the file (never replaces one) and says how it went in a toast.
void SaveLyrics(const TrackInfo& track, const LyricsAnswer& lyrics, ui::Host* toastHost);

// --- subtitles (OpenSubtitles) ------------------------------------------------------------------
struct SubtitleSearch {
    std::wstring videoPath;  // local file: its moviehash is sent along
    std::wstring query;
    int season = 0, episode = 0;
    std::vector<std::string> languages;
};
struct SubtitleSearchResult {
    bool ok = false;
    std::wstring error;  // translated, for the dialog's status line
    bool usedHash = false;
    std::vector<SubtitleResult> items;
};
void SearchSubtitles(const SubtitleSearch& search, std::function<void(const SubtitleSearchResult&)> done);

struct SubtitleFetch {
    bool ok = false;
    std::wstring path;   // where the file was saved
    std::wstring error;  // translated
    bool inDataDir = false;  // the video's folder was not writable
};
void DownloadSubtitle(const SubtitleResult& item, const std::wstring& videoPath,
                      std::function<void(const SubtitleFetch&)> done);
// Downloads left today: from the last download answer, or asked from /infos/user when logged
// in. -1 when unknown. `done` gets the number (and an error text when the call failed).
void SubtitleQuota(std::function<void(int remaining, const std::wstring& error)> done);
int LastSubtitleQuota();
// The user's subtitle languages (Settings › Subtitles › Preferred language, then English).
std::vector<std::string> PreferredSubtitleLanguages();

// --- metadata (TMDB) ----------------------------------------------------------------------------
struct MediaMeta {
    enum class State : uint8_t { Queued, Matched, NotFound, Failed };
    State state = State::Queued;
    bool manual = false;  // chosen with "Find another match"
    bool tv = false;
    int64_t id = 0;
    std::wstring title, overview, episodeTitle;
    int year = 0, season = 0, episode = 0;
    double rating = 0;
    std::string poster, backdrop, still;  // TMDB image paths
    int64_t checked = 0;                  // unix seconds of the lookup
};
// The record of a library video, or null. Asks for a lookup (once) when the feature is on and
// the name reads as a movie with a year or an episode.
const MediaMeta* Meta(const std::wstring& path);
// Local image files once downloaded (empty until then; asking starts the download). Card: the
// episode still or the backdrop (16:9); poster: 2:3.
std::wstring CardImage(const std::wstring& path);
std::wstring PosterImage(const std::wstring& path);
// "2010 · ★ 8.4" for a matched record, empty otherwise.
std::wstring MetaSummary(const MediaMeta& m);

// Manual matching ("Find another match").
void SearchMetadata(const std::wstring& query, int year, bool tv,
                    std::function<void(const std::vector<MetaMatch>&, const std::wstring& error)> done);
void SetMatch(const std::wstring& path, const MetaMatch& match);
void ResetMeta(const std::wstring& path);  // forget the match; the next look finds it again
// What GuessVideo makes of the file (the dialogs prefill their search with it).
VideoGuess Guess(const std::wstring& path);

// Fires when lyrics, records or images change (views repaint).
app::Signal<>& Changed();

// --- privacy -----------------------------------------------------------------------------------
// Deletes the online caches, the metadata records and the downloaded images (Clear history,
// "Clear online data"). Subtitle files the user downloaded stay.
void ClearAll();

}  // namespace net::online

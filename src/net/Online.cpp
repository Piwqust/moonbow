#include "net/Online.h"

#include "Util.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "core/Paths.h"
#include "library/MediaTypes.h"
#include "net/Cache.h"
#include "net/Http.h"
#include "net/ServiceKeys.h"
#include "ui/Controls.h"
#include "ui/Host.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace net::online {

using app::Tr;
using app::TheApp;

namespace {

Config& Cfg() {
    return TheApp().config();
}

std::string Trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// The key a service gets: the one built into Moonbow (net/ServiceKeys.h), unless the hidden setting
// holds another (builds made without keys, test runs).
bool OwnKey(const char* setting) {
    return !Trimmed(Cfg().GetString(setting)).empty();
}
std::string OsKey() {
    return OwnKey("privacy.opensubtitlesKey") ? Trimmed(Cfg().GetString("privacy.opensubtitlesKey"))
                                               : keys::OpenSubtitles();
}
std::string TmdbKey() {
    return OwnKey("privacy.tmdbKey") ? Trimmed(Cfg().GetString("privacy.tmdbKey")) : keys::Tmdb();
}

// Test servers: the hidden privacy.*Server settings point a service at a local mock, and only in
// --automation runs (a hand-edited settings.json can't send requests anywhere else).
std::wstring Server(const char* key, const wchar_t* fallback) {
    if (ui::Host::Automation()) {
        std::wstring v = Cfg().GetWString(key);
        while (!v.empty() && v.back() == L'/') v.pop_back();
        if (!v.empty()) return v;
    }
    return fallback;
}

Request Base(const std::wstring& url) {
    Request r;
    r.url = url;
    r.allowLoopbackHttp = ui::Host::Automation();
    return r;
}

std::wstring LrclibBase() {
    return Server("privacy.lrclibServer", L"https://lrclib.net");
}
std::wstring OsBase() {
    return Server("privacy.opensubtitlesServer", L"https://api.opensubtitles.com/api/v1");
}
std::wstring TmdbBase() {
    return Server("privacy.tmdbServer", L"https://api.themoviedb.org/3");
}
std::wstring TmdbImages() {
    return Server("privacy.tmdbImageServer", L"https://image.tmdb.org/t/p");
}

// The hosts each feature may talk to. Download links of OpenSubtitles point at their own
// subdomains; anything else in an answer is refused.
bool OsHostAllowed(const std::wstring& url) {
    if (!UrlAllowed(url, ui::Host::Automation())) return false;
    const std::wstring host = HostOf(url);
    if (ui::Host::Automation() && (host == L"127.0.0.1" || host == L"localhost")) return true;
    auto ends = [&host](const std::wstring& suffix) {
        return host == suffix.substr(1) || (host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0);
    };
    return ends(L".opensubtitles.com") || ends(L".opensubtitles.org");
}

void ToastOnMain(const std::wstring& text, const char* icon) {
    for (ui::Host* h : ui::Host::All())
        if (h->IsActive()) {
            ui::ShowToast(h, text, icon, 2600);
            return;
        }
    if (TheApp().main()) ui::ShowToast(TheApp().main(), text, icon, 2600);
}

app::Signal<>& ChangedSignal() {
    static app::Signal<> s;
    return s;
}

// A translated reason for a failed answer of a user action.
std::wstring Failure(const Response& r, const wchar_t* service) {
    switch (r.error) {
        case Error::Offline:
        case Error::Network:
            return app::Fmt(Tr(L"No connection to {0}. Check the internet connection."), {service});
        case Error::Timeout:
            return app::Fmt(Tr(L"{0} did not answer in time. Try again later."), {service});
        case Error::TooLarge:
            return app::Fmt(Tr(L"{0} sent an unexpectedly large answer."), {service});
        case Error::Blocked:
            return app::Fmt(Tr(L"{0} sent an address Moonbow does not open."), {service});
        case Error::File:
            return Tr(L"Couldn't save the file.");
        default:
            break;
    }
    if (r.status == 401 || r.status == 403) {
        const bool own = std::wstring_view(service) == L"TMDB" ? OwnKey("privacy.tmdbKey")
                                                                : OwnKey("privacy.opensubtitlesKey");
        return own ? app::Fmt(Tr(L"{0} rejected the API key. Check it in Settings › Privacy › Online services."), {service})
                   : app::Fmt(Tr(L"{0} refused Moonbow's key. Try again later or update Moonbow."), {service});
    }
    if (r.status == 429) {
        std::string wait = r.Header("retry-after");
        if (wait.empty()) wait = r.Header("ratelimit-reset");
        return app::Fmt(Tr(L"Too many requests to {0}. Try again in {1} s."),
                        {service, json::ToWide(wait.empty() ? std::string("10") : wait)});
    }
    return app::Fmt(Tr(L"{0} is not available right now (error {1})."), {service, std::to_wstring(r.status)});
}

}  // namespace

// --- settings -------------------------------------------------------------------------------

bool LyricsOn() {
    return Cfg().GetBool("privacy.fetchLyrics");
}

bool SubtitlesOn() {
    return Cfg().GetBool("privacy.onlineSubtitles");
}

bool SubtitlesKeySet() {
    return !OsKey().empty();
}

std::wstring SubtitlesUnavailable() {
    if (!SubtitlesOn()) return Tr(L"Turn on subtitle search in Settings › Privacy › Online services.");
    if (!SubtitlesKeySet()) return Tr(L"This copy of Moonbow was built without an OpenSubtitles key.");
    return {};
}

bool MetadataToggle() {
    return Cfg().GetBool("privacy.fetchMetadata") && Cfg().GetString("library.metadata") == "internet";
}

namespace {
std::string g_rejectedTmdbKey;  // the key TMDB answered 401 to
uint64_t g_metaStamp = 1;       // bumped by everything that can change what Meta() answers
}

bool MetadataKeyRejected() {
    const std::string key = TmdbKey();
    return !key.empty() && key == g_rejectedTmdbKey;
}

bool MetadataKeySet() {
    return !TmdbKey().empty();
}

bool MetadataOn() {
    return MetadataToggle() && MetadataKeySet() && !MetadataKeyRejected();
}

app::Signal<>& Changed() {
    return ChangedSignal();
}

// =============================================================================================
// LRCLIB
// =============================================================================================

uint64_t FetchLyrics(const TrackInfo& t, std::function<void(const LyricsAnswer&)> done, bool once) {
    if ((!LyricsOn() && !once) || t.artist.empty() || t.title.empty()) return 0;
    if (t.path.find(L"://") != std::wstring::npos) return 0;  // streams: their tags change per song
    Request r = Base(LrclibBase() + LrclibGetPath(t.artist, t.title, t.album, t.duration));
    r.timeoutMs = 20000;  // /api/get may look the song up elsewhere before it answers
    r.cacheKey = "lrclib:get:" + json::ToUtf8(r.url);
    r.cacheTtl = 30 * 86400;
    r.cache404 = 3 * 86400;
    const TrackInfo track = t;
    return Send(std::move(r), [track, done](const Response& resp) {
        if (resp.Ok()) {
            auto doc = json::Parse(resp.body);
            done(doc ? ParseLrclibRecord(*doc) : LyricsAnswer{});
            return;
        }
        // 404: no exact record; 5xx: /api/get could not reach its upstream lookups. The search
        // reads LRCLIB's own records and finds the song with another album name or length.
        if (resp.error != Error::None || (resp.status != 404 && resp.status < 500)) {
            done({});
            return;
        }
        Request s = Base(LrclibBase() + L"/api/search?track_name=" + UrlEncode(track.title) + L"&artist_name=" +
                         UrlEncode(track.artist));
        s.cacheKey = "lrclib:search:" + json::ToUtf8(s.url);
        s.cacheTtl = 30 * 86400;
        Send(std::move(s), [track, done](const Response& sr) {
            LyricsAnswer a;
            if (sr.Ok())
                if (auto doc = json::Parse(sr.body)) {
                    const int i = PickLrclibRecord(*doc, track.duration);
                    if (i >= 0) a = ParseLrclibRecord((*doc)[static_cast<size_t>(i)]);
                }
            done(a);
        });
    });
}

void SaveLyrics(const TrackInfo& t, const LyricsAnswer& lyrics, ui::Host* host) {
    std::wstring folder = library::ParentFolder(t.path);
    const std::wstring stem = library::StemOf(t.path);
    if (folder.empty() || stem.empty() || !lyrics.found) return;
    const std::wstring file = folder + L"\\" + stem + L".lrc";
    const std::string text = LrcFileText(lyrics, t.artist, t.title, t.album);
    auto result = std::make_shared<DWORD>(0);
    Async(
        [file, text, result] {
            // Never replaces a file: the user's own .lrc would have been shown instead.
            HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) {
                *result = GetLastError();
                return;
            }
            DWORD written = 0;
            const BOOL ok = WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            CloseHandle(f);
            if (!ok || written != text.size()) {
                *result = ERROR_WRITE_FAULT;
                DeleteFileW(file.c_str());
            }
        },
        [file, result, host] {
            std::wstring msg;
            const char* icon = "info";
            if (*result == 0) {
                msg = app::Fmt(Tr(L"Lyrics saved as {0}"), {library::FileNameOf(file)});
                icon = "save";
            } else if (*result == ERROR_FILE_EXISTS || *result == ERROR_ALREADY_EXISTS) {
                msg = app::Fmt(Tr(L"{0} already exists"), {library::FileNameOf(file)});
            } else {
                msg = Tr(L"Couldn't save the lyrics next to the song: the folder is read-only.");
            }
            bool shown = false;
            for (ui::Host* h : ui::Host::All())
                if (h == host) {
                    ui::ShowToast(h, msg, icon, 2600);
                    shown = true;
                }
            if (!shown) ToastOnMain(msg, icon);
        });
}

// =============================================================================================
// OpenSubtitles
// =============================================================================================

namespace {

struct OsSession {
    std::string token;
    std::wstring base;      // from the login answer (VIP accounts get their own host)
    std::string credentials;  // user + password the token belongs to
    std::string failed;       // credentials the server refused: not sent again
    int quota = -1;
};

OsSession& Os() {
    static OsSession s;
    return s;
}

std::wstring OsUserAgent() {
    // Their API refuses agents that aren't "App vX.Y".
    return L"Moonbow v" + AppVersion();
}

Request OsRequest(const std::wstring& url) {
    Request r = Base(url);
    r.user = true;
    r.userAgent = OsUserAgent();
    r.headers.push_back({L"Api-Key", json::ToWide(OsKey())});
    r.headers.push_back({L"Accept", L"application/json"});
    return r;
}

std::string Credentials() {
    const std::string user = Trimmed(Cfg().GetString("privacy.opensubtitlesUser"));
    const std::string pass = Cfg().GetString("privacy.opensubtitlesPassword");
    return user.empty() || pass.empty() ? std::string() : user + '\n' + pass;
}

// Logs in once per session when a user name and password are set; `done(error)` with an empty
// error when there is a token or nothing to log in with.
void EnsureLogin(std::function<void(const std::wstring& error)> done) {
    OsSession& s = Os();
    const std::string creds = Credentials();
    if (creds.empty() || creds == s.credentials) {
        if (creds.empty()) s.token.clear(), s.credentials.clear();
        done({});
        return;
    }
    if (creds == s.failed) {
        done(Tr(L"OpenSubtitles did not accept the user name or password; downloading without an account."));
        return;
    }
    const size_t nl = creds.find('\n');
    json::Value body = json::Value::MakeObject();
    body.Set("username", creds.substr(0, nl));
    body.Set("password", creds.substr(nl + 1));
    Request r = OsRequest(OsBase() + L"/login");
    r.method = "POST";
    r.headers.push_back({L"Content-Type", L"application/json"});
    r.body = json::Serialize(body);
    Send(std::move(r), [creds, done](const Response& resp) {
        OsSession& s = Os();
        if (resp.Ok())
            if (auto doc = json::Parse(resp.body)) {
                const SubtitleLogin l = ParseSubtitleLogin(*doc);
                if (!l.token.empty()) {
                    s.token = l.token;
                    s.credentials = creds;
                    s.base = l.baseUrl.empty() || ui::Host::Automation() ? std::wstring()
                                                                          : L"https://" + l.baseUrl + L"/api/v1";
                    // The key and the token go to this host: only their own.
                    if (!OsHostAllowed(s.base)) s.base.clear();
                    if (l.allowed >= 0 && s.quota < 0) s.quota = l.allowed;
                    done({});
                    return;
                }
            }
        if (resp.error == Error::None && (resp.status == 401 || resp.status == 400)) {
            s.failed = creds;
            done(Tr(L"OpenSubtitles did not accept the user name or password; downloading without an account."));
            return;
        }
        done(Failure(resp, L"OpenSubtitles"));
    });
}

std::wstring DownloadBase() {
    return Os().base.empty() ? OsBase() : Os().base;
}

}  // namespace

std::vector<std::string> PreferredSubtitleLanguages() {
    std::string id = Cfg().GetString("subtitles.language");
    std::vector<std::string> out;
    if (id == "app") id = Cfg().GetString("general.language") == "en" ? "en" : "ru";
    if (id == "system" || id.empty()) {
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) > 0) {
            std::string code = json::ToUtf8(name);
            id = code.substr(0, code.find('-'));
        } else {
            id.clear();
        }
    }
    if (id == "original") id.clear();
    if (!id.empty()) out.push_back(id);
    if (id != "en") out.push_back("en");
    return out;
}

int LastSubtitleQuota() {
    return Os().quota;
}

void SearchSubtitles(const SubtitleSearch& search, std::function<void(const SubtitleSearchResult&)> done) {
    if (const std::wstring why = SubtitlesUnavailable(); !why.empty()) {
        SubtitleSearchResult r;
        r.error = why;
        done(r);
        return;
    }
    // The moviehash reads 128 KB of the file: on a worker, the file may sit on a slow share.
    auto hash = std::make_shared<std::string>();
    const std::wstring video = search.videoPath;
    Async(
        [video, hash] {
            uint64_t h = 0;
            if (!video.empty() && video.find(L"://") == std::wstring::npos && MovieHash(video, &h)) *hash = HashHex(h);
        },
        [search, hash, done] {
            SubtitleQuery q;
            q.moviehash = *hash;
            q.query = search.query;
            q.season = search.season;
            q.episode = search.episode;
            q.languages = search.languages;
            Request r = OsRequest(OsBase() + L"/subtitles" + SubtitleQueryString(q));
            r.cacheKey = "os:search:" + json::ToUtf8(r.url);
            r.cacheTtl = 6 * 3600;
            const bool usedHash = !hash->empty();
            Send(std::move(r), [done, usedHash](const Response& resp) {
                SubtitleSearchResult out;
                out.usedHash = usedHash;
                if (!resp.Ok()) {
                    out.error = Failure(resp, L"OpenSubtitles");
                    done(out);
                    return;
                }
                auto doc = json::Parse(resp.body);
                if (!doc) {
                    out.error = Tr(L"OpenSubtitles sent an answer Moonbow couldn't read.");
                    done(out);
                    return;
                }
                out.ok = true;
                out.items = ParseSubtitleSearch(*doc);
                done(out);
            });
        });
}

void SubtitleQuota(std::function<void(int, const std::wstring&)> done) {
    if (!SubtitlesOn() || !SubtitlesKeySet() || Credentials().empty()) {
        done(Os().quota, {});
        return;
    }
    EnsureLogin([done](const std::wstring& error) {
        if (!error.empty() || Os().token.empty()) {
            done(Os().quota, error);
            return;
        }
        Request r = OsRequest(DownloadBase() + L"/infos/user");
        r.headers.push_back({L"Authorization", L"Bearer " + json::ToWide(Os().token)});
        Send(std::move(r), [done](const Response& resp) {
            if (resp.Ok())
                if (auto doc = json::Parse(resp.body)) {
                    const int q = ParseSubtitleQuota(*doc);
                    if (q >= 0) Os().quota = q;
                }
            done(Os().quota, resp.Ok() ? std::wstring() : Failure(resp, L"OpenSubtitles"));
        });
    });
}

void DownloadSubtitle(const SubtitleResult& item, const std::wstring& videoPath,
                      std::function<void(const SubtitleFetch&)> done) {
    if (const std::wstring why = SubtitlesUnavailable(); !why.empty()) {
        SubtitleFetch f;
        f.error = why;
        done(f);
        return;
    }
    const int64_t fileId = item.fileId;
    const std::wstring language = item.language, serverName = item.fileName;
    EnsureLogin([fileId, language, serverName, videoPath, done](const std::wstring& loginNote) {
        json::Value body = json::Value::MakeObject();
        body.Set("file_id", json::Value(fileId));
        Request r = OsRequest(DownloadBase() + L"/download");
        r.method = "POST";
        r.headers.push_back({L"Content-Type", L"application/json"});
        if (!Os().token.empty()) r.headers.push_back({L"Authorization", L"Bearer " + json::ToWide(Os().token)});
        r.body = json::Serialize(body);
        Send(std::move(r), [language, serverName, videoPath, done, loginNote](const Response& resp) {
            SubtitleFetch out;
            auto doc = json::Parse(resp.body);
            const SubtitleDownload d = doc ? ParseSubtitleDownload(*doc) : SubtitleDownload{};
            if (d.remaining >= 0) Os().quota = d.remaining;
            if (resp.error == Error::None && resp.status == 406) {
                // Daily quota used up: their message names the reset time.
                const int64_t left = d.resetAt - UnixNow();
                out.error = left <= 0
                                ? std::wstring(Tr(L"The OpenSubtitles download limit for today is used up."))
                                : app::Fmt(Tr(L"The OpenSubtitles download limit for today is used up. It resets in {0}."),
                                           {app::Fmt(Tr(L"{0} h {1} min"), {std::to_wstring(left / 3600),
                                                                             std::to_wstring(left % 3600 / 60)})});
                // Without an account the limit is 5 a day per address; a free account gets 20.
                if (Os().token.empty())
                    out.error += L" " + std::wstring(Tr(L"Sign in with a free OpenSubtitles account in Settings › "
                                                        L"Privacy › Online services to download more."));
                Os().quota = 0;
                done(out);
                return;
            }
            if (!resp.Ok() || d.link.empty()) {
                out.error = resp.Ok() ? std::wstring(Tr(L"OpenSubtitles sent an answer Moonbow couldn't read."))
                                      : Failure(resp, L"OpenSubtitles");
                done(out);
                return;
            }
            if (!OsHostAllowed(d.link)) {
                out.error = app::Fmt(Tr(L"{0} sent an address Moonbow does not open."), {L"OpenSubtitles"});
                done(out);
                return;
            }
            // Next to the video when its folder takes a new file, else in the data folder.
            const std::wstring name = SubtitleFileName(videoPath, language, d.fileName.empty() ? serverName : d.fileName);
            auto target = std::make_shared<std::wstring>();
            auto inData = std::make_shared<bool>(false);
            const std::wstring link = d.link;
            Async(
                [videoPath, name, target, inData] {
                    auto unique = [](const std::wstring& folder, const std::wstring& file) {
                        std::wstring stem = file, ext;
                        if (const size_t dot = file.rfind(L'.'); dot != std::wstring::npos) {
                            stem = file.substr(0, dot);
                            ext = file.substr(dot);
                        }
                        std::wstring p = folder + L"\\" + file;
                        for (int i = 2; GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; ++i) {
                            if (i == 100) return std::wstring();  // never replace the 99th copy
                            p = folder + L"\\" + stem + L"." + std::to_wstring(i) + ext;
                        }
                        return p;
                    };
                    auto writable = [](const std::wstring& p) {
                        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
                        if (h == INVALID_HANDLE_VALUE) return false;
                        CloseHandle(h);
                        return true;
                    };
                    const std::wstring folder = library::ParentFolder(videoPath);
                    if (!folder.empty() && videoPath.find(L"://") == std::wstring::npos) {
                        const std::wstring p = unique(folder, name);
                        if (writable(p)) {
                            *target = p;
                            return;
                        }
                    }
                    const std::wstring data = DataDir().empty() ? std::wstring() : DataDir() + L"\\subtitles";
                    if (data.empty()) return;
                    CreateDirectoryW(data.c_str(), nullptr);
                    *target = unique(data, name);
                    *inData = true;
                },
                [target, inData, link, done, loginNote] {
                    SubtitleFetch out;
                    if (target->empty()) {
                        out.error = Tr(L"Couldn't save the file.");
                        done(out);
                        return;
                    }
                    Request get = Base(link);
                    get.user = true;
                    get.userAgent = OsUserAgent();
                    get.saveTo = *target;
                    get.maxBytes = 8u << 20;
                    const std::wstring path = *target;
                    const bool data = *inData;
                    Send(std::move(get), [path, data, done, loginNote](const Response& resp) {
                        SubtitleFetch f;
                        f.inDataDir = data;
                        if (resp.Ok()) {
                            f.ok = true;
                            f.path = path;
                            f.error = loginNote;  // shown as a note, the download worked
                        } else {
                            f.error = Failure(resp, L"OpenSubtitles");
                        }
                        done(f);
                    });
                });
        });
    });
}

// =============================================================================================
// TMDB
// =============================================================================================

namespace {

struct ImageState {
    bool have = false, requested = false;
};

class MetaStore {
public:
    // Loaded on a worker the first time the feature asks; until then nothing is known.
    bool Ready() {
        if (loaded_) return true;
        if (!loading_) Load();
        return false;
    }
    MediaMeta* Find(const std::wstring& key) {
        auto it = items_.find(key);
        return it == items_.end() ? nullptr : &it->second;
    }
    MediaMeta& Put(const std::wstring& key, const std::wstring& path) {
        ++g_metaStamp;
        paths_[key] = path;
        return items_[key];
    }
    void Erase(const std::wstring& key) {
        ++g_metaStamp;
        items_.erase(key);
        paths_.erase(key);
    }
    const std::wstring* PathOf(const std::wstring& key) const {
        auto it = paths_.find(key);
        return it == paths_.end() ? nullptr : &it->second;
    }
    ImageState& Image(const std::wstring& file) { return images_[file]; }
    void Save();
    void Clear() {
        ++g_metaStamp;
        items_.clear();
        paths_.clear();
        images_.clear();
        generation_++;
    }
    uint64_t Generation() const { return generation_; }

private:
    void Load();
    bool loaded_ = false, loading_ = false, saving_ = false, saveAgain_ = false;
    uint64_t generation_ = 0;
    std::unordered_map<std::wstring, MediaMeta> items_;   // PathKey -> record
    std::unordered_map<std::wstring, std::wstring> paths_;  // PathKey -> path as seen
    std::unordered_map<std::wstring, ImageState> images_;   // local file -> state
};

MetaStore& Store() {
    static MetaStore* s = new MetaStore;
    return *s;
}

std::wstring StoreFile() {
    return OnlineDir().empty() ? std::wstring() : OnlineDir() + L"\\metadata.json";
}

std::wstring ImageDir() {
    return OnlineDir().empty() ? std::wstring() : OnlineDir() + L"\\images";
}

// "w342" + "/abc.jpg" -> <online>\images\w342_abc.jpg
std::wstring ImageFile(const char* size, const std::string& tmdbPath) {
    if (tmdbPath.size() < 2 || ImageDir().empty()) return {};
    std::wstring name = json::ToWide(tmdbPath.substr(1));
    for (wchar_t& c : name)
        if (!(std::iswalnum(c) || c == L'.' || c == L'_' || c == L'-')) c = L'_';
    return ImageDir() + L"\\" + json::ToWide(size) + L"_" + name;
}

json::Value ToJson(const MediaMeta& m, const std::wstring& path) {
    json::Value v = json::Value::MakeObject();
    v.Set("path", json::ToUtf8(path));
    v.Set("state", m.state == MediaMeta::State::Matched ? "matched" : "none");
    v.Set("checked", json::Value(m.checked));
    if (m.manual) v.Set("manual", true);
    if (m.state != MediaMeta::State::Matched) return v;
    v.Set("kind", m.tv ? "tv" : "movie");
    v.Set("id", json::Value(m.id));
    v.Set("title", json::ToUtf8(m.title));
    if (m.year) v.Set("year", m.year);
    if (!m.overview.empty()) v.Set("overview", json::ToUtf8(m.overview));
    if (m.rating > 0) v.Set("rating", m.rating);
    if (!m.poster.empty()) v.Set("poster", m.poster);
    if (!m.backdrop.empty()) v.Set("backdrop", m.backdrop);
    if (m.tv) {
        v.Set("season", m.season);
        v.Set("episode", m.episode);
        if (!m.episodeTitle.empty()) v.Set("episodeTitle", json::ToUtf8(m.episodeTitle));
        if (!m.still.empty()) v.Set("still", m.still);
    }
    return v;
}

MediaMeta FromJson(const json::Value& v) {
    MediaMeta m;
    m.state = v.GetString("state") == "matched" ? MediaMeta::State::Matched : MediaMeta::State::NotFound;
    m.checked = v.GetInt("checked");
    m.manual = v.GetBool("manual");
    m.tv = v.GetString("kind") == "tv";
    m.id = v.GetInt("id");
    m.title = v.GetWString("title");
    m.year = static_cast<int>(v.GetInt("year"));
    m.overview = v.GetWString("overview");
    m.rating = v.GetNumber("rating");
    m.poster = v.GetString("poster");
    m.backdrop = v.GetString("backdrop");
    m.season = static_cast<int>(v.GetInt("season"));
    m.episode = static_cast<int>(v.GetInt("episode"));
    m.episodeTitle = v.GetWString("episodeTitle");
    m.still = v.GetString("still");
    return m;
}

void MetaStore::Load() {
    loading_ = true;
    struct Loaded {
        std::vector<std::pair<std::wstring, MediaMeta>> items;
        std::vector<std::wstring> paths;
        std::vector<std::wstring> images;
    };
    auto out = std::make_shared<Loaded>();
    const uint64_t gen = generation_;
    Async(
        [out] {
            std::string text;
            if (json::ReadFileUtf8(StoreFile(), &text))
                if (auto doc = json::Parse(text))
                    if (const json::Value* list = doc->Find("items"))
                        for (const json::Value& v : list->AsArray()) {
                            const std::wstring path = v.GetWString("path");
                            if (path.empty()) continue;
                            out->paths.push_back(path);
                            out->items.push_back({library::PathKey(path), FromJson(v)});
                        }
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileExW((ImageDir() + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                        nullptr, 0);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.nFileSizeLow > 0)
                        out->images.push_back(ImageDir() + L"\\" + fd.cFileName);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        },
        [this, out, gen] {
            loading_ = false;
            loaded_ = true;
            ++g_metaStamp;
            if (gen == generation_) {
                for (size_t i = 0; i < out->items.size(); ++i) {
                    paths_[out->items[i].first] = out->paths[i];
                    items_.emplace(out->items[i].first, std::move(out->items[i].second));
                }
                for (const std::wstring& f : out->images) images_[f].have = true;
            }
            ChangedSignal().Emit();
            TheApp().images().Refresh();
        });
}

void MetaStore::Save() {
    if (!loaded_) return;
    if (saving_) {
        saveAgain_ = true;
        return;
    }
    json::Value list = json::Value::MakeArray();
    for (const auto& [key, m] : items_) {
        if (m.state != MediaMeta::State::Matched && m.state != MediaMeta::State::NotFound) continue;
        auto p = paths_.find(key);
        if (p != paths_.end()) list.Push(ToJson(m, p->second));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("items", std::move(list));
    auto text = std::make_shared<std::string>(json::Serialize(doc));
    saving_ = true;
    Async([text] { json::WriteFileAtomic(StoreFile(), *text); },
          [this] {
              saving_ = false;
              if (saveAgain_) {
                  saveAgain_ = false;
                  Save();
              }
          });
}

std::wstring TmdbLanguage() {
    return app::CurrentLanguage() == app::Language::Russian ? L"ru-RU" : L"en-US";
}

Request TmdbRequest(const std::wstring& pathAndQuery) {
    const std::string key = TmdbKey();
    std::wstring url = TmdbBase() + pathAndQuery;
    url += (url.find(L'?') == std::wstring::npos ? L"?" : L"&") + std::wstring(L"language=") + TmdbLanguage();
    const std::string cacheKey = "tmdb:" + json::ToUtf8(url);  // without the key
    if (!TmdbKeyIsBearer(key)) url += L"&api_key=" + UrlEncode(json::ToWide(key));
    Request r = Base(url);
    r.cacheKey = cacheKey;
    if (TmdbKeyIsBearer(key)) r.headers.push_back({L"Authorization", L"Bearer " + json::ToWide(key)});
    r.headers.push_back({L"Accept", L"application/json"});
    return r;
}

void NoteTmdbAnswer(const Response& r) {
    if (r.error == Error::None && r.status == 401) {
        g_rejectedTmdbKey = TmdbKey();
        ++g_metaStamp;
        ChangedSignal().Emit();
    }
}

// --- the lookup queue: one file at a time, only what views asked for ---

struct Lookup {
    std::deque<std::wstring> queue;  // PathKeys
    bool busy = false;
    uint64_t pauseUntil = 0;  // GetTickCount64 after a 429
    std::unordered_map<std::wstring, int> retries;  // 429 answers per file this session
};

Lookup& Q() {
    static Lookup q;
    return q;
}

void RunNext();

void Finish(uint64_t gen) {
    Q().busy = false;
    const bool current = gen == Store().Generation();
    if (current) {
        ChangedSignal().Emit();
        TheApp().images().Refresh();
    }
    RunNext();
    // One save when the queue stops (empty, paused, offline), not one per file: a save builds the
    // whole store on the UI thread.
    if (current && !Q().busy) Store().Save();
}

void FetchImage(const char* size, const std::string& tmdbPath) {
    const std::wstring file = ImageFile(size, tmdbPath);
    if (file.empty()) return;
    ImageState& st = Store().Image(file);
    if (st.have || st.requested || !MetadataOn()) return;
    st.requested = true;
    Request r = Base(TmdbImages() + L"/" + json::ToWide(size) + json::ToWide(tmdbPath));
    r.saveTo = file;
    r.keepExisting = true;  // never download what is already here
    r.maxBytes = 4u << 20;
    const uint64_t gen = Store().Generation();
    Send(std::move(r), [file, gen](const Response& resp) {
        if (gen != Store().Generation()) return;
        ImageState& s = Store().Image(file);
        s.requested = false;
        std::string type = resp.Header("content-type");
        for (char& c : type) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (resp.Ok() && !resp.fromCache && type.rfind("image/", 0) != 0) {
            // A 200 page instead of the picture (a captive portal): keepExisting would keep it
            // for good, so it goes, and the next session asks again.
            Async([file] { DeleteFileW(file.c_str()); });
            s.requested = true;
            return;
        }
        if (resp.Ok()) {
            s.have = true;
            ChangedSignal().Emit();
            TheApp().images().Refresh();
        } else {
            s.requested = true;  // not again this session
        }
    });
}

void ApplyEpisode(const std::wstring& key, uint64_t gen, MetaMatch show, int season, int episode, bool manual) {
    Request r = TmdbRequest(L"/tv/" + std::to_wstring(show.id) + L"/season/" + std::to_wstring(season) + L"/episode/" +
                            std::to_wstring(episode));
    r.cacheTtl = 30 * 86400;
    r.cache404 = 3 * 86400;
    Send(std::move(r), [key, gen, show, season, episode, manual](const Response& resp) {
        NoteTmdbAnswer(resp);
        if (gen != Store().Generation()) return;
        MediaMeta* m = Store().Find(key);
        if (!m) return Finish(gen);
        m->state = MediaMeta::State::Matched;
        m->manual = manual;
        m->tv = true;
        m->id = show.id;
        m->title = show.title;
        m->year = show.year;
        m->overview = show.overview;
        m->rating = show.rating;
        m->poster = show.poster;
        m->backdrop = show.backdrop;
        m->season = season;
        m->episode = episode;
        m->checked = UnixNow();
        if (resp.Ok())
            if (auto doc = json::Parse(resp.body)) {
                const EpisodeMeta e = ParseTmdbEpisode(*doc);
                m->episodeTitle = e.name;
                if (!e.overview.empty()) m->overview = e.overview;
                if (e.rating > 0) m->rating = e.rating;
                m->still = e.still;
            }
        if (resp.error != Error::None && !manual) m->state = MediaMeta::State::Failed;
        Finish(gen);
    });
}

void StartLookup(const std::wstring& key) {
    const std::wstring* path = Store().PathOf(key);
    MediaMeta* m = Store().Find(key);
    if (!path || !m) return RunNext();
    const VideoGuess g = GuessVideo(*path);
    const bool tv = g.kind == VideoGuess::Episode;
    std::wstring q = L"/search/" + std::wstring(tv ? L"tv" : L"movie") + L"?query=" + UrlEncode(g.title);
    if (g.year) q += std::wstring(tv ? L"&first_air_date_year=" : L"&year=") + std::to_wstring(g.year);
    if (!tv) q += L"&include_adult=false";
    Request r = TmdbRequest(q);
    r.cacheTtl = 14 * 86400;
    Q().busy = true;
    const uint64_t gen = Store().Generation();
    Send(std::move(r), [key, gen, g, tv](const Response& resp) {
        NoteTmdbAnswer(resp);
        if (gen != Store().Generation()) return Finish(gen);
        MediaMeta* m = Store().Find(key);
        if (!m) return Finish(gen);
        if (resp.error == Error::None && resp.status == 429 && ++Q().retries[key] <= 2) {
            // Back off and try this one again later (twice; then it waits for the next session).
            m->state = MediaMeta::State::Queued;
            Q().queue.push_front(key);
            Q().pauseUntil = GetTickCount64() + 10000;
            Q().busy = false;
            if (app::MainWindow* w = TheApp().main()) w->StartTimer(10500, [] { RunNext(); });
            return;
        }
        if (!resp.Ok()) {
            // Offline, a timeout or a bad key: nothing is saved, the next session asks again.
            m->state = MediaMeta::State::Failed;
            return Finish(gen);
        }
        auto doc = json::Parse(resp.body);
        const std::vector<MetaMatch> results = doc ? ParseTmdbSearch(*doc, tv) : std::vector<MetaMatch>{};
        const int pick = PickTmdbMatch(results, g);
        m->checked = UnixNow();
        if (pick < 0) {
            m->state = MediaMeta::State::NotFound;
            return Finish(gen);
        }
        const MetaMatch& best = results[static_cast<size_t>(pick)];
        if (tv) return ApplyEpisode(key, gen, best, g.season, g.episode, false);
        m->state = MediaMeta::State::Matched;
        m->tv = false;
        m->id = best.id;
        m->title = best.title;
        m->year = best.year;
        m->overview = best.overview;
        m->rating = best.rating;
        m->poster = best.poster;
        m->backdrop = best.backdrop;
        Finish(gen);
    });
}

void RunNext() {
    Lookup& q = Q();
    if (q.busy || q.queue.empty() || !MetadataOn()) return;
    if (GetTickCount64() < q.pauseUntil || Offline()) return;
    const std::wstring key = q.queue.front();
    q.queue.pop_front();
    StartLookup(key);
}

constexpr size_t kMaxQueued = 120;
constexpr int64_t kRetryNotFound = 14 * 86400;

}  // namespace

VideoGuess Guess(const std::wstring& path) {
    return GuessVideo(path);
}

namespace {

const MediaMeta* FindOrQueue(const std::wstring& path) {
    if (!MetadataOn() || path.empty() || path.find(L"://") != std::wstring::npos) return nullptr;
    if (library::KindOf(path) != library::MediaKind::Video) return nullptr;
    MetaStore& store = Store();
    if (!store.Ready()) return nullptr;
    const std::wstring key = library::PathKey(path);
    MediaMeta* m = store.Find(key);
    const bool stale = m && m->state == MediaMeta::State::NotFound && !m->manual && UnixNow() - m->checked > kRetryNotFound;
    if (m && !stale) return m;
    // Automatic lookups need a year for movies: "Holiday 2019.mp4" is someone's video, not a film.
    const VideoGuess g = GuessVideo(path);
    if (g.kind == VideoGuess::None || (g.kind == VideoGuess::Movie && g.year == 0)) return nullptr;
    MediaMeta& fresh = store.Put(key, path);
    fresh.state = MediaMeta::State::Queued;
    Lookup& q = Q();
    q.queue.push_back(key);
    while (q.queue.size() > kMaxQueued) {
        // Forget the oldest wish: the view asks again when it shows that card.
        store.Erase(q.queue.front());
        q.queue.pop_front();
    }
    RunNext();
    return &fresh;
}

}  // namespace

const MediaMeta* Meta(const std::wstring& path) {
    // A card asks twice per paint (its artwork, then year and rating): the second ask is free.
    static std::wstring lastPath;
    static const MediaMeta* last = nullptr;
    static uint64_t lastStamp = 0;
    if (lastStamp == g_metaStamp && path == lastPath) return last;
    last = FindOrQueue(path);
    lastPath = path;
    lastStamp = g_metaStamp;
    return last;
}

std::wstring CardImage(const std::wstring& path) {
    const MediaMeta* m = Meta(path);
    if (!m || m->state != MediaMeta::State::Matched) return {};
    // An episode shows its own still; a movie (or an episode without one) the backdrop.
    const char* size = !m->still.empty() ? "w300" : "w780";
    const std::string& img = !m->still.empty() ? m->still : m->backdrop;
    const std::wstring file = ImageFile(size, img);
    if (file.empty()) return {};
    if (Store().Image(file).have) return file;
    FetchImage(size, img);
    return {};
}

std::wstring PosterImage(const std::wstring& path) {
    const MediaMeta* m = Meta(path);
    if (!m || m->state != MediaMeta::State::Matched) return {};
    const std::wstring file = ImageFile("w342", m->poster);
    if (file.empty()) return {};
    if (Store().Image(file).have) return file;
    FetchImage("w342", m->poster);
    return {};
}

std::wstring MetaSummary(const MediaMeta& m) {
    if (m.state != MediaMeta::State::Matched) return {};
    std::wstring s;
    if (m.year) s = std::to_wstring(m.year);
    if (m.rating > 0) {
        wchar_t buf[16];
        swprintf_s(buf, L"%.1f", m.rating);
        std::wstring r = buf;
        if (app::CurrentLanguage() == app::Language::Russian) std::replace(r.begin(), r.end(), L'.', L',');
        s += (s.empty() ? L"" : L" · ") + std::wstring(L"★ ") + r;
    }
    return s;
}

void SearchMetadata(const std::wstring& query, int year, bool tv,
                    std::function<void(const std::vector<MetaMatch>&, const std::wstring&)> done) {
    if (!MetadataToggle()) {
        done({}, Tr(L"Turn on posters and descriptions in Settings › Privacy › Online services."));
        return;
    }
    if (!MetadataKeySet()) {
        done({}, Tr(L"This copy of Moonbow was built without a TMDB key."));
        return;
    }
    std::wstring q = L"/search/" + std::wstring(tv ? L"tv" : L"movie") + L"?query=" + UrlEncode(query);
    if (year > 0) q += std::wstring(tv ? L"&first_air_date_year=" : L"&year=") + std::to_wstring(year);
    Request r = TmdbRequest(q);
    r.user = true;
    r.cacheTtl = 14 * 86400;
    Send(std::move(r), [tv, done](const Response& resp) {
        NoteTmdbAnswer(resp);
        if (!resp.Ok()) {
            done({}, Failure(resp, L"TMDB"));
            return;
        }
        auto doc = json::Parse(resp.body);
        done(doc ? ParseTmdbSearch(*doc, tv) : std::vector<MetaMatch>{}, {});
    });
}

void SetMatch(const std::wstring& path, const MetaMatch& match) {
    const std::wstring key = library::PathKey(path);
    MetaStore& store = Store();
    MediaMeta& m = store.Put(key, path);
    const uint64_t gen = store.Generation();
    const VideoGuess g = GuessVideo(path);
    if (match.tv) {
        m.state = MediaMeta::State::Queued;
        ApplyEpisode(key, gen, match, std::max(1, g.season), std::max(1, g.episode), true);
        return;
    }
    m = MediaMeta{};
    m.state = MediaMeta::State::Matched;
    m.manual = true;
    m.id = match.id;
    m.title = match.title;
    m.year = match.year;
    m.overview = match.overview;
    m.rating = match.rating;
    m.poster = match.poster;
    m.backdrop = match.backdrop;
    m.checked = UnixNow();
    store.Save();
    ChangedSignal().Emit();
    TheApp().images().Refresh();
}

void ResetMeta(const std::wstring& path) {
    const std::wstring key = library::PathKey(path);
    MetaStore& store = Store();
    // A manual "nothing" so the next paint doesn't match the same wrong film again at once:
    // the user resets to see the file's own thumbnail; "Find another match" picks a new one.
    MediaMeta& m = store.Put(key, path);
    m = MediaMeta{};
    m.state = MediaMeta::State::NotFound;
    m.manual = true;
    m.checked = UnixNow();
    auto& q = Q().queue;
    q.erase(std::remove(q.begin(), q.end(), key), q.end());
    store.Save();
    ChangedSignal().Emit();
    TheApp().images().Refresh();
}

// =============================================================================================

void ClearAll() {
    SharedMemory().Clear();
    Q().queue.clear();
    Store().Clear();
    // The empty store goes to disk after any save still in flight, which could otherwise land
    // after the delete below and bring the data back.
    Store().Save();
    Os().quota = -1;
    const std::wstring dir = OnlineDir();
    if (dir.empty()) return;
    Async(
        [dir] {
            SharedDisk().Clear();
            DeleteFileW((dir + L"\\metadata.json").c_str());
            // Leftovers of a save cut short by a crash ("metadata.json.<pid>.tmp").
            WIN32_FIND_DATAW tf;
            HANDLE th = FindFirstFileExW((dir + L"\\*.tmp").c_str(), FindExInfoBasic, &tf, FindExSearchNameMatch, nullptr, 0);
            if (th != INVALID_HANDLE_VALUE) {
                do {
                    DeleteFileW((dir + L"\\" + tf.cFileName).c_str());
                } while (FindNextFileW(th, &tf));
                FindClose(th);
            }
            const std::wstring images = dir + L"\\images";
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileExW((images + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((images + L"\\" + fd.cFileName).c_str());
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        },
        [] {
            TheApp().images().Clear();
            ChangedSignal().Emit();
            TheApp().images().Refresh();
        });
}

void Init() {
    // One switch shown in two places: Privacy › Network (the consent) and Library › Metadata.
    static bool syncing = false;
    auto sync = [](const std::string& key) {
        if (syncing) return;
        syncing = true;
        Config& c = Cfg();
        if (key == "privacy.fetchMetadata")
            c.SetString("library.metadata", c.GetBool("privacy.fetchMetadata") ? "internet" : "file");
        else if (key == "library.metadata")
            c.SetBool("privacy.fetchMetadata", c.GetString("library.metadata") == "internet");
        syncing = false;
        ++g_metaStamp;
        if (!MetadataOn()) Q().queue.clear();
        ChangedSignal().Emit();
        TheApp().images().Refresh();
    };
    Cfg().Subscribe("privacy.fetchMetadata", sync);
    Cfg().Subscribe("library.metadata", sync);
    // Downloading missing subtitles is subtitle search: choosing it turns the search on, and turning
    // the search off stops it.
    Cfg().Subscribe("subtitles.autoDownload", [](const std::string&) {
        if (Cfg().GetString("subtitles.autoDownload") != "off" && !Cfg().GetBool("privacy.onlineSubtitles"))
            Cfg().SetBool("privacy.onlineSubtitles", true);
    });
    Cfg().Subscribe("privacy.onlineSubtitles", [](const std::string&) {
        if (!Cfg().GetBool("privacy.onlineSubtitles") && Cfg().GetString("subtitles.autoDownload") != "off")
            Cfg().SetString("subtitles.autoDownload", "off");
    });
    Cfg().Subscribe("privacy.tmdbKey", [](const std::string&) {
        ++g_metaStamp;
        ChangedSignal().Emit();
        TheApp().images().Refresh();
    });
    if (Cfg().GetString("library.metadata") == "internet" && !Cfg().GetBool("privacy.fetchMetadata"))
        Cfg().SetString("library.metadata", "file");
}

}  // namespace net::online

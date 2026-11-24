#include "OnlineData.h"  // relative: the unit tests build it without /I src

#include "Http.h"
#include "../library/MediaTypes.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace net {

// --- moviehash ------------------------------------------------------------------------------

namespace {

constexpr size_t kHashBlock = 65536;

uint64_t SumWords(const unsigned char* p) {
    uint64_t sum = 0;
    for (size_t i = 0; i < kHashBlock; i += 8) {
        uint64_t w = 0;
        std::memcpy(&w, p + i, 8);  // little-endian on every Windows target
        sum += w;
    }
    return sum;
}

}  // namespace

uint64_t MovieHashOf(uint64_t size, const unsigned char* head, const unsigned char* tail) {
    return size + SumWords(head) + SumWords(tail);
}

bool MovieHash(const std::wstring& path, uint64_t* hash, uint64_t* sizeOut) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    std::vector<unsigned char> head(kHashBlock), tail(kHashBlock);
    bool ok = GetFileSizeEx(f, &size) && static_cast<uint64_t>(size.QuadPart) >= 2 * kHashBlock;
    auto readAt = [&](int64_t offset, unsigned char* out) {
        LARGE_INTEGER pos;
        pos.QuadPart = offset;
        DWORD got = 0;
        return SetFilePointerEx(f, pos, nullptr, FILE_BEGIN) &&
               ReadFile(f, out, static_cast<DWORD>(kHashBlock), &got, nullptr) && got == kHashBlock;
    };
    ok = ok && readAt(0, head.data()) && readAt(size.QuadPart - static_cast<int64_t>(kHashBlock), tail.data());
    CloseHandle(f);
    if (!ok) return false;
    *hash = MovieHashOf(static_cast<uint64_t>(size.QuadPart), head.data(), tail.data());
    if (sizeOut) *sizeOut = static_cast<uint64_t>(size.QuadPart);
    return true;
}

std::string HashHex(uint64_t hash) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

// --- file names -----------------------------------------------------------------------------

namespace {

std::wstring LowerW(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool IsYear(const std::wstring& t) {
    if (t.size() != 4 || !std::all_of(t.begin(), t.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
        return false;
    const int y = _wtoi(t.c_str());
    return y >= 1900 && y <= 2099;
}

std::vector<std::wstring> Tokens(const std::wstring& name) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : name) {
        if (c == L'.' || c == L'_' || c == L' ' || c == L'(' || c == L')' || c == L'[' || c == L']' || c == L'{' ||
            c == L'}') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

std::wstring Join(const std::vector<std::wstring>& words) {
    std::wstring s;
    for (const std::wstring& w : words) {
        if (w == L"-" || w == L"–") continue;
        if (!s.empty()) s.push_back(L' ');
        s += w;
    }
    while (!s.empty() && (s.back() == L'-' || s.back() == L' ')) s.pop_back();
    return s;
}

bool IsSeasonFolder(const std::wstring& name) {
    const std::wstring n = LowerW(name);
    for (const wchar_t* p : {L"season", L"сезон", L"series", L"s"}) {
        const size_t len = wcslen(p);
        if (n.compare(0, len, p) != 0) continue;
        size_t i = len;
        while (i < n.size() && (n[i] == L' ' || n[i] == L'.' || n[i] == L'_')) ++i;
        if (i < n.size() && std::all_of(n.begin() + static_cast<std::ptrdiff_t>(i), n.end(), ::iswdigit)) return true;
    }
    return false;
}

}  // namespace

std::wstring CleanTitle(const std::wstring& name, int* year) {
    const std::vector<std::wstring> tokens = Tokens(name);
    std::vector<std::wstring> words;
    int found = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::wstring& t = tokens[i];
        // A year ends the title, unless another year follows ("Blade Runner 2049 (2017)") or it is
        // the whole title so far ("2012.mkv", "1917 (2019)").
        if (IsYear(t) && !words.empty() && !(i + 1 < tokens.size() && IsYear(tokens[i + 1]))) {
            found = _wtoi(t.c_str());
            break;
        }
        if (!words.empty() && library::IsReleaseTag(t)) break;
        words.push_back(t);
    }
    if (year) *year = found;
    return Join(words);
}

VideoGuess GuessVideo(const std::wstring& path) {
    VideoGuess g;
    const std::wstring stem = library::StemOf(path);
    if (stem.empty()) return g;
    library::EpisodeInfo ep;
    // As the library groups it: S01E02 and 1x02, numbered files in season folders, anime releases.
    if (library::ParseEpisode(stem, &ep) || library::ParseEpisodePath(path, &ep)) {
        g.kind = VideoGuess::Episode;
        g.season = ep.season;
        g.episode = ep.episode;
        g.title = CleanTitle(ep.show, &g.year);
        if (g.title.empty()) {
            // "S01E02.mkv" in "Show Name\Season 1": the show is the folder above the season.
            std::wstring folder = library::ParentFolder(path);
            std::wstring name = library::FileNameOf(folder);
            if (IsSeasonFolder(name)) name = library::FileNameOf(library::ParentFolder(folder));
            g.title = CleanTitle(name, &g.year);
        }
        if (g.title.empty()) g.kind = VideoGuess::None;
        return g;
    }
    g.title = CleanTitle(stem, &g.year);
    if (!g.title.empty()) g.kind = VideoGuess::Movie;
    return g;
}

// --- LRCLIB ---------------------------------------------------------------------------------

LyricsAnswer ParseLrclibRecord(const json::Value& v) {
    LyricsAnswer a;
    if (!v.IsObject()) return a;
    a.instrumental = v.GetBool("instrumental");
    a.duration = v.GetNumber("duration");
    const std::wstring synced = v.GetWString("syncedLyrics"), plain = v.GetWString("plainLyrics");
    if (!synced.empty()) {
        a.synced = true;
        a.text = synced;
    } else {
        a.text = plain;
    }
    a.found = !a.text.empty() && !a.instrumental;
    return a;
}

int PickLrclibRecord(const json::Value& array, double duration) {
    int best = -1;
    double bestScore = 1e9;
    const auto& list = array.AsArray();
    for (size_t i = 0; i < list.size(); ++i) {
        const LyricsAnswer a = ParseLrclibRecord(list[i]);
        if (!a.found) continue;
        const double diff = duration > 0 && a.duration > 0 ? std::abs(a.duration - duration) : 0;
        if (duration > 0 && a.duration > 0 && diff > 3) continue;
        const double score = diff + (a.synced ? 0 : 100);
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::wstring LrclibGetPath(const std::wstring& artist, const std::wstring& title, const std::wstring& album,
                           double duration) {
    std::wstring p = L"/api/get?artist_name=" + UrlEncode(artist) + L"&track_name=" + UrlEncode(title);
    if (!album.empty()) p += L"&album_name=" + UrlEncode(album);
    if (duration > 0) p += L"&duration=" + std::to_wstring(static_cast<long long>(std::llround(duration)));
    return p;
}

std::string LrcFileText(const LyricsAnswer& a, const std::wstring& artist, const std::wstring& title,
                        const std::wstring& album) {
    std::string out = "\xEF\xBB\xBF";  // BOM: older Windows tools read .lrc files as ANSI otherwise
    auto tag = [&](const char* name, const std::wstring& value) {
        if (!value.empty()) out += std::string("[") + name + ":" + json::ToUtf8(value) + "]\r\n";
    };
    tag("ar", artist);
    tag("ti", title);
    tag("al", album);
    out += "[re:LRCLIB]\r\n";
    std::string text = json::ToUtf8(a.text);
    std::string lines;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') continue;
        if (text[i] == '\n') lines += "\r\n";
        else lines.push_back(text[i]);
    }
    out += lines;
    if (!lines.empty() && lines.back() != '\n') out += "\r\n";
    return out;
}

// --- OpenSubtitles --------------------------------------------------------------------------

std::wstring SubtitleQueryString(const SubtitleQuery& q) {
    std::vector<std::pair<std::wstring, std::wstring>> params;
    if (q.episode > 0) params.push_back({L"episode_number", std::to_wstring(q.episode)});
    std::vector<std::string> langs;
    for (std::string l : q.languages) {
        for (char& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!l.empty() && std::find(langs.begin(), langs.end(), l) == langs.end()) langs.push_back(l);
    }
    std::sort(langs.begin(), langs.end());
    if (!langs.empty()) {
        std::wstring v;
        for (const std::string& l : langs) v += (v.empty() ? L"" : L",") + UrlEncode(json::ToWide(l));
        params.push_back({L"languages", v});
    }
    if (!q.moviehash.empty()) params.push_back({L"moviehash", json::ToWide(q.moviehash)});
    if (!q.query.empty()) params.push_back({L"query", UrlEncode(LowerW(q.query), true)});
    if (q.season > 0) params.push_back({L"season_number", std::to_wstring(q.season)});
    std::sort(params.begin(), params.end());
    std::wstring s;
    for (const auto& [k, v] : params) s += (s.empty() ? L"?" : L"&") + k + L"=" + v;
    return s;
}

std::vector<SubtitleResult> ParseSubtitleSearch(const json::Value& doc) {
    std::vector<SubtitleResult> out;
    const json::Value* data = doc.Find("data");
    if (!data) return out;
    for (const json::Value& item : data->AsArray()) {
        const json::Value* a = item.Find("attributes");
        if (!a) continue;
        const json::Value* files = a->Find("files");
        if (!files || files->AsArray().empty()) continue;
        const json::Value& file = files->AsArray().front();
        SubtitleResult r;
        r.fileId = file.GetInt("file_id");
        if (r.fileId <= 0) continue;
        r.fileName = file.GetWString("file_name");
        r.release = a->GetWString("release");
        if (r.release.empty()) r.release = r.fileName;
        r.language = a->GetWString("language");
        r.downloads = static_cast<int>(a->GetInt("download_count"));
        r.rating = a->GetNumber("ratings");
        r.votes = static_cast<int>(a->GetInt("votes"));
        r.hearingImpaired = a->GetBool("hearing_impaired");
        r.foreignOnly = a->GetBool("foreign_parts_only");
        r.hashMatch = a->GetBool("moviehash_match");
        r.trusted = a->GetBool("from_trusted");
        r.machine = a->GetBool("machine_translated");
        r.ai = a->GetBool("ai_translated");
        if (const json::Value* f = a->Find("feature_details")) {
            r.title = f->GetWString("movie_name");
            if (r.title.empty()) r.title = f->GetWString("title");
            r.year = static_cast<int>(f->GetInt("year"));
        }
        out.push_back(std::move(r));
    }
    // Hash matches first (the server does that too), the rest keeps the server's order.
    std::stable_sort(out.begin(), out.end(),
                     [](const SubtitleResult& x, const SubtitleResult& y) { return x.hashMatch && !y.hashMatch; });
    return out;
}

SubtitleDownload ParseSubtitleDownload(const json::Value& doc) {
    SubtitleDownload d;
    d.link = doc.GetWString("link");
    d.fileName = doc.GetWString("file_name");
    d.message = doc.GetWString("message");
    d.resetTime = doc.GetWString("reset_time");
    d.resetAt = ParseIsoUtc(doc.GetString("reset_time_utc"));
    if (const json::Value* v = doc.Find("remaining"); v && v->IsNumber()) d.remaining = static_cast<int>(v->AsInt());
    if (const json::Value* v = doc.Find("requests"); v && v->IsNumber()) d.requests = static_cast<int>(v->AsInt());
    return d;
}

SubtitleLogin ParseSubtitleLogin(const json::Value& doc) {
    SubtitleLogin l;
    l.token = doc.GetString("token");
    l.baseUrl = doc.GetWString("base_url");
    if (const json::Value* u = doc.Find("user")) {
        if (const json::Value* v = u->Find("allowed_downloads"); v && v->IsNumber()) l.allowed = static_cast<int>(v->AsInt());
        l.vip = u->GetBool("vip");
    }
    return l;
}

int ParseSubtitleQuota(const json::Value& doc) {
    const json::Value* d = doc.Find("data");
    if (!d) return -1;
    const json::Value* v = d->Find("remaining_downloads");
    return v && v->IsNumber() ? static_cast<int>(v->AsInt()) : -1;
}

std::wstring SubtitleFileName(const std::wstring& videoPath, const std::wstring& language, const std::wstring& fileName) {
    // The server names the file: only a subtitle extension is kept (never .lnk/.hta, "a\b" or an
    // NTFS stream "x.:s"). IsSubtitlePath ignores a dot before a slash.
    std::wstring ext = L"srt";
    if (library::IsSubtitlePath(fileName)) ext = LowerW(fileName.substr(fileName.rfind(L'.') + 1));
    std::wstring lang;
    for (wchar_t c : language)
        if (std::iswalnum(c) || c == L'-') lang.push_back(static_cast<wchar_t>(std::towlower(c)));
    // A stream URL's stem may hold characters a file name can't ("watch?v=a", "host:8080").
    std::wstring name = library::StemOf(videoPath);
    for (wchar_t& c : name)
        if (c < 0x20 || std::wcschr(L"<>:\"/\\|?*", c)) c = L'_';
    while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) name.pop_back();
    if (name.empty()) name = L"subtitles";
    if (!lang.empty()) name += L"." + lang;
    return name + L"." + ext;
}

// --- TMDB -----------------------------------------------------------------------------------

int YearOf(const std::string& isoDate) {
    if (isoDate.size() < 4) return 0;
    const int y = std::atoi(isoDate.substr(0, 4).c_str());
    return y >= 1800 && y <= 2200 ? y : 0;
}

std::vector<MetaMatch> ParseTmdbSearch(const json::Value& doc, bool tv) {
    std::vector<MetaMatch> out;
    const json::Value* results = doc.Find("results");
    if (!results) return out;
    for (const json::Value& r : results->AsArray()) {
        MetaMatch m;
        m.tv = tv;
        m.id = r.GetInt("id");
        if (m.id <= 0) continue;
        m.title = r.GetWString(tv ? "name" : "title");
        if (m.title.empty()) m.title = r.GetWString(tv ? "original_name" : "original_title");
        m.year = YearOf(r.GetString(tv ? "first_air_date" : "release_date"));
        m.overview = r.GetWString("overview");
        m.rating = r.GetNumber("vote_average");
        m.poster = r.GetString("poster_path");
        m.backdrop = r.GetString("backdrop_path");
        out.push_back(std::move(m));
    }
    return out;
}

int PickTmdbMatch(const std::vector<MetaMatch>& results, const VideoGuess& guess) {
    if (results.empty()) return -1;
    if (guess.year > 0) {
        for (size_t i = 0; i < results.size(); ++i)
            if (results[i].year == guess.year) return static_cast<int>(i);
        for (size_t i = 0; i < results.size(); ++i)
            if (results[i].year && std::abs(results[i].year - guess.year) <= 1) return static_cast<int>(i);
    }
    return 0;
}

EpisodeMeta ParseTmdbEpisode(const json::Value& doc) {
    EpisodeMeta e;
    e.name = doc.GetWString("name");
    e.overview = doc.GetWString("overview");
    e.still = doc.GetString("still_path");
    e.rating = doc.GetNumber("vote_average");
    e.year = YearOf(doc.GetString("air_date"));
    return e;
}

int64_t ParseIsoUtc(const std::string& iso) {
    SYSTEMTIME st{};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (sscanf_s(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    st.wYear = static_cast<WORD>(y);
    st.wMonth = static_cast<WORD>(mo);
    st.wDay = static_cast<WORD>(d);
    st.wHour = static_cast<WORD>(h);
    st.wMinute = static_cast<WORD>(mi);
    st.wSecond = static_cast<WORD>(s);
    FILETIME ft{};
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<int64_t>(t / 10000000ull) - 11644473600ll;
}

bool TmdbKeyIsBearer(const std::string& key) {
    return key.rfind("eyJ", 0) == 0 && std::count(key.begin(), key.end(), '.') == 2;
}

// --- scrobbling -----------------------------------------------------------------------------

std::string Md5Hex(std::string_view data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0))) return {};
    unsigned char digest[16];
    const bool ok = BCRYPT_SUCCESS(BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                              static_cast<ULONG>(data.size()), digest, sizeof(digest)));
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

std::string LastfmForm(std::map<std::string, std::string> params, const std::string& apiKey,
                       const std::string& secret) {
    params["api_key"] = apiKey;
    std::string sig;
    for (const auto& [name, value] : params) sig += name + value;  // std::map: in name order
    params["api_sig"] = Md5Hex(sig + secret);
    params["format"] = "json";
    std::string body;
    for (const auto& [name, value] : params) {
        if (!body.empty()) body += '&';
        body += json::ToUtf8(UrlEncode(json::ToWide(name))) + '=' + json::ToUtf8(UrlEncode(json::ToWide(value)));
    }
    return body;
}

LastfmAnswer ParseLastfm(const json::Value& doc) {
    LastfmAnswer a;
    a.error = static_cast<int>(doc.GetInt("error"));
    a.message = doc.GetString("message");
    if (const json::Value* s = doc.Find("session")) {
        a.sessionKey = s->GetString("key");
        a.sessionName = s->GetString("name");
    }
    if (const json::Value* s = doc.Find("scrobbles"))
        if (const json::Value* attr = s->Find("@attr")) {
            a.accepted = static_cast<int>(attr->GetInt("accepted", -1));
            a.ignored = static_cast<int>(attr->GetInt("ignored", -1));
        }
    return a;
}

std::string ListenBrainzBody(const char* listenType, const std::vector<ListenInfo>& listens,
                             const std::string& clientVersion) {
    const bool now = std::strcmp(listenType, "playing_now") == 0;
    json::Value payload = json::Value::MakeArray();
    for (const ListenInfo& l : listens) {
        json::Value meta = json::Value::MakeObject();
        meta.Set("artist_name", json::ToUtf8(l.artist));
        meta.Set("track_name", json::ToUtf8(l.title));
        if (!l.album.empty()) meta.Set("release_name", json::ToUtf8(l.album));
        json::Value info = json::Value::MakeObject();
        if (l.duration > 0) info.Set("duration_ms", static_cast<int64_t>(l.duration) * 1000);
        info.Set("media_player", "Moonbow");
        info.Set("submission_client", "Moonbow");
        info.Set("submission_client_version", clientVersion);
        meta.Set("additional_info", std::move(info));
        json::Value listen = json::Value::MakeObject();
        if (!now) listen.Set("listened_at", l.listenedAt);
        listen.Set("track_metadata", std::move(meta));
        payload.Push(std::move(listen));
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("listen_type", listenType);
    doc.Set("payload", std::move(payload));
    return json::Serialize(doc);
}

}  // namespace net

#include "app/Scrobbler.h"

#include "Player.h"
#include "app/App.h"
#include "app/I18n.h"
#include "app/MainWindow.h"
#include "app/player/NowPlaying.h"
#include "core/Config.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "library/MediaTypes.h"
#include "net/Http.h"
#include "net/OnlineData.h"
#include "net/ServiceKeys.h"
#include "ui/Controls.h"
#include "ui/Host.h"

#include <algorithm>
#include <ctime>
#include <map>
#include <vector>

namespace app {

namespace {

constexpr int64_t kMaxAge = 14 * 86400;  // Last.fm ignores older listens
constexpr size_t kMaxPending = 1000;
constexpr size_t kBatch = 50;            // per request, both services' limit

Config& Cfg() {
    return TheApp().config();
}

int64_t UnixNow() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string Trimmed(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// --automation runs may point the services at a local mock server (as net/Online.cpp does).
std::wstring Endpoint(const char* setting, const wchar_t* real) {
    if (ui::Host::Automation())
        if (std::wstring v = Cfg().GetWString(setting); !v.empty()) return v;
    return real;
}

void Toast(const std::wstring& text, const char* icon = "info") {
    if (MainWindow* w = TheApp().main()) ui::ShowToast(w, text, icon, 5000);
}

// --- listens that wait to be sent: <data>\scrobbles.json ------------------------------------

struct Pending {
    bool loaded = false;
    std::vector<net::ListenInfo> lastfm, listenbrainz;
};

Pending& Store() {
    static Pending p;
    return p;
}

std::vector<net::ListenInfo> ReadList(const json::Value* list) {
    std::vector<net::ListenInfo> out;
    if (!list) return out;
    for (const json::Value& v : list->AsArray()) {
        net::ListenInfo l;
        l.artist = v.GetWString("artist");
        l.title = v.GetWString("title");
        l.album = v.GetWString("album");
        l.duration = static_cast<int>(v.GetInt("duration"));
        l.listenedAt = v.GetInt("at");
        if (!l.artist.empty() && !l.title.empty() && l.listenedAt > 0) out.push_back(std::move(l));
    }
    return out;
}

json::Value WriteList(const std::vector<net::ListenInfo>& list) {
    json::Value out = json::Value::MakeArray();
    for (const net::ListenInfo& l : list) {
        json::Value v = json::Value::MakeObject();
        v.Set("artist", json::ToUtf8(l.artist));
        v.Set("title", json::ToUtf8(l.title));
        if (!l.album.empty()) v.Set("album", json::ToUtf8(l.album));
        if (l.duration > 0) v.Set("duration", l.duration);
        v.Set("at", l.listenedAt);
        out.Push(std::move(v));
    }
    return out;
}

Pending& Loaded() {
    Pending& p = Store();
    if (p.loaded) return p;
    p.loaded = true;
    std::string text;
    if (!json::ReadFileUtf8(DataFilePath(L"scrobbles.json"), &text)) return p;
    if (auto doc = json::Parse(text)) {
        p.lastfm = ReadList(doc->Find("lastfm"));
        p.listenbrainz = ReadList(doc->Find("listenbrainz"));
    }
    return p;
}

// Too old for Last.fm, or more than the store keeps: the oldest go.
void Prune(std::vector<net::ListenInfo>& list, bool ageLimit) {
    const int64_t oldest = UnixNow() - kMaxAge;
    if (ageLimit)
        std::erase_if(list, [oldest](const net::ListenInfo& l) { return l.listenedAt < oldest; });
    if (list.size() > kMaxPending) list.erase(list.begin(), list.end() - kMaxPending);
}

void Save() {
    Pending& p = Loaded();
    const std::wstring path = DataFilePath(L"scrobbles.json");
    if (path.empty()) return;
    if (p.lastfm.empty() && p.listenbrainz.empty()) {
        DeleteFileW(path.c_str());
        return;
    }
    json::Value doc = json::Value::MakeObject();
    doc.Set("version", 1);
    doc.Set("lastfm", WriteList(p.lastfm));
    doc.Set("listenbrainz", WriteList(p.listenbrainz));
    json::WriteFileAtomic(path, json::Serialize(doc, true));
}

// --- Last.fm ---------------------------------------------------------------------------------

const wchar_t kLastfmApi[] = L"https://ws.audioscrobbler.com/2.0/";

bool LastfmOn() {
    return Cfg().GetBool("privacy.lastfmSubmit");
}

bool LastfmKeys() {
    return !net::keys::LastfmKey().empty() && !net::keys::LastfmSecret().empty();
}

std::string LastfmSession() {
    return Cfg().GetString("privacy.lastfmSession");
}

net::Request LastfmPost(std::map<std::string, std::string> params) {
    net::Request r;
    r.method = "POST";
    r.url = Endpoint("privacy.lastfmServer", kLastfmApi);
    r.allowLoopbackHttp = ui::Host::Automation();
    r.body = net::LastfmForm(std::move(params), net::keys::LastfmKey(), net::keys::LastfmSecret());
    r.headers.push_back({L"Content-Type", L"application/x-www-form-urlencoded"});
    r.timeoutMs = 20000;
    return r;
}

void FlushLastfm();

bool g_signingIn = false;

// The user name and password are traded once for a session key; the password is not kept.
void LastfmSignIn() {
    if (!LastfmOn() || g_signingIn || !LastfmSession().empty()) return;
    const std::string user = Trimmed(Cfg().GetString("privacy.lastfmUser"));
    const std::string pass = Cfg().GetString("privacy.lastfmPassword");
    if (user.empty() || pass.empty()) return;
    if (!LastfmKeys()) {
        Toast(Tr(L"This copy of Moonbow was built without a Last.fm key."));
        return;
    }
    g_signingIn = true;
    net::Request r = LastfmPost({{"method", "auth.getMobileSession"}, {"username", user}, {"password", pass}});
    r.user = true;
    net::Send(std::move(r), [user](const net::Response& resp) {
        g_signingIn = false;
        const auto doc = json::Parse(resp.body);
        const net::LastfmAnswer a = doc ? net::ParseLastfm(*doc) : net::LastfmAnswer{};
        if (!a.sessionKey.empty()) {
            Cfg().SetString("privacy.lastfmSession", a.sessionKey);
            Cfg().SetString("privacy.lastfmPassword", "");  // traded for the session, not kept
            Toast(Fmt(Tr(L"Signed in to Last.fm as {0}"), {json::ToWide(a.sessionName.empty() ? user : a.sessionName)}),
                  "check_selected");
            FlushLastfm();
            return;
        }
        Log("lastfm: sign-in failed, status " + std::to_string(resp.status) + ", error " + std::to_string(a.error));
        if (a.error == 4)  // authentication failed
            Toast(Tr(L"Last.fm did not accept the user name or password."));
        else if (resp.error != net::Error::None)
            Toast(Tr(L"No connection to Last.fm. Moonbow tries to sign in again at the next start."));
        else
            Toast(Fmt(Tr(L"Last.fm is not available right now (error {0})."), {std::to_wstring(a.error ? a.error : resp.status)}));
    });
}

// Last.fm said the session is over (revoked in the account's settings): sign in again.
void LastfmSignedOut() {
    Cfg().SetString("privacy.lastfmSession", "");
    Toast(Tr(L"Last.fm signed Moonbow out. Enter your password again in Settings › Privacy › Online services."));
}

bool g_lastfmBusy = false;

void FlushLastfm() {
    Pending& p = Loaded();
    Prune(p.lastfm, true);
    if (!LastfmOn() || !LastfmKeys() || LastfmSession().empty() || p.lastfm.empty() || g_lastfmBusy) return;
    const size_t n = std::min(p.lastfm.size(), kBatch);
    std::map<std::string, std::string> params{{"method", "track.scrobble"}, {"sk", LastfmSession()}};
    for (size_t i = 0; i < n; ++i) {
        const net::ListenInfo& l = p.lastfm[i];
        const std::string at = "[" + std::to_string(i) + "]";
        params["artist" + at] = json::ToUtf8(l.artist);
        params["track" + at] = json::ToUtf8(l.title);
        params["timestamp" + at] = std::to_string(l.listenedAt);
        if (!l.album.empty()) params["album" + at] = json::ToUtf8(l.album);
        if (l.duration > 0) params["duration" + at] = std::to_string(l.duration);
    }
    g_lastfmBusy = true;
    net::Send(LastfmPost(std::move(params)), [n](const net::Response& resp) {
        g_lastfmBusy = false;
        const auto doc = json::Parse(resp.body);
        const net::LastfmAnswer a = doc ? net::ParseLastfm(*doc) : net::LastfmAnswer{};
        Pending& p = Loaded();
        if (resp.Ok() && a.error == 0 && a.accepted >= 0) {
            // Accepted or ignored (too old, a filtered name): either way done with them.
            p.lastfm.erase(p.lastfm.begin(), p.lastfm.begin() + static_cast<std::ptrdiff_t>(std::min(n, p.lastfm.size())));
            Save();
            if (!p.lastfm.empty()) FlushLastfm();
            return;
        }
        Log("lastfm: scrobble failed, status " + std::to_string(resp.status) + ", error " + std::to_string(a.error));
        if (a.error == 9) LastfmSignedOut();
        // Anything else (offline, 11/16 service down, 29 rate limit): they wait for the next one.
    });
}

void LastfmNowPlaying(const net::ListenInfo& l) {
    if (!LastfmOn() || !LastfmKeys() || LastfmSession().empty()) return;
    std::map<std::string, std::string> params{{"method", "track.updateNowPlaying"},
                                              {"artist", json::ToUtf8(l.artist)},
                                              {"track", json::ToUtf8(l.title)},
                                              {"sk", LastfmSession()}};
    if (!l.album.empty()) params["album"] = json::ToUtf8(l.album);
    if (l.duration > 0) params["duration"] = std::to_string(l.duration);
    net::Send(LastfmPost(std::move(params)), [](const net::Response& resp) {
        const auto doc = json::Parse(resp.body);
        if (doc && net::ParseLastfm(*doc).error == 9) LastfmSignedOut();
    });
}

// --- ListenBrainz ----------------------------------------------------------------------------

const wchar_t kListenBrainzApi[] = L"https://api.listenbrainz.org/1/submit-listens";

std::string ListenBrainzToken() {
    return Trimmed(Cfg().GetString("privacy.listenbrainzToken"));
}

bool ListenBrainzOn() {
    return Cfg().GetBool("privacy.listenbrainzSubmit") && !ListenBrainzToken().empty();
}

net::Request ListenBrainzPost(const char* type, const std::vector<net::ListenInfo>& listens) {
    net::Request r;
    r.method = "POST";
    r.url = Endpoint("privacy.listenbrainzServer", kListenBrainzApi);
    r.allowLoopbackHttp = ui::Host::Automation();
    r.body = net::ListenBrainzBody(type, listens, json::ToUtf8(net::AppVersion()));
    r.headers.push_back({L"Authorization", L"Token " + json::ToWide(ListenBrainzToken())});
    r.headers.push_back({L"Content-Type", L"application/json"});
    r.timeoutMs = 20000;
    return r;
}

bool g_listenBrainzBusy = false;
bool g_listenBrainzRefusedSaid = false;

void ListenBrainzRefused() {
    if (g_listenBrainzRefusedSaid) return;
    g_listenBrainzRefusedSaid = true;
    Toast(Tr(L"ListenBrainz did not accept the user token. Check it in Settings › Privacy › Online services."));
}

void FlushListenBrainz() {
    Pending& p = Loaded();
    Prune(p.listenbrainz, false);
    if (!ListenBrainzOn() || p.listenbrainz.empty() || g_listenBrainzBusy) return;
    const size_t n = std::min(p.listenbrainz.size(), kBatch);
    const std::vector<net::ListenInfo> batch(p.listenbrainz.begin(), p.listenbrainz.begin() + static_cast<std::ptrdiff_t>(n));
    g_listenBrainzBusy = true;
    net::Send(ListenBrainzPost(n == 1 ? "single" : "import", batch), [n](const net::Response& resp) {
        g_listenBrainzBusy = false;
        Pending& p = Loaded();
        if (resp.Ok()) {
            p.listenbrainz.erase(p.listenbrainz.begin(),
                                 p.listenbrainz.begin() + static_cast<std::ptrdiff_t>(std::min(n, p.listenbrainz.size())));
            Save();
            if (!p.listenbrainz.empty()) FlushListenBrainz();
            return;
        }
        Log("listenbrainz: submit failed, status " + std::to_string(resp.status));
        if (resp.error == net::Error::None && resp.status == 401) ListenBrainzRefused();
    });
}

void ListenBrainzNowPlaying(const net::ListenInfo& l) {
    if (!ListenBrainzOn()) return;
    net::Send(ListenBrainzPost("playing_now", {l}), [](const net::Response& resp) {
        if (resp.error == net::Error::None && resp.status == 401) ListenBrainzRefused();
    });
}

// --- the song that plays ---------------------------------------------------------------------

struct Play {
    uint64_t serial = 0;
    bool music = false;      // a local audio file (streams and videos are not scrobbled)
    net::ListenInfo listen;
    double listened = 0;     // seconds actually heard (seeks left out)
    double lastPosition = 0;
    bool announced = false, counted = false;
};

Play& Current() {
    static Play p;
    return p;
}

void OnPlayerChanged(unsigned changes) {
    if (!LastfmOn() && !ListenBrainzOn()) return;
    const PlayerState& s = TheApp().player().State();
    if (!s.fileLoaded || s.path.empty()) return;
    Play& p = Current();
    if (s.loadSerial != p.serial) {
        p = Play{};
        p.serial = s.loadSerial;
        p.music = library::KindOf(s.path) == library::MediaKind::Audio && s.path.find(L"://") == std::wstring::npos;
        p.listen.listenedAt = UnixNow();
        p.lastPosition = s.position;
    }
    if (!p.music) return;
    // The library's tags arrive a moment after the file loads: take them once both are there.
    if (p.listen.title.empty()) {
        const PlayingTags& t = CurrentTags();
        if (t.artist.empty() || t.title.empty()) return;
        p.listen.artist = t.artist;
        p.listen.title = t.title;
        p.listen.album = t.album;
    }
    if (p.listen.duration <= 0 && s.duration > 0) p.listen.duration = static_cast<int>(s.duration + 0.5);
    if (p.listen.duration > 0 && p.listen.duration < 30) return;  // too short to count
    if (changes & kChangeTime) {
        const double step = s.position - p.lastPosition;
        if (!s.paused && step > 0 && step < 3) p.listened += step;
        p.lastPosition = s.position;
    }
    if (!p.announced && !s.paused && p.listen.duration > 0) {
        p.announced = true;
        LastfmNowPlaying(p.listen);
        ListenBrainzNowPlaying(p.listen);
    }
    if (!p.counted && p.listen.duration > 0 && p.listened >= std::min(p.listen.duration / 2.0, 240.0)) {
        p.counted = true;
        Pending& store = Loaded();
        if (LastfmOn()) store.lastfm.push_back(p.listen);
        if (ListenBrainzOn()) store.listenbrainz.push_back(p.listen);
        Save();
        FlushLastfm();
        FlushListenBrainz();
    }
}

}  // namespace

void InitScrobbler() {
    TheApp().playerChanged.Connect(OnPlayerChanged);
    Config& c = Cfg();
    c.Subscribe("privacy.lastfmSubmit", [](const std::string&) {
        LastfmSignIn();
        FlushLastfm();
    });
    // Another account: the old session is not its.
    c.Subscribe("privacy.lastfmUser", [](const std::string&) {
        if (!LastfmSession().empty()) Cfg().SetString("privacy.lastfmSession", "");
        LastfmSignIn();
    });
    c.Subscribe("privacy.lastfmPassword", [](const std::string&) { LastfmSignIn(); });
    c.Subscribe("privacy.listenbrainzSubmit", [](const std::string&) { FlushListenBrainz(); });
    c.Subscribe("privacy.listenbrainzToken", [](const std::string&) {
        g_listenBrainzRefusedSaid = false;
        FlushListenBrainz();
    });
    // What the last session could not send goes out a little after the start, off its busy part.
    if (MainWindow* w = TheApp().main())
        w->StartTimer(30000, [] {
            if (!LastfmOn() && !ListenBrainzOn()) return;
            LastfmSignIn();
            FlushLastfm();
            FlushListenBrainz();
        });
}

}  // namespace app

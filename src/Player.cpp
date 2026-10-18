#include "Player.h"

#include "Trace.h"
#include "Util.h"
#include "core/Paths.h"
#include "core/SettingsSchema.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace {

enum PropId : uint64_t {
    kPropTimePos = 1,
    kPropDuration,
    kPropPause,
    kPropVolume,
    kPropMute,
    kPropTitle,
    kPropPath,
    kPropTrackList,
    kPropIdle,
    kPropWidth,
    kPropHeight,
    kPropSpeed,
    kPropEof,
    kPropSeekable,
    kPropPausedForCache,
    kPropBuffering,
    kPropCacheTime,
    kPropChapter,
    kPropChapterList,
    kPropMetadata,
    kPropHwdec,
    kPropGamma,
    kPropVolumeMax,
};

const mpv_node* MapGet(const mpv_node& map, const char* key) {
    if (map.format != MPV_FORMAT_NODE_MAP) return nullptr;
    for (int i = 0; i < map.u.list->num; ++i)
        if (_stricmp(map.u.list->keys[i], key) == 0) return &map.u.list->values[i];
    return nullptr;
}

std::wstring NodeString(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_STRING ? Utf8ToWide(n->u.string) : std::wstring();
}

int64_t NodeInt(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_INT64 ? n->u.int64 : 0;
}

double NodeDouble(const mpv_node* n) {
    if (!n) return 0;
    if (n->format == MPV_FORMAT_DOUBLE) return n->u.double_;
    if (n->format == MPV_FORMAT_INT64) return static_cast<double>(n->u.int64);
    return 0;
}

bool NodeFlag(const mpv_node* n) {
    return n && n->format == MPV_FORMAT_FLAG && n->u.flag;
}

}  // namespace

std::wstring Track::Label() const {
    std::wstring label = Name();
    if (!codec.empty()) label += L" · " + codec;
    return label;
}

bool Track::TitleFromFileName() const {
    // mpv titles a sidecar file with the part of its name that differs from the video's: for
    // "Movie.srt" next to "Movie.mp4" that is just "srt".
    if (!external || file.empty()) return false;
    if (title.empty()) return true;
    const std::wstring name = FileNameOf(file);
    return name.size() > title.size() && name.compare(name.size() - title.size(), title.size(), title) == 0;
}

std::wstring Track::Name() const {
    std::wstring label;
    if (TitleFromFileName())
        label = FileNameOf(file);
    else if (!title.empty())
        label = title;
    else if (!lang.empty())
        label = lang;
    else
        label = L"#" + std::to_wstring(id);
    if ((!title.empty() || TitleFromFileName()) && !lang.empty()) label += L" (" + lang + L")";
    return label;
}

Player::~Player() {
    Shutdown();
}

void Player::StartAsync(HWND notifyWnd, HWND videoWnd) {
    notify_ = notifyWnd;
    int64_t wid = static_cast<int64_t>(reinterpret_cast<intptr_t>(videoWnd));
    init_ = std::thread([this, wid] {
        bool ok = InitOnWorker(wid);
        PostMessageW(notify_, WM_APP_MPV_READY, ok ? 1 : 0, 0);
    });
}

bool Player::InitOnWorker(int64_t wid) {
    Trace("mpv load dll");
    if (!api_.Load(ExeDir() + L"libmpv-2.dll", &error_)) return false;
    Trace("mpv dll loaded");

    mpv_ = api_.mpv_create();
    if (!mpv_) {
        error_ = L"mpv_create failed";
        return false;
    }

    auto opt = [this](const char* name, const std::string& value) {
        api_.mpv_set_option_string(mpv_, name, value.c_str());
    };

    api_.mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &wid);
    opt("vo", "gpu-next");
    // Defaults for the settings below (performance.hwdec / decodeThreads); see SettingsSchema.h.
    opt("hwdec", settings::kAutoHwdec);
    opt("vd-lavc-threads", std::to_string(settings::AutoDecodeThreads()));
    opt("keep-open", "yes");
    opt("idle", "yes");
    opt("osc", "no");
    opt("osd-level", "0");
    opt("input-default-bindings", "no");
    opt("input-vo-keyboard", "no");
    opt("input-cursor", "no");
    opt("cursor-autohide", "no");
    opt("load-scripts", "no");
    opt("ytdl", "no");
    opt("terminal", "no");
    opt("volume-max", "150");
    // Lumen writes watch-later itself (SaveWatchLater / Shutdown): mpv's own save also fired
    // when a finished file was replaced by the next one, so finished files resumed at the end.
    opt("save-position-on-quit", "no");
    // Per file only the position and the chosen tracks; delays, mute and the
    // A-B loop are for the current file, not the next one.
    opt("watch-later-options", "start,aid,sid,secondary-sid");
    opt("reset-on-next-file",
        "audio-delay,sub-delay,ab-loop-a,ab-loop-b,video-aspect-override,video-zoom,video-pan-x,video-pan-y,video-rotate");
    opt("audio-display", "no");  // cover art is shown by our music view
    opt("background-color", "#000000");

    std::wstring appData = DataDir();
    if (!appData.empty()) {
        opt("config-dir", WideToUtf8(appData));
        opt("config", "yes");
        std::wstring watchLater = appData + L"\\watch_later";
        CreateDirectoryW(watchLater.c_str(), nullptr);
        opt("watch-later-dir", WideToUtf8(watchLater));
    }
    std::wstring shots = KnownFolderPath(FOLDERID_Pictures, L"Lumen");
    if (!shots.empty()) opt("screenshot-dir", WideToUtf8(shots));

    for (const auto& [name, value] : startOptions_) opt(name.c_str(), value);

    Trace("mpv initialize");
    int err = api_.mpv_initialize(mpv_);
    Trace("mpv initialized");
    if (err < 0) {
        error_ = L"mpv_initialize: " + Utf8ToWide(api_.mpv_error_string(err));
        return false;
    }
    return true;
}

bool Player::FinishStart(bool ok) {
    if (init_.joinable()) init_.join();
    if (!ok) {
        if (mpv_) {
            api_.mpv_terminate_destroy(mpv_);
            mpv_ = nullptr;
        }
        return false;
    }

    struct {
        PropId id;
        const char* name;
        mpv_format format;
    } const props[] = {
        {kPropTimePos, "time-pos", MPV_FORMAT_DOUBLE},
        {kPropDuration, "duration", MPV_FORMAT_DOUBLE},
        {kPropPause, "pause", MPV_FORMAT_FLAG},
        {kPropVolume, "volume", MPV_FORMAT_DOUBLE},
        {kPropMute, "mute", MPV_FORMAT_FLAG},
        {kPropTitle, "media-title", MPV_FORMAT_STRING},
        {kPropPath, "path", MPV_FORMAT_STRING},
        {kPropTrackList, "track-list", MPV_FORMAT_NODE},
        {kPropIdle, "idle-active", MPV_FORMAT_FLAG},
        {kPropWidth, "dwidth", MPV_FORMAT_INT64},
        {kPropHeight, "dheight", MPV_FORMAT_INT64},
        {kPropSpeed, "speed", MPV_FORMAT_DOUBLE},
        {kPropEof, "eof-reached", MPV_FORMAT_FLAG},
        {kPropSeekable, "seekable", MPV_FORMAT_FLAG},
        {kPropPausedForCache, "paused-for-cache", MPV_FORMAT_FLAG},
        {kPropBuffering, "cache-buffering-state", MPV_FORMAT_DOUBLE},
        {kPropCacheTime, "demuxer-cache-time", MPV_FORMAT_DOUBLE},
        {kPropChapter, "chapter", MPV_FORMAT_INT64},
        {kPropChapterList, "chapter-list", MPV_FORMAT_NODE},
        {kPropMetadata, "metadata", MPV_FORMAT_NODE},
        {kPropHwdec, "hwdec-current", MPV_FORMAT_STRING},
        {kPropGamma, "video-params/gamma", MPV_FORMAT_STRING},
        {kPropVolumeMax, "volume-max", MPV_FORMAT_DOUBLE},
    };
    for (const auto& p : props) api_.mpv_observe_property(mpv_, p.id, p.name, p.format);
    api_.mpv_set_wakeup_callback(mpv_, &Player::Wakeup, this);

    state_.ready = true;
    return true;
}

void Player::Wakeup(void* ctx) {
    auto* self = static_cast<Player*>(ctx);
    if (!self->wakePending_.exchange(true)) PostMessageW(self->notify_, WM_APP_MPV_WAKEUP, 0, 0);
}

unsigned Player::ProcessEvents() {
    wakePending_ = false;
    if (!mpv_) return 0;
    unsigned changes = 0;
    for (;;) {
        mpv_event* ev = api_.mpv_wait_event(mpv_, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        switch (ev->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto* prop = static_cast<mpv_event_property*>(ev->data);
                if (ev->reply_userdata >= 1000) {
                    auto it = observers_.find(ev->reply_userdata);
                    if (it != observers_.end()) it->second(*prop);
                } else {
                    changes |= OnProperty(prop, ev->reply_userdata);
                }
                break;
            }
            case MPV_EVENT_START_FILE:
                // Forget the previous file's size, so window fitting waits for this one's.
                state_.videoW = state_.videoH = 0;
                changes |= kChangeVideoSize;
                break;
            case MPV_EVENT_FILE_LOADED:
                Trace("file loaded");
                state_.fileLoaded = true;
                ++state_.loadSerial;
                state_.eof = false;
                state_.lastError.clear();
                changes |= kChangeFile | kChangeState;
                break;
            case MPV_EVENT_END_FILE: {
                auto* end = static_cast<mpv_event_end_file*>(ev->data);
                if (end->reason == MPV_END_FILE_REASON_ERROR) {
                    state_.lastError = Utf8ToWide(api_.mpv_error_string(end->error));
                    changes |= kChangeError;
                }
                break;
            }
            default:
                break;
        }
    }
    return changes;
}

unsigned Player::OnProperty(const mpv_event_property* prop, uint64_t id) {
    const bool has = prop->format != MPV_FORMAT_NONE && prop->data;
    auto dbl = [&] { return has ? *static_cast<double*>(prop->data) : 0.0; };
    auto flag = [&] { return has && *static_cast<int*>(prop->data) != 0; };
    auto str = [&] { return has ? Utf8ToWide(*static_cast<char**>(prop->data)) : std::wstring(); };
    auto i64 = [&] { return has ? *static_cast<int64_t*>(prop->data) : int64_t{0}; };
    auto node = [&]() -> const mpv_node* {
        return has && prop->format == MPV_FORMAT_NODE ? static_cast<mpv_node*>(prop->data) : nullptr;
    };

    switch (id) {
        case kPropTimePos: state_.position = dbl(); return kChangeTime;
        case kPropDuration: state_.duration = dbl(); return kChangeTime;
        case kPropPause: state_.paused = flag(); return kChangeState;
        case kPropVolume: state_.volume = has ? dbl() : 100.0; return kChangeVolume;
        case kPropMute: state_.mute = flag(); return kChangeVolume;
        case kPropTitle: state_.title = str(); return kChangeMeta;
        case kPropPath: state_.path = str(); return kChangeFile;
        case kPropIdle:
            state_.idle = flag();
            if (state_.idle) {
                state_.fileLoaded = false;
                state_.position = state_.duration = 0;
                state_.chapters.clear();
            }
            return kChangeState;
        case kPropWidth: state_.videoW = i64(); return kChangeVideoSize;
        case kPropHeight: state_.videoH = i64(); return kChangeVideoSize;
        case kPropSpeed: state_.speed = has ? dbl() : 1.0; return kChangeState;
        case kPropEof: {
            const bool was = state_.eof;
            state_.eof = flag();
            return kChangeState | (state_.eof && !was ? kChangeEnd : 0u);
        }
        case kPropSeekable: state_.seekable = !has || flag(); return kChangeState;
        case kPropPausedForCache: state_.buffering = flag(); return kChangeState;
        case kPropBuffering: state_.bufferPercent = dbl(); return kChangeState;
        case kPropCacheTime: state_.cacheTime = dbl(); return kChangeTime;
        case kPropChapter: state_.chapter = has ? i64() : -1; return kChangeChapters;
        case kPropHwdec: state_.hwdec = str(); return kChangeState;
        case kPropVolumeMax: volumeMax_ = has ? dbl() : 100.0; return 0;
        case kPropGamma: {
            const std::wstring g = str();
            state_.hdr = g == L"pq" || g == L"hlg";
            return kChangeState;
        }
        case kPropChapterList: {
            state_.chapters.clear();
            if (const mpv_node* n = node(); n && n->format == MPV_FORMAT_NODE_ARRAY)
                for (int i = 0; i < n->u.list->num; ++i) {
                    const mpv_node& c = n->u.list->values[i];
                    state_.chapters.push_back({NodeString(MapGet(c, "title")), NodeDouble(MapGet(c, "time"))});
                }
            return kChangeChapters;
        }
        case kPropMetadata: {
            state_.artist.clear();
            state_.album.clear();
            if (const mpv_node* n = node()) {
                state_.artist = NodeString(MapGet(*n, "artist"));
                if (state_.artist.empty()) state_.artist = NodeString(MapGet(*n, "album_artist"));
                state_.album = NodeString(MapGet(*n, "album"));
            }
            return kChangeMeta;
        }
        case kPropTrackList: {
            state_.tracks.clear();
            state_.hasVideo = false;
            if (const mpv_node* n = node(); n && n->format == MPV_FORMAT_NODE_ARRAY) {
                for (int i = 0; i < n->u.list->num; ++i) {
                    const mpv_node& t = n->u.list->values[i];
                    Track track;
                    track.id = NodeInt(MapGet(t, "id"));
                    track.type = NodeString(MapGet(t, "type"));
                    track.title = NodeString(MapGet(t, "title"));
                    track.lang = NodeString(MapGet(t, "lang"));
                    track.codec = NodeString(MapGet(t, "codec"));
                    track.selected = NodeFlag(MapGet(t, "selected"));
                    track.external = NodeFlag(MapGet(t, "external"));
                    track.file = NodeString(MapGet(t, "external-filename"));
                    if (track.type == L"video" && !NodeFlag(MapGet(t, "albumart"))) state_.hasVideo = true;
                    state_.tracks.push_back(std::move(track));
                }
            }
            return kChangeTracks;
        }
        default:
            return 0;
    }
}

// Commands from the UI thread are async: mpv's video window is a child of our
// window, so a synchronous call that waits on mpv's VO thread could deadlock.
void Player::Cmd(std::initializer_list<const char*> args, bool async) {
    if (!mpv_) return;
    const char* argv[10] = {};
    size_t n = 0;
    for (const char* a : args)
        if (n < std::size(argv) - 1) argv[n++] = a;
    if (async)
        api_.mpv_command_async(mpv_, 0, argv);
    else
        api_.mpv_command(mpv_, argv);
}

void Player::Command(const std::vector<std::string>& args) {
    if (!mpv_ || args.empty()) return;
    std::vector<const char*> argv;
    for (const std::string& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    api_.mpv_command_async(mpv_, 0, argv.data());
}

void Player::SetDouble(const char* name, double value) {
    if (mpv_) api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_DOUBLE, &value);
}

void Player::SetString(const char* name, const std::string& value) {
    if (!mpv_) return;
    const char* v = value.c_str();
    api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_STRING, &v);
}

void Player::SetProperty(const char* name, double value) {
    SetDouble(name, value);
}

void Player::SetProperty(const char* name, bool value) {
    if (!mpv_) return;
    int flag = value ? 1 : 0;
    api_.mpv_set_property_async(mpv_, 0, name, MPV_FORMAT_FLAG, &flag);
}

void Player::SetProperty(const char* name, const std::string& value) {
    SetString(name, value);
}

std::string Player::GetPropertyString(const char* name) {
    if (!mpv_) return {};
    char* v = nullptr;
    if (api_.mpv_get_property(mpv_, name, MPV_FORMAT_STRING, &v) < 0 || !v) return {};
    std::string out = v;
    api_.mpv_free(v);
    return out;
}

double Player::GetPropertyDouble(const char* name, double fallback) {
    double v = fallback;
    if (!mpv_ || api_.mpv_get_property(mpv_, name, MPV_FORMAT_DOUBLE, &v) < 0) return fallback;
    return v;
}

uint64_t Player::Observe(const char* name, mpv_format format, PropertyCallback cb) {
    if (!mpv_) return 0;
    const uint64_t id = nextObserver_++;
    observers_[id] = std::move(cb);
    api_.mpv_observe_property(mpv_, id, name, format);
    return id;
}

void Player::Unobserve(uint64_t id) {
    if (!mpv_ || !id) return;
    api_.mpv_unobserve_property(mpv_, id);
    observers_.erase(id);
}

void Player::Open(const std::wstring& path, double start) {
    if (state_.fileLoaded) SaveWatchLater();
    const std::string utf8 = WideToUtf8(path);
    if (start > 0) {
        const std::string opts = "start=" + std::to_string(start);
        Cmd({"loadfile", utf8.c_str(), "replace", "-1", opts.c_str()});
    } else {
        Cmd({"loadfile", utf8.c_str(), "replace"});
    }
}

void Player::Stop() {
    if (state_.fileLoaded) SaveWatchLater();
    Cmd({"stop"});
}

void Player::TogglePause() {
    if (!state_.fileLoaded) return;
    if (state_.eof && state_.paused) Cmd({"seek", "0", "absolute"});
    Cmd({"cycle", "pause"});
}

void Player::SetPause(bool paused) {
    SetProperty("pause", paused);
}

void Player::SeekAbsolute(double seconds, bool exact) {
    if (!state_.fileLoaded) return;
    std::string pos = std::to_string(seconds < 0 ? 0 : seconds);
    Cmd({"seek", pos.c_str(), exact ? "absolute+exact" : "absolute+keyframes"});
}

void Player::SeekRelative(double seconds) {
    if (!state_.fileLoaded) return;
    std::string delta = std::to_string(seconds);
    Cmd({"seek", delta.c_str(), "relative+exact"});
}

void Player::SeekChapter(int delta) {
    if (!state_.fileLoaded || state_.chapters.empty()) return;
    std::string d = std::to_string(delta);
    Cmd({"add", "chapter", d.c_str()});
}

void Player::FrameStep(bool back) {
    if (!state_.fileLoaded) return;
    Cmd({back ? "frame-back-step" : "frame-step"});
}

void Player::SetVolume(double volume) {
    if (volume < 0) volume = 0;
    if (volume > volumeMax_) volume = volumeMax_;  // audio.maxVolume
    state_.volume = volume;  // optimistic, so repeated wheel steps accumulate
    SetDouble("volume", volume);
}

void Player::SetMute(bool mute) {
    state_.mute = mute;
    SetProperty("mute", mute);
}

void Player::ToggleMute() {
    SetMute(!state_.mute);
}

void Player::SetSpeed(double speed) {
    if (speed < 0.1) speed = 0.1;
    if (speed > 8.0) speed = 8.0;
    state_.speed = speed;
    SetDouble("speed", speed);
}

void Player::SetTrack(const char* prop, int64_t id) {
    SetString(prop, id > 0 ? std::to_string(id) : "no");
}

void Player::CycleTrack(const char* prop) {
    Cmd({"cycle", prop});
}

void Player::AddSubtitle(const std::wstring& path) {
    const std::string p = WideToUtf8(path);
    Cmd({"sub-add", p.c_str(), "select"});
}

void Player::AddAudio(const std::wstring& path) {
    const std::string p = WideToUtf8(path);
    Cmd({"audio-add", p.c_str(), "select"});
}

void Player::Screenshot(const std::wstring& file, bool withSubtitles) {
    if (!state_.fileLoaded) return;
    const char* mode = withSubtitles ? "subtitles" : "video";
    if (file.empty()) {
        Cmd({"screenshot", mode});
    } else {
        const std::string f = WideToUtf8(file);
        Cmd({"screenshot-to-file", f.c_str(), mode});
    }
}

void Player::SetRecording(const std::wstring& file) {
    SetString("stream-record", WideToUtf8(file));
}

// A file that was (nearly) finished starts from the beginning next time, the
// way mpv and MPC-HC treat it.
bool Player::NearEnd() const {
    if (state_.eof) return true;
    if (state_.duration <= 0) return false;
    return state_.position >= state_.duration - std::max(5.0, state_.duration * 0.05);
}

void Player::SaveWatchLater() {
    if (!state_.fileLoaded) return;
    if (NearEnd())
        Cmd({"delete-watch-later-config"});
    else if (rememberPosition)
        Cmd({"write-watch-later-config"});
}

void Player::Shutdown() {
    if (init_.joinable()) init_.join();
    if (mpv_) {
        // Runs off the UI thread (see App shutdown), so a blocking call is fine here.
        if (state_.fileLoaded) {
            if (NearEnd())
                Cmd({"delete-watch-later-config"}, false);
            else if (rememberPosition)
                Cmd({"write-watch-later-config"}, false);
        }
        api_.mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
        api_.mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    state_.ready = false;
}
